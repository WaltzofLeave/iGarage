#include "../app.h"

#include <cctype>
#include "backend.h"
#include "client.h"

namespace {
constexpr UINT languageWake = WM_APP + 20;
constexpr int errorIndicator = 20, warningIndicator = 21;
bool pythonFile(const std::string& path) { const auto type = extension(path); return type == "py" || type == "pyi" || type == "pyw"; }
std::wstring interpreterPath(std::wstring interpreter) {
    if (fs::is_regular_file(interpreter)) return fs::absolute(interpreter).wstring();
    wchar_t buffer[32768]{};
    const auto count = SearchPathW(nullptr, interpreter.c_str(), L".exe", 32768, buffer, nullptr);
    return count && count < 32768 ? std::wstring(buffer) : interpreter;
}
} // namespace

void App::startLanguage(bool force) {
    const auto interpreter = interpreterPath(settings.interpreter);
    if (!force && languageRoot == root && languageInterpreter == interpreter) return;
    closeLanguageHints();
    languageRoot = root; languageInterpreter = interpreter;
    const auto executable = exeDir / L"services" / L"pyrefly" / L"pyrefly.exe";
    if (!fs::is_regular_file(executable)) {
        language.stop(); languageStatus = "代码分析未安装 · 请运行依赖准备脚本";
        return;
    }
    orbit::language::ProcessOptions options{executable, root,
        {L"lsp", L"--threads", L"2", L"--workspace-indexing-limit", L"2000"}};
    auto session = orbit::language::pyreflySession(orbit::language::fileUri(utf8(root.wstring())),
        utf8(root.filename().wstring()), utf8(interpreter), GetCurrentProcessId());
    language.start(std::make_unique<orbit::language::ProcessTransport>(std::move(options)),
        std::move(session), [owner = window] { PostMessageW(owner, languageWake, 0, 0); });
    languageStatus = language.status();
    SetTimer(window, 7, 100, nullptr);
    // Snapshot only opened, bounded Python documents. Future edits use incremental changes.
    for (const auto& path : tabs) {
        const auto found = documents.find(path);
        if (!pythonFile(path) || found == documents.end() || !found->second.loaded) continue;
        if (path == active) { syncLanguageDocument(); continue; }
        const auto& document = found->second;
        if (document.cache.size() <= orbit::language::Service::fileLimit)
            language.open(path, document.cache, document.revision);
    }
}
void App::syncLanguageDocument() {
    if (!pythonFile(active) || !documents.contains(active) || !documents.at(active).loaded) return;
    if (sci(SCI_GETLENGTH) > orbit::language::Service::fileLimit) { language.close(active); return; }
    const auto* known = language.document(active);
    if (!known || known->revision != documents.at(active).revision)
        language.open(active, currentText(), documents.at(active).revision);
}
void App::closeLanguageHints() {
    language.cancelRequests(active);
    completionPopup.close(); completionItems.clear();
    pendingCompletionAccept.clear();
    hoverPosition = std::string::npos;
    signatureVisible = false;
    if (editor) sci(SCI_CALLTIPCANCEL);
    if (window) { KillTimer(window, 4); KillTimer(window, 8); KillTimer(window, 9); }
}
void App::languageChanged(SCNotification* event) {
    if (suppress || active.empty() || !pythonFile(active) || !documents.contains(active)) return;
    using namespace orbit::language;
    if (event->nmhdr.code == SCN_MODIFIED && (event->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) {
        completionPopup.close(); completionItems.clear();
        pendingCompletionAccept.clear();
        hoverPosition = std::string::npos;
        const bool insert = (event->modificationType & SC_MOD_INSERTTEXT) != 0;
        if (!language.change(active, static_cast<std::size_t>(event->position), insert ? 0 : static_cast<std::size_t>(event->length),
            insert && event->text ? std::string_view(event->text, event->length) : std::string_view{}, documents.at(active).revision))
            syncLanguageDocument();
        updateDiagnosticIndicators();
        if (signatureVisible) SetTimer(window, 8, 80, nullptr);
    }
    if (event->nmhdr.code == SCN_CHARADDED) {
        if (event->ch == '.' || event->ch == '\'' || event->ch == '"') SetTimer(window, 4, 35, nullptr);
        if (event->ch == '(' || event->ch == ',') SetTimer(window, 8, 60, nullptr);
        if (event->ch == ')') SetTimer(window, 8, 60, nullptr);
    }
    if (event->nmhdr.code == SCN_DWELLSTART && event->position >= 0) requestHover(static_cast<std::size_t>(event->position));
    if (event->nmhdr.code == SCN_DWELLEND) { hoverPosition = std::string::npos; if (!signatureVisible) sci(SCI_CALLTIPCANCEL); }
    if (event->nmhdr.code == SCN_UPDATEUI) {
        if (completionPopup.isOpen() && static_cast<std::size_t>(sci(SCI_GETCURRENTPOS)) != completionCaret) completionPopup.close();
        if (signatureVisible && (event->updated & SC_UPDATE_SELECTION)) SetTimer(window, 8, 100, nullptr);
    }
}
void App::requestLanguageCompletion(bool explicitRequest) {
    if (GetFocus() != editor || !pythonFile(active)) return;
    if (sci(SCI_GETLENGTH) > orbit::language::Service::fileLimit) {
        if (explicitRequest) toast("文件超过 2 MiB，语义分析已暂停");
        return;
    }
    startLanguage(); syncLanguageDocument();
    const auto caret = static_cast<std::size_t>(sci(SCI_GETCURRENTPOS));
    const auto prefix = static_cast<std::size_t>(sci(SCI_WORDSTARTPOSITION, caret, TRUE));
    if (!explicitRequest && prefix == caret && (caret == 0 || sci(SCI_GETCHARAT, caret - 1) != '.')) return;
    if (!explicitRequest && caret - prefix == 1 && (prefix == 0 || sci(SCI_GETCHARAT, prefix - 1) != '.')) return;
    completionPath = active; completionRevision = documents.at(active).revision;
    completionCaret = caret; completionPrefix = prefix;
    if (language.request(orbit::language::RequestKind::Completion, active, caret)) return;
    if (explicitRequest) toast(languageStatus);
}
void App::requestSignature() {
    if (GetFocus() != editor || !pythonFile(active)) return;
    startLanguage(); syncLanguageDocument();
    language.request(orbit::language::RequestKind::Signature, active, static_cast<std::size_t>(sci(SCI_GETCURRENTPOS)));
}
void App::requestHover(std::size_t at) {
    if (!pythonFile(active)) return;
    startLanguage(); syncLanguageDocument();
    hoverPosition = at;
    if (const auto* document = language.document(active)) {
        for (const auto& diagnostic : document->diagnostics) {
            auto a = document->buffer.offset(diagnostic.span.start), b = document->buffer.offset(diagnostic.span.end);
            if (a && b && at >= *a && at <= *b) { signatureVisible = false; showLanguageTip(diagnostic.message, at); return; }
        }
    }
    language.request(orbit::language::RequestKind::Hover, active, at);
}
void App::resolveCompletion() {
    if (const auto* item = completionPopup.current(); item && !item->resolved && language.canResolve())
        language.request(orbit::language::RequestKind::Resolve, active, completionCaret, item->item);
}
void App::acceptCompletion(std::size_t index) {
    const auto* document = language.document(active);
    if (!document || active != completionPath || document->revision != completionRevision ||
        static_cast<std::size_t>(sci(SCI_GETCURRENTPOS)) != completionCaret || index >= completionItems.size()) { completionPopup.close(); return; }
    auto item = completionItems[index];
    if (const auto* selectedItem = completionPopup.current(); selectedItem && completionPopup.selected() == index) item = *selectedItem;
    if (!item.resolved && language.canResolve()) {
        pendingCompletionAccept = item.label;
        language.request(orbit::language::RequestKind::Resolve, active, completionCaret, item.item);
        return;
    }
    pendingCompletionAccept.clear();
    const auto plan = orbit::language::completionEdit(document->buffer, item, completionPrefix, completionCaret);
    completionPopup.close(); completionItems.clear();
    if (!plan) { toast("补全内容已过期，请重新触发补全"); return; }
    sci(SCI_BEGINUNDOACTION);
    for (const auto& edit : plan->edits) {
        sci(SCI_SETTARGETSTART, edit.start); sci(SCI_SETTARGETEND, edit.end);
        sci(SCI_REPLACETARGET, edit.text.size(), reinterpret_cast<sptr_t>(edit.text.data()));
    }
    sci(SCI_SETSEL, plan->caret, plan->caret);
    sci(SCI_ENDUNDOACTION);
    sci(SCI_SCROLLCARET);
    SetFocus(editor);
}
void App::showLanguageTip(std::string text, std::size_t at, int highlightStart, int highlightEnd) {
    if (text.empty()) { sci(SCI_CALLTIPCANCEL); return; }
    // Calltips are native, non-activating editor windows. Bound long provider documents.
    const auto tip = orbit::language::boundedTip({std::move(text), highlightStart, highlightEnd});
    sci(SCI_CALLTIPUSESTYLE, px(16));
    sci(SCI_STYLESETFONT, STYLE_CALLTIP, reinterpret_cast<sptr_t>("JetBrains Mono"));
    sci(SCI_STYLESETSIZEFRACTIONAL, STYLE_CALLTIP, 825);
    sci(SCI_STYLESETFORE, STYLE_CALLTIP, rgb(theme.text)); sci(SCI_STYLESETBACK, STYLE_CALLTIP, rgb(theme.panel));
    sci(SCI_CALLTIPSETBACK, rgb(theme.panel)); sci(SCI_CALLTIPSETFORE, rgb(theme.text));
    sci(SCI_CALLTIPSETFOREHLT, rgb(theme.accent));
    sci(SCI_CALLTIPSHOW, at, reinterpret_cast<sptr_t>(tip.text.c_str()));
    sci(SCI_CALLTIPSETHLT, tip.highlightStart, tip.highlightEnd);
}
void App::pollLanguage() {
    using namespace orbit::language;
    const auto results = language.poll();
    if (languageStatus != language.status() && languageRoot == root) { languageStatus = language.status(); invalidate(); }
    if (language.diagnosticsChanged()) { updateDiagnosticIndicators(); invalidate(); }
    for (const auto& result : results) {
        if (GetFocus() != editor || hasTransientPopup() || result.path != active || !documents.contains(active) || documents.at(active).revision != result.revision) continue;
        if (result.kind != RequestKind::Hover && static_cast<std::size_t>(sci(SCI_GETCURRENTPOS)) != result.caret) continue;
        if (!result.error.empty()) {
            if (result.kind == RequestKind::Resolve && !pendingCompletionAccept.empty()) {
                pendingCompletionAccept.clear(); toast("暂时无法准备补全，请重新选择候选项");
            }
            continue;
        }
        if (result.kind == RequestKind::Completion) {
            auto values = completions(result.value);
            const auto* document = language.document(active);
            if (!document || completionPrefix > result.caret) continue;
            const auto prefix = lower(document->buffer.text().substr(completionPrefix, result.caret - completionPrefix));
            std::erase_if(values, [&](const auto& item) { return !lower(item.filter).starts_with(prefix); });
            completionItems = values;
            POINT anchor{static_cast<LONG>(sci(SCI_POINTXFROMPOSITION, 0, result.caret)),
                static_cast<LONG>(sci(SCI_POINTYFROMPOSITION, 0, result.caret) + sci(SCI_TEXTHEIGHT, 0) + px(3))};
            ClientToScreen(editor, &anchor);
            completionPopup.show(window, anchor, dpi, theme, d2d.Get(), dw.Get(), fontCollection.Get(), std::move(values),
                [this](std::size_t index) { acceptCompletion(index); }, [this](std::size_t) { SetTimer(window, 9, 55, nullptr); });
        } else if (result.kind == RequestKind::Resolve) {
            auto values = completions(Json::array({result.value}));
            if (!values.empty() && completionPopup.current() && completionPopup.current()->label == values.front().label) {
                auto& item = values.front(); item.resolved = true;
                const auto selectedIndex = completionPopup.selected();
                completionPopup.resolved(item);
                if (selectedIndex < completionItems.size()) completionItems[selectedIndex] = item;
                if (pendingCompletionAccept == item.label) acceptCompletion(selectedIndex);
                else pendingCompletionAccept.clear();
            }
        } else if (result.kind == RequestKind::Signature) {
            const auto hint = signature(result.value);
            signatureVisible = !hint.text.empty();
            showLanguageTip(hint.text, result.caret, hint.highlightStart, hint.highlightEnd);
        } else if (result.kind == RequestKind::Hover && hoverPosition == result.caret) {
            signatureVisible = false;
            showLanguageTip(plainDocumentation(result.value.is_object() ? result.value.value("contents", Json{}) : Json{}), result.caret);
        }
    }
}
bool App::languageKey(UINT key, bool ctrl, bool shift, bool alt) {
    if (GetFocus() != editor || hasTransientPopup()) return false;
    if (key == VK_ESCAPE) {
        const bool visible = completionPopup.isOpen() || signatureVisible || sci(SCI_CALLTIPACTIVE);
        closeLanguageHints();
        if (visible) return true;
    }
    if (!ctrl && !alt && completionPopup.key(key)) return true;
    if (ctrl && !alt && !shift && key == VK_SPACE && pythonFile(active)) { requestLanguageCompletion(true); return true; }
    if (ctrl && shift && !alt && key == VK_SPACE) { requestSignature(); return true; }
    if (ctrl && !alt && !shift && key == 'Q') { requestHover(static_cast<std::size_t>(sci(SCI_GETCURRENTPOS))); return true; }
    return false;
}
void App::updateDiagnosticIndicators() {
    for (const auto indicator : {errorIndicator, warningIndicator}) {
        sci(SCI_INDICSETSTYLE, indicator, INDIC_SQUIGGLE);
        sci(SCI_INDICSETFORE, indicator, rgb(indicator == errorIndicator ? theme.red : theme.accent));
        sci(SCI_INDICSETUNDER, indicator, TRUE);
        sci(SCI_SETINDICATORCURRENT, indicator); sci(SCI_INDICATORCLEARRANGE, 0, sci(SCI_GETLENGTH));
    }
    if (const auto* document = language.document(active)) for (const auto& diagnostic : document->diagnostics) {
        const auto a = document->buffer.offset(diagnostic.span.start), b = document->buffer.offset(diagnostic.span.end);
        if (!a || !b) continue;
        sci(SCI_SETINDICATORCURRENT, diagnostic.severity == 1 ? errorIndicator : warningIndicator);
        sci(SCI_INDICATORFILLRANGE, *a, std::max<std::size_t>(1, *b - *a));
    }
}
int App::diagnosticCount() const {
    int total = 0;
    for (const auto& [path, document] : language.documents()) total += static_cast<int>(document.diagnostics.size());
    return total;
}
void App::renderLanguageProblems(D2D1_RECT_F bounds, float top) {
    float y = top + 7;
    if (problems && !lastError.empty()) {
        std::stringstream runtimeError(lastError);
        std::string line;
        while (std::getline(runtimeError, line) && y + 21 < bounds.bottom) {
            paint.text(wide(line), rect(bounds.left + 17, y, width(bounds) - 34, 21), theme.red, 10, 400, 0, true);
            y += 21;
        }
        y += 8;
    }
    for (const auto& [path, document] : language.documents()) for (const auto& diagnostic : document.diagnostics) {
        if (y + 45 > bounds.bottom) return;
        paint.icon(diagnostic.severity == 1 ? Icon::Stop : Icon::Help, bounds.left + 12, y + 9, 16, theme.red);
        paint.text(wide(path) + L":" + std::to_wstring(diagnostic.span.start.line + 1), rect(bounds.left + 39, y, width(bounds) - 55, 20), theme.muted, 9);
        auto diagnosticText = diagnostic.message; std::replace(diagnosticText.begin(), diagnosticText.end(), '\n', ' ');
        paint.text(wide(diagnosticText), rect(bounds.left + 39, y + 19, width(bounds) - 55, 23), theme.text, 10);
        hit(rect(bounds.left, y, width(bounds), 44), "language-problem", path, diagnostic.span.start.line);
        y += 45;
    }
}
