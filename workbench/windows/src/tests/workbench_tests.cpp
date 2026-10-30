#include "../app.h"
#include "../editor/commands.h"
#include "../editor/text_format.h"

namespace {

std::string editorText(HWND editor) {
    const auto length = static_cast<std::size_t>(SendMessageW(editor, SCI_GETLENGTH, 0, 0));
    std::string text(length + 1, 0);
    SendMessageW(editor, SCI_GETTEXT, length + 1, reinterpret_cast<LPARAM>(text.data()));
    text.resize(length);
    return text;
}

} // namespace

int App::workbenchTest(const fs::path& report) {
    std::string previousSession;
    try { previousSession = readFile(dataDir / L"session.json"); } catch (...) {}
    json results = json::array();
    json performance = json::object();
    const auto check = [&](std::string name, bool passed) {
        results.push_back({{"name", std::move(name)}, {"passed", passed}});
    };
    const auto pump = [&](const std::function<bool()>& ready, int milliseconds = 8000) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(milliseconds);
        do {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message != WM_QUIT) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            if (ready()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (Clock::now() < deadline);
        return false;
    };

    try {
        using orbit::NavigationHistory;
        using orbit::NavigationLocation;
        NavigationHistory history;
        history.visit({"a.py", {10, 10, 2, 0}});
        history.visit({"b.py", {20, 20, 4, 0}});
        const auto back = history.back({"b.py", {30, 30, 5, 0}});
        check("navigation_back_restores_precise_location", back && back->path == "a.py" && back->view.caret == 10);
        const auto forward = history.forward(*back);
        check("navigation_forward_remembers_departure", forward && forward->path == "b.py" && forward->view.caret == 30);
        history.back(*forward);
        history.visit({"c.py", {}});
        check("new_navigation_discards_forward_branch", !history.canGoForward() && history.size() == 2);
        for (int i = 0; i < 200; ++i) history.visit({std::to_string(i) + ".py", {}});
        check("history_and_recent_memory_are_bounded", history.size() == NavigationHistory::historyLimit && history.recent().size() == NavigationHistory::recentLimit);
        history.touchRecent("recent.py");
        history.touchRecent("recent.py");
        check("recent_paths_are_deduplicated_and_mru_ordered", history.recent().front() == "recent.py" && std::count(history.recent().begin(), history.recent().end(), "recent.py") == 1);
        history.rememberClosed({"recent.py", {42, 40, 8, 12}});
        history.renamePath("recent.py", "renamed.py");
        const auto reopened = history.takeClosed();
        check("rename_updates_recent_and_closed_locations", reopened && reopened->path == "renamed.py" && history.recent().front() == "renamed.py");
        history.forgetPath("renamed.py");
        check("deleted_paths_are_removed_from_navigation", std::find(history.recent().begin(), history.recent().end(), "renamed.py") == history.recent().end());

        const auto outline = orbit::analyzeDocument("class Worker:\n    async def run(self):\n        return '你好'\n\ndef finish():\n    pass\n", "py");
        check("python_outline_tracks_classes_async_and_lines", outline.symbols.size() == 3 && outline.symbols[1].name == "run" && outline.symbols[1].line == 1 && outline.symbols[1].depth == 1);
        const auto markdown = orbit::analyzeDocument("# Title\n\n## Details\n", "md");
        check("markdown_outline_tracks_headings", markdown.symbols.size() == 2 && markdown.symbols[1].name == "Details");
        const auto javascript = orbit::analyzeDocument("export async function loadData() {}\nclass Store {}\n", "js");
        check("javascript_outline_tracks_export_async", javascript.symbols.size() == 2 && javascript.symbols[0].name == "loadData");
        check("line_endings_detect_lf_crlf_cr_and_empty", orbit::detectLineEnding("a\nb\n") == orbit::LineEnding::Lf &&
            orbit::detectLineEnding("a\r\nb\r\n") == orbit::LineEnding::CrLf &&
            orbit::detectLineEnding("a\rb\r") == orbit::LineEnding::Cr &&
            orbit::detectLineEnding("") == orbit::LineEnding::Lf);

        check("initial_project_ready", pump([&] { return !active.empty() && documents.contains(active) && documents.at(active).loaded; }));
        const auto fixture = exeDir / (L"workbench-fixture-" + std::to_wstring(GetCurrentProcessId()));
        fs::create_directories(fixture);
        fs::copy(exeDir / L"sample-project", fixture, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        scanProject(fixture);
        check("isolated_project_loaded", pump([&] { return root == fixture && documents.contains("src/main.py") && documents.at("src/main.py").loaded; }));
        const auto open = [&](const std::string& path) {
            openFile(path);
            return pump([&] { return active == path && documents.at(path).loaded && documents.at(path).pointer == sci(SCI_GETDOCPOINTER); });
        };
        check("open_second_native_document", open("src/utils.py"));
        const auto originalTabIndex = std::find(tabs.begin(), tabs.end(), "src/utils.py") - tabs.begin();
        closeTab("src/utils.py");
        reopenClosedTab();
        const bool tabReopened = pump([&] { return active == "src/utils.py" && documents.at(active).loaded; });
        check("reopen_restores_original_tab_order", tabReopened && tabs[originalTabIndex] == "src/utils.py");
        gotoLine(7);
        const auto savedLocation = captureEditorLocation();
        open("src/main.py");
        navigateHistory(false);
        check("back_across_tabs_restores_caret", pump([&] { return active == "src/utils.py" && sci(SCI_GETCURRENTPOS) == savedLocation.view.caret; }));
        navigateHistory(true);
        check("forward_across_tabs_restores_target", pump([&] { return active == "src/main.py"; }));
        const auto documentPointer = documents.at(active).pointer;
        open("README.md");
        open("src/main.py");
        check("cross_language_switch_restores_python_colors", sci(SCI_STYLEGETFORE, SCE_P_WORD) == rgb(settings.light ? 0x925b91 : 0xcd96c8));
        check("open_tabs_keep_existing_document", documents.at(active).pointer == documentPointer);

        newFile("src/editing_workflow.py");
        const auto resetEditor = [&](const std::string& text) {
            sci(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(text.c_str()));
            sci(SCI_EMPTYUNDOBUFFER);
            sci(SCI_SETSEL, 0, 0);
        };
        using orbit::EditorCommand;
        resetEditor("alpha\nbeta\n");
        orbit::executeEditorCommand(editor, EditorCommand::Duplicate);
        check("duplicate_line", currentText() == "alpha\nalpha\nbeta\n");
        orbit::executeEditorCommand(editor, EditorCommand::Undo);
        check("duplicate_is_one_undo_step", currentText() == "alpha\nbeta\n");
        sci(SCI_SETSEL, 0, 5);
        orbit::executeEditorCommand(editor, EditorCommand::Duplicate);
        check("duplicate_selection", currentText().find("alphaalpha") == 0);
        resetEditor("one\ntwo\nthree\n");
        sci(SCI_SETSEL, 0, 8);
        orbit::executeEditorCommand(editor, EditorCommand::DeleteLines);
        check("delete_selected_lines_preserves_next_line", currentText() == "three\n");
        orbit::executeEditorCommand(editor, EditorCommand::Undo);
        check("delete_lines_is_undoable", currentText() == "one\ntwo\nthree\n");
        sci(SCI_SETSEL, 0, 0);
        orbit::executeEditorCommand(editor, EditorCommand::MoveLinesDown);
        check("move_line_down", currentText() == "two\none\nthree\n");
        orbit::executeEditorCommand(editor, EditorCommand::Undo);
        check("move_line_is_one_undo_step", currentText() == "one\ntwo\nthree\n");
        resetEditor("word other word\n");
        sci(SCI_SETSEL, 0, 4);
        orbit::executeEditorCommand(editor, EditorCommand::SelectNextOccurrence);
        check("select_next_occurrence_adds_caret", sci(SCI_GETSELECTIONS) == 2);
        resetEditor("你好\nworld\n");
        orbit::executeEditorCommand(editor, EditorCommand::Duplicate);
        check("line_commands_preserve_utf8", currentText() == "你好\n你好\nworld\n");
        resetEditor("first\nsecond\n");
        orbit::executeEditorCommand(editor, EditorCommand::JoinLines);
        check("join_lines_removes_line_break", sci(SCI_GETLINECOUNT) == 2 && currentText().find("second") != std::string::npos);

        resetEditor("# persisted\nprint('reopened')\n");
        sci(SCI_GOTOPOS, 15);
        const auto closedCaret = sci(SCI_GETCURRENTPOS);
        closeTab("src/editing_workflow.py");
        check("dirty_closed_document_waits_for_save", documents.at("src/editing_workflow.py").pointer != 0);
        check("saved_closed_document_is_released", pump([&] {
            const auto& document = documents.at("src/editing_workflow.py");
            return !document.dirty && !document.pointer && document.cache.empty() && !document.loaded;
        }));
        reopenClosedTab();
        check("reopen_closed_tab_restores_text_and_caret", pump([&] {
            return active == "src/editing_workflow.py" && documents.at(active).loaded &&
                sci(SCI_GETCURRENTPOS) == closedCaret && currentText() == "# persisted\nprint('reopened')\n";
        }));
        saveSession();
        const auto session = json::parse(readFile(dataDir / L"session.json"));
        check("recent_files_and_view_state_are_persisted", session.contains("recentFiles") && session["editorViews"][active]["caret"] == closedCaret);
        sci(SCI_APPENDTEXT, 10, reinterpret_cast<sptr_t>("# pending\n"));
        const auto removedPath = active;
        deleteFile(removedPath);
        io.drain();
        pump([&] { return true; });
        check("recoverable_delete_cannot_recreate_dirty_closed_file", !fs::exists(root / fs::u8path(removedPath)) && !documents.contains(removedPath));

        const std::string sample = "from dataclasses import dataclass\nvalue = sum(range(100))  # navigation timing\n";
        Document large;
        large.path = "src/large_navigation.py";
        large.loaded = true;
        large.cache.reserve(10 * 1024 * 1024 + sample.size());
        while (large.cache.size() < 10 * 1024 * 1024) large.cache += sample;
        documents.emplace(large.path, std::move(large));
        openFile("src/large_navigation.py");
        std::vector<double> times;
        for (int i = 0; i < 5; ++i) {
            const auto began = Clock::now();
            activateDoc("src/large_navigation.py");
            times.push_back(std::chrono::duration<double, std::milli>(Clock::now() - began).count());
        }
        performance["cached_10MiB_activation_samples_ms"] = times;
        performance["scope"] = "Five method-level warm samples; not input-to-screen latency. No disk read or full lexical analysis is forced on activation.";
        closeTab("src/large_navigation.py");
        check("clean_large_closed_document_releases_storage", !documents.at("src/large_navigation.py").pointer && documents.at("src/large_navigation.py").cache.empty());
        check("structure_cache_reused_without_copying_large_file", documents.at("src/large_navigation.py").outline.symbols.empty());

        open("src/main.py");
        run(true);
        check("existing_python_test_workflow_still_runs", pump([&] { return !running; }) && !problems && editorText(output).find("Ran 4 tests") != std::string::npos);
    } catch (const std::exception& error) {
        results.push_back({{"name", "unexpected_exception"}, {"passed", false}, {"error", error.what()}});
        if (running) { stopRun(); pump([&] { return !running; }); }
    }
    io.drain();
    searchWorker.drain();
    if (!previousSession.empty()) atomicWrite(dataDir / L"session.json", previousSession);
    bool passed = true;
    for (const auto& result : results) if (!result["passed"].get<bool>()) passed = false;
    atomicWrite(report, json{{"passed", passed}, {"checks", results.size()}, {"results", results},
        {"performance", performance}, {"scope", "Bounded state models and isolated real native-editor workflows. Visual checks are recorded separately."}}.dump(2));
    return passed ? 0 : 1;
}
