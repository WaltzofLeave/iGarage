#include "vim.h"
#include <algorithm>
#include <cctype>
#include <utility>
namespace orbit {
namespace {
bool space(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool letter(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
}
void VimEditor::normal(Key key) {
    const bool visual = mode_ == Mode::Visual || mode_ == Mode::VisualLine;
    if (key == Escape) {
        const auto p = caret(); mode_ = Mode::Normal; position(p); resetCommand(); notice_.clear(); refreshCaret(); return;
    }
    notice_.clear();
    const int count = std::min(countLimit, std::max(1, count_) * operatorCount_);
    if (pending_) {
        const auto pending = std::exchange(pending_, 0);
        if (pending == '"') {
            if (letter(key) || key == '"' || key == '_' || key == '0' || key == '1' || key == '-') { selectedRegister_ = static_cast<char>(key); return; }
        } else if (pending == 'q') {
            if (letter(key) && !replaying_) { recordingRegister_ = static_cast<char>(std::tolower(static_cast<unsigned char>(key))); recordingKeys_.clear(); recording_ = true; }
        } else if (pending == '@') {
            const auto reg = key == '@' ? lastMacro_ : static_cast<char>(key);
            if (macros_.contains(reg)) { const auto keys = macros_.at(reg); lastMacro_ = reg; resetCommand(); play(keys, count); return; }
        } else if (pending == 'g' && key == 'g') {
            const auto range = motion('g', count); if (op_) { apply(op_, range); return; }
        } else if (pending == 'i' || pending == 'a') {
            const auto range = textObject(key, pending == 'a', count);
            if (visual && range.valid) { visualAnchor_ = range.start; visualCaret_ = previous(range.end); selectVisual(); resetCommand(); return; }
            if (op_) { apply(op_, range); return; }
        } else if (pending == 'f' || pending == 'F' || pending == 't' || pending == 'T') {
            const auto range = find(pending, key, count); findKind_ = pending; findChar_ = key;
            if (op_) { apply(op_, range); return; }
        } else if (pending == 'r' && key >= 32) {
            const auto start = caret(); auto end = start;
            int replaced = 0;
            for (; replaced < count && end < lineEnd(lineOf(start)); ++replaced) end = next(end);
            if (replaced == count) {
                std::string value; for (int i = 0; i < count; ++i) value += encode(key);
                beginChange(); replace({start, end, false, true}, value); position(previous(start + value.size())); finishChange(); return;
            }
        }
        resetCommand(); return;
    }
    if (key >= '0' && key <= '9' && (count_ || key != '0')) {
        count_ = std::min(countLimit, count_ * 10 + key - '0'); return;
    }
    if (key == '"' || key == 'g' || key == 'f' || key == 'F' || key == 't' || key == 'T') { pending_ = key; return; }
    if ((op_ || visual) && (key == 'i' || key == 'a')) { pending_ = key; return; }
    if (key == 'd' || key == 'c' || key == 'y' || key == '>' || key == '<') {
        if (visual) { apply(key, visualRange()); return; }
        if (op_ == key) { apply(key, lines(lineOf(caret()), std::min<Pos>(lineOf(caret()) + count - 1, send(SCI_GETLINECOUNT) - 1))); return; }
        if (op_) { resetCommand(); return; }
        op_ = key; operatorCount_ = std::max(1, count_); count_ = 0; return;
    }
    if (key == ';' || key == ',') {
        auto kind = findKind_;
        if (key == ',') kind = kind == 'f' ? 'F' : kind == 'F' ? 'f' : kind == 't' ? 'T' : 't';
        const auto range = findChar_ ? find(kind, findChar_, count, true) : Range{};
        if (op_) { apply(op_, range); return; }
        resetCommand(); return;
    }
    const auto range = motion(key == Left || key == Backspace ? 'h' : key == Right || key == ' ' ? 'l' : key == Up ? 'k' : key == Down || key == Enter ? 'j' : key == Home ? '0' : key == End ? '$' : key, count);
    if (range.valid) {
        if (op_) { apply(op_, range); return; }
        resetCommand(); return;
    }
    if (op_) { resetCommand(); return; }
    if (key == 'v' || key == 'V') {
        if ((key == 'v' && mode_ == Mode::Visual) || (key == 'V' && mode_ == Mode::VisualLine)) { mode_ = Mode::Normal; position(visualCaret_); }
        else { if (!visual) visualAnchor_ = visualCaret_ = caret(); mode_ = key == 'v' ? Mode::Visual : Mode::VisualLine; selectVisual(); }
        resetCommand(); refreshCaret(); return;
    }
    if (key == 'i' || key == 'I' || key == 'a' || key == 'A' || key == 'o' || key == 'O') {
        if (visual) { mode_ = Mode::Normal; position(visualCaret_); }
        auto p = caret();
        if (key == 'I') p = send(SCI_GETLINEINDENTPOSITION, lineOf(p));
        if (key == 'A') p = lineEnd(lineOf(p));
        if (key == 'a') p = std::min(next(p), lineEnd(lineOf(p)));
        enterInsert(count);
        if (key == 'o' || key == 'O') {
            const auto indent = std::min<Pos>(send(SCI_GETLINEINDENTATION, lineOf(p)), 4096);
            const auto at = key == 'o' ? lineEnd(lineOf(p)) : lineStart(lineOf(p));
            std::string value = key == 'o' ? eol() + std::string(indent, ' ') : std::string(indent, ' ') + eol();
            replace({at, at, false, true}, value);
            p = key == 'o' ? at + value.size() : at + indent;
        }
        position(p); return;
    }
    if (key == 'D' || key == 'C' || key == 'Y') {
        op_ = key == 'D' ? 'd' : key == 'C' ? 'c' : 'y';
        apply(op_, key == 'Y' ? lines(lineOf(caret()), lineOf(caret()) + count - 1) : motion('$', count)); return;
    }
    if (key == 'x' || key == 'X' || key == Delete || key == 's') {
        if (visual) { apply(key == 's' ? 'c' : 'd', visualRange()); return; }
        op_ = key == 's' ? 'c' : 'd'; apply(op_, motion(key == 'X' ? 'h' : 'l', count)); return;
    }
    if (key == 'p' || key == 'P') {
        if (visual) { notice_ = "Exit Visual mode before paste"; resetCommand(); return; }
        paste(key == 'P', count); return;
    }
    if (key == 'r') { pending_ = key; return; }
    if (key == 'u' || key == Redo) {
        mode_ = Mode::Normal;
        for (int i = 0; i < count; ++i) send(key == 'u' ? SCI_UNDO : SCI_REDO);
        position(caret()); refreshCaret(); resetCommand(); return;
    }
    if (key == '.') { dot(count, count_ > 0); return; }
    if (key == 'q' || key == '@') { pending_ = key; return; }
    if (key == '/' || key == '?' || key == ':') {
        commandOrigin_ = caret(); mode_ = Mode::Command; commandKind_ = key; commandLine_.clear(); refreshCaret(); return;
    }
    if (key == 'n' || key == 'N') { search(key == 'n' ? searchForward_ : !searchForward_, count); resetCommand(); return; }
    if (key == '*' || key == '#') {
        const auto first = send(SCI_WORDSTARTPOSITION, caret(), TRUE), last = send(SCI_WORDENDPOSITION, caret(), TRUE);
        if (last > first && last - first < 4096) { search_ = text(first, last); searchWord_ = true; searchForward_ = key == '*'; search(searchForward_, count); }
        resetCommand(); return;
    }
    if (key == PageUp || key == PageDown || key == HalfUp || key == HalfDown) {
        const auto amount = std::max<Pos>(1, send(SCI_LINESONSCREEN) / (key == HalfUp || key == HalfDown ? 2 : 1));
        motion(key == PageUp || key == HalfUp ? 'k' : 'j', static_cast<int>(std::min<Pos>(countLimit, amount * count)));
    } else if (key == 'J') {
        const auto firstLine = lineOf(caret());
        beginChange();
        for (int i = 0; i < std::max(1, count - 1) && firstLine + 1 < send(SCI_GETLINECOUNT); ++i) {
            const auto end = lineEnd(firstLine), after = send(SCI_GETLINEINDENTPOSITION, firstLine + 1);
            replace({end, after, false, true}, end > lineStart(firstLine) && !space(byte(previous(end))) ? " " : "");
            position(end);
        }
        finishChange(); return;
    }
    resetCommand();
}

bool VimEditor::search(bool forward, int count) {
    if (search_.empty()) { notice_ = "No previous search"; return false; }
    const auto flags = send(SCI_GETSEARCHFLAGS), oldStart = send(SCI_GETTARGETSTART), oldEnd = send(SCI_GETTARGETEND);
    send(SCI_SETSEARCHFLAGS, SCFIND_MATCHCASE | (searchWord_ ? SCFIND_WHOLEWORD : 0));
    Pos at = caret(), firstMatch = -1; bool found = true;
    for (int i = 0; i < count; ++i) {
        send(SCI_SETTARGETSTART, forward ? next(at) : at); send(SCI_SETTARGETEND, forward ? length() : 0);
        auto match = send(SCI_SEARCHINTARGET, search_.size(), reinterpret_cast<sptr_t>(search_.data()));
        if (match < 0) {
            send(SCI_SETTARGETSTART, forward ? 0 : length()); send(SCI_SETTARGETEND, forward ? at : next(at));
            match = send(SCI_SEARCHINTARGET, search_.size(), reinterpret_cast<sptr_t>(search_.data()));
        }
        if (match < 0) { found = false; break; }
        at = match;
        if (!i) firstMatch = at;
        else if (at == firstMatch) count = i + 1 + (count - i - 1) % i;
    }
    send(SCI_SETSEARCHFLAGS, flags); send(SCI_SETTARGETSTART, oldStart); send(SCI_SETTARGETEND, oldEnd);
    if (found) position(at); else notice_ = "Pattern not found";
    return found;
}
void VimEditor::command(Key key) {
    if (key == Escape) { mode_ = Mode::Normal; position(commandOrigin_); commandLine_.clear(); resetCommand(); refreshCaret(); return; }
    if (key == Backspace) {
        if (commandLine_.empty()) { command(Escape); return; }
        do { const auto c = static_cast<unsigned char>(commandLine_.back()); commandLine_.pop_back(); if ((c & 0xc0) != 0x80) break; } while (!commandLine_.empty());
        return;
    }
    if (key != Enter) { if (key >= 32 && commandLine_.size() < 4096) commandLine_ += encode(key); return; }
    mode_ = Mode::Normal; refreshCaret();
    if (commandKind_ == '/' || commandKind_ == '?') {
        if (!commandLine_.empty()) search_ = commandLine_;
        searchWord_ = false; searchForward_ = commandKind_ == '/'; search(searchForward_, std::max(1, count_));
    } else {
        const auto& value = commandLine_;
        if (value == "w" || value == "write") hostCommand_ = "save";
        else if (value == "wa" || value == "wall") hostCommand_ = "save-all";
        else if (value == "q" || value == "quit") hostCommand_ = "close";
        else if (value == "wq" || value == "x") hostCommand_ = "save-close";
        else if (value == "bn" || value == "bnext") hostCommand_ = "next-tab";
        else if (value == "bp" || value == "bprevious") hostCommand_ = "previous-tab";
        else if (value == "set novim") hostCommand_ = "disable";
        else if (value == "$" || (!value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))) {
            Pos line = 0;
            for (auto c : value) line = std::min<Pos>(send(SCI_GETLINECOUNT), line * 10 + c - '0');
            if (value == "$") line = send(SCI_GETLINECOUNT);
            position(send(SCI_GETLINEINDENTPOSITION, std::max<Pos>(0, line - 1)));
        } else notice_ = "Unsupported Ex command";
    }
    commandLine_.clear(); resetCommand();
}

void VimEditor::play(const std::vector<Key>& keys, int count) {
    if (playbackDepth_ >= 8) { notice_ = "Macro recursion limit"; return; }
    if (!playbackDepth_) { playbackBudget_ = sequenceLimit; playbackBytes_ = registerLimit; }
    ++playbackDepth_;
    const bool previousReplay = replaying_; replaying_ = true;
    for (int n = 0; n < count && playbackBudget_; ++n) {
        for (auto key : keys) {
            if (!playbackBudget_) break;
            --playbackBudget_; feed(key);
            // Host commands run only after playback unwinds, never against a stale document.
            if (!hostCommand_.empty()) { playbackBudget_ = 0; break; }
        }
    }
    replaying_ = previousReplay; --playbackDepth_;
    if (!playbackDepth_ && !playbackBudget_ && hostCommand_.empty()) { leaveDocument(); notice_ = "Macro key budget exhausted"; }
}
void VimEditor::dot(int count, bool explicitCount) {
    auto keys = lastChange_;
    resetCommand();
    if (keys.empty()) return;
    if (explicitCount) {
        auto start = keys.begin();
        if (keys.size() > 1 && keys[0] == '"') start += 2;
        auto end = start;
        while (end != keys.end() && *end >= '0' && *end <= '9') ++end;
        const auto at = static_cast<std::size_t>(start - keys.begin());
        keys.erase(start, end);
        const auto number = std::to_string(count);
        keys.insert(keys.begin() + at, number.begin(), number.end());
    }
    const bool previousDot = repeatingDot_; repeatingDot_ = true;
    play(keys, 1);
    repeatingDot_ = previousDot;
    lastChange_ = std::move(keys);
}
}
