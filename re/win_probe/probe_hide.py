#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""探测：e.exe 启动期间，属于它的顶层可见窗口在什么时候出现、STARTUPINFO/SW_HIDE 能否压住。

用法：
    python probe_hide.py <show|hide> [秒数]

输出每条「窗口可见性变化」的时间线，末尾给结论（是否出现过可见窗口、首次出现时刻）。
"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

EnumWindows = user32.EnumWindows
EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
GetWindowThreadProcessId = user32.GetWindowThreadProcessId
IsWindowVisible = user32.IsWindowVisible
GetClassNameA = user32.GetClassNameA
GetWindowTextA = user32.GetWindowTextA
GetWindowRect = user32.GetWindowRect
GetWindow = user32.GetWindow
GW_OWNER = 4

E_EXE = r"D:\ides\e\e.exe"
E_DIR = r"D:\ides\e"
PROJ = r"C:\Users\MadeSpark\Desktop\测试\re\up_test\console_proj.e"

SW_HIDE = 0
SW_SHOWNORMAL = 1


def top_windows(pid):
    """当前进程的顶层窗口 [(hwnd, class, title, visible, rect, owner)]"""
    out = []

    @EnumWindowsProc
    def cb(hwnd, _):
        p = wt.DWORD()
        GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value != pid:
            return True
        cls = ctypes.create_string_buffer(256)
        GetClassNameA(hwnd, cls, 256)
        ttl = ctypes.create_string_buffer(512)
        GetWindowTextA(hwnd, ttl, 512)
        r = wt.RECT()
        GetWindowRect(hwnd, ctypes.byref(r))
        out.append((hwnd, cls.value.decode("gbk", "replace"), ttl.value.decode("gbk", "replace"),
                    bool(IsWindowVisible(hwnd)), (r.left, r.top, r.right, r.bottom),
                    GetWindow(hwnd, GW_OWNER)))
        return True

    EnumWindows(cb, 0)
    return out


def main():
    mode = (sys.argv[1] if len(sys.argv) > 1 else "hide").lower()
    dur = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0

    si = subprocess.STARTUPINFO()
    if mode == "hide":
        si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        si.wShowWindow = SW_HIDE

    print("== probe_hide mode=%s dur=%ss ==" % (mode, dur))
    t0 = time.time()
    proc = subprocess.Popen([E_EXE, PROJ], cwd=E_DIR, startupinfo=si,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pid = proc.pid
    print("   pid = %d" % pid)

    seen = {}            # hwnd -> last state
    vis_events = []      # (t, hwnd, class, title, visible, rect)
    first_visible = None
    ever_visible = False

    try:
        while time.time() - t0 < dur:
            if proc.poll() is not None:
                print("   [%.3fs] 进程退出 rc=%s" % (time.time() - t0, proc.returncode))
                break
            for hwnd, cls, ttl, vis, rect, owner in top_windows(pid):
                key = hwnd
                prev = seen.get(key)
                cur = (vis, rect, owner)
                if prev != cur:
                    seen[key] = cur
                    el = time.time() - t0
                    kind = "owner=%d" % owner if owner else "owner=-"
                    print("   [%.3fs] hwnd=0x%08X %-28s visible=%d rect=%s %s %r"
                          % (el, hwnd, cls[:28], 1 if vis else 0, rect, kind, ttl[:30]))
                    if vis:
                        vis_events.append(el)
                        ever_visible = True
                        if first_visible is None:
                            first_visible = el
            time.sleep(0.005)
    finally:
        if proc.poll() is None:
            subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
        for h in (proc.stdout, proc.stderr):
            if h:
                h.close()

    print("---- 结论 ----")
    print("   期间出现过可见顶层窗口 : %s" % ("是" if ever_visible else "否"))
    if first_visible is not None:
        print("   首次可见时刻           : %.3fs" % first_visible)
        print("   可见事件数             : %d" % len(vis_events))
    print("   全程顶层窗口数         : %d" % len(seen))
    for hwnd, (vis, rect, owner) in seen.items():
        cls = ctypes.create_string_buffer(256)
        GetClassNameA(hwnd, cls, 256)
        print("      0x%08X %s visible=%d" % (hwnd, cls.value.decode("gbk", "replace"), 1 if vis else 0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
