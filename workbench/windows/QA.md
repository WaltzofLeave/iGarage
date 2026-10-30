# Orbit Native 核验记录

核验日期：2026-10-30，Asia/Shanghai。

## Navigation update

The Windows platform directory was rebuilt with the default `build.cmd`, including
the vendored libraries. The 32 baseline checks, 35 navigation/editor checks, and
18 existing integration workflows passed (85 total). Results are in
`qa/workbench-tests.json`, `qa/native-self-test.json`, and `qa/native-integration-test.json`.

Live UI checks at 150% DPI covered Recent Files filtering and its empty state,
Escape/focus restoration, file-outline filtering and Enter navigation, Back to
the previous caret, switching file types, and closing/reopening a tab.
PyCharm's actual Recent Files UI was inspected without modifying its project.

The new five-sample warm 10 MiB activation test records method timings around
1.5 ms on this machine. The earlier audit measured approximately 88 ms with forced
full recolouring. These are method timings, not input-to-screen latency or an
IDE-to-IDE benchmark. Clean closed documents now release their text and editor
reference; dirty ones wait for successful saving.

参考：此前 Orbit 网页版的 `index.html`、`style.css`、`app.js` 与首屏截图。实际桌面核验使用 Windows 原生窗口，在本机 150% DPI 缩放下操作并截图，未以内部布局检查代替真实画面。

## 显示与交互

| 项目 | 核验与结果 |
| --- | --- |
| 全局布局 | 核对标题、菜单、48 DIP 工具栏、默认 238 DIP 项目栏、208 DIP 结构栏、226 DIP 输出面板和 27 DIP 状态栏 |
| 配色 | 沿用 #1e1f22 编辑器、#25262a 面板、#292a2e 标题、#e6ad85 强调色及语法配色；实际检查深浅主题切换 |
| 字体 | DM Sans 与 JetBrains Mono 随包加载；实际检查英文、中文、勾选字符及 Python 输出 |
| 行号与留白 | 修正行号栏白底，核对深色行号背景、首行 14 DIP 留白、代码起点和行高 |
| 编辑标签 | 检查当前标签下划线、关闭区、文件类型图标；补充标签自动滚动，使窄窗口下的活动文件保持可见 |
| 文件树 | 核对目录缩进、展开箭头、选中底色与左侧细线；修正大小写排序，使 pyproject.toml 与 README 顺序对齐 |
| 编辑内容 | 实际创建 native_smoke.py，输入含中文的 Python，多文件导入运行输出 42，检查落盘内容与 UTF-8 一致 |
| 搜索 | 实际搜索 progress_bar，得到 9 项、4 文件的结果并跳至对应行；补充匹配背景和长文本省略 |
| 弹窗 | 检查文件创建、命令面板与设置弹窗的尺寸、边距、输入框、按钮、焦点、Enter 和 Escape；修正 GPU 子窗口上的遮罩层 |
| 输出面板 | 实际运行示例与 4 项单元测试；核对 UTF-8、退出码、耗时、滚动和清空入口 |
| 面板拖动 | 实际拖动侧栏；发现底部分隔线与横向滚动条命中重叠，修复后重新验证底部拖动 |
| 窗口尺寸 | 实际检查约 1400×900 与 1040×900 DIP 窗口，验证编辑器重绘、标题区、右侧结构和输出裁切 |
| GPU 呈现 | 真实窗口暴露旧 HWND / discard 呈现路径的空白问题；切换 DXGI flip sequential 后复核主界面、编辑器与弹窗 |

其他 DPI 比例、多个显示器之间拖动、ARM64、不同显卡和远程桌面未完成实机覆盖。内部布局检查覆盖 640–1920 DIP 宽度，不等同于这些环境的视觉验证。

行距调整复核：根据使用反馈，将代码编辑器在默认 13 DIP 字号下的目标行高由 22 DIP 收紧至 20 DIP；输出面板行距保持原值。在本机 150% DPI 下重新检查行号、折叠箭头、当前行背景与代码文字，同一窗口从约 21 行增至 23 行。重新编译后 32 项基础检查通过，并更新实际截图与交付包。

## 自动检查

- 32 项基础检查：路径、Unicode、Windows 参数转义、18 组布局尺寸、专注模式、Web 项目格式和编辑器 / 字体初始化。
- 18 项隔离流程检查：项目加载、新建、UTF-8 编辑与保存、切换文件保留撤销栈、跨文件运行、异步搜索、检查点不重置编辑文档、恢复与备份、项目导出、重命名、4 项 unittest、错误面板、停止无限循环、标准输入、双向管道压力、换行设置、浅色主题和可恢复删除。
- 具体结果保存在 `qa/native-self-test.json` 与 `qa/native-integration-test.json`。

检查点测试曾提前检查恢复结果；已改为等待文件、编辑器内容及备份列表共同更新，再完成断言。

另外，将源码压缩包解压到全新目录，从第三方库开始重新编译成功，并在重建程序上再次通过基础与流程检查。结果见 `qa/source-build-check.json`。构建脚本会自行建立所需输出目录，不依赖交付环境已有的 obj / bin 缓存。

## 性能检查的范围

`qa/native-benchmark.json` 记录约 10 MiB、272,359 行文本的装载与批量插入耗时，以及该基准进程的工作集和私有提交。它测量未启用语法分析的文本内核，不测量 GPU 输入延迟、项目索引耗时或带语言服务的内存，也不是与其他 IDE 的对比跑分。

此次目标是交付性能开销可控的原生前端，并确保既有 Orbit 流程可用；不将选用 C++ 或 GPU 渲染本身当作“性能第一”的证明。

## Second pass: performance and structure

All 108 checks passed: 32 baseline, 37 navigation/editor, 18 integration, and 21 stress checks. The default platform build completed without compiler warnings. See [the measured follow-up](docs/performance-followup.md), `qa/workbench-followup.json`, and `qa/performance-tests.json`.

## 2026-10-30: folding arrows and licensed icons

All 118 native checks passed: the previous 108 plus 10 icon/rendering/folding/cache
checks in `qa/icon-tests.json`. The updated application compiled without warnings.
The offline provenance gate passed for all 105 imported SVG files; its report is
`qa/icon-license-check.json`. Complete permissions, exceptions and pinned sources
are documented in [the asset review](docs/icon-sources.md).

The live PyCharm window was inspected read-only. Its small open chevrons differ
from Scintilla's original wide outlined triangles. The replacement uses the
official 9×9 fold/unfold artwork, with the original 13 DIP margin and click area.
Both folding states were exercised in the actual Orbit window. The code font and
20 DIP target line height remain unchanged.

At the current 150% display scale, the running application was checked in dark and
light themes: project/file types, tabs, breadcrumb, interpreter selector, structure
symbols, toolbar and Recent Files picker. The old text placeholders are replaced
consistently. Screenshots are `qa/icons-main.png`, `qa/icons-folded.png`,
`qa/icons-light.png`, `qa/icons-recent.png` and `qa/icons-outline.png`.
PyCharm's private project contents are not included in the repository screenshots.

The native SVG tests additionally render 330 registered icon/theme/scale variants
at 100%, 150% and 200%, verify Python's blue/yellow artwork, and check arrow alpha
bounds, actual native folding and warmed cache reuse. The 100% and 200% checks are
offscreen raster tests, not physical-monitor visual coverage. The cache lookup
timing in the report measures CPU access only; it is not GPU latency or an IDE
performance comparison.
