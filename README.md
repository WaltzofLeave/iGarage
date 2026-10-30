# iGarage

Platform implementations live under `workbench/<platform>`.

| Platform | Implementation | Build |
| --- | --- | --- |
| Windows x64 | [Orbit Native IDE](workbench/windows/README.md), C++20 / Win32 / Direct2D / Scintilla | `workbench\windows\build.cmd` |

The Windows application, vendored dependency sources, resources, sample project,
and verification records are all contained in `workbench/windows`.
Generated executables, build caches, local projects, and session state are ignored.

From the repository root, on Windows with Visual Studio 2022 C++ tools and a Windows SDK:

```bat
workbench\windows\build.cmd
workbench\windows\app\OrbitNative.exe
```

Python execution requires a local Python interpreter. The UI can start without Python.

See [Windows verification](workbench/windows/QA.md) and the
[performance audit](workbench/windows/qa/performance-audit.txt).
The audit records known issues; its proposed performance fixes are not implemented yet.
Third-party license notices are retained alongside their sources and assets.
