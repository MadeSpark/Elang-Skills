# PoC-1 / 任务D：免登记加载 —— **成功** + 两个根因（__stdcall）

> 状态：**已跑通（可复现）**。发布版（clean）与诊断版（diag）均全程 ALIVE，无崩溃。
> 判据（唯一）：`%TEMP%\elang_addin_selfcheck.txt` 中出现本次 `pid=`（diag 构建无条件落盘）。
> 日期：本轮；实验脚本：`re/opens_decl_e.py`；证据：`re/addin/evidence/D_trace_full.txt`。

---

## 0. 一句话结论

**把「引用本库的 .e」交给 e.exe 打开时，e.exe 会按需去 `lib\` 加载我们的 `.fne`、调用
`GetNewInf()`、下发 `NL_SYS_NOTIFY_FUNCTION`(×2) 与 `NL_IDE_READY`，并允许我们通过官方
接口只读探测 IDE —— 全过程不需要把库登记进任何全局选择列表（免全局登记成立）。
此前"打开即崩"的根因不是 RT_VERSION、不是 m_dwState、不是 m_szzCategory，而是
`__stdcall / __cdecl` 调用约定不符（共两处）。**

---

## 1. 两个根因（全部经反汇编 + 崩溃现场实测证实）

### 根因①：本库的 `m_pfnNotify` 必须是 `__stdcall`，SDK 头说 `__cdecl` 是误导

- 官方 `mtypes.h` 第 9 行 `#define WINAPI`（置空）→ SDK 内 `PFN_NOTIFY_LIB = INT (WINAPI *)(...)`
  被展开成 **__cdecl**。我方原先照此实现，于是 notify 用 `ret` 返回、不弹出 3 个参数。
- 但 e.exe 调用 `m_pfnNotify` 处（**e.exe+0x460872..0x46087A**）：
  ```
  push %ebx            ; dwParam2 = 0
  push $0x00467FF0     ; dwParam1 = IDE 的 NotifySys
  push $0x1            ; nMsg = NL_SYS_NOTIFY_FUNCTION
  call *0x78(%eax)     ; eax = LIB_INFO*（0x78 = offsetof m_pfnNotify = 120）
  mov  0x14(%esp),%ecx ; ← 调用后**没有** add $0xc,%esp
  ```
  调用后不调栈 ⇒ e.exe 期望 callee 清栈 ⇒ **它是按 `__stdcall` 调的**。
- 实测旁证：真实支持库的 notify 入口全部以 `ret $0xc` 返回（= __stdcall）：
  `cncnv.fne@0x100010F0`、`dp1.fne@0x10003550`、`console.fne@0x10001500`、`iext.fne@0x10001B80`。
- 后果（用 `-DPOC_CRASHTRAP` 的 VEH 抓到现场）：
  我们的 notify 少弹 0xC ⇒ e.exe ESP 整体低 0xC ⇒ 它把栈上的垃圾值当指针传给一个
  "LIB_INFO 转储"函数 ⇒ 在 **e.exe+0x100BE8** 执行 `mov 0x4(%edi),%ecx`（EDI=1）
  ⇒ **READ 0x00000005 → 0xC0000005**。现场 EDI/ESI/EAX 与"少弹一个参数"完全吻合。

### 根因②：调用 e.exe 的 `NotifySys(0x467FF0)` 也必须按 `__stdcall`

- e.exe 通过 `NL_SYS_NOTIFY_FUNCTION` 把 `PFN_NOTIFY_SYS = 0x00467FF0` 交给库，供库反向通知 IDE。
- 反汇编 0x467FF0：末尾 `ret $0xc`（**__stdcall**）：
  ```
  468008: call *%eax          ; 调 e.exe 内部派发器 g[0x675F6C]
  46800A: add  $0xc,%esp      ; 派发器是 __cdecl，由本函数清
  46800D: ret  $0xc           ; ← NotifySys 自身是 __stdcall
  ```
- 若库按 SDK 的 `WINAPI`(空=__cdecl) 去调它：callee 已清 3 参、调用方再加 esp ⇒ 工作线程
  ESP 高 0xC ⇒ 读写错位。实测后果：只读到垃圾、且 **e.exe 往我们的只读段写入**
  ⇒ 第二次崩于 `WRITE 0x7BBB2947 (elang_addin.fne+0x2947)`（= 我们的 `g_ProbeFns[]` 所在 .rdata），
  同时表现为"探测表里 `dwFn` 变成乱值 + e.exe 提前 exit 0"。

### 修复
```cpp
/* 凡是"由 e.exe 回调 / 回调 e.exe"的函数指针，一律显式 __stdcall */
typedef INT (__stdcall *PFN_NOTIFY_SYS_STD)(INT, DWORD, DWORD);   // 调 e.exe 用
INT __stdcall ProcessNotifyLib(INT,DWORD,DWORD);                  // 被 e.exe 调
INT __stdcall ElangAi_ProcessNotifyLib(INT,DWORD,DWORD);          // = m_pfnNotify
s_LibInfo.m_pfnNotify = (PFN_NOTIFY_LIB)ElangAi_ProcessNotifyLib;
```
> 与 `etools.fne` 的比对：LIB_INFO 各字段（含 `m_szzCategory=NULL`、`m_dwState`)几乎一致；
> **唯一的语义性差异就是这个调用约定**。这也解释了为何控制组(projD_base/projE_cncnv/
> projF_etools)**都不崩**，只有引用我们库的 .e 崩。

---

## 2. 成功证据（发布版/clean 构建，判据=自检 pid 命中）

| 项目 | 结果 |
|---|---|
| e.exe 打开 `.e`（该 .e 声明 `Key=elang_addin`) | ALIVE，exit=-（未退出），随后被精确 PID 关闭 |
| `GetNewInf()` 被调用 | ✅（selfcheck pid 命中） |
| `NL_SYS_NOTIFY_FUNCTION` | ✅ 收到 **2 次**，PFN_NOTIFY_SYS=0x00467FF0 |
| `NL_IDE_READY`(=18) | ✅ 收到 |
| trace 行数 / 结束标志 | 269 行 / `WORKER done`（STEP3 完整跑完） |
| 崩溃陷阱 | 本次进程**无**任何 CRASHTRAP 段落 |

### 关键探测结果（来自 `D_trace_full.txt`）
- `NES_GET_MAIN_HWND` → **hwnd=0x001D03D2，class='ENewFrame'**，visible=1。
- `GetCommandLineA` = `e.exe …\ework_decl\probe.e`；`GetModuleFileNameA` = `D:\ides\e\e.exe`。
- **`FN_IS_FUNC_ENABLED` 全表 handled=1**（e.exe 接受这些功能号）：
  - `FN_COMPILE_AND_RUN(0x05020002)` → **enabled=1**  ← "调试运行"可被程序化触发
  - `FN_COMPILE(0x05020001)`→1、`FN_OPEN_FILE2(0x03010008)`→1、`FN_ADD_TAB(0x05030001)`→1、
    `FN_SWITCH_OUTPUT_BAR(0x04020003)`→1、`FN_GET_PRG_TEXT(0x0503000A)`→1、
    `FN_RUN_TO_CURSOR`→1、`FN_SET_BREAK_POINTER(0x05010006)`→1、`FN_CLEAR_ALL_BREAK_POINTER`→1、
    `FN_MOVE_CARET`/`FN_INSERT_TEXT`/`FN_PRE_COMPILE`→1
  - 运行/调试态相关：`FN_END_RUN`→0、`FN_STEP_INTO`/`FN_STEP`/`FN_STEP_OUT`→0、
    `FN_SHOW_NEXT_STATMENT`→0（当前未在运行/调试，符合预期）
- `EnumChildWindows` 完整列出主窗口 **208** 个子窗口（class/text/rect/visible），
  含 `MDIClient`、`SysTabControl32 状态夹左右`、`ScintillaForEIDETools`（内联汇编插件输出）、
  `ToolbarWindow32 标准/对齐/定位工具条`、`ComboBox 程序集1 / _启动子程序`、状态栏等。

---

## 3. 复现方法

```bash
# 1) 编译（含 __stdcall 修复）
cd re/addin
export PATH="/c/msys64/mingw32/bin:$PATH"
windres.exe -i src/version.rc -O coff -o version.res
SDK="/d/ides/e/sdk/cpp/elib"
# 发布版
g++ -m32 -shared -O2 -D__GCC_ -DWIN32 -fpermissive -finput-charset=UTF-8 -fexec-charset=GBK \
    -I"$SDK" src/elang_addin.cpp src/elang_addin.def version.res \
    -o elang_addin.fne -static -s
# 诊断版（自检 + 崩溃陷阱）
g++ -m32 -shared -O2 -D__GCC_ -DWIN32 -fpermissive -DPOC_SELFCHECK -DPOC_CRASHTRAP \
    -finput-charset=UTF-8 -fexec-charset=GBK -I"$SDK" \
    src/elang_addin.cpp src/elang_addin.def version.res -o elang_addin_diag.fne -static

# 2) 免登记实验（投放 diag.fne → e.exe 打开声明该库的 .e → 查 selfcheck/trace → 精确PID清理）
python re/opens_decl_e.py "C:/Users/MadeSpark/Desktop/测试/re/projD_lib.e" 25
```

---

## 4. 附：本轮顺带排除/确认的事项

- **RT_VERSION 不是根因**：已用 `windres` 给 .fne 补上 `RT_VERSION(16)`（`src/version.rc`，
  之前 `windres` 报 `syntax error` 的原因是 **GNU windres 不认 `;` 行注释**，须用 `//`；
  数值常量不能带 `L` 后缀）——补了之后**依旧崩**，改 __stdcall 后才不崩。保留该资源（无害、且与真库一致）。
- **`m_szzCategory`**：已从 `""` 改为 `NULL`，与 `etools.fne`（0 类别时）对齐。
- **`m_dwState`** 不是根因：0xE0000104 为当前值（= OS_ALL|LBS_IDE_PLUGIN|LBS_FUNC_NO_RUN_CODE）。
- **崩溃陷阱工具**（`-DPOC_CRASHTRAP`）：`AddVectoredExceptionHandler` 抓异常码/出错地址/
  出错指令/寄存器/**栈上候选返回地址 + 所属模块**，无条件下写
  `%TEMP%\elang_addin_crashtrap.txt`。正是它把根因从"猜"变成"看"。
  （模块枚举用 **PEB 手工遍历**，因为 MinGW 下 ToolHelp32 在本工程出现未解析符号。）
- **纪律**：实验后 `lib\elang_addin.fne` 已删除（lib\ 77 个 .fne 完整）；
  唯一遗留是 PID 49560 的 e.exe（**User=N/A、无窗口、20K、0 CPU 的僵尸条目**，
  `taskkill /F /PID` 返回"拒绝访问"，非本会话所有，疑似沙箱/更早实验残留，非用户 IDE）。

---

## 5. 对后续的含义（给主理人/架构师）

1. **免全局登记路线（D）成立**：产出物可让"声明了本库的 .e"自带加载能力，
   IDE 打开即生效，**不需要**改注册表/走"支持库配置"对话框。
2. **"调试运行/监控调试框"的关键闸门已开**：`FN_COMPILE_AND_RUN / FN_END_RUN /
   FN_SET_BREAK_POINTER / FN_SHOW_NEXT_STATMENT / FN_GET_PRG_TEXT` 全部 handled=1，
   且 `FN_COMPILE_AND_RUN` 当前 enabled=1 → 下一步（PoC-2）可用 `NES_RUN_FUNC` 真正触发
   "调试运行"并观察 `FN_END_RUN`/状态夹文本变化。
3. **SDK 头不可尽信**：凡涉及"跨 DLL 边界的函数指针"，一律以 e.exe 实测反汇编为准。
   已知：`m_pfnNotify`=__stdcall、`PFN_NOTIFY_SYS`=__stdcall；`GetNewInf` 无参不受影响。
   （待查：`m_pfnRunAddInFn(INT)`、`m_pfnSuperTemplate(INT)` —— 真库样本里分别见 `ret $0x4` 迹象，用前需再验。）
