# PoC-2：程序化「调试运行」+ 增量监控调试框 —— 成功

> 判据（**均在盘、可 grep**）：`re/addin/evidence/runA_*` / `runB_*` / `runC_*`。
> 实验台：`re/poc2_run.py --tag <TAG>`（加壳 `re/mkcage.py` → 投放 `.fne` → e.exe 打开 → `ELANG_AI_RUN=1` 触发）。
> 全部**只用精确 PID** 收尾；`D:\ides\e\` 顶层 before/after diff = 无新增文件。

## 0. 一句话

**在 e.exe 进程内、通过官方 `NES_RUN_FUNC(FN_COMPILE_AND_RUN)`，可以程序化触发一次完整的
“调试运行”，并把“调试框”的文本做 50ms 增量流式落盘；面板里确实出现被调试程序自己输出的业务文本。**

## 1. 加壳（前置）——`re/mkcage.py`（按 team-lead 1~5 步实现）

`e2t`(副本, `-level 2 -enc UTF-8`) → **只改 `<文本目录>/config/lib.config.json`**（读 utf-8-sig →
append `{Key,Guid,CmdCount,MaxRefConstPos,MaxRefObjectPos,Name,Version}` → `json.dump(ensure_ascii=False,
indent=4, sort_keys=True)` 保留 BOM）→ `t2e` 到 `<原名>_ai.e` → 双判据。**全程不碰任何 `*.e.txt`。**

实测（`projD_base.e`）：

| 项 | 值 |
|---|---|
| 用户原件 | 6747 B / md5 `d0d47062b032606e78d5a8b8039822f2` |
| 加壳产物 | **6818 B / md5 `ff549e9c6c2afdb688d386dc7770b7e0`** |
| ① md5 ≠ 原件 | ✅ |
| ② 二进制含 ASCII `elang_addin` | ✅ |

> 与 team-lead 受控实验的 B 组 md5 `ff549e9c6c2a` **逐字节一致**，也与 `re/projD_lib.e` 一致 → 加壳确定性复现。
> `t2e` 会打一条**非致命**前端提示：`引入支持库失败！…[原因] 支持库不存在`（此刻 `lib\` 未装该库）——它仍写库记录并 `SUCC:`。加壳后**不要**再 `e2t`。

### 1b. 加固：Guid/库名/版本**从 `.fne` 现取**（team-lead §7.3）

`re/addin/src/offset_probe.cpp` 新增 `--dump-libinfo <fne>`：`LoadLibrary` 该 `.fne` → 调其
`GetNewInf()` → 打印机器可读 `KEY/GUID/NAME/MAJOR/MINOR`。`re/mkcage.py` 据此填库记录
（GUID 统一小写，与 e2t 写 config 的约定一致；取不到才回退内置常量）。
**目的**：`.fne` 重建后 Guid 漂移不会让“加壳产物声明旧 Guid → e.exe 加载失败”。
实测：加固后对 `projD_base.e` 的产物 md5 仍为 `ff549e9c6c2afdb688d386dc7770b7e0`（**逐字节不变**）：
```
[0/4] 本库信息(来自 offset_probe.exe): KEY=elang_addin GUID=7a1e4f22c3b0499e8d6a0011223344fe NAME=AI调试宿主 VER=1.0
```

## 2. 触发“调试运行”

`ELANG_AI_RUN=1`（+`ELANG_AI_RUN_DELAY` 毫秒）→ worker 在 `NL_IDE_READY` 后调用
`NES_RUN_FUNC(FN_COMPILE_AND_RUN=0x05020002, 0, 0)`，返回 `handled=1`。

## 3. 三个问题的答复 —— 物证（逐条 文件 / 行号 / 原始行）

> ⚠️ 面板文本是 **GBK**。原始证据文件逐字节保存；另产出 `<TAG>_*.utf8.txt` **UTF-8 转码视图**，
> 便于在 UTF-8 终端直接 `grep` 中文。**grep 中文请用 `.utf8.txt`**；ASCII 关键字（`PANEL-DELTA` 等）两者都可。

### Q1 日志里是否出现「程序自己输出的业务文本」？→ **是 ✅**

文件：`re/addin/evidence/runA_capture.utf8.txt`

```
31:[21:11:13.411][PANEL-DELTA] DELTA: [21:11:13] * 嵌套压测完成
14:正在编译现行程序
21:程序代码编译成功
24:[21:11:13] 开始运行被调试程序
37:[21:11:13.534][PANEL-DELTA] DELTA: 被调试易程序运行完毕
```
即被调试程序的 `输出调试文本()` 会以 **`[HH:MM:SS] * ` 前缀**出现在调试框
（`class='ScintillaForEIDETools'`，状态夹里的调试输出面板）。

### Q2 面板能否增量流式落盘？→ **是 ✅**

文件：`re/addin/evidence/runA_capture.utf8.txt`（捕获线程每 50ms 取文本类子窗口全文，
**只把相对上次的“新增后缀”**写盘，标记 `PANEL-DELTA`）。同一 `ScintillaForEIDETools` 面板 `len` 序列：

```
8:[21:11:13.281][PANEL-CHANGED] hwnd=0x00641392 class='Edit' oldlen=161 newlen=20
11:[21:11:13.284][PANEL-CHANGED] hwnd=0x005E0702 class='ScintillaForEIDETools' oldlen=560 newlen=808
27:[21:11:13.348][PANEL-CHANGED] hwnd=0x005E0702 class='ScintillaForEIDETools' oldlen=808 newlen=837
30:[21:11:13.411][PANEL-CHANGED] hwnd=0x005E0702 class='ScintillaForEIDETools' oldlen=837 newlen=863
33:[21:11:13.475][PANEL-CHANGED] hwnd=0x005E0702 class='ScintillaForEIDETools' oldlen=863 newlen=903
39:[21:11:13.534][PANEL-CHANGED] hwnd=0x005E0702 class='ScintillaForEIDETools' oldlen=903 newlen=936
```
`560 → 808 → 837 → 863 → 903 → 936`，每段带毫秒时间戳、内容是**增量后缀** → **边跑边落**，无需等运行结束。

### Q3 三种收尾的「原始尾行」

文件：`re/addin/evidence/runA_capture.utf8.txt`（①）、`runB_capture.utf8.txt`（②）、`runC_trace.txt` + 进程退出码（③）。

| 收尾 | 触发 | 原始尾行 / 证据 |
|---|---|---|
| **① 正常结束** | runA：demo02 跑完 | `34:[...]PANEL-DELTA] DELTA: [调试] - 调试程序[59116]退出,ExitCode:0`<br>`40:[...]DELTA: [21:11:13] 被调试易程序运行完毕` |
| **② 用户中断** | runB：18s 长任务，3.0s 时我们发 `FN_END_RUN` | `38:[...]DELTA: [调试] - 调试程序[76208]退出,ExitCode:0`<br>`39:[21:11:42] 被调试易程序运行完毕` |
| **③ 程序异常** | runC：`被零除` 程序 | **e.exe 自身崩，面板无尾行** → 见下（改用**子进程退出码**，已取证） |

**① 与 ② 尾行逐字相同**（都 `ExitCode:0` + `被调试易程序运行完毕`）。
②中断确实生效的证据（runB，两处）：
```bash
# 文件 runB_capture.utf8.txt
31:[21:11:39.254][PANEL-DELTA] DELTA: [21:11:39] * 长任务开始
33:[21:11:42.079][RUN-STOP] send FN_END_RUN
41:[21:11:42.141][RUN-STATE] FN_END_RUN enabled: 1 -> 0 (t=3.1s)
# 且 grep "长任务结束" -> 0 命中（中断真的生效）
```

### ③ 程序异常：**改用「子进程退出码」，已取证 ✅**（team-lead §7.1）

e.exe 是宿主**直接子进程**（`poc2_run.py` 用 `subprocess.Popen([E_EXE, ...])` 拉起）——
故宿主对 e.exe 调 `WaitForSingleObject` + `GetExitCodeProcess` 即可拿到**确定信号**。实测：

```
# runC（re/poc2_crash/crash.e，被零除）
   e.exe pid=78156
   alive=False exit=0xC0000005 elapsed=13.0s        ← GetExitCodeProcess 风格：0xC0000005
```
崩溃点**不在本库**（对照实验 `ELANG_AI_NOPOLL=1` 复现同址）：
```
ExceptionCode = 0xC0000005  READ 0x00000004
ExceptionAddr = iControls.dll+0x5092E      ← 第三方 IDE 插件（空指针）
栈：<main-exe> … iDraw.fne+0x556E … iControls.dll+0x50E47 … Plugin_eFix.dll … SciLexer.dll … GDI32
```
证据：`re/addin/evidence/PoC2_crashtrap_iControls.txt`、`runC_trace.txt`。

## 4. ★ 产品约束（明确写死，不是“备注”）

1. **「被调试程序异常」不要读面板，读子进程退出码。**
   宿主拉起 e.exe 后 `WaitForSingleObject(hProc)` + `GetExitCodeProcess`；
   `0xC0000005 / 0xC0000094(除零) / 非 0` ⇒ 判定为“异常/非正常退出”并上报 AI。
   （面板在异常时可能随 e.exe 一起消失，**面板不可靠**。）
2. **「正常跑完」vs「被主动中断」不可由尾行区分**（两者都 `退出,ExitCode:0` + `被调试易程序运行完毕`）。
   产品侧判据**只能**是：
   `我们主动发过 FN_END_RUN` **且** 观测到 `FN_END_RUN enabled 由 1→0` ⇒ “被中断”；否则为“自然结束”。
3. **运行结束的判定信号**：`FN_IS_FUNC_ENABLED(FN_END_RUN)` 由 1→0（本次全部用例中，该跃迁在 ~60ms 内出现）。
4. **异常程序应隔离运行**：被调试程序一旦运行期异常，可能连带打挂 IDE（③）。批量/自动场景建议
   每个被调试会话独立 e.exe 进程 + 独立退出码判定。

## 5. 复现命令

```bash
# 加壳（离线）；可选 --fne <fne> 指定要嵌入的库，默认 addin/elang_addin.fne
python re/mkcage.py <用户.e> [<输出_ai.e>]
python re/mkcage.py <用户.e> --fne re/addin/elang_addin.fne
# 端到端取证（证据按 tag 唯一命名，落在 re/addin/evidence/）
python re/poc2_run.py "demos/02-嵌套控制流压测/项目/代码.e" --tag runA --delay 2500
python re/poc2_run.py "re/poc2_long/long.e"  --tag runB --delay 2500 --stop 3
python re/poc2_run.py "re/poc2_crash/crash.e" --tag runC --delay 2500
# 设备信息自证：
re/addin/offset_probe.exe --dump-libinfo re/addin/elang_addin.fne
```
环境变量：`ELANG_AI_RUN=1`、`ELANG_AI_RUN_DELAY`(ms)、`ELANG_AI_STOP`(秒，`-1`=运行即中止)、
`ELANG_AI_CAPTURE`(直接指向 evidence 文件，`%TEMP%` 拷件已废弃)、`ELANG_AI_NOPOLL=1`(对照用)。

## 6. 交付物

- `re/mkcage.py`（加壳内核；**已加固**：Guid/库名/版本从 `.fne` 现取）
- `re/poc2_run.py`（实验台；**证据按 `--tag` 唯一命名、不再删/拷**，并产出 `.utf8.txt` 视图）
- `re/addin/src/offset_probe.cpp`（新增 `--dump-libinfo`）
- `re/addin/src/elang_addin.cpp`（PoC-1 STEP1-3 + PoC-2 STEP4：`DoRunAndCapture` / `PanelSnapshot` /
  `PanelPollOnce`(增量) / `PanelDumpTails`(尾行)）
- 构建件 `re/addin/elang_addin.fne`(clean) / `elang_addin_diag.fne` / `offset_probe.exe`
- **证据**（`re/addin/evidence/`）：
  - `runA_trace.txt` + `runA_capture.txt` (+ `.utf8.txt`) — ① 正常结束
  - `runB_trace.txt` + `runB_capture.txt` (+ `.utf8.txt`) — ② 主动中断
  - `runC_trace.txt` + `runC_capture.txt` (+ `.utf8.txt`) — ③ 进程崩溃 + 退出码
  - `PoC2_crashtrap_iControls.txt` — 崩溃陷阱原始现场（iControls.dll）

## 7. 待办 / 未做

1. ~~收尾③ 面板尾行~~ → **已由「子进程退出码」方案替代并取证**（见 §3③ / §4.1）。
2. ~~`mkcage` Guid 硬编码~~ → **已加固：从 `.fne` 现取**（见 §1b），产物 md5 不变。
3. **待评估风险**：`e2t→t2e` 对真实大工程有损（team-lead 已摸底：库表是可读文本记录，
   但容器另有**原地改写的偏移/校验字段**（首差异@偏移 513，4B 疑似校验和）⇒ 二进制直插要先解偏移表+校验和，短期不划算）。
   计划：**取证过后用真实大工程跑一遍 `e2t→t2e` 往返看还能不能编过**。
