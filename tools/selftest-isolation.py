#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""selftest-isolation.py —— elaunch.IsolatedProcess（隐藏桌面 + 作业对象）的自测与输入延迟探针。

    python tools/selftest-isolation.py                    # 5 项自测（不碰用户输入）
    python tools/selftest-isolation.py --with-control     # 另加对照组（⚠️ 会真的卡用户键鼠数十秒）
    python tools/selftest-isolation.py latency --seconds 60   # 只测系统输入延迟（可与编译并行）

自测项：
  1. 钩子隔离   隐藏桌面里的子进程装了「永不处理消息」的全局低级键鼠钩子（模拟卡死的 IDE 插件），
                主进程在真实桌面用 SendInput 注入 200 次按键/鼠标事件，对比无子进程时的基线：
                SendInput 返回耗时的 p50 ≤ 基线 p50 + 15ms 且 p90 ≤ 基线 p90 + 40ms
                （钩子若挂在用户输入链上，每次都会等满 LowLevelHooksTimeout≈300ms，差一个数量级）。
                本机的后台程序本来就会让 p99 偶尔上百毫秒，所以不用绝对阈值。
  2. 强杀安全   中间进程持有作业，把中间进程 TerminateProcess，子进程必须在 2 秒内消失。
  3. CPU 硬上限 子进程组满负荷空转，作业 CPU 占用 ≤ 上限 + 10 个百分点（占整机）。
  4. 内存上限   子进程申请超过单进程上限的内存必须失败。
  5. UI 限制    子进程调用 SystemParametersInfo(SET) / CreateDesktop 必须失败。

latency 子命令只读：用 SendInput 注入「零位移鼠标移动」与 F24 按键，测 SendInput 耗时
（它会等全局低级钩子链处理完）与自装钩子的投递延迟，输出 JSON。
"""
from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wt
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True


def load_elaunch():
    spec = importlib.util.spec_from_file_location("elaunch_mod", os.path.join(HERE, "elaunch.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


# --------------------------------------------------------------------------- #
# 输入延迟探针
# --------------------------------------------------------------------------- #
u32 = ctypes.WinDLL("user32", use_last_error=True)
k32 = ctypes.WinDLL("kernel32", use_last_error=True)

ULONG_PTR = ctypes.c_size_t


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD), ("dwFlags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ULONG_PTR)]


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD), ("time", wt.DWORD),
                ("dwExtraInfo", ULONG_PTR)]


class _IU(ctypes.Union):
    _fields_ = [("mi", MOUSEINPUT), ("ki", KEYBDINPUT)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wt.DWORD), ("u", _IU)]


u32.SendInput.argtypes = [wt.UINT, ctypes.c_void_p, ctypes.c_int]
HOOKPROC = ctypes.WINFUNCTYPE(ctypes.c_ssize_t, ctypes.c_int, wt.WPARAM, wt.LPARAM)
u32.SetWindowsHookExW.restype = wt.HANDLE
u32.SetWindowsHookExW.argtypes = [ctypes.c_int, HOOKPROC, wt.HINSTANCE, wt.DWORD]
u32.CallNextHookEx.restype = ctypes.c_ssize_t
u32.CallNextHookEx.argtypes = [wt.HANDLE, ctypes.c_int, wt.WPARAM, wt.LPARAM]
u32.UnhookWindowsHookEx.argtypes = [wt.HANDLE]
u32.PeekMessageW.argtypes = [ctypes.c_void_p, wt.HWND, wt.UINT, wt.UINT, wt.UINT]
k32.GetModuleHandleW.restype = wt.HMODULE
k32.OpenProcess.restype = wt.HANDLE
k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.WaitForSingleObject.argtypes = [wt.HANDLE, wt.DWORD]
k32.CloseHandle.argtypes = [wt.HANDLE]


def now_ms() -> float:
    return time.perf_counter() * 1000.0


def send_mouse0():
    i = INPUT(type=0)
    i.u.mi = MOUSEINPUT(0, 0, 0, 0x0001, 0, 0)      # MOUSEEVENTF_MOVE, 零位移
    return u32.SendInput(1, ctypes.byref(i), ctypes.sizeof(INPUT))


def send_f24(up: bool):
    i = INPUT(type=1)
    i.u.ki = KEYBDINPUT(0x87, 0, 0x0002 if up else 0, 0, 0)    # VK_F24
    return u32.SendInput(1, ctypes.byref(i), ctypes.sizeof(INPUT))


def measure_latency(seconds: float, interval_ms: float = 25.0, count: int | None = None) -> dict:
    """返回 SendInput 耗时与自装钩子投递延迟的统计（毫秒）。"""
    sent_at = {"t": 0.0}
    hook_delays: list[float] = []
    ready = threading.Event()
    stop = threading.Event()

    def hook_thread():
        def _proc(n, w, l):
            if n >= 0 and sent_at["t"]:
                hook_delays.append(now_ms() - sent_at["t"])
            return u32.CallNextHookEx(None, n, w, l)
        proc = HOOKPROC(_proc)
        hm = u32.SetWindowsHookExW(14, proc, k32.GetModuleHandleW(None), 0)      # WH_MOUSE_LL
        hk = u32.SetWindowsHookExW(13, proc, k32.GetModuleHandleW(None), 0)      # WH_KEYBOARD_LL
        ready.set()
        msg = wt.MSG()
        while not stop.is_set():
            if u32.PeekMessageW(ctypes.byref(msg), None, 0, 0, 1):
                u32.TranslateMessage(ctypes.byref(msg))
                u32.DispatchMessageW(ctypes.byref(msg))
            else:
                time.sleep(0.0005)
        for h in (hm, hk):
            if h:
                u32.UnhookWindowsHookEx(h)

    th = threading.Thread(target=hook_thread, daemon=True)
    th.start()
    ready.wait(3)
    send_ms: list[float] = []
    slow: list = []                      # (墙钟 HH:MM:SS.mmm, SendInput 耗时 ms)：>100ms 的慢事件
    t_end = time.time() + seconds
    n = 0
    k_up = False
    while (count is None and time.time() < t_end) or (count is not None and n < count):
        sent_at["t"] = now_ms()
        t0 = now_ms()
        if n % 2 == 0:
            send_mouse0()
        else:
            send_f24(k_up)
            k_up = not k_up
        dt_ms = now_ms() - t0
        send_ms.append(dt_ms)
        if dt_ms > 100 and len(slow) < 200:
            slow.append((time.strftime("%H:%M:%S") + ".%03d" % (int(time.time() * 1000) % 1000),
                         round(dt_ms, 1)))
        n += 1
        time.sleep(interval_ms / 1000.0)
    stop.set()
    th.join(2)

    def pct(a, p):
        if not a:
            return None
        a = sorted(a)
        return round(a[min(len(a) - 1, int(len(a) * p / 100.0))], 2)

    def stats(a):
        return {"n": len(a), "p50": pct(a, 50), "p90": pct(a, 90), "p99": pct(a, 99),
                "max": round(max(a), 2) if a else None,
                "over100ms": sum(1 for x in a if x > 100), "over150ms": sum(1 for x in a if x > 150)}
    return {"sendInput": stats(send_ms), "hookDelivery": stats(hook_delays), "slowEvents": slow}


# --------------------------------------------------------------------------- #
# 自测
# --------------------------------------------------------------------------- #
RESULTS: list = []


def check(name: str, ok: bool, detail: str):
    RESULTS.append((name, ok, detail))
    print("[%s] %s  %s" % ("PASS" if ok else "FAIL", name, detail), flush=True)


def py() -> str:
    return sys.executable


HOOK_CHILD = r"""
import ctypes, ctypes.wintypes as wt, time
u=ctypes.WinDLL('user32',use_last_error=True); k=ctypes.WinDLL('kernel32')
P=ctypes.WINFUNCTYPE(ctypes.c_ssize_t,ctypes.c_int,wt.WPARAM,wt.LPARAM)
u.SetWindowsHookExW.restype=wt.HANDLE
u.SetWindowsHookExW.argtypes=[ctypes.c_int,P,wt.HINSTANCE,wt.DWORD]
u.CallNextHookEx.restype=ctypes.c_ssize_t
u.CallNextHookEx.argtypes=[wt.HANDLE,ctypes.c_int,wt.WPARAM,wt.LPARAM]
k.GetModuleHandleW.restype=wt.HMODULE
def stuck(n,w,l):
    time.sleep(2)            # 模拟「卡死的 IDE 插件钩子」
    return u.CallNextHookEx(None,n,w,l)
cb=P(stuck)
h1=u.SetWindowsHookExW(13,cb,k.GetModuleHandleW(None),0)
h2=u.SetWindowsHookExW(14,cb,k.GetModuleHandleW(None),0)
print('hooks',bool(h1),bool(h2),flush=True)
# 不处理消息：一旦有输入路由到这个桌面，钩子就会把输入链拖住
time.sleep(120)
"""


def new_proc(el, argv, desktop=True, env_extra=None, stdout=None):
    env = os.environ.copy()
    env.update(env_extra or {})
    return el.IsolatedProcess(argv, cwd=os.getcwd(), env=env, stdout=stdout, desktop=desktop,
                              tag="selftest", show=5).start()


def read_out(f) -> str:
    f.flush()
    f.seek(0)
    return f.read().decode("utf-8", "replace").strip()


def test_hook_isolation(el, with_control: bool):
    base = measure_latency(0, interval_ms=10, count=200)["sendInput"]
    out = tempfile.TemporaryFile()
    p = new_proc(el, [py(), "-c", HOOK_CHILD], desktop=True, stdout=out)
    try:
        time.sleep(1.5)
        hooks_txt = read_out(out)
        res = measure_latency(0, interval_ms=10, count=200)
        s = res["sendInput"]
        ok = ("hooks True True" in hooks_txt and s["p50"] is not None
              and s["p50"] <= base["p50"] + 15 and s["p90"] <= base["p90"] + 40)
        check("1. 钩子隔离（隐藏桌面里有卡死的全局钩子）", ok,
              "child=%r 基线 p50/p90/p99=%s/%s/%sms；隔离后 p50/p90/p99/max=%s/%s/%s/%sms (n=%d)"
              % (hooks_txt, base["p50"], base["p90"], base["p99"], s["p50"], s["p90"], s["p99"],
                 s["max"], s["n"]))
    finally:
        p.kill()
        p.wait(5)
        p.close()
    if with_control:
        print("   ⚠️ 对照组：同样的卡死钩子挂在真实桌面，会拖住你的键鼠，约 30 秒……", flush=True)
        p = new_proc(el, [py(), "-c", HOOK_CHILD], desktop=False)
        try:
            time.sleep(1.5)
            res = measure_latency(0, interval_ms=10, count=15)
            s = res["sendInput"]
            check("1c. 对照组（无隔离，预期明显变慢）", s["max"] is not None and s["max"] > 150,
                  "SendInput max=%sms p99=%sms" % (s["max"], s["p99"]))
        finally:
            p.kill()
            p.wait(5)
            p.close()


def test_kill_safety(el):
    mid = r"""
import os, sys, time
sys.path.insert(0, %r)
import elaunch
p = elaunch.IsolatedProcess([sys.executable, '-c', 'import time; time.sleep(300)'],
                            cwd=os.getcwd(), env=dict(os.environ), desktop=True, tag='mid').start()
print(p.pid, flush=True)
time.sleep(300)
""" % HERE
    m = subprocess.Popen([py(), "-B", "-c", mid], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    try:
        line = m.stdout.readline().decode().strip()
        cpid = int(line)
        time.sleep(0.5)
        h = k32.OpenProcess(0x00100000 | 0x1000, False, cpid)       # SYNCHRONIZE | QUERY_LIMITED
        alive_before = bool(h) and k32.WaitForSingleObject(h, 0) == 0x102
        t0 = time.time()
        m.kill()                                                    # = TerminateProcess，finally 不会执行
        gone = False
        while time.time() - t0 < 5 and h:
            if k32.WaitForSingleObject(h, 100) == 0:
                gone = True
                break
        dt = time.time() - t0
        if h:
            k32.CloseHandle(h)
        check("2. 强杀安全（中间进程被 TerminateProcess）", alive_before and gone and dt < 2.0,
              "子进程存活前=%s 消失=%s 用时=%.2fs" % (alive_before, gone, dt))
    finally:
        try:
            m.kill()
        except OSError:
            pass


CPU_CHILD = r"""
import subprocess, sys, os, time
n = int(sys.argv[1])
ps = [subprocess.Popen([sys.executable, '-c', 'while True: pass']) for _ in range(n)]
time.sleep(60)
"""


def test_cpu_cap(el):
    pct = 25
    ncpu = os.cpu_count() or 4
    workers = min(max(4, ncpu), 24)
    p = new_proc(el, [py(), "-c", CPU_CHILD, str(workers)], desktop=True,
                 env_extra={"ELANG_AI_CPU_PCT": str(pct)})
    try:
        time.sleep(2.0)
        s0 = p.stats().get("cpuSeconds", 0.0)
        t0 = time.time()
        time.sleep(6.0)
        s1 = p.stats().get("cpuSeconds", 0.0)
        dt = time.time() - t0
        used = (s1 - s0) / (dt * ncpu) * 100.0
        check("3. CPU 硬上限 %d%%" % pct, p.limits.get("cpuPct") == pct and used <= pct + 10,
              "整机占用 %.1f%%（%d 个满载进程，%d 逻辑核）" % (used, workers, ncpu))
    finally:
        p.kill()
        p.wait(5)
        p.close()


MEM_CHILD = r"""
try:
    b = bytearray(1536 * 1024 * 1024)
    print('ALLOC_OK')
except MemoryError:
    print('MEMORY_ERROR')
"""


def test_mem_cap(el):
    out = tempfile.TemporaryFile()
    p = new_proc(el, [py(), "-c", MEM_CHILD], desktop=True,
                 env_extra={"ELANG_AI_PROC_MEM_MIB": "1024", "ELANG_AI_JOB_MEM_MIB": "2048"}, stdout=out)
    try:
        try:
            p.wait(30)
        except subprocess.TimeoutExpired:
            pass
        txt = read_out(out)
        check("4. 内存上限（单进程 1024MiB，申请 1536MiB）",
              "MEMORY_ERROR" in txt or (p.returncode not in (None, 0) and "ALLOC_OK" not in txt),
              "输出=%r 退出码=%s" % (txt, p.returncode))
    finally:
        p.kill()
        p.close()


UI_CHILD = r"""
import ctypes, ctypes.wintypes as wt
u = ctypes.WinDLL('user32', use_last_error=True)
cur = wt.BOOL()
u.SystemParametersInfoW(0x1, 0, ctypes.byref(cur), 0)      # SPI_GETBEEP
ok_set = u.SystemParametersInfoW(0x2, int(cur.value), None, 0)   # SPI_SETBEEP 写回同一个值
err_set = ctypes.get_last_error()
u.CreateDesktopW.restype = wt.HANDLE
h = u.CreateDesktopW('elang_ai_ui_probe', None, None, 0, 0x10000000, None)
err_desk = ctypes.get_last_error()
print('SPI_SET_OK=%s err=%s CREATEDESKTOP_OK=%s err=%s' % (bool(ok_set), err_set, bool(h), err_desk))
"""


def test_ui_limits(el):
    out = tempfile.TemporaryFile()
    p = new_proc(el, [py(), "-c", UI_CHILD], desktop=True, stdout=out)
    try:
        try:
            p.wait(20)
        except subprocess.TimeoutExpired:
            pass
        txt = read_out(out)
        check("5. UI 限制（改系统参数 / 建桌面必须失败）",
              "SPI_SET_OK=False" in txt and "CREATEDESKTOP_OK=False" in txt, txt or "（无输出）")
    finally:
        p.kill()
        p.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", nargs="?", choices=["run", "latency"], default="run")
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--interval-ms", type=float, default=25.0)
    ap.add_argument("--with-control", action="store_true",
                    help="对照组：同样的卡死钩子放真实桌面（⚠️ 会卡用户键鼠约 30 秒）")
    a = ap.parse_args()
    if sys.platform != "win32":
        print("仅支持 Windows")
        return 2
    if a.cmd == "latency":
        res = measure_latency(a.seconds, a.interval_ms)
        print(json.dumps(res, ensure_ascii=False))
        return 0
    el = load_elaunch()
    test_hook_isolation(el, a.with_control)
    test_kill_safety(el)
    test_cpu_cap(el)
    test_mem_cap(el)
    test_ui_limits(el)
    bad = [r for r in RESULTS if not r[1]]
    print("\n合计: PASS %d / FAIL %d" % (len(RESULTS) - len(bad), len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
