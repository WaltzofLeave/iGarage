#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "../../vendor/scintilla/include/Scintilla.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace orbit {
// UI-thread-only controller. Scintilla continues to own text, rendering and undo.
class VimEditor {
public:
    using Key = std::int32_t;
    enum Special : Key {
        Escape = -1, Enter = -2, Backspace = -3, Delete = -4,
        Left = -5, Right = -6, Up = -7, Down = -8, Home = -9, End = -10,
        Tab = -11, Redo = -12, PageUp = -13, PageDown = -14,
        HalfUp = -15, HalfDown = -16
    };
    enum class Mode { Normal, Insert, Visual, VisualLine, Command };
    void attach(HWND editor);
    void enable(bool enabled);
    bool enabled() const { return enabled_; }
    Mode mode() const { return mode_; }
    void feed(Key key);
    // Before every document-pointer switch, including async loads.
    void leaveDocument();
    void pointerInput();
    void refreshCaret();
    std::string status() const;
    std::string takeHostCommand();
    std::uint64_t copiedBytes() const { return copiedBytes_; }
    static constexpr std::size_t registerLimit = 4 * 1024 * 1024;
    static constexpr std::size_t sequenceLimit = 65536;
    static constexpr int countLimit = 10000;
private:
    using Pos = sptr_t;
    struct Range { Pos start = 0, end = 0; bool line = false, valid = false; };
    struct Register { std::shared_ptr<const std::string> text; bool line = false; };
    HWND editor_ = nullptr;
    SciFnDirect direct_ = nullptr;
    sptr_t context_ = 0;
    bool enabled_ = false, undoOpen_ = false, changed_ = false, replaying_ = false;
    bool sequenceOverflow_ = false, recording_ = false, repeatingDot_ = false;
    Mode mode_ = Mode::Normal;
    int count_ = 0, operatorCount_ = 1, insertCount_ = 1, playbackDepth_ = 0;
    std::size_t playbackBudget_ = 0, playbackBytes_ = 0;
    Key pending_ = 0, op_ = 0, findKind_ = 0, findChar_ = 0;
    char selectedRegister_ = '"', recordingRegister_ = 0, lastMacro_ = 0;
    Pos visualAnchor_ = 0, visualCaret_ = 0, desiredColumn_ = -1, commandOrigin_ = 0;
    std::map<char, Register> registers_;
    std::map<char, std::vector<Key>> macros_;
    std::vector<Key> sequence_, lastChange_, recordingKeys_;
    std::string commandLine_, search_, notice_, hostCommand_;
    Key commandKind_ = 0;
    bool searchForward_ = true, searchWord_ = false;
    std::uint64_t copiedBytes_ = 0;
    Pos send(unsigned message, uptr_t w = 0, sptr_t l = 0) const;
    Pos length() const;
    Pos caret() const;
    Pos lineOf(Pos p) const;
    Pos lineStart(Pos line) const;
    Pos lineEnd(Pos line) const;
    Pos next(Pos p) const;
    Pos previous(Pos p) const;
    Pos normalPosition(Pos p) const;
    int byte(Pos p) const;
    int wordClass(Pos p, bool big) const;
    std::string text(Pos first, Pos last);
    std::string eol() const;
    void position(Pos p, bool vertical = false);
    void selectVisual();
    Range visualRange() const;
    Range lines(Pos first, Pos last) const;
    Range motion(Key key, int count);
    Range textObject(Key key, bool around, int count);
    Range find(Key kind, Key character, int count, bool repeat = false);
    void apply(Key operation, Range range);
    bool store(Range range, bool yank);
    void paste(bool before, int count);
    void replace(Range range, const std::string& value);
    void beginChange();
    void finishChange();
    void insert(Key key);
    void enterInsert(int count = 1);
    void resetCommand();
    void normal(Key key);
    void command(Key key);
    bool search(bool forward, int count);
    void play(const std::vector<Key>& keys, int count);
    void remember(Key key);
    void dot(int count, bool explicitCount);
    static std::string encode(Key character);
};
}
