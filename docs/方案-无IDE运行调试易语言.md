# 方案列表：无 IDE 运行 + 调试易语言程序（决策文档）

> 状态：**v6 —— 决策已定并落地**（选路 A④a，技能 `elang-debug` 已发布；本文保留完整决策与论证过程，含被否路线）。
> 撰写：架构师 高见远 ｜ 项目：`C:\Users\MadeSpark\Desktop\测试`（Elang-AiTools）
> 需求原文：新增一个易语言代码调试技能，支持传入 `.e` 文件路径作为参数，在无需打开易语言的情况下直接运行并调试程序，同时让 AI 能通过该工具实时查看该程序的运行日志，效果与易语言正常运行源码时的调试输出窗口一致。

---

## 阅读导航

- [A. 项目现状梳理](#a-项目现状梳理简表)
- [B. 可行性评估（需求拆 4 个能力点）](#b-可行性评估)
- [C. 方案列表（二维组合矩阵：技能组织方式 × 技术路线）](#c-方案列表核心交付物)
  - [C.4 路线④：宿主化 e.exe（新增，来源：工程师实证报告）](#c4-路线宿主化-eexe新增来源工程师实证报告)
- [D. 决策辅助（对比矩阵 / 推荐 / 待拍板问题 / 分阶段落地）](#d-决策辅助)
  - [D.5 给目标 `.e` 加壳流程（规范，v5 订正因果）](#d5-给目标-e-加壳流程规范v4-新增v5-订正因果)
  - [D.6 正式技能 `elang-debug` 包内文件清单（自包含规划）](#d6-正式技能-elang-debug-包内文件清单自包含规划v4-新增)
- [附：本方案用到的关键事实与出处](#附本方案用到的关键事实与出处便于复核)

> **v2 更新说明（本版新增）**：并入工程师寇豆码的实证报告 `docs/分析-易语言主程序与官方扩展接口.md`（431 行，逐条带 `文件:行号`/RVA 证据）与证据目录 `re/`，新增**技术路线 ④「宿主化 e.exe」**（④a 官方宿主化 / ④b 外部 DLL 注入），并据此**重新排布推荐与优先级**、扩充矩阵、新增待拍板条目、把 PoC-1/2/3 纳入分阶段落地。
> **v3 更新说明（本版新增）**：并入工程师 PoC-1/任务 D 的实证（`re/addin/POC1-结果与阻塞.md`、`re/addin/POC1D-免登记成功与__stdcall根因.md`、`re/addin/evidence/D_trace_full.txt`）：① 纠正「e.exe 启动即全量加载 `lib\*.fne`（77/77）」为「**已登记集合 76/77 + 按需加载**」；② 新增 **`__stdcall` 调用约定**（SDK 头写 `__cdecl` 属误导，照抄会崩）；③ **任务 D「免登记按需加载」已跑通**、`FN_COMPILE_AND_RUN` `enabled=1`；④ **PoC-1 / 任务 D 状态标为已完成**。
> **v4 更新说明（本版新增）**：为正式技能 `elang-debug` 补技术底稿，新增/扩充 4 处：① A.4「**跨 DLL 函数指针约定清单**」（表 + 可执行规则）；② **D.5「给目标 `.e` 加壳流程（规范）」**；③ D.4 阶段一把 **PoC-2 判据写死**（判据 A/B/C + 三种收尾 + 备选通道）；④ **D.6「正式技能 `elang-debug` 包内文件清单」**。**凡不宜随技能外发的本机信息，均以 `⟨待通用化⟩` 标注（不删事实，只标识）。**
> **v5 更新说明（本版新增）**：**订正 D.5「加壳」的因果**（此处已按实证修正）——v4 把「改文本 `.支持库` 行」当输入，**因果写反了**；受控实验（`re/verify_libsource.py` / `verify_libsource2.py`）证明 **`配置/支持库.config.json` 才是 `t2e` 的输入**（产物 6818 B / `ff549e9c` / 含 `elang_addin`），文本里的 `.支持库 <key>` 行**完全无效**（产物与基线逐字节相同、换已安装的 `cncnv` 亦然）。据此重写 D.5「三步」表 + 坑块（改因果 / 双判据闸门 md5+二进制含 key / 顺序「先 e2t→改 json→t2e，之后绝不再 e2t」）、D.6 `scripts/mkcage` 说明与 C.4 表述**同步新口径**；`t2e` 回写一律 **`-level 2`**（`-level 1` 会静默丢空产物）；并**采信**工程师最初「改 `config.json`」的做法为正确。
> **v6 更新说明（本版新增，2026-09-22 回填）**：**回填 PoC-2 结论（本文档此前最大的一处「文档与现实断裂」，至此消除）**——PoC-2 **已完成且判据 A/B/C 全部通过**（三问物证在 `re/addin/evidence/run{A,B,C}_*`，可 grep 复核，见 D.4）。据此：① C.4 路线④a 的「输出面板 HWND/类名尚未坐实」→ **已坐实**（`ScintillaForEIDETools` 为主、另有 `Edit` 旧面板，双通道取文本）；「风险与验证」仅剩 **PoC-3（无窗口启动）**开放；② D.4 阶段一收口：**P0 探针（DBWIN）不再执行**——主路线已定为 ④a 且**不依赖 DBWIN 假设**（该环节仅当未来启动路线①时才需要）；③ 阶段二/三标记**已完成**：正式技能 `elang-debug` 已发布（`mkcage` / `elaunch` / `readlog` 三脚本 + `elang_addin.fne`，发布链路总检 56 项通过 / 0 失败）；④ 补充**真实工程规模实测**：7 个 0.66~3.9 MB 真实工程 `e2t→t2e` 往返零 `[错误]`、加壳产物全链路可编译可运行（含内嵌双 `.ec` 模块的工程），**「大工程可能编不过」的担忧被实测排除**（详见 D.4 阶段一收尾）。**未做且如实标注**：PoC-2(c) 的 DBWIN 并行验证未做（④a 直读面板已够用）；`FN_ADD_TAB` 自建日志夹未启用（实际出口 = 轮询面板取差分）；PoC-3 未做（当前链路以可见窗口运行）。
> **并稿原则**：工程师的结论**凡带证据者一律采信**；与原方案冲突处**以实证为准**，并在文中显式标注「此处已按实证修正」。

**一句话结论（先看这个）**：
技术上**可行但有条件**。原先唯一的关键未知量——「调试版本 exe 在无 IDE 宿主时是否仍向系统调试通道（DBWIN）输出日志」——**已被二进制级证据大幅加强**（核心库 `krnln.fnr` 与 `krnln.fne` **均导入 `KERNEL32!OutputDebugStringA`**，见 B.3），只剩「**无宿主时是否仍投递**」这一环待**活体探针**收口。
工程师侦察另开一条**技术路线 ④「宿主化 e.exe」**：把自写 `.fne`（AddIn）放进易语言 `lib\`，**当被打开的 `.e` 声明了本库时由 e.exe 按需加载**（任务 D 已实证；**不是**「丢进去就自动加载」），再用**官方接口** `NES_RUN_FUNC(FN_COMPILE_AND_RUN / FN_OPEN_FILE2 / FN_ADD_TAB …)` 驱动编译执行、并可触达输出面板——**它不需要第三方 `ecl.exe`，让「单步 / 断点」首次变得可行，且 AddIn 骨架已由 PoC-1 打通**（见 C.4）。
**推荐（v3）**：**新增独立技能 `elang-debug`**；技术路线**并列首选为 ④a（官方宿主化，已由 PoC-1/D 实证可跑通、无第三方依赖）与 ①（命令行调试版 + DBWIN，若 P0 探针通过）**，②（源码插桩）为**确定可行的兜底**，④b（外部 DLL 注入）仅作「禁止写 `lib\`」时的极端兜底。

**一句话结论（v6 定稿）**：**已落地**。选路 = **A④a（独立技能 + 官方宿主化）**，正式技能 `elang-debug` 已发布并装到本机各 AI 工具（PoC-1 / 任务 D / PoC-2 全部实证通过，发布链路总检 56 项通过 / 0 失败）；路线①（DBWIN）**未采纳**（其 P0 探针随之关闭），②/④b 未采纳；唯一开放项 = **PoC-3 无窗口启动**（不阻塞主功能，当前以可见窗口运行）。

---

## A. 项目现状梳理（简表）

### A.0 一句话定位
`C:\Users\MadeSpark\Desktop\测试` 是一个**让 AI（40+ 种 Agent 工具）真正能读、能写、能验证易语言 `.e` / `.ec` 代码**的工具链；`Releases/Elang-AiTools/` 是只放成品的发布区；核心资产是**两个 Agent Skills**（`elang-ai-coding`、`e2txt-cli`）+ 一个 32 位 C 写的支持库文档导出 exe。

### A.1 目录职责表

| 目录 / 文件 | 职责 | 与本次需求的关系 |
|---|---|---|
| `Releases/Elang-AiTools/` | 发布区（**只放成品**，无中间物） | 新技能最终落此 |
| `…/skills/elang-ai-coding/` | 编码技能：格式铁律 + 支持库查询 + GUI 工程产出（SKILL.md ≈ 640 行，自包含） | **可复用其脚本/echeck/规范** |
| `…/skills/e2txt-cli/` | e2txt 命令行技能（单文件） | 复用 e2txt 证据链 |
| `…/install-skills.py` | 跨工具安装器，顶部 `GLOBAL_TARGETS` 表（10 目标，探测式） | 新技能会被**自动发现并安装**（无需改名） |
| `…/安装技能.cmd` | Windows 双击安装（GBK+CRLF，源模板 `tools/mk-cmd.py`） | 不因新增技能而变 |
| `…/使用说明.md` | 发布区说明书（含技能表格 + 目录树） | **需改**（加新技能一行 + 目录树） |
| `src/` | `elibdoc.c` + `build.sh` + `test_out.c`（导出工具源码/构建/编码回归） | 新技能自带 exe（DBWIN 监听器）可仿此风格 |
| `tools/` | 技能脚本副本（`echeck/mkproj/rtcheck/efix.py`）+ 开发脚本（`pack-skill.py`/`mk-cmd.py`/`verify-release.py`/`scan-machineinfo.py`/`sync-release.sh`） | **需改 sync-release.sh 名单**；`pack-skill.py` 自动发现 |
| `docs/` | `易语言文本格式规范.md` + `开发工程说明.md` + **`分析-易语言主程序与官方扩展接口.md`** | `docs/*.md` 会被同步进技能 `references/`；**新分析报告是路线④的证据源** |
| `demos/` | 01/02/03 三个**实测可编译**工程样本 | **P0 探针的现成素材** |
| `re/` | **工程师的逆向/运行时证据目录**（14 个脚本 + `mods_noproj.txt`/`proj_out.txt`/`reg.txt` 等证据文件 + `pylib/pefile`） | **路线④的全部实证来源**（只读 `D:\ides\e\`，不常驻进程） |
| `dist/` | 技能分发包 zip（zip 根即 SKILL.md） | 新技能自动出第三个 zip |
| `支持库文档/` | exe 生成物（`commands.jsonl` 等） | echeck 在用 |

### A.2 skill 机制与扩展方式（要点）
- 遵循 **Agent Skills 开放规范**（agentskills.io）：frontmatter 仅 `name` + `description`；`name` 匹配 `^[a-z0-9]+(-[a-z0-9]+)*$` 且与目录名一致。
- **技能必须自包含**：脚本/工具/文档全放 `scripts/`、`references/`、`assets/`，安装是整个文件夹一起拷。
- **正文不写本机信息**（绝对路径、本机统计数、模块版本、个人信息）；外部依赖只写「怎么找」（环境变量 → PATH）。
- 安装：`install-skills.py` 顶部 `GLOBAL_TARGETS` 一张表；**技能由 `scan_skills()` 自动发现**（扫 `skills/` 下含 `SKILL.md` 的一级子目录）——**新增技能不需要动目标表**。Claude Code / Trae / WorkBuddy 不读共享根，须单独装。
- 打包：`python tools/pack-skill.py` → zip，**zip 根直接是 SKILL.md**；打包前按规范校验（`name` 正则、`description` ≤1024、引用文件真实存在）。
- 同步：`bash tools/sync-release.sh`（**显式名单** `SKILL_SCRIPTS="mkproj.py rtcheck.py efix.py echeck.py"`；并把 `docs/*.md` 拷进 `elang-ai-coding/references/`）。
- 总检：`python tools/verify-release.py`（7 项，只读）。

### A.3 现有工作流的瓶颈点

```
echeck（静态语义）→ rtcheck（格式收敛）→ e2txt t2e 生成 .e → ★ 易语言 IDE 人工编译运行（终审）★
```

- README 明确写着：**唯一验收标准 = 能不能在易语言 IDE 里打开并编译运行**。
- ⇒ **瓶颈就是最后一步**：必须有人打开 IDE、肉眼编译、肉眼读输出窗口。AI 无法自己闭环。
- 新技能的本质 = **把「人工进 IDE 终审」自动化**：AI 自己编译调试版 → 自己跑 → 自己读日志 → 自己定位问题。

### A.4 e.exe 静态画像与官方扩展能力（新增，来源：`docs/分析-易语言主程序与官方扩展接口.md`）

> 以下为工程师实测/逆向结论，均带 `文件:行号` 或 RVA 证据；对写方案有用的部分摘录如下。

**e.exe 静态画像**

| 项 | 值 |
|---|---|
| 平台 | 32 位 x86 / GUI 子系统 / Linker 6.0（VC6/MFC）；ProductName=易语言 **v5.9.0.0**（标题含「加密狗版」），时间戳 2021-05-29 |
| 保护 | **4 节、无壳**（节熵 < 6.7）；**无 TLS、无重定位目录**；`DllCharacteristics=0`（**无 ASLR/DEP/SafeSEH**） |
| 导出表 | **无**（⇒ 注入后无官方函数可调，只能逆向 → 这是 ④b 的最大成本） |
| 资源 | MENU×17 / DIALOG×56 / STRING×47 / ACCELERATOR×1 … |
| 导入（关键） | `CreateProcessA`、`GetCommandLineA`、`LoadLibraryA`、**调试 API 全套**（`WaitForDebugEvent`/`ContinueDebugEvent`/`SetThreadContext`/`WriteProcessMemory`…）、`OpenFileMappingA`+`MapViewOfFile`+`CreateEventA`（DBWIN/IPC）、`RegisterWindowMessageA`、`SetWindowsHookExA`、`SetWindowLongA`/`CallWindowProcA`（可挂钩/子类化）、注册表全套、SETUPAPI/HID/WinSCard（**加密狗**） |
| 导入（未含） | **未导入** `OutputDebugString*`、`CreateRemoteThread`、`IsDebuggerPresent`、`ws2_32` |
| 主窗口类名 | **`ENewFrame`**（RVA `0x1FE630` + 运行时枚举一致） |
| 命令行 | **支持 `e.exe "<x>.e"` 直接打开工程**（实测生效） |

**官方扩展链路（打通路线④a 的钥匙，已证实）**
- 机制：支持库（`.fne`）导出 `GetNewInf` → 填 `LIB_INFO.m_pfnNotify`；系统通过 `PFN_NOTIFY_LIB` 下发 `NL_SYS_NOTIFY_FUNCTION(=1)`，`dwParam1` 即 `PFN_NOTIFY_SYS` 指针；库保存它，之后用 `NotifySys(NES_GET_MAIN_HWND)` / `NotifySys(NES_RUN_FUNC, 功能号, &{p1,p2})` 反过来驱动 IDE。
  证据：`sdk/cpp/elib/lib2.h:1016-1028,1173-1177,1242-1251,1345,1363`；官方实现 `sdk/cpp/elib/fnshare.cpp:26-55`；官方示例 `sdk/cpp/samples/HtmlView/HtmlView.cpp:364-378,449,460-463`、`HtmlView.def:4-7`。
- ⚠️ **调用约定（v3 实证修正，必须标注）**：官方 SDK `mtypes.h:9` 把 `WINAPI` 定义为**空**，导致头文件里 `PFN_NOTIFY_LIB`/`PFN_NOTIFY_SYS` 展开成 **`__cdecl`**——**这是误导**。实测 e.exe **按 `__stdcall` 调用**：e.exe 调用 `m_pfnNotify` 处（`e.exe+0x460872..0x46087A`）调用后**没有 `add esp`**（期望 callee 清栈）；真库 `cncnv/dp1/console/iext` 的 notify 入口均以 `ret $0xc` 返回；`PFN_NOTIFY_SYS=0x00467FF0` 末尾也是 `ret $0xc`。⇒ **凡跨 DLL 边界「由 e.exe 回调 / 回调 e.exe」的函数指针，一律显式 `__stdcall`**；照头文件写 `__cdecl` 会栈错位、e.exe 崩（这正是「打开引用本库的 .e 即崩」的真正根因）。`GetNewInf` 无参不受影响；`m_pfnRunAddInFn(INT)`/`m_pfnSuperTemplate(INT)` **待实测**（真库样本见 `ret $0x4` 迹象）。
  证据：`re/addin/POC1D-免登记成功与__stdcall根因.md`。
- ✅ **加载机制（此处已按实证修正）**：原分析报告的「e.exe **启动即全量加载** `lib\*.fne`（77/77）」**不准确**。PoC-1 实测：e.exe 启动加载的是一份**「已登记 / 已选择」集合**（实测 **76/77**，`etools.fne` 未加载），**新丢进 `lib\` 的 `.fne` 不会自动加载**（重启、优雅关闭都不登记）。⇒ 让 `.fne` 进入 IDE 进程有两路：**(i) 全局登记**——走「工具→支持库配置」（任务 A，**未打通 / 阻塞**）；**(ii) 按需加载**——当 e.exe 打开的 `.e` **声明了该库**（即其库表 `配置/支持库.config.json` 里登记了本库 Key）时，它会**按需**去 `lib\` 加载我们的 `.fne`、调 `GetNewInf`、下发通知，**全程无需全局登记**（任务 D，**已跑通**）。
  证据：`re/mods_noproj.txt`、`re/compare.py`、`re/addin/POC1-结果与阻塞.md`、`re/addin/POC1D-免登记成功与__stdcall根因.md`。安装位置取自注册表 `HKCU\Software\FlySky\E\Install\Path`。
- ✅ **任务 D 成功（免登记按需加载）**：e.exe 打开声明 `Key=elang_addin` 的 `.e` → 按需加载 `lib\elang_addin.fne` → `GetNewInf` 被调用 → 收到 `NL_SYS_NOTIFY_FUNCTION`×2 与 `NL_IDE_READY(=18)`；`NES_GET_MAIN_HWND` 得 `ENewFrame`（visible=1）；**`FN_IS_FUNC_ENABLED` 全表 `handled=1`，且 `FN_COMPILE_AND_RUN` 当前 `enabled=1`**（⇒「调试运行」可被程序化触发；`FN_STEP*`/`FN_END_RUN` 当前=0，符合「未处于运行/调试态」）。证据：`re/addin/POC1D-免登记成功与__stdcall根因.md`、`re/addin/evidence/D_trace_full.txt`、`re/opens_decl_e.py`。
- 建议 AddIn 在 `LIB_INFO.m_dwState` 置 `LBS_IDE_PLUGIN(1<<8)` 以接收 `NL_IDE_READY`（`lib2.h:1310-1311,1193-1195`）。

**两条并存路径（对调试输出的关键理解）**
- **调试输出底层是 `OutputDebugStringA`**（`krnln.fnr` 运行版 / `krnln.fne` 编辑器版**均导入**该 API，`re/probe_debug_io.py`）——**为 B.3 的 P0 假设提供了直接二进制证据**。
- **e.exe 自己就是调试器**（导入 `WaitForDebugEvent`/`ContinueDebugEvent`/`SetThreadContext`/`WriteProcessMemory`；字符串「开始运行被调试程序」「★ 被调试程序」「被调试易程序运行完毕」「DebugActiveProcessStop」）⇒ 调试运行由 IDE 用 **Windows 调试 API** 托管子进程。
- ⇒ 调试运行时，`OutputDebugStringA` **同时**产生 `OUTPUT_DEBUG_STRING_EVENT` 给**已附加的调试器（e.exe）**、**又**写入 **DBWIN 缓冲区**给任意 DBWIN 监听者。

**跨 DLL 函数指针约定清单（v4 新增；供技能 `references/接口与调用约定清单` 使用）**

> 纪律：SDK 头 `mtypes.h:9` 把 `WINAPI` 置空 ⇒ 头里所有 `(WINAPI *)` 都**展开成 `__cdecl`**，**不可信**；实际约定**以 e.exe 反汇编为准**（见下）。`⟨待通用化⟩`：进技能前，地址/RVA 类证据改为「方法 + 现象」表述，不保留本机偏移。

| 字段 / 符号 | 所在结构 / 来源 | 实测约定 | 证据（地址 / `ret $N` / 行号） | 现状 |
|---|---|---|---|---|
| `LIB_INFO.m_pfnNotify`（`PFN_NOTIFY_LIB`） | 本库导出，**e.exe 回调** | **`__stdcall`** | e.exe 调用处 `e.exe+0x460872..0x46087A` 后**无 `add esp`**；真库 `cncnv@0x100010F0`/`dp1@0x10003550`/`console@0x10001500`/`iext@0x10001B80` 的 notify 均以 **`ret $0xc`** 返回（`lib2.h:1242,1247,1345`） | **已证** |
| `PFN_NOTIFY_SYS`（`NL_SYS_NOTIFY_FUNCTION` 下发） | IDE 提供，**本库回调 e.exe** | **`__stdcall`** | 实测指针 `0x00467FF0`，末尾 **`ret $0xc`**（`lib2.h:1173-1177,1244,1249`） | **已证** |
| `GetNewInf`（`PFN_GET_LIB_INFO` / `FUNCNAME_GET_LIB_INFO`） | 本库导出，**e.exe 调用** | 无参 | 约定对**无参函数无影响**；导出名未修饰（`lib2.h:1363-1364`） | **已证（不受影响）** |
| `LIB_INFO.m_pfnRunAddInFn`（`PFN_RUN_ADDIN_FN`，`INT(INT)`） | 本库导出，运行时/IDE 回调 | **待实测** | 真库样本**疑似 `ret $0x4`**（→ 指向 `__stdcall`），**未定点验证，勿当结论**（`lib2.h:1261,1338`） | **待实测** |
| `LIB_INFO.m_pfnSuperTemplate`（`PFN_SUPER_TEMPLATE`，`INT(INT)`） | 本库导出 | **待实测** | 头注释「超级模板**暂时保留不用**」；同 `m_pfnRunAddInFn` 疑似 `ret $0x4`（`lib2.h:1263,1347-1348`） | **待实测** |
| `LIB_INFO.m_pCmdsFunc[]`（`PFN_EXECUTE_CMD*`） | 本库导出，运行时**逐条回调**执行命令 | **待实测** | 头定义 `void (*PFN_EXECUTE_CMD)(...)` **无 `WINAPI`**（头里= `__cdecl`）；**但按下方规则先按 `__stdcall` 写**（`lib2.h:1259,1336`） | **待实测**（本库 `m_nCmdCount=0`，暂不涉及） |
| `PFN_NOTIFY_PROPERTY_CHANGED` / `PFN_GET_INTERFACE` | 其它结构（**非 `LIB_INFO`**） | **待核** | `lib2.h:672` / `lib2.h:543,756` | **待核** |

> 说明：`lib2.h` 中**未见** `m_pfnGetCmdFuncName`（grep 无命中）——若后续在其它结构/头文件出现，按同一规则处理。

**可执行规则（写进技能底稿）**：
> **凡由 e.exe（或运行时）回调、或回调 e.exe 的函数指针，一律显式 `__stdcall`；不确定时先按 `__stdcall` 写，并在 trace 里记录「调用前后 ESP 差」——若不为 0 即约定不符，立即排查。**

---

## B. 可行性评估

把需求拆成 4 个能力点，逐个判定。

### B.1 能力点①：传入 `.e` 路径 → 无 IDE 编译

| 项 | 判定 |
|---|---|
| **结论** | **有条件可**（依赖引入第三方命令行编译器） |
| **依据** | 易语言**无官方命令行编译**（编译逻辑在 `e.exe` 内部）。社区成熟开源工具 `ecl.exe`（ECommandPrompt，Gitee `zhongjianhua163/ECommandPrompt`）可后台编译，支持 `make` / `-epath` / `-s`(静态) / `-d`(独立) / `-r`(调试运行) / `-ct`(超时) / `-utf8` 等；它按 `set epath`→ini→注册表→自动搜索定位易语言。 |
| **外部依赖** | ⚠️ 本机**尚未安装** `ecl.exe`（`D:\Tools\` 无）；**必须装原版易语言**（工具不绕过正版验证）。 |
| **坑** | ① 引入一个第三方二进制即引入供应链/兼容性风险；② `ecl.exe` 的 `-r`（调试运行）与 `-d`/`-s`（发布/独立编译）是**两条互斥的产物路线**，而调试输出只在**调试版本**里有效（见 B.3）；③ 支持库缺失时编译可能失败（echeck 可提前拦一部分）。 |
| **风险等级** | 中（依赖外部工具，但证据链清晰、工具成熟） |

### B.2 能力点②：无 IDE 运行程序

| 项 | 判定 |
|---|---|
| **结论** | **有条件可**（需配套「进程管理 + 超时 + 窗口策略」） |
| **依据** | 编译产物就是普通 Win32 exe，可直接 `CreateProcess` / `subprocess` 启动。但易语言程序**多为窗口程序（GUI）**： |
| **坑** | ① **GUI 程序无头运行会弹真实窗口**，可能打断用户桌面；② **死循环 / 等待输入 / 模态框**会导致挂起 → **必须有超时 + 强制结束**（`ecl.exe -ct/-st` 或自建 kill 逻辑）；③ 运行期错误（数组越界、除零）在 IDE 里弹**模态错误框并高亮行**，无头环境会**卡在模态框**上（无人点确定）→ 需要处理策略（见 B.4）。 |
| **风险等级** | 中（技术可控，但产品策略需用户拍板：窗口是否允许弹出、错误框如何处理） |

### B.3 能力点③：实时捕获运行日志

| 项 | 判定 |
|---|---|
| **结论** | **假定可，但为 P0 未验证假设**（必须做探针）；**兜底路线确定可行** |
| **核心约束** | 官方帮助文档原文：核心库 `krnln` 的 `输出调试文本`（`OutputDebugText`）与特殊功能库 `spec` 的 `调试输出`（`Trace`）**「仅在调试版本中被执行，发布版本中将被直接跳过」**。⇒ **发布/静态编译的 exe 里一条日志都没有**，必须生成**调试版本**。 |
| **主路线（v2 证据升级）** | 调试输出走 **Windows `OutputDebugStringA` → DBWIN 缓冲区**（`DBWIN_BUFFER` 共享内存 + `DBWIN_DATA_READY` 事件），任何 DbgView 类监听器可收。**原为推断，现已有二进制级直接证据**：核心库 `krnln.fnr`（运行版）与 `krnln.fne`（编辑器版）**均导入 `KERNEL32!OutputDebugStringA`**（`re/probe_debug_io.py`，已独立复核）。⇒ 可自写 **~100 行 32 位 C 的 DBWIN 监听器**（与 `src/elibdoc.c` 同风格，零依赖）。 |
| ⚠️ **P0 未验证假设（范围已收窄）** | **一个「调试版本」的易语言 exe，在完全没有 IDE 进程时，`输出调试文本` 是否仍写 DBWIN 缓冲区？还是运行时发现无宿主就丢弃？** —— v2 后这条已从「通道是否存在」的全局怀疑，**收窄为「无宿主时是否仍投递」的唯一一环**（因为底层 API 已是 `OutputDebugStringA`，通道确定存在）。 |
| ✅ **本例已按实证修正** | 原文写「DBWIN 属未完全证实的推断」—— **v2 修正为**：`krnln.fnr`/`krnln.fne` 均 import `OutputDebugStringA`，通道**有直接二进制证据**，仅剩「无 IDE 宿主时是否仍投递」待活体探针收口。 |
| **兜底路线（确定可行）** | 走 e2txt `e2t`→文本→`t2e` 往返，在**文本副本**上把 `调试输出(…)`/`输出调试文本(…)` **重写为写日志文件**或 `OutputDebugStringA`（DLL 命令），再编译运行、读日志文件。**不依赖调试版本机制**。 |
| **风险等级** | 主路线高（未验证）；兜底路线低（确定可行但工程量大） |

### B.4 能力点④：「效果与调试输出窗口一致」——逐条对齐

> 真实 IDE「输出」窗口 ≠ 纯日志：除 `输出调试文本`（自动加 `*` 前缀 + 回车换行），还含编译/链接器输出、运行期出错信息与出错位置。逐条对齐如下。

| 对齐项 | 能否对齐 | 说明 |
|---|---|---|
| **自动 `*` 前缀 + 换行** | ⚠️ 部分 | `输出调试文本` 每条会带 `*` 前缀与 CRLF。若走**源码插桩**，需自己复刻该前缀格式才能「一致」；若走 **DBWIN 原样**则保留原格式，但拿到的是纯字符串，前缀由易语言运行时加（需探针确认是否含 `*`）。 |
| **多参数 / 可变参数 `调试输出(a, b, c)`** | ⚠️ 插桩路线难点 | `spec.Trace` 是**通用型可变参数**，可接任意多个「通用型/数组」；参数类型五花八门，插桩要生成正确的 `到文本()` 包裹与数组/日期/逻辑显示格式。**DBWIN 路线天然规避此问题**（运行时自己格式化）。 |
| **数组显示形态** | ⚠️ 同上 | 插桩需复刻 `{ ... }` / `数组: N{...}` 等显示形态；DBWIN 路线免。 |
| **日期 / 逻辑型显示** | ⚠️ 同上 | 插桩需复刻 `[年月日时分秒]`、`真/假`；DBWIN 路线免。 |
| **编译期诊断（编译/链接器输出）** | ✅ 可 | 路线①：`ecl.exe` 的 stdout/stderr 可捕获，原样回读。路线④a：由 **IDE 自身**执行 `FN_COMPILE_AND_RUN`，编译/链接诊断经 IDE 输出渠道（或 `FN_ADD_TAB` 自建夹）回读。 |
| **运行期错误定位（越界/除零 + 高亮行）** | ❌ 难对齐 | IDE 弹**模态框**并高亮源码行；无头环境拿不到「高亮行」。插桩可在**关键点**自埋检查，但**不可能覆盖所有错误**。**建议本期明确划出范围外**（见待拍板问题）。 |
| **断点 / 单步 / 变量监视** | ⚠️→✅ **此处已按实证修正** | 原文「❌ 不做」是基于当时只有命令行路线的前提。**v2 修正**：路线④a 通过 IDE 官方功能号 `FN_STEP_INTO`/`FN_STEP`/`FN_STEP_OUT`/`FN_RUN_TO_CURSOR`/`FN_VIEW_VAR`/`FN_SET_BREAK_POINTER`/`FN_CLEAR_ALL_BREAK_POINTER`/`FN_SHOW_NEXT_STATMENT`/`FN_ADV_BREAKPOINT`（`PublicIDEFunctions.h:387-395`）**让交互式调试首次变得可行**。是否纳入本期需重新估成本（见 D.3 新条目）。 |

**结论**：**「日志文本本身」高度可对齐**；**「编译期诊断为可对齐」**；**「运行期错误定位」仍难对齐（建议范围外）**。**「交互式断点/单步」原划为范围外——v2 因路线④a 出现而需重新评估**（④a 有官方单步/断点功能号，技术上可行，成本待估，见 D.3）。若走插桩路线，`调试输出` 的**多参数格式化**是对齐成本最高的点。

---

## C. 方案列表（核心交付物）

采用**二维组合矩阵**：**技能组织方式（A/B/C）** × **技术路线（①/②/③/④）**。

- 技能组织方式：**A** 新增独立技能 ｜ **B** 在 `elang-ai-coding` 上扩展 ｜ **C** 混合（新增独立技能 + 复用既有能力）
- 技术路线：**①** 命令行编译调试版本 + DBWIN 监听器 ｜ **②** 源码插桩重写 `调试输出` ｜ **③** 二者并存（探针自动选择 / 开关切换）｜ **④a** 官方宿主化 e.exe（写 `.fne`/AddIn）｜ **④b** 外部 DLL 注入

> 组织方式主要影响**打包/发布/文档/安装**的改动范围；技术路线主要影响**机制**。为便于拍板，下面先分别给出**技术路线详表（①②③）**、**组织方式详表**、**组合矩阵与改动范围**，最后给出 **v2 新增的路线④详表**。

### C.1 技术路线详表（机制维度）

#### 路线①：命令行编译调试版本 + DBWIN 监听器

| 维度 | 内容 |
|---|---|
| **思路（机制）** | `ecl.exe make <源.e> -r`（调试运行）生成**调试版本** exe → 自写 **32 位 DBWIN 监听器**（`DBWIN_BUFFER` 共享内存 + `DBWIN_DATA_READY` 事件）在**无 IDE 进程**下挂起接收 → 边跑边把日志落盘/流式回传给 AI。 |
| **端到端流程** | `传入 .e` →（echeck 预检）→ `ecl.exe make -r` 出调试版 exe → 启动 `dbwinwatch.exe` 监听 → 启动 exe（带超时）→ 监听器收 `输出调试文本`/`调试输出` → 落 `run.log` → exe 退出/超时被 kill → AI 读 `run.log` + 编译输出。 |
| **适用范围** | ✅ 需**真实调试输出**、且**不想改源码**；✅ 编译期诊断也要。❌ 探针证伪时不用；❌ 需断点单步时不够。 |
| **优点** | 不动源码；日志为运行时**原生格式**（多参数/数组/日期显示天然正确）；调试/发布分离干净。 |
| **缺点/限制** | ⚠️ **依赖 DBWIN 假设成立（P0 未验证）**；⚠️ 依赖第三方 `ecl.exe`；⚠️ 无 IDE 时是否仍输出**待探针**；GUI 窗口/模态框/超时需另行处理。 |
| **风险与验证** | **P0 探针**（见 D.4）；探针失败即本路线作废。 |
| **工作量粗估** | **中**（引入 ecl + ~100 行 C 监听器 + 运行器脚本 + 超时/窗口策略） |

#### 路线②：源码插桩重写 `调试输出`

| 维度 | 内容 |
|---|---|
| **思路（机制）** | 在 `.e` 的**文本副本**（e2t 产物，绝不动原 `.e`）上，把所有 `调试输出(...)` / `输出调试文本(...)` 重写为「写日志文件」或 `OutputDebugStringA`（DLL 命令）调用 → `t2e` 回写为新的 `.e` → 编译运行 → AI 读日志文件。**不依赖调试版本机制**。 |
| **端到端流程** | `传入 .e` → `e2t` 转文本（`-level 2 -ns 2`）→ 正则/结构解析定位 `调试输出` 调用点 → 重写为写日志（自动加 `*` 前缀、复刻显示格式）→ `rtcheck` 校验格式 → `e2t`→`t2e` 回写新 `.e` → 编译运行 → 读日志文件。 |
| **适用范围** | ✅ 探针证伪后的主力；✅ 需要**插桩控制**（如给日志加时间戳/文件行号）；✅ 不依赖「调试版本」这一运行时行为。❌ 不想动源码/要求逐字节运行原 `.e` 时不用。 |
| **优点** | 机制**确定可行**（只需 e2txt + 目标编译）；日志落到**文件**，回读简单；可在插桩里附加丰富信息（时间戳、调用位置）。 |
| **缺点/限制** | ⚠️ **必须改源码副本**（工程量大）；⚠️ `调试输出` 是**通用型可变参数**，要生成正确 `到文本()` 包裹与数组/日期/逻辑显示格式才能「效果一致」——**对齐成本最高点**；⚠️ 需严守往返格式铁律（UTF-8 BOM + CRLF + 每层 4 空格）；⚠️ 插桩后的 `.e` 与原始语义可能有细微差异。 |
| **风险与验证** | 低-中。验证方式：拿 `demos/` 三个真实样本做「原 `.e` 输出」vs「插桩后输出」逐行对比。 |
| **工作量粗估** | **中-高**（插桩器 + 类型格式化 + 往返校验；若只需「能看日志」可弱化格式一致性） |

#### 路线③：二者并存（探针自动选择 / 开关切换）

| 维度 | 内容 |
|---|---|
| **思路（机制）** | 用户传入 `.e` → 工具读**能力探针缓存**（首跑时决定 DBWIN 是否可用）→ 可用则走**路线①**（不改源码、格式天然正确）；不可用 / 用户强制则走**路线②**（插桩）。对外是**同一个技能、同一套命令**，内部按开关/探针分派。 |
| **端到端流程** | `传入 .e` → 读 `capabilities.json`（本机是否验证过 DBWIN）→ 分支①/② → 统一产出 `run.log` + 编译输出 → AI 读同一格式结果。 |
| **适用范围** | ✅ **通用首选**：既吃「不改源码 + 格式真」的红利，又在 DBWIN 不可用时**自动兜底**；✅ 跨机器/跨易语言版本差异大时最稳。❌ 想「最小实现」时过重。 |
| **优点** | 鲁棒性最好；对用户是**单一入口**；两条路线互为备份；探针结果可缓存复用。 |
| **缺点/限制** | ⚠️ 工程量最大（两套机制都要维护）；⚠️ 两条路线的日志格式需**归一化**才能「同一格式结果」（否则 AI 读到的形态不一致）。 |
| **风险与验证** | 中。先做 P0 探针，探针通过与失败**都**有落地路线 → 整体风险被摊薄。 |
| **工作量粗估** | **高**（= ① + ② + 分派/归一化层） |

### C.2 组织方式详表（打包/发布维度）

#### 组织方式 A：新增独立技能

- **机制**：在 `Releases/Elang-AiTools/skills/` 下新建一个自包含技能目录（建议名 `elang-debug`），SKILL.md 讲「传 `.e` → 编译调试版 → 跑 → 读日志」的完整闭环。
- **命名建议**：`elang-debug`（匹配 `^[a-z0-9]+(-[a-z0-9]+)*$`，与目录名一致；语义聚焦「运行+调试」，不与 `elang-ai-coding` 的「写代码」重叠）。备选：`elang-run-debug`、`elang-debugger`。
- **frontmatter 草案**（须 ≤1024 字符，含触发词，**不含本机信息**）：
  ```yaml
  ---
  name: elang-debug
  description: 在无需打开易语言 IDE 的情况下，传入 .e 文件路径直接编译并运行易语言程序，并把运行日志（输出调试文本 / 调试输出）实时回读给 AI，效果对齐易语言 IDE 的调试输出窗口。用于验证生成的易语言代码能否编译、运行、按预期输出，形成「写→编译→运行→读日志→修」的自闭环。触发词：易语言调试、运行易语言、无 IDE 运行、调试输出、输出调试文本、看运行日志、验证易语言程序、ecl 命令行编译、调试版本、DBWIN、日志回读。
  ---
  ```
- **技能内部文件结构草案**（满足自包含 + 不写本机信息）：
  ```
  elang-debug/
  ├── SKILL.md                          # 唯一入口：闭环用法、参数、判读、坑
  ├── scripts/
  │   ├── run_debug.py                  # 主运行器：传 .e → 选路线(①/②/④a) → 跑 → 收日志（纯标准库）
  │   ├── capture_dbwin.py             # 路线① DBWIN 监听封装（找 <ASSETS>/dbwinwatch.exe）
  │   ├── instrument_debug.py          # 路线② 插桩器（e2t → 重写 调试输出 → t2e）
  │   └── host_addin.py                # 路线④a 宿主化：部署/清理 .fne、驱动 e.exe、收日志
  ├── assets/
  │   ├── dbwinwatch.exe               # 路线① DBWIN 监听器（32 位 C，与 导出支持库文档.exe 同风格，零依赖）
  │   └── elang_addin.fne              # 路线④a 自研 AddIn（32 位；放进易语言 lib\ 被 e.exe 加载）
  └── references/
      └── 无IDE调试说明.md              # 技术路线、假设、坑、对齐范围（明细）
  ```
  > `assets/` 只放**技能自己用到**的二进制；`ecl.exe`（仅路线①需要）**不放进技能包**（第三方二进制、体积与许可问题）——正文只写「怎么找」（`ECL` 环境变量 → 同目录 → `PATH`），符合「不写本机信息 / 只写怎么找」纪律。`elang_addin.fne` 是**我们自研**的 AddIn，属技能自包含资产。
  > ⚠️ **路线④a 的运行期副作用（须向用户披露）**：使用时会**临时往易语言安装目录 `lib\` 投放 `elang_addin.fne`**，用完移除并 `diff` 自证；若目标机器禁止写 `lib\`，退化到 `re\e_sandbox\` 只读整目录副本方案（成本更高）。
- **优点**：职责单一、正文短、**触发范围精准**（不污染 `elang-ai-coding` 的触发词）；跨工具通用性最好；可独立打包上架。
- **缺点/限制**：需新增并维护一整套发布同步（脚本/exe/参考）。
- **改动范围**：见 C.3 表第 1 段（组织方式 A 的公共改动）。

#### 组织方式 B：在现有 `elang-ai-coding` 上扩展

- **机制**：把「运行调试」能力作为 `elang-ai-coding` 的新章节 + 新脚本塞进去，不新增技能。
- **优点**：改动集中；用户只装一个技能；与「写代码」链路天然同目录，互引方便。
- **缺点/限制（重点回答 team-lead 的关注）**：
  - ⚠️ **可维护性**：`SKILL.md` 已 ≈640 行，再塞「编译/运行/日志/探针/插桩/超时/窗口策略」大段，**逼近单文件维护极限**，章节定位变慢。
  - ⚠️ **上下文占用**：技能被调起时**整份 SKILL.md 进上下文**（或按需分段读）；正文越长，写代码任务里也白带一大段运行调试内容，**挤占写代码的注意力与 token**。
  - ⚠️ **触发范围变宽 → 误触发**：`description` 要加「运行日志/调试输出/DBWIN/ecl…」触发词，会让**只要提到「运行/日志」就命中本技能**，与「写易语言代码」场景**抢触发**，且在非易语言任务里也可能被拉高匹配分而误触发。
  - ⚠️ **自包含负担**：新增 `assets/dbwinwatch.exe`、插桩器脚本都要塞进同一技能包，包体与校验项同步膨胀。
- **改动范围**：见 C.3 表第 2 段（组织方式 B 的公共改动）——**改动面最集中，但对既有技能是侵入式**。

#### 组织方式 C：混合（新增独立技能 + 复用既有能力）

- **机制**：新增 `elang-debug` 技能，但**明确复用** `elang-ai-coding` 的既有能力，**不复制**：
  - 复用 `echeck.py`（编译前静态语义预检，挡掉一部分进编译才报的错）；
  - 复用 `rtcheck.py`（插桩路线的格式收敛自检）；
  - 复用 `mkproj.py`（工程形态）；
  - 复用 `references/易语言文本格式规范.md`（往返格式铁律）；
  - 复用 `支持库文档/commands.jsonl`（命令存在性校验）。
- **复用边界与依赖关系（必须写清，避免「跨技能引用」踩坑）**：
  - `pack-skill.py` 的校验器**允许**跨技能引用（会打 warn「引用了同级技能 …（跨技能引用，合法）」），**但要求被引用文件在**同级技能目录里真实存在。
  - ⇒ 因此 `elang-debug` 正文引用 `elang-ai-coding/scripts/echeck.py` 是**合法的**，但**前提是两个技能都已安装**。若对方只装了 `elang-debug`，则预检能力降级。
  - **纪律建议**：`elang-debug` **自带**运行调试专属脚本（`run_debug.py`/`capture_dbwin.py`/`instrument_debug.py`/`dbwinwatch.exe`）；**格式与语义预检**这类通用能力**以「若同级存在 `elang-ai-coding` 则复用，否则跳过并提示」**的方式弱依赖，**不把它的文件复制进本技能**（避免两份副本漂移）。正文写清依赖与降级行为即可。
- **优点**：职责仍单一（技能独立）；触发范围精准；**不重复造轮子**，与既有闭环衔接最顺；跨工具通用性好。
- **缺点/限制**：需在正文写清「跨技能依赖 + 降级行为」；两个技能需**成对发布**（发新技能时应同时带上 `elang-ai-coding`）。
- **改动范围**：见 C.3 表第 3 段（组织方式 C 的公共改动）——**= A 的改动 + 少量跨技能引用说明/校验**。

### C.3 组合矩阵与「改动范围」精确清单

> `sync-release.sh` / `verify-release.py` / `install-skills.py` / `pack-skill.py` 的**实测现状**（已逐文件确认）：
> - `install-skills.py`：**自动发现** `skills/` 下含 `SKILL.md` 的目录 → **新增技能无需改动本文件**（仅当新增**安装目标工具**时才改 `GLOBAL_TARGETS`）。
> - `pack-skill.py`：**自动发现**所有技能目录并**通用校验** → **新增技能无需改动**（除非新技能需新增 `SKIP_NAMES`）。
> - `verify-release.py`：head(2) 自动遍历所有技能；但 **`SELF_CONTAINED` 列表硬编码** `elang-ai-coding/assets/导出支持库文档.exe`、`references/易语言文本格式规范.md`；**head(6) `DOCS` 列表硬编码** 4 个 SKILL.md 路径 → **新技能若要「自包含校验」，须往 `SELF_CONTAINED` 加条目；新技能 SKILL.md 若要「失效路径」检查，须往 `DOCS` 加一条**。
> - `sync-release.sh`：**是硬编码名单** —— `SKILL_SCRIPTS="mkproj.py rtcheck.py efix.py echeck.py"`，且 `cp docs/*.md → elang-ai-coding/references/`**只服务 elang-ai-coding** → **新技能需扩展脚本把新脚本/新参考/新 exe 同步进新技能目录**。

**组合定评表**（行 = 组合，列 = 关键维度）：

| # | 组织 × 路线 | 可行性 | 外部依赖 | 日志还原度 | 改动量 | 可维护性 | 跨工具通用性 | 综合推荐 |
|---|---|---|---|---|---|---|---|---|
| A① | 独立技能 + 编译调试版+DBWIN | 中(待探针) | 高(ecl) | 高 | 中 | 高 | 高 | ⭐⭐☆ |
| A② | 独立技能 + 源码插桩 | 高 | 中(e2txt+编译器) | 中(格式需复刻) | 中高 | 高 | 高 | ⭐⭐⭐ |
| A③ | 独立技能 + 并存 | 高 | 高 | 高 | 高 | 高 | 高 | ⭐⭐⭐⭐ |
| B① | 扩展现有技能 + 编译调试版+DBWIN | 中 | 高 | 高 | 中 | **低** | 中 | ⭐☆ |
| B② | 扩展现有技能 + 源码插桩 | 高 | 中 | 中 | 中 | **低** | 中 | ⭐⭐ |
| B③ | 扩展现有技能 + 并存 | 高 | 高 | 高 | 高 | **低** | 中 | ⭐⭐ |
| C① | 混合 + 编译调试版+DBWIN | 中 | 高 | 高 | 中 | 高 | 高 | ⭐⭐⭐ |
| C② | 混合 + 源码插桩 | 高 | 中 | 中 | 中高 | 高 | 高 | ⭐⭐⭐ |
| C③ | 混合 + 并存 | 高 | 高 | 高 | 高 | 高 | 高 | ⭐⭐⭐⭐ |

**v2 新增：路线④的组合（来源：工程师实证报告）**

| # | 组织 × 路线 | 可行性 | 外部依赖 | 日志还原度 | 改动量 | 可维护性 | 跨工具通用性 | 综合推荐 |
|---|---|---|---|---|---|---|---|---|
| **A④a** | 独立技能 + 官方宿主化 | **高（证据充分）** | **低（无 ecl；仅需写 `lib\`）** | 高 | 中高 | 高 | 高 | **⭐⭐⭐⭐（并列首选）** |
| C④a | 混合 + 官方宿主化 | 高（证据充分） | 低 | 高 | 中高 | 高 | 高 | ⭐⭐⭐⭐ |
| A④b | 独立技能 + 外部 DLL 注入 | 中（脆弱） | 低 | 中 | 高 | 中 | 高 | ⭐⭐（极端兜底） |
| B④a | 扩展现有技能 + 官方宿主化 | 高 | 低 | 高 | 中高 | **低** | 中 | ⭐⭐（不推荐，污染既有技能） |

> **推荐（v2）**：路线④a 因**不需要 `ecl.exe`** 且**可覆盖单步/断点**两个新事实，**从「P0 失败时的兜底」升格为并列首选**（与路线①并列），其代价是「往易语言安装目录 `lib\` 投放 `.fne`」这一**需用户拍板**的部署足迹。④b 维持「极端兜底」定位。

**逐组合的「改动范围」精确清单**（新技能统一以 `elang-debug` 为例；路线决定新增脚本/exe，组织方式决定发布链路改动）：

<details>
<summary><b>组合 A（新增独立技能）× 路线①②③ 的共用改动（展开）</b></summary>

**新增文件/目录**
- `Releases/Elang-AiTools/skills/elang-debug/SKILL.md`
- `Releases/Elang-AiTools/skills/elang-debug/scripts/{run_debug.py, capture_dbwin.py, instrument_debug.py, host_addin.py}`（按路线取用，见下）
- `Releases/Elang-AiTools/skills/elang-debug/assets/dbwinwatch.exe`（**路线①/③**）
- `Releases/Elang-AiTools/skills/elang-debug/assets/elang_addin.fne`（**路线④a**，自研 AddIn，32 位）
- `Releases/Elang-AiTools/skills/elang-debug/references/无IDE调试说明.md`
- 工程侧开发副本：`tools/{run_debug.py, capture_dbwin.py, instrument_debug.py, host_addin.py}`、`src/dbwinwatch.c` + `src/build-dbwin.sh`、`src/elang_addin.c` + `src/build-addin.sh`、`docs/无IDE调试说明.md`

**修改现有文件**
- `tools/sync-release.sh`：**必改** —— 新增一段把上面新脚本按显式名单拷进 `skills/elang-debug/scripts/`，并把 `docs/无IDE调试说明.md` 拷进 `skills/elang-debug/references/`（现有 `cp docs/*.md → elang-ai-coding/references/` 不覆盖新技能）；如新 exe 由 `src/build-*.sh` 产出，同步段照 `build.sh` 对 `assets/` 的做法加一段。
- `tools/verify-release.py`：**必改** ——
  - `SELF_CONTAINED` 列表加 `("elang-debug/assets/dbwinwatch.exe", ROOT/"dbwinwatch.exe")`（路线①/③）与 `("elang-debug/references/无IDE调试说明.md", ROOT/"docs"/"无IDE调试说明.md")`；
  - head(6) `DOCS` 列表加 `"Releases/Elang-AiTools/skills/elang-debug/SKILL.md"`。
- `Releases/Elang-AiTools/使用说明.md`：技能表格加一行「`elang-debug`」+ 目录树补 `elang-debug/` 节点。
- `README.md`：三件套/技能表格加 `elang-debug`；`目录` 树补节点。
- （可选）`tools/pack-skill.py`：若新技能内部有不该进包的目录，往 `SKIP_NAMES` 加。

**无需改动（已确认自动适配）**
- `install-skills.py`：`scan_skills()` 自动发现新技能并安装到全部目标（共享根 + 已探测工具目录）。
- `tools/pack-skill.py`：自动发现并通用校验新技能，产出 `dist/elang-debug.zip`。

**新增技能自包含需带的 assets**：`assets/dbwinwatch.exe`（路线①/③）；`assets/elang_addin.fne`（路线④a）。若走路线②则这两个二进制都不需要，插桩器为纯 Python 脚本放 `scripts/`。
</details>

<details>
<summary><b>组合 B（扩展现有 <code>elang-ai-coding</code>）× 路线①②③ 的改动（展开）</b></summary>

**修改现有文件**
- `Releases/Elang-AiTools/skills/elang-ai-coding/SKILL.md`：**正文 + frontmatter `description` 都要改** —— 加「运行/调试/日志」章节；`description` 加触发词（**这直接导致触发范围变宽、误触发风险**）。
- `Releases/Elang-AiTools/skills/elang-ai-coding/scripts/`：放入 `run_debug.py`/`capture_dbwin.py`/`instrument_debug.py`。
- `Releases/Elang-AiTools/skills/elang-ai-coding/assets/`：放 `dbwinwatch.exe`（路线①/③）——**注意这会改变既有技能包的内容，需同步 `verify-release.py` 的 `SELF_CONTAINED` 与安装点 md5**。
- `tools/sync-release.sh`：把新脚本加进 `SKILL_SCRIPTS` 名单（如 `"mkproj.py rtcheck.py efix.py echeck.py run_debug.py capture_dbwin.py instrument_debug.py"`）；`docs/*.md` 已自动拷进该技能 `references/`，新参考文档放 `docs/` 即可。
- `tools/verify-release.py`：`SELF_CONTAINED` 加 `dbwinwatch.exe` 条目（若带 exe）。
- `Releases/Elang-AiTools/使用说明.md`、`README.md`：补技能「自带资源」表格（**不新增技能行**，但技能描述要更新）。
- `src/build.sh`（或新增 `src/build-dbwin.sh`）：把 `dbwinwatch.exe` 编译并同步进该技能 `assets/`。

**无需改动**：`install-skills.py`、`pack-skill.py`（技能数量不变，自动适配）。
</details>

<details>
<summary><b>组合 C（混合：独立技能 + 复用既有）× 路线①②③ 的改动（展开）</b></summary>

**= 组合 A 的全部改动**，外加：
- `elang-debug/SKILL.md` 正文写明**跨技能依赖**：默认复用 `elang-ai-coding/scripts/{echeck.py, rtcheck.py, mkproj.py}` 与其 `references/易语言文本格式规范.md`；写明「若同级 `elang-ai-coding` 不存在则降级（跳过预检并提示）」。
- `pack-skill.py` 打包 `elang-debug` 时**会打 warn**「引用了同级技能 elang-ai-coding 的 scripts/xxx（跨技能引用，合法）」——**这是预期行为，不是错误**；在 CI/文档里说明即可，**无需改代码**。
- 发布纪律：**发 `elang-debug.zip` 时同时发 `elang-ai-coding.zip`**（成对发布），并在 `使用说明.md` 注明二者配套关系。
</details>

**各路线新增脚本/exe 汇总**

| 路线 | 新增脚本 | 新增 exe/DLL | 备注 |
|---|---|---|---|
| ① | `run_debug.py`、`capture_dbwin.py` | `dbwinwatch.exe`（~100 行 32 位 C） | 依赖 `ecl.exe`（外部，不进包） |
| ② | `run_debug.py`、`instrument_debug.py` | 无 | 插桩器为纯 Python |
| ③ | ①+② 全部 | `dbwinwatch.exe` | 额外一个分派/归一化层（可并入 `run_debug.py`） |
| ④a | `run_debug.py`、`host_addin.py`（部署/清理 AddIn、驱动 e.exe） | `elang_addin.fne`（32 位 C/C++ 写的 AddIn） | **不需要 `ecl.exe`**；需写 `lib\`；**可覆盖单步/断点** |
| ④b | `inject.py` | `injector.dll`（32 位 C） | 极端兜底；e.exe 无导出表，全靠逆向 |

### C.4 路线④：宿主化 e.exe（新增，来源：工程师实证报告）

> 缘起：用户提出第二条思路——「写一个 DLL，需要时通过命令行启动并注入易语言主程序；默认不显示易语言窗口，但注入的 DLL 能控制易语言各项功能，最需要的是**调试运行**和**监控调试框**。」
> 工程师寇豆码据此分析了 `D:\ides\e\e.exe` 主程序结构与官方扩展接口，结论在 `docs/分析-易语言主程序与官方扩展接口.md`，证据与脚本在 `re/`。路线④据此分 **④a 官方宿主化** 与 **④b 外部 DLL 注入**。

#### 路线④a：官方宿主化（写 `.fne`/AddIn，被 e.exe 加载）

| 维度 | 内容 |
|---|---|
| **思路（机制）** | 写一个 32 位 DLL（后缀 `.fne`，导出 `GetNewInf` 返回 `LIB_INFO`；**跨 DLL 函数指针一律 `__stdcall`**），放进易语言 `lib\`。**进入 IDE 进程的路径（v3 实证修正）**：e.exe **并非**启动即全量加载新 `.fne`，而是「**被打开的 `.e` 声明了本库**（库表 `配置/支持库.config.json` 里登记了本库 Key）时**按需**加载 `lib\` 里的它」——任务 D 已跑通。库收到 `NL_SYS_NOTIFY_FUNCTION` 时保存系统下发的 `PFN_NOTIFY_SYS` → 之后用 `NotifySys(NES_GET_MAIN_HWND)` 取主窗口、`NotifySys(NES_RUN_FUNC, FN_*)` **反过来驱动 IDE**：`FN_OPEN_FILE2(路径)` 打开 `.e` → `FN_COMPILE_AND_RUN` 编译执行 → 监控输出。置 `LBS_IDE_PLUGIN(1<<8)` 接收 `NL_IDE_READY`。 |
| **端到端流程** | `传入 .e` → 生成一个**派生副本**（仅在库表 `配置/支持库.config.json` 里追加本库一项，**不改原 .e**），并把 `elang_addin.fne` 投放进 `lib\` → 启动 `e.exe` 打开该派生 `.e`（默认主窗口可见；拿到 `ENewFrame` 后 `ShowWindow(SW_HIDE)` 隐藏）→ 我们的 `.fne` **被按需加载**、收到 `NL_IDE_READY` → `FN_COMPILE_AND_RUN` 编译执行 → 被调试程序起/停 → 日志出口（`FN_ADD_TAB` 自建日志夹 → **同时写文件/命名管道**）→ AI 读日志 → 结束并清理（移除 `lib\` 里的 `.fne` + `diff` 自证）。 |
| **适用范围** | ✅ 要官方编译执行、要**单步/断点**、要最稳的日志出口；✅ 能接受「往 `lib\` 投放 `.fne` + 打开加了库声明的派生 `.e`」。❌ 禁止写易语言安装目录时不用（退 ④b，或用 `re\e_sandbox\` 只读副本）。 |
| **优点** | **不需要第三方 `ecl.exe`**（`FN_COMPILE_AND_RUN` 内部就是 IDE 自己的编译执行）→ 绕开第三方依赖；**官方接口**，跨版本相对稳；**首次可覆盖交互式调试（单步/断点/变量监视）**（功能号齐全）；`FN_ADD_TAB` 给 AI 一个**官方支持**的日志工作夹；**无需全局登记**即可按需加载（任务 D **已实证**）；走官方加载路径，**不触发「可疑注入」面**。 |
| **缺点/限制** | ⚠️ **部署足迹（两处，均须向用户披露）**：① 往易语言安装目录 `lib\` 投放 `.fne`（运行后移除并 `diff` 自证）；② 需一个**库表登记了本库的派生 `.e`**（改 `配置/支持库.config.json` 追加库项、**不动原文件**）。⚠️ **调用约定陷阱**：SDK 头把函数指针写成 `__cdecl` 是误导，实际必须 `__stdcall`（否则 e.exe 崩）。⚠️ **仍会启动 `e.exe` 进程**（当前链路默认主窗口可见）⇒ 口径是「**后台无窗口地跑易语言**」，**不是「完全不启动易语言」**（与路线①中 `ecl.exe` 也需 e.exe 参与的口径一致）。✅ ~~输出面板确切 HWND/类名尚未坐实~~（**v6 已坐实**：主面板 `class='ScintillaForEIDETools'`（Scintilla 系），另有一个 `class='Edit'` 旧面板并存——两种消息约定（`WM_GETTEXT` / `SCI_GETTEXT` 2182）与两类窗口都要试）。⚠️ `e.exe` **未见「无窗口」命令行开关（推断）**，隐藏时机仍需 **PoC-3** 验证（`.fne` 侧已实现 `ELANG_AI_HIDE` 隐藏逻辑，链路尚未默认启用）。✅ ~~加载 `.fne` 是否需事前签名/白名单~~：**已实证无需**——PoC-1/D 中我们的 `.fne` 被正常加载、`GetNewInf` 被正常调用。 |
| **风险与验证** | ~~中~~ → **低（v6，主路径已全部实证）**。**PoC-1 已完成**（AddIn 骨架打通 + 定位并修复 `__stdcall` 根因：`NES_GET_MAIN_HWND`、`FN_IS_FUNC_ENABLED` 全表 `handled=1`、`FN_COMPILE_AND_RUN` `enabled=1`）；**任务 D 已完成**（免登记按需加载跑通）；**PoC-2 已完成（v6 回填，判据 A/B/C 全过，见 D.4）**——`FN_COMPILE_AND_RUN` 真实触发调试运行、业务文本拿到、面板增量流式落盘、`FN_END_RUN` 运行态翻转 + 主动中断生效（含负向检查）、运行期异常已捕获（第三方插件链路连带崩溃，列为已知风险）。**待做**：仅 **PoC-3**（无窗口启动；不阻塞主功能，见 D.4）。详见 D.4 与 `re/addin/POC1D-免登记成功与__stdcall根因.md`。 |
| **工作量粗估** | **中-高**（一个最小 `.fne` AddIn + 输出监控 + 进程生命周期/窗口隐藏 + 部署/清理） |

**④a 监控「调试框」的 4 条落地手段（采信报告 §2.2-Q5；v6 标注实际采用情况）**：
- **a) 子类化输出面板窗口**：可（e.exe 已导入 `SetWindowLongA`/`CallWindowProcA`/`SetWindowsHookExA`），面板为 **Scintilla 系**（`ScintillaForEIDETools` 由第三方插件 `lib\iDraw\superTools\plugin\eOutPutControl.dll` 注册、启动即被加载），读文本要走 **SCI 消息**而非 `WM_GETTEXT`；~~面板确切 HWND/类名尚未坐实~~ **v6 已坐实**（见 C.4 表）。
- **b) `FN_ADD_TAB` 在输出工具条挂自建工作夹**：**官方支持**（`ADD_TAB_INF{HWND,HICON,标题,提示}`，`PublicIDEFunctions.h:19-27,421`），最合适的「AI 可读日志出口」——日志由我们完全掌控，可同时落文件/命名管道。**v6 注：未启用**（直读面板已满足需求，留作备选）。
- **c) 拦截**：hook `OutputDebugStringA`（在调试子进程里），或**直接用本方案已有的 DBWIN 监听器**。**v6 注：未采用**（无必要）。
- **d) 直读面板**：同 a 的限制。**✅ v6 实际采用的就是这条**：轮询面板文本长度、**增量取差分**流式落盘，同时给 GBK 原始字节 + UTF-8 转码视图。
- ~~**明确未解项**：输出面板确切 HWND/类名需靠「触发一次运行前后的 `EnumChildWindows` 差集」定位（PoC-2）~~ → **v6 已解决**：PoC-1 的 208 子窗口清单已含 `ScintillaForEIDETools`，PoC-2 实跑确认其为主日志面板（`Edit` 旧面板并存，双通道兜底）。

#### 路线④b：外部 DLL 注入（`CreateRemoteThread`/`SetWindowsHookEx`/AppInit）

| 维度 | 内容 |
|---|---|
| **思路（机制）** | 用 `CreateRemoteThread` / `SetWindowsHookEx` / AppInit 等手段把外部 DLL **注入** `e.exe`，企图在进程内控制 IDE。 |
| **适用范围** | **仅**「目标机器**禁止写易语言安装目录 `lib\`**」这一极端场景（推断）。除此之外不具优势。 |
| **优点** | 不改 `lib\`；`e.exe` **无 ASLR、无 DEP、未导入 `CreateRemoteThread`/`IsDebuggerPresent`** ⇒ 注入本身技术门槛低。 |
| **缺点/限制** | **e.exe 无导出表** ⇒ 注入后**无官方函数可调**，只能逆向定位内部函数（**跨版本易碎**）；**相对 ④a 几乎没有不可替代价值**（④a 的官方功能号已覆盖 ④b 能做的一切，且稳定）；更容易触发安全软件。 |
| **风险与验证** | **高**（脆弱、无稳定性承诺、逆向成本高）。 |
| **工作量粗估** | **高**（逆向 + 注入 + 全靠 RVA/特征码，无官方接口） |

> **④b 判定（此处已按实证修正，采信报告 §5.2 + PoC-1/D）**：**有条件可，但相对 ④a 仍不具优势**——④a 已实证可跑通（官方接口 + **免登记按需加载**），且不触发「可疑注入」面；④b 得不到任何官方能力（e.exe 无导出表，全靠逆向）。~~原以为 ④a 的足迹只是「丢一个文件进 `lib\`」~~，**v3 修正**：④a 的足迹是「写 `lib\` **+** 打开一个声明了本库的派生 `.e`」两处，且可用「只读沙箱整目录副本」把源目录零写入。故 ④b 唯一可能的价值仍是「**禁止写 `lib\` 且禁用沙箱副本**」的极端场景。**优先实现 ④a；④b 仅作极端兜底并标注高脆弱性。**

---

## D. 决策辅助

### D.1 方案对比矩阵表（高/中/低 + ★，v2 已并入路线④）

| 方案 | 可行性 | 外部依赖 | 日志还原度 | 交互调试 | 改动量 | 可维护性 | 跨工具通用性 | 结论 |
|---|---|---|---|---|---|---|---|---|
| **A④a 独立技能+官方宿主化** | **高(证据充分)** | **低(无 ecl；仅需写 `lib\`)** | 高 | **可** | 中高 | 高 | 高 | **并列首选** |
| **A① 独立技能+编译调试版+DBWIN** | 中(待探针) | 高(ecl) | 高 | 否 | 中 | 高 | 高 | **并列首选（若 P0 探针通过）** |
| A③ 独立技能+并存 | 高 | 高 | 高 | 视所选路线 | 高 | 高 | 高 | 组合首选（覆盖面最全） |
| C④a | 高(证据充分) | 低 | 高 | 可 | 中高 | 高 | 高 | 次选（省重复，需成对发布） |
| C③ | 高 | 高 | 高 | 视 | 高 | 高 | 高 | 次选（同 C④a） |
| A② 独立技能+插桩 | 高 | 中 | 中 | 否 | 中高 | 高 | 高 | **兜底（机制确定可行）** |
| A④b 独立技能+外部注入 | 中(脆弱) | 低 | 中 | 可 | 高 | 中 | 高 | 极端兜底（仅禁写 `lib\` 时） |
| B④a 扩展+官方宿主化 | 高 | 低 | 高 | 可 | 中高 | **低** | 中 | 不推荐（污染既有技能） |
| B② 扩展+插桩 | 高 | 中 | 中 | 否 | 中 | **低** | 中 | 不推荐 |
| B①/B③ | 中-高 | 高 | 高 | 否/视 | 中/高 | **低** | 中 | 不推荐 |
| C①/C② | 中/高 | 高/中 | 高/中 | 否 | 中/中高 | 高 | 高 | 可选 |

> **v2 关键变化**：新增「**交互调试**」列——这是路线④a 独占的能力（官方单步/断点功能号），也是它从「兜底」升格为「并列首选」的核心理由之一；另一理由是它**不需要 `ecl.exe`**。

### D.2 推荐方案（v2 已重排优先级）

**组织方式（结论不变）：首选 A（新增独立技能 `elang-debug`），备选 C（混合）。**
理由：避免让已 640 行的 `elang-ai-coding` 继续膨胀，保住触发精准度与上下文经济性；B（扩展现有技能）不推荐（误触发 + 上下文占用）。

**技术路线（v2 重排）：并列首选 ④a 与 ①。**

- **④a（官方宿主化）→ 并列首选（不再只是「兜底」）。** 依据工程师实证：**AddIn 骨架已由 PoC-1 打通，免登记按需加载已由任务 D 跑通**（e.exe 打开「声明了本库的 `.e`」即**按需**从 `lib\` 加载我们的 `.fne`；`FN_IS_FUNC_ENABLED` 全表 handled=1、`FN_COMPILE_AND_RUN` enabled=1），官方功能号覆盖「打开工程→编译执行→结束」，**不需要第三方 `ecl.exe`**，且**可覆盖单步/断点**。
  **关键理由（为什么升格为并列首选而非兜底）**：即便 P0 探针失败，④a 也**不依赖 DBWIN 假设**——它可在 IDE 进程内用 `FN_ADD_TAB` 自建日志夹（或直接读输出面板）拿到日志，因此是**独立成立的一条主线**。
  代价：**往易语言安装目录 `lib\` 投放 `.fne`**（需用户拍板），且**仍会启动 e.exe 进程**（默认主窗口可见，需运行后隐藏）。
- **①（命令行编译调试版 + DBWIN）→ 并列首选（若 P0 探针通过）。** 依赖 `ecl.exe`；不启动可见 IDE 窗口的诉求更彻底（但仍需 e.exe 参与编译）。
- **②（源码插桩）→ 确定可行的兜底**（探针失败、且用户不采用 ④a、或不接受写 `lib\` 时）。
- **④b（外部注入）→ 极端兜底**（仅「禁止写 `lib\`」时，且明确标注高脆弱性）。

**组合首选（推荐）：**
1. **A④a**（独立技能 + 官方宿主化）——**证据最硬、无第三方依赖、可交互调试**。
2. **A①**（独立技能 + 命令行调试版 + DBWIN）——**并列首选**，若 P0 探针通过。
3. **A③ 或（含④a的）并存**——若要「两条路线互为兜底 + 对用户单一入口」，覆盖面最全，工程量最大。

**备选：A②**（独立技能 + 源码插桩）——机制确定可行，代价是插桩与格式复刻。

> **口径澄清（重要）**：④a 与「完全不启动易语言」不是一回事——④a **仍会启动 `e.exe` 进程**（默认主窗口可见，需运行后 `ShowWindow(SW_HIDE)` 隐藏），即「**后台无窗口地跑易语言**」；这与路线①（`ecl.exe` 也需 e.exe 参与）**口径一致**。若用户要求「一根易语言进程都不起」，则本需求的①/④a 都需重新讨论（②源码插桩 + 独立编译器才勉强接近）。

### D.3 待用户拍板的问题清单（v2：新增第 7/8 条，修订第 2 条）

| # | 问题 | 选项 | 影响 |
|---|---|---|---|
| 1 | **是否接受引入第三方 `ecl.exe` 命令行编译器依赖？**（路线①的前提；**路线④a 不需要它**） | (a) 接受，随技能收录/引导用户下载；(b) 接受但**不进技能包**，只写「怎么找」；(c) 不接受 → 改走 ④a 或源码插桩② | 决定路线①是否可选 |
| 2 | **（修订）是否把「交互式调试（单步/断点/变量监视）」纳入本期？** v2 因 ④a 出现而**技术可行**（官方 `FN_STEP_INTO`/`FN_STEP`/`FN_STEP_OUT`/`FN_SET_BREAK_POINTER`/`FN_VIEW_VAR`…），成本需重估 | (a) 本期不做，只做「运行 + 日志」（**建议**，控范围）；(b) 纳入本期（④a 路线下追加工作量） | 决定本期范围与工作量；原「明确排除」表述**已按实证放宽为可选项** |
| 3 | **GUI 程序的运行窗口是否允许真实弹出？** | (a) 允许（简单）；(b) 必须隐藏/最小化（④a 需拿到 `ENewFrame` 后 `ShowWindow(SW_HIDE)`，复杂）；(c) 仅支持无窗口（控制台）程序 | 影响无头运行实现与用户体验 |
| 4 | **运行期错误（越界/除零）是否要求覆盖？** | (a) 不覆盖，只保证正常路径日志（**建议**）；(b) 需捕获错误信息（无头下模态框难处理，成本高） | 「效果一致」的边界 |
| 5 | **日志还原度**：是否要求逐字复刻 IDE 输出窗口（含 `*` 前缀、多参数/数组/日期显示形态）？ | (a) 要求逐字一致（插桩路线成本陡增）；(b) 只要「能看到日志内容、可判读」即可（**建议**） | 直接决定路线②的对齐成本 |
| 6 | **技能组织**：独立技能 / 扩展现有 / 混合？ | (a) 独立 `elang-debug`（**推荐**）；(b) 扩展 `elang-ai-coding`；(c) 混合 | 决定打包/发布/触发范围 |
| 7 | **（v2 新增）是否接受往易语言安装目录 `lib\` 投放 `.fne`？**（路线④a 的核心前提） | (a) **接受**（用完移除并 `diff` 自证）；(b) 不接受，改用 `re\e_sandbox\` **只读整目录副本**方案（零写入源目录，成本更高）；(c) 不接受 → **放弃 ④a**，走 ①/② | 决定 ④a 是否可用；替代是 ④b 或 ② |
| 8 | **（v2 新增）「无需打开易语言」的口径确认**：④a/① 都会**启动 `e.exe` 进程**（后台无窗口跑），是否接受？ | (a) 接受「**后台无窗口地跑易语言**」（**推荐**，与路线①口径一致）；(b) 要求「一根易语言进程都不起」（则需求需重新讨论，源码插桩②才勉强接近） | 需求口径对齐，避免验收争议 |

### D.4 分阶段落地建议（v2：并入工程师 PoC-1/2/3）

> **依赖关系一览**：**P0 探针** 与 **PoC-1/2/3** 可**并行推进**；其中 **PoC-2(c) 顺带收口 P0 假设**（同一实验里同时挂 DBWIN 监听器，省一半成本）。阶段一的判定结果决定阶段二走 **①** 还是 **④a / ②**。

#### 阶段一：可行性收敛（P0 探针 + PoC-1/2/3，**最先做**）

> 全部实验代码只放 `re/`，不碰项目正式代码；对 `D:\ides\e\` 默认**只读**（PoC-1 唯一「写」是往 `lib\` 投放 `.fne`，**投放前备份清单、实验后移除并 `diff` 自证**）。

- **P0 探针（既有，保留；建议与 PoC-2 合并执行）— ⛔ v6 关闭（不再执行）**
  - **v6 结论**：主路线已定为 **④a** 且已发布，④a **不依赖 DBWIN 假设**（日志出口 = 直读面板取差分）；本探针只为路线①服务，而路线①未被采纳。**该假设仍开放**（krnln 导入 `OutputDebugStringA` 的二进制证据仍在），仅当未来启动路线①时再执行。以下保留原文备查。
  - 目标：证伪/证实「调试版 exe 在无 IDE 进程时是否仍向 DBWIN 输出日志」。
  - 做法：① 取 `demos/02-嵌套控制流压测`（或 `01`，已有 `输出调试文本` 调用）；② 备 `ecl.exe`，`ecl.exe make <源.e> -r` 生成调试版 exe（确认不弹 IDE 窗口）；③ **先启动**最小 DBWIN 监听器（~100 行 32 位 C，或先用现成 DebugView），再运行该 exe；④ 观察是否收到，再**关闭所有易语言进程**重复一次。
  - 成功判据：**无任何易语言 IDE 进程**时仍收到该 exe 的调试文本（内容与 IDE 输出窗口一致）。
  - 失败判据：收不到 → 路线①作废，走 **④a / ②**。
  - 与 **PoC-2(c) 是同一实验**，可合并；工作量约半天。

- **PoC-1（路线④a 打通：最小 AddIn `.fne`）— ✅ 已完成**
  - 结果：AddIn 骨架**已完整写通**（`GetNewInf` 导出合法、`LIB_INFO` 的 `offsetof` 自证通过）。**首轮卡点**是「丢进 `lib\` 不自动加载」——据此把机制重新定位为「e.exe 加载的是**已登记集合**、非实时扫描目录」；**最终根因是调用约定**：SDK 头 `#define WINAPI`（置空）使 `PFN_NOTIFY_LIB`/`PFN_NOTIFY_SYS` 被当成 `__cdecl`，而 e.exe 实为 **`__stdcall`** → 栈错位崩溃。**改为 `__stdcall` 后崩溃消失**。
  - 产物：`re/addin/POC1-结果与阻塞.md`、`re/addin/POC1D-免登记成功与__stdcall根因.md`、`re/launch_ai.py`、`re/poc1_dualrun.py`、`re/addin/src/*`。
  - **投放/清理纪律已落实**：每次实验 finally 用**精确 PID 终止**（非 `/IM`，避免误杀用户正在编辑的 IDE）+ 移除 `lib\elang_addin.fne` + `diff` 自证（`re/addin/lib_restore_diff.txt`：唯一差异是 e.exe 自写的 `lib\iDraw\*.ini`）。
  - 经验沉淀（供阶段二复用）：跨 DLL 函数指针一律 `__stdcall`；`lib2.h` 是 C++ 专用需 `g++ -fpermissive`；`CreateThread` 入口须 `__stdcall`。
  - 拒绝写 `lib\` 时：改用**全量只读沙箱**——`robocopy D:\ides\e → re\e_sandbox\`，在沙箱副本 `lib\` 放 `.fne` 并启动沙箱副本的 `e.exe`。成本更高但**零写入源目录**。

- **任务 D（`.e` 内嵌库列表 → 免登记按需加载）— ✅ 已完成**
  - 结果：e.exe 打开**声明了 `Key=elang_addin`** 的 `.e` 时，**按需**去 `lib\` 加载我们的 `.fne`、调 `GetNewInf`、下发 `NL_SYS_NOTIFY_FUNCTION`×2 + `NL_IDE_READY`，**全程无需全局登记**；`NES_GET_MAIN_HWND` 得 `ENewFrame`；**`FN_IS_FUNC_ENABLED` 全表 `handled=1`、`FN_COMPILE_AND_RUN` `enabled=1`**。
  - 对方案的含义：**④a 有了确定的落地路径**（不再卡在「全局登记」这一关）——但该路径要求「被打开的 `.e` 声明本库」，故 ④a 需**生成一个库表登记了本库的派生 `.e`**（改 `配置/支持库.config.json` 追加库项；见 C.4 / D.5）。
  - 产物：`re/opens_decl_e.py`、`re/addin/evidence/D_trace_full.txt`、`re/projD_lib.e`。

- **PoC-2（监控调试输出；承接 P0）— ✅ 已完成（v6 回填；判据 A/B/C 全部通过）**
  - 前置：**`FN_COMPILE_AND_RUN` `enabled=1`（任务 D 已证）**，直接用 `NES_RUN_FUNC(FN_COMPILE_AND_RUN)` 触发「调试运行」；**样本用 `demos/02-嵌套控制流压测`**。
  - **v6 结果（物证在 `re/addin/evidence/`，原始文件 GBK、中文 grep 用 `.utf8.txt` 视图；可复核命令见 `docs/交接-易语言AI调试技能.md` §5）**：
    - **判据 A ✅**：日志出现程序自己输出的业务文本——`grep -n "嵌套压测完成" re/addin/evidence/runA_capture.utf8.txt` 命中，且带 `[HH:MM:SS] * ` 原生前缀，证明是真从调试面板抓的。
    - **判据 B ✅**：**增量流式落盘**——`PANEL-CHANGED` 长度阶梯式增长（560→808→837→863→903→936），每级都有对应 `PANEL-DELTA`，是轮询取差分，不是收尾一次性 dump。
    - **判据 C ✅**：`FN_IS_FUNC_ENABLED(FN_END_RUN)` 运行态翻转实测（`0→1` 起跑、`1→0` 结束）；**主动中断生效且带负向检查**——`runB` 里我们发 `FN_END_RUN` 后 3.1 秒运行态回落，且 `长任务结束` 出现次数 = **0**（程序确实被打断，不是等它自然跑完）。
    - **三种收尾覆盖**：① 正常结束 ✅（`ExitCode:0` + `被调试易程序运行完毕`）；② 主动中断 ✅（同上；技能的「超时兜底」走同一 `FN_END_RUN` 通道）；③ 运行期异常 ✅ 已捕获（`PoC2_crashtrap_iControls.txt`：`0xC0000005` @ `iControls.dll+0x5092E`，栈上返回地址含 `iDraw.fne`/`Plugin_eFix.dll`/`SciLexer.dll`）⇒ **发现已知风险：第三方 IDE 插件链路会连带把 e.exe 带崩**，已写入技能的「务必告知用户」条款（每会话独立 e.exe + 可疑程序隔离运行）。
    - ⚠️ **判定口径（产品约束）**：「正常跑完」与「被中断」的**面板尾行逐字相同** ⇒ **禁止用面板尾行判结果**；技能以「运行态标志 + 是否我们发的 `FN_END_RUN` + e.exe 退出码」四态判定（`ok/stopped/timeout/crashed`）。
    - **未做（如实标注）**：并行挂 DBWIN 监听器的对照验证（判据 A 直读面板已通过，无必要）；`FN_ADD_TAB` 自建日志夹未启用（备选通道 (i) 未动用）。
  - （原文的做法/备选通道记录保留于上，供追溯。）

- **PoC-3（命令行无窗口启动，验证隐藏）— ⏳ 仍开放（v6：不阻塞主功能）**
  - 写什么：纯脚本：启动 `e.exe` → 等主窗口 → `FindWindow("ENewFrame", NULL)` → `ShowWindow(SW_HIDE)` → `IsWindowVisible` 校验 → （配合 PoC-1）调 `FN_IS_FUNC_ENABLED`。
  - 成功判据：隐藏后 `IsWindowVisible(main)==0`，且 `FN_IS_FUNC_ENABLED`/`FN_COMPILE_AND_RUN` 仍可执行。
  - 顺带测：`e.exe /?`/`-h` 是否有「无窗口」官方开关；启动到隐藏的可见时长。
  - **v6 现状**：`.fne` 侧已实现 `ELANG_AI_HIDE` 隐藏逻辑，但链路（`elaunch.py`）当前**以可见窗口运行**——即每次调试运行会短暂弹出 IDE 主窗口。**这不影响功能正确性**（面板照常抓取），只影响观感；做不做 PoC-3 只决定「是否默认无窗口」。注意：技能正文口径已按「不需要人工操作 IDE」表述，**未承诺「完全不弹窗」**。

- **阶段一产出/验收（v6 收口）**：**④a 打通 = ✅ 全部完成**（PoC-1 + 任务 D + **PoC-2**）；**P0 探针 = ⛔ 关闭**（主路线不依赖 DBWIN 假设，仅路线①需要）；**PoC-3 = ⏳ 开放但不阻塞**（当前可见窗口运行可接受）。~~结论落 `docs/无IDE调试说明.md` 草案；阶段二据此在 ①/④a/② 间选路~~ → **v6：选路已定 = ④a**（①/②/④b 均未采纳；技能为独立技能 A④a）。
  - **v6 补充：真实工程规模实测（回应 v4 时「大工程往返可能编不过」的担忧）**：对 **7 个真实工程（约 0.66 ~ 3.9 MB**：普通窗口工程、数据库管理、自绘 UI、OpenGL 向导、内嵌**双 `.ec` 模块**的框架工程）批量实测 `e2t→t2e` 往返：全部 `SUCC`、`[错误]`/`[警告]` = 0、无 `<?未知名称:支持库不存在?>` 占位；体积偏差 −0.2%~+1.1%（内嵌模块工程 −13.3%，为 `t2e` 重建模块区更紧凑，**非内容丢失**）。其中 2 个代表工程（普通 0.66 MB + 内嵌模块 1.49 MB）加壳后走完整链路（`mkcage.py → elaunch.py`）：面板报「程序代码编译成功」（42 ms / 208 ms）、真实跑出 `输出调试文本` 业务行，一个自然结束（`ok`）、一个被 `FN_END_RUN` 按时中断（`stopped`）。**结论：「大工程禁用/降级」不需要**；真正要防的是**依赖缺失**（源工程引用了本机没装的库 → 原件在本机本来就编不过，非加壳引入）。实测脚本与矩阵：`re/rt_big_test.py`、`re/rt_batch.py`（+ `re/rt_batch/matrix.json`）。此结论已写入技能 `references/加壳流程.md` §5 与 `SKILL.md` 排查表。

#### 阶段二：核心机制（按阶段一判定结果选主路线）— ✅ 已完成（v6：选路 ④a 并实现完毕）
- ~~**若 P0 通过** → 实现**路线①**~~：**未采纳**（P0 探针已关闭，见阶段一）。
- **若采用 ④a（用户接受写 `lib\`）→ 实现**路线④a**：✅ 已完成** —— `elang_addin.fne`（AddIn，32 位 mingw）+ 三个脚本：`mkcage.py`（加壳）、`elaunch.py`（投放 `.fne` → 启动 e.exe → 驱动调试运行 → 面板轮询差分流式落盘 → 精确 PID 收尾 + `lib\` 还原 diff 自证）、`readlog.py`（GBK→UTF-8 / `--business` / `--follow`）。日志出口实际采用「直读面板取差分」（`FN_ADD_TAB` 未启用，留作备选）。源码正式目录 `src/addin/`。
- ~~**若 P0 失败且不采用 ④a** → 实现**路线②**~~：**未采纳**。
- **验收标准**：~~对 `demos/` 三个样本，无需人工进 IDE 即可跑完并产出日志；日志可被人工核对为正确~~ → **达成**（demos 三个样本之外，另在 2 个真实工程上全链路实测通过，见阶段一收尾）。

#### 阶段三：技能化与发布链路 — ✅ 已完成（v6）
- **目标**：把所选机制包装成 `elang-debug` 技能，跑通全部发布链路。**达成**：`skills/elang-debug/`（SKILL.md + `scripts/{mkcage,elaunch,readlog}.py` + `references/{加壳流程,接口与调用约定清单,踩坑清单}.md` + `assets/elang_addin.fne`+`.libinfo.txt`）已发布；`sync-release.sh` 改为**按技能显式名单**（防内部文档泄漏，曾出过一次事故并已修复 + 加 references 白名单闸门）、`verify-release.py` 同步扩项。
- **验收标准**：~~`python tools/verify-release.py` 7 项全过~~ → **实测 56 项通过 / 0 项失败**；`dist/elang-debug.zip` 根为 `SKILL.md`；各安装点落盘一致；技能正文**无本机信息**（`scan-machineinfo.py` 干净）。

#### 阶段四：多路线并存（可选，按投入决定）
- **目标**：加能力探针/开关，使 **① 与 ④a（必要时含 ②）互为兜底**；**归一化**各路日志格式为同一形态。
- **验收标准**：同一 `.e` 在不同路线产出**可对比、格式统一**的 `run.log`；`capabilities.json` 缓存生效。

#### 阶段五：健壮性与边界（按拍板结果）
- **内容**：超时/挂起强制结束、GUI 窗口策略、运行期错误的降级处理（若拍板要覆盖）；**若 D.3-第2条拍板「纳入交互式调试」**，则追加单步/断点/变量查看（④a 路线）。
- **验收标准**：对「死循环样本」「弹窗样本」能**在超时内安全结束并给出明确提示**，不卡死调用方；若含交互调试，单步/断点可用。

### D.5 给目标 `.e` 加壳流程（规范，v4 新增；v5 订正因果）

> 「加壳」= **不动用户的原 `.e`**，只产出一个**库表里登记了本库的派生 `.e`**，使 e.exe 打开它时**按需加载** `elang_addin.fne`（依据任务 D）。这是技能要落地的**确定性步骤**。
> ⚠️ **v5 订正（此处已按实证修正）**：v4 曾把「改文本 `.支持库` 行」当输入，**因果写反了**——受控实验证明 **`配置/支持库.config.json` 才是 `t2e` 的输入**，文本里的 `.支持库 <key>` 行对 `t2e` **完全无效**。详见下方「坑 1」。

**三步 + 验收点**

| 步 | 动作 | 命令 / 要点 | 验收点 |
|---|---|---|---|
| 1 | 把**副本** `.e` 转成文本工程（**绝不改用户原 `.e`**） | `e2txt -mode e2t -src "<原名>.e" -dst "<临时文本目录>" -level 2 -enc UTF-8 -ns 2` | stdout 出现 `SUCC:`；文本目录含 `代码/` 与 `配置/` |
| 2 | **在 `配置/支持库.config.json` 里追加一项**（该文件是 `e2t` 产出的**库表**，`t2e` 从这里读） | 追加：`{"Key":"elang_addin","Guid":"<32位无连字符小写hex>","Name":"AI调试宿主","Version":{"Major":1,"Minor":0},"CmdCount":0,"MaxRefConstPos":0,"MaxRefObjectPos":0}`；`elang_addin` = 本库 `.fne` 文件名主干 | config.json 里该项存在；**不要去改** `代码/…static.e.txt` 的 `.支持库` 行（无效） |
| 3 | 文本 → `.e`，命名 `<原名>_ai.e` | `e2txt -mode t2e -src "<临时文本目录>" -dst "<原名>_ai.e" -enc UTF-8 -level 2 -ns 2`（`-dst` 父目录**先存在**；**必须 `-level 2`**，`-level 1` 会**静默丢空产物**） | stdout 出现 `SUCC:`；**产出 `.e` 存在且非空** |
| ★ | **总验收（双判据，缺一不可）** | — | ① **`<原名>_ai.e` 的 md5 必须与原 `.e` 不同**；② **二进制里能找到该 key 的 ASCII**（如 `elang_addin`）。**任一项不过 = 加壳失败，不得进入下一步** |

**必须写进去的三个坑（均有实证）**
1. **坑 1（v5 订正因果，关键）**：`配置/支持库.config.json` 是 `t2e` 的「输入」；文本文件里的 `.支持库 <key>` 行对 `t2e` **完全无效**——不要去写它，也不要把 `e2t` 回读文本里「没有 `.支持库` 行」当成异常。
   - 受控实验（`re/verify_libsource.py`；源 = `demos/01-C盘结构输出/项目`，基线 `.e` 6747 B / md5 `d0d47062b032`）：
     - **B 组｜只改 `配置/支持库.config.json`** → 产物 6818 B / md5 `ff549e9c6c2a`、**含 `elang_addin`** ⇒ ✅ **这才是 `t2e` 的输入**。
     - **C 组｜只改文本 `.支持库` 行** → 产物 **与基线逐字节相同**（6747 B / `d0d47062b032`）、不含该 key ⇒ ❌ **完全无效**。
     - **D 组｜B+C** = 同 B ⇒ 说明 C 没起作用。
   - **排除替代解释**（`re/verify_libsource2.py`）：把 C 组 key 换成**本机已安装**的 `cncnv`（`lib\cncnv.fne` 确实存在）→ 产物**仍与基线逐字节相同**、不含 `cncnv` ⇒ 文本通道**不是**「因库没装被丢弃」，而是**压根不通**。
   - ⚠️ **名字/产物混淆（v4 曾据此得出反向结论）**：`re/projD/代码.e` 是 `e2t` 文本目录里**自带的伴生文件**，**不是 `t2e` 的产物**（`t2e` 只写到 `-dst` 指定路径）。**改了 config.json 后 `t2e` 的产物就是 `ff549e9c`（6818 B）——正是任务 D 里那个成功加载了我们 `.fne` 的 `.e`**。⇒ 工程师最初的「改 config.json」做法**从一开始就是对的**。
2. **坑 2（保留并加强）：验收闸门 = 产物 `.e` 的 md5 必须变化，且二进制里能找到该 key 的 ASCII**（`ff549e9c` 那次即 6818 B 且含 `elang_addin`）。**两项都过才算加壳成功；技能把这一步做成硬校验、失败即报错退出。**
3. **坑 3（新增，顺序，关键）**：`e2t` 一个声明了「本机没装的支持库」的 `.e` 会 **失败**（stderr `加载支持库失败！[名称] … [主键] …`，**不产出** `class/*.e.txt`）。所以顺序**必须**是 **先 `e2t`（此时还没声明）→ 再改 `config.json` → 再 `t2e`**；**加壳完成后绝不要再 `e2t`**。

**`Key` / `Guid` 取值**
- `Key` = 本库 `.fne` 的**文件名主干**（例：`elang_addin.fne` → `elang_addin`）。
- `Guid` = **32 位无连字符、小写 hex**（须与 `LIB_INFO.m_szGuid` 一致）。
- `配置/支持库.config.json` 对应项：`CmdCount=0`（本库不暴露命令）、`Name` = 库名。
- `⟨待通用化⟩` 上例具体值（`7a1e4f22c3b0499e8d6a0011223344fe`、「AI调试宿主」等）仅为本库样例；**技能正文只写「怎么取」**（读 `LIB_INFO` / 看 `.fne` 名），不写死具体值。

### D.6 正式技能 `elang-debug` 包内文件清单（自包含规划，v4 新增）

> 依据 **Agent Skills 开放规范**：技能自包含，`scripts/` + `references/` + `assets/` 三类附属资源随技能整体安装；**正文不写本机绝对路径 / 版本号 / 本机统计数**，外部依赖只写「怎么找」。

| 路径 | 作用 | 纪律 |
|---|---|---|
| `SKILL.md` | 唯一入口：闭环用法、参数、判读、坑 | 正文**不含**安装目录绝对路径、库数量、易语言版本号等本机信息 `⟨待通用化⟩`；外部依赖写「`ECL` 环境变量 → 同目录 → `PATH`」式找法 |
| `assets/elang_addin.fne` | 编译好的宿主支持库（AddIn） | 32 位；**跨 DLL 指针一律 `__stdcall`** |
| `scripts/mkcage` | **加壳**：副本 e2t → 改 `配置/支持库.config.json`（追加库项）→ t2e（`-level 2`）→ **双判据校验**（md5 变化 **且** 二进制含该 key ASCII）（见 D.5） | 只读原 `.e`；双判据任一不过则报错退出；**加壳后绝不再 `e2t`** |
| `scripts/launch` | 设环境变量 → 启动 e.exe → **精确 PID 收尾** → 清理 `.fne` 并 `diff` 自证 | **禁止 `/IM`**（避免误杀用户正在编辑的 IDE） |
| `scripts/readlog` | 读输出面板落盘 / 增量日志 | 支持流式 append |
| `references/接口与调用约定清单` | = 上文 A.4「跨 DLL 函数指针约定清单」的成果 | 通用化，不含本机地址/偏移 |
| `references/踩坑清单` | 见下（六条） | 通用化 |
| `references/加壳流程` | = 上文 D.5 的成果 | 通用化 |

**踩坑清单**（转写自 `re/addin/POC1-结果与阻塞.md` §8，**已去掉本机信息**）
1. **官方 `mtypes.h` 把 `WINAPI` 置空** ⇒ SDK 内函数指针全被当成 `__cdecl`；垫片/实现**须把 `WINAPI` 还原为 `__stdcall`**，否则跨 DLL 回调栈错位、宿主崩溃。
2. **`CreateThread` 线程体必须是 `__stdcall`**——若沿用被置空的 `WINAPI` 会编成 `__cdecl`，行为错。
3. **`windows.h` 没有 `INT / FLOAT / DOUBLE / DATE / INT64 / PDATE`**，需自行 `typedef` 补齐（SDK 依赖这些类型）。
4. **`lib2.h` 是 C++ 专用**（含未 typedef 的联合 / 默认参数等）⇒ 需 **`g++ -fpermissive`** 编译。
5. **他人进程的模块枚举（ToolHelp32）可能被拒（`error 5`）** ⇒ **不能作为证据来源**；改用**库内自检**落盘（本方案 PoC-1 的做法）。
6. **`windres` 的 `.rc` 注释必须用 `//`，不能用 `;` 行注释**；数值常量不能带 `L` 后缀。

> `⟨待通用化⟩`：以上若含示例机器名/路径，进技能前须替换为「占位符 + 怎么找」表述；技能内路径一律以 `SKILL.md` 所在目录为基准。

---

## 附：本方案用到的关键事实与出处（便于复核）

**基础事实**
- 调试输出仅在调试版本有效：易语言官方帮助文档（`输出调试文本` / `调试输出` 备注原文）；e.exe 内亦含该限制字符串（RVA `0x0024B154` / `0x002650EC` / `0x0026B644`，见分析报告 §3.5）。
- **DBWIN 通道（v2 证据升级）**：调试输出底层为 `OutputDebugStringA` → `DBWIN_BUFFER` 共享内存 + `DBWIN_DATA_READY` 事件。**已有直接二进制证据**：核心库 `krnln.fnr`（运行版）与 `krnln.fne`（编辑器版）**均导入 `KERNEL32!OutputDebugStringA`**（`re/probe_debug_io.py`，已独立复核）。~~仅剩「无 IDE 宿主时是否仍投递」这一环待 P0 活体探针收口（或由 PoC-2(c) 一并验证）。~~ → **v6**：该环节**仍开放但不再阻塞任何已选路线**——主路线 ④a 直读面板取日志、不依赖 DBWIN；PoC-2(c) 的 DBWIN 并行对照未做（无必要）。仅当未来启动路线①时才需要补此探针。
- `ecl.exe` 用法（路线①）：ECommandPrompt（Gitee `zhongjianhua163/ECommandPrompt` / eyuyan.cn 介绍页）。
- 往返/格式铁律、`-level 2`、`-ns 2`、etprj 编码：见 `docs/易语言文本格式规范.md` 与 `elang-ai-coding/SKILL.md` §1/§5/§6。
- 改动范围结论：基于对 `sync-release.sh`、`verify-release.py`、`pack-skill.py`、`install-skills.py`、`scan-machineinfo.py`、`src/build.sh` 的**逐文件确认**（非猜测）。

**路线④ 事实与出处（来源：`docs/分析-易语言主程序与官方扩展接口.md` 与证据目录 `re/`）**
- 官方进程内控制链路（`NES_GET_MAIN_HWND` / `NES_RUN_FUNC`）：`sdk/cpp/elib/lib2.h:1016-1028,1173-1177,1242-1251,1345,1363`；官方实现 `sdk/cpp/elib/fnshare.cpp:26-55`；官方示例 `sdk/cpp/samples/HtmlView/HtmlView.cpp:364-378,449,460-463`、`HtmlView.def:4-7`。
- **调用约定（v3 实证，关键）**：`m_pfnNotify` 与 `PFN_NOTIFY_SYS` 实为 **`__stdcall`**（e.exe 调用 `m_pfnNotify` 处 `e.exe+0x460872..0x46087A` 之后**无 `add esp`**；真库 `cncnv/dp1/console/iext` 的 notify 均以 `ret $0xc` 返回；`PFN_NOTIFY_SYS=0x00467FF0` 末尾 `ret $0xc`）。SDK 头 `mtypes.h:9` 把 `WINAPI` 置空 → 误判为 `__cdecl`，**照抄会崩**。`GetNewInf`（无参）不受影响；`m_pfnRunAddInFn(INT)`/`m_pfnSuperTemplate(INT)` **待实测**。出处：`re/addin/POC1D-免登记成功与__stdcall根因.md`。
- IDE 功能号表（`PublicIDEFunctions.h`）：`FN_COMPILE_AND_RUN`（:400）、`FN_END_RUN`、`FN_COMPILE`、`FN_STATIC_COMPILE_WINDOWS_EXE`、`FN_OPEN_FILE2`（:332）、`FN_ADD_TAB`（:421，结构 `ADD_TAB_INF` :19-27）、`FN_SWITCH_OUTPUT_BAR`（:338）、`FN_IS_FUNC_ENABLED`（:444）、调试整类（:387-395：`FN_STEP_INTO`/`FN_STEP`/`FN_STEP_OUT`/`FN_RUN_TO_CURSOR`/`FN_VIEW_VAR`/`FN_SET_BREAK_POINTER`/`FN_CLEAR_ALL_BREAK_POINTER`/`FN_SHOW_NEXT_STATMENT`/`FN_ADV_BREAKPOINT`）。
- ✅ **加载机制（此处已按实证修正）**：e.exe **并非**启动即全量加载新 `.fne`；其启动加载集合 = **「已登记集合」**（实测 **76/77**，`etools.fne` 未加载），**新丢进 `lib\` 的库不自动加载**（`re/addin/POC1-结果与阻塞.md`）。**进入 IDE 进程有两条路**：**(i) 全局登记**（「工具→支持库配置」，任务 A，**未通**）；**(ii) 被打开的 `.e` 声明本库时「按需加载」**（任务 D，**✅ 已通**：`re/opens_decl_e.py`、`re/addin/evidence/D_trace_full.txt`）。安装位置取自注册表 `HKCU\Software\FlySky\E\Install\Path`（`re/reg.txt`）。
- e.exe 是调试器 / 主窗口类名 `ENewFrame` / 支持命令行开工程 / **无导出表** / **无 ASLR·DEP·SafeSEH**：`re/analyze_e.py`、`re/strings_e.py`、`re/run_enum2.py`、`re/run_proj.py`、`re/proj_out.txt`。
- 输出面板控件来源（`ScintillaForEIDETools` 由 `lib\iDraw\superTools\plugin\eOutPutControl.dll` 注册、启动即加载）：`re/find_classes.py`、`re/mods_noproj.txt`。
- ✅ **已由 PoC-1/D 解决/明确的开放点**：加载 `.fne` **无需**事前签名/白名单（我们的库被正常加载、`GetNewInf` 被正常调用）；`NL_IDE_READY` 到达时功能号即可用（`FN_IS_FUNC_ENABLED` 全表 `handled=1`）。**仍待验证**：e.exe 是否有「无窗口」命令行开关（PoC-3）；`m_pfnRunAddInFn`/`m_pfnSuperTemplate` 的调用约定。见分析报告 §7 与 `re/addin/POC1D-免登记成功与__stdcall根因.md` §5。

> **并稿声明**：本附表与正文中凡标注「**已按实证修正**」处，均以工程师带证据的结论为准覆盖早前表述。**v2**：B.3 的 DBWIN 证据强度、B.4 的「交互式断点是否可行」。**v3**：A.4/C.4 的「启动即全量加载 77/77」纠正为「**已登记集合 76/77 + 按需加载**」；新增 **`__stdcall` 调用约定**与**任务 D 免登记加载成功**两项事实；**PoC-1 / 任务 D 状态标为已完成**（PoC-2/PoC-3 待做）。**v4**：新增 **A.4「跨 DLL 函数指针约定清单」**（覆盖 `m_pfnNotify`/`PFN_NOTIFY_SYS`/`GetNewInf`/`m_pfnRunAddInFn`/`m_pfnSuperTemplate`/`m_pCmdsFunc[]` 等，来源 `lib2.h`）、**D.5「加壳流程（规范）」**、**D.6「技能包内文件清单」**，并把 **PoC-2 判据写死**；不宜外发的本机信息以 `⟨待通用化⟩` 标注（不删事实）。**v5**：**订正 D.5「加壳」因果**（此处已按实证修正）——`配置/支持库.config.json` 是 `t2e` 的**输入**、文本里的 `.支持库 <key>` 行**无效**（来源 `re/verify_libsource.py` / `verify_libsource2.py`）；D.5 坑块改为 **3 条**（改因果 / 双判据闸门 / 顺序）、`t2e` 一律 **`-level 2`**；D.6 `scripts/mkcage` 与 C.4 表述**同步新口径**。**v6**：**回填 PoC-2 结论**（判据 A/B/C 全过 + 三种收尾覆盖 + 面板尾行不可判的口径；物证 `re/addin/evidence/run{A,B,C}_*`）；C.4 面板 HWND/类名标**已坐实**；D.4 **P0 探针关闭**（主路线不依赖 DBWIN）、**阶段二/三标已完成**（技能已发布，总检 56/0）；补**真实工程规模实测**（7 工程 0.66~3.9 MB 往返零 `[错误]`、加壳产物可编译可运行，「大工程可能编不过」被实测排除，实测脚本 `re/rt_big_test.py`/`re/rt_batch.py`）；如实标注未做项：DBWIN 并行对照、`FN_ADD_TAB` 备选通道、PoC-3。
