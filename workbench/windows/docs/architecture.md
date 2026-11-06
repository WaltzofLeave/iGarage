# Windows workbench structure

The native Win32 application stays in this platform directory. Navigation and
outline models do not own editor windows; the host adapts them to Scintilla.
Language sessions keep a bounded UTF-8 mirror for protocol position conversion.

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
| `src/ui/icons.*` | Typed asset registry, file/symbol mappings, licensed SVG loading, bounded shared GPU bitmap cache and Scintilla fold images. |
| `src/language/protocol.*` | Window-independent framing, UTF-8/UTF-16 positions, validated completion transactions and hint models. |
| `src/language/client.*` | Hidden Windows process, restricted inherited pipes, bounded queues, worker threads and job-owned lifetime. |
| `src/language/service.*` | LSP capabilities/configuration, incremental document mirrors, cancellation, revision checks and diagnostics. |
| `src/language/workbench_language.cpp` | Scintilla adapter: requests, atomic edits, calltips and Problems panel integration. |
| `src/ui/completion_popup.*` | Nonactivating native candidate/documentation window; retained swap chain and DirectWrite formats. |
| `src/runtime/output_buffer.*` | UI-independent bounded transport, cancellation, and UTF-8-safe line display limits. |
| `src/runtime/console_view.*` | Non-owning Scintilla console adapter: bounded display, styles, viewport following, and disabled undo. |
| `src/workspace/project_tree.*` | UI-independent directory membership index and visible-row projection. |
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
- The graphics device owns a shared icon atlas. Popups reuse it; device loss
  invalidates it. Sources load once, and the GPU cache holds at most 256 bitmap
  variants. Only fold markers normally need CPU RGBA copies. Theme and physical
  pixel size are cache keys, so DPI changes do not stretch an old low-resolution
  icon. Provenance and license validation remain an offline tool responsibility.

## Extending the workbench

Keep state transitions testable without a window. Add editor mutations to the
editor command adapter, not the popup renderer. Choosers receive immutable rows
and return a selected value after closing; they do not open files themselves.
New shortcuts must check editor focus so they cannot modify files while the user
is typing into a search box or terminal input.

The earlier performance audit remains a baseline. The two passes address cached
activation, closed-document retention, indexed tree projection, bounded process
output, and background checkpoint listing. Main-workbench repaint regions and
remaining synchronous file-operation waits still need separate work. This is a
native workbench update. Python now connects to a separate language server;
semantic refactoring and debugging remain future work.

## Language-service boundaries

The UI thread owns `Service` and document revisions. It never performs pipe I/O
or waits for analysis. `Client` launches the process on a worker, serializes and
writes on a second worker, and drains stderr on a third. Its wake callback only
posts a window message. A 100 ms maintenance timer handles failure/timeouts; normal
responses wake the UI immediately. The analysis engine uses two worker threads.

Each edit is converted using the old text range before updating the mirror. The
host sends incremental changes and cancels obsolete requests. Results must match
the active document, revision, caret and focus. Closing, switching, Escape and
focus loss cancel hints. A lazy completion resolve completes before accepting an
item so its import edits are retained. All offsets/overlaps are checked before
opening one Scintilla undo group. Closing a document releases its mirror and
diagnostics; changing projects or interpreter replaces the job-owned process.

Mirrors are capped at 64 documents × 2 MiB. Incoming/outgoing queue budgets are
16 MiB, also capped at 128 events / 512 requests; overflow fails analysis rather
than blocking the editor. Framing limits are 8 KiB headers and 16 MiB bodies.
Completion models keep at most 500 candidates, diagnostics 200 per document.
Provider requests expire at 10 seconds; initialization expires at 15 seconds.
Large files remain editable while semantic analysis is paused. The process's
total memory is not capped; its project/import work may use additional memory.

The completion window hides between requests rather than recreating its window,
swap chain or font formats for every keystroke. It borrows the shared icon atlas.
Signature and hover calltips stay native to Scintilla. Calltips wrap with a byte
mapping that preserves Unicode and active-argument highlighting, capped at
2,200 bytes and 16 lines. Neither surface activates a modal overlay.

## Output and checkpoint budgets

The process output transport has a 1 MiB capacity. A full queue applies backpressure;
stopping the process wakes blocked writers. The UI consumes at most 256 KiB per
timer tick, approximately every 16 ms while a process runs. Process completion
drains the remaining bounded data before displaying its final status.

Each console displays at most 2 MiB and disables undo history. Individual physical
lines display at most 16 KiB, followed by an explicit truncation marker. Display
truncation is intentional: complete oversized output is not retained in the UI
or automatically saved elsewhere. Vertical following does not move the caret to
the end of a growing line. Old console text is removed in amortized blocks.

Project directory membership is indexed on file-set changes. Expanding directories
projects rows from that index. Checkpoint listing selects up to ten recent snapshots
on a worker and retains only metadata. New snapshots have small metadata sidecars;
legacy snapshots can be read on the worker. Restore still loads the selected
snapshot and performs its backup/write workflow, so it is not claimed to be fully
asynchronous.
