#include "commands.h"

#include "../../vendor/scintilla/include/Scintilla.h"

namespace orbit {

void executeEditorCommand(HWND editor, EditorCommand command) {
    const auto send = [editor](unsigned message, uptr_t w = 0, sptr_t l = 0) {
        return SendMessageW(editor, message, w, l);
    };
    if (command == EditorCommand::Undo) { send(SCI_UNDO); return; }
    if (command == EditorCommand::Redo) { send(SCI_REDO); return; }
    if (command == EditorCommand::SelectNextOccurrence) {
        const auto flags = send(SCI_GETSEARCHFLAGS);
        const auto targetStart = send(SCI_GETTARGETSTART);
        const auto targetEnd = send(SCI_GETTARGETEND);
        send(SCI_SETSEARCHFLAGS, SCFIND_MATCHCASE);
        send(SCI_SETTARGETSTART, 0);
        send(SCI_SETTARGETEND, send(SCI_GETLENGTH));
        send(SCI_MULTIPLESELECTADDNEXT);
        send(SCI_SETSEARCHFLAGS, flags);
        send(SCI_SETTARGETSTART, targetStart);
        send(SCI_SETTARGETEND, targetEnd);
        return;
    }

    send(SCI_BEGINUNDOACTION);
    switch (command) {
    case EditorCommand::Duplicate:
        send(SCI_SELECTIONDUPLICATE);
        break;
    case EditorCommand::DeleteLines: {
        const auto start = send(SCI_GETSELECTIONSTART);
        const auto end = send(SCI_GETSELECTIONEND);
        const auto first = send(SCI_LINEFROMPOSITION, start);
        auto last = send(SCI_LINEFROMPOSITION, end);
        if (last > first && end == send(SCI_POSITIONFROMLINE, last)) --last;
        if (first == last) {
            send(SCI_LINEDELETE);
        } else {
            const auto afterLast = send(SCI_POSITIONFROMLINE, last + 1);
            send(SCI_SETTARGETSTART, send(SCI_POSITIONFROMLINE, first));
            send(SCI_SETTARGETEND, afterLast < 0 ? send(SCI_GETLENGTH) : afterLast);
            send(SCI_REPLACETARGET, 0, reinterpret_cast<sptr_t>(""));
        }
        break;
    }
    case EditorCommand::JoinLines: {
        const auto start = send(SCI_GETSELECTIONSTART);
        auto end = send(SCI_GETSELECTIONEND);
        if (start == end) end = send(SCI_GETLINEENDPOSITION, send(SCI_LINEFROMPOSITION, end) + 1);
        if (end >= 0) {
            send(SCI_SETTARGETSTART, start);
            send(SCI_SETTARGETEND, end);
            send(SCI_LINESJOIN);
        }
        break;
    }
    case EditorCommand::MoveLinesUp:
        send(SCI_MOVESELECTEDLINESUP);
        break;
    case EditorCommand::MoveLinesDown:
        send(SCI_MOVESELECTEDLINESDOWN);
        break;
    default:
        break;
    }
    send(SCI_ENDUNDOACTION);
}

} // namespace orbit
