# Windows workbench structure

The native Win32 application stays in this platform directory. Domain state does
not hold Windows handles or editor text; the host adapts that state to Scintilla.

| Location | Responsibility |
| --- | --- |
| `src/navigation/history.*` | Bounded Back/Forward locations, recent paths, and closed-tab records. Pure C++ with no Windows APIs. |
| `src/navigation/workbench_navigation.cpp` | Application adapter: capture/restore editor views, dispatch navigation, and release saved closed documents. |
| `src/editor/view_state.h` | UTF-8 byte positions and viewport state. |
| `src/editor/document.h` | Document ownership, appearance signature, and revision-keyed outline cache. |
| `src/editor/outline.*` | Pure lexical outline and completion-word analysis for Python, JavaScript, and Markdown. |
| `src/editor/commands.*` | Editor-only line/selection operations, grouped for undo. |
| `src/editor/text_format.h` | Newline detection without rewriting existing content. |
| `src/ui/quick_pick.*` | Reusable native filtered list. Owns its popup, input font, brush, and rendering resources. |
| `src/workspace.cpp` | File loading/saving, project state, and document activation. |
| `src/runtime.cpp` | Python process management and output. |
| `src/tests/workbench_tests.cpp` | Pure model checks plus isolated native-editor workflows. |

## Lifetimes and limits

- Back/Forward contains at most 128 locations; Recent Files at most 64 paths;
  reopening uses at most 20 closed-tab locations. These records never store text.
- Each open document owns one Scintilla document reference. Switching tabs keeps
  that reference and its undo history. Closing a clean document releases it.
- Dirty closed documents stay alive until their queued save succeeds. Failed
  saves retain the document so its contents remain recoverable.
- Cursor, main selection, vertical scroll, and horizontal scroll are restored on
  activation. Recent paths and bounded view records are persisted per project.
- Scintilla styles are view state, while lexers belong to documents. Both the
  active view appearance and document initialization are checked before skipping
  configuration; switching languages must reapply the view's style table.
- Full-file recolouring is not forced on activation. Visible text is styled by
  Scintilla, with remaining styling scheduled for idle time.
- Outline analysis is revision-cached and pauses above 2 MiB. The length budget
  is checked before creating a text snapshot.

## Extending the workbench

Keep state transitions testable without a window. Add editor mutations to the
editor command adapter, not the popup renderer. Choosers receive immutable rows
and return a selected value after closing; they do not open files themselves.
New shortcuts must check editor focus so they cannot modify files while the user
is typing into a search box or terminal input.

The earlier performance audit remains a baseline. This update addresses cached
activation, closed-document retention, and unnecessary tree rebuilding during
navigation. Long-line process output and synchronous checkpoint loading still
need separate work. This is a native editing/navigation update, not a language
server, semantic refactoring engine, or debugger implementation.
