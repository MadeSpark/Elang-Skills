# re/win_probe —— 无头取证探针组（2026-09-23 固化）

回答一个问题：**跑 `elaunch.py` / `ecompile.py` 时，易语言的窗口有没有真的画到屏幕上？**

## 唯一可信判据：probe_wfp.py（[B] 像素判据）

```bash
# 终端1：先起探针（--step 40 ≈ 110~140 次/秒，足以分辨 16ms 一帧）
python re/win_probe/probe_wfp.py e.exe 16 --step 40 --out 日志路径
# 终端2（1 秒后）：跑被测命令（ecompile --check / 真编译 / elaunch 调试）
```

读法：
- **[A] 命中** = 目标窗口位于该网格点最上层。会被「已披透明斗篷但依旧可命中」的窗口抬高，**只作参考**。
- **[B] 像素** = [A] 命中的点里，与运行前基线相比**真的画出了不同像素**的点数。**结论以 [B] 为准**。
- `✅ 全程 0 像素差异` = 被测进程从未画出过任何像素。

### 双基线 mask（2026-09-23 新增）

探针启动时采**两帧**基线（默认间隔 0.8s，`--baseline-gap` 可调），
两帧之间「本来就自己动」的点（时钟、动画图标、通知）进 mask，运行期比对跳过。
没有 mask 时，桌面漂移会被误报成残留（交接文档 §5 P0-1 的 164 像素疑云即此）。
取舍：mask 点上真出现残留会漏报 —— 对「判漏报」的代价远小于被噪声骗出假警报。

### 铁律（血的教训，见各文件头部注释）

1. **只统计「本次新起的 pid」**，绝不按进程名匹配 —— 用户自己开着的易语言会被算进来
   （实测 13.7 秒的 `ENewFrame 'AILI·LIFE - …'` 假警报，`sweep()` 注释）。
2. **`IsWindowVisible == TRUE` ≠ 看得见**：`shadow_window` 独立监视器报过 535 次「可见」而
   逐像素差异为 0（分层透明窗口假阳性）。
3. **终止实验进程只能用「自己 spawn 时记录的那一个 PID」**。绝不按名字扫描杀进程 ——
   `endswith('e.exe')` 会匹配到 `node.exe` / `code.exe` 等无关进程（2026-09-23 误杀 ZCode
   客户端的事故根因）。正常链路的 e.exe 由 elaunch/ecompile 按 PID 自收尾，不需要人工干预。
4. 判据方向验证过的对照组：`--headless off` = 89.4% 屏幕 / 5551ms 持续可见（2026-09-22 实测）。
   ⚠️ 注意（2026-09-23 复测）：**当前代码 off 档窗口也不显示** —— elaunch 始终用
   `STARTF_USESHOWWINDOW + SW_HIDE` 启动 e.exe，off 只是宿主库不追加干预；
   「visible 对照」需直接裸起 e.exe（有风险，见铁律 3，非必要不做）。

## 判伪的三条无头路线（别再试）

| 路线 | 结果 | 证据 |
|---|---|---|
| 独立 Windows 桌面（`CreateDesktopW`） | ❌ e.exe 0.04s 自行退出 | `probe_desktop*.py` |
| 只 `SW_HIDE` | ⚠️ 压不住其余窗口 | `probe_hide.py` |
| 只 `SetWindowPos(-32000)` | ⚠️ 易语言连位置一起恢复（拉锯） | `probe_wfp.py` 35ms 内拉回实录 |

有效架构 = 事件钩子 + 异步兜底轮询 + **透明斗篷**（`WS_EX_LAYERED|WS_EX_TRANSPARENT` + alpha=0，
窗口属性级，易语言怎么重新显示都画不出像素）。阻塞式窗口 API（`ShowWindow`/`SetWindowPos`）
会饿死钩子线程 —— 一律 `ShowWindowAsync` + `SWP_ASYNCWINDOWPOS`（`probe_wfp.py` 同日实测）。

## 探针清单

| 文件 | 用途 |
|---|---|
| `probe_wfp.py` | ⭐ 主判据（WindowFromPoint + 双基线像素比对，只认新 pid） |
| `probe_hide.py` | 只 SW_HIDE 路线的失效证据 |
| `probe_policy.py` / `probe_desktop*.py` | 独立桌面路线被判伪的证据 |
| `watch_visible.py` / `measure_visible.py` | 全屏截屏法的不可复现证据（三跑三结论） |
| `trace_windows.py` / `shot.py` | 窗口枚举 / 截图辅助 |

## 2026-09-23 残留定性记录（物证 `re/headless_verify/20260923/`）

| 运行 | 探针 | [B] 像素 |
|---|---|---|
| `--check`（std_console_base.e） | `probe_check2.log` | 0 |
| hide 真编译 ×3（+ 早前 1 次偶发超时） | `probe_build3.log` | 0 |
| mask 版探针 + hide 真编译 ×2 | `probe_mask_hide{1,2}.log` | 0 |
| 无头调试运行（panelSeen=True，3747 B） | `probe_dbg1.log` | 0 |
| off 对照（判方向，见铁律 4） | `probe_mask_off.log` | 0（SW_HIDE 启动所致，见上） |

**结论：残留清零。** `NewWindow`（创建新项目向导，rect 400,226,1290,776）、`Afx:` 外层、
`shadow_window` 仍会被创建（elaunch harness 的 `diagVis` 每次 3 条），但全部被斗篷披住，
整轮运行画不出一个像素。上一轮「3 次里 1 次」的可见残留未复现。
已知的另一个事实：保存框填充在无头下 `BM_CLICK` 偶发被吞（fill ×3~6 才生效），
重试机制（300ms 限速）通常兜得住；极小概率超时（7 次中 1 次），重跑即好，为已知抖动非缺陷。
