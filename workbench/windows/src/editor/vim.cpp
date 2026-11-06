#include "vim.h"
#include <algorithm>
#include <cctype>
#include <utility>

namespace orbit {
namespace {
bool space(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
}

void VimEditor::attach(HWND editor) {
    editor_ = editor;
    direct_ = reinterpret_cast<SciFnDirect>(SendMessageW(editor, SCI_GETDIRECTFUNCTION, 0, 0));
    context_ = SendMessageW(editor, SCI_GETDIRECTPOINTER, 0, 0);
}
VimEditor::Pos VimEditor::send(unsigned message, uptr_t w, sptr_t l) const {
    return direct_ ? direct_(context_, message, w, l) : 0;
}
VimEditor::Pos VimEditor::length() const { return send(SCI_GETLENGTH); }
VimEditor::Pos VimEditor::caret() const {
    return mode_ == Mode::Visual || mode_ == Mode::VisualLine ? visualCaret_ : send(SCI_GETCURRENTPOS);
}
VimEditor::Pos VimEditor::lineOf(Pos p) const { return send(SCI_LINEFROMPOSITION, p); }
VimEditor::Pos VimEditor::lineStart(Pos line) const {
    if (line >= send(SCI_GETLINECOUNT)) return length();
    return send(SCI_POSITIONFROMLINE, std::max<Pos>(0, line));
}
VimEditor::Pos VimEditor::lineEnd(Pos line) const { return send(SCI_GETLINEENDPOSITION, line); }
VimEditor::Pos VimEditor::next(Pos p) const { return send(SCI_POSITIONAFTER, std::clamp<Pos>(p, 0, length())); }
VimEditor::Pos VimEditor::previous(Pos p) const { return send(SCI_POSITIONBEFORE, std::clamp<Pos>(p, 0, length())); }
int VimEditor::byte(Pos p) const { return p >= 0 && p < length() ? static_cast<int>(send(SCI_GETCHARAT, p)) : 0; }
VimEditor::Pos VimEditor::normalPosition(Pos p) const {
    p = std::clamp<Pos>(p, 0, length());
    const auto end = lineEnd(lineOf(p)), start = lineStart(lineOf(p));
    return end > start && p >= end ? previous(end) : p;
}
int VimEditor::wordClass(Pos p, bool big) const {
    const int c = byte(p);
    if (!c || space(c)) return 0;
    return big || c >= 128 || std::isalnum(static_cast<unsigned char>(c)) || c == '_' ? 1 : 2;
}
std::string VimEditor::text(Pos first, Pos last) {
    if (last <= first) return {};
    std::string result(static_cast<std::size_t>(last - first) + 1, 0);
    Sci_TextRangeFull range{{first, last}, result.data()};
    send(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<sptr_t>(&range));
    result.resize(static_cast<std::size_t>(last - first));
    copiedBytes_ += result.size();
    return result;
}
std::string VimEditor::eol() const {
    const auto mode = send(SCI_GETEOLMODE);
    return mode == SC_EOL_CRLF ? "\r\n" : mode == SC_EOL_CR ? "\r" : "\n";
}
std::string VimEditor::encode(Key c) {
    std::string s;
    if (c < 0 || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return s;
    if (c < 0x80) s += static_cast<char>(c);
    else if (c < 0x800) { s += static_cast<char>(0xc0 | (c >> 6)); s += static_cast<char>(0x80 | (c & 63)); }
    else if (c < 0x10000) { s += static_cast<char>(0xe0 | (c >> 12)); s += static_cast<char>(0x80 | ((c >> 6) & 63)); s += static_cast<char>(0x80 | (c & 63)); }
    else { s += static_cast<char>(0xf0 | (c >> 18)); s += static_cast<char>(0x80 | ((c >> 12) & 63)); s += static_cast<char>(0x80 | ((c >> 6) & 63)); s += static_cast<char>(0x80 | (c & 63)); }
    return s;
}

void VimEditor::enable(bool enabled) {
    if (enabled_ != enabled) { leaveDocument(); enabled_ = enabled; }
    refreshCaret();
}
void VimEditor::leaveDocument() {
    // Never leave an undo group attached to the wrong Scintilla document.
    if (undoOpen_) finishChange();
    mode_ = Mode::Normal;
    desiredColumn_ = -1;
    resetCommand();
    commandLine_.clear(); hostCommand_.clear(); notice_.clear();
    recording_ = false; recordingKeys_.clear();
    refreshCaret();
}
void VimEditor::pointerInput() {
    if (!enabled_) return;
    if (undoOpen_) finishChange();
    if (mode_ != Mode::Insert) mode_ = Mode::Normal;
    resetCommand(); recording_ = false; recordingKeys_.clear();
    desiredColumn_ = -1;
    if (mode_ == Mode::Insert) { remember('i'); beginChange(); }
    refreshCaret();
}
void VimEditor::refreshCaret() {
    if (!direct_) return;
    send(SCI_SETCARETSTYLE, enabled_ && mode_ != Mode::Insert && mode_ != Mode::Command ? CARETSTYLE_BLOCK : CARETSTYLE_LINE);
    send(SCI_SETOVERTYPE, FALSE);
    if (enabled_ && mode_ == Mode::Normal) {
        const auto p = normalPosition(send(SCI_GETCURRENTPOS));
        send(SCI_SETSEL, p, p);
    }
}
std::string VimEditor::status() const {
    if (!enabled_) return {};
    if (mode_ == Mode::Command) return std::string(1, static_cast<char>(commandKind_)) + commandLine_;
    std::string result = mode_ == Mode::Insert ? "INSERT" : mode_ == Mode::Visual ? "VISUAL" : mode_ == Mode::VisualLine ? "VISUAL LINE" : "NORMAL";
    if (recording_) { result += "  REC @"; result += recordingRegister_; }
    if (op_) { result += "  "; result += static_cast<char>(op_); }
    if (count_) result += " " + std::to_string(count_);
    if (pending_ > 0 && pending_ < 128) { result += " "; result += static_cast<char>(pending_); }
    if (!notice_.empty()) result += "  " + notice_;
    return result;
}
std::string VimEditor::takeHostCommand() { return std::exchange(hostCommand_, {}); }
void VimEditor::position(Pos p, bool vertical) {
    p = mode_ == Mode::Insert ? std::clamp<Pos>(p, 0, length()) : normalPosition(p);
    if (!vertical) desiredColumn_ = -1;
    if (mode_ == Mode::Visual || mode_ == Mode::VisualLine) { visualCaret_ = p; selectVisual(); }
    else send(SCI_SETSEL, p, p);
    send(SCI_SCROLLCARET);
}
VimEditor::Range VimEditor::lines(Pos first, Pos last) const {
    if (first > last) std::swap(first, last);
    return {lineStart(first), lineStart(last + 1), true, true};
}
VimEditor::Range VimEditor::visualRange() const {
    if (mode_ == Mode::VisualLine) return lines(lineOf(visualAnchor_), lineOf(visualCaret_));
    return {std::min(visualAnchor_, visualCaret_), next(std::max(visualAnchor_, visualCaret_)), false, true};
}
void VimEditor::selectVisual() {
    const auto range = visualRange();
    if (visualCaret_ >= visualAnchor_) send(SCI_SETSEL, range.start, range.end);
    else send(SCI_SETSEL, range.end, range.start);
}

void VimEditor::resetCommand() {
    count_ = 0; operatorCount_ = 1; op_ = pending_ = 0; selectedRegister_ = '"';
    sequence_.clear(); sequenceOverflow_ = false;
}
void VimEditor::beginChange() {
    if (!undoOpen_) { send(SCI_BEGINUNDOACTION); undoOpen_ = true; changed_ = false; }
}
void VimEditor::finishChange() {
    if (undoOpen_) { send(SCI_ENDUNDOACTION); undoOpen_ = false; }
    if (changed_ && !repeatingDot_) {
        if (!sequenceOverflow_) lastChange_ = sequence_;
        else { lastChange_.clear(); notice_ = "Dot unavailable for this change"; }
    }
    changed_ = false;
    resetCommand();
}
void VimEditor::replace(Range range, const std::string& value) {
    if (send(SCI_GETREADONLY)) return;
    if (replaying_ && playbackDepth_) {
        if (value.size() > playbackBytes_) { playbackBudget_ = 0; notice_ = "Macro insertion budget exhausted"; return; }
        playbackBytes_ -= value.size();
    }
    send(SCI_SETTARGETSTART, range.start); send(SCI_SETTARGETEND, range.end);
    send(SCI_REPLACETARGET, value.size(), reinterpret_cast<sptr_t>(value.data()));
    if (range.start != range.end || !value.empty()) changed_ = true;
    send(SCI_SETSEL, range.start + value.size(), range.start + value.size());
}
bool VimEditor::store(Range range, bool yank) {
    if (selectedRegister_ == '_') return true;
    if (range.end - range.start > static_cast<Pos>(registerLimit)) { notice_ = "Register limit: 4 MiB (use black-hole register _)"; return false; }
    auto content = text(range.start, range.end);
    if (range.line && !content.ends_with('\n') && !content.ends_with('\r')) content += eol();
    const char name = static_cast<char>(std::tolower(static_cast<unsigned char>(selectedRegister_)));
    if (selectedRegister_ >= 'A' && selectedRegister_ <= 'Z' && registers_.contains(name)) {
        if (registers_[name].text->size() + content.size() > registerLimit) { notice_ = "Register limit: 4 MiB"; return false; }
        content.insert(0, *registers_[name].text);
    }
    if (content.size() > registerLimit) { notice_ = "Register limit: 4 MiB"; return false; }
    Register value{std::make_shared<const std::string>(std::move(content)), range.line};
    if (name != '"') registers_[name] = value;
    registers_['"'] = value;
    registers_[yank ? '0' : range.line ? '1' : '-'] = std::move(value);
    return true;
}
void VimEditor::apply(Key operation, Range range) {
    if (!range.valid) { resetCommand(); return; }
    const auto original = caret();
    const bool wasVisual = mode_ == Mode::Visual || mode_ == Mode::VisualLine;
    if (operation == 'y' || operation == 'd' || operation == 'c') {
        if (!store(range, operation == 'y')) { resetCommand(); return; }
    }
    mode_ = Mode::Normal;
    if (operation == 'y') { position(wasVisual ? range.start : original); resetCommand(); refreshCaret(); return; }
    if (wasVisual) sequenceOverflow_ = true;
    beginChange();
    if (operation == '>' || operation == '<') {
        const auto first = lineOf(range.start), last = lineOf(range.end > range.start ? previous(range.end) : range.end);
        const auto step = send(SCI_GETINDENT);
        for (auto line = first; line <= last; ++line) send(SCI_SETLINEINDENTATION, line, std::max<Pos>(0, send(SCI_GETLINEINDENTATION, line) + (operation == '>' ? step : -step)));
        changed_ = true; position(send(SCI_GETLINEINDENTPOSITION, first));
    } else {
        auto deletion = range;
        // Delete a final unterminated line together with its preceding EOL.
        if (range.line && operation == 'd' && range.end == length() && range.start > 0 &&
            (range.start == range.end || lineEnd(lineOf(range.end)) > lineStart(lineOf(range.end)))) deletion.start = lineEnd(lineOf(range.start) - 1);
        std::string replacement;
        std::size_t indentation = 0;
        if (range.line && operation == 'c') {
            indentation = static_cast<std::size_t>(std::min<Pos>(send(SCI_GETLINEINDENTATION, lineOf(range.start)), 4096));
            replacement.assign(indentation, ' ');
            if (range.end < length()) replacement += eol();
        }
        replace(deletion, replacement);
        position(range.start);
        if (operation == 'c') { send(SCI_SETSEL, deletion.start + indentation, deletion.start + indentation); enterInsert(); return; }
    }
    finishChange(); refreshCaret();
}
void VimEditor::paste(bool before, int count) {
    const auto found = registers_.find(static_cast<char>(std::tolower(static_cast<unsigned char>(selectedRegister_))));
    if (found == registers_.end() || found->second.text->empty()) { resetCommand(); return; }
    const auto value = found->second; // Register aliases share immutable storage.
    if (value.text->size() * static_cast<std::size_t>(count) > registerLimit) { notice_ = "Paste limit: 4 MiB"; resetCommand(); return; }
    const auto origin = caret();
    Pos at = before ? origin : std::min(next(origin), lineEnd(lineOf(origin)));
    std::string content;
    if (value.line) {
        at = before ? lineStart(lineOf(origin)) : lineStart(lineOf(origin) + 1);
        if (!before && at == length() && at > 0 && byte(previous(at)) != '\n' && byte(previous(at)) != '\r') content += eol();
    }
    const auto prefix = content.size();
    for (int i = 0; i < count; ++i) content += *value.text;
    beginChange(); replace({at, at, false, true}, content);
    position(value.line ? at + prefix : previous(at + content.size()));
    finishChange();
}

void VimEditor::enterInsert(int count) {
    beginChange(); mode_ = Mode::Insert; insertCount_ = count; refreshCaret();
}
void VimEditor::insert(Key key) {
    const auto p = caret();
    if (key == Escape) {
        // A counted insert replays its typed part inside the same undo action.
        if (insertCount_ > 1 && !sequenceOverflow_) {
            const int repetitions = insertCount_ - 1; insertCount_ = 1;
            auto first = std::find_if(sequence_.begin(), sequence_.end(), [](Key c) { return c == 'i' || c == 'a' || c == 'I' || c == 'A'; });
            if (first != sequence_.end()) {
                std::vector<Key> typed(first + 1, sequence_.end() - 1);
                const bool old = replaying_; replaying_ = true;
                std::size_t maximumBytes = 0;
                for (auto c : typed) maximumBytes += c == Enter ? 4100 : c == Tab ? 8 : 4;
                if (typed.size() * static_cast<std::size_t>(repetitions) <= sequenceLimit && maximumBytes * repetitions <= registerLimit) {
                    for (int n = 0; n < repetitions && (!playbackDepth_ || playbackBudget_); ++n) {
                        for (auto c : typed) {
                            if (playbackDepth_) { if (!playbackBudget_) break; --playbackBudget_; }
                            insert(c);
                        }
                    }
                } else notice_ = "Counted insert key limit";
                replaying_ = old;
            }
        }
        mode_ = Mode::Normal;
        if (p > lineStart(lineOf(p))) position(previous(caret())); else position(caret());
        finishChange(); refreshCaret(); return;
    }
    if (key == Left || key == Right || key == Up || key == Down || key == Home || key == End || key == PageUp || key == PageDown) {
        const unsigned message = key == Left ? SCI_CHARLEFT : key == Right ? SCI_CHARRIGHT : key == Up ? SCI_LINEUP : key == Down ? SCI_LINEDOWN : key == Home ? SCI_HOME : key == End ? SCI_LINEEND : key == PageUp ? SCI_PAGEUP : SCI_PAGEDOWN;
        send(message); return;
    }
    if (key == Backspace) { if (p > 0) replace({previous(p), p, false, true}, ""); return; }
    if (key == Delete) { if (p < length()) replace({p, next(p), false, true}, ""); return; }
    std::string value;
    if (key == Enter) {
        value = eol();
        const auto indent = std::min<Pos>(send(SCI_GETLINEINDENTATION, lineOf(p)), 4096);
        value.append(static_cast<std::size_t>(indent), ' ');
    } else if (key == Tab) {
        const auto width = std::max<Pos>(1, send(SCI_GETTABWIDTH));
        value.assign(static_cast<std::size_t>(width - send(SCI_GETCOLUMN, p) % width), ' ');
    } else if (key >= 32) value = encode(key);
    if (!value.empty()) replace({p, p, false, true}, value);
}

void VimEditor::remember(Key key) {
    if (sequence_.size() < sequenceLimit) sequence_.push_back(key);
    else sequenceOverflow_ = true;
}
void VimEditor::feed(Key key) {
    if (!enabled_ || !direct_) return;
    if (!replaying_) {
        if (recording_) {
            if (key == 'q' && mode_ == Mode::Normal && !op_ && !pending_ && !count_) {
                macros_[recordingRegister_] = std::move(recordingKeys_); recordingKeys_.clear(); recording_ = false; resetCommand(); return;
            }
            if (recordingKeys_.size() >= sequenceLimit) { recording_ = false; recordingKeys_.clear(); notice_ = "Macro limit: 65536 keys"; }
            else recordingKeys_.push_back(key);
        }
        remember(key);
    } else remember(key);
    if (mode_ == Mode::Insert) insert(key);
    else if (mode_ == Mode::Command) command(key);
    else normal(key);
}

}
