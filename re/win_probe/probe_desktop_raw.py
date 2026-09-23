#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""用原生 CreateProcessW（自带 lpDesktop）验证「独立桌面」是否真的能隔离窗口。

    python probe_desktop_raw.py [exe] [seconds]

⚠️ 关键坑：Python 的 `subprocess.STARTUPINFO` **不暴露 `lpDesktop`** ——
   `si.lpDesktop = "X"` 只是给 Python 对象加了个没人读的属性，子进程照样在默认桌面上。
   必须自己用 ctypes 建 STARTUPINFOA 调 CreateProcessW。
"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

u32 = ctypes.WinDLL("user32", use_last_error=True)
k32 = ctypes.WinDLL("kernel32", use_last_error=True)


class STARTUPINFOA(ctypes.Structure):
    _fields_ = [("cb", wt.DWORD), ("lpReserved", wt.LPSTR), ("lpDesktop", wt.LPSTR),
                ("lpTitle", wt.LPSTR), ("dwX", wt.DWORD), ("dwY", wt.DWORD),
                ("dwXSize", wt.DWORD), ("dwYSize", wt.DWORD), ("dwXCountChars", wt.DWORD),
                ("dwYCountChars", wt.DWORD), ("dwFillAttribute", wt.DWORD),
                ("dwFlags", wt.DWORD), ("wShowWindow", wt.WORD), ("cbReserved2", wt.WORD),
                ("lpReserved2", ctypes.POINTER(ctypes.c_byte)),
                ("hStdInput", wt.HANDLE), ("hStdOutput", wt.HANDLE), ("hStdError", wt.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [("hProcess", wt.HANDLE), ("hThread", wt.HANDLE),
                ("dwProcessId", wt.DWORD), ("dwThreadId", wt.DWORD)]


CreateDesktopW = u32.CreateDesktopW
CreateDesktopW.restype = wt.HANDLE
CreateDesktopW.argtypes = [wt.LPCWSTR, wt.LPCWSTR, ctypes.c_void_p, wt.DWORD, wt.DWORD, ctypes.c_void_p]
CloseDesktop = u32.CloseDesktop
CloseDesktop.argtypes = [wt.HANDLE]
CreateProcessW = k32.CreateProcessW
CreateProcessW.restype = wt.BOOL
CreateProcessW.argtypes = [wt.LPCWSTR, wt.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, wt.BOOL,
                           wt.DWORD, ctypes.c_void_p, wt.LPCWSTR,
                           ctypes.POINTER(STARTUPINFOA), ctypes.POINTER(PROCESS_INFORMATION)]
EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
EnumDesktopWindows = u32.EnumDesktopWindows
GetWindowThreadProcessId = u32.GetWindowThreadProcessId
IsWindowVisible = u32.IsWindowVisible
GetClassNameA = u32.GetClassNameA

DESKTOP_ALL = 0x000F01FF
INFINITE = 0xFFFFFFFF


def visible_windows(pid, hdesk=None):
    """pid=None → 不按 pid 过滤（用于看整张桌面上到底有没有窗口）。"""
    out = []

    @EnumWindowsProc
    def cb(hwnd, _):
        p = wt.DWORD()
        GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if (pid is None or p.value == pid) and IsWindowVisible(hwnd):
            c = ctypes.create_string_buffer(128)
            GetClassNameA(hwnd, c, 128)
            out.append((hwnd, c.value.decode("gbk", "replace")))
        return True

    if hdesk:
        EnumDesktopWindows(hdesk, cb, 0)
    else:
        u32.EnumWindows(cb, 0)
    return out


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else r"C:\Windows\System32\winver.exe"
    extra = [a for a in sys.argv[2:] if not a.replace(".", "").isdigit()]
    dur = float(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2].replace(".", "").isdigit() else 6.0
    dname = "ElangAIHeadless"

    hdesk = u32.OpenDesktopW(dname, 0, False, DESKTOP_ALL)
    made = False
    if not hdesk:
        hdesk = CreateDesktopW(dname, None, None, 0, DESKTOP_ALL, None)
        made = True
    print("desktop=%r handle=%r created=%s err=%d" % (dname, hdesk, made, ctypes.get_last_error()))
    if not hdesk:
        return 1

    si = STARTUPINFOA()
    si.cb = ctypes.sizeof(si)
    si.lpDesktop = dname.encode("mbcs")
    si.dwFlags = 0x00000001          # STARTF_USESHOWWINDOW
    si.wShowWindow = 1               # SW_SHOWNORMAL（故意：靠桌面隔离，不靠隐藏）
    pi = PROCESS_INFORMATION()
    parts = ['"%s"' % exe] + ['"%s"' % a for a in extra]
    cmdline = ctypes.create_unicode_buffer(" ".join(parts))
    print("cmdline=%r" % " ".join(parts))
    ok = CreateProcessW(None, cmdline, None, None, False, 0, None, None,
                        ctypes.byref(si), ctypes.byref(pi))
    print("CreateProcessW -> %s err=%d pid=%d" % (bool(ok), ctypes.get_last_error(), pi.dwProcessId))
    if not ok:
        CloseDesktop(hdesk)
        return 1

    pid = pi.dwProcessId
    seen_user, seen_ai, all_ai = [], [], []
    t0 = time.time()
    try:
        while time.time() - t0 < dur:
            if k32.WaitForSingleObject(pi.hProcess, 0) == 0:
                print("   [%.2fs] 进程已退出" % (time.time() - t0))
                break
            for hwnd, cls in visible_windows(pid):
                if cls not in [c for c, _ in seen_user]:
                    seen_user.append((cls, hwnd))
                    print("   [%.2fs] !! 用户桌面可见 %s" % (time.time() - t0, cls))
            for hwnd, cls in visible_windows(pid, hdesk):
                if cls not in [c for c, _ in seen_ai]:
                    seen_ai.append((cls, hwnd))
                    print("   [%.2fs]    独立桌面可见 %s" % (time.time() - t0, cls))
            for hwnd, cls in visible_windows(None, hdesk):
                if cls not in [c for c, _ in all_ai]:
                    all_ai.append((cls, hwnd))
                    print("   [%.2fs]    独立桌面(任意pid)可见 %s" % (time.time() - t0, cls))
            time.sleep(0.02)
    finally:
        if k32.WaitForSingleObject(pi.hProcess, 0) != 0:
            k32.TerminateProcess(pi.hProcess, 0)
        k32.CloseHandle(pi.hThread)
        k32.CloseHandle(pi.hProcess)

    print("---- 结论 ----")
    print("   用户桌面可见 : %d %s" % (len(seen_user), [c for c, _ in seen_user]))
    print("   独立桌面可见 : %d %s" % (len(seen_ai), [c for c, _ in seen_ai]))
    print("   独立桌面任意pid可见 : %d %s" % (len(all_ai), [c for c, _ in all_ai]))
    CloseDesktop(hdesk)
    return 0


if __name__ == "__main__":
    sys.exit(main())
