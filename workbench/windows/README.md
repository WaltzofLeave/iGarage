# Orbit Native IDE

面向 Windows 的 C++ 原生 IDE 前端，以此前的 Orbit 网页版为视觉与功能基准。界面不依赖浏览器、WebView、Electron 或网络字体。

## 运行

1. 将便携包完整解压到可写目录。
2. 打开 `OrbitNative.exe`。源码目录中的可执行文件位于 `app/OrbitNative.exe`。
3. 点击绿色运行按钮或按 Shift+F10 运行示例。烧瓶按钮运行 4 项 unittest 测试。
4. 若 Python 不在 PATH 中，在「运行 → Python 解释器」选择本地 `python.exe`，也可选择 `.venv/Scripts/python.exe`。

适用 Windows 10/11 x64；示例项目使用 Python 3.12。C++ 运行库静态链接，无需安装 Visual Studio 即可运行。

首次启动会把 `sample-project/` 复制为程序旁的 `orbit-python/`。项目编辑自动写回磁盘，设置保存在 `data/session.json`。可通过「文件 → 打开项目文件夹」切换项目。

## 与 Orbit 的功能对应

| Orbit 功能 | Windows 原生实现 |
| --- | --- |
| 深色工作台、文件树、编辑标签、面包屑、结构栏、运行面板、状态栏 | Direct2D / DirectWrite 自绘，沿用原始色值与主要尺寸 |
| 语法高亮、折叠、选区、括号匹配、自动缩进、撤销重做 | Scintilla + Lexilla |
| 单词补全、注释切换、查找替换、转到行 | 原生编辑器命令与当前文件词表 |
| 项目搜索、结果跳转、文件搜索、命令面板 | 后台搜索队列与原生弹窗 |
| 新建、重命名、删除、导入、单文件下载 | 本地文件操作；删除移入项目 `.orbit/trash/` |
| 项目导入导出 | 沿用 `orbit-project-v1` JSON 格式，可与网页版互通 |
| 自动保存、本地检查点、恢复 | 后台原子写入；恢复前创建备份 |
| Python 运行、多文件 import、标准输入、停止、单元测试 | 本机 Python、标准流管道、Windows Job Object |
| 项目终端 | help、ls、pwd、cat、clear、python、python -c、python -m unittest |
| 深浅主题、字号、缩进、自动换行、可拖动面板 | 即时应用并保存到本地 |

项目搜索返回前 150 个匹配，跳过超过 16 MiB 的文件；项目扫描上限为 20,000 个支持的文本文件。大于 2 MiB 的文件暂停结构提取和单词补全。检查点列表展示最近 10 个版本。

## 快捷键

| 操作 | 快捷键 |
| --- | --- |
| 运行当前 Python 文件 | Shift+F10 |
| 保存 | Ctrl+S |
| 查找文件 / 命令面板 | Ctrl+P / Ctrl+K |
| 项目搜索 | Ctrl+Shift+F |
| 文件内查找 / 替换 | Ctrl+F / Ctrl+H |
| 转到行 | Ctrl+G |
| 单词补全 | Ctrl+Space |
| 注释切换 | Ctrl+/ |
| 新建文件 | Ctrl+Alt+N |
| 关闭当前标签 / 切换标签 | Ctrl+W / Ctrl+Tab |
| 项目终端 | Alt+F12 |
| 重命名文件 | F2 |

## 编译

安装带有 Desktop development with C++ 的 Visual Studio 2022 和 Windows SDK，在本目录运行：

```bat
build.cmd
```

脚本使用 vswhere 定位 MSVC，编译已随源码提供的依赖，生成 `app/OrbitNative.exe`。编译参数为 x64、C++20、`/O2 /GL /LTCG /MT`。编译无需联网。

```bat
app\OrbitNative.exe --self-test
app\OrbitNative.exe --integration-test
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

本次完成原生前端与 Orbit 网页版已有工作流程。项目终端是内置项目命令环境；完整 ConPTY 系统终端、Git、LSP 语义补全、debugpy 断点调试和插件系统尚未接入。原生文件选择器、窗口控制按钮和部分焦点表现采用 Windows 习惯，不承诺与浏览器逐像素相同。自绘控件的完整 UI Automation / 屏幕阅读器支持仍需补充。

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
