#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""窗口状态追踪：盯着某个 exe 名新起的进程，把它的**每个顶层窗口的状态变化**
（新建 / 可见性翻转 / 矩形变化 / 销毁）按时刻记成日志，便于和宿主库 trace 对齐。

    python trace_windows.py e.exe <秒数> [--out 日志]

每行格式： <t>s  <事件>  hwnd=0x...  cls=<类名>  rect=(l,t,r,b)  vis=0/1  onscreen=0/1
只在状态变化时打行，因此日志长度 = 真实变化次数，可读性好。
退出时额外打印：曾「在屏且可见」的窗口清单（这才是用户能看见的）。
"""
import ctypes
import ctypes.wintypes as wt
import os
import sys
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
u32 = ctypes.WinDLL("user32", use_last_error=True)
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
winmm = ctypes.WinDLL("winmm")
psapi = ctypes.WinDLL("psapi")
k32.EnumProcesses = psapi.EnumProcesses
EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


class RECT(ctypes.Structure):
    _fields_ = [("left", wt.LONG), ("top", wt.LONG), ("right", wt.LONG), ("bottom", wt.LONG)]


def all_pids(exe_name):
    out = []
    arr = (wt.DWORD * 4096)()
    used = wt.DWORD()
    if not k32.EnumProcesses(ctypes.byref(arr), ctypes.sizeof(arr), ctypes.byref(used)):
        return out
    for i in range(used.value // ctypes.sizeof(wt.DWORD)):
        pid = arr[i]
        if pid == 0:
            continue
        h = k32.OpenProcess(0x1000 | 0x0400, False, pid)
        if not h:
            continue
        try:
            p = ctypes.create_unicode_buffer(1024)
            sz = wt.DWORD(1024)
            if k32.QueryFullProcessImageNameW(h, 0, p, ctypes.byref(sz)):
                if os.path.basename(p.value).lower() == exe_name.lower():
                    out.append(pid)
        finally:
            k32.CloseHandle(h)
    return out


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else "e.exe"
    dur = float(sys.argv[2]) if len(sys.argv) > 2 else 60.0
    outpath = None
    if "--out" in sys.argv:
        outpath = sys.argv[sys.argv.index("--out") + 1]

    lines = []
    winmm.timeBeginPeriod(1)

    base = set(all_pids(exe))
    log("== trace_windows exe=%s dur=%ss baseline=%s ==" % (exe, dur, sorted(base)), lines, outpath)

    vx, vy = u32.GetSystemMetrics(76), u32.GetSystemMetrics(77)
    vw, vh = u32.GetSystemMetrics(78), u32.GetSystemMetrics(79)
    log("   虚拟屏幕 (%d,%d)-(%d,%d)" % (vx, vy, vx + vw, vy + vh), lines, outpath)

    t0 = time.time()
    watched = set()
    state = {}          # hwnd -> 描述串
    onscreen_ever = {}  # hwnd -> (cls, rect, first_t)
    last_scan = 0.0

    while time.time() - t0 < dur:
        now = time.time() - t0
        cur = set(all_pids(exe)) - base
        if cur - watched:
            watched |= (cur - watched)
            log("   [%.3fs] 监视新进程 pid=%s" % (now, sorted(cur)), lines, outpath)
        if not watched:
            time.sleep(0.02)
            continue

        seen = set()

        def cb(hwnd, _l):
            p = wt.DWORD()
            u32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
            if p.value not in watched:
                return True
            seen.add(hwnd)
            c = ctypes.create_string_buffer(128)
            u32.GetClassNameA(hwnd, c, 128)
            cls = c.value.decode("gbk", "replace")
            t = ctypes.create_string_buffer(512)
            u32.GetWindowTextA(hwnd, t, 512)
            title = t.value.decode("gbk", "replace")
            r = RECT()
            u32.GetWindowRect(hwnd, ctypes.byref(r))
            rect = (r.left, r.top, r.right, r.bottom)
            vis = 1 if u32.IsWindowVisible(hwnd) else 0
            owned = 1 if u32.GetWindow(hwnd, 4) else 0        # GW_OWNER
            onscreen = 1 if (rect[2] > rect[0] and rect[3] > rect[1]
                             and rect[2] > vx and rect[0] < vx + vw
                             and rect[3] > vy and rect[1] < vy + vh) else 0
            desc = "cls=%-22s rect=%-24s vis=%d own=%d on=%d title=%s" % (
                cls[:22], rect, vis, owned, onscreen, title[:40])
            if state.get(hwnd) != desc:
                state[hwnd] = desc
                log("   [%7.3fs] %s hwnd=0x%08X %s" % (now, "CHG", hwnd, desc), lines, outpath)
            if vis and onscreen and hwnd not in onscreen_ever:
                onscreen_ever[hwnd] = (cls, rect, now)
            return True

        u32.EnumWindows(EnumProc(cb), 0)
        for h in list(state):
            if h not in seen:
                log("   [%7.3fs] GONE hwnd=0x%08X %s" % (now, h, state.pop(h)), lines, outpath)

        # 每 0.4s 打一条心跳，并**无视变更**地把当前「可见且在屏」的窗口全量 dump 出来，
        # 这样能排除「变更检测漏报」的可能。
        if now - last_scan > 0.4 and watched:
            last_scan = now
            live = []
            for h, d in state.items():
                if " vis=1 " in d and " on=1 " in d:
                    live.append("0x%08X %s" % (h, d))
            log("   [%7.3fs] ... 心跳 在管窗口=%d 当前可见且在屏=%d %s"
                % (now, len(state), len(live), ("| " + " || ".join(live)) if live else ""),
                lines, outpath)
        time.sleep(0.001)

    print("---- 曾「可见且在屏」的窗口（= 用户真的能看见）(%d 个) ----" % len(onscreen_ever))
    for h, (cls, rect, t) in sorted(onscreen_ever.items(), key=lambda x: x[1][2]):
        print("   [%7.3fs] 0x%08X %-24s %s" % (t, h, cls[:24], rect))
    winmm.timeEndPeriod(1)
    if outpath:
        print("日志已写 %s" % outpath)
    return 0


def log(s, lines, outpath):
    print(s)
    sys.stdout.flush()
    lines.append(s)
    if outpath:
        with open(outpath, "w", encoding="utf-8") as f:
            f.write("\n".join(lines))


if __name__ == "__main__":
    sys.exit(main())
