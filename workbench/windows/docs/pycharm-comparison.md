# PyCharm comparison: navigation and editing update

The comparison used the user's running PyCharm window. The actual editor, tab
strip, project tree, and Recent Files chooser were inspected. The PyCharm project
was not edited or included in this repository. Features beyond those screens are
workflow additions, not a claim of complete PyCharm compatibility.

A second pass inspected PyCharm's Markdown structure chooser and preview layout.
Orbit's outline now indents nested entries and includes their class context in
search results. Markdown preview remains outside this update; the existing source
editor and heading navigation are retained.

| Workflow | Windows workbench behavior |
| --- | --- |
| Recent Files | `Ctrl+E`: filter recent paths; the previous file is initially selected when available; arrows and Enter choose; Escape restores focus. |
| Back / Forward | `Ctrl+Alt+Left/Right`: restore file, main selection, and viewport. A new jump discards the abandoned forward branch. |
| Reopen a closed tab | `Ctrl+Shift+T`: reload the saved file and restore its cursor and original tab position. Already open or deleted paths are skipped. |
| File structure | `Ctrl+F12`: filter classes, functions, or Markdown headings and jump to a line. This is a lexical outline. |
| Tab management | Right-click a tab to close it, close other tabs, close all tabs, or reopen a closed tab. |
| Duplicate | `Ctrl+D`: duplicate the selection, or the current line when no text is selected. |
| Delete lines | `Ctrl+Y`: delete selected lines as a single undoable operation. Redo is `Ctrl+Shift+Z`. |
| Move / join lines | `Alt+Shift+Up/Down` and `Ctrl+Shift+J`. |
| Select next occurrence | `Alt+J`: add another selection using the selected text. With an empty selection, first select the word around the caret. |
| Newline handling | Line commands and typed newlines preserve the file's existing LF, CRLF, or CR convention. New single-line files default to LF. |

Navigation actions also appear in the Navigate menu and command palette.
The existing `Ctrl+P` file search and `Ctrl+K` command palette remain available.

## Verification

Run from this directory after building:

```bat
app\OrbitNative.exe --self-test
app\OrbitNative.exe --workbench-test
app\OrbitNative.exe --integration-test
app\OrbitNative.exe --performance-test
```

`--workbench-test` uses an isolated sample project and records its results and
warm document-activation samples in `app/workbench-tests.json`. Method timing
does not measure input-to-screen latency. UI screenshots and the final checks
are recorded under `qa/`.
