#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""最小验证：CreateDesktop + STARTUPINFO.lpDesktop 能否把子进程的窗口隔离到独立桌面。

    python probe_desktop_min.py [exe] [-- 其余参数]

默认用 mmc.exe（经典 Win32 GUI，会创建 MMCMainFrame）。
⚠️ 别用 notepad.exe / mspaint.exe 这类「现代应用」：它们由 broker 进程代开窗口，
   窗口不属于我们启动的那个 pid，会让探测得出错误结论。
"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
user32 = ctypes.WinDLL("user32", use_last_error=True)

CreateDesktopW = user32.CreateDesktopW
CreateDesktopW.restype = wt.HANDLE
CreateDesktopW.argtypes = [wt.LPCWSTR, wt.LPCWSTR, ctypes.c_void_p, wt.DWORD, wt.DWORD, ctypes.c_void_p]
CloseDesktop = user32.CloseDesktop
CloseDesktop.argtypes = [wt.HANDLE]
EnumDesktopWindows = user32.EnumDesktopWindows
EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
GetWindowThreadProcessId = user32.GetWindowThreadProcessId
IsWindowVisible = user32.IsWindowVisible
GetClassNameA = user32.GetClassNameA

DESKTOP_ALL = 0x000F01FF


def count(pid, hdesk=None):
    n = [0]

    @EnumWindowsProc
    def cb(hwnd, _):
        p = wt.DWORD()
        GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid and IsWindowVisible(hwnd):
            c = ctypes.create_string_buffer(128)
            GetClassNameA(hwnd, c, 128)
            n[0] += 1
            print("        hwnd=0x%08X %s" % (hwnd, c.value.decode("gbk", "replace")))
        return True

    if hdesk:
        EnumDesktopWindows(hdesk, cb, 0)
    else:
        user32.EnumWindows(cb, 0)
    return n[0]


name = "ElangAITestDesk"
ctypes.set_last_error(0)
h = CreateDesktopW(name, None, None, 0, DESKTOP_ALL, None)
print("CreateDesktopW('%s') -> %r  GetLastError=%d" % (name, h, ctypes.get_last_error()))
if not h:
    sys.exit(1)

si = subprocess.STARTUPINFO()
si.lpDesktop = name
print("si.lpDesktop = %r" % si.lpDesktop)
target = sys.argv[1] if len(sys.argv) > 1 else r"C:\Windows\System32\mmc.exe"
argv = [target] + sys.argv[2:]
print("target = %r" % argv)
p = subprocess.Popen(argv, startupinfo=si,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print("pid = %d" % p.pid)
time.sleep(3.5)
print("  独立桌面(ElangAITestDesk) 可见窗口数:")
n_ai = count(p.pid, h)
print("  用户默认桌面 可见窗口数:")
n_user = count(p.pid, None)
print("---- 结论 ----")
print("  独立桌面 : %d" % n_ai)
print("  用户桌面 : %d" % n_user)
subprocess.run(["taskkill", "/F", "/PID", str(p.pid)], capture_output=True)
CloseDesktop(h)
