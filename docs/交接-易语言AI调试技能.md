# 交接文档 · Elang-AiTools 的「无 IDE 调试易语言」技能（`elang-debug`）

> **交给谁**：接手的 AI 助手 / 工程师。本文假设你没有参与过之前的任何对话。
> **交接时点**：2026-09-21 夜。工程根：`C:\Users\MadeSpark\Desktop\测试`。
> **一句话现状**：**核心目标已跑通且有物证** —— 不打开易语言 IDE 界面就能对 `.e` 做**调试运行**，并把**调试输出面板**的内容**实时流式**抓到手（效果等同人跑 `输出调试文本 ()` 时看到的调试框）。技能已成包、发布链路健康（**总检 56 项通过 / 0 项失败**）。**原列 6 处断点已于 2026-09-22 全部收口**（见下方「接手后进度补记」与 §8 表格）。
>
> ⚠️ **本文含本机绝对路径，属于「给协作者看的过程材料」，严禁进入技能包**（`tools/sync-release.sh` 用显式名单，已不会误同步；`tools/verify-release.py` 有 references 白名单闸门兜底）。

---

## 0. 接手者 5 分钟上手

| 你想干什么 | 看哪里 | 跑什么 |
|---|---|---|
| **直接用它**（跑一个 `.e` 拿调试输出） | `Releases/Elang-AiTools/使用说明.md` §「调试技能脚本的用法」 | 见 §6 的三条命令 |
| **搞懂它怎么跑通的** | §3 原理链 + `re/addin/POC1D-免登记成功与__stdcall根因.md` | — |
| **验证它真的能跑**（别信我，信物证） | §5 的 grep 命令 + `re/addin/evidence/` | `grep -n "嵌套压测完成" re/addin/evidence/runA_capture.utf8.txt` |
| **接着开发** | §8 断点清单（按优先级） | 从 P0 开始 |
| **改技能内容** | `Releases/Elang-AiTools/skills/elang-debug/SKILL.md`（**真源在 `tools/` 与 `docs/`，改完必须跑同步**） | §10 的三条命令 |
| **看决策过程 / 为什么选这条路** | `docs/方案-无IDE运行调试易语言.md`（**v6**，PoC-2 已回填） | — |
| **看 e.exe 逆向证据** | `docs/分析-易语言主程序与官方扩展接口.md`（431 行，逐条带 `文件:行号`） | — |

---

## ⭐ 接手后进度补记（2026-09-22，接手 AI 完成 §8 断点收口）

| # | 断点 | 结果 |
|---|---|---|
| ① | 方案文档回填 PoC-2 | ✅ **`docs/方案-无IDE运行调试易语言.md` 升 v6**：PoC-2 判据 A/B/C 回填进 D.4（含物证 grep 指针）、C.4 面板 HWND/类名标「已坐实」、P0 探针（DBWIN）关闭（主路线不依赖）、阶段二/三标已完成、并稿声明追加 v6；**如实标注未做项**（DBWIN 并行对照 / `FN_ADD_TAB` 备选通道 / PoC-3） |
| ② | 真实大工程往返实测 | ✅ **担忧被实测排除**：7 个真实工程（0.66~3.9 MB，含内嵌双 `.ec` 模块）`e2t→t2e` 往返全部零 `[错误]`/零占位、体积偏差 −0.2%~+1.1%（模块工程 −13.3% 系 `t2e` 重建模块区更紧凑，**非丢失**）；2 个代表工程加壳产物全链路真机**编译成功（42/208 ms）并真实运行**（一个 `ok` 自然结束、一个被 `FN_END_RUN` 干净中断）。**「大工程禁用/降级」不需要**；真正要防的是**依赖缺失**（此类工程原件在本机本来就编不过，非加壳引入）。矩阵 `re/rt_batch/matrix.json`，脚本 `re/rt_batch.py` / `rt_big_test.py`。结论已写入技能 `references/加壳流程.md` §5 + `SKILL.md` 排查表（新增 2 行） |
| ③ | mermaid 图 | ✅ **关闭不建**：全仓检索证实「架构师在方案里承诺过两图」并不存在（唯一提及处就是本表 §8）；§3 ASCII 链已承担该职责，无引用方的孤儿图不建 |
| ④ | `readlog.py` 接口评审 | ✅ **已复核并修 4 处偏差**：`--business` 漏掉 DELTA 包装行内嵌的**块首业务行**（实测：只输出一行的程序会被完全滤空——runA 的 `嵌套压测完成` 就只在包装行里）；`--follow` 跨块丢半行；`--follow` 中 GBK 双字节字符跨块被截成替换符；「默认输出 UTF-8」实际受控制台代码页影响。改为：DELTA 行提取 + 字节级余量切行 + 显式 UTF-8 字节输出 + Ctrl-C 正常退出 + `--json --follow` 先快照再跟随。回归 `re/test_readlog_follow.py` PASS；对 runA 物证 `--business` 现在能取到 `嵌套压测完成` |
| ⑤ | `re/` 清理 | ✅ `re/mkcage.py`（PoC 版）改名归档为 `re/_archive/poc_mkcage.py`；新增 **`re/README.md`** 导览（物证/脚本/归档三分区 + 纪律）；本会话实测的可再生中间产物（文本目录、`.e` 副本）已删，留矩阵与日志物证 |
| ⑥ | git 未提交 | ✅ 已按主题分 4 个 commit（技能实现 / 文档 / 实验物证 / 发布链路） |
| ⑦ | 僵尸 e.exe PID 49560 | ⏸ 未动（按 §9 纪律等待重启自然消失） |
| ⑧ | 三项待拍板 | ⏸ 维持原推荐方案落地，无变化 |

**收尾总检（补记后实测）**：`sync-release.sh`（27 处安装点刷新，0 失败）→ `pack-skill.py`（3 zip 重建，`elang-debug.zip` 56.0 KB）→ `verify-release.py` **56 项通过 / 0 项失败** → `scan-machineinfo.py` **干净**。

**另注意（本次新发现的口实差）**：`elaunch.py` 当前以**可见窗口**运行（`ELANG_AI_HIDE=0`），每次调试运行会短暂弹出 IDE 主窗口；技能口径已按「不需要人操作 IDE」表述、**未承诺「不弹窗」**，两相一致。`.fne` 已实现 `ELANG_AI_HIDE` 隐藏逻辑但链路未默认启用 —— 这就是方案文档里唯一开放项 **PoC-3（无窗口启动）**，做了即可默认无窗口。

---

## 1. 需求从哪来（三轮演进，别走回头路）

| 轮次 | 用户原话要点 | 结论 |
|---|---|---|
| ① | 「新增一个易语言代码调试技能，传入 `.e` 路径，**无需打开易语言**直接运行调试，让 AI **实时看运行日志**，效果与易语言调试输出窗口一致」 | 定需求，出方案矩阵 |
| ② | 「写个 DLL 通过命令行启动并注入易语言主程序，默认不显示窗口，DLL 能控制易语言各项功能，最需要**调试运行**和**监控调试框**」 | 这就是**宿主化**思路，与①合流 |
| ③ | 「**走独立技能 + 官方宿主化**。写一个易语言支持库，启用后在易语言启动时读启动参数，检测到 AI 工具提交的参数就直接加载参数里的 e 源码」 | **拍板**。最终落地为「独立技能 `elang-debug` + 官方宿主库 `elang_addin.fne`」 |

**关键取舍（已拍板，勿推翻）**：

- **走官方宿主化（路线④a），不走外部 DLL 注入（④b）** —— ④a 用官方插件接口，稳定、无注入对抗；④b 需要 CreateRemoteThread 之类手法，脆弱且易被杀软误杀。
- **不依赖任何第三方命令行编译器**（社区工具 `ecl.exe` 在本机**未安装**）。整条链路只用 `e.exe` 自己。
- **不做单步 / 断点 / 变量查看**（`FN_STEP_INTO` / `FN_SET_BREAK_POINTER` / `FN_VIEW_VAR` 已定位但**未纳入**）。目标只有：编译运行 + 抓调试框。
- **「无需打开易语言」的准确口径**：脚本**仍会启动 `e.exe` 进程**，只是**不需要人打开 IDE 界面、不需要人点编译**（默认后台跑）。**别向用户承诺成「完全不启动易语言」。**

---

## 2. 现在盘上有什么（可用产物）

### 2.1 三个技能（都由 `Releases/Elang-AiTools/install-skills.py` 装到本机所有 AI 工具）

| 技能 | 干什么 | 包内容 |
|---|---|---|
| `elang-ai-coding` | 教 AI **写对**易语言代码：文本格式规范、五种流程控制块的精确块结构、支持库命令查询 | `SKILL.md` + `scripts/{mkproj,rtcheck,efix,echeck}.py` + `references/易语言文本格式规范.md` + `assets/导出支持库文档.exe` |
| `e2txt-cli` | `.e` ⇄ 文本目录互转（让 AI 能读写易语言源码） | `SKILL.md`（单文件） |
| **`elang-debug`（本次新建）** | **把 `.e` 跑起来并抓到调试输出**：加壳 → 启 IDE 调试运行 → 流式收日志 → 结果 JSON | `SKILL.md`(122 行) + `scripts/{mkcage,elaunch,readlog}.py`(308/453/132 行) + `references/{加壳流程,接口与调用约定清单,踩坑清单}.md`(61/39/38 行) + `assets/elang_addin.fne`(59904 B) + `assets/elang_addin.libinfo.txt` |

**安装目标**（`install-skills.py` 顶部 `GLOBAL_TARGETS`，共 10 个）：`~/.agents/skills`（共享根，**always**）+ Claude Code / Codex / DSH / Trae(国际) / Trae(国内) / Cursor / Windsurf / OpenCode / WorkBuddy（**probe**）。
⚠️ **Claude Code 与 Trae 不读共享根**，只读自己目录 —— 所以必须逐个装。

### 2.2 发布链路

```
tools/*.py  ──[sync-release.sh 显式名单]──►  Releases/Elang-AiTools/skills/<技能>/scripts/
docs/*.md   ──[同上]─────────────────────►  Releases/Elang-AiTools/skills/<技能>/references/
src/*/build.sh ─────────────────────────►  Releases/Elang-AiTools/skills/<技能>/assets/（编译产物，不经 sync 脚本）
                    │
                    ├─► tools/pack-skill.py  →  dist/{elang-ai-coding,e2txt-cli,elang-debug}.zip
                    └─► install-skills.py    →  刷到本机 10 个 AI 工具技能目录
```

`tools/sync-release.sh` 里**每技能一张表**（`SKILLS=(...)`，格式 `<技能名>|<scripts>|<references>`）。**新增技能必须同时改这张表和 `tools/verify-release.py` 的检查项。**

### 2.3 健康度（最近一次实跑）

| 检查 | 结果 |
|---|---|
| `python tools/verify-release.py` | **56 项通过，0 项失败** |
| `python tools/scan-machineinfo.py` | **干净**（技能里无本机路径 / 统计数 / 版本号 / 个人信息） |
| `dist/` 三个 zip | `elang-ai-coding.zip` 163,634 B ／ `e2txt-cli.zip` 7,145 B ／ **`elang-debug.zip` 55,094 B** |
| 本机安装 | 9 个目标 × 3 技能全部落盘 |

---

## 3. 核心成果：官方宿主化路线的完整原理链

这是本项目**唯一**的技术核心，理解了这一条就理解了全部：

```
① 我们写一个 32 位支持库 elang_addin.fne（导出唯一未修饰符号 GetNewInf，返回 PLIB_INFO）
        │
        │  易语言默认【不会】自动加载新丢进 lib\ 的库（实测：启动加载集合 = 已登记集合 76/77，
        │  etools.fne 都没载）。「工具→支持库配置」的 UI 自动化路子【不可靠，已放弃】。
        ▼
② 让目标 .e 的【库表】里登记我们的库 → 打开该 .e 时 e.exe 会【按需加载】我们的 .fne
        │  （这一步叫「加壳」，见 §4.3 —— 通道是 配置/支持库.config.json，不是代码文本）
        ▼
③ .fne 被加载 → e.exe 调 GetNewInf → 我们填 LIB_INFO.m_pfnNotify
        │  ⚠️ m_dwState 必须置 LBS_IDE_PLUGIN (1<<8)，否则收不到 NL_IDE_READY
        ▼
④ 系统下发 NL_SYS_NOTIFY_FUNCTION(=1)，dwParam1 就是 PFN_NOTIFY_SYS 指针
        │  （可多次下发，后值覆盖前值 —— 保存最新那个）
        ▼
⑤ 拿到 PFN_NOTIFY_SYS 后【反过来驱动 IDE】：
        NES_RUN_FUNC(=2)，dwParam1 = 功能号，dwParam2 = 双 DWORD 数组指针
        ├─ FN_COMPILE_AND_RUN = 0x05020002   ← 触发「调试运行」，这就是主入口
        ├─ FN_END_RUN         = 0x05020003   ← 主动中断（我们的「停」按钮）
        └─ FN_IS_FUNC_ENABLED = 0x05030004   ← 探测功能是否可用
        ▼
⑥ 调试输出面板是 class='ScintillaForEIDETools' 的控件（由 lib\iDraw\superTools\plugin\eOutPutControl.dll 注册）
   我们轮询它的文本长度，【增量取差值】→ 流式落盘 → 同时给 GBK 原始字节与 .utf8.txt 转码视图
```

**关键结论**：`NL_IDE_READY` 到达时功能号即可用（`FN_IS_FUNC_ENABLED` 全表 `handled=1`）；加载 `.fne` **无需**事前签名 / 白名单。

---

## 4. 已钉死的技术事实（含证据路径，可直接复核）

### 4.1 ⭐ 调用约定：跨 DLL 函数指针一律 `__stdcall`（照抄 SDK 会崩）

- 官方 `mtypes.h:9` 把 `WINAPI` **`#define` 成空宏** → 整个 SDK 的声明都变成了 `__cdecl`。**这是坑。**
- 但 `e.exe` 实际期望 **`__stdcall`**：证据 ① 调用 `m_pfnNotify` 处 `e.exe+0x460872..0x46087A` **之后没有 `add esp`**；证据 ② 真库（`cncnv` / `dp1` / `console` / `iext`）的 notify 全部以 `ret $0xc` 返回；证据 ③ `PFN_NOTIFY_SYS=0x00467FF0` 末尾是 `ret $0xc`。
- **`GetNewInf`（无参）不受影响。** `m_pfnRunAddInFn(INT)` / `m_pfnSuperTemplate(INT)` 的约定**仍待实测**（`INT` 参数下 `ret $4` 与 `ret` 无法靠返回值区分）。
- 出处：`re/addin/POC1D-免登记成功与__stdcall根因.md`；工程内 `src/addin/sdk_compat.h` 已做兼容处理。
- 汇总文档：`Releases/Elang-AiTools/skills/elang-debug/references/接口与调用约定清单.md`。

### 4.2 加载机制（**已推翻**早前「启动即全量加载 77/77」的错误结论）

- `e.exe` **不是**启动即全量加载 `lib\*.fne`：实测**已登记集合 76/77**（`etools.fne` 未加载）。
- **新丢进 `lib\` 的 `.fne` 不会自动加载。**
- 进 IDE 进程两条路：**(i) 全局登记**（「工具→支持库配置」，菜单 ID `0x808A`）—— **UI 自动化不可靠，已关闭**；**(ii) 按需加载**——被打开的 `.e` 库表登记了本库时加载 —— **✅ 已跑通**（任务 D）。
- 出处：`re/addin/POC1-结果与阻塞.md`、`re/addin/evidence/D_trace_full.txt`。

### 4.3 ⭐⭐ 加壳的**唯一**输入通道：`配置/支持库.config.json`

**这是本次最严重的认知反转（我曾经判反过，差点让整个方案走错），请务必按此执行。**

| 做法 | 产物大小 | md5 | 二进制含 `elang_addin` | 结论 |
|---|---|---|---|---|
| A 基线（什么都不改） | 6747 B | `d0d47062` | ❌ | — |
| **B 只改 `配置/支持库.config.json`** | **6818 B** | **`ff549e9c`** | **✅** | **✅ 唯一有效通道** |
| C 只在代码文本里写顶格 `.支持库 elang_addin` | 6747 B | `d0d47062` | ❌ | ❌ **完全无效**（与基线逐字节相同） |
| D 两者都改 | 6818 B | `ff549e9c` | ✅ | = B（文本改动被忽略） |

- **排除替代解释**：把 C 组的 key 换成一个**本机已安装**的库（`cncnv`）→ 产物**仍与基线逐字节相同** ⇒ **与「库是否安装」无关，文本通道压根不通。**
- 受控实验脚本：`re/verify_libsource.py`（四组对照）、`re/verify_libsource2.py`（换已安装库）。
- **两个必踩的坑**：
  - **坑 A**：`e2t` 产出的文本目录里**自带的 `<名>.e` 是伴生文件，不是 `t2e` 的产物**（`t2e` 写到 `-dst` 指定的位置）。**拿那个当基准会得出完全反向的结论** —— 我已经因此踩过一次。
  - **坑 B（顺序硬约束）**：必须 `e2t`（此时还没声明库）→ 改 `json` → `t2e`。**加壳完成后绝不能再 `e2t`**：一个声明了「当前没装的支持库」的 `.e` 会让 `e2t` 直接失败（stderr `加载支持库失败！[名称] … [主键] …`）且**不产出** `class/*.e.txt`。
- **验收双判据（缺一即失败）**：① 产物 md5 ≠ 原件；② 产物二进制里能找到库 key 的 ASCII。
- 另注：`t2e` 回写一律 **`-level 2`**（`-level 1` 会**静默丢空产物**）。

### 4.4 调试输出面板与编码

- 面板控件：`class='ScintillaForEIDETools'`（来源 `lib\iDraw\superTools\plugin\eOutPutControl.dll`，**启动即加载**）。另有一个 `class='Edit'` 的旧面板同时存在，两个都要试。
- **面板文本是 GBK。**
- ❌ **拿原始 GBK 字节去 grep 中文会「看起来什么都没抓到」** —— 必须用 `.utf8.txt` 视图。
- 取文本两种消息约定都要试：`WM_GETTEXT`、`SCI_GETTEXT`(2182)。
- **业务行格式**：被调试程序自己输出的行长这样 —— `[HH:MM:SS] * 内容`（即 `输出调试文本 ()` / `调试输出 ()` 的效果）。`readlog.py --business` 就靠这个正则过滤。

### 4.5 `.e` 二进制里的支持库表格式（已解出）

```
u16 条数
每一条:
  u32 载荷长
  载荷 = Key(ASCII) "\r" Guid(32 位小写 hex 的 ASCII) "\r" Major "\r" Minor "\r" Name(GBK)
```

- **GUID 是以 ASCII 十六进制字符串存储的，不是 16 字节原始值**（正 / 反向原始字节搜索都是 -1）。
- 参考载荷长度：`krnln` = 57（5+1+32+1+1+1+1+1+14）；`elang_addin` = 59（11+1+32+1+1+1+1+1+10）。
- 本库信息（`assets/elang_addin.libinfo.txt`）：`KEY=elang_addin` / `GUID=7A1E4F22C3B0499E8D6A0011223344FE` / `NAME=AI调试宿主` / `MAJOR=1` / `MINOR=0` / `LIBFORMATVER=20000101` / `CMDCOUNT=0`。
- 分析脚本：`re/diff_librec.py` ~ `diff_librec4.py`（base 6747 B vs caged 6818 B：公共前缀 513、公共后缀 6047、净插入点 base@700、Δ+71 B；`[513,517)` 4 B 疑似校验和、`[561,567)` 6 B 待解）。
- **二进制直插**（不走 `e2t/t2e`）是备选路线，**短期不划算**（容器另有偏移表 / 校验和需再解），**已完成摸底，未采纳**。

### 4.6 其它已确认的 `e.exe` 属性

| 属性 | 值 |
|---|---|
| 调试器 | 是（自身就是调试器） |
| 主窗口类名 | `ENewFrame` |
| 导出表 | **无** |
| ASLR / DEP / SafeSEH | **全无** |
| 命令行开工程 | 支持 |
| 安装路径来源 | 注册表 `HKCU\Software\FlySky\E\Install\Path`（`re/reg.txt`） |
| 调试输出限制 | `输出调试文本` / `调试输出` 官方备注原文「**仅在调试版本中被执行，发布版本直接跳过**」⇒ **拿日志必须编译调试版本** |

---

## 5. PoC-2 验收物证（**可 grep 复核，别信结论信证据**）

全部在 `re/addin/evidence/`，文件名按运行唯一命名。**原始文件是 GBK，中文必须 grep `.utf8.txt` 视图。**

### Q1 业务文本拿到了吗？→ ✅ 拿到了

```bash
grep -n "嵌套压测完成" re/addin/evidence/runA_capture.utf8.txt
# → 31:[21:11:13.411][PANEL-DELTA] DELTA: [21:11:13] * 嵌套压测完成
```

注意那个 `[HH:MM:SS] * ` 前缀 —— 这正是易语言调试框里 `输出调试文本()` 的原生格式，证明是**真从面板抓的**。

### Q2 是增量流式落盘，还是最后一次性 dump？→ ✅ 增量流式

```bash
grep -nE "PANEL-CHANGED" re/addin/evidence/runA_capture.utf8.txt
# 11: oldlen=560 newlen=808
# 27: oldlen=808 newlen=837
# 30: oldlen=837 newlen=863
# 33: oldlen=863 newlen=903
# 39: oldlen=903 newlen=936
```

长度阶梯式增长（560→808→837→863→903→936）、每级都有对应 `PANEL-DELTA`，是**轮询取差分**，不是收尾 dump。

### ① 正常结束 → ✅

```bash
grep -nE "ExitCode|运行完毕" re/addin/evidence/runA_capture.utf8.txt
# 34: [调试] - 调试程序[59116]退出,ExitCode:0
# 37/40: 被调试易程序运行完毕
```

### ② 主动中断生效 → ✅（含**负向检查**，这条最关键）

```bash
grep -nE "FN_END_RUN|长任务开始|长任务结束" re/addin/evidence/runB_capture.utf8.txt
# 26: [RUN-STATE] FN_END_RUN enabled: 0 -> 1 (t=0.0s)     ← 运行态起来了
# 31: DELTA: [21:11:39] * 长任务开始
# 33: [RUN-STOP] send FN_END_RUN                          ← 我们主动发的
# 41: [RUN-STATE] FN_END_RUN enabled: 1 -> 0 (t=3.1s)     ← 3.1 秒后回落，中断生效
```

**负向检查**：`grep -c "长任务结束" runB_capture.utf8.txt` = **0** —— 程序被打断在「开始」之后、「结束」之前，证明是**真的中断了**，不是等它自然跑完。

> ⚠️ **为什么不能靠面板尾行判结果**：「正常跑完」与「被我们中断」的面板尾行**逐字相同**（都是 `[调试] - 调试程序[pid]退出,ExitCode:0` + `被调试易程序运行完毕`）。这就是 `elaunch.py` 用「运行态标志 + 谁发的 FN_END_RUN + 子进程退出码」判定的原因（见 §6）。

### ③ 异常（运行期崩溃）→ ✅ 已捕获

`re/addin/evidence/PoC2_crashtrap_iControls.txt`：

```
pid=39088
ExceptionCode = 0xC0000005 (ACCESS_VIOLATION)
ExceptionAddr = 0x0BE8092E (iControls.dll+0x5092E)
AccessType = READ, AccessAddr = 0x00000004
栈上返回地址：iDraw.fne+0x556E / Plugin_eFix.dll+0x57C24 / SciLexer.dll / GDI32.dll / USER32.dll
```

⇒ **第三方 IDE 插件（`iControls.dll` / `iDraw.fne` / `Plugin_eFix.dll`）在被调试程序抛异常时会连带把 `e.exe` 带崩。** 这是**已知风险**，见 §9 纪律。

### 技能端到端冒烟（非交付，但证明技能真能跑）

`re/skill_smoke/`：`runA2_harness.txt` / `runA3_harness.txt`（`观测: runStart=True runEnd=True weSent=False e_self_exit=None`）、`crashC_harness.txt`（`e_self_exit=3221225477` = `0xC0000005`）。
⚠️ 早期版本 `runA_harness.txt` / `smokeA_harness.txt` 尾部有 `{"ok": false, "status": "error", "error": "1"}` —— **那是当时的 bug，`runA2/runA3` 之后已修**，别被旧文件误导。

---

## 6. 工具 CLI 契约（可直接照抄）

### `scripts/mkcage.py` —— 加壳（308 行）

```bash
python scripts/mkcage.py <输入.e> [<输出_ai.e>] [--fne <path>] [--json] [-q]
```

- 不传输出路径 → 默认产出 `<原名>_ai.e`，**绝不覆盖原 `.e`**。
- 退出码：0 = 成功；非 0 = 失败。
- **成功判据两道都要过**：① 产物 md5 ≠ 输入；② 产物二进制能搜到库 Key 的 ASCII。
- 库信息（Key/Guid/库名/版本）来源顺序：① `<fne 同名>.libinfo.txt` → ② 同目录 `offset_probe.exe --dump-libinfo <fne>`（仅开发环境有）→ ③ 内置回退常量。**这样 `.fne` 重建后 Guid 漂移会被自动带上**，不会出现「加壳产物声明旧 Guid → 加载失败」。
- 依赖：仅 Python 3.9+ 标准库；需要 `e2txt` 在 `PATH`。

### `scripts/elaunch.py` —— 调试运行 + 流式收日志（453 行）

```bash
python scripts/elaunch.py <加壳后的_ai.e> --log <path> [--timeout 30] [--stop-after <秒>]
                          [--json] [--fne <path>] [--home <dir>] [--tag <name>] [-q]
```

- **stdout 只放结果 JSON**；Tee 进度走 stderr；宿主 stdout/stderr tee 到 `<TAG>_harness.txt`。
- 退出码：`0` = 调试会话正常收尾（ok/stopped）；`1` = crashed/timeout；`3` = 启动器自身失败。

**输出 JSON 契约**：

```json
{"ok":true,"status":"ok|timeout|stopped|crashed",
 "targetExitCode":0,"eExitCode":0,"runSeconds":1.2,
 "logFile":"...","panelSeen":true,"logBytes":3123}
```

**`status` 判定口径（产品约束，判错会误导用户）**：

| status | 判定条件 |
|---|---|
| `crashed` | `e.exe` 的调试子进程**退出码 ≠ 0**（如 `0xC0000005`） |
| `stopped` | **我们主动发过** `FN_END_RUN`，且观测到运行态标志 `enabled` 由 `1` 回落到 `0` |
| `timeout` | 超时后运行态标志**仍未回落** |
| `ok` | 目标程序**自然结束**（未被我们中断、也非异常） |

- ❌ **禁止用面板尾行判**（理由见 §5 ② 的警告框）。

### `scripts/readlog.py` —— 读日志（132 行）

```bash
python scripts/readlog.py <日志> [--business] [--follow] [--raw] [--json] [--idle 秒] [--timeout 秒]
```

- 默认 **GBK → UTF-8** 打到 stdout；`--business` 只留业务行（`[HH:MM:SS] * …`）；`--follow` 增量跟随；`--raw` 原始字节；`--json` 结构化。
- 退出码：`0` 正常；`2` 文件不存在。

---

## 7. 目录地图（谁是谁）

| 路径 | 是什么 | 注意 |
|---|---|---|
| `Releases/Elang-AiTools/` | **发布区，只放成品**（3 技能 + 3 安装入口） | 这里的东西要能原样发给任何人 |
| `Releases/.../skills/elang-debug/` | **本次新建的正式技能（9 个文件）** | `SKILL.md` 122 行 |
| `tools/` | 脚本真源：技能脚本（`mkcage`/`elaunch`/`readlog`/`echeck`/`mkproj`/`rtcheck`/`efix`）+ **纯开发脚本**（`pack-skill`/`mk-cmd`/`verify-release`/`scan-machineinfo`/`sync-release.sh`） | 开发脚本**不要**拷进技能 `scripts/` |
| `src/` | `elibdoc.c`（导出支持库文档工具）+ `addin/`（宿主支持库源码，**本次从 `re/addin/` 提升为正式源码目录**） | 32 位 mingw 编译 |
| `src/addin/` | `elang_addin.cpp`(51560 B) / `.def` / `version.rc` / `offset_probe.cpp`（含 `--dump-libinfo`）/ `load_test.cpp` / `sdk_compat.h` / `build.sh` | `build.sh` 产出**可复现验证版** `.fne`（cmp 仅差 6 字节 PE 时间戳） |
| `docs/` | **内部工作文档**：`方案-无IDE运行调试易语言.md`(v5, 82914 B)、`分析-易语言主程序与官方扩展接口.md`(39127 B)、`开发工程说明.md`、`易语言文本格式规范.md`(24713 B) | ⚠️ **前三个严禁进技能包**；只有 `易语言文本格式规范.md` 是成品 |
| `re/` | **全部是实验 / 逆向脚本与物证**（约 60 个文件 + 子目录） | **非交付产物，未清理**（见 §8 ④） |
| `re/addin/evidence/` | **PoC 物证（最重要的证据目录）** | 见 §5 |
| `re/verify_libsource.py` / `verify_libsource2.py` | **加壳通道的四组受控实验** | 结论的反证来源 |
| `demos/` | 三个在 IDE 里实测过的 demo | 含 `.e` / `.txt` / 文本目录 |
| `dist/` | 三个技能 zip | `elang-debug.zip` 55,094 B |
| `.workbuddy/memory/MEMORY.md` | **项目长期记忆（跨会话必读）** | 约 9 KB |
| `.workbuddy/memory/2026-09-21.md` | 当日工作日志（append-only） | 约 280 行 |

---

## 8. ⚠️ 明确断点（**接手者从这里开始，按优先级**）

这一节是本文最重要的部分 —— **哪些地方是「断的」，照实说。**

| # | 优先级 | 断点 | 现状 | 建议动作 |
|---|---|---|---|---|
| ① | **P0** | **`docs/方案-无IDE运行调试易语言.md` 停在 v5，未回填 PoC-2 结论** | 架构师已明确标注这是「**最实的一处断裂**」：方案文档里 PoC-2 还是「待做」，但实际**已做完且全通过** | 把 §5 的物证结论回填进 D.4，版本升 v6 |
| ② | **P0** | **`e2t → t2e` 对真实大工程有损，未实测** | 实测一个 1.23 MB 工程往返后 **→ 1.00 MB，385 条 `[错误]`** ⇒ **加壳产物在真实大工程上可能编不过** | 拿一个真实规模的 `.e` 跑完整链路，量化损失；必要时给出「大工程禁用/降级」的明示警告 |
| ③ | P1 | **`docs/sequence-diagram.mermaid` / `docs/class-diagram.mermaid` 从未创建** | 架构师在方案里承诺过 | 补图或用 §3 的 ASCII 链替代并同步删掉引用 |
| ④ | P1 | **`tools/readlog.py` 未经过接口评审** | 它是架构师在 `SKILL.md` 里先引用、工程师为让打包通过才补的，**不在原 CLI 契约评审范围内** | 按 §6 的契约复核一遍（参数、退出码、`--business` 正则），或补评审记录 |
| ⑤ | P1 | **`re/` 下非交付产物未清理** | 约 60 个文件，含 `re/skill_smoke/`（冒烟）、`re/poc2_run.py`、`re/mkcage.py`（PoC 版，与 `tools/mkcage.py` 同名易混）、`re/projD_*.e` 等 | 归档到 `re/_archive/` 或加 README 说明；**至少把 `re/mkcage.py` 改名**避免与成品混淆 |
| ⑥ | P2 | **git 有 16 项未提交改动** | 分支 `main`，最后提交 `bafd77e`；`re/`、`src/addin/`、`tools/{elaunch,mkcage,readlog}.py`、两份 docs 全部 **untracked** | 建议先按主题分 3~4 个 commit 落盘（技能实现 / 发布链路 / 实验物证 / 文档） |
| ⑦ | P2 | **残留一个不可杀僵尸 `e.exe` PID 49560** | User=N/A、无窗口、约 20 K 内存、**非本会话产生** | 重启机器自然消失，**不要去 `taskkill /F /IM e.exe`**（见 §9） |
| ⑧ | 参考 | 三个待拍板问题用户未答复，但**实现中已按推荐方案落地** | (a) 是否接受运行期写 `lib\` → **接受**（用完复原 + diff 自证）；(b) 异常处置口径 → **每会话独立 e.exe + 报退出码**；(c) 交互式单步/断点是否纳入 → **不纳入** | 如用户后续改主意，从这三处改 |

---

## 9. 环境与纪律（红线，**违反会伤到用户**）

### 9.1 铁律

1. 🔴 **绝不 `taskkill /F /IM e.exe`** —— 会**连带杀掉用户正在编辑的 IDE 里没保存的东西**。**只用精确 PID 终止**（`elaunch.py` 已这么做）。
2. 🔴 **`D:\ides\e\` 默认只读** —— 整条链路只允许**写 / 删一个** `lib\elang_addin.fne`，且在 `finally` 里删除并对安装目录做 **before/after `diff` 自证还原**。
3. 🔴 **绝不改用户的原 `.e`** —— 加壳只在副本上作业。
4. 🔴 **`docs/` 里的内部工作文档（方案 / 分析报告 / 开发工程说明）严禁进技能包**（满本机路径）。曾经出过一次事故：`sync-release.sh` 的 `cp docs/*.md` 把三份内部文档塞进技能 references，**10 目标 × 3 文件 = 30 副本**；已修（显式名单 + `verify-release.py` references 白名单闸门）+ `re/clean_stray_refs.py` 清理。
5. 🔴 **技能正文不准写本机信息**（绝对路径 / 本机统计数 / 版本号 / 个人信息）。外部依赖只写「怎么找」。检查器：`python tools/scan-machineinfo.py`。
6. 🔴 **证据文件必须按运行唯一命名**（`--tag`）。曾经因为脚本开头 `os.remove()` 预删、且最后一次跑的是崩溃那一跑，**把成功证据覆盖掉了**，差点误判为「编造」。现在改为：不预删、唯一命名、capture 直接落 `re/addin/evidence/`。
7. 🟡 **可疑的异常程序要隔离运行** —— 被调试程序运行期异常可能**带崩 `e.exe`**（第三方 `iControls.dll` 链路，见 §5 ③）。每个会话用独立 `e.exe`。
8. 🟡 **`efix.py` 的 `apply` 不要用于交付物**（清变量 flags bit3 → **IDE 打不开**）。`t2e` 产物**原样交付**。
9. 🟡 **`reg.exe` 会被本机安全策略拦截** → 改注册表 / 读注册表请用 Python `winreg`。

### 9.2 本机环境（换机器需重新确认）

| 项 | 路径 |
|---|---|
| 易语言 | `D:\ides\e`：`e.exe`（IDE，内含编译器）、`lib/*.fne`、`static_lib/`、`linker/VC6..VC10`、`tools/link.dll`、`samples/` |
| e2txt | `D:\Tools\e2txt\e2txt.exe`（+`-gui`）；同目录 `BaseELangIDE.dll` |
| 32 位编译 | `C:\msys64\mingw32\bin\g++`（**必须先把 `dirname $GCC` 前置进 `PATH`**） |
| Python | `C:\Users\MadeSpark\.workbuddy\binaries\python\versions\3.13.12\python.exe` |

### 9.3 工具链踩坑（会「静默失败」，最耗时间）

- ⚠️ **mingw gcc 只给全路径而目录不在 `PATH` → 会 `exit 1` 且不打印任何错误**（gcc 找不到自己的 `cc1`/`as`/`ld`）。表象是「脚本跑完但产物没更新」。
- ⚠️ **`bash x.sh 2>&1 | tail` 的退出码是 `tail` 的**，会掩盖真实失败。
- ⚠️ **Bash 工具的 heredoc 会把反斜杠换成斜杠**（`\s` 变 `/s`）。凡是要用反斜杠的 Python（正则 / Windows 路径），**先写成 `.py` 文件再跑**，别用 `python - <<'EOF'`。
- ⚠️ **`cut -c` 按字节切 UTF-8 会切断多字节字符 → 显示成乱码**（文件本身没坏）。检查编码请用 Python 按字节验证。
- ⚠️ `windres` 的 `.rc` 注释必须 `//`，**不能 `;`**。
- ⚠️ 32 位编译：`-D__GCC_` + `-fpermissive` + `-finput-charset=UTF-8 -fexec-charset=GBK` + `-static`。
- ⚠️ 命令行路径：`e2txt` 的 `-src`/`-dst` 一律**绝对路径**；`-dst` 父目录**必须先建**，否则**静默不产出**。
- ⚠️ **`e2txt` 退出码恒为 0**，必须解析 stdout 的 `SUCC:` / `ERROR:`；输出为 GBK。

---

## 10. 复现健康度 / 验证命令

```bash
# 全链路（改完技能内容、或改完 tools/ 与 docs/ 之后必跑）
bash tools/sync-release.sh          # 把 tools/ 与 docs/ 按显式名单刷进发布区 + 刷本机 10 个安装点
python tools/pack-skill.py          # 打包三个 zip（会检查自包含性与跨技能引用）
python tools/verify-release.py      # ← 期望：56 项通过，0 项失败
python tools/scan-machineinfo.py    # ← 期望：干净

# 单独验证「技能真能跑」（端到端，会启动 e.exe）
python Releases/Elang-AiTools/skills/elang-debug/scripts/mkcage.py  demos/01/xxx.e /tmp/t_ai.e
python Releases/Elang-AiTools/skills/elang-debug/scripts/elaunch.py /tmp/t_ai.e --log /tmp/t.debug.log --json
python Releases/Elang-AiTools/skills/elang-debug/scripts/readlog.py /tmp/t.debug.log --business

# 复核核心结论（不启动易语言，纯离线）
python re/verify_libsource.py       # 加壳通道四组对照：只有「改 config.json」那组 md5 变化且含 key

# 复核 PoC-2 物证
grep -n "嵌套压测完成" re/addin/evidence/runA_capture.utf8.txt
grep -nE "FN_END_RUN|长任务开始|长任务结束" re/addin/evidence/runB_capture.utf8.txt
```

---

## 11. 建议的下一步（按这个顺序）

1. **先做 §8 ①②（P0）** —— 回填方案文档、量化大工程往返损失。这两条是**唯一可能让「已跑通的结论」失效**的点。
2. 再清 ③④⑤（补图 / 评审 `readlog.py` / 整理 `re/`）。
3. 然后 ⑥ 把 16 项改动分主题提交。
4. 最后考虑**能力扩展**（当前明确未做）：单步 / 断点 / 变量查看（`FN_STEP_INTO` / `FN_SET_BREAK_POINTER` / `FN_VIEW_VAR` 已定位，接口清单在 `re/`，但**未纳入本轮范围**）。

---

## 附 A. 关键文件清单（大小核实于 2026-09-21）

| 文件 | 大小 / 行数 |
|---|---|
| `Releases/.../skills/elang-debug/SKILL.md` | 122 行 |
| `.../elang-debug/references/加壳流程.md` | 61 行 |
| `.../elang-debug/references/接口与调用约定清单.md` | 39 行 |
| `.../elang-debug/references/踩坑清单.md` | 38 行 |
| `.../elang-debug/scripts/mkcage.py` | 308 行 |
| `.../elang-debug/scripts/elaunch.py` | 453 行 |
| `.../elang-debug/scripts/readlog.py` | 132 行 |
| `.../elang-debug/assets/elang_addin.fne` | 59,904 B |
| `src/addin/elang_addin.cpp` | 51,560 B |
| `docs/方案-无IDE运行调试易语言.md` | 82,914 B / 622 行（v5） |
| `docs/分析-易语言主程序与官方扩展接口.md` | 39,127 B / 431 行 |
| `docs/易语言文本格式规范.md` | 24,713 B |

## 附 B. git 状态（交接时）

- 分支 `main`，最后提交 `bafd77e`（`chore(gitignore): 忽略Releases目录下的发布压缩包`）。
- **16 项未提交**：7 项 modified（`README.md`、`dist/{e2txt-cli,elang-ai-coding}.zip`、`docs/易语言文本格式规范.md`、`tools/{echeck.py,sync-release.sh,verify-release.py}`）+ 9 项 untracked（`.zcodeignore`、`dist/elang-debug.zip`、`docs/{分析-…,方案-…}.md`、`re/`、`src/addin/`、`tools/{elaunch,mkcage,readlog}.py`）。

## 附 C. 背景记忆在哪（接手前建议先读）

- `.workbuddy/memory/MEMORY.md` —— **项目长期记忆**（目录职责、技能机制、本机环境、易语言调试硬约束、格式铁律、用户偏好）。**最有价值的一页。**
- `.workbuddy/memory/2026-09-21.md` —— 当日日志，含「★ 关键反转：加壳的输入通道是 config.json」与「★ 二进制直插备选摸底」两大节。
- `~/.workbuddy/MEMORY.md` —— 跨项目的用户级记忆（e2txt 坑、精易模块、Unicode 口径、技能通用规范）。
- 技能：`e2txt-cli`（`.e` ⇄ 文本）、`elang-ai-coding`（写对易语言）、`elang-debug`（跑起来 + 抓日志）。
