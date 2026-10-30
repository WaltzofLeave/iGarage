# Second performance and structure pass

Validated on 2026-10-30, Windows x64 Release, the same Ryzen 7 9800X3D machine.
The default build, 108 checks, and live native UI verification passed.

| Case | Original baseline | Current measurement | Scope |
| --- | --- | --- | --- |
| Warm 10 MiB document activation | about 88 ms | 1.80 ms median, 5 samples | Method timing; visible styling is performed by Scintilla and remaining work is idle-scheduled. |
| 1 MiB single-line Python output | about 7.5 s | 35.9 ms | Full run/dispatch case in an initialized hidden window. Long lines are now visibly truncated at 16 KiB, so this is not equal-volume rendering. |
| 20,000 files / 2,000 expanded folders | about 300 ms per rebuild | 1.51 ms median projection; 9.22 ms to build the index | Index construction occurs on file-set changes; projection reuses it. |
| 50 legacy 1 MiB snapshots | about 370 ms on the UI thread | 0.0127 ms submission; 95.3 ms background completion | Up to ten recent snapshots are inspected; only metadata stays in the list. New snapshots use small metadata sidecars. |

The 4 MiB multiline stress case retained 1,857,920 visible bytes,
below the hard 2 MiB limit. Console undo history remains empty. The real Python
case reached a queue peak of 1,048,576 bytes,
within its 1 MiB capacity. Cancellation was verified while output backpressure was active.

## Structural changes

- `runtime/output_buffer.*` owns thread-safe transport and UTF-8-safe display limiting.
- `runtime/console_view.*` adapts bounded output to Scintilla without owning its window.
- `workspace/project_tree.*` builds membership once and projects visible rows.
- The host owns these components privately and exposes action dispatch/state queries.
- Navigation and editor models remain independent of the native chooser.
- Recoverable deletion cannot enqueue a fresh write that recreates a moved dirty file.

The second live PyCharm comparison included the Markdown file-structure chooser.
The native chooser now shows indentation and class context. The user's PyCharm
project was not modified and its screenshots/source are not part of this repository.

## Verification boundaries

These are method and hidden-window workflow measurements, not GPU completion or
input-to-screen latency. Output measurements are individual controlled runs;
activation and tree projection have five warm samples. The console's documented
line truncation is a behavior change and is visible to the user. Full oversized
output is not automatically saved elsewhere.

The original audit in `qa/performance-audit.*` is retained as historical evidence.
Main-workbench dirty-region rendering, legacy popup font lifetimes, fine-grained
search cancellation, and remaining synchronous save/restore waits are not claimed
to be resolved. Full semantic completion, debugger integration, and Markdown preview
remain outside these two passes.

Raw results are in `qa/performance-tests.json` and `qa/workbench-tests.json`.
