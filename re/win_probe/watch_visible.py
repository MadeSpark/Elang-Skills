#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""第三方取证：独立进程监视「某个 exe 名的新进程」的顶层窗口，判定是否曾在屏幕上露脸。

    python watch_visible.py <exe名> <秒数> [--tag 前缀]

判据（每 1ms 采样，独立于被测程序自己的统计）：
    窗口 WS_VISIBLE 且 宽高>0 且 与虚拟屏幕矩形有交集   →  记一次「露脸」
退出时打印：露脸次数、每次的 [时刻, 类名, 矩形]，以及全程出现过的窗口类清单。
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
        h = k32.OpenProcess(0x1000 | 0x0400, False, pid)   # QUERY_LIMITED | QUERY_INFORMATION
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
    tag = sys.argv[3] if len(sys.argv) > 3 else exe

    winmm.timeBeginPeriod(1)

    base = set(all_pids(exe))
    print("== watch_visible exe=%s dur=%ss baseline_pids=%s ==" % (exe, dur, sorted(base)))

    vx, vy = u32.GetSystemMetrics(76), u32.GetSystemMetrics(77)
    vw, vh = u32.GetSystemMetrics(78), u32.GetSystemMetrics(79)

    t0 = time.time()
    watched = set()
    hits = []
    classes = {}
    while time.time() - t0 < dur:
        cur = set(all_pids(exe)) - base
        if cur - watched:
            watched |= (cur - watched)
            print("   [%.2fs] 开始监视新进程 pid=%s" % (time.time() - t0, sorted(cur - (watched - cur) or cur)))
        if not watched:
            time.sleep(0.02)
            continue

        def cb(hwnd, _l):
            p = wt.DWORD()
            u32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
            if p.value not in watched:
                return True
            c = ctypes.create_string_buffer(128)
            u32.GetClassNameA(hwnd, c, 128)
            cls = c.value.decode("gbk", "replace")
            classes[cls] = classes.get(cls, 0) + 1
            if not u32.IsWindowVisible(hwnd):
                return True
            r = RECT()
            u32.GetWindowRect(hwnd, ctypes.byref(r))
            if r.right <= r.left or r.bottom <= r.top:
                return True
            if (r.right <= vx or r.left >= vx + vw or r.bottom <= vy or r.top >= vy + vh):
                return True                     # 完全在屏幕外 → 不算露脸
            hits.append((round(time.time() - t0, 3), cls, (r.left, r.top, r.right, r.bottom)))
            return True

        u32.EnumWindows(EnumProc(cb), 0)
        time.sleep(0.001)

    print("---- 取证结果 (%s) ----" % tag)
    print("   露脸次数: %d" % len(hits))
    for t, cls, rect in hits[:20]:
        print("      [%.3fs] %-26s %s" % (t, cls[:26], rect))
    print("   出现过的窗口类: %s" % ", ".join(sorted(classes)))
    winmm.timeEndPeriod(1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
