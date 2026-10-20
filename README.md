# Elang-AiTools —— 让 AI 能读写易语言的工具链

一套让 AI（Claude Code / Codex / Cursor / Trae / WorkBuddy 等 40+ 种 Agent 工具）
**真正能读、能写、能验证易语言（`.e` / `.ec`）代码**的技能与工具集。

> 痛点：AI 写易语言代码，几乎必翻车——不知道流程控制语句的确切语法，写出来的块结构
> 让 IDE 的**流程线串线**；`判断` 被当成 switch 用；`寻找文件` 边找边递归导致漏项。
> 本项目把这些坑全部实测钉死，做成跨工具通用的 Agent Skills。

## 组件

| 组件 | 说明 |
|---|---|
| `Releases/Elang-AiTools/skills/elang-ai-coding/` | **编码技能**：格式铁律、五种控制语句精确语义、Unicode/文本长度铁律、声明槽位总表、DLL cdecl、支持库命令查询、双次往返验证、GUI 项目产出。自包含（脚本/exe/规范全文都在包内）。**references 带四份专题**：置入代码与机器码、Win32 与 DLL 互操作、程序入口与编译验证、黑月编译与黑月界面类（搬运自 [aiqinxuancai/e-language-skill](https://github.com/aiqinxuancai/e-language-skill)，MIT，经本机实测修订） |
| `Releases/Elang-AiTools/skills/qaq-etool-cli/` | **转换技能**：自带 QAQ-ETool（命令行 `QAQ-ETool.exe` + 图形界面 `qaq-etool-gui.exe` + `qaq-libinfo.exe`），`.e` ⇄ 文本目录互转的参数、退出码与交付红线。其它三个技能的脚本都靠它转换，需同装 |
| `Releases/Elang-AiTools/skills/elang-debug/` | **调试技能**：给 `.e` 加壳声明宿主库 → 程序化触发易语言 IDE 的「调试运行」→ 流式收调试框日志 → 输出结果 JSON（`ok`/`stopped`/`crashed`/`timeout`）。自包含（宿主库 `.fne` + 两个脚本都在包内） |
| `Releases/Elang-AiTools/skills/elang-build/` | **无头编译技能**：不开 IDE 把 `.e` **静态编译**成原生 exe/dll——自家宿主库链路（`ecompile.py` 一条命令：加壳→触发官方 FNST_RUN 静态编译功能号→进程内自动填产物对话框→收尾自证，实测可用）；不依赖任何第三方工具。自包含 |
| `src/` + `tools/` | 支持库文档导出工具、调试宿主支持库的 C 源码与构建脚本；以及 `mkcage`/`elaunch`/`ecompile` 等工程脚本 |

技能遵循 **Agent Skills 开放规范**（agentskills.io），frontmatter 只有 `name` + `description`，
一份文件跨工具通用；`安装技能.cmd` / `install-skills.py` 可一键装进本机所有 AI 工具（实测 9 处 × 4 技能）。

## 实测钉死的关键结论（AI 最容易写错的地方）

- **`判断` 是 `if / else if / else` 链，不是 switch**。条件必须是逻辑型，
  给整数直接编译报错 `错误(10044): 不能将 "整数型" 数据转换到 "逻辑型" 数据`。
  `.判断 (x ＝ 2)` 里的 `x ＝ 2` 是一条独立布尔条件，语义就是 C 的 `else if`。
- 分支差异：`判断` / `如果` 都有「判断成功 + 判断失败」两路（`.默认` / `.否则` 即 `else`）；
  `如果真` **只有成功分支**；块尾 `()` 规则：循环尾必须带 `()`，`.判断结束` / `.如果结束` / `.如果真结束` 不带。
- **块尾之后紧接另一条块语句必须空一行**，否则 IDE 流程线串线（空行只是 19 字节空记录，多写无害）。
- `寻找文件 (路径, #子目录)` **不是"只要子目录"**——实测返回全部条目（文件也在内）；
  真目录要用 `位与 (取文件属性 (…), #子目录) ≠ 0` 过滤，且 `取文件属性` 失败返回 **-1**（`位与(-1,16)≠0` 恒成立），
  会把 `hiberfil.sys` 这类被系统独占的文件误判成目录。
- `寻找文件` 的迭代位置记在**子程序**上，边找边递归会被内层冲掉状态（漏项且不报错），
  必须先收进数组再递归。
- 易语言原生**不支持 Unicode**（文本型就是 GBK 字节流）：`取文本长度` 数的是字节，中文一个字算 2。
- 文本格式：UTF-8 带 BOM、CRLF、缩进每层 4 空格、全角引号 `“ ”` 与全角运算符 `＋ ＝ ≠`，
  声明里的数组尺寸用半角引号 `"0"`。

## Demo（`demos/`，全部在易语言 IDE 实测通过）

| Demo | 内容 | 验证 |
|---|---|---|
| `01-C盘结构输出/` | 递归输出 C 盘目录树（最多 3 层），只用 `krnln` 核心库命令，五种控制语句各就其位 | rtcheck 双次往返收敛 ✓；IDE 编译 ✓；运行 ✓；`ExitCode:0`；输出见 `示例输出_C盘结构.txt` |
| `02-嵌套控制流压测/` | 最深 **5 层**嵌套：素数判断（`跳出循环`）、九九乘法表（`判断循环首 × 变量循环首`）、do-while（`循环判断首/尾`）+ `到循环尾` | rtcheck 一次通过 ✓；IDE 编译 32ms ✓；`ExitCode:0`；结果逐项核对正确 |
| `03-Json解析测试/` | 用 yyJson（酷C版）模块的 `json对象` 做四组用例：RFC6901 路径取值（`地址/市`、`标签/0`）、`取对象/取数组` 参考参数接收、对象枚举、坏 JSON 错误捕捉 | echeck ✓、rtcheck 收敛 ✓；IDE 运行 ✓；四组用例全部通过，输出见 `示例输出_Json解析测试.txt` |

每个 demo 目录下 `项目/` 都是**把 `项目.qtprj` 拖到 `qaq-etool-gui.exe` 上就能打开**的完整工程
（把 `.qtprj` 的「打开方式」设成 `qaq-etool-gui.exe` 后可直接双击）
（文本源码在 `项目/代码/*.e.txt`，`项目/代码.e` 是可直接用易语言 IDE 打开的产物）。

## 快速开始

```bash
# 1. 把技能装进本机所有 AI 工具（Windows）
cd Releases/Elang-AiTools && ./安装技能.cmd        # 或: python install-skills.py

# 2. 查本机支持库命令（77 库 / 5951 条）
Releases/Elang-AiTools/skills/elang-ai-coding/assets/导出支持库文档.exe -q

# 3. 对话里直接说：「用易语言写一个……」，技能会被自动调起
```

依赖：易语言 IDE（提供 `lib\*.fne`）与 Python 3.9+；`.e` ⇄ 文本转换工具 QAQ-ETool 随 `qaq-etool-cli` 技能发布，不用另装。

## 项目结构

仓库分三层：**发布区**（`Releases/`，交付给用户的成品）、**源与工具**（`src/` `tools/` `QAQ-ETool/`，构建成品的源本）、**素材与产物**（`docs/` `demos/` `支持库文档/` `miniblink/`）。发布区里的 `skills/*/scripts|references|assets` 是 `tools/` `docs/` `src/` 的同步副本，`tools/verify-release.py` 会断言它们与源本逐字节一致。

```
测试/
├── Releases/Elang-AiTools/       发布区（成品）：四个技能 + 跨工具安装器
│   ├── skills/
│   │   ├── elang-ai-coding/       编码技能（assets: 导出exe / references: 四份专题 / scripts）
│   │   ├── elang-build/           无头编译技能（scripts: ecompile 等）
│   │   ├── elang-debug/           调试技能（assets: 宿主库 .fne / references / scripts）
│   │   └── qaq-etool-cli/         .e⇄文本 转换技能（assets: QAQ-ETool 三件套 exe）
│   ├── install-skills.py / .sh    一键装进本机所有 AI 工具
│   ├── 安装技能.cmd               Windows 安装入口
│   └── 使用说明.md
│
├── src/                          C 源码与构建脚本
│   ├── addin/                    调试/编译宿主支持库 elang_addin.fne 的源码
│   ├── mbkit/                    miniblink 多实例热重载内核 MbKit.dll 源码
│   └── elibdoc.c                 支持库文档导出工具源码（产物 = 导出支持库文档.exe）
│
├── tools/                        工程脚本
│   ├── mkproj / rtcheck / echeck / elibs / mkcage / elaunch / ecompile / etool   核心闭环脚本
│   ├── pack-skill.py / sync-release.sh / verify-release.py                       打包与发布校验
│   └── *-eide-*.ps1                                                              IDE 卡死排查脚本
│
├── QAQ-ETool/                    .e ⇄ 文本目录 转换工具的源码工程（dist/gui/src/tests/vendor…）
│
├── demos/                        实测 demo（工程 + 示例输出）
│   ├── 01-C盘结构输出/
│   ├── 02-嵌套控制流压测/
│   ├── 03-Json解析测试/
│   └── 下载接管/                 下载接管 A/B 可运行模板（A=控制位置并取链接，B=彻底拦截只取链接；各带 exe 与 README）
│
├── docs/                         格式规范 + 开发工程说明 + 下载接管修复（2 篇；模块使用说明在 miniblink/）
├── 支持库文档/                   导出产物（77 库 / 5951 条命令 + commands.jsonl）
├── miniblink/                    miniblink 模块交付目录（miniblink.e / .ec、使用说明.md、模块测试源码.e/.bak、MbKit146.dll、WebView2Loader.dll）
├── re/                           运行验证留档（mbkit_run/evidence 原始日志）
├── 测试.e                        小样例工程
├── 导出支持库文档.exe            支持库文档导出工具（成品）
└── README.md / .gitignore / .gitattributes
```

## 工作流（AI 写易语言代码的标准闭环）

```
echeck 静态语义检查（命令/参数/块结构，复用 commands.jsonl）
  → rtcheck 双次往返收敛自检（格式）
  → QAQ-ETool t2e 生成 .e（出现 [错误] 或退出码 2 即失败）
  → 易语言 IDE 编译运行验收（终审）
```

`echeck` 不依赖 IDE：用支持库文档校验每个命令调用（存在性 / 参数个数 / 可空留空）与
块首块尾配对，把 AI 最常犯的错挡在进 IDE 之前；`rtcheck` 保证格式收敛。

2026-10-20 起 `echeck` 会额外打印一行「**流程分支**」：汇总 `check_blocks` / `check_hidden_tail_gap` /
`check_sub_tail_gap` / `check_empty_branch_gap` 四类块结构问题，**必须 `✓ 0 错误 0 警告`，任一非零即退出码 1**。
这类「编译通过、`t2e`/`rtcheck` 全绿，但 IDE 打开弹『控制块有格式错误』或流程线串到窗口底」的缺陷，
`ecompile --check`、`e2t`、`t2e`、`rtcheck` 全都看不见，只有它和真 IDE 能发现。

唯一验收标准：**能不能在易语言 IDE 里打开并编译运行**。

## 调试易语言程序（`elang-debug`）

让 AI 不只是“写”，还能**跑起来看**：

1. `mkcage.py` 给用户的 `.e` **加壳**（只在工程库表里声明一个宿主支持库，全程离线、不启 IDE）；
2. `elaunch.py` 启动易语言 IDE 打开加壳后的 `.e`，**程序化触发一次「调试运行」**，
   并把调试框文本 **50ms 增量流式落盘**（中文同时给 `.utf8.txt` 视图）；
3. 结束时输出**机器可读 JSON**：`status` 判定**不看面板尾行**（尾行分不出“正常结束/被中断”）——
   `crashed` 靠 **IDE 子进程退出码 ≠ 0**（如 `0xC0000005`），
   `stopped` 靠“**我们主动**发过 `FN_END_RUN` 且观测到 `enabled 1→0`”，
   `ok` = 自然结束，`timeout` = 超时未结束。

前置：Windows + 易语言 IDE（用 `ELANG_HOME` 或注册表 `HKCU\Software\FlySky\E\Install` 定位）+ Python 3.9+（QAQ-ETool 由同装的 `qaq-etool-cli` 技能提供）。
```bash
python skills/elang-debug/scripts/mkcage.py   <用户.e> <_ai.e>      # 加壳
python skills/elang-debug/scripts/elaunch.py  <_ai.e> --json        # 调试运行 + 收日志
```
风险：被调试程序**运行期异常**可能连带打挂 IDE（第三方插件 bug）——因此**每个会话独立进程**、
以**子进程退出码**判定，异常程序应隔离运行。

## License

本项目工具代码与文档随意使用；
易语言为大连大有吴涛易语言软件开发有限公司产品。
