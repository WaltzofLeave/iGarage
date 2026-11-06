#include "../app.h"
#include <numeric>
#include <psapi.h>
#include "../language/backend.h"
#include "../language/client.h"
#include "language_contract_tests.h"

int App::languageTest(const fs::path& report) {
    using namespace orbit::language;
    json checks = json::array(), timings = json::object();
    const auto check = [&](std::string name, bool passed) { checks.push_back({{"name", std::move(name)}, {"passed", passed}}); };
    const auto throws = [](auto operation) { try { operation(); return false; } catch (...) { return true; } };
    Service server;
    for (auto& result : orbit::tests::languageContractChecks()) checks.push_back(std::move(result));
    std::string previousSession;
    try { previousSession = readFile(dataDir / L"session.json"); } catch (...) {}
    const auto responseEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    try {
        TextBuffer unicode("中文😀name\r\nsecond\n");
        check("utf8_bytes_convert_to_utf16_surrogate_pairs", unicode.at(10) == Position{0, 4} && unicode.offset({0, 4}) == 10);
        check("invalid_middle_of_surrogate_is_rejected", !unicode.offset({0, 3}) && throws([&] { unicode.at(7); }));
        check("crlf_and_eof_positions_round_trip", unicode.at(16) == Position{1, 0} && unicode.offset({2, 0}) == unicode.text().size());
        const auto edit = unicode.change(10, 4, "值");
        check("incremental_change_uses_pre_edit_utf16_range", edit["range"]["start"] == encode({0, 4}) && edit["range"]["end"] == encode({0, 8}));
        check("line_index_tracks_insertions_and_deletions", unicode.at(15) == Position{1, 0} && unicode.offset({1, 2}) == 17);
        unicode.change(13, 2, "\nfirst\n");
        check("line_index_rebuilds_after_newline_changes", unicode.offset({2, 0}) == 20);
        const std::string path = "C:\\开发 项目\\中文😀.py";
        check("file_uri_roundtrips_unicode_spaces_and_windows_drive", uriPath(fileUri(path)) == path);
        check("file_uri_roundtrips_unc_paths", uriPath(fileUri("\\\\server\\share\\a b.py")) == "\\\\server\\share\\a b.py");
        check("malformed_uri_and_nonfile_schemes_are_rejected", !uriPath("https://example.org/a") && !uriPath("file:///C:/bad%zz.py"));

        const Json payload{{"jsonrpc", "2.0"}, {"id", 7}, {"result", "中文😀"}};
        const auto wire = frame(payload);
        FrameDecoder decoder;
        std::vector<Json> decoded;
        for (const auto c : wire) for (auto& value : decoder.feed(std::string_view(&c, 1))) decoded.push_back(std::move(value));
        check("lsp_headers_and_utf8_body_can_arrive_one_byte_at_a_time", decoded == std::vector<Json>{payload});
        check("multiple_lsp_frames_in_one_read_preserve_order", decoder.feed(wire + wire).size() == 2);
        check("oversized_or_duplicate_content_lengths_are_rejected", throws([] { FrameDecoder d; d.feed("Content-Length: 999999999\r\n\r\n"); }) &&
            throws([] { FrameDecoder d; d.feed("Content-Length: 1\r\nContent-Length: 1\r\n\r\n{}"); }));

        TextBuffer source("# demo\nPat");
        Completion candidate;
        candidate.label = "Path";
        candidate.item = {{"label", "Path"}, {"textEdit", {{"range", {{"start", encode({1, 0})}, {"end", encode({1, 3})}}}, {"newText", "Path"}}},
            {"additionalTextEdits", Json::array({{{"range", {{"start", encode({0, 0})}, {"end", encode({0, 0})}}}, {"newText", "from pathlib import Path\n"}}})}};
        auto plan = completionEdit(source, candidate, 7, 10);
        std::string inserted = source.text();
        if (plan) for (const auto& replacement : plan->edits) inserted.replace(replacement.start, replacement.end - replacement.start, replacement.text);
        check("completion_and_auto_import_are_planned_as_one_validated_transaction", plan && inserted == "from pathlib import Path\n# demo\nPath" && plan->caret == inserted.size());
        candidate.item["additionalTextEdits"][0]["range"] = {{"start", encode({1, 0})}, {"end", encode({1, 0})}};
        const auto sharedBoundary = completionEdit(source, candidate, 7, 10);
        check("import_insertion_at_primary_edit_start_is_merged_deterministically", sharedBoundary && sharedBoundary->edits.size() == 1 &&
            sharedBoundary->edits.front().text == "from pathlib import Path\nPath" && sharedBoundary->caret == inserted.size());
        candidate.item["additionalTextEdits"][0]["range"] = {{"start", encode({1, 1})}, {"end", encode({1, 2})}};
        check("overlapping_server_edits_are_rejected_before_editor_mutation", !completionEdit(source, candidate, 7, 10));
        const auto defaults = completions({{"items", Json::array({{{"label", "Path"}}})},
            {"itemDefaults", {{"editRange", {{"start", encode({1, 0})}, {"end", encode({1, 3})}}}}}});
        check("completion_list_default_edit_ranges_are_supported", defaults.size() == 1 && completionEdit(source, defaults[0], 7, 10).has_value());
        const auto sign = signature({{"activeParameter", 1}, {"signatures", Json::array({{{"label", "greet(name: str, count: int)"},
            {"parameters", Json::array({{{"label", Json::array({6, 15})}}, {{"label", Json::array({17, 27})}}})}}})}});
        check("signature_preserves_current_parameter_highlight", sign.highlightStart == 17 && sign.highlightEnd == 27);
        const auto wrapped = boundedTip({std::string(90, 'x') + "中文😀", 90, 96});
        check("calltip_wrapping_preserves_unicode_and_active_parameter_bytes", wrapped.text.substr(91) == "中文😀" && wrapped.highlightStart == 91 && wrapped.highlightEnd == 97);
        check("bounded_documents_never_end_in_partial_utf8", boundedTip({std::string(2190, 'x') + "😀😀😀😀"}).text.size() <= 2200 &&
            !throws([&] { TextBuffer buffer(boundedTip({std::string(2190, 'x') + "😀😀😀😀"}).text); buffer.at(buffer.text().size()); }));
        check("documentation_markdown_is_rendered_without_html_execution", plainDocumentation({{"kind", "markdown"}, {"value", "```python\nPath\n```\nA `path`."}}).find("Path") != std::string::npos);

        const auto fixture = exeDir / L"language-fixture";
        fs::create_directories(fixture);
        atomicWrite(fixture / L"pyproject.toml", "[tool.pyrefly]\nproject-includes = [\"**/*.py\"]\n");
        atomicWrite(fixture / L"model.py", "class User:\n    def greet(self, name: str, count: int = 1) -> str:\n        \"\"\"Build a friendly greeting.\"\"\"\n        return name * count\n\nclass ProjectWidget:\n    pass\n");
        const std::string code = "from pathlib import Path\nfrom model import User\npath = Path(\"demo.txt\")\nuser = User()\npath.rea\nuser.gre\n";
        atomicWrite(fixture / L"main.py", code);
        wchar_t interpreter[32768]{}; SearchPathW(nullptr, settings.interpreter.c_str(), L".exe", 32768, interpreter, nullptr);
        const auto localSession = pyreflySession(fileUri(utf8(fixture.wstring())),
            "language-fixture", utf8(interpreter), GetCurrentProcessId());
        server.start(std::make_unique<ProcessTransport>(ProcessOptions{
            exeDir / L"services/pyrefly/pyrefly.exe", fixture, {L"lsp", L"--threads", L"2"}}),
            localSession, [responseEvent] { SetEvent(responseEvent); });
        server.open("main.py", code, 0);
        std::vector<Result> received;
        const auto pump = [&](auto ready, int milliseconds = 10000) {
            const auto deadline = Clock::now() + std::chrono::milliseconds(milliseconds);
            do {
                for (auto& response : server.poll()) received.push_back(std::move(response));
                if (ready()) return true;
                WaitForSingleObject(responseEvent, 50);
            } while (Clock::now() < deadline);
            return false;
        };
        check("real_pyrefly_process_initializes_with_utf16_incremental_sync", pump([&] { return server.ready(); }));
        timings["initialize_ms"] = server.initializationMs();
        const auto memberAt = code.find("path.rea") + 8;
        const auto get = [&](RequestKind kind, std::size_t at) -> std::optional<Result> {
            received.clear();
            if (!server.request(kind, "main.py", at) || !pump([&] { return !received.empty(); })) return {};
            return received.back();
        };
        auto result = get(RequestKind::Completion, memberAt);
        auto values = result ? completions(result->value) : std::vector<Completion>{};
        const auto found = std::find_if(values.begin(), values.end(), [](const auto& c) { return c.label == "read_text"; });
        check("stdlib_members_are_inferred_even_when_absent_from_document_text", found != values.end() && code.find("read_text") == code.npos);
        if (result) timings["first_stdlib_completion_ms"] = result->milliseconds;
        result = get(RequestKind::Completion, code.find("user.gre") + 8);
        values = result ? completions(result->value) : std::vector<Completion>{};
        check("cross_file_class_members_are_resolved", std::any_of(values.begin(), values.end(), [](const auto& c) { return c.label == "greet"; }));
        result = get(RequestKind::Hover, code.find("path ="));
        check("hover_reports_inferred_type", result && result->value.dump().find("Path") != std::string::npos);
        check("diagnostics_are_published_for_unsaved_open_document_version", pump([&] { return server.document("main.py") && !server.document("main.py")->diagnostics.empty(); }));

        const std::string call = "from model import User\nuser = User()\nuser.greet(\"Orbit\", )\n";
        check("incremental_document_change_is_accepted", server.change("main.py", 0, code.size(), call, 1));
        result = get(RequestKind::Signature, call.find(", ") + 2);
        check("real_signature_help_resolves_imported_method_and_active_argument", result && signature(result->value).text.find("count") != std::string::npos && signature(result->value).highlightEnd > signature(result->value).highlightStart);
        std::vector<double> warm;
        for (int i = 0; i < 12; ++i) {
            result = get(RequestKind::Completion, call.find("greet") + 3);
            if (result) warm.push_back(result->milliseconds);
        }
        check("repeated_warm_requests_complete_without_server_restart", warm.size() == 12 && server.processId() != 0);
        timings["warm_completion_samples_ms"] = warm;
        HANDLE analysisProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, server.processId());
        PROCESS_MEMORY_COUNTERS_EX analysisMemory{}, hostMemory{};
        if (analysisProcess && GetProcessMemoryInfo(analysisProcess, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&analysisMemory), sizeof(analysisMemory))) {
            timings["server_working_set_MiB"] = analysisMemory.WorkingSetSize / (1024. * 1024);
            timings["server_private_commit_MiB"] = analysisMemory.PrivateUsage / (1024. * 1024);
        }
        if (analysisProcess) CloseHandle(analysisProcess);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&hostMemory), sizeof(hostMemory))) {
            timings["test_host_working_set_MiB"] = hostMemory.WorkingSetSize / (1024. * 1024);
            timings["test_host_private_commit_MiB"] = hostMemory.PrivateUsage / (1024. * 1024);
        }
        const std::string autoCode = "# auto import\nProjectWi";
        server.open("auto.py", autoCode, 0);
        received.clear();
        server.request(RequestKind::Completion, "auto.py", autoCode.size());
        pump([&] { return !received.empty(); });
        values = received.empty() ? std::vector<Completion>{} : completions(received.back().value);
        auto autoItem = std::find_if(values.begin(), values.end(), [](const auto& c) { return c.label == "ProjectWidget"; });
        check("workspace_auto_import_candidates_are_available", autoItem != values.end());
        bool autoImport = false;
        if (autoItem != values.end()) {
            received.clear();
            server.request(RequestKind::Resolve, "auto.py", autoCode.size(), autoItem->item);
            pump([&] { return !received.empty(); });
            if (!received.empty()) {
                auto resolved = completions(Json::array({received.back().value}));
                atomicWrite(fixture / L"resolved-auto.json", received.back().value.dump(2));
                auto editPlan = resolved.empty() ? std::optional<EditPlan>{} : completionEdit(TextBuffer(autoCode), resolved[0], autoCode.size() - 9, autoCode.size());
                auto updated = autoCode;
                if (editPlan) for (const auto& e : editPlan->edits) updated.replace(e.start, e.end - e.start, e.text);
                autoImport = updated.find("from model import ProjectWidget") != updated.npos && updated.ends_with("ProjectWidget");
            }
        }
        check("real_completion_resolve_supplies_validated_auto_import_edit", autoImport);
        server.close("auto.py");
        received.clear();
        server.request(RequestKind::Completion, "main.py", 0);
        server.change("main.py", call.size(), 0, "# updated\n", 2);
        pump([&] { return server.staleResponses() > 0; }, 2500);
        check("late_response_from_previous_document_version_is_discarded", received.empty() && server.staleResponses() > 0);
        check("transport_queue_remains_bounded", server.peakQueuedBytes() <= 16 * 1024 * 1024);
        timings["peak_outgoing_bytes"] = server.peakQueuedBytes();
        server.close("main.py");
        check("closed_documents_release_text_and_diagnostics", !server.document("main.py"));
        server.open("large.py", std::string(Service::fileLimit + 1, 'x'), 0);
        check("oversized_files_do_not_create_semantic_snapshots", !server.document("large.py"));
        const auto processId = server.processId();
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processId);
        const auto shutdown = Clock::now(); server.stop();
        const auto waited = process ? WaitForSingleObject(process, 3000) : WAIT_OBJECT_0;
        if (process) CloseHandle(process);
        check("language_server_and_workers_exit_on_session_shutdown", waited == WAIT_OBJECT_0 && server.processId() == 0);
        timings["shutdown_ms"] = std::chrono::duration<double, std::milli>(Clock::now() - shutdown).count();
        server.start(std::make_unique<ProcessTransport>(ProcessOptions{exeDir / L"services/pyrefly/missing.exe", fixture}), localSession, [responseEvent] { SetEvent(responseEvent); });
        check("missing_backend_reports_failure_without_blocking_editor_thread", pump([&] { return server.status().find("Cannot start") != std::string::npos; }));
        server.stop();

        ProcessTransport invalidPayload({exeDir / L"services/pyrefly/pyrefly.exe", fixture, {L"lsp", L"--threads", L"2"}});
        invalidPayload.start( [responseEvent] { SetEvent(responseEvent); });
        invalidPayload.send({{"invalid_utf8", std::string(1, static_cast<char>(0xff))}});
        bool writeFailure = false;
        const auto failureDeadline = Clock::now() + std::chrono::seconds(3);
        while (!writeFailure && Clock::now() < failureDeadline) {
            for (const auto& event : invalidPayload.drain()) if (event.kind == TransportEvent::Kind::Disconnected) writeFailure = true;
            if (!writeFailure) WaitForSingleObject(responseEvent, 50);
        }
        invalidPayload.stop();
        check("invalid_transport_payload_reports_failure_and_releases_pipe_workers", writeFailure && invalidPayload.processId() == 0);

        // Exercise the actual Scintilla adapter, not a second implementation of editing.
        const auto pumpUi = [&](auto ready) {
            const auto deadline = Clock::now() + std::chrono::seconds(8);
            do {
                MSG event{};
                while (PeekMessageW(&event, nullptr, 0, 0, PM_REMOVE)) {
                    if (event.message != WM_QUIT) { TranslateMessage(&event); DispatchMessageW(&event); }
                }
                if (ready()) return true;
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
            } while (Clock::now() < deadline);
            return false;
        };
        scanProject(fixture);
        check("native_fixture_opens_without_touching_user_project", pumpUi([&] { return root == fixture && documents.contains(active) && documents.at(active).loaded; }));
        openFile("main.py");
        check("native_editor_fixture_document_is_loaded", pumpUi([&] { return active == "main.py" && documents.at(active).loaded; }));
        language.stop();
        suppress = true; sci(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(source.text().c_str())); suppress = false;
        sci(SCI_EMPTYUNDOBUFFER);
        language.open(active, source.text(), documents.at(active).revision);
        completionPath = active; completionRevision = documents.at(active).revision;
        completionPrefix = 7; completionCaret = 10; sci(SCI_SETSEL, 10, 10);
        candidate.item["additionalTextEdits"][0]["range"] = {{"start", encode({0, 0})}, {"end", encode({0, 0})}};
        candidate.resolved = true;
        completionItems = {candidate}; acceptCompletion(0);
        check("native_completion_applies_import_and_identifier_at_correct_offsets", currentText() == inserted && static_cast<std::size_t>(sci(SCI_GETCURRENTPOS)) == inserted.size());
        sci(SCI_UNDO);
        check("native_auto_import_and_completion_undo_as_single_transaction", currentText() == source.text() && !sci(SCI_CANUNDO));
        sci(SCI_REDO);
        check("native_completion_transaction_supports_redo", currentText() == inserted);
        completionItems = {candidate}; completionPath = "other.py";
        const auto before = currentText(); acceptCompletion(0);
        check("completion_for_departed_tab_cannot_mutate_current_document", currentText() == before);
        const auto focusBefore = GetFocus();
        orbit::CompletionPopup list;
        list.show(window, {10, 10}, dpi, theme, d2d.Get(), dw.Get(), fontCollection.Get(), {candidate}, [](auto) {}, [](auto) {});
        const auto popupHandle = list.window();
        RECT popupBounds{}; GetWindowRect(popupHandle, &popupBounds);
        check("completion_popup_is_nonactivating_and_keeps_editor_focus", list.isOpen() && GetFocus() == focusBefore &&
            (GetWindowLongPtrW(popupHandle, GWL_EXSTYLE) & WS_EX_NOACTIVATE));
        check("completion_popup_uses_eight_consistent_rows_at_current_dpi", popupBounds.bottom - popupBounds.top == static_cast<int>(std::lround(286 * dpi / 96)));
        list.key(VK_ESCAPE);
        check("escape_hides_completion_without_destroying_cached_window", !list.isOpen() && list.window() == popupHandle);
        list.show(window, {10, 10}, dpi, Theme::light(), d2d.Get(), dw.Get(), fontCollection.Get(), {candidate}, [](auto) {}, [](auto) {});
        check("completion_reuses_window_and_rendering_resources_across_themes", list.isOpen() && list.window() == popupHandle && list.current()->label == candidate.label);
        list.close();
        language.stop();
    } catch (const std::exception& error) { checks.push_back({{"name", "unexpected_exception"}, {"passed", false}, {"error", error.what()}}); }
    server.stop();
    language.stop();
    io.drain();
    if (!previousSession.empty()) atomicWrite(dataDir / L"session.json", previousSession);
    if (responseEvent) CloseHandle(responseEvent);
    bool passed = true;
    for (const auto& value : checks) passed &= value["passed"].get<bool>();
    atomicWrite(report, json{{"passed", passed}, {"checks", checks.size()}, {"results", checks}, {"measurements", timings},
        {"scope", "Includes 24 in-memory simulated-remote contract checks and 47 real Pyrefly/native-editor checks. Timings measure the local pinned process and pipe/event handling, not real network or screen presentation."}}.dump(2));
    return passed ? 0 : 1;
}
