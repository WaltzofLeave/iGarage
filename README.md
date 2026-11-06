# iGarage

Platform implementations live under `workbench/<platform>`.

| Platform | Implementation | Build |
| --- | --- | --- |
| Windows x64 | [Orbit Native IDE](workbench/windows/README.md), C++20 / Win32 / Direct2D / Scintilla | `workbench\windows\build.cmd` |

The Windows application, vendored dependency sources, resources, sample project,
and verification records are all contained in `workbench/windows`.
Generated executables, build caches, local projects, and session state are ignored.

From the repository root, on Windows with Visual Studio 2022 C++ tools, a Windows SDK and Python 3.11+ for build preparation:

```bat
workbench\windows\build.cmd
workbench\windows\app\OrbitNative.exe
```

Python execution requires a local Python interpreter. The UI can start without Python.

See [Windows verification](workbench/windows/QA.md) and the
[performance audit](workbench/windows/qa/performance-audit.txt).
The audit is the original baseline. The [navigation update](workbench/windows/docs/pycharm-comparison.md)
addresses cached document activation and closed-tab memory retention. The
[second performance pass](workbench/windows/docs/performance-followup.md) adds
bounded output, indexed project trees, and background checkpoint metadata.
Remaining work is documented in the [architecture notes](workbench/windows/docs/architecture.md).
Third-party license notices are retained alongside their sources and assets.
