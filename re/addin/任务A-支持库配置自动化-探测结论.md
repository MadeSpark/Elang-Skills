# 任务A（程序化「启用」）—— A2 探测结论

> ⚠️ 其中「改 `配置/支持库.config.json` 即可」这一条已被 2026-09-21 受控实验证实（见 `re/verify_libsource.py`）
> —— 即 `t2e` 读「支持库清单」的**唯一有效通道**是工程文本里的 lib 配置文件，
> 而在 `类/*.e.txt` 里写 `.支持库 <key>` **不生效**。本文件第 3 节据此成立（不再属推测）。

> 目标（team-lead 派工 A2）：判定「工具→支持库配置」对话框能否 UI 自动化。
> 只读探测：只读菜单、只 PostMessage/SendMessage(WM_COMMAND)、只枚举控件，**不动任何勾选**。
> 脚本：`re/a2_lib_config_dialog.py`、`a2_lib_config_dialog2.py`、`a2_open_via_keys.py`、
> `a2_probe_ids.py`、`a2_with_project.py`。结束一律精确 PID 杀 + WM_CLOSE。

## 1. 已验证（确定成立）

1. **e.exe 主窗口菜单是标准 `HMENU`**（不是自绘）：
   `GetMenu(ENewFrame)` 非空，17 个顶层项，`GetSubMenu`/`GetMenuString`/`GetMenuItemID` 全部可用。
   顶层：`F.程序 E.编辑 V.查看 I.插入 B.数据库 R.运行 C.编译 **T.工具** W.窗口 H.帮助 | 模块 支持库 静编 便签 词库 更多`。
2. **『T.工具』子菜单（12 项）已完整读出**，其中「支持库」相关三项：
   - `0x808C` = `I.安装新的支持库`
   - `0x8091` = `Y.类型库或OCX组件->支持库`
   - **`0x808A` = `L.支持库配置`**  ← 目标
   - `0x80F5` = `F.在模块及支持库中查找`
3. **`WM_COMMAND` 路由本身是通的**：`PostMessage(hMain, WM_COMMAND, 0x806C /*系统配置*/, 0)` **成功弹出**
   `#32770 '系统配置对话框'`，其 **79 个控件全部是标准 Win32 控件**（Button/Static/Edit/ComboBox，
   带 `GetDlgCtrlID` 数值 id、`GetWindowText` 文本、enabled 状态）——说明 e.exe 的对话框
   **是可枚举、可定位的**（若将来真需要，勾选类控件理论上可自动化）。

## 2. 未成功（= 命中 A2 停止线）

**`0x808A`（支持库配置）无论何种方式都不弹窗**：
| 尝试 | 结果 |
|---|---|
| `PostMessage(hMain, WM_COMMAND, 0x808A, 0)`（无工程） | 无新窗口 |
| 同上（**已打开工程** projD_base.e，标题变为 `… - Windows窗口程序`） | 无新窗口 |
| `SendMessage(hMain, WM_COMMAND, 0x808A, 0)`（带工程） | **立即返回 1（handler 已执行）但不弹窗、不阻塞** |
| 真实键盘助记符 `Alt+T` → `L`（keybd_event，SetForegroundWindow） | 无对话框（仅出现 tooltips 等惰性窗口） |
| `0x808C`（安装新的支持库）同上 | 无 #32770 |

结论：`0x808A` 的命令**确实被派发并返回"已处理"**，但**没有产生可见对话框**（可能：需要前台激活/
需要工程处于特定状态/内部遇到条件而静默返回）。**在当前（非交互、非前台）自动化环境下，
「打开支持库配置」不可靠复现。**

## 3. 关键判断：**A 已不必再攻 —— D 是它的等价程序化途径**

- 「支持库配置」的语义 = *为「当前工程」选择要用哪些支持库*。
- `.e` 工程里这份清单就存在 **`配置/支持库.config.json`**（`{Key,Guid,CmdCount,Name,Version,…}`）。
- **任务D 已实测证明**：只要在该 .e 的 `支持库.config.json` 里加一项
  `{Key:"elang_addin", Guid:"…", …}`，并在 `lib\` 放好 `.fne`，**e.exe 打开该 .e 就会按需加载我们的库**
  （GetNewInf 被调用 + 收到 NL_SYS_NOTIFY_FUNCTION×2 与 NL_IDE_READY），**完全不需要触碰「支持库配置」对话框**。
- 因此：**"程序化启用"的目标已由「改工程内嵌库清单」达成**，A2 的 UI 自动化失败不构成阻塞。

## 4. 给主理人的建议

1. **A 可关闭**（或降级为"已定位菜单 ID=0x808A；UI 自动化不成功；由 D 等价替代"）。
2. 若仍想在真交互环境验证 0x808A：需要 e.exe **前台且用户在场**手动点一次，我方可从旁记录
   `reg_before/reg_after` 差异（这一步交给用户/架构师更合适）。
3. 后续把精力放在 **PoC-2**：用 `NES_RUN_FUNC` 触发 `FN_COMPILE_AND_RUN`(0x05020002, 现 enabled=1)
   并观察调试框（`FN_END_RUN`/状态夹文本/`FN_GET_PRG_TEXT`）——这是用户最关心的"调试运行+监控调试框"。
