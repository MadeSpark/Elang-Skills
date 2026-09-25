# -*- coding: utf-8 -*-
"""列出指定 PID 的顶层窗口标题（判断 e.exe 是遗留无头实例还是用户 IDE）"""
import ctypes
import sys
from ctypes import wintypes

u32 = ctypes.WinDLL("user32", use_last_error=True)

EnumWindows = u32.EnumWindows
EnumWindows.argtypes = [ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM), wintypes.LPARAM]
GetWindowTextW = u32.GetWindowTextW
GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
GetWindowThreadProcessId = u32.GetWindowThreadProcessId
GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
IsWindowVisible = u32.IsWindowVisible
IsWindowVisible.argtypes = [wintypes.HWND]
GetClassNameW = u32.GetClassNameW
GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]

targets = set(int(a) for a in sys.argv[1:])
res = []


def cb(hwnd, lparam):
    pid = wintypes.DWORD()
    GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    if pid.value in targets:
        t = ctypes.create_unicode_buffer(512)
        GetWindowTextW(hwnd, t, 512)
        c = ctypes.create_unicode_buffer(256)
        GetClassNameW(hwnd, c, 256)
        res.append((pid.value, hwnd, c.value, bool(IsWindowVisible(hwnd)), t.value))
    return True


EnumWindows(ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)(cb), 0)
for pid, hwnd, cls, vis, title in res:
    print("pid=%-7d hwnd=0x%08X vis=%-5s cls=%-24s title=%r" % (pid, hwnd, vis, cls, title))
if not res:
    print("(这些 PID 没有任何顶层窗口 —— 疑似无头/遗留实例)")
