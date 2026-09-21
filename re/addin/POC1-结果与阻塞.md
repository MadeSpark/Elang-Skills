# PoC-1 结果与阻塞（AI 宿主支持库 · 路线④a）

> 结论一句话：**支持库骨架已完整写通并能被普通进程正常加载；但第 0 步（把新 .fne 丢进
> `lib\` 后 e.exe 启动时自动加载它）失败 —— e.exe 根本没有 LoadLibrary/GetNewInf 我们的库。**
> 因此第 1~3 步（探测钩子、读参数、只读探测）无从触发，按约定停下回报。

---

## 1. 先摆证据：e.exe 是否调用过我们的库？（决定性）

为了排除“其实加载了、只是环境变量没传进去”的可能，专门做了一个**无条件**自检：
编译带 `-DPOC_SELFCHECK` 的 .fne，在 `GetNewInf()` 里**不依赖任何环境变量**就往
`%TEMP%\elang_addin_selfcheck.txt` 追加一行。

| 试验 | 结果 |
|---|---|
| `load_test.exe` 独立加载我们的 .fne（对照组） | ✅ selfcheck 文件出现，写着 `pid=15508 / GetNewInf() called` |
| 投放进 `lib\` 后启动 e.exe（3 次独立运行 + 1 次双次运行） | ❌ selfcheck 文件**始终没有新增任何 e.exe 的记录** |

**结论：e.exe 从未调用过我们的 `GetNewInf()` —— 即从未 LoadLibrary 我们的 .fne。**

另：e.exe 那几次确实正常跑起来了（它在 `lib\iDraw\...` 下改了 2 个 ini，说明它加载了
插件、正常初始化），所以不是“e.exe 没起来”。

## 2. 再排除“我们的 .fne 本身有问题”

`load_test.exe`（32 位，普通 Win32 进程）实测：

```
LoadLibraryA('...\elang_addin.fne') -> OK, hModule=0x50090000
GetNewInf @ 0x500924C0 -> 返回合法 LIB_INFO：
  m_dwLibFormatVer = 20000101 (== LIB_FORMAT_VER)
  m_szGuid         = '7A1E4F22C3B0499E8D6A0011223344FE'
  m_szName         = 'AI调试宿主'      <-- GBK 正常，无乱码
  m_dwState        = 0x00000100 (LBS_IDE_PLUGIN)
  m_pfnNotify      = 0x50092490 (非 NULL)
  合成通知 NL_SYS_NOTIFY_FUNCTION->0, NL_IDE_READY->0, 未知码->-1
```
导出表：**只有 1 个导出 `GetNewInf`（未修饰）**；依赖仅 `KERNEL32/USER32/msvcrt`（无 MinGW 运行库依赖）。

**结论：.fne 是合法的、可加载的、导出正确的。问题不在库本身。**

## 3. e.exe 到底加载哪些库？（关键旁证）

对 e.exe 运行时模块清单（阶段一 `re/mods_noproj.txt`）与 `lib\` 磁盘清单做集合差：

```
lib\ 顶层 *.fne        = 77 个
e.exe 实际加载 *.fne   = 76 个
差集（在磁盘、但没被加载） = { etools.fne }
```

- 76 = 顶层 77 个减去 `etools.fne`，**一个不多一个不少**。
- `etools.fne`（133120 字节、有 `GetNewInf` 导出、`m_dwState=0xE0000104`，
  库名「易语言助手」，**文件日期 2023-05-20**，是这份安装里“最新加进来的库”）**没被加载**。
- 我们在 e.exe 里扫到的 `.fne/.fnr/.fnl` 字面量**只有 5 个**：
  `cncnv.fne / cnvpe.fne / dp1.fne / krnln.fne / krnln.fnr` —— 即 e.exe 只“硬编码”了核心库，
  其余 70+ 个不可能来自硬编码。

**推论：e.exe 加载的库集合来自一份“已登记/已选择”的持久列表，而不是启动时实时扫描
`lib\` 目录。** 新丢进去的库（我们、以及当年的 etools.fne）都不在列表里 → 不加载。

### 已排除的“列表存放位置”
- HKCU\Software\FlySky 全树（含二进制值）：无任何 `.fne`/库名明文；
  `EInf40\SaveStr40(1363B)`、`SaveE40(892B)` 是**压缩/编码**过的，无明文库名。
- D:\ides\e 全树、%APPDATA%、%LOCALAPPDATA%、%PROGRAMDATA%：无“库名清单”类文件
  （命中项分别是 Inno 安装日志 `unins000.dat`、iDraw 插件自带的 `category_id.db`、
  以及各 .e 样例工程，均非 e.exe 运行时配置）。
- HKLM\SOFTWARE[\Wow6432Node]\FlySky：**键不存在**。

## 4. 试过但都失败的“让它自动加载”的路子
1. 投放 + 启动一次（taskkill 强杀）→ 未加载。
2. 投放 + 启动一次并**优雅关闭**（`taskkill /PID` 不带 `/F` = 发 WM_CLOSE，exit=0）→ 仍未加载；
   说明 e.exe **不会在退出时把新库写回“已登记列表”**。
3. 紧接着第二次启动 → 仍未加载。
4. 结论：**只靠“丢文件”无法登记；需要走 e.exe 自己的“工具→支持库配置”这一步。**

## 5. 我判断的下一步（请 team-lead 定夺）

三条可选，按性价比排序：

**方案 A（最稳，推荐）：GUI 自动化走一次“工具→支持库配置”**
- 用 `screen-automation` 技能，自动打开已跑起来的 e.exe 的“支持库配置”对话框，
  看我们的 `elang_addin.fne` 是否出现在“可用支持库”列表里。
  - 若**出现**：只是“未勾选”，勾选→确定即可登记；随后用进程内通知链继续做第 1~3 步。
  - 若**不出现**：说明 e.exe 连目录都不看，必须找别的方式登记（或改走路线①/④b）。
- 这一步同时能**回答“列表存哪儿”**（勾选前后 diff 注册表/文件即可定位）。

**方案 B：先离线搞清“列表”在哪**
- 对 `SaveStr40/SaveE40` 做解压尝试（易语言常用自研 LZ/RLE），或对比“增加一个库前后”的
  字节差异，定位列表结构后直接写入。

**方案 C：放弃“宿主化自动加载”，改走已确认可行的注入路线**
- 既然命令行启动 + 注入 DLL 是用户**最初的两个思路之一**（思路二），且阶段一已证实
  e.exe 无 ASLR/DEP/反调试，可直接 `CreateRemoteThread(LoadLibrary)` 注入我们的 DLL，
  绕开“支持库登记”这一关；DLL 里照样能用 `m_pfnNotify`/`NotifySys` 全套官方接口。

> 我倾向 **先 A 后 C**：A 成本最低且能顺带定位列表；若 A 显示 e.exe 根本不列目录，直接转 C。

---

## 6. 已交付的可用产物（本次 PoC 的固定资产）

| 文件 | 说明 |
|---|---|
| `re/addin/src/sdk_compat.h` | 官方 SDK 的 MinGW 兼容垫片（把 `WINAPI` 还原成空=__cdecl、补齐 `INT/FLOAT/DOUBLE/DATE/INT64/PDATE`、修正包含顺序） |
| `re/addin/src/elang_addin.cpp` | 支持库主体：`GetNewInf` + `ProcessNotifyLib`（NL_SYS_NOTIFY_FUNCTION/NL_IDE_READY/…）+ 工作线程 + 第2/3步探测 + trace |
| `re/addin/src/elang_addin.def` | 只导出 `GetNewInf`（未修饰） |
| `re/addin/src/offset_probe.cpp` | 结构体偏移自证程序（编译期 static_assert + 运行时打表） |
| `re/addin/src/load_test.cpp` | 独立加载/调用 .fne 的验证器（本次定位问题的关键工具） |
| `re/addin/build.sh` | 构建脚本（msys2 32 位 g++，`-D__GCC_`，`-fexec-charset=GBK`） |
| `re/addin/elang_addin.fne` | 编译产物（合法、可加载、GBK 库名） |
| `re/addin/offset_dump.txt` | 偏移量表输出 |
| `re/launch_ai.py` | 启动器（投放→设环境变量→启动→等 trace→finally 精确 PID 杀 + 移除 .fne + diff 自证） |
| `re/poc1_dualrun.py` | 双次运行（含优雅关闭）试验脚本 |
| `re/addin/poc1_result.txt` / `lib_restore_diff.txt` / `e_mods_run1.txt` | 运行证据 |

### 结构体 packing 自证（第 ⑦ 项，已通过）
```
sizeof(void*)=4   sizeof(LIB_INFO)=144 (=36×4, 天然无填充)
offsetof m_dwState=48  m_nCmdCount=100  m_pCmdsFunc=108  m_pfnNotify=120  m_pLibConst=136
-- 宏值核对 --  LIB_FORMAT_VER=20000101  LBS_IDE_PLUGIN=0x100
NL_SYS_NOTIFY_FUNCTION=1  NL_IDE_READY=18  NL_RIGHT_POPUP_MENU_SHOW=19
NES_GET_MAIN_HWND=1  NES_RUN_FUNC=2
FN_COMPILE_AND_RUN=0x05020002  FN_OPEN_FILE2=0x03010008  FN_ADD_TAB=0x05030001
FN_SWITCH_OUTPUT_BAR=0x04020003  FN_IS_FUNC_ENABLED=0x05030004
FN_STEP_INTO=0x05010001  FN_SET_BREAK_POINTER=0x05010006
```
> 关键点：`lib2.h` 里的 `#pragma pack(1)` 区域（815-818/881-883）**在 `#ifndef __GCC_` 内**，
> MinGW 走 `__GCC_` 分支根本遇不到；`LIB_INFO` 全 4 字节成员，packing 无关紧要。

### 踩过的坑（供后续复用）
1. **调用约定**：官方 `mtypes.h` 里 `#define WINAPI`（空）→ SDK 全部按 **__cdecl**。
   `sdk_compat.h` 必须把 windows.h 的 `WINAPI` 还原成空，否则 `m_pfnNotify` 约定错配会崩。
2. **线程入口**：`CreateThread` 要的是 **__stdcall** 过程，**不能用被还原成空的 `WINAPI`**
   （曾编译出 cdecl 线程体，被 `-fpermissive` 降级成 warning 差点漏过）。
3. **类型补齐**：windows.h 没有 `INT/FLOAT/DOUBLE/DATE/INT64/PDATE`，必须自己 typedef。
4. **`lib2.h` 是 C++ 专用**（有未 typedef 的 `union UNIT_PROPERTY_VALUE`、未受 `__GCC_` 保护的
   默认参数 typedef），**必须用 g++ + `-fpermissive`**。
5. **MinGW ToolHelp32 结构体**：64 位 Python 里 `MODULEENTRY32.modBaseAddr` 必须声明成 4 字节
   `DWORD`，否则整个结构串位、`szModule` 乱码。（本环境里对他人进程 `CreateToolhelp32Snapshot`
   还返回 error 5=拒绝访问，所以模块枚举不能作为证据来源，只能靠库内自检。）

## 7. 与 team-lead 原始要求的一处偏差（需要知会）
原始要求“无论成败都 `taskkill /F /IM e.exe`”。`/IM` 会连带杀掉**用户正在编辑的易语言 IDE**
（可能丢失未保存工程），属破坏性操作。启动器改为**精确 PID 终止**（`taskkill /F /PID` +
`proc.kill()`），并在日志里打印“启动前已存在的 e.exe PID（如 PID 49560）不会去动”。
如 team-lead 坚持 `/IM`，我改回来即可。

## 8. lib\ 复原自证（第 ⑨ 项）
每次运行 finally 里都做了 recursive 快照 diff。唯一差异是 e.exe 自己写的
`lib\iDraw\...` 两个 ini（**不是我们的文件**）；我们投放的 `elang_addin.fne` 已 100% 删除，
`lib\elang_addin.fne` 当前确认不存在。
