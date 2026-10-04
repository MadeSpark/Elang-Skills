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

    def __init__(self, pid: int, mode: str = "hide", verbose=None):
        self.pid = pid
        self.mode = mode if mode in self.MODES else "hide"
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

        while not self._stop.is_set():
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

        try:
            ctypes.WinDLL("winmm").timeEndPeriod(1)
        except BaseException:                                      # noqa: BLE001
            pass

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
        return ("mode=%s 动作 %d 次（事件通道 %d / 轮询 %d；轮询 %d 次见到可见窗口 %d 个）；"
                "采样到「可见且与屏幕相交」%d 次"
                % (self.mode, self.handled, self.hook_hits, self.handled - self.hook_hits,
                   self.polls, self.seen, self.peak_visible))


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
            open_target: str = None) -> dict:
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

    # `--check` 走真编译时（`--check --compile <种类>`）不关心产物，但 IDE 的保存框必须
    # **有东西可填**才会被宿主库消掉（模态框不消掉，触发线程就永远不返回）。所以给个临时
    # 路径，用完删掉。预编译模式没有保存框，不需要。
    check_out_made = False
    if check_only and not pre_mode and not expect_out:
        expect_out = os.path.join(log_dir, "%s_checkout.exe" % tag)
        check_out_made = True

    # 只清**同名 tag** 旧证据，不碰别次运行
    for f in (log, trace, harness, events):
        if os.path.exists(f):
            safe_remove(f)

    tee = Tee(harness)
    old_out, old_err = sys.stdout, sys.stderr
    sys.stdout = sys.stderr = tee
    pid = None
    proc = None
    ho = None
    hider = None
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
        hider = WindowHider(pid, mode=hl_mode).start()
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
             "worstRects": (hider._worst[:4] or None), "error": hider.error}),
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
                         open_target=a.open_target)
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
