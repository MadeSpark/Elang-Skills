#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""elaunch.py —— 启动易语言 IDE、打开「加壳后」的 .e、触发一次“调试运行”，
并**流式**收集调试框日志，最后打出一份机器可读的结果 JSON。

它把 team-lead 定的**产品约束**落进代码：**不用面板尾行判断结果**（尾行分不出
“正常跑完”和“被中断”），而是用——
  * `crashed`  ← 易语言 IDE 子进程**退出码 ≠ 0**（如 `0xC0000005`）
  * `stopped`  ← **我们主动发过** `FN_END_RUN` 且观测到 `FN_END_RUN enabled 1→0`
  * `timeout`  ← 到 `--timeout` 仍未见到结束跃迁
  * `ok`       ← 自然结束（观测到 `1→0` 且不是我们发的）

用法
====
    python elaunch.py <加壳后的_ai.e>
      --log <path>        调试日志落盘路径（默认 <ai.e 同目录>/<名>.debug.log）
      --timeout <秒>      整体超时（默认 30）
      --stop-after <秒>   运行满这么久就主动发 FN_END_RUN（默认：不主动停）
      --json              把结果 JSON 打到 stdout
      （编译模式：二选一）
      --compile <种类>    真编译：期望产物落盘才算成功
      --check             语法兜底检查：**不要产物**，只回答「源码能不能过编译器的
                          语法/语义静态检查」。判据是「IDE 弹出询问产物路径的保存框」——
                          编译器只有在静态检查全过之后才会来要产物路径（见下）
      --visible           排障用：开着窗口跑（默认无头，窗口会被隐藏并移出屏幕）

      （以下为纪律/调试补充项）
      --fne <path>        要投放的支持库 .fne（默认 ../assets/elang_addin.fne）
      --home <dir>        易语言安装目录（默认 $ELANG_HOME → 注册表 → 报错）
      --tag <name>        证据命名前缀（默认 <名>_<时分秒>）
      -q, --quiet

退出码：0=调试会话正常收尾（ok/stopped）或 `--check` 判定语法通过；1=crashed/timeout/
syntax-error；3=启动器自身失败。

`--check` 为什么要这样判（实测依据）
==================================
易语言编译的第一阶段就是整个工程的语法/语义静态检查；**只有它全过了，编译器才会弹
「请输入欲编译到的可执行文件名：」保存框**。所以 `--check` 的权威判据就是「看到保存框」：

  * 保存框被弹出（日志里 `COMPILE-DIALOG`）⇒ `syntax-ok`，立刻按 PID 收尾
    （语义等于：用户看到保存框时点了「取消」——不产出任何东西）
  * 面板出现 `错误(NNNN): …` ⇒ `syntax-error`，错误原文进结果 JSON 的 `errors`
  * 触发被拒 / IDE 自崩 / 到点两者都没出现 ⇒ `failed` / `timeout`（**不下"通过"结论**）

`--check --compile precompile` 则改走官方预编译入口：
  FN_PRE_COMPILE (FNST_MODIFY|112, = 0x02020070)  「预编译当前程序」
    功能参数1(BOOL*): 指向一个 BOOL 变量的指针, 用作返回是否预编译成功
毫秒级、不弹保存框。**但它实测不可靠**：`FNST_MODIFY` 家族只作用于「当前编辑窗口」，
我们用 `e.exe <工程>` 打开却从不激活代码窗口，调用几乎空转、`BOOL*` 回参**恒为 TRUE**
——对确定有问题的源码也一律返回成功（本机 6 组受控实验，坏样本全被放过）。
留着只为「已激活窗口」的宿主场景，**不要拿它当通过判据**。

这些错误**只有编译器能发现**：`echeck.py` 那种纯文本静态检查只能看格式与块结构配对，
看不了「所有分支是否都返回了值」「类型能不能隐式转换」这种语义问题。例如：

    错误(10022): 子程序“X”具有返回值定义，但实际上却没有返回数据或者并不是所有程序
                 分支都返回了数据。
    错误(10044): 不能将 "整数型" 数据转换到 "逻辑型" 数据

事件通道（2026-10-03 起全链路打通，防「弹窗卡死」）
======================================================
编译/运行期的致命报错（如「无法找到启动子程序"_启动子程序"!」）走的是 **MessageBox**，
输出面板一个字都没有 —— 面板判据全部落空，旧版只能空等到超时。现在：

  * elaunch 给每个会话设置**专属事件文件** `ELANG_AI_EVENTS=<log目录>/<tag>_events.txt`
    与 `ELANG_AI_DLG_POLICY=dismiss`（fne 的 DlgWatch 替用户点掉可安全应答的框）；
  * 等待循环里 tail 事件文件：`DLG-ERROR`（标题含「错误」的致命框）出现 1.5s 内、
    普通模态框 12s 无人应答 → **立刻收尾**，结果 JSON 的 `dialogBlocked` 带回
    `{title, msg, raw}`（msg 就是弹窗原文），`eventsTail` 带事件尾 40 行；
  * `DLG-SAVEBOX`（编译保存框）归填充线程管，绝不据此判失败；
  * 事件行同时落面板日志与 trace 可回看：`DLG-MODAL`（含 msg=）、`DLG-DISMISSED`、
    `DLG-RETRY`（自动应答补点）、`COMPILE-START/END/OUT-SEEN`、`IDE-READY/IDE-EXIT`。

宿主事件总线 + 双向 IPC（2026-10-29 起，协议见 docs\方案-宿主事件总线与双向IPC协议.md）
==========================================================================================
elaunch 是管道**服务端**（`PipeBridge`），fne 是客户端：先 Popen 拿 e.exe 的 PID，立刻按
`\\.\pipe\elang_addin.<pid>.<tag>` 建管道；环境变量注入 `ELANG_AI_PIPE`（含 `{pid}` 占位的模板，
启用开关）/ `ELANG_AI_PIPE_TOKEN` / `ELANG_AI_PIPE_TAG` / `ELANG_AI_DLG_TIMEOUT_MS`。
弹窗由 fne 上报 `dialog`、本进程回 `decide`（默认规则见 `PipeBridge._default_decide`），
两个方向的每一帧写 `<log目录>/<tag>_ipc.jsonl`。窗口隐藏默认交给宿主事件总线（
`ELANG_AI_HIDE_ENGINE=host`，Python 不再周期性 EnumWindows）；`both`/`python` 可回退。
结果 JSON 增加 `ipc` 段。`--no-ipc` 或 `ELANG_AI_IPC=0` 关闭，行为与此前一致。

无头（默认开）
==============
**默认不开可见窗口**：`e.exe` 以 `STARTUPINFO.wShowWindow = SW_HIDE` 启动（压住主窗口），
同时一个后台线程以 2~12ms 轮询把它的**所有可见顶层窗口** `SW_HIDE` 并挪到 `(-32000,-32000)`。

* 排障要看界面时加 `--visible`（= 旧行为，窗口正常显示）。
* **不要用「独立 Windows 桌面」**：实测 `e.exe` 在非默认桌面上 **0.04 秒就自行退出**。
  另注意 Python 的 `subprocess.STARTUPINFO` **不暴露 `lpDesktop`**，设了也没人读。
* 宿主库侧另有 `ELANG_AI_HIDE`（默认 1）做同样的事，两层互为兜底。详见 `WindowHider`。

纪律（team-lead 要求，勿退化）
==============================
  * 只用**精确 PID** 结束 e.exe（绝不 `taskkill /F /IM`）
  * `finally` 里清理投放的 `.fne`，并对易语言安装目录做 before/after `diff` 自证
  * 易语言安装目录**默认只读**（只写/删一个 `lib\elang_addin.fne`）
  * 日志里的中文同时给 `.utf8.txt` 视图（面板文本是 GBK）
  * `--tag` 唯一命名证据
  * 启动器自身的 stdout/stderr **tee** 到 `<TAG>_harness.txt`
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import secrets
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_FNE = os.path.normpath(os.path.join(HERE, os.pardir, "assets", "elang_addin.fne"))

REG_PATH = r"Software\FlySky\E\Install"
REG_VALUE = "Path"

# 支持库在 lib\ 下的文件名（= 技能加壳用的 Key）
LIB_FNAME = "elang_addin.fne"


# --------------------------------------------------------------------------- #
# 路径解析（**不出现任何本机绝对路径**）
# --------------------------------------------------------------------------- #
def _install_root(d: str):
    """把候选目录规范化成「含 e.exe 的安装根」。

    易语言注册表 `HKCU\\Software\\FlySky\\E\\Install\\Path` 实测存的是**lib 目录**
    （如 `…\\e\\lib\\`），不是安装根；用户设 `ELANG_HOME` 时也可能指到 lib。
    这里统一兜底：本身含 `e.exe` 就用它；否则若它是 `lib` 且其父目录含 `e.exe`，
    就用父目录。
    """
    d = os.path.abspath(d)
    if os.path.isfile(os.path.join(d, "e.exe")):
        return d
    stripped = d.rstrip("\\/")
    if os.path.basename(stripped).lower() == "lib":
        parent = os.path.dirname(stripped)
        if os.path.isfile(os.path.join(parent, "e.exe")):
            return parent
    return d


def resolve_home(explicit: str = None) -> str:
    """按 ① 参数 → ② 环境变量 ELANG_HOME → ③ 注册表 → ④ 报错 的顺序定位易语言目录。"""
    tried = []

    def consider(cand):
        if not cand:
            return None
        r = _install_root(cand)
        tried.append(r)
        return r if os.path.isfile(os.path.join(r, "e.exe")) else None

    r = consider(explicit)
    if r:
        return r

    r = consider(os.environ.get("ELANG_HOME"))
    if r:
        return r

    if sys.platform == "win32":
        try:
            import winreg  # noqa: WPS433 (仅 Windows 有)
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, REG_PATH) as k:
                val, _ = winreg.QueryValueEx(k, REG_VALUE)
            r = consider(val)
            if r:
                return r
        except Exception:                                          # noqa: BLE001
            pass

    raise SystemExit(
        "找不到易语言安装目录（应含 e.exe 与 lib\\）。请用 --home 或设环境变量 "
        "ELANG_HOME=<易语言安装目录>。已尝试: %s" % (tried or "无"))


def md5f(p: str) -> str:
    h = hashlib.md5()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(65536), b""):
            h.update(c)
    return h.hexdigest()


def toplist(d: str):
    try:
        return set(os.listdir(d))
    except OSError:
        return set()


# --------------------------------------------------------------------------- #
# Tee：把启动器自身的输出同时写进 <TAG>_harness.txt
# --------------------------------------------------------------------------- #
class Tee:
    """进程内所有 print 的去向。

    * **stderr**：给人看的进度/诊断（这样 `--json` 时 **stdout 保持干净**，只放结果 JSON）。
    * **<TAG>_harness.txt**：完整留证（含 e.exe 自己的 stdout/stderr，另开句柄追加）。
    """

    def __init__(self, path: str):
        self.path = path
        self.fh = open(path, "wb")

    def write(self, s):
        try:
            sys.__stderr__.write(s)
            sys.__stderr__.flush()
        except Exception:                                          # noqa: BLE001
            pass
        if self.fh:
            self.fh.write(s.encode("utf-8", errors="replace"))
            self.fh.flush()

    def flush(self):
        try:
            sys.__stderr__.flush()
        except Exception:                                          # noqa: BLE001
            pass
        if self.fh:
            self.fh.flush()

    def close(self):
        if self.fh:
            try:
                self.fh.close()
            finally:
                self.fh = None


def read_text_best(p: str) -> str:
    if not os.path.isfile(p):
        return ""
    return open(p, "rb").read().decode("gbk", errors="replace")


def write_utf8_view(p: str) -> str:
    """把（可能是 GBK 的）文件转出 UTF-8 伴生视图；返回视图路径。"""
    u8 = os.path.splitext(p)[0] + ".utf8.txt"
    open(u8, "wb").write(open(p, "rb").read().decode("gbk", errors="replace").encode("utf-8"))
    return u8


# 面板增量包装行：`[HH:MM:SS.mmm][PANEL-DELTA] DELTA: <面板文本的一行>`
DLINE_RE = re.compile(r"^\[\d{2}:\d{2}:\d{2}(?:\.\d+)?\]\[PANEL-[A-Z]+\]\s*DELTA:\s?(.*)$")
# 易语言编译器报错行：`错误(10022): …`
COMPILE_ERR_RE = re.compile(r"错误\s*\(\s*\d+\s*\)\s*[:：]")
# 宿主库预编译返回：日志里形如 `[21:22:49.871][COMPILE-PRE-RET] handled=1 ok=1`
#   —— ⚠️ 标签后面**有 `]`**（CapWrite 的格式是 `[标签] 值`），正则必须容忍它，
#      否则永远匹配不上、--check 一路空等到超时（踩过）。
PRE_RET_RE = re.compile(r"COMPILE-PRE-RET\]\s*handled=(\d+)\s+ok=(\d+)")
# 触发被拒的事件行（同上，注意 `]`）
TRIGGER_REFUSED = "COMPILE-TRIGGER-RET] handled=0"
# 保存框出现的事件行
DIALOG_SEEN = "COMPILE-DIALOG]"

# 宿主库事件文件（ELANG_AI_EVENTS）里的行：`[HH:MM:SS.mmm] KIND k=v …`
# DLG-MODAL 自 2026-10-03 起带 `msg=`（弹窗正文，如「无法找到启动子程序…」）
EV_DLG_MODAL = re.compile(
    r"\]\s*DLG-MODAL\s+hwnd=(0x[0-9A-Fa-f]+)\s+title='([^']*)'\s+btns=(\d+)\s+msg='([^']*)'")
EV_DLG_ERROR = re.compile(
    r"\]\s*DLG-ERROR\s+hwnd=(0x[0-9A-Fa-f]+)\s+title='([^']*)'\s+msg='([^']*)'")
EV_DLG_DISMISSED = re.compile(r"\]\s*DLG-DISMISSED\s+hwnd=(0x[0-9A-Fa-f]+)")
EV_DLG_SAVEBOX = re.compile(r"\]\s*DLG-SAVEBOX\s+hwnd=(0x[0-9A-Fa-f]+)")
EV_IDE_EXIT = re.compile(r"\]\s*IDE-EXIT\b")


class EventWatch:
    """增量 tail 宿主库事件文件（fne 以 GBK 逐行追加），解析弹窗与里程碑事件。

    为什么必须有它：编译/运行期的致命报错（如「无法找到启动子程序」）走的是
    **MessageBox**，输出面板一个字都没有 —— 面板判据全部落空，AI 只能空等到超时。
    宿主库的 DlgWatch 线程会把每个模态框（标题+正文+按钮）写进事件文件；
    本类把它变成「秒级可读」的信号源：

      * DLG-ERROR   → 致命弹窗（标题含「错误/Error」），宽限期后判定会话失败；
      * DLG-MODAL   → 任何模态框都记录；非保存框若长期无人应答（>DLG_STUCK_SEC）
                      也按阻塞处理（fne 的 dismiss 策略应答不了「是/否问句」，
                      这种框本来就该交还给人/AI 判断，但必须立刻报告，不能等超时）；
      * DLG-DISMISSED → fne 已替用户点掉该框，解除阻塞候选。
    保存框（DLG-SAVEBOX）由编译填充线程负责，绝不据此判失败。
    """

    DLG_STUCK_SEC = 12.0        # 非错误弹窗无人应答多少秒 → 视为阻塞
    ERR_GRACE_SEC = 1.5         # 错误弹窗出现后给 fne 多少秒完成自动应答
    TAIL_MAX = 40               # 结果 JSON 里带的 eventsTail 行数

    def __init__(self, path: str):
        self.path = path
        self.pos = 0
        self.raw_tail = []          # 全部事件行（有界，供 eventsTail）
        self.dialogs = []           # DLG-MODAL/DLG-ERROR 摘要（有界）
        self.errors = []            # DLG-ERROR（致命错误弹窗）——出现即失败信号
        self.pending = {}           # hwnd → dict(t=time, raw=..., title=..., msg=..., error=bool)
        self.dismissed = set()      # 已被 fne 应答过的 hwnd
        self.ide_exit = False

    def pump(self) -> list:
        """读入新事件行，更新内部状态；返回本次新解析到的事件 dict 列表。"""
        new_events = []
        if not os.path.isfile(self.path):
            return new_events
        try:
            with open(self.path, "rb") as f:
                f.seek(self.pos)
                data = f.read()
                self.pos = f.tell()
        except OSError:
            return new_events
        if not data:
            return new_events
        for ln in data.decode("gbk", errors="replace").splitlines():
            ln = ln.rstrip()
            if not ln:
                continue
            self.raw_tail.append(ln)
            if len(self.raw_tail) > 400:
                self.raw_tail = self.raw_tail[-200:]
            m = EV_DLG_ERROR.search(ln)
            if m:
                ev = {"kind": "DLG-ERROR", "hwnd": m.group(1), "title": m.group(2),
                      "msg": m.group(3), "raw": ln, "t": time.time()}
                self.dialogs.append(ev)
                self.errors.append(ev)
                self.pending.setdefault(m.group(1), dict(ev))
                new_events.append(ev)
                continue
            m = EV_DLG_MODAL.search(ln)
            if m:
                ev = {"kind": "DLG-MODAL", "hwnd": m.group(1), "title": m.group(2),
                      "btns": int(m.group(3)), "msg": m.group(4), "raw": ln,
                      "t": time.time()}
                self.dialogs.append(ev)
                if len(self.dialogs) > 40:
                    self.dialogs = self.dialogs[-20:]
                self.pending.setdefault(m.group(1), dict(ev))
                new_events.append(ev)
                continue
            m = EV_DLG_DISMISSED.search(ln)
            if m:
                self.dismissed.add(m.group(1))
                self.pending.pop(m.group(1), None)
                continue
            if EV_DLG_SAVEBOX.search(ln):
                continue
            if EV_IDE_EXIT.search(ln):
                self.ide_exit = True
                continue
        return new_events

    def blocked_dialog(self) -> dict | None:
        """返回当前「阻塞会话」的弹窗（致命错误框优先，其次长期无人应答的框）。

        判定口径：
          * DLG-ERROR 出现 DLG_ERR_GRACE_SEC 秒后仍未消失（没等到 DLG-DISMISSED）
            → 致命（错误框点掉后编译/运行也必然没产出了）；
          * 普通模态框 DLG_STUCK_SEC 秒无人应答 → 阻塞（等于 AI 在替人扛着一个
            语义不明的问句干等）。
          * 已被 fne 应答（DLG-DISMISSED）的框不算阻塞。
        """
        now = time.time()
        for hwnd, ev in list(self.pending.items()):
            if hwnd in self.dismissed:
                self.pending.pop(hwnd, None)
                continue
            grace = self.ERR_GRACE_SEC if ev.get("kind") == "DLG-ERROR" else self.DLG_STUCK_SEC
            if now - ev["t"] >= grace:
                return {"title": ev.get("title") or "", "msg": ev.get("msg") or "",
                        "kind": ev.get("kind"), "raw": ev.get("raw") or ""}
        return None

    def first_error(self) -> dict | None:
        """第一条 DLG-ERROR（致命错误弹窗）。

        ⚠️ 与「是否已被 fne 应答」无关：错误框一出现，编译/运行就已经中止
        （dismiss 只是替用户把模态框点掉、让 IDE 不卡死），会话必然没有产出
        —— 所以这是**失败信号**，等待循环见到就该带原文收尾，不能等超时。
        """
        return self.errors[0] if self.errors else None

    def tail(self) -> list:
        return self.raw_tail[-self.TAIL_MAX:]



def panel_payload_lines(text: str) -> list:
    """把日志还原成「面板原文行」：剥掉 `[..][PANEL-DELTA] DELTA: ` 包装。

    包装行里嵌的是**同一行面板文本**（实测：单行输出程序的业务行只出现在包装行里），
    所以剥掉前缀即可还原；独立行原样保留。
    """
    out = []
    for ln in text.splitlines():
        m = DLINE_RE.match(ln)
        out.append(m.group(1) if m else ln)
    return out


def extract_compile_errors(text: str) -> list:
    """从面板日志里抽出编译器报错行（去重、保序）。"""
    errs = []
    for ln in panel_payload_lines(text):
        s = ln.strip()
        if s and COMPILE_ERR_RE.search(s) and s not in errs:
            errs.append(s)
    return errs


def _os_remove(p: str) -> bool:
    try:
        os.remove(p)
        return True
    except BaseException:                                          # noqa: BLE001
        return False


def _win32_delete(p: str) -> bool:
    """Windows 上直接走 Win32 `DeleteFileW`（当 `os.remove` 因占用/拦截失败时的兜底）。"""
    try:
        import ctypes
        return bool(ctypes.windll.kernel32.DeleteFileW(ctypes.c_wchar_p(os.path.abspath(p))))
    except BaseException:                                          # noqa: BLE001
        return False


def safe_remove(p: str) -> bool:
    """尽力删除一个文件；**绝不抛**。

    收尾清理失败（文件被占用 / 被环境的「批量删除」保护拦下）**不能**把一次成功的
    调试运行变成失败 —— 所以这里吞掉一切异常（含 SystemExit），只回报最终是否已删除。
    先试 `os.remove`，仍在则退到 Win32 `DeleteFileW`。
    """
    if not os.path.exists(p):
        return True
    _os_remove(p)
    if os.path.exists(p) and sys.platform == "win32":
        _win32_delete(p)
    return not os.path.exists(p)


# --------------------------------------------------------------------------- #
# 无头：把 IDE 的窗口藏起来 + 移出屏幕（不需要管理员权限，也不改 IDE 任何配置）
# --------------------------------------------------------------------------- #
_U32 = None


def _user32():
    """缓存 user32 句柄（每个 Win32 调用都现取一次会明显拖慢事件回调）。"""
    global _U32
    if _U32 is None:
        import ctypes
        _U32 = ctypes.WinDLL("user32", use_last_error=True)
    return _U32


def _cls_name(u32, hwnd):
    import ctypes
    buf = ctypes.create_string_buffer(128)
    u32.GetClassNameA(hwnd, buf, 128)
    return buf.value.decode("gbk", "replace")


def _rect(u32, hwnd):
    import ctypes
    import ctypes.wintypes as wt

    class _R(ctypes.Structure):
        _fields_ = [("left", wt.LONG), ("top", wt.LONG),
                    ("right", wt.LONG), ("bottom", wt.LONG)]

    r = _R()
    u32.GetWindowRect(hwnd, ctypes.byref(r))
    return (r.left, r.top, r.right, r.bottom)


# --------------------------------------------------------------------------- #
# 宿主事件总线 + 双向 IPC（协议见 docs\方案-宿主事件总线与双向IPC协议.md，两端唯一契约）
# --------------------------------------------------------------------------- #
def hexg(text) -> str:
    """文本 → GBK 原始字节的小写十六进制（协议里所有中文/路径字段都这样存）。

    GBK 编不了的字符用 errors="replace"（变 "?"），绝不抛。"""
    if text is None:
        return ""
    return str(text).encode("gbk", errors="replace").hex()


def unhexg(s) -> str:
    """hexg 的逆运算：GBK 十六进制 → 文本。非法十六进制返回空串（来自对端的数据不可信）。"""
    if not s:
        return ""
    try:
        return bytes.fromhex(str(s)).decode("gbk", errors="replace")
    except ValueError:
        return ""


_PIPE_API = None


def _pipe_api():
    """惰性加载 kernel32 管道 API（ctypes，**不依赖 pywin32**；本机 Python 也没装它）。

    只在真要建管道时才 import ctypes/wintypes，非 IPC 路径零开销。
    返回 (kernel32, OVERLAPPED 结构类)。"""
    global _PIPE_API
    if _PIPE_API is None:
        import ctypes
        import ctypes.wintypes as wt

        class OVERLAPPED(ctypes.Structure):
            _fields_ = [("Internal", ctypes.c_size_t), ("InternalHigh", ctypes.c_size_t),
                        ("Offset", wt.DWORD), ("OffsetHigh", wt.DWORD),
                        ("hEvent", wt.HANDLE)]

        k = ctypes.WinDLL("kernel32", use_last_error=True)
        vp = ctypes.c_void_p
        dw = wt.DWORD
        pdw = ctypes.POINTER(wt.DWORD)

        def sig(name, res, *args):
            f = getattr(k, name)
            f.restype = res
            f.argtypes = list(args)

        sig("CreateNamedPipeW", wt.HANDLE, wt.LPCWSTR, dw, dw, dw, dw, dw, dw, vp)
        sig("ConnectNamedPipe", wt.BOOL, wt.HANDLE, vp)
        sig("DisconnectNamedPipe", wt.BOOL, wt.HANDLE)
        sig("ReadFile", wt.BOOL, wt.HANDLE, vp, dw, pdw, vp)
        sig("WriteFile", wt.BOOL, wt.HANDLE, vp, dw, pdw, vp)
        sig("CreateEventW", wt.HANDLE, vp, wt.BOOL, wt.BOOL, wt.LPCWSTR)
        sig("SetEvent", wt.BOOL, wt.HANDLE)
        sig("ResetEvent", wt.BOOL, wt.HANDLE)
        sig("CloseHandle", wt.BOOL, wt.HANDLE)
        sig("WaitForSingleObject", dw, wt.HANDLE, dw)
        sig("WaitForMultipleObjects", dw, dw, ctypes.POINTER(wt.HANDLE), wt.BOOL, dw)
        sig("GetOverlappedResult", wt.BOOL, wt.HANDLE, vp, pdw, wt.BOOL)
        sig("CancelIoEx", wt.BOOL, wt.HANDLE, vp)
        _PIPE_API = (k, OVERLAPPED)
    return _PIPE_API


# 受限令牌下用的管道 ACL：SYSTEM 全权 + Everyone 读写（不含 ACL 写权限，改不了 DACL）
RESTRICTED_PIPE_SDDL = "D:(A;;GA;;;SY)(A;;GRGW;;;WD)"


def _token_restricted():
    """当前进程是否跑在**受限令牌**下（沙箱 / 低权限容器）。

    受限令牌做访问检查时要求「正常令牌 + 受限 SID 列表」同时通过；系统为新建对象配的
    默认 DACL 里没有受限 SID，于是连自己进程 CreateFile 自己的命名管道都会
    ERROR_ACCESS_DENIED(=5)。检测到就自动放宽 ACL，而不是让用户手工设 SDDL。
    """
    try:
        import ctypes
        import ctypes.wintypes as wt
        k = ctypes.WinDLL("kernel32", use_last_error=True)
        adv = ctypes.WinDLL("advapi32", use_last_error=True)
        k.GetCurrentProcess.restype = wt.HANDLE
        adv.OpenProcessToken.argtypes = [wt.HANDLE, wt.DWORD, ctypes.POINTER(wt.HANDLE)]
        adv.IsTokenRestricted.argtypes = [wt.HANDLE]
        tok = wt.HANDLE()
        if not adv.OpenProcessToken(k.GetCurrentProcess(), 0x0008, ctypes.byref(tok)):
            return False                                  # TOKEN_QUERY
        try:
            return bool(adv.IsTokenRestricted(tok))
        finally:
            k.CloseHandle(tok)
    except BaseException:                                 # noqa: BLE001
        return False


class PipeBridge(object):
    """IPC 管道服务端（Python 侧）。fne 是客户端，本类是服务端：

      * `start()` 起一个 daemon 读线程：CreateNamedPipe → ConnectNamedPipe →
        握手（hello→ack）→ 逐行读 JSONL；断线后回到等待连接（fne 每 5 秒会重连）。
      * 收 `dialog` 就在读线程里立刻 `_decide` 并回 `decide` 行（默认规则见
        `_default_decide`；可传 decider 回调覆盖，回调必须很快——fne 只等 timeout_ms）；
        收 result / event / pong / policy 只计数与审计；收 ping 回 pong。
      * 所有收发的每一帧写 `<log_dir>/<tag>_ipc.jsonl`（`{ts,dir,raw}`，UTF-8）。

    ⚠️ 必须用**重叠 IO**：同步句柄上 ReadFile 阻塞时，别的线程的 WriteFile 会被 IO 管理器
    串行化排在后面 ⇒ 读线程等 fne 开口、发送线程等读线程 ⇒ 死锁。重叠 IO + 一个「停止事件」
    也让 close() 能立刻唤醒阻塞中的 Connect/Read，e.exe 退出后不会卡住 Python 进程。
    """

    ACK_V = 1                  # 本端协议主版本
    LAT_MAX = 2000             # 决策延迟样本上限（有界，供算 p95）
    LINE_MAX = 1 << 20         # 单行上限 1 MiB，超了丢弃缓冲（防对端乱发）
    AUDIT_MAX = 32 << 20       # 审计文件上限 32 MiB，超了停写（防事件风暴塞满磁盘）
    ACTS = ("none", "click", "fill", "fillclick", "hide")    # 协议白名单
    IDOK, IDCANCEL, IDYES = 1, 2, 6

    def __init__(self, pipe_path: str, token: str, log_path: str = None,
                 timeout_ms: int = 1500, decider=None, sddl: str = None):
        import collections
        self.pipe_path = pipe_path
        # 管道 ACL。默认 None = 系统默认安全描述符（仅本用户/系统可访问）。
        # 受限令牌（沙箱、低权限容器）下默认 DACL 里没有受限 SID，**连本进程自己**去
        # CreateFile 自己建的管道都会被拒（实测 ERROR_ACCESS_DENIED=5）⇒ 这时自动放宽到
        # Everyone（管道名带 48 位随机 tag、握手令牌 128 位，仍只有拿到令牌的一方能发帧）。
        # ELANG_AI_PIPE_SDDL 可显式覆盖（见 elaunch()）。
        self.sddl = sddl
        if self.sddl is None and _token_restricted():
            self.sddl = RESTRICTED_PIPE_SDDL
        self.token = token
        self.log_path = log_path
        self.timeout_ms = int(timeout_ms)
        self._decider = decider                  # 可选：dialog(dict) → decision(dict)|None
        self.error = None                        # 建管道/线程异常（诊断）
        self.audit_error = None                  # 审计文件打不开的原因（不影响 IPC）
        self.peer_pid = None
        self.peer_fw = ""
        self.frames_in = 0
        self.frames_out = 0
        self.decisions = 0                       # 我们发出的 decide 数
        self.fallbacks = 0                       # fne 回报 result.how=fallback 的次数
        self.latency = collections.deque(maxlen=self.LAT_MAX)    # 毫秒，有界
        self.dialogs_in = 0
        self.bad_frames = 0                      # 非法 JSON / 超长行
        self.unknown_frames = 0                  # 未识别的 t（按协议忽略并计数）
        self.ignored_pre_hello = 0
        self.handshake_fail = 0
        self.events_in = {}                      # event.k → 次数（键数有界）
        self.results_in = {}                     # result.how → 次数
        self.recent = []                         # 最近的弹窗决策摘要（有界，诊断）
        self.ever_connected = False
        self._connected_at = None
        self._hello_ok = False
        self._conn = False                       # 管道上当前有客户端
        self._closing = False
        self._t = None
        self._h = None                           # 管道句柄（读线程创建/关闭）
        self._stop_h = None                      # Win32 事件：close() 用它唤醒阻塞 IO
        self._ev_w = None
        self._ov_w = None
        self._created = threading.Event()        # 管道已建好（或建失败）
        self._ready = threading.Event()          # 握手完成
        self._lk = threading.Lock()              # 统计
        self._wlock = threading.Lock()           # 写 + 句柄/连接状态
        self._alk = threading.Lock()             # 审计文件
        self._audit_f = None
        self._audit_bytes = 0

    # -- 对外属性 ------------------------------------------------------------ #
    @property
    def connected(self) -> bool:
        return bool(self._conn and self._hello_ok)

    def connected_for(self) -> float:
        """已握手连通多少秒（未连通为 0）。WindowHider 据此决定何时停掉轮询。"""
        t = self._connected_at
        return (time.time() - t) if (self.connected and t) else 0.0

    def wait_ready(self, timeout: float = 3.0) -> bool:
        """等握手完成（fne 连上并通过令牌校验）；超时返回 False。"""
        return self._ready.wait(timeout)

    # -- 生命周期 ------------------------------------------------------------ #
    def start(self):
        if sys.platform != "win32":
            self.error = "非 Windows，IPC 不可用"
            return self
        if self._t is not None:
            return self
        if self.log_path:
            # 审计打不开不应拖垮 IPC 本身：只记下原因，照常建管道
            try:
                os.makedirs(os.path.dirname(os.path.abspath(self.log_path)), exist_ok=True)
                self._audit_f = open(self.log_path, "a", encoding="utf-8", newline="\n")
            except OSError as e:
                self.audit_error = "%s: %s" % (type(e).__name__, e)
        try:
            k, ov_cls = _pipe_api()
            self._stop_h = k.CreateEventW(None, True, False, None)
            self._ev_w = k.CreateEventW(None, True, False, None)
            self._ov_w = ov_cls()
            self._ov_w.hEvent = self._ev_w
        except BaseException as e:                                 # noqa: BLE001
            self.error = "%s: %s" % (type(e).__name__, e)
            self._release()
            return self
        self._t = threading.Thread(target=self._serve, name="pipe-bridge", daemon=True)
        self._t.start()
        self._created.wait(1.0)          # 让调用方返回时管道已存在（fne 本来也会重试）
        return self

    def close(self):
        """干净退出：唤醒阻塞中的 IO、关句柄、关审计。可重复调用。"""
        self._closing = True
        try:
            if self._stop_h:
                _pipe_api()[0].SetEvent(self._stop_h)
        except BaseException:                                      # noqa: BLE001
            pass
        t = self._t
        if t is not None and t is not threading.current_thread():
            t.join(timeout=2.0)
        # 线程若还没退（极端情况）就不去关它在用的句柄；它是 daemon，不会卡住进程
        if t is None or not t.is_alive():
            self._release()

    def _release(self):
        try:
            k = _pipe_api()[0] if sys.platform == "win32" else None
        except BaseException:                                      # noqa: BLE001
            k = None
        with self._wlock:
            for name in ("_ev_w", "_stop_h"):
                h = getattr(self, name)
                setattr(self, name, None)
                if h and k is not None:
                    try:
                        k.CloseHandle(h)
                    except BaseException:                          # noqa: BLE001
                        pass
        with self._alk:
            f, self._audit_f = self._audit_f, None
            if f is not None:
                try:
                    f.close()
                except OSError:
                    pass

    # -- 读线程 -------------------------------------------------------------- #
    def _serve(self):
        import ctypes
        k, ov_cls = _pipe_api()
        ev = None
        try:
            invalid = ctypes.c_void_p(-1).value
            flags = 0x3 | 0x40000000 | 0x00080000     # DUPLEX | OVERLAPPED | FIRST_PIPE_INSTANCE
            mode = 0x0 | 0x0 | 0x0 | 0x8              # BYTE | READMODE_BYTE | WAIT | REJECT_REMOTE
            sa_ptr, sd = None, None
            if self.sddl:
                import ctypes.wintypes as wt
                adv = ctypes.WinDLL("advapi32", use_last_error=True)
                adv.ConvertStringSecurityDescriptorToSecurityDescriptorW.argtypes = [
                    wt.LPCWSTR, wt.DWORD, ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p]

                class SECURITY_ATTRIBUTES(ctypes.Structure):
                    _fields_ = [("nLength", wt.DWORD), ("lpSecurityDescriptor", ctypes.c_void_p),
                                ("bInheritHandle", wt.BOOL)]

                sd = ctypes.c_void_p()
                if not adv.ConvertStringSecurityDescriptorToSecurityDescriptorW(
                        self.sddl, 1, ctypes.byref(sd), None):
                    raise OSError(ctypes.get_last_error(), "SDDL 无效: %s" % self.sddl)
                sa = SECURITY_ATTRIBUTES(ctypes.sizeof(SECURITY_ATTRIBUTES), sd, False)
                sa_ptr = ctypes.byref(sa)
            h = k.CreateNamedPipeW(self.pipe_path, flags, mode, 255, 65536, 65536, 0, sa_ptr)
            if sd is not None and sd.value:
                ctypes.WinDLL("kernel32").LocalFree(sd)
            if h is None or h == invalid:
                raise OSError(ctypes.get_last_error(), "CreateNamedPipe 失败: %s" % self.pipe_path)
            with self._wlock:
                self._h = h
            self._created.set()
            ev = k.CreateEventW(None, True, False, None)
            while not self._closing:
                if not self._accept(k, ov_cls, h, ev):
                    break
                with self._wlock:
                    self._conn = True
                try:
                    self._session(k, ov_cls, h, ev)
                finally:
                    self._drop_conn(k, h)
        except BaseException as e:                                 # noqa: BLE001
            self.error = "%s: %s" % (type(e).__name__, e)
        finally:
            self._created.set()
            with self._wlock:
                hh, self._h = self._h, None
                self._conn = False
                self._hello_ok = False
            if hh:
                for fn in (k.DisconnectNamedPipe, k.CloseHandle):
                    try:
                        fn(hh)
                    except BaseException:                          # noqa: BLE001
                        pass
            if ev:
                try:
                    k.CloseHandle(ev)
                except BaseException:                              # noqa: BLE001
                    pass

    def _wait(self, k, ev, ms: int = 0xFFFFFFFF) -> int:
        """等 ev 或「停止事件」。返回 0=ev 触发 / 1=要求停止 / 其它=超时或失败。"""
        import ctypes.wintypes as wt
        arr = (wt.HANDLE * 2)(ev, self._stop_h)
        return k.WaitForMultipleObjects(2, arr, False, ms)

    def _accept(self, k, ov_cls, h, ev) -> bool:
        import ctypes
        import ctypes.wintypes as wt
        ov = ov_cls()
        ov.hEvent = ev
        k.ResetEvent(ev)
        if k.ConnectNamedPipe(h, ctypes.byref(ov)):
            return True
        err = ctypes.get_last_error()
        if err == 535:                                  # ERROR_PIPE_CONNECTED：客户端抢先连上了
            return True
        if err != 997:                                  # 不是 IO_PENDING
            raise OSError(err, "ConnectNamedPipe 失败")
        n = wt.DWORD(0)
        if self._wait(k, ev) == 0:
            if k.GetOverlappedResult(h, ctypes.byref(ov), ctypes.byref(n), False):
                return True
            raise OSError(ctypes.get_last_error(), "ConnectNamedPipe 完成态失败")
        k.CancelIoEx(h, ctypes.byref(ov))               # 收到停止：撤销挂起的连接等待
        k.GetOverlappedResult(h, ctypes.byref(ov), ctypes.byref(n), True)
        return False

    def _drop_conn(self, k, h):
        with self._wlock:
            self._conn = False
            self._hello_ok = False
            self._connected_at = None
            try:
                k.DisconnectNamedPipe(h)
            except BaseException:                                  # noqa: BLE001
                pass

    def _session(self, k, ov_cls, h, ev):
        """一次连接的生命周期：逐块读、按换行符切行、派发。返回即表示会话结束。"""
        import ctypes
        import ctypes.wintypes as wt
        buf = ctypes.create_string_buffer(65536)
        ov = ov_cls()
        ov.hEvent = ev
        acc = bytearray()
        while not self._closing:
            k.ResetEvent(ev)
            n = wt.DWORD(0)
            if not k.ReadFile(h, buf, 65536, ctypes.byref(n), ctypes.byref(ov)):
                if ctypes.get_last_error() != 997:      # 109=对端已关 等：会话结束
                    return
                if self._wait(k, ev) != 0:              # 要求停止：撤销挂起的读
                    k.CancelIoEx(h, ctypes.byref(ov))
                    k.GetOverlappedResult(h, ctypes.byref(ov), ctypes.byref(n), True)
                    return
                if not k.GetOverlappedResult(h, ctypes.byref(ov), ctypes.byref(n), False):
                    return
            if n.value == 0:
                return
            acc += ctypes.string_at(buf, n.value)
            while True:
                i = acc.find(b"\n")
                if i < 0:
                    break
                line = bytes(acc[:i])
                del acc[:i + 1]
                if not self._on_line(line):
                    return
            if len(acc) > self.LINE_MAX:
                acc.clear()
                with self._lk:
                    self.bad_frames += 1

    # -- 帧处理 -------------------------------------------------------------- #
    def _on_line(self, line: bytes) -> bool:
        """处理一行；返回 False 表示应结束本次会话（握手失败）。"""
        text = line.decode("utf-8", errors="replace").rstrip("\r")
        if not text.strip():
            return True
        with self._lk:
            self.frames_in += 1
        self._audit("in", text)
        try:
            obj = json.loads(text)
            if not isinstance(obj, dict):
                raise ValueError("not an object")
        except ValueError:
            with self._lk:
                self.bad_frames += 1
            return True
        t = obj.get("t")
        if not self._hello_ok:
            if t != "hello":
                with self._lk:
                    self.ignored_pre_hello += 1
                return True
            return self._on_hello(obj)
        if t == "dialog":
            self._on_dialog(obj)
        elif t == "event":
            kk = obj.get("k")
            if isinstance(kk, str) and len(kk) <= 32:
                with self._lk:
                    if kk in self.events_in or len(self.events_in) < 32:
                        self.events_in[kk] = self.events_in.get(kk, 0) + 1
        elif t == "result":
            how = obj.get("how")
            how = how if isinstance(how, str) and len(how) <= 16 else "?"
            with self._lk:
                self.results_in[how] = self.results_in.get(how, 0) + 1
                if how == "fallback":
                    self.fallbacks += 1
        elif t == "ping":
            self.send({"t": "pong", "ts": obj.get("ts", 0)})
        elif t in ("pong", "policy", "hello"):
            pass                                       # 合法但无需处理（hello 重发直接忽略）
        else:
            with self._lk:
                self.unknown_frames += 1               # 未识别的 t：忽略并计数
        return True

    def _on_hello(self, obj: dict) -> bool:
        import hmac
        tok = unhexg(obj.get("token"))
        # 令牌按协议是 hex(GBK)；也容忍对端直接发明文（令牌本身就是十六进制串）
        good = (hmac.compare_digest(tok.encode("utf-8"), self.token.encode("utf-8"))
                or hmac.compare_digest(str(obj.get("token", "")).encode("utf-8"),
                                       self.token.encode("utf-8")))
        if not good:
            with self._lk:
                self.handshake_fail += 1
            self.send({"t": "ack", "v": self.ACK_V, "ok": 0, "err": hexg("token mismatch")})
            return False
        try:
            self.peer_pid = int(obj.get("pid"))
        except (TypeError, ValueError):
            self.peer_pid = None
        self.peer_fw = unhexg(obj.get("fw"))
        if not self.send({"t": "ack", "v": self.ACK_V, "ok": 1}):
            return False
        self._connected_at = time.time()
        self._hello_ok = True
        self.ever_connected = True
        self._ready.set()
        return True

    @staticmethod
    def _num(v, default=None):
        try:
            return int(v)
        except (TypeError, ValueError):
            return default

    def _on_dialog(self, obj: dict):
        t0 = time.perf_counter()
        did = self._num(obj.get("id"))
        if did is None:
            with self._lk:
                self.bad_frames += 1
            return
        btns = []
        for b in (obj.get("btns") or []):
            if isinstance(b, dict) and self._num(b.get("i")) is not None:
                btns.append({"i": self._num(b.get("i")), "txt": unhexg(b.get("txt"))})
        edits = []
        for e in (obj.get("edits") or []):
            if isinstance(e, dict) and self._num(e.get("c")) is not None:
                edits.append({"c": self._num(e.get("c")), "cls": unhexg(e.get("cls")),
                              "txt": unhexg(e.get("txt"))})
        dlg = {"id": did, "hwnd": obj.get("hwnd"), "kind": obj.get("kind"),
               "cls": unhexg(obj.get("cls")), "title": unhexg(obj.get("title")),
               "msg": unhexg(obj.get("msg")), "btns": btns, "edits": edits,
               "pol": obj.get("pol"), "ts": obj.get("ts")}
        dec = self._decide(dlg)
        frame = self._build_decide(did, dec, btns, edits)
        sent = self.send(frame)
        ms = (time.perf_counter() - t0) * 1000.0
        with self._lk:
            self.dialogs_in += 1
            if sent:
                self.decisions += 1
                self.latency.append(ms)
            self.recent.append({"id": did, "kind": dlg["kind"], "title": dlg["title"],
                                "msg": dlg["msg"][:200], "act": frame["act"],
                                "btn": frame.get("btn"), "sent": sent})
            if len(self.recent) > 20:
                del self.recent[:-20]

    def _decide(self, dlg: dict) -> dict:
        """决策入口：先问外部 decider（若有），异常/无结果/形态不对就退回默认规则。"""
        if self._decider is not None:
            try:
                d = self._decider(dlg)
                if isinstance(d, dict) and d.get("act") in self.ACTS:
                    return d
            except BaseException:                                  # noqa: BLE001
                pass
        return self._default_decide(dlg)

    def _default_decide(self, dlg: dict) -> dict:
        """默认规则（与 fne 内置兜底一致）：
        单按钮 → 点它；多按钮有 IDOK → 点 IDOK；
        多按钮无 IDOK、有 IDCANCEL 且无 IDYES → 点 IDCANCEL；其余 → none。

        例外：`pol == "observe"`（ELANG_AI_DLG_POLICY=observe）时一律 none。
        观察模式的语义是「只记录，不动窗口」，桥这边要是照点不误，等于把
        用户显式设的观察模式破了 —— 这类框可能是程序自己的业务确认框。"""
        if (dlg.get("pol") or "").lower() == "observe":
            return {"act": "none"}
        ids = [b["i"] for b in (dlg.get("btns") or [])]
        if len(ids) == 1:
            return {"act": "click", "btn": ids[0]}
        if len(ids) > 1:
            if self.IDOK in ids:
                return {"act": "click", "btn": self.IDOK}
            if self.IDCANCEL in ids and self.IDYES not in ids:
                return {"act": "click", "btn": self.IDCANCEL}
        return {"act": "none"}

    def _build_decide(self, did: int, dec: dict, btns: list, edits: list) -> dict:
        """把决策按协议白名单规整成 decide 帧；越权的 btn / 控件号一律剔除或降级为 none。"""
        act = dec.get("act")
        btn_ids = {b["i"] for b in btns}
        edit_ids = {e["c"] for e in edits}
        btn = self._num(dec.get("btn"))
        fill = {}
        for c, v in (dec.get("edits") or {}).items():
            ci = self._num(c)
            if ci in edit_ids:
                fill[str(ci)] = hexg(v)
        if act in ("click", "fillclick") and btn not in btn_ids:
            act = "fill" if (act == "fillclick" and fill) else "none"
        if act in ("fill", "fillclick") and not fill:
            act = "click" if (act == "fillclick" and btn in btn_ids) else "none"
        if act not in self.ACTS:
            act = "none"
        fr = {"t": "decide", "id": did, "act": act}
        if act in ("click", "fillclick"):
            fr["btn"] = btn
        if act in ("fill", "fillclick"):
            fr["edits"] = fill
        if dec.get("hide"):
            fr["hide"] = 1
        return fr

    # -- 发送 ---------------------------------------------------------------- #
    def send(self, obj: dict) -> bool:
        """按协议序列化成一行写进管道（同步等写完，最多 2 秒）并写审计。

        text 类字段请调用方自己先 hexg()；这里只保证输出是单行纯 ASCII JSON。"""
        try:
            line = json.dumps(obj, ensure_ascii=True, separators=(",", ":"))
        except (TypeError, ValueError):
            return False
        if not self._write((line + "\n").encode("ascii")):
            return False
        with self._lk:
            self.frames_out += 1
        self._audit("out", line)
        return True

    def set_policy(self, dlg: str, hide: str = None) -> bool:
        """运行期改 fne 策略（observe / dismiss）。"""
        fr = {"t": "policy", "dlg": dlg}
        if hide:
            fr["hide"] = hide
        return self.send(fr)

    def _write(self, raw: bytes) -> bool:
        import ctypes
        import ctypes.wintypes as wt
        with self._wlock:
            h = self._h
            if h is None or not self._conn or self._ov_w is None or self._ev_w is None:
                return False
            k = _pipe_api()[0]
            ev, ov = self._ev_w, self._ov_w
            k.ResetEvent(ev)
            buf = ctypes.create_string_buffer(raw, len(raw))
            n = wt.DWORD(0)
            if not k.WriteFile(h, buf, len(raw), ctypes.byref(n), ctypes.byref(ov)):
                if ctypes.get_last_error() != 997:
                    return False
                if k.WaitForSingleObject(ev, 2000) != 0:      # 对端不读：撤销，别无限卡
                    k.CancelIoEx(h, ctypes.byref(ov))
                    k.GetOverlappedResult(h, ctypes.byref(ov), ctypes.byref(n), True)
                    return False
                if not k.GetOverlappedResult(h, ctypes.byref(ov), ctypes.byref(n), False):
                    return False
            return n.value == len(raw)

    # -- 审计与统计 ---------------------------------------------------------- #
    def _audit(self, direction: str, raw: str):
        """<tag>_ipc.jsonl：每行 {"ts":毫秒,"dir":"in"|"out","raw":"原始一行"}（UTF-8）。"""
        with self._alk:
            f = self._audit_f
            if f is None or self._audit_bytes > self.AUDIT_MAX:
                return
            try:
                s = json.dumps({"ts": int(time.time() * 1000), "dir": direction, "raw": raw},
                               ensure_ascii=False) + "\n"
                f.write(s)
                f.flush()
                self._audit_bytes += len(s)
            except (OSError, ValueError):
                pass

    def p95_ms(self):
        with self._lk:
            xs = sorted(self.latency)
        if not xs:
            return None
        return round(xs[max(0, int(-(-len(xs) * 95 // 100)) - 1)], 2)

    def stats(self) -> dict:
        p95 = self.p95_ms()
        with self._lk:
            return {
                "connected": self.connected, "everConnected": self.ever_connected,
                "framesIn": self.frames_in, "framesOut": self.frames_out,
                "decisions": self.decisions, "fallbacks": self.fallbacks, "p95Ms": p95,
                "dialogsIn": self.dialogs_in, "eventsIn": dict(self.events_in),
                "resultsIn": dict(self.results_in), "badFrames": self.bad_frames,
                "unknownFrames": self.unknown_frames, "handshakeFail": self.handshake_fail,
                "ignoredPreHello": self.ignored_pre_hello, "peerPid": self.peer_pid,
                "pipe": self.pipe_path, "auditFile": self.log_path, "error": self.error,
                "auditError": self.audit_error,
            }


class WindowHider(object):
    """后台线程：持续把 `e.exe` 的顶层窗口「移出屏幕」和/或「SW_HIDE」并统计露脸情况。

    ## 两条通道（缺一不可，2026-09-22 逐像素取证后加的）

    | 通道 | 机制 | 延迟 | 覆盖 |
    |---|---|---|---|
    | **事件驱动**（主） | `SetWinEventHook(OBJECT_CREATE…OBJECT_SHOW)` + 消息泵 | **~1ms，零轮询 CPU** | 所有「新显示」的窗口 |
    | **轮询**（兜底 + 计数） | `EnumWindows` 逐顶层窗口 | ~50ms（要回调全系统 800+ 窗口） | 事件漏掉的、或已存在才被改动的 |

    ⚠️ **为什么必须有事件通道**：轮询一轮要回调**全系统 800 多个顶层窗口**（Python 回调
    开销），实测一轮 ~50ms。而 DWM 约 **16ms** 合成一帧 ⇒ 一个只显示一帧的窗口
    （易语言启动时的 `shadow_window` 全屏阴影）**轮询根本抓不到**。
    逐像素测量抓到过 17637 抽样像素的整屏差异，就是这一帧。
    事件通道是内核直接投递到本线程消息队列的，且**只在真有窗口显示时才唤醒**，
    所以既快又不吃 CPU。

    ## 轮询引擎（engine，2026-10-29 宿主事件总线）

    环境变量 `ELANG_AI_HIDE_ENGINE`（优先于构造参数）取值：

    | engine | 行为 |
    |---|---|
    | `host`（IPC 启用时的默认） | **不跑周期性全系统 EnumWindows**：只在启动后 ~2 秒宽限期内做有限轮次兜底，之后仅保留 SetWinEventHook 事件通道；桥已连通 >3 秒也会立刻停轮询 |
    | `both` | 事件 + 轮询（旧行为；IPC 未启用时的默认，保持与改动前一致） |
    | `python` | 只跑轮询（调试用） |

    停轮询的原因记在 `poll_stopped_reason`。`host` 下若事件通道没成功挂上
    （`hook_ok` 为假），轮询**不会停**，等同 `both`——否则就没有任何 Python 侧兜底了。

    ## 三种策略（mode）

    | mode | 启动时 SW_HIDE 主窗口 | 动作 | 实测结论 |
    |---|---|---|---|
    | `park` | 否 | 只移到 (-32000,-32000)，**保持可见** | 兜底。⚠️ 可见 ⇒ 任务栏会留按钮 |
    | `shide` | **是** | 只移到屏幕外（不再隐藏） | 主窗口零闪；其它窗口靠移出屏幕 |
    | `hide` | **是** | 隐藏 **+** 移出屏幕 | **默认**。什么都没有（无任务栏、无 Alt-Tab） |

    ## ⚠️ `hide` 曾经「卡死编译」—— 根因在本套装自己的填充器，已修

    2026-09-22 首轮实测：把所有窗口 `SW_HIDE` 之后，`FN_COMPILE_STATIC` 触发出去
    **再也不返回**（面板停在 `COMPILE-TRIGGER send`，300 秒超时），
    当时误以为是「易语言编译链路依赖窗口可见」。**错的**。真因有两条，都在宿主库
    `elang_addin.cpp` 的保存框填充器里：

    1. `FindCompileSaveDlgProc` 要求对话框 `IsWindowVisible` —— 被隐藏后永远找不到，
       模态框没人消 ⇒ 触发线程 `IdeRunFunc` 永不返回；
    2. 点击只用 `PostMessage(BM_CLICK)` 且**只点一次** —— 隐藏状态下第一次点击被吞掉，
       `blFilled` 已置位、再没人重试 ⇒ 同样卡死。

    修法：① 判据改成「同进程 + `#32770` + 标题含『请输入欲编译到』」，**不看可见性**；
    ② 点击前把对话框 `SW_SHOWNOACTIVATE` 并重新挪到屏幕外，且**只要它还在就重试**
    （限速 300ms）。修完后 `hide` 在 `--check` / 真编译 / 调试运行三条链路上全部通过。

    ## ⚠️ 2026-09-24：还有第三条真因（同样在填充器里，已修）

    修完上面两条后仍**间歇**卡超时。取证（`ELANG_AI_DLG_ONLY=1` 只扫描不填不点）发现：
    易语言这个保存框是 **Vista 风格公共对话框**，`EnumChildWindows` 在它刚弹出的头一两秒
    里只能看到按钮 / `DirectUIHWND` / `RebarWindow32`，**一个 `Edit` 都没有** ——
    文件名编辑框是延迟创建的。此时旧实现照样「写第一个 Edit（找不到就白写）+ 点确定」，
    等于**拿空文件名去存** ⇒ 易语言退回按工程名另存 ⇒ 目标已存在 ⇒ 弹繁中
    「確認另存新檔」覆盖确认框 ⇒ 没人应答 ⇒ 模态栈不消 ⇒ 触发线程永不返回。
    旧实现在 `e2ework2` 那轮连续报了 **1155 次**「填充成功」，产物却从未出现。

    修法：① 收集全部候选控件并**回读自证**，回读不上就**拒绝点确定**；② 文件名框优先按
    comdlg32 固定控件号 `edt1(0x0480)` 取，并同时写它的父 `ComboBox`（ComboBox 型文件名框
    读的是 ComboBox 自己的文本，只改内层 Edit 无效）；③ 每轮先应答「另存新檔」覆盖框；
    ④ 确认改成 `WM_COMMAND(IDOK)` + `BM_CLICK` + 向输入框投 `VK_RETURN` 三连；
    ⑤ 新增 `ELANG_AI_DLG_ONLY=1` 快速取证模式（只把控件树打进 trace，不填不点），
    把排查从「一次 9 分钟等超时」压到「一次 90 秒看 dump」。
    **排查保存框问题时先开它。**

    ## 为什么是这套方案（三条路都实测过，见 re/win_probe/）

    1. **独立 Windows 桌面**（`CreateDesktop` + `STARTUPINFO.lpDesktop`）：❌ **不通**。
       `e.exe` 在非默认桌面上 **0.04 秒就自行退出** —— 易语言拒绝在非交互桌面上运行。
       （顺带踩到一个 Python 坑：`subprocess.STARTUPINFO` **不暴露 `lpDesktop`**，
        `si.lpDesktop = "X"` 只是给 Python 对象加了个没人读的属性，子进程照样在默认桌面上。）
    2. **只靠 `STARTUPINFO.wShowWindow = SW_HIDE`**：⚠️ 只压得住主窗口 `ENewFrame`。
       实测仍有 `shadow_window`(1.5s) / `Afx:…`「创建新项目」对话框(3.9s) / `NewWindow`
       编辑器窗口(4.4s) 会照常显示 —— 用户看见的就是它们。
    3. **本策略**：✅ 「移出屏幕」把露脸压到 0~1 次、单次 ≤2ms。

    窗口一旦被挪到 (-32000,-32000)，即使 IDE 之后自己再 `ShowWindow` 一次也仍在屏幕外，
    所以只需低频重扫即可；只有「刚出现新窗口」时才切到高频。
    """

    POLL_BUSY = 0.002          # 刚看到新窗口：2ms
    POLL_IDLE = 0.012          # 安静时：12ms
    OFFSCREEN = -32000
    MODES = ("park", "shide", "hide")
    ENGINES = ("host", "both", "python")
    HOST_GRACE_SEC = 2.0       # host 引擎：启动后兜底轮询的宽限期
    HOST_MAX_ROUNDS = 200      # host 引擎：宽限期内最多轮询多少轮（有限轮次）
    HOST_BRIDGE_SEC = 3.0      # host 引擎：桥连通超过这么久就彻底停轮询

    def __init__(self, pid: int, mode: str = "hide", verbose=None, engine: str = "host",
                 bridge=None):
        self.pid = pid
        self.mode = mode if mode in self.MODES else "hide"
        # 环境变量优先于参数；非法值一律按参数/默认 host
        _env_engine = os.environ.get("ELANG_AI_HIDE_ENGINE", "").strip().lower()
        if _env_engine in self.ENGINES:
            engine = _env_engine
        self.engine = engine if engine in self.ENGINES else "host"
        self.bridge = bridge                    # PipeBridge（可为 None）；只读它的连通状态
        self.poll_stopped_reason = None         # 轮询分支为什么停的（统计用）
        self.enabled = bool(mode != "off" and sys.platform == "win32")
        self.hide_windows = (self.mode == "hide")     # 轮询里是否 SW_HIDE
        self._verbose = verbose
        self._stop = threading.Event()
        self._t = None
        self._th = None             # 事件通道线程
        self._tid_win = None        # 事件通道线程的 Win32 线程 id（用于 PostThreadMessage 退出）
        self._lk = threading.Lock()
        self._cb = None             # SetWinEventHook 回调（必须持引用）
        self.hook_ok = False
        self.hook_error = None
        self.hook_hits = 0          # 事件通道动作次数
        self.hook_log = []          # 事件通道发现的窗口（有界）
        self.handled = 0            # 累计动过的窗口次数（去抖后）
        self.peak_visible = 0       # 采样到「可见且与屏幕相交」的次数上限（越少越好）
        self._worst = []            # 前若干条露脸记录
        self.polls = 0              # 轮询次数（诊断：轮询是否真在跑）
        self.seen = 0               # 累计「枚举到可见顶层窗口」的个数（诊断）
        self.diag = []              # 诊断：逐轮询聚合（有界）
        self.diag_vis = []          # 诊断：本进程「可见」窗口的首批记录（有界）
        self.n_enum = 0             # 诊断：累计 EnumWindows 枚举到的窗口总数（含别的进程）
        self.n_own = 0              # 诊断：累计「属于本进程」的窗口数
        self.n_vis_own = 0          # 诊断：累计「属于本进程且可见」的窗口数
        self.errs = []              # 诊断：EnumWindows 抛出的异常（有界）
        self.error = None
        if not self.enabled:
            self.poll_stopped_reason = "disabled"
    # -- 内部：Win32 --
    def _run(self):
        import ctypes
        import ctypes.wintypes as wt
        u32 = ctypes.WinDLL("user32", use_last_error=True)
        EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
        # ⚠️ Windows 默认定时器精度是 **15.6ms** —— 不提升的话 `sleep(0.002)` 实际睡 15.6ms，
        #    「2ms 轮询」就是假的。`timeBeginPeriod(1)` 把它提到 1ms（winmm 全局设置）。
        try:
            ctypes.WinDLL("winmm").timeBeginPeriod(1)
        except BaseException:                                      # noqa: BLE001
            pass

        class RECT(ctypes.Structure):
            _fields_ = [("left", wt.LONG), ("top", wt.LONG),
                        ("right", wt.LONG), ("bottom", wt.LONG)]

        _vx = u32.GetSystemMetrics(76)
        _vy = u32.GetSystemMetrics(77)
        _vw = u32.GetSystemMetrics(78)
        _vh = u32.GetSystemMetrics(79)
        _t0 = time.time()

        def on_screen(r):
            if r.right <= r.left or r.bottom <= r.top:
                return False
            return not (r.right <= _vx or r.left >= _vx + _vw
                        or r.bottom <= _vy or r.top >= _vy + _vh)

        _poll_t0 = time.time()
        while not self._stop.is_set():
            if self.engine == "host":
                _why = self._host_poll_stop_reason(_poll_t0)
                if _why:
                    self.poll_stopped_reason = _why
                    break
            found = []
            box = [0]
            st = [0, 0]

            def cb(hwnd, _l):
                self.n_enum += 1
                st[0] += 1
                p = wt.DWORD()
                u32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
                if p.value != self.pid:
                    return True
                self.n_own += 1
                r = RECT()
                u32.GetWindowRect(hwnd, ctypes.byref(r))
                vis = bool(u32.IsWindowVisible(hwnd))
                if vis:
                    st[1] += 1
                    self.n_vis_own += 1
                    if len(self.diag_vis) < 40:
                        c = ctypes.create_string_buffer(128)
                        u32.GetClassNameA(hwnd, c, 128)
                        self.diag_vis.append("[%.2fs] VISIBLE cls=%-24s rect=%s"
                                             % (time.time() - _t0,
                                                c.value.decode("gbk", "replace")[:24],
                                                (r.left, r.top, r.right, r.bottom)))
                if not vis:
                    return True
                found.append((hwnd, on_screen(r), (r.left, r.top, r.right, r.bottom)))
                box[0] = 1
                return True

            try:
                u32.EnumWindows(EnumProc(cb), 0)
            except BaseException as e:                                 # noqa: BLE001
                if len(self.errs) < 4:
                    self.errs.append("%s: %s" % (type(e).__name__, e))
            self.polls += 1
            if len(self.diag) < 300:
                self.diag.append("[%6.2fs] poll#%-4d 枚举=%-4d 本进程可见=%-3d 命中=%d"
                                 % (time.time() - _t0, self.polls, st[0], st[1], len(found)))

            busy = False
            for hwnd, ons, rect in found:
                if ons:
                    self.peak_visible += 1
                    if len(self._worst) < 12:
                        self._worst.append(rect)
                self._act(hwnd)
                busy = True
            self.seen += len(found)

            self._stop.wait(self.POLL_BUSY if busy else self.POLL_IDLE)

        if self.poll_stopped_reason is None:
            self.poll_stopped_reason = "stop-requested"
        try:
            ctypes.WinDLL("winmm").timeEndPeriod(1)
        except BaseException:                                      # noqa: BLE001
            pass

    def _host_poll_stop_reason(self, t0: float):
        """host 引擎：轮询分支该不该停；返回原因串，None=继续。

        事件通道没挂上（hook_ok 为假）时绝不停，否则 Python 侧就彻底没有兜底了。"""
        if not self.hook_ok:
            return None
        br = self.bridge
        if br is not None and br.connected:
            # 桥活着：连满 HOST_BRIDGE_SEC 就把轮询撤掉，从此靠宿主事件总线
            if br.connected_for() > self.HOST_BRIDGE_SEC:
                return "bridge-connected"
            return None
        if br is None or not getattr(br, "ever_connected", False):
            # 桥从头到尾没连上过（旧版 fne / ACL 拒绝 / --no-ipc）⇒ 绝不停轮询。
            # 这时候停掉等于把 Python 侧唯一的兜底撤了，宁可退回改动前的行为。
            return None
        if time.time() - t0 >= self.HOST_GRACE_SEC:
            return "bridge-lost"
        if self.polls >= self.HOST_MAX_ROUNDS:
            return "bridge-lost"
        return None

    def start(self):
        if not self.enabled:
            return self
        self._t = threading.Thread(target=self._guard, name="window-hider", daemon=True)
        self._t.start()
        self._th = threading.Thread(target=self._hook_loop, name="window-hook", daemon=True)
        self._th.start()
        return self

    def _guard(self):
        try:
            self._run()
        except BaseException as e:                                 # noqa: BLE001
            self.error = "%s: %s" % (type(e).__name__, e)
            self.poll_stopped_reason = "error"

    # -- 事件驱动通道 ------------------------------------------------------- #
    EV_OBJECT_CREATE = 0x8000
    EV_OBJECT_SHOW = 0x8002
    WINEVENT_OUTOFCONTEXT = 0x0000
    OBJID_WINDOW = 0
    WM_QUIT = 0x0012

    def _act(self, hwnd):
        """对一个窗口执行「隐藏（可选）+ 移出屏幕」。两个通道共用。"""
        u32 = _user32()
        if self.hide_windows:
            u32.ShowWindow(hwnd, 0)                                # SW_HIDE
        u32.SetWindowPos(hwnd, 0, self.OFFSCREEN, self.OFFSCREEN, 0, 0,
                         0x0001 | 0x0004 | 0x0010)                 # NOSIZE|NOZORDER|NOACTIVATE
        with self._lk:
            self.handled += 1

    def _hook_loop(self):
        """`SetWinEventHook` + 消息泵。内核把「窗口显示」事件投到本线程队列，
        回调里立刻把窗口隐藏/挪走 —— 在 DWM 下一次合成（~16ms）之前完成。"""
        import ctypes
        import ctypes.wintypes as wt
        u32 = _user32()
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)

        WinEventProc = ctypes.WINFUNCTYPE(None, wt.HANDLE, wt.DWORD, wt.HWND,
                                          wt.LONG, wt.LONG, wt.DWORD, wt.DWORD)
        u32.SetWinEventHook.restype = wt.HANDLE

        def on_event(_hook, _ev, hwnd, idObject, idChild, _tid, _ms):
            if not hwnd or idObject != self.OBJID_WINDOW or idChild != 0:
                return
            p = wt.DWORD()
            u32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
            if self.pid is None or p.value != self.pid:
                return
            if not u32.IsWindowVisible(hwnd):
                return
            with self._lk:
                self.hook_hits += 1
                if len(self.hook_log) < 24:
                    self.hook_log.append("cls=%s rect=%s" % (_cls_name(u32, hwnd),
                                                             _rect(u32, hwnd)))
            self._act(hwnd)

        self._cb = WinEventProc(on_event)          # 必须持引用，否则被 GC
        try:
            h = u32.SetWinEventHook(self.EV_OBJECT_CREATE, self.EV_OBJECT_SHOW, 0,
                                    self._cb, 0, 0, self.WINEVENT_OUTOFCONTEXT)
        except BaseException as e:                                     # noqa: BLE001
            self.hook_error = "%s: %s" % (type(e).__name__, e)
            return
        if not h:
            self.hook_error = "SetWinEventHook 返回 NULL (err=%d)" % ctypes.get_last_error()
            return
        self.hook_ok = True
        self._tid_win = k32.GetCurrentThreadId()

        msg = wt.MSG()
        while not self._stop.is_set():
            r = u32.GetMessageW(ctypes.byref(msg), 0, 0, 0)            # 阻塞在内核里，不占 CPU
            if r in (0, -1):
                break
            u32.TranslateMessage(ctypes.byref(msg))
            u32.DispatchMessageW(ctypes.byref(msg))
        u32.UnhookWinEvent(h)

    def stop(self):
        self._stop.set()
        import ctypes
        try:
            if self._tid_win:
                _user32().PostThreadMessageW(self._tid_win, self.WM_QUIT, 0, 0)
        except BaseException:                                          # noqa: BLE001
            pass
        if self._t is not None:
            self._t.join(timeout=2.0)
        if self._th is not None:
            self._th.join(timeout=2.0)

    def summary(self) -> str:
        if not self.enabled:
            return "未启用（--headless off / 非 Windows）"
        if self.error:
            return "失败: %s" % self.error
        s = ("mode=%s 动作 %d 次（事件通道 %d / 轮询 %d；轮询 %d 次见到可见窗口 %d 个）；"
             "采样到「可见且与屏幕相交」%d 次"
             % (self.mode, self.handled, self.hook_hits, self.handled - self.hook_hits,
                self.polls, self.seen, self.peak_visible))
        if self.engine != "both":
            s += "；engine=%s 轮询停止原因=%s" % (self.engine, self.poll_stopped_reason)
        return s


def make_hidden_startupinfo():
    """让 e.exe 自己就**不显示主窗口**：`STARTUPINFO.wShowWindow = SW_HIDE`。

    ⚠️ 只有 mode 含 `shide`/`hide` 时才用。`subprocess.STARTUPINFO` 是暴露
    `wShowWindow` 的（但**不暴露** `lpDesktop`，别想拿它做独立桌面）。
    """
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0                                              # SW_HIDE
    return si


def make_visible_startupinfo():
    """`park`/`off` 档用：**显式** `STARTF_USESHOWWINDOW + wShowWindow = SW_SHOW`。

    ⚠️ 不能图省事传 `startupinfo=None`：全零 `STARTUPINFO` 里 `wShowWindow=0`，
    GUI 程序的 `SW_SHOWDEFAULT` 会回读这个 0 ⇒ 主窗口照样按 SW_HIDE 创建
    （2026-09-23 实测：off 档全程 0 像素，`--visible` 名不符实）。"""
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 5                                              # SW_SHOW
    return si


# --------------------------------------------------------------------------- #
# 主流程
# --------------------------------------------------------------------------- #
def preflight(e_dir: str, lib_dir: str, log_fn=print):
    """启动前置健康检查（交接 2026-09-26「易语言 IDE 卡死」专项，红线 1/C.2）：

    ① LowLevelHooksTimeout 不得 > 300（Windows 默认 300ms）。历史 AI 曾把它改成
       25000，低级钩子一卡就能把全系统输入冻结 5-10 秒。>300 时自动删键值回滚
       默认并大声打日志；键值不存在 = 默认，正常。
    ② lib\\ 下残留 elang_addin.fne = 上次异常残留（严禁常驻），先删再启动。
    幂等：只删违规项，其余一律不动。
    """
    # ① 注册表门禁
    try:
        import winreg
        key_path = r"Control Panel\Desktop"
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path, 0,
                            winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as k:
            try:
                cur, _typ = winreg.QueryValueEx(k, "LowLevelHooksTimeout")
            except OSError:
                cur = None
        if cur is not None and int(cur) > 300:
            log_fn("!! LowLevelHooksTimeout=%s > 300（历史配置又改坏了，会冻键鼠），"
                   "自动回滚为 Windows 默认 300ms" % cur)
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path, 0,
                                winreg.KEY_SET_VALUE | winreg.KEY_WOW64_64KEY) as k:
                winreg.DeleteValue(k, "LowLevelHooksTimeout")
            log_fn("!! 已删除 LowLevelHooksTimeout（重启/注销后对现有钩子生效）")
    except Exception as _e:      # 门禁自身绝不阻断启动
        log_fn("preflight[registry] 跳过: %s" % _e)
    # ② 残留 fne 清理
    try:
        stray = os.path.join(lib_dir, LIB_FNAME)
        if os.path.exists(stray):
            try:
                os.remove(stray)
                log_fn("!! 已清理 lib\\ 下残留的 %s（严禁常驻，见卡死交接红线）" % LIB_FNAME)
            except OSError:
                log_fn("!! lib\\%s 残留且删除失败——请手动删除后重试" % LIB_FNAME)
    except Exception as _e:
        log_fn("preflight[fne] 跳过: %s" % _e)


def elaunch(ai_e: str, log: str = None, timeout: float = 30.0, stop_after: float = None,
            fne: str = None, home: str = None, tag: str = None, quiet: bool = False,
            compile_kind: str = None, expect_out: str = None,
            check_only: bool = False, headless: str = None,
            open_target: str = None, ipc: bool = None) -> dict:
    # `precompile`（= FN_PRE_COMPILE）只做语法/语义静态检查：无产物、无保存框。
    pre_mode = (compile_kind == "precompile")
    # 编译模式禁止默认启动两层隐藏守护链：其会并发改写 IDE 窗口，
    # 大型 DLL 工程触发官方编译功能号时可能因此令 e.exe 访问冲突。
    # 调试运行仍沿用 hide 默认；无头编译须由调用方显式声明。
    if headless is None:
        headless = "off" if compile_kind else "hide"
    if headless is True:
        headless = "park"
    elif headless is False:
        headless = "off"
    hl_mode = headless
    # 宿主事件总线 + 双向 IPC（协议见 docs\方案-宿主事件总线与双向IPC协议.md）。
    # 默认开；--no-ipc / ELANG_AI_IPC=0 / 非 Windows 时与改动前行为完全一致。
    if ipc is None:
        ipc = os.environ.get("ELANG_AI_IPC", "1").strip().lower() not in ("0", "off", "false", "no")
    ipc = bool(ipc and sys.platform == "win32")

    ai_e = os.path.abspath(ai_e)
    if not os.path.isfile(ai_e):
        raise SystemExit("找不到加壳后的 .e: %s" % ai_e)
    if open_target:
        open_target = os.path.abspath(open_target)
        if not os.path.isfile(open_target):
            raise SystemExit("找不到待打开的目标 .e: %s" % open_target)

    e_dir = resolve_home(home)
    e_exe = os.path.join(e_dir, "e.exe")
    if not os.path.isfile(e_exe):
        raise SystemExit("在 %s 下找不到 e.exe" % e_dir)
    lib_dir = os.path.join(e_dir, "lib")
    dst_fne = os.path.join(lib_dir, LIB_FNAME)
    preflight(e_dir, lib_dir)   # 红线1/C.2：注册表门禁 + 残留 fne 清理

    fne = os.path.abspath(fne or DEFAULT_FNE)
    if not os.path.isfile(fne):
        raise SystemExit("找不到支持库 .fne: %s（用 --fne 指定）" % fne)
    # 仅清理本次投放的文件。已有同名文件表示上次异常残留或用户手动投放；
    # 覆盖它可能掩盖问题，随后 finally 的删除更可能误删用户文件。
    if os.path.exists(dst_fne):
        raise SystemExit(
            "检测到 lib\\%s 已存在；拒绝覆盖。请先将其移出 lib\\，确认普通 IDE "
            "可正常启动/编译后，再运行无头工具。" % LIB_FNAME)

    if log is None:
        log = os.path.splitext(ai_e)[0] + ".debug.log"
    log = os.path.abspath(log)
    log_dir = os.path.dirname(log)
    os.makedirs(log_dir, exist_ok=True)
    if not tag:
        tag = "%s_%s" % (os.path.splitext(os.path.basename(ai_e))[0], time.strftime("%H%M%S"))
    harness = os.path.join(log_dir, "%s_harness.txt" % tag)
    trace = os.path.join(log_dir, "%s_trace.txt" % tag)
    # 事件文件：**每会话独享**（旧实现缺省落 %TEMP%\elang_ai_events.log，多会话互相
    # 覆盖、还会读到上一次的旧事件）。fne 侧 EventWrite 认 ELANG_AI_EVENTS。
    events = os.path.join(log_dir, "%s_events.txt" % tag)
    ipc_log = os.path.join(log_dir, "%s_ipc.jsonl" % tag)

    # `--check` 走真编译时（`--check --compile <种类>`）不关心产物，但 IDE 的保存框必须
    # **有东西可填**才会被宿主库消掉（模态框不消掉，触发线程就永远不返回）。所以给个临时
    # 路径，用完删掉。预编译模式没有保存框，不需要。
    check_out_made = False
    if check_only and not pre_mode and not expect_out:
        expect_out = os.path.join(log_dir, "%s_checkout.exe" % tag)
        check_out_made = True

    # 只清**同名 tag** 旧证据，不碰别次运行
    for f in (log, trace, harness, events, ipc_log):
        if os.path.exists(f):
            safe_remove(f)

    tee = Tee(harness)
    old_out, old_err = sys.stdout, sys.stderr
    sys.stdout = sys.stderr = tee
    pid = None
    proc = None
    ho = None
    hider = None
    bridge = None
    ipc_stats = None
    before_dir = toplist(e_dir)
    cleanup_warn = []
    start = {"runStart": None, "runEnd": None, "weSent": False}
    e_self_exit = None
    e_exit_after_grace = None
    run_seconds = 0.0
    t0 = time.time()
    try:
        print("== elaunch ==")
        print("   ai.e    = %s" % ai_e)
        if open_target:
            print("   target  = %s" % open_target)
        print("   ELANG   = %s" % e_dir)
        print("   fne     = %s" % fne)
        print("   log     = %s" % log)
        print("   tag     = %s" % tag)
        print("   timeout = %ss   stop-after = %s" % (timeout, stop_after))

        # 投放支持库（只写这一个文件；结束删掉）
        shutil.copy2(fne, dst_fne)
        print("   已投放 lib\\%s (%d B)" % (LIB_FNAME, os.path.getsize(dst_fne)))

        env = os.environ.copy()
        # 事件通道（2026-10-03 起）：fne 的 DlgWatch 把每个模态框（含编译/运行期的
        # 致命错误框）写进本会话专属的事件文件；dismiss 策略让它替用户点掉可安全
        # 应答的框。Python 侧在等待循环里 tail 这份文件 —— 弹窗一出现就能带原文
        # 收尾，而不是空等到超时（面板判据对「弹窗报错」完全失效）。
        env["ELANG_AI_EVENTS"] = events
        env["ELANG_AI_DLG_POLICY"] = "dismiss"
        ipc_tag = ipc_token = ipc_pipe_tpl = None
        if ipc:
            # IPC：fne 是管道客户端，本进程是服务端。管道名含 e.exe 的 PID，而环境变量
            # 必须在 Popen 之前定好 —— 所以这里只能注入带 {pid} 占位的模板（fne 侧用
            # GetCurrentProcessId() + ELANG_AI_PIPE_TAG 自行拼出同一个名字，ELANG_AI_PIPE
            # 主要是「启用开关」），Popen 之后再按真实 PID 建管道（fne 会重试 3 秒）。
            ipc_tag = secrets.token_hex(3)
            ipc_token = secrets.token_hex(16)
            ipc_pipe_tpl = "\\\\.\\pipe\\elang_addin.{pid}.%s" % ipc_tag
            env["ELANG_AI_PIPE"] = ipc_pipe_tpl
            env["ELANG_AI_PIPE_TOKEN"] = ipc_token
            env["ELANG_AI_PIPE_TAG"] = ipc_tag
            env.setdefault("ELANG_AI_DLG_TIMEOUT_MS", "1500")
        if compile_kind:
            # 编译模式：触发 FNST_RUN 编译家族功能号，监视期望产物出现
            env.update({
                "ELANG_AI_TRACE": trace,
                "ELANG_AI_TASK": ai_e,
                "ELANG_AI_COMPILE": compile_kind,
                "ELANG_AI_COMPILE_DELAY": "2500",
                "ELANG_AI_EXPECT_OUT": expect_out or "",
                "ELANG_AI_CAPTURE": log,
                "ELANG_AI_HIDE": ("0" if hl_mode == "off" else hl_mode),
            })
            if open_target:
                env["ELANG_AI_OPEN_TARGET"] = open_target
        else:
            env.update({
                "ELANG_AI_TRACE": trace,
                "ELANG_AI_TASK": ai_e,
                "ELANG_AI_RUN": "1",
                "ELANG_AI_RUN_DELAY": "2500",
                "ELANG_AI_CAPTURE": log,
                "ELANG_AI_HIDE": ("0" if hl_mode == "off" else hl_mode),
            })
            env["ELANG_AI_STOP"] = str(int(stop_after)) if stop_after else "0"

        # e.exe 的 stdout/stderr 也并进 harness（追加）
        ho = open(harness, "ab")
        if compile_kind and expect_out and os.path.exists(expect_out):
            # 旧产物会干扰「产物出现」判定，启动前清掉（该路径是本次编译的输出目标）
            safe_remove(expect_out)
        # 无头：`park`/`shide`/`hide` 见 WindowHider；`off` = 旧行为（窗口正常显示）
        # ⚠️ park/off 也必须**显式** SW_SHOW（见 make_visible_startupinfo 的注释）
        si_hide = (hl_mode in ("shide", "hide"))
        proc = subprocess.Popen([e_exe, ai_e], cwd=e_dir, env=env,
                                stdout=ho, stderr=ho,
                                startupinfo=(make_hidden_startupinfo() if si_hide
                                             else make_visible_startupinfo()))
        pid = proc.pid
        print("   e.exe pid = %d" % pid)
        print("   无头模式 = %s" % hl_mode)
        if ipc:
            pipe_name = ipc_pipe_tpl.replace("{pid}", str(pid))
            try:
                _to = int(env.get("ELANG_AI_DLG_TIMEOUT_MS") or 1500)
            except ValueError:
                _to = 1500
            bridge = PipeBridge(pipe_name, ipc_token, ipc_log, _to,
                                sddl=(os.environ.get("ELANG_AI_PIPE_SDDL") or None)).start()
            print("   ipc     = %s%s" % (pipe_name, ("  !! %s" % bridge.error) if bridge.error else ""))
        # IPC 启用 → 窗口隐藏交给宿主事件总线（host 引擎）；未启用 → 沿用事件+轮询（both）
        hider = WindowHider(pid, mode=hl_mode, engine=("host" if bridge else "both"),
                            bridge=bridge).start()
        evw = EventWatch(events)

        deadline = t0 + float(timeout)
        if compile_kind:
            # 编译等待，三条路各有一个判据：
            #   * 真编译（--compile）        → 期望产物出现（mtime 晚于启动）即成
            #   * 预编译（--check --compile precompile） → `COMPILE-PRE-RET ... ok=N` 即结果，再等
            #                                 `=== COMPILE END ===` 把面板报错收全
            #   * 语法检查 + 真编译（--check --compile X）→ 保存框出现即静态检查通过
            # 触发被拒 / 面板出现 `错误(NNNN):` / IDE 自崩 都是失败路径；否则到点超时
            while True:
                rc = proc.poll()
                if rc is not None:
                    e_self_exit = rc
                    break
                evw.pump()
                err = evw.first_error()
                blocked = evw.blocked_dialog()
                if err or blocked:
                    # 弹窗报错（如「无法找到启动子程序」）：面板一个字都没有，
                    # 原有判据（面板错误行/保存框/产物）全部落空 —— 带着弹窗原文
                    # 立刻收尾，不再空等到超时。错误框出现即失败（已被自动点掉
                    # 也一样：编译已经中止）；普通模态框 12 秒无人应答也按阻塞算。
                    start["dialogBlocked"] = err or blocked
                    _b = start["dialogBlocked"]
                    print("   !! 模态弹窗: title='%s' msg='%s'"
                          % (_b.get("title"), _b.get("msg")))
                    break
                tr = read_text_best(trace)
                cap = read_text_best(log)
                # ⚠️ `COMPILE-*` 这些阶段事件是宿主库用 CapWrite 写的 → 落**面板日志**
                # （`--log` 那个文件），**不在 trace 里**。曾在这里读错文件，
                # 导致 --check 永远等不到保存框、一路空等到超时。
                if TRIGGER_REFUSED in cap:
                    start["compileFailed"] = True
                    break
                m = PRE_RET_RE.search(cap)
                if pre_mode and m:
                    if start.get("preOk") is None:
                        start["preHandled"] = m.group(1) == "1"
                        start["preOk"] = m.group(2) == "1"
                    if "=== COMPILE END ===" in cap:
                        break               # 宿主库已把面板增量落完，可以收尾了
                elif check_only and DIALOG_SEEN in cap:
                    # 编译器来要产物路径了 ⇒ 语法/语义静态检查已全过。立刻收尾。
                    start["dialogSeen"] = True
                    break
                elif not pre_mode and re.search(r"错误\(\d+\)", cap):
                    start["compileFailed"] = True
                    break
                if not check_only and expect_out and os.path.isfile(expect_out) \
                        and os.path.getmtime(expect_out) >= t0 - 1:
                    start["outSeen"] = time.time()
                    time.sleep(1.0)          # 陪 1 秒让面板/文件系统落定
                    break
                if time.time() > deadline:
                    break
                time.sleep(0.2)
        else:
            while True:
                rc = proc.poll()
                if rc is not None:
                    e_self_exit = rc
                    break
                evw.pump()
                err = evw.first_error()
                blocked = evw.blocked_dialog()
                if (err or blocked) and start["runEnd"] is None:
                    # 运行还没开始/没结束就被弹窗挡住（如「无法找到启动子程序」）：
                    # 带原文立刻收尾；若运行已自然结束才弹的框，只记录不判失败。
                    start["dialogBlocked"] = err or blocked
                    _b = start["dialogBlocked"]
                    print("   !! 模态弹窗: title='%s' msg='%s'"
                          % (_b.get("title"), _b.get("msg")))
                    break
                tr = read_text_best(trace)
                if start["runStart"] is None and "FN_END_RUN enabled: 0 -> 1" in tr:
                    start["runStart"] = time.time()
                if "发送 FN_END_RUN" in tr:
                    start["weSent"] = True
                if start["runEnd"] is None and "FN_END_RUN enabled: 1 -> 0" in tr:
                    start["runEnd"] = time.time()
                if start["runEnd"] is not None:
                    # 给 IDE 一点余量把退出码/尾行刷出来，再判是否崩
                    for _ in range(15):
                        if proc.poll() is not None:
                            e_exit_after_grace = proc.returncode
                            break
                        time.sleep(0.05)
                    break
                if time.time() > deadline:
                    break
                time.sleep(0.2)

        if start["runStart"] and start["runEnd"]:
            run_seconds = round(start["runEnd"] - start["runStart"], 2)
        elif start["runStart"]:
            run_seconds = round(time.time() - start["runStart"], 2)
        print("   观测: runStart=%s runEnd=%s weSent=%s e_self_exit=%s"
              % (bool(start["runStart"]), bool(start["runEnd"]),
                 start["weSent"], e_self_exit))
    finally:
        # 先停无头守护线程（它只动窗口样式，不影响收尾）
        if hider is not None:
            hider.stop()
        # 桥统计先于杀进程快照（杀掉后连接必断）；close 放在 e.exe 结束之后
        if bridge is not None:
            try:
                ipc_stats = bridge.stats()
            except BaseException:                                  # noqa: BLE001
                ipc_stats = None
        # 精确 PID 结束；绝不 /IM（收尾失败也不改判结果）
        try:
            if pid:
                subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
                if proc:
                    try:
                        proc.wait(timeout=10)
                    except Exception:                              # noqa: BLE001
                        pass
        except BaseException:                                      # noqa: BLE001
            pass
        if bridge is not None:
            try:
                bridge.close()
                _fin = bridge.stats()          # close 后再取一次，把收尾瞬间的帧也算进去
                if ipc_stats is not None:
                    _fin["connectedAtExit"] = ipc_stats.get("connected")
                ipc_stats = _fin
            except BaseException:                                  # noqa: BLE001
                pass
        # 清理投放的 .fne + diff 自证（尽力而为，失败只记警告）
        if os.path.exists(dst_fne) and not safe_remove(dst_fne):
            cleanup_warn.append("未能移除 lib\\%s" % LIB_FNAME)
        try:
            newf = sorted(toplist(e_dir) - before_dir)
        except BaseException:                                      # noqa: BLE001
            newf = []
        print("   install-dir 新文件: %s" % (newf or "（无）"))
        for n in newf:
            p = os.path.join(e_dir, n)
            if os.path.isfile(p):
                if safe_remove(p):
                    print("   已清理: %s" % n)
                else:
                    print("   无法清理 %s（请手动处理）" % n)
                    cleanup_warn.append("未能清理 %s" % n)
        try:
            ho.close()
        except Exception:                                          # noqa: BLE001
            pass
        if hider is not None:
            print("   无头观测: %s" % hider.summary())
            if hider._worst:
                print("   无头最坏露脸矩形: %s" % (hider._worst[:4],))
        sys.stdout, sys.stderr = old_out, old_err
    # ---------- 判定 ----------
    log_txt = read_text_best(log)
    log_bytes = os.path.getsize(log) if os.path.isfile(log) else 0
    panel_seen = "PANEL-DELTA" in log_txt or "PANEL-CHANGED" in log_txt

    # 编译器报错原文（`错误(NNNN): …`）—— 判定要用，先算出来
    errors = extract_compile_errors(log_txt) if compile_kind else []

    # 被调试程序退出码（面板里 `调试程序[pid]退出,ExitCode:N`）
    target_exit = None
    ms = re.findall(r"ExitCode:\s*(-?\d+)", log_txt)
    if ms:
        target_exit = int(ms[-1])

    e_code = e_self_exit if e_self_exit is not None else e_exit_after_grace
    if e_code is None:
        e_exit = 0                      # 我们主动关的，没崩
    else:
        e_exit = e_code & 0xFFFFFFFF

    if compile_kind:
        if check_only and pre_mode:
            # 预编译：拿 IDE 自己的返回值 `ok=N` 当主判据；只要面板里出现 `错误(NNNN)`
            # 就绝不判"通过"（宁可保守，也不给一个假的"没问题"）。
            if errors or start.get("dialogBlocked"):
                status = "syntax-error"
            elif start.get("preOk") is True:
                status = "syntax-ok"
            elif start.get("preOk") is False:
                status = "syntax-error"
            elif e_self_exit is not None:
                status = "failed"
            else:
                status = "timeout"
        elif check_only:
            # 语法检查 + 真编译：出现保存框 = 静态检查已过；面板报错 = 源码有错；
            # 错误弹窗（DLG-ERROR，如「无法找到启动子程序」）= 源码有错（原文在
            # dialogBlocked 里）；IDE 自崩 = failed；都没出现 = 超时（**不下"通过"结论**）
            if start.get("dialogSeen"):
                status = "syntax-ok"
            elif start.get("dialogBlocked") or start.get("compileFailed") or errors:
                status = "syntax-error"
            elif e_self_exit is not None:
                status = "failed"
            else:
                status = "timeout"
        elif start.get("outSeen"):
            status = "ok"
        elif start.get("dialogBlocked") or start.get("compileFailed") \
                or e_self_exit is not None:
            status = "failed"
        else:
            status = "timeout"
    elif start.get("dialogBlocked") and start["runEnd"] is None:
        # 调试运行被弹窗挡住（还没跑到/没跑完）：立刻失败并带回弹窗原文
        status = "failed"
    elif e_self_exit is not None and (e_self_exit & 0xFFFFFFFF) != 0:
        status = "crashed"
    elif start["runEnd"] and start["weSent"]:
        status = "stopped"
    elif start["runEnd"]:
        status = "ok"
    else:
        status = "timeout"

    ok = status in ("ok", "stopped", "syntax-ok")
    log_utf8 = write_utf8_view(log) if os.path.isfile(log) else None
    if os.path.isfile(trace):
        write_utf8_view(trace)

    # 有错时再从**第一条报错前 5 行**起截一段面板原文做上下文
    # （编译器的输出里，报错前后才是关键信息）
    error_tail = None
    if errors:
        lines = panel_payload_lines(log_txt)
        idx = next((i for i, ln in enumerate(lines) if COMPILE_ERR_RE.search(ln)), 0)
        error_tail = [ln for ln in lines[max(0, idx - 5):] if ln.strip()][:200]
    if check_out_made:
        safe_remove(expect_out)

    result = {
        "ok": ok,
        "status": status,
        "targetExitCode": target_exit,
        "eExitCode": e_exit,
        "runSeconds": run_seconds,
        "logFile": log,
        "panelSeen": panel_seen,
        "logBytes": log_bytes,
        "eExe": e_exe,
        "libDeployed": dst_fne,
        "tag": tag,
        "harnessFile": harness,
        "traceFile": trace,
        "logUtf8": log_utf8,
        "stopAfter": stop_after,
        "timeout": timeout,
        "cleanupWarning": cleanup_warn or None,
        "compileKind": compile_kind,
        "expectOut": expect_out,
        "checkOnly": check_only or None,
        "syntaxOk": (status == "syntax-ok") if check_only else None,
        "precompileOk": start.get("preOk"),
        "errors": errors or None,
        "errorTail": error_tail,
        "dialogBlocked": start.get("dialogBlocked"),
        "eventsFile": events,
        "eventsTail": evw.tail() or None,
        "dialogsSeen": [d.get("raw") for d in evw.dialogs[-8:]] or None,
        "headless": None if hider is None else (
            {"mode": hider.mode, "enabled": hider.enabled, "hiddenActions": hider.handled,
             "hookOk": hider.hook_ok, "hookError": hider.hook_error,
             "hookHits": hider.hook_hits, "hookLog": hider.hook_log or None,
             "polls": hider.polls, "visibleSeen": hider.seen,
             "nEnum": hider.n_enum, "nOwn": hider.n_own, "nVisOwn": hider.n_vis_own,
             "errs": hider.errs or None,
             "diag": hider.diag[:60] or None, "diagVis": hider.diag_vis[:20] or None,
             "visibleOnScreenSamples": hider.peak_visible,
             "worstRects": (hider._worst[:4] or None), "error": hider.error,
             "hideEngine": hider.engine, "pollStoppedReason": hider.poll_stopped_reason}),
    }
    if ipc_stats is not None:
        # ipc 段（仅 IPC 启用时出现）。connected = 本会话期间曾握手成功
        result["ipc"] = {
            "connected": bool(ipc_stats.get("everConnected")),
            "connectedAtExit": ipc_stats.get("connectedAtExit"),
            "framesIn": ipc_stats.get("framesIn"), "framesOut": ipc_stats.get("framesOut"),
            "decisions": ipc_stats.get("decisions"), "fallbacks": ipc_stats.get("fallbacks"),
            "p95Ms": ipc_stats.get("p95Ms"),
            "hideEngine": None if hider is None else hider.engine,
            "pollStoppedReason": None if hider is None else hider.poll_stopped_reason,
            "dialogsIn": ipc_stats.get("dialogsIn"), "eventsIn": ipc_stats.get("eventsIn"),
            "resultsIn": ipc_stats.get("resultsIn"), "badFrames": ipc_stats.get("badFrames"),
            "unknownFrames": ipc_stats.get("unknownFrames"),
            "handshakeFail": ipc_stats.get("handshakeFail"),
            "pipe": ipc_stats.get("pipe"), "auditFile": ipc_stats.get("auditFile"),
            "error": ipc_stats.get("error"),
        }
    # 结果摘要**始终**写进 harness（stderr），这样 `<TAG>_harness.txt` 里也有
    # 「异常 → 退出码」这条物证（即使面板随崩溃一起没了）。
    # ⚠️ 摘要只走 stderr —— `--json` 契约是「stdout 只放结果 JSON」。
    print("== 结果 ==", file=sys.stderr)
    print("   status=%s targetExitCode=%s eExitCode=0x%08X runSeconds=%s"
          % (status, target_exit, e_exit, run_seconds), file=sys.stderr)
    print("   panelSeen=%s logBytes=%d cleanupWarning=%s"
          % (panel_seen, log_bytes, cleanup_warn or "无"), file=sys.stderr)
    if hider is not None:
        print("   无头=%s" % hider.summary(), file=sys.stderr)
    if ipc_stats is not None:
        print("   ipc=connected:%s in:%s out:%s decisions:%s fallbacks:%s p95Ms:%s"
              % (result["ipc"]["connected"], result["ipc"]["framesIn"],
                 result["ipc"]["framesOut"], result["ipc"]["decisions"],
                 result["ipc"]["fallbacks"], result["ipc"]["p95Ms"]), file=sys.stderr)
    for e in errors:
        print("   编译错误: %s" % e, file=sys.stderr)
    if start.get("dialogBlocked"):
        _b = start["dialogBlocked"]
        print("   弹窗阻塞: title='%s' msg='%s'" % (_b.get("title"), _b.get("msg")),
              file=sys.stderr)
    return result


def main():
    ap = argparse.ArgumentParser(
        description="启动易语言 IDE 调试运行加壳后的 .e，流式收调试日志并输出结果 JSON",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ai_e", help="加壳后的 .e")
    ap.add_argument("--log", help="调试日志落盘路径（默认 <ai.e 同目录>/<名>.debug.log）")
    ap.add_argument("--timeout", type=float, default=30.0, help="整体超时秒数（默认 30）")
    ap.add_argument("--stop-after", type=float, default=None, dest="stop_after",
                    help="运行满这么多秒就主动发 FN_END_RUN（默认不主动停）")
    ap.add_argument("--json", action="store_true", dest="as_json", help="结果 JSON 打到 stdout")
    ap.add_argument("--fne", help="要投放的 .fne（默认 ../assets/elang_addin.fne）")
    ap.add_argument("--compile", dest="compile_kind",
                    choices=["precompile", "static", "static_exe", "static_console",
                             "static_dll", "exe", "console", "dll", "ecom"],
                    help="编译模式：触发官方对应功能号（不调试运行）。"
                         "precompile=官方预编译、不出产物（⚠️ 实测不可靠，只当冒烟）")
    ap.add_argument("--check", action="store_true", dest="check_only",
                    help="编译即检查：不出产物、不运行，只判源码能否通过易语言编译器"
                         "的语法/语义静态检查。权威判据 = 「弹出询问产物路径的保存框」"
                         "（编译器静态检查全过才会来要路径）。"
                         "与 --compile precompile 同用时改走官方预编译（快但不可靠）")
    ap.add_argument("--expect-out", dest="expect_out",
                    help="编译模式下期望产物路径（出现即判成功并收尾）；--check 下可省略")
    ap.add_argument("--open-target", dest="open_target",
                    help="由已加壳引导工程加载 FNE 后，用官方 FN_OPEN_FILE2 打开的真实 .e")
    ap.add_argument("--visible", action="store_true", dest="visible",
                    help="排障用：**开着窗口**跑（等价 --headless off）")
    ap.add_argument("--headless", choices=["park", "shide", "hide", "off"],
                    default=None,
                    help="窗口策略。编译默认 off，调试运行默认 hide；hide=隐藏+移出屏幕（屏幕零窗口）；"
                         "shide=同上但主窗口走 SW_HIDE 启动；park=只移出屏幕、保持可见；"
                         "off=显示窗口")
    ap.add_argument("--no-ipc", action="store_true", dest="no_ipc",
                    help="不启用宿主事件总线/双向 IPC（等价 ELANG_AI_IPC=0；行为与引入 IPC 前一致）")
    ap.add_argument("--home", help="易语言安装目录（默认 $ELANG_HOME → 注册表）")
    ap.add_argument("--tag", help="证据命名前缀（默认 <名>_<时分秒>）")
    ap.add_argument("-q", "--quiet", action="store_true", help="不打印过程")
    a = ap.parse_args()

    if a.check_only and not a.compile_kind:
        # 权威路径 = 真编译 + 保存框判据（见模块 docstring）。precompile 需显式指定。
        a.compile_kind = "static_console"
    try:
        if a.compile_kind and a.timeout == 30.0:
            a.timeout = 300.0        # 真编译（含 --check 的权威路径）都要给足
        result = elaunch(a.ai_e, a.log, a.timeout, a.stop_after, a.fne, a.home, a.tag,
                         quiet=(a.quiet or a.as_json),
                         compile_kind=a.compile_kind, expect_out=a.expect_out,
                         check_only=a.check_only,
                         headless=("off" if a.visible else a.headless),
                         open_target=a.open_target,
                         ipc=(False if a.no_ipc else None))
    except SystemExit as e:
        if a.as_json:
            print(json.dumps({"ok": False, "status": "error", "error": str(e)},
                             ensure_ascii=False))
        else:
            print("!! %s" % e, file=sys.stderr)
        return 3
    if a.as_json:
        # JSON 期间 stdout 已恢复
        sys.__stdout__.write(json.dumps(result, ensure_ascii=False) + "\n")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
