#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Open a project copy in e.exe (command-line), find the output panel, then kill."""
import sys
import io
import os
import time
import shutil
import ctypes
from ctypes import wintypes
import subprocess

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

E_EXE = r"D:\ides\e\e.exe"
E_DIR = r"D:\ides\e"
SRC = r"C:\Users\MadeSpark\Desktop\测试\测试.e"
DST = r"C:\Users\MadeSpark\Desktop\测试\re\probe.e"

u = ctypes.WinDLL("user32", use_last_error=True)
k = ctypes.WinDLL("kernel32", use_last_error=True)
PROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def cn(h):
    b = ctypes.create_unicode_buffer(512)
    u.GetClassNameW(h, b, 512)
    return b.value


def ti(h):
    n = u.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 2)
    u.GetWindowTextW(h, b, n + 2)
    return b.value


def pid_of(h):
    p = wintypes.DWORD()
    u.GetWindowThreadProcessId(h, ctypes.byref(p))
    return p.value


def enum_all(parent=None):
    out = []

    def cb(h, l):
        out.append(h)
        return True

    f = PROC(cb)
    if parent:
        u.EnumChildWindows(parent, f, 0)
    else:
        u.EnumWindows(f, 0)
    return out


def main():
    if not os.path.exists(DST) or os.path.getsize(DST) != os.path.getsize(SRC):
        shutil.copyfile(SRC, DST)
    print("project copy:", DST, os.path.getsize(DST), "bytes")

    proc = subprocess.Popen([E_EXE, DST], cwd=E_DIR)
    pid = proc.pid
    print("pid:", pid)
    time.sleep(8)
    if proc.poll() is not None:
        print("!! exited early", proc.poll())

    tops = [h for h in enum_all() if pid_of(h) == pid]
    print("\n--- top-level windows: ---")
    for h in tops:
        print("  0x%08X vis=%d class=%-24r title=%r" % (h, u.IsWindowVisible(h), cn(h), ti(h)))

    main = [h for h in tops if cn(h) == "ENewFrame"]
    for m in main:
        print("\n=== children of ENewFrame 0x%08X (%r) ===" % (m, ti(m)))
        for h in enum_all(m):
            kc = cn(h)
            kt = ti(h)
            print("   0x%08X vis=%d class=%-28r title=%r" % (h, u.IsWindowVisible(h), kc, kt))

    # Look for likely output/debug panel windows anywhere in the process
    print("\n=== candidate output/debug windows (title or class hints) ===")
    hints = ["输出", "调试", "Output", "Debug", "Trace", "Log", "EDIT", "RichEdit",
             "OutBar", "Tab", "输出夹"]
    for t in tops:
        for h in [t] + enum_all(t):
            if pid_of(h) != pid:
                continue
            kc = cn(h)
            kt = ti(h)
            if any(x.lower() in (kc + " " + kt).lower() for x in hints) and kt:
                print("   class=%-28r title=%r vis=%d" % (kc, kt, u.IsWindowVisible(h)))

    print("\nterminate", pid)
    rc = subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
    print("taskkill:", rc.returncode, rc.stdout.decode("gbk", "replace").strip())


if __name__ == "__main__":
    main()
