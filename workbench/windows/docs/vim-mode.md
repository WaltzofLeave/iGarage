# 原生 Vim 编辑模式

在「工具 → Vim 模式」或「设置 → Vim 模式」开启；命令面板搜索 `Vim` 也能切换。
默认关闭，开关随工作区设置保存。状态栏显示 NORMAL、INSERT、VISUAL、录制状态和待完成的命令；点击该区域可退出 Vim 模式。

这是 Orbit 内部的 C++ 模式编辑控制器，使用同一个 Scintilla 文档、DirectWrite 渲染和撤销栈。
不启动 Vim/gVim/Neovim，不挂接其他程序窗口，不使用浏览器或脚本插件实现按键。
本阶段实现常用 Vim 编辑子集，不提供完整 Vim 兼容。

## 支持的操作

| 功能 | 按键 |
| --- | --- |
| 模式 | `Esc` / `Ctrl-[` / `Ctrl-C` 返回 Normal；`i I a A o O` 进入 Insert；`v V` 进入字符／整行 Visual |
| 移动 | `h j k l`、方向键、`0 ^ $ _`、`w b e W B E`、`gg G`、`%` |
| 翻页 | `Ctrl-F/B`、`Ctrl-D/U`、PageUp/Down；Insert 使用方向键、Home/End、PageUp/Down |
| 字符查找 | `f F t T` + 字符，`; ,` 重复／反向；支持 Unicode 目标字符 |
| 操作符 | `d c y > <` + 移动；`dd cc yy >> <<`；操作符前后的计数相乘，例如 `2d3w` |
| 文本对象 | `iw aw iW aW`；`i/a` + `() [] {} b B` 或单引号、双引号、反引号；成对括号支持嵌套 |
| 常用修改 | `x X s D C Y`、`r` + 字符、`J`、`p P`、`u` / `Ctrl-R` |
| 寄存器 | 默认未命名、`a-z`、`A-Z` 追加、`0` 最近复制、`1` 最近整行删除、`-` 最近字符删除、`_` 黑洞；使用 `"` + 寄存器名选择 |
| 重复 | `.` 重放最近一次从 Normal 开始的修改，包含插入内容；支持计数插入和重复计数 |
| 宏 | `qa` 开始录到 a，Normal 下 `q` 停止，`@a` 执行，`@@` 重复；可带计数 |
| 搜索 | `/`、`?` 输入字面文本，Enter 确认，`n N` 重复，`* #` 搜索光标所在的整词；环绕查找 |
| 命令 | `:w`、`:wa`、`:q`、`:wq`、`:x`、`:bn`、`:bp`、`:数字`、`:$`、`:set novim` |

Insert 使用现有文件的 LF/CRLF/CR，Enter 保留行缩进，Tab 按设置插入空格。
不自动补齐括号或弹出补全列表。代码诊断和通用语言服务接口仍可独立工作。
此模式不新增终端或 IME 集成。编辑命令按 UTF-8 码点移动，不把多字节中文、emoji 或 CRLF 拆开。

Vim 快捷键只在代码编辑器获得焦点时生效。`Ctrl-S`、`Ctrl-P/K`、`Ctrl-Tab`、`Ctrl-W`、`Ctrl-Shift-F` 和 Alt／功能键仍走工作台。
工具菜单、设置、命令面板和项目搜索输入不受 Vim 按键影响。鼠标或焦点变化会取消未完成的命令并结束当前插入的撤销事务；Insert 模式保留。
切换标签或项目会返回 Normal，结束旧文档的插入事务；寄存器、已录好的宏和最近修改可继续使用。

`:q` 关闭当前标签，沿用 Orbit 自动保存与失败保留机制；`:wq` 先提交保存再关闭。
它们不关闭 IDE。这里没有放弃未保存内容的 `:q!` 或执行 shell 的 `:!`。

## 当前边界

不支持 Vimscript、`.vimrc`、插件、自定义映射、Visual Block、Replace 模式、标记／跳转栈、正则搜索、Ex 范围／替换、剪贴板寄存器及完整编号寄存器历史。
Visual 支持选择、文本对象、删除、修改、复制和缩进；Visual 中粘贴和 Visual 修改后的 `.` 暂未实现。
不支持的组合取消待输入命令；不支持的 Ex 命令在状态栏提示，不执行其他操作。
计数与部分边界语义是明确的常用子集，不等同于 Vim 的全部选项组合。

## 结构与性能

- `src/editor/vim.h`：状态、有限预算和接口。
- `src/editor/vim.cpp`：原生文档访问、模式、寄存器、编辑及撤销事务。
- `src/editor/vim_motion.cpp`：移动、字符查找和文本对象范围。
- `src/editor/vim_commands.cpp`：按键语法、计数、搜索、Ex、宏与重复。
- `src/editor/workbench_vim.cpp`：Windows 键盘／UTF-16 字符输入和工作台命令接入。
- `src/tests/vim_tests.cpp`：隔离的真实 Scintilla 与窗口输入回归。

控制器在 UI 线程使用 Scintilla direct function；没有轮询线程、IPC 或整个文档的第二份镜像。
普通移动不调用 `GETTEXT`，不创建正文快照。只有复制／删除所选范围和整词搜索会读取必要文本。
未命名、命名和自动寄存器共享不可变字符串，避免为寄存器别名重复复制正文。

计数最大 10,000；单寄存器／粘贴 4 MiB；单条修改记录／宏 65,536 键；宏深度 8，单次重放总键数 65,536、插入量 4 MiB。
计数插入也限制展开键数与最坏插入大小。长词和文本对象扫描最多约 1 MiB；到达限制时提示并取消该命令。
超出寄存器预算的删除会取消，不丢失文本；需要删除超大范围时，可显式使用 `"_` 黑洞寄存器并保留原生撤销能力。
长录制被停止；超过修改记录预算的编辑仍可撤销，但不能用 `.` 重放。

运行 `app\OrbitNative.exe --vim-test` 生成 `app/vim-tests.json`。
其中 10 MiB 文档基准统计同步指令处理时间和正文复制字节数，不表示真实按键到屏幕延迟。
行为参考 [Vim 移动与操作符文档](https://vimhelp.org/motion.txt.html)、[修改文档](https://vimhelp.org/change.txt.html) 和 [重复文档](https://vimhelp.org/repeat.txt.html)。本实现没有复制 Vim 源码或新增第三方运行时。
