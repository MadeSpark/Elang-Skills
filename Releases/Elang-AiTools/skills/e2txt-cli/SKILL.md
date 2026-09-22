---
name: e2txt-cli
description: 用 e2txt 命令行在「易语言源码(.e/.ec)」和「文本目录」之间互转，从而让 AI 能读取、搜索、编辑易语言代码。当用户提到 .e / .ec 文件、易语言源码、精易模块、e2txt，或需要查看/修改易语言程序或模块时使用。触发词：易语言、e语言、精易模块、e2txt、.ec、.e 源码、编译模块、反编、转文本。
---

# e2txt CLI —— 易语言源码 ⇄ 文本

> 本技能符合 **Agent Skills 开放规范**，只用 `name` + `description` 两个 frontmatter 字段，正文纯 Markdown，可在 Claude Code / Codex / DSH / Trae / Cursor / OpenCode 等任何兼容工具中加载。它不需要任何脚本，纯知识型技能。

AI 无法直接读二进制 `.e` / `.ec`。必须先转成文本目录再读，改完再转回。

## 先确认 e2txt 在哪

本技能**不假定安装路径**（e2txt 是 Windows 工具，跟着易语言生态走）。先确认能跑通：

```bash
e2txt -mode e2t -src "..." -dst "..."
```

跑不通就是它不在 `PATH` 上，二选一：

- 把 `e2txt.exe` 所在目录加进用户 `PATH`（`HKCU\Environment\Path`），**改完要新开终端**；
  老终端临时补一句：`export PATH="$PATH:/实际路径"`（Git Bash）。
- 或设环境变量 `E2TXT` 指向 `e2txt.exe` —— 技能 `elang-ai-coding` 自带的三个脚本优先读它。

同目录通常还有 `e2txt-gui.exe`（图形界面）与 `config.conf`（GUI 读写，CLI 不读）。

## 命令行参数

调用格式：`e2txt -mode <模式> -src <源> -dst <目标> [其它]`

| 参数 | 说明 | 取值 |
|---|---|---|
| `-mode` | **必填**，工作模式 | `e2t` / `t2e` / `ve` / `vt` |
| `-src` | 源文件或源目录 | 路径 |
| `-dst` | 输出文件或输出目录 | 路径 |
| `-enc` | 输出文本编码 | 如 `UTF-8`（**输出带 BOM**）；非法值会被静默忽略并回落默认 |
| `-level` | 输出详细级别，**默认 `1`** | `1` 最小化（省略空产物）/ `2` 最大化（完整） |
| `-ns` | 命名空间相关开关 | `0` / `2` |
| `-nr` | 匿名组命名（给模块里未公开的成员起名） | 自定义字符串 |
| `-pwd` | 口令（**不支持**加密源码/模块） | — |
| `-async` | 异步执行 | `0` / 非0 |
| `-e` | 扩展选项 | — |
| `-log` | 日志开关，**出现即开启**（带什么值都一样） | 习惯写 `真` |
| `-debug-source` | 调试来源 | — |

> **务必显式传 `-level 2`**：默认的 `1` 会跳过空内容产物。实测同一个 .e，`-level 1` 只出 131 个文件，`-level 2` 出 138 个（多出 `const.e.txt`、`dll.e.txt`、`struct.e.txt`、`icon.ico`）。少这些文件会导致回写时缺常量/图标。

### 四种模式

| mode | 方向 | src | dst |
|---|---|---|---|
| `e2t` | 易源码/模块 → 文本 | `.e` 或 `.ec` 文件 | 输出**目录** |
| `t2e` | 文本 → 易源码 | 文本**目录**（e2t 的产物） | 输出 `.e` 文件 |
| `ve` | 查看源码（GUI 窗口） | `.e` / `.ec` | — |
| `vt` | 查看文本（GUI 窗口） | 文本目录 | — |

> `ve` / `vt` 会弹 GUI，CLI/自动化场景不要用。`txt` / `view` / `code` 等都是**非法** mode。

## 用法示例

```bash
# 源码 → 文本（最常用）
e2txt -mode e2t -src "D:/proj/主程序.e" -dst "D:/proj/out" -enc UTF-8 -level 2

# 模块 → 文本
e2txt -mode e2t -src "D:/lib/精易模块.ec" -dst "D:/lib/精易模块.src"

# 文本 → 源码（改完回写）
e2txt -mode t2e -src "D:/proj/out" -dst "D:/proj/主程序_new.e" -log 真
```

## 输出结构（e2t 产物，`-level 2`）

```
out/
├── class/            程序集与类：xxx.static.e.txt / xxx.form.e.txt
│                     + dirs.desc.json、order.list.txt
├── module/           引用的模块源码（含 class/ 子目录）
├── config/           lib.config.json / system.config.json / user.config.json
├── form/             窗口资源 xxx.form.json
├── resource/         图标等资源
├── const.e.txt       常量（`-level 1` 时若为空则省略）
├── dll.e.txt         DLL 声明（同上）
├── struct.e.txt      自定义数据类型（同上）
├── global.e.txt      全局变量
├── icon.ico          图标（同上）
├── project.etprj     工程描述（**GBK 无 BOM**，与其它文件的 UTF-8+BOM 不同）
└── log/              【仅 -log 真 时出现】error.log + info.log
```

> 上面是**不传 `-ns` 的英文风格**。**e2txt GUI 用中文风格**（对应 `-ns 2`）：`代码/`（class）、`配置/`（config）、`窗口/`（form）、`资源/`（resource）、`常量.e.txt`（const.e.txt）、`项目.etprj`（project.etprj）。给 GUI 用的工程一律用中文风格，混用会让工具以为文件全丢了。完整对照见技能 `elang-ai-coding` 里的 `references/易语言文本格式规范.md` §10.1。

> `-log 真` 会在**目录型参数**下额外生成 `log/`：`e2t` 时落在 `dst` 目录，`t2e` 时因为 `dst` 是文件、所以落在 `src` 文本目录里。不想污染文本目录就别加 `-log`。

## 行为要点

### 退出码恒为 0，且成败信息分在两个流里（实测确认）

| 内容 | 去向 |
|---|---|
| `SUCC:<输出路径>`（成功判定行） | **stdout** |
| `[信息]` 日志 | **stdout** |
| `ERROR:<中文原因>`（致命错误判定行） | **stderr** |
| `[错误]` 日志 | **stderr** |

所以判定逻辑必须是 **两个流都读**：

```python
p = subprocess.run([E2TXT] + args, capture_output=True, timeout=600)
so = p.stdout.decode("gbk", errors="replace")   # SUCC: 在这里
se = p.stderr.decode("gbk", errors="replace")   # ERROR: / [错误] 在这里
ok = "SUCC:" in so                              # 不要看 p.returncode
```

**只读 stdout 会漏掉全部错误信息**（`[错误]` 一条都不会出现在 stdout）。

### 其它

- 输出是 **GBK 编码**，用 Python `subprocess` 接时要 `decode('gbk', errors='replace')`。
- 致命错误对照：
  - `-mode e2t` 源文件不存在 → `ERROR:来源文件不存在`
  - `-mode t2e` 源目录不存在 → `ERROR:目录不存在`
  - 无参数 / 未知参数 → `ERROR:请输入有效的命令行参数`
- 致命错误时 stdout **为空**（0 行），stderr 只有一行 `ERROR:`。

### 实测基准（一份 1.23 MB 的 .e）

| 操作 | 耗时 | stdout | stderr `[错误]` | 判定 |
|---|---|---|---|---|
| `e2t`（含 `-level 2`） | 0.6 s | 227 行 | 1 条 | SUCC，产出 138 文件 |
| `t2e`（含 `-log 真`） | 0.9 s | 17589 行 | 385 条 | SUCC，产出 1.00 MB |

## 已知坑

- **转换非无损，务必保留原始 `.e`**。实测 1.23 MB 源码 `e2t→t2e` 回写后变成 1.00 MB，并伴随 385 条 `[错误]`（分布：`ParseParams` 189 / `ParseMethodCode` 98 / `ParseExpression` 97 / `ParseCodeRange` 1）。
- **`[错误]` 大量出现 ≠ 转换失败**。只要 stdout 有 `SUCC:` 就算成功。上述 385 条错误绝大多数是**源文件引用的支持库没装**导致的：文本里会出现 `<?未知名称:支持库不存在?>` 占位，对应 `未定义的常量: XXX`。这是**源文件自身的依赖缺失**，不是 e2txt 的缺陷。
- **但生成的 `.e` 不可信**：有支持库缺失时，丢进易语言 IDE 里编译基本都会失败（变量/函数名缺失）。**永远不要用回写产物覆盖原文件**，只当参考。
- **「数组位」（变量 flags bit3 = `0x08`）不要去清 —— 原样交付**（2026-09-20 用易语言 IDE 实测更正；本节早前的结论是**错的**，曾据此建议用 `efix.py apply` 清理，**别照做**）：
  - **命令行 `e2txt.exe` 与 GUI `e2txt-gui.exe` 的 `t2e` 产物逐字节完全相同**（同一文本目录、同一时刻实测：12333 B / md5 `1d4a8e7e`）。**不存在「GUI 干净、CLI 脏」** —— 早先那句「只出在命令行版」的归因是错的。
  - 用 `efix.py apply` 清掉 bit3 之后，**易语言 IDE 直接打不开该 `.e`**；未清的原样产物能打开、能成功编译运行（用户实测）。
  - `e2t` 回读时把 bit3 显示成 `, 数组`，那只是 e2txt 自己的解读口径，**IDE 并不把该变量当数组**。
  - ⇒ **`t2e` 产物直接交付，不要跑 `apply`。** 可靠性判据是「**用户能否在 IDE 里打开并编译运行**」，不是「回读文本是否干净」。
  - 因此 `efix.py` 只剩只读用途：`check` 列出带 bit3 的记录供研究对照（其文件头已写明「原样交付就行」）。
  - **教训**：验证之前先确认「基准是哪一侧产出的」—— 当时拿错了对照侧，才得出「只有命令行有问题」的假结论。
- **`项目.etprj` 是 GBK 无 BOM**（`*.e.txt` / `*.config.json` 才是 UTF-8 带 BOM）。e2txt 按 ANSI 读 etprj，写成 UTF-8+BOM 会让 JSON 解析失败。另外命令行 `t2e` 会顺手把 `<文本目录>\项目.etprj` 重写成只剩 4 个 `TXT2E-*` 键，所以要在 `t2e` **之后**再写 etprj。
- **`-src` / `-dst` 都给绝对路径**；相对路径报 `ERROR:来源文件不存在`。
- **格式写入要求**（否则 t2e 会报一堆错）：UTF-8 **带 BOM** + **CRLF**；缩进**每层 4 空格**禁 Tab；程序集/子程序/程序集变量顶格，子程序内声明与语句 4 空格；字符串全角 `“ ”`、运算符全角 `＋ ＝`（但**声明里的初值/尺寸用半角引号** `"4"`）。完整规范见技能 `elang-ai-coding` 自带的 `references/易语言文本格式规范.md`，自检工具是该技能 `scripts/` 下的 `rtcheck.py`。
- **支持库清单的唯一输入是 `config/lib.config.json`（中文布局 `配置/支持库.config.json`），不是文本里的 `.支持库` 行**（2026-09-21 受控实验确认，四组对照同源文本只改一处）：
  - 只在 json 里追加一项 → `t2e` 产物 `.e` **变化**（6747 B → 6818 B）且二进制里能找到该 key 的 ASCII ✅
  - 只在 `class/*.e.txt`（中文 `代码/*.e.txt`）的 `.版本 2` 之后插一行**顶格** `.支持库 <key>` → `t2e` 产物**与基线逐字节相同**（md5 一致），key 没进 `.e` ❌
  - 换一个**本机已安装**的库（`cncnv`）重复该实验，**同样无效** ⇒ 与「该库是否安装」无关，**这条通道就是不通**
  - 要项结构（`Key` = `.fne` 文件名主干，`Guid` = 32 位无连字符小写 hex）：
    `{"Key":"<key>","Guid":"<32位小写hex>","Name":"<库名>","Version":{"Major":1,"Minor":0},"CmdCount":0,"MaxRefConstPos":0,"MaxRefObjectPos":0}`
  - **验收闸门 = 产物 `.e` 的 md5 必须变化**（更硬的判据：二进制里能找到该 key 的 ASCII）。md5 没变就是白改，不得进入下一步。
  - `e2t` 只会把库表写成 `config/lib.config.json`，**任何文本文件里都不会出现 `.支持库` 行**。
  - 顺序坑：`e2t` 一个声明了**本机没装**的支持库的 `.e` 会失败（stderr `加载支持库失败！[名称] … [主键] …`，且**不产出** `class/*.e.txt`）。所以「给 `.e` 追加库声明」的顺序必须是 **先 `e2t`（此时还没声明）→ 再改 json → 再 `t2e`**；加壳完成后**不要再 `e2t`**。
- **格式正确性的判据是「双次往返收敛」**，不是「原文 == 回读」：e2txt 会把 Tab 转空格、`甲 = "x"` 转全角、**忽略文本里的 `.支持库` 行**并重写空行，所以一次往返必然不一致。规范形走第二遍应当逐字节不变。
- **不支持加密**的源码和模块，无破解能力。
- **`-level` 默认是 1**，会静默丢掉空内容的 `const.e.txt` / `dll.e.txt` / `struct.e.txt` / `icon.ico`。转文本时**一定要显式 `-level 2`**，否则回写必然缺常量。
- `-enc UTF-8` 写出的文件**带 UTF-8 BOM**（`\xef\xbb\xbf`），Git Bash 的 `head` 会显示成乱码 ``，是正常的。
- `-enc` 传非法值**不报错、静默回落**，别指望靠它验证编码参数是否生效。
- `-log` 是**开关参数，出现即开启**（实测传 `0` / `假` / `false` / `no` **照样生成**，传不了「关闭」），只能靠整个参数不写来关闭。开启后会额外生成 `log/` 目录（`info.log` 可达 1 MB 以上）；`t2e` 时它落在 **src 文本目录**里，会污染你自己的源码目录。
- `ve` / `vt` 会弹 GUI 窗口，自动化脚本里会挂住，不要用。
- 大工程 `t2e` 的 stdout 可以有几万行，`capture_output=True` 没问题但别忘了 **stderr 也要读**。

## 推荐工作流（AI 读改易语言代码）

```bash
# 1. 转文本（-level 2 必须有；-log 不要加，会污染目录）
e2txt -mode e2t -src "xxx.e" -dst "<临时文本目录>" -level 2 -enc UTF-8   # → stdout 找 SUCC:

# 2. 读或搜索 <临时文本目录>/class/*.e.txt，定位要改的代码
# 3. 编辑文本文件
# 4. 转回（绝不覆盖原文件）
e2txt -mode t2e -src "<临时文本目录>" -dst "xxx_new.e"                  # → stdout 找 SUCC:；stderr 看 [错误]
```

5. 让用户在易语言 IDE 里打开 `xxx_new.e` 编译验证。

**每步都要读 stderr**：即使 `SUCC:` 也要看 `[错误]` 有多少、是不是 `未定义的常量`（说明支持库缺失，回写的产物大概率编不过）。

## 顺带一提：模块（`.ec`）转成文本后，本身就是一份现成的 API 手册

易模块的源码文本里，每个子程序写成 `.子程序 名字, 返回类型, 公开, 备注` ——
**第 4 个字段就是函数备注**，参数紧跟其后（`.参数 名字, 类型, 参考?, 可空?, 备注?`）。
所以「`e2txt -mode e2t` 一个模块 + `grep "^.子程序 "`」就等于把它的 API 手册抽出来了：

```bash
e2txt -mode e2t -src "<你要查的模块>.e" -dst "<临时目录>" -level 2 -enc UTF-8 -ns 2
grep -rn "^.子程序 编码_" "<临时目录>/代码/"      # 子程序名, 返回类型, 公开, 备注
grep -rn "^.子程序 文本_取长度" "<临时目录>/代码/"
```

查**精易模块 / NB模块 / HP_Socket** 这类易模块的函数签名和备注，这是**唯一可靠**的办法 ——
它们是 `.ec` 易模块而不是 `.fne` 支持库，任何「支持库文档导出工具」都查不到它们。

> 具体到 Unicode 处理（`编码_Utf8到Unicode`、`编码_Unicode到usc2`）和「取长度」口径差异
> （`取文本长度` 数字节 vs `文本_取长度ex` 数字符），见技能 `elang-ai-coding` 的 §★。

## 著作权提示

e2txt 作者 JimStone(谢栋)，站点 e2eeE.com。转换源码时请在关于信息中保留工具来源与作者信息。
