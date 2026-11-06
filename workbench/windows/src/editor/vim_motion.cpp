#include "vim.h"
#include <algorithm>
namespace orbit {
namespace {
constexpr sptr_t scanLimit = 1024 * 1024;
bool space(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
}
VimEditor::Range VimEditor::motion(Key key, int count) {
    const Pos origin = caret(), currentLine = lineOf(origin);
    Pos target = origin, budget = scanLimit;
    bool inclusive = false, linewise = false, vertical = false, valid = true;
    const bool big = key == 'W' || key == 'B' || key == 'E';
    if (key == 'h' || key == 'l') {
        for (int i = 0; i < count; ++i) {
            const auto p = key == 'h' ? previous(target) : next(target);
            if (p < lineStart(currentLine) || p > lineEnd(currentLine)) break;
            target = p;
        }
    } else if (key == 'j' || key == 'k') {
        if (desiredColumn_ < 0) desiredColumn_ = send(SCI_GETCOLUMN, origin);
        const auto line = std::clamp<Pos>(currentLine + (key == 'j' ? count : -count), 0, send(SCI_GETLINECOUNT) - 1);
        target = send(SCI_FINDCOLUMN, line, desiredColumn_);
        linewise = true; vertical = true;
    } else if (key == '0') target = lineStart(currentLine);
    else if (key == '^' || key == '_') target = send(SCI_GETLINEINDENTPOSITION, std::min<Pos>(currentLine + (key == '_' ? count - 1 : 0), send(SCI_GETLINECOUNT) - 1));
    else if (key == '$') { target = lineEnd(std::min<Pos>(currentLine + count - 1, send(SCI_GETLINECOUNT) - 1)); target = normalPosition(target); inclusive = true; }
    else if (key == 'g' || key == 'G') {
        target = send(SCI_GETLINEINDENTPOSITION, key == 'G' && !count_ && operatorCount_ == 1 ? send(SCI_GETLINECOUNT) - 1 : std::min<Pos>(count - 1, send(SCI_GETLINECOUNT) - 1));
        linewise = true;
    } else if (key == 'w' || key == 'W' || key == 'b' || key == 'B' || key == 'e' || key == 'E') {
        // cw behaves like ce when the caret starts on a non-blank character.
        const bool changeWord = op_ == 'c' && (key == 'w' || key == 'W') && wordClass(origin, big);
        for (int i = 0; i < count && budget > 0; ++i) {
            if (key == 'b' || key == 'B') {
                if (target > 0) target = previous(target);
                while (target > 0 && !wordClass(target, big) && budget-- > 0) target = previous(target);
                const int kind = wordClass(target, big);
                while (target > 0 && wordClass(previous(target), big) == kind && budget-- > 0) target = previous(target);
            } else if (key == 'e' || key == 'E' || changeWord) {
                if (!changeWord || i) target = next(target);
                while (target < length() && !wordClass(target, big) && budget-- > 0) target = next(target);
                const int kind = wordClass(target, big);
                while (next(target) < length() && wordClass(next(target), big) == kind && budget-- > 0) target = next(target);
                inclusive = true;
            } else {
                const int kind = wordClass(target, big);
                while (target < length() && wordClass(target, big) == kind && budget-- > 0) target = next(target);
                while (target < length() && !wordClass(target, big) && budget-- > 0) target = next(target);
            }
        }
        if (budget <= 0) { notice_ = "Motion scan limit"; valid = false; }
    } else if (key == '%') {
        auto p = origin;
        while (p < lineEnd(currentLine) && p - origin < scanLimit && std::string("()[]{}").find(static_cast<char>(byte(p))) == std::string::npos) p = next(p);
        target = send(SCI_BRACEMATCH, p, 0);
        valid = p < lineEnd(currentLine) && target >= 0; inclusive = true;
    } else valid = false;
    if (!valid) return {};
    if (op_) {
        if (linewise) return lines(currentLine, lineOf(target));
        Pos first = std::min(origin, target), last = std::max(origin, target);
        if (inclusive && last < lineEnd(lineOf(last))) last = next(last);
        // An exclusive forward word motion ending at column zero omits that line's EOL.
        if ((key == 'w' || key == 'W') && target > origin && lineOf(target) > currentLine && target == lineStart(lineOf(target))) {
            last = lineEnd(lineOf(target) - 1);
            if (origin == lineStart(currentLine)) return lines(currentLine, lineOf(target) - 1);
        }
        return {first, last, false, true};
    }
    position(target, vertical);
    return {origin, target, false, true};
}

VimEditor::Range VimEditor::find(Key kind, Key character, int count, bool repeat) {
    const auto origin = caret(), line = lineOf(origin);
    const auto needle = encode(character);
    Pos p = origin, budget = scanLimit;
    const bool forward = kind == 'f' || kind == 't';
    for (int i = 0; i < count; ++i) {
        bool found = false;
        while (--budget > 0) {
            if (forward) { if (p >= lineEnd(line)) break; p = next(p); }
            else { if (p <= lineStart(line)) break; p = previous(p); }
            if (p >= lineEnd(line)) break;
            bool matches = !needle.empty();
            for (std::size_t n = 0; n < needle.size(); ++n) if (byte(p + n) != static_cast<unsigned char>(needle[n])) matches = false;
            if (matches) {
                // Repeating t/T skips the match immediately beside its previous stop.
                if (repeat && !i && ((kind == 't' && p == next(origin)) || (kind == 'T' && p == previous(origin)))) continue;
                found = true; break;
            }
        }
        if (!found) { notice_ = "Character not found"; return {}; }
    }
    if (kind == 't') p = previous(p);
    if (kind == 'T') p = next(p);
    if (op_) return {std::min(origin, p), forward ? next(p) : next(origin), false, true};
    position(p);
    return {origin, p, false, true};
}

VimEditor::Range VimEditor::textObject(Key key, bool around, int count) {
    auto p = caret();
    if (key == 'w' || key == 'W') {
        const bool big = key == 'W';
        Pos first = p, last = p, budget = scanLimit;
        int kind = wordClass(p, big);
        while (first > lineStart(lineOf(p)) && wordClass(previous(first), big) == kind && budget-- > 0) first = previous(first);
        for (int i = 0; i < count && budget > 0; ++i) {
            kind = wordClass(last, big);
            while (last < length() && wordClass(last, big) == kind && budget-- > 0) last = next(last);
            if (i + 1 < count) while (last < length() && !wordClass(last, big) && budget-- > 0) last = next(last);
        }
        if (around && !kind) {
            kind = wordClass(last, big);
            while (last < length() && wordClass(last, big) == kind && budget-- > 0) last = next(last);
        } else if (around) {
            const auto end = last;
            while (last < lineEnd(lineOf(p)) && space(byte(last)) && budget-- > 0) last = next(last);
            if (end == last) while (first > lineStart(lineOf(p)) && space(byte(previous(first))) && budget-- > 0) first = previous(first);
        }
        return {first, last, false, budget > 0};
    }
    char open = 0, close = 0;
    if (key == '(' || key == ')' || key == 'b') { open = '('; close = ')'; }
    if (key == '[' || key == ']') { open = '['; close = ']'; }
    if (key == '{' || key == '}' || key == 'B') { open = '{'; close = '}'; }
    if (key == '"' || key == '\'' || key == '`') open = close = static_cast<char>(key);
    if (!open) return {};
    const auto lower = open == close ? lineStart(lineOf(p)) : std::max<Pos>(0, p - scanLimit);
    Pos first = -1, last = -1;
    if (open == close) {
        bool escaped = false;
        for (auto at = lower; at < lineEnd(lineOf(p)) && at - lower < scanLimit; at = next(at)) {
            const int c = byte(at);
            if (c == open && !escaped) {
                if (first < 0) first = at;
                else { if (at >= p) { last = at; break; } first = -1; }
            }
            escaped = c == '\\' && !escaped;
        }
    } else {
        int depth = 0;
        for (auto at = p; at >= lower; at = previous(at)) {
            const int c = byte(at);
            if (c == close && at != p) ++depth;
            if (c == open) { if (!depth) { first = at; break; } --depth; }
            if (at == 0) break;
        }
        if (first >= 0) {
            depth = 1;
            for (auto at = next(first); at < length() && at - first < scanLimit; at = next(at)) {
                if (byte(at) == open) ++depth;
                if (byte(at) == close && !--depth) { last = at; break; }
            }
        }
    }
    if (first < 0 || last < 0) return {};
    return {around ? first : next(first), around ? next(last) : last, false, true};
}

}
