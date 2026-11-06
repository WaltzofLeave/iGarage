# Python semantic hints on Windows

Orbit uses its existing C++20/Scintilla frontend with a separate native
[Pyrefly 1.4.1](https://github.com/meta/pyrefly/tree/1.4.1) executable. This adds
semantic completion, completion resolve/auto-import, active-argument signature
help, hover documentation and versioned diagnostics through LSP. The backend
is written in Rust and distributed unchanged; Orbit implements the client and
native UI. It does not reproduce PyCharm's PSI/index internals or its proprietary
models. There is no AI completion request or cloud connection.

## Interaction

- `Ctrl+Space` requests semantic candidates. Typing a member dot or word also
  debounces a request. Up/Down and PageUp/PageDown select; Enter/Tab accepts.
- Candidates show the licensed symbol icon, detail and documentation. Resolve
  runs for the selected item and finishes before acceptance when supported.
- Auto-import edits are validated against the same revision, applied from the
  end of the document, and grouped with the main replacement for one undo.
- `Ctrl+Shift+Space`, `(` and `,` request parameter help. The active argument is
  highlighted. `Ctrl+Q` or a 500 ms hover shows type/doc or a diagnostic.
- Errors and warnings receive editor squiggles. Problems lists source/line and
  message; selecting a row navigates there. Existing runtime errors remain.
- Escape, focus loss and switching documents dismiss hints; old responses cannot
  change a departed document. Tools → Restart Code Analysis restarts the backend.

The selected interpreter is passed to Pyrefly for environment/import resolution.
Project configuration remains authoritative. For unconfigured projects Orbit
selects the basic diagnostic preset. Real project dependencies must be installed
in the chosen environment; bundled type stubs are not executable Python packages.
Workspace auto-import indexing is lazy/background and limited to 2,000 files;
early requests may omit candidates until that indexing finishes. Pyrefly also
uses a minimum typed prefix for auto-imports. Orbit preserves server sort order
within its bounded list and filters the current prefix; it does not add a learned
ranking model or claim completion parity with PyCharm.

See [architecture.md](architecture.md) for lifetimes and budgets. Native tests use
a real separate server and isolated files, including cross-file class members,
stdlib members absent from the document, incremental edits, auto-import resolve,
active parameters, diagnostics, cancellation, process exit and Scintilla undo.
Timing measurements cover local request/response handling on that small fixture;
they exclude GPU presentation and are not a comparison with other IDEs.

## Reproducible preparation and redistribution

`tools/fetch_pyrefly.py` reads `vendor/pyrefly/manifest.json`. It verifies the
official Windows x64 release ZIP and extracted binary independently using SHA-256,
then installs only the named executable and reviewed notices under
`app/services/pyrefly/`. It never executes an installer or changes system settings.
The archive is cached under ignored `build/downloads/`; `--offline` refuses to
download. The portable distribution includes the service and all these notices.

Pyrefly's top-level license is MIT. Upstream's release ZIP contains only the
executable, so Orbit additionally supplies full dependency and stub notices.
`dependency-inventory.json` records the exact versions, source archives and
Cargo.lock checksums of all 443 registry packages in the upstream lockfile. This
is a conservative superset, including development and other-platform dependencies;
it is not a binary link map. Packaged notices were retained, missing workspace
licenses recovered from versioned upstream sources or the published SPDX grant,
and bundled stub licenses checked against the pinned upstream tree. Typeshed's
source archive was checked against Pyrefly's recorded checksum.

The reviewed grants allow commercial redistribution subject to their conditions:
MIT/Apache, BSD, ISC, Zlib, Unicode, 0BSD, BSL, CC0 and Unlicense; full text and
attribution are provided. For dual licensing, the selected permissive alternatives
are explained in `THIRD-PARTY-LICENSES.txt`, including configparser's MIT and
r-efi's Apache alternatives. No noncommercial license was selected.

`colored 3.1.1` uses MPL-2.0 and is unmodified. Its complete MPL text and a
versioned [corresponding source archive](https://static.crates.io/crates/colored/colored-3.1.1.crate)
are supplied to recipients in the notices. This component's source obligations
do not change the application source license. Stub licenses include MIT, BSD-3
and Typeshed's Apache-2.0. Existing icon/font licenses remain independently
applicable. The checksum gate protects reviewed bytes; it is not a substitute
for reviewing licenses when changing the pinned backend or dependencies.

Primary implementation references:

- [Pyrefly IDE/LSP configuration](https://pyrefly.org/en/docs/IDE/)
- [Pinned Pyrefly source and MIT license](https://github.com/meta/pyrefly/tree/1.4.1)
- [LSP 3.17 specification](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/)
