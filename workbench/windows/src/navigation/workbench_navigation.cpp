#include "../app.h"
#include "../editor/commands.h"

orbit::NavigationLocation App::captureEditorLocation() {
    orbit::NavigationLocation location;
    if (active.empty() || !documents.contains(active)) return location;
    auto& document = documents.at(active);
    location.path = active;
    location.view = document.view;
    if (document.loaded && document.pointer && sci(SCI_GETDOCPOINTER) == document.pointer) {
        location.view.caret = sci(SCI_GETCURRENTPOS);
        location.view.anchor = sci(SCI_GETANCHOR);
        location.view.firstVisibleLine = static_cast<int>(sci(SCI_GETFIRSTVISIBLELINE));
        location.view.horizontalOffset = static_cast<int>(sci(SCI_GETXOFFSET));
    }
    return location;
}

void App::rememberEditorView() {
    const auto location = captureEditorLocation();
    if (!location.path.empty()) documents.at(location.path).view = location.view;
}

void App::restoreEditorView(const orbit::EditorViewState& view) {
    const auto length = static_cast<std::int64_t>(sci(SCI_GETLENGTH));
    sci(SCI_SETSEL, std::clamp(view.anchor, std::int64_t(0), length),
        std::clamp(view.caret, std::int64_t(0), length));
    sci(SCI_SETFIRSTVISIBLELINE, std::max(0, view.firstVisibleLine));
    sci(SCI_SETXOFFSET, std::max(0, view.horizontalOffset));
}

void App::navigateHistory(bool forward) {
    if (active.empty()) return;
    const auto current = captureEditorLocation();
    const auto target = forward ? navigation.forward(current) : navigation.back(current);
    if (!target) return;
    if (!documents.contains(target->path)) {
        navigation.forgetPath(target->path);
        return;
    }
    pendingNavigation = *target;
    historyReplay = true;
    openFile(target->path);
}

void App::reopenClosedTab() {
    while (auto target = navigation.takeClosed()) {
        if (!documents.contains(target->path)) continue;
        if (std::find(tabs.begin(), tabs.end(), target->path) != tabs.end()) continue;
        pendingNavigation = *target;
        historyReplay = false;
        openFile(target->path);
        return;
    }
    toast("没有可重新打开的标签");
}

void App::releaseClosedDocument(const std::string& path) {
    const auto found = documents.find(path);
    if (found == documents.end() || std::find(tabs.begin(), tabs.end(), path) != tabs.end()) return;
    auto& document = found->second;
    // Failed or unfinished saves keep their document so recovery remains possible.
    if (document.dirty || document.pending) return;
    if (document.pointer) {
        if (sci(SCI_GETDOCPOINTER) == document.pointer) {
            suppress = true;
            sci(SCI_SETDOCPOINTER, 0, 0);
            suppress = false;
        }
        sci(SCI_RELEASEDOCUMENT, 0, document.pointer);
        document.pointer = 0;
    }
    std::string{}.swap(document.cache);
    orbit::DocumentOutline empty;
    std::swap(document.outline, empty);
    document.appearance.reset();
    document.outlineRevision = std::numeric_limits<std::uint64_t>::max();
    document.loaded = false;
}

void App::showRecentFiles() {
    closePopup();
    closeMenu();
    std::vector<orbit::QuickPickItem> rows;
    for (const auto& path : navigation.recent()) {
        if (documents.contains(path)) {
            const auto language = extension(path);
            const char kind = language == "py" ? 'P' : language == "md" ? 'M' : language == "js" ? 'J' : 'F';
            rows.push_back({path, wide(baseName(path)), wide(path), -1, kind});
        }
    }
    if (rows.empty() && !active.empty()) rows.push_back({active, wide(baseName(active)), wide(active)});
    const int initial = rows.size() > 1 && rows.front().id == active ? 1 : 0;
    quickPick.show(window, dpi, theme, d2d.Get(), dw.Get(), fontCollection.Get(),
        L"最近的文件", L"按文件名或路径筛选…", std::move(rows),
        [this](const orbit::QuickPickItem& item) { openFile(item.id); }, initial);
}

void App::showFileOutline() {
    if (active.empty()) return;
    closePopup();
    closeMenu();
    rebuildSymbols();
    std::vector<orbit::QuickPickItem> rows;
    for (const auto& symbol : symbols) {
        rows.push_back({active, wide(symbol.name), L"第 " + std::to_wstring(symbol.line + 1) + L" 行",
            symbol.line, symbol.kind});
    }
    quickPick.show(window, dpi, theme, d2d.Get(), dw.Get(), fontCollection.Get(),
        L"文件结构 · " + wide(baseName(active)), L"搜索类、函数或标题…", std::move(rows),
        [this](const orbit::QuickPickItem& item) { openFile(item.id, item.line); });
}

bool App::workbenchAction(const std::string& id, const std::string& data) {
    if (id == "recent-files") showRecentFiles();
    else if (id == "file-outline") showFileOutline();
    else if (id == "navigate-back") navigateHistory(false);
    else if (id == "navigate-forward") navigateHistory(true);
    else if (id == "reopen-tab") reopenClosedTab();
    else if (id == "close-others" || id == "close-all-tabs") {
        const auto keep = data.empty() ? active : data;
        const auto openTabs = tabs;
        for (const auto& path : openTabs) {
            if (id == "close-all-tabs" || path != keep) closeTab(path);
        }
    } else {
        using orbit::EditorCommand;
        static const std::map<std::string, EditorCommand> commands = {
            {"duplicate", EditorCommand::Duplicate}, {"delete-lines", EditorCommand::DeleteLines},
            {"join-lines", EditorCommand::JoinLines}, {"move-lines-up", EditorCommand::MoveLinesUp},
            {"move-lines-down", EditorCommand::MoveLinesDown},
            {"select-next-occurrence", EditorCommand::SelectNextOccurrence}};
        const auto command = commands.find(id);
        if (command == commands.end()) return false;
        if (!active.empty()) {
            SetFocus(editor);
            orbit::executeEditorCommand(editor, command->second);
        }
    }
    return true;
}

bool App::workbenchKey(UINT key, bool ctrl, bool shift, bool alt) {
    if (popup || menuWindow || quickPick.isOpen()) return false;
    if (ctrl && !shift && !alt && key == 'E') { showRecentFiles(); return true; }
    if (ctrl && !shift && !alt && key == VK_F12) { showFileOutline(); return true; }
    if (ctrl && shift && !alt && key == 'T') { reopenClosedTab(); return true; }
    if (ctrl && alt && !shift && (key == VK_LEFT || key == VK_RIGHT)) {
        navigateHistory(key == VK_RIGHT);
        return true;
    }
    if (GetFocus() != editor || active.empty()) return false;
    using orbit::EditorCommand;
    std::optional<EditorCommand> command;
    if (ctrl && !shift && !alt && key == 'D') command = EditorCommand::Duplicate;
    else if (ctrl && !shift && !alt && key == 'Y') command = EditorCommand::DeleteLines;
    else if (ctrl && shift && !alt && key == 'J') command = EditorCommand::JoinLines;
    else if (alt && shift && !ctrl && key == VK_UP) command = EditorCommand::MoveLinesUp;
    else if (alt && shift && !ctrl && key == VK_DOWN) command = EditorCommand::MoveLinesDown;
    else if (alt && !shift && !ctrl && key == 'J') command = EditorCommand::SelectNextOccurrence;
    else if (ctrl && shift && !alt && key == 'Z') command = EditorCommand::Redo;
    if (!command) return false;
    orbit::executeEditorCommand(editor, *command);
    return true;
}
