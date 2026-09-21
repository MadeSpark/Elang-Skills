#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Controlled launch of e.exe:
   - list loaded modules (to see if .fne/.fnr support libs are loaded)
   - enumerate top-level windows + children of the main frame
   - terminate ONLY the pid we launched.
Usage: run_enum2.py [project_path]
"""
import sys
import io
import time
import ctypes
from ctypes import wintypes
import subprocess

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

E_EXE = r"D:\ides\e\e.exe"
E_DIR = r"D:\ides\e"

u = ctypes.WinDLL("user32", use_last_error=True)
k = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)
psapi.EnumProcessModules.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.HMODULE),
                                     wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
psapi.EnumProcessModules.restype = wintypes.BOOL
psapi.GetModuleFileNameExW.argtypes = [wintypes.HANDLE, wintypes.HMODULE,
                                       wintypes.LPWSTR, wintypes.DWORD]
psapi.GetModuleFileNameExW.restype = wintypes.DWORD

PROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def classname(h):
    b = ctypes.create_unicode_buffer(512)
    u.GetClassNameW(h, b, 512)
    return b.value


def title(h):
    n = u.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 2)
    u.GetWindowTextW(h, b, n + 2)
    return b.value


def pid_of(h):
    p = wintypes.DWORD()
    u.GetWindowThreadProcessId(h, ctypes.byref(p))
    return p.value


def enum_top():
    out = []

    def cb(h, l):
        out.append(h)
        return True

    u.EnumWindows(PROC(cb), 0)
    return out


def enum_child(parent):
    out = []

    def cb(h, l):
        out.append(h)
        return True

    u.EnumChildWindows(parent, PROC(cb), 0)
    return out


def modules(pid):
    PROCESS_QUERY_INFORMATION = 0x0400
    PROCESS_VM_READ = 0x0010
    h = k.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
    if not h:
        return []
    try:
        arr = (wintypes.HMODULE * 1024)()
        needed = wintypes.DWORD()
        if not psapi.EnumProcessModules(h, arr, ctypes.sizeof(arr), ctypes.byref(needed)):
            return []
        n = needed.value // ctypes.sizeof(ctypes.c_void_p)
        names = []
        for i in range(n):
            buf = ctypes.create_unicode_buffer(1024)
            psapi.GetModuleFileNameExW(h, arr[i], buf, 1024)
            names.append(buf.value)
        return names
    finally:
        k.CloseHandle(h)


def main():
    proj = sys.argv[1] if len(sys.argv) > 1 else None
    args = [E_EXE] + ([proj] if proj else [])
    print("launch:", args)
    proc = subprocess.Popen(args, cwd=E_DIR)
    mypid = proc.pid
    print("pid:", mypid)
    time.sleep(6)
    if proc.poll() is not None:
        print("!! exited early, code=", proc.poll())

    mods = modules(mypid)
    print("\n=== LOADED MODULES (%d) ===" % len(mods))
    fnes = [m for m in mods if m.lower().endswith((".fne", ".fnr", ".run", ".fnl"))]
    print("support-lib modules (%d):" % len(fnes))
    for m in fnes:
        print("   ", m)
    dlls = [m for m in mods if m.lower().endswith(".dll")]
    print("dll modules (%d):" % len(dlls))
    for m in dlls:
        print("   ", m)

    tops = enum_top()
    mine = [h for h in tops if pid_of(h) == mypid]
    print("\n=== e.exe TOP-LEVEL WINDOWS (%d) ===" % len(mine))
    for h in mine:
        print("  hwnd=0x%08X vis=%d en=%d class=%-24r title=%r" % (
            h, u.IsWindowVisible(h), u.IsWindowEnabled(h), classname(h), title(h)))

    print("\n=== CHILDREN OF EACH TOP-LEVEL ===")
    for h in mine:
        cn = classname(h)
        ti = title(h)
        kids = enum_child(h)
        if not kids:
            continue
        print("  [%s] %r -> %d children" % (cn, ti, len(kids)))
        for kh in kids:
            kc = classname(kh)
            kt = title(kh)
            kv = u.IsWindowVisible(kh)
            # show only potentially interesting classes to keep output small
            interesting = ("Edit", "ListBox", "SysListView", "SysTreeView", "RichEdit",
                           "ToolbarWindow", "msvb", "输出", "Output", "Static", "Button",
                           "ComboBox", "Tab", "SBar", "Status")
            if any(s.lower() in kc.lower() for s in interesting) or kt:
                print("      hwnd=0x%08X vis=%d class=%-28r title=%r" % (kh, kv, kc, kt))

    print("\nterminate pid", mypid)
    try:
        h = k.OpenProcess(0x0001, False, mypid)
        if h:
            k.TerminateProcess(h, 0)
            k.CloseHandle(h)
    except Exception as e:  # noqa: BLE001
        print("terminate err", e)
    time.sleep(1)
    rc = subprocess.run(["taskkill", "/F", "/PID", str(mypid)], capture_output=True)
    print("taskkill:", rc.returncode, rc.stdout.decode("gbk", "replace").strip())


if __name__ == "__main__":
    main()
