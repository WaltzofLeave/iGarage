#pragma once

#include <windows.h>

namespace orbit {

enum class EditorCommand {
    Duplicate,
    DeleteLines,
    JoinLines,
    MoveLinesUp,
    MoveLinesDown,
    SelectNextOccurrence,
    Undo,
    Redo
};

// Commands operate on the editor only. Shortcut focus checks belong to the host.
void executeEditorCommand(HWND editor, EditorCommand command);

} // namespace orbit
