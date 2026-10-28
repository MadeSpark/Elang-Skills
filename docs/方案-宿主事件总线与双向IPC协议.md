# 宿主事件总线与双向 IPC 协议（v1）

> 状态：实施中（2026-10-29）。本文是 `src/addin/elang_addin.cpp`（.fne 宿主库）
> 与 `tools/elaunch.py` / `tools/ecompile.py`（Python 桥）之间的**唯一契约**。
> 两端必须严格按本文实现；改协议必须先改本文。

## 1. 角色与拓扑

```
   e.exe 进程                            Python 进程（elaunch.py / ecompile.py）        Agent
┌───────────────────────┐   named pipe   ┌──────────────────────────────┐        ┌────────┐
│ elang_addin.fne       │◄──────────────►│ PipeBridge (server)          │◄──────►│ 模型   │
│  · WinEvent 事件总线   │  JSONL / UTF-8 │  · 读写线程 + 决策回调        │  函数   │        │
│  · 内置兜底策略        │                │  · <tag>_ipc.jsonl 审计      │  调用   │        │
│  · 事件文件（GBK 审计）│                └──────────────────────────────┘        └────────┘
└───────────────────────┘
```

* **fne = 管道客户端**，**Python = 管道服务端**。
* 事件文件（`ELANG_AI_EVENTS`，GBK 逐行）**保留不动**，仅作崩溃后审计；IPC 是主通道而非替代品。
* 非 AI 模式（没有 `ELANG_AI_PIPE` 且没有其它 AI 参数）**零影响**：不建线程、不连管道、不写文件。

## 2. 管道与握手

| 项 | 约定 |
|---|---|
| 名字 | `\\.\pipe\elang_addin.<pid>.<tag>`，`pid` = e.exe 的 PID，`tag` 来自 `ELANG_AI_PIPE_TAG`（默认 `0`） |
| 类型 | `PIPE_ACCESS_DUPLEX` + `PIPE_TYPE_BYTE`，`PIPE_UNLIMITED_INSTANCES`，缓冲区 64 KiB |
| ACL | 默认系统安全描述符（仅当前用户 + SYSTEM）；检测到**受限令牌**（沙箱/低权限容器）时自动放宽为 `D:(A;;GA;;;SY)(A;;GRGW;;;WD)`——受限令牌下默认 DACL 连本进程 CreateFile 自己建的管道都拒（ERROR_ACCESS_DENIED=5），管道名带 48 位随机 tag + 128 位握手令牌兜底；`ELANG_AI_PIPE_SDDL` 可显式覆盖 |
| 启动 | Python 先 `Popen` 拿到 e.exe 的 PID，**紧接着**建管道（fne 侧会重试，来得及）；fne 侧 `CreateFileA` 失败则每 60ms 重试，最多 3000ms，之后每 5000ms 再试 |
| 名字的确定方式 | **两端各自算，谁也不信对方传的字符串**。`ELANG_AI_PIPE` 是**模板**，可含占位符 `{pid}`：fne 打开前把它替换成 `GetCurrentProcessId()`，Python 建管道前替换成 Popen 到的 e.exe PID |
| 环境变量 | `ELANG_AI_PIPE`（管道名模板，设置即启用 IPC，建议写 `\\.\pipe\elang_addin.{pid}.<tag>`）、`ELANG_AI_PIPE_TOKEN`（握手令牌）、`ELANG_AI_PIPE_TAG`（诊断标签）、`ELANG_AI_DLG_TIMEOUT_MS`（决策超时，默认 1500） |
| 空值语义 | `ELANG_AI_PIPE` 为空串或未设置 ⇒ **完全不启用 IPC**（不建线程、不重试连接） |

**握手**：fne 连接后发第一行 `hello`；Python 回 `ack`。`ack.ok=false` 或 2 秒内无 `ack` ⇒ fne 断开并进入「无 IPC」模式（继续用内置策略）。

## 3. 帧格式

* **一行一个 JSON 对象**，以 `\n` 结尾；UTF-8 只用于 ASCII 结构字符。
* **所有可能含中文/路径的文本字段一律存放为字段值的 hex（GBK 原始字节的小写十六进制）**，例如
  `"title":"c8edb6a8"`。**这样两端都不需要处理 JSON 字符串转义**，也不会引入编码歧义。
  空文本写 `""`。
* 数字一律十进制；发送给 Python 的句柄写成十进制整数（Python 只做透传与展示，不做数值运算）。
* 未识别的字段必须忽略（前向兼容）；未识别的 `t` 必须忽略并计数。

## 4. 消息类型

### 4.1 fne → Python

| t | 字段 | 说明 |
|---|---|---|
| `hello` | `token`(hex) `pid`(int) `fw`(hex) | 首帧（`fw` = 宿主库版本，如 `v1.3.9`，同样 hex） |
| `event` | `k`(str) `cls`(hex) `title`(hex) `msg`(hex) | 只镜像 `DLG-MODAL`/`DLG-ERROR` 两种 `k`；窗口创建/销毁的原始总线事件仍只走事件文件（避免高频帧打爆管道） |
| `dialog` | `id`(int) `hwnd`(int) `cls`(hex) `title`(hex) `msg`(hex) `btns`([{i,txt}]) `edits`([{c,cls,txt}]) `pol`(str) `ts`(s) | 需要决策的弹窗；`pol` ∈ `observe`/`dismiss`；`btns[].i` = 控件 id，`edits[].c` = 控件 id（密码框 `txt` 恒为空） |
| `result` | `id`(int) `how`(str) `act`(str) `ms`(int) | 决策执行结果；`how` ∈ `ipc`/`fallback`/`skip`；`act` ∈ `click`/`none`（observe 与「无动作」都记 `none`） |
| `ping` | `ts`(ms) | 每 5 秒一次；15 秒收不到 `pong` 视为断线 |

### 4.2 Python → fne

| t | 字段 | 说明 |
|---|---|---|
| `ack` | `ok`(0/1) `err`(hex, 可选) | 握手应答（`ok=0` ⇒ fne 不再重连，永久退回内置策略） |
| `decide` | `id`(int) `act`(str) `btn`(int, 可选) `edits`({"<ctlid>":"<hex>"}, 可选) `hide`(0/1, 可选) | 决策 |
| `pong` | `ts`(ms) | 心跳应答 |
| `policy` | `dlg`(str) `hide`(str, 可选) | 运行期改策略（`observe`/`dismiss`） |

`act` 取值（**白名单，其它一律忽略并记 `result.how=none`**）：

| act | 行为 |
|---|---|
| `none` | 什么都不做（等同 `observe`） |
| `click` | 点击 `btn` 指定的控件 id（必须出现在该 `dialog` 的 `btns` 里） |
| `fill` | 把 `edits` 写入对应控件（控件 id 必须出现在该 `dialog` 的 `edits` 里），不点击。**当前宿主未实现：收到即按内置兜底并记 `IPC-FALLBACK reason=bad-decision`** |
| `fillclick` | `fill` 之后再 `click btn`。**当前宿主未实现**（同上，兜底）；保留字段以便后续版本加而不改协议 |
| `hide` | 只把窗口挪出屏幕/披斗篷，不交互（与 `click` 互斥；`hide=1` 时宿主按「不动」处理） |

### 4.3 决策时序（关键）

1. fne 发现弹窗 → 写一行 `dialog`（含全部子控件快照）→ 记下 `id`。
2. fne **在决策线程上**等待匹配 `id` 的 `decide`，最长 `ELANG_AI_DLG_TIMEOUT_MS`（默认 1500ms）。
   **绝不允许在 e.exe UI 线程或 WinEvent 回调里等待**。
3. 收到 → 按白名单执行 → 发 `result{how:"ipc"}`。
4. 超时/无 IPC/动作非法 → 走**内置兜底策略**（沿用现有安全规则：单按钮→点它；多按钮有 IDOK→点 IDOK；
   有 IDCANCEL 且无 IDYES→点取消；其余只记录）→ 发 `result{how:"fallback"}`。
5. `hide` 与交互可叠加：`hide=1` 时先移出屏幕再执行动作。

## 5. 线程模型（硬性）

| 线程 | 允许做的事 | 禁止 |
|---|---|---|
| e.exe UI 线程 / WinEvent 回调 | 只入队、置标志、打时间戳 | 任何阻塞调用（`ReadFile`、`WaitForSingleObject`、跨线程同步 `SendMessage`、`EnumWindows`） |
| IPC 线程 | `CreateFile`/`ReadFile`/`WriteFile`、解析、派发 | 触碰窗口、调用 `SetWindowPos` |
| 决策线程（现有 `WorkerProc` 家族） | 执行白名单动作、等待 `decide`（≤超时） | 无限等待 |

跨线程点击优先 `PostMessageA`；必须同步时用 `SendMessageTimeoutA(..., 200, SMTO_ABORTIFHUNG)`。

## 6. 审计与诊断

* Python 侧把两个方向的每一帧写进 `<log_dir>/<tag>_ipc.jsonl`（含 `dir`:`in|out`、`ts`、原始行）。
* `--headless`/诊断 JSON 增加：`ipc` 段，含 `connected`、`framesIn`、`framesOut`、`decisions`、`fallbacks`、`p95Ms`。
* fne 侧事件文件继续写 `DLG-*` 行，新增 `IPC-CONN`/`IPC-DECIDE`/`IPC-FALLBACK` 三类行便于离线复盘。

## 7. 版本演进

* `v` 字段为主版本；只增字段不改语义。收到更高 `v` 时降级为「只用认识的消息」。
* 变更必须同步更新本文与两端实现，并在 `docs\验收报告-*.md` 记录。
