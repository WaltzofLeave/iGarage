#include "../app.h"

bool App::vimKey(UINT key, bool ctrl, bool shift, bool alt) {
    if (!settings.vim || popup || menuWindow || quickPick.isOpen() || GetFocus() != editor || active.empty()) return false;
    using V = orbit::VimEditor;
    if (alt) return false;
    V::Key input = 0;
    if (ctrl) {
        if (shift && key == 'F') return false;
        if (key == VK_OEM_4 || key == 'C') input = V::Escape;
        else if (key == 'R') input = V::Redo;
        else if (key == 'F') input = V::PageDown;
        else if (key == 'B') input = V::PageUp;
        else if (key == 'D') input = V::HalfDown;
        else if (key == 'U') input = V::HalfUp;
        else if (key == 'H') input = V::Backspace;
        else if (key == 'M' || key == 'J') input = V::Enter;
        // Keep explicit workbench shortcuts; block unimplemented Ctrl editing commands.
        else if (key == 'S' || key == 'P' || key == 'K' || key == 'W' || key == VK_TAB || (key >= VK_F1 && key <= VK_F24)) return false;
        else return true;
    } else {
        switch (key) {
        case VK_ESCAPE: input = V::Escape; break;
        case VK_RETURN: input = V::Enter; break;
        case VK_BACK: input = V::Backspace; break;
        case VK_DELETE: input = V::Delete; break;
        case VK_LEFT: input = V::Left; break;
        case VK_RIGHT: input = V::Right; break;
        case VK_UP: input = V::Up; break;
        case VK_DOWN: input = V::Down; break;
        case VK_HOME: input = V::Home; break;
        case VK_END: input = V::End; break;
        case VK_PRIOR: input = V::PageUp; break;
        case VK_NEXT: input = V::PageDown; break;
        case VK_TAB: input = V::Tab; break;
        case VK_INSERT: return true;
        default: return false; // Printable keys arrive through WM_CHAR, with the actual layout/Shift state.
        }
    }
    closeLanguageHints();
    vimSurrogate = 0;
    vim.feed(input);
    finishVimInput();
    return true;
}

bool App::vimCharacter(wchar_t unit) {
    if (!settings.vim) return false;
    if (popup || menuWindow || quickPick.isOpen() || active.empty() || GetFocus() != editor) return true;
    // Win32 sends supplementary Unicode as a UTF-16 surrogate pair.
    if (unit >= 0xd800 && unit <= 0xdbff) { vimSurrogate = unit; return true; }
    orbit::VimEditor::Key character = unit;
    if (unit >= 0xdc00 && unit <= 0xdfff) {
        if (!vimSurrogate) return true;
        character = 0x10000 + ((vimSurrogate - 0xd800) << 10) + unit - 0xdc00;
    }
    vimSurrogate = 0;
    if (character >= 32) { closeLanguageHints(); vim.feed(character); finishVimInput(); }
    return true;
}

void App::finishVimInput() {
    const auto command = vim.takeHostCommand();
    if (command == "save-all") saveAll();
    if (command == "save" || command == "save-close") saveActive(true);
    if (command == "close" || command == "save-close") {
        // Orbit's tab-close path retains a dirty native document until its async save succeeds.
        if (!active.empty()) closeTab(active);
    } else if (command == "next-tab" || command == "previous-tab") {
        if (!tabs.empty()) {
            const auto current = std::find(tabs.begin(), tabs.end(), active) - tabs.begin();
            openFile(tabs[(current + (command == "next-tab" ? 1 : tabs.size() - 1)) % tabs.size()]);
        }
    } else if (command == "disable") action("vim-toggle");
    RECT status{0, px(layout.statusRect.top), px(layout.w), px(layout.h)};
    InvalidateRect(window, &status, FALSE);
}

void App::configureVim() {
    if (!vimAttached) { vim.attach(editor); vimAttached = true; }
    vim.enable(settings.vim);
    vim.refreshCaret();
    vimSurrogate = 0;
    if (settings.vim) { closeLanguageHints(); KillTimer(window, 4); }
}
