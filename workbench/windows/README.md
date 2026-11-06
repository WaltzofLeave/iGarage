# Orbit Native IDE

面向 Windows 的 C++ 原生 IDE 前端，以此前的 Orbit 网页版为视觉与功能基准。界面不依赖浏览器、WebView、Electron 或网络字体。

## 运行

1. 将便携包完整解压到可写目录。
2. 打开 `OrbitNative.exe`。源码目录中的可执行文件位于 `app/OrbitNative.exe`。
3. 点击绿色运行按钮或按 Shift+F10 运行示例。烧瓶按钮运行 4 项 unittest 测试。
4. 若 Python 不在 PATH 中，在「运行 → Python 解释器」选择本地 `python.exe`，也可选择 `.venv/Scripts/python.exe`。

适用 Windows 10 1703 或更新版本、Windows 11 x64；示例项目使用 Python 3.12。C++ 运行库静态链接，无需安装 Visual Studio 即可运行。

首次启动会把 `sample-project/` 复制为程序旁的 `orbit-python/`。项目编辑自动写回磁盘，设置保存在 `data/session.json`。可通过「文件 → 打开项目文件夹」切换项目。

原生 Vim 模式可在「工具 → Vim 模式」开启。支持常用模式编辑、操作符／文本对象、寄存器、宏和重复；不嵌入或启动其他编辑器。完整支持范围与边界见 [Vim 模式说明](docs/vim-mode.md)。

## 与 Orbit 的功能对应

| Orbit 功能 | Windows 原生实现 |
| --- | --- |
| 深色工作台、文件树、编辑标签、面包屑、结构栏、运行面板、状态栏 | Direct2D / DirectWrite 自绘，沿用原始色值与主要尺寸 |
| 语法高亮、折叠、选区、括号匹配、自动缩进、撤销重做 | Scintilla + Lexilla |
| Python 语义补全、参数提示、快速文档、实时诊断 | 原生 Pyrefly 子进程，通过增量 LSP 与 Scintilla 集成 |
| 其他语言单词补全、注释切换、查找替换、转到行 | 原生编辑器命令与当前文件词表 |
| 项目搜索、结果跳转、文件搜索、命令面板 | 后台搜索队列与原生弹窗 |
| 新建、重命名、删除、导入、单文件下载 | 本地文件操作；删除移入项目 `.orbit/trash/` |
| 项目导入导出 | 沿用 `orbit-project-v1` JSON 格式，可与网页版互通 |
| 自动保存、本地检查点、恢复 | 后台原子写入；恢复前创建备份 |
| Python 运行、多文件 import、标准输入、停止、单元测试 | 本机 Python、标准流管道、Windows Job Object |
| 项目终端 | help、ls、pwd、cat、clear、python、python -c、python -m unittest |
| 深浅主题、字号、缩进、自动换行、可拖动面板 | 即时应用并保存到本地 |

项目搜索返回前 150 个匹配，跳过超过 16 MiB 的文件；项目扫描上限为 20,000 个支持的文本文件。大于 2 MiB 的文件暂停结构提取与语义分析。检查点列表展示最近 10 个版本。

## 快捷键

| 操作 | 快捷键 |
| --- | --- |
| 运行当前 Python 文件 | Shift+F10 |
| 保存 | Ctrl+S |
| 查找文件 / 命令面板 | Ctrl+P / Ctrl+K |
| 项目搜索 | Ctrl+Shift+F |
| 文件内查找 / 替换 | Ctrl+F / Ctrl+H |
| 转到行 | Ctrl+G |
| Python 语义补全 / 其他语言单词补全 | Ctrl+Space |
| 函数参数提示 | Ctrl+Shift+Space |
| 快速文档 | Ctrl+Q，或鼠标停留 500 ms |
| 注释切换 | Ctrl+/ |
| 新建文件 | Ctrl+Alt+N |
| 关闭当前标签 / 切换标签 | Ctrl+W / Ctrl+Tab |
| 项目终端 | Alt+F12 |
| 重命名文件 | F2 |

## 编译

安装带有 Desktop development with C++ 的 Visual Studio 2022、Windows SDK 和 Python 3.11+，在本目录运行：

```bat
build.cmd
```

脚本使用 vswhere 定位 MSVC，编译已随源码提供的静态库，生成 `app/OrbitNative.exe`。编译参数为 x64、C++20、`/O2 /GL /LTCG /MT`。首次构建会从官方 release 下载固定版本的 Pyrefly 并校验归档与可执行文件的 SHA-256；缓存准备后可离线构建。也可提前运行 `python tools/fetch_pyrefly.py`，用 `--offline` 检查离线准备。`ORBIT_PYTHON` 可指定构建脚本使用的 Python 路径。

```bat
app\OrbitNative.exe --self-test
app\OrbitNative.exe --integration-test
app\OrbitNative.exe --language-test
app\OrbitNative.exe --vim-test
app\OrbitNative.exe --benchmark
```

分别生成布局与基础检查、隔离项目流程检查、编辑器文本内核基准 JSON。集成测试使用独立的 `integration-fixture-*` 文件夹，不修改用户打开的项目。

## 性能与实现

- 界面按事件重绘，没有常驻动画刷新循环。
- Direct3D 11 + Direct2D 1.1 + DXGI 翻转呈现；编辑器同样使用 DirectWrite / DXGI。
- 后台处理文件读取、保存和项目搜索；搜索支持取消过期请求。
- 编辑器保留独立文档与撤销栈，避免切换标签时重新复制全部文本。
- Python 在独立进程组中运行；输入和输出分别处理，避免管道互相阻塞。
- 字体从程序资产目录加载，不安装或修改系统字体。

已测结果与显示核验记录见 `QA.md` 和 `qa/`。基准是文本内核操作，不代表完整文件打开、语法分析或键入到屏幕呈现的端到端延迟。

## 当前边界

本次完成原生前端与 Orbit 网页版已有工作流程，并接入 Python LSP 语义提示。项目终端是内置项目命令环境；完整 ConPTY 系统终端、Git、语义重构、debugpy 断点调试和插件系统尚未接入。原生文件选择器、窗口控制按钮和部分焦点表现采用 Windows 习惯，不承诺与浏览器逐像素相同。自绘控件的完整 UI Automation / 屏幕阅读器支持仍需补充。

Orbit 与 JetBrains / PyCharm 无关联。第三方许可证随便携包及源码提供。

## Repository location

This Windows implementation is self-contained under `workbench/windows`. From the repository root, run `workbench\windows\build.cmd`; the executable is generated at `workbench/windows/app/OrbitNative.exe`. The original performance baseline is retained in [qa/performance-audit.txt](qa/performance-audit.txt) and [qa/performance-audit.json](qa/performance-audit.json).

## Navigation and editing update

Recent Files (`Ctrl+E`), searchable file structure (`Ctrl+F12`), Back/Forward
(`Ctrl+Alt+Left/Right`), reopening closed tabs (`Ctrl+Shift+T`), and tab context
menus are available. Cursor, selection, and viewport state follow document navigation.

Editing commands include duplicate (`Ctrl+D`), delete lines (`Ctrl+Y`), move lines
(`Alt+Shift+Up/Down`), join lines (`Ctrl+Shift+J`), select the next occurrence
(`Alt+J`), and redo (`Ctrl+Shift+Z`). Commands preserve existing newline conventions.

See the [workflow comparison](docs/pycharm-comparison.md) and
[code structure](docs/architecture.md). Run `app\OrbitNative.exe --workbench-test`
for the new isolated checks. `build.cmd --app-only` skips dependency rebuilding
when the existing vendored static libraries are already available.

## Second performance and structure pass

Process output uses a bounded 1 MiB queue with backpressure and batched UI updates.
The visible console is limited to 2 MiB; physical lines display up to 16 KiB with
an explicit truncation marker. Oversized complete logs are not saved elsewhere.
Console undo is disabled, and following output does not repeatedly reposition the
caret at the end of a long line.

Directory expansion uses a cached membership index. Checkpoint listing runs on a
worker and keeps metadata only; restoration loads the selected snapshot separately.
Run `app\OrbitNative.exe --performance-test` for stress checks, cancellation,
UTF-8 boundaries, memory limits, and recorded method-level timings. See
[the measured follow-up](docs/performance-followup.md) and `qa/performance-tests.json`.

## Icons and folding controls

File types, folders, symbols, tool windows, tabs and pickers use a shared native
icon registry. Folding uses the official small 9×9 chevrons while retaining a
13 DIP click area. SVGs are rasterized once per size/theme/color and cached as
GPU bitmaps; the application does not fetch icons from the network.

104 original SVG files carry explicit Apache-2.0 notices. The unaltered official
Python logo is used only for file/interpreter identification under the PSF's
published permission for commercial and non-commercial compatibility uses.
Licenses, notices, pinned source URLs and SHA-256 hashes ship with the program.
See [the asset review](docs/icon-sources.md). The Python logo is not relicensed
under Apache-2.0, and no JetBrains/PyCharm product branding is included.

Run `python tools/check_icon_licenses.py` for the offline asset gate and
`app\OrbitNative.exe --icon-test` for native SVG/DPI/folding/cache checks.

## Python semantic hints

Python completion now resolves standard-library and imported project types rather
than relying on words in the current file. The native chooser shows symbol kinds,
signatures and documentation; Enter/Tab inserts and Escape dismisses. Automatic
imports are validated before editing and share one undo transaction with the
completed identifier. Parameter help highlights the active argument. Hover/Ctrl+Q
shows inferred types or a diagnostic; the Problems panel lists diagnostics and can
jump to their source line. Tools → Restart Code Analysis recovers the service.

The bundled Pyrefly 1.4.1 process uses two analysis threads, background indexing
limited to 2,000 workspace files, and the selected Python interpreter. Analysis
mirrors at most 64 opened Python documents, each at most 2 MiB; transport queues
have 16 MiB budgets. Runtime startup and analysis are asynchronous. No AI service,
runtime download, Node.js runtime, or WebView is required. First completion may
wait for imports/indexing; it is not claimed to be instant on large projects.

See [the design and redistribution review](docs/python-language-service.md),
`qa/language-tests.json`, and [the architecture](docs/architecture.md).

Build a checked portable ZIP with `python tools/package_portable.py --output build/Orbit-Workbench-Windows-x64.zip`.

On Windows, packaging also extracts a ready-to-run copy under **Desktop → Orbit
IDE → Orbit-Windows-<date>-<binary hash>**. Desktop lookup honors Windows folder
redirection/OneDrive. Each delivery uses a fresh directory, preserving earlier
versions, settings and projects. Use `--no-desktop` for CI or verification-only
packaging. Reports include the exact delivered folder.

## Analysis frontend/backend boundary

The editor now injects a `Transport` into an OS-independent LSP session. Backend
configuration and Windows/POSIX workspace URI mapping are separate modules, so
a future remote connector can reuse completion, diagnostics and editing logic.
The current application continues to use local Pyrefly; no server is configured
or contacted in this stage. Project synchronization and a production connection
module remain future work, as requested.

`build.cmd --language-core-test` builds the session/protocol contract checks
without GUI, Scintilla, the Windows process adapter or Pyrefly. The normal
`--language-test` includes these 24 simulated endpoint checks and the existing
47 real-server/native-editor checks. See [the interface contract](docs/language-service-contract.md).
Run `python -m unittest discover -s tools -p test_portable_delivery.py` for the
five extraction/preservation checks.
