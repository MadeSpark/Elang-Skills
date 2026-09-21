#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Reliable loaded-module enumeration for the launched 32-bit e.exe (ToolHelp32).
Usage: mod2.py [project_path]
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

k = ctypes.WinDLL("kernel32", use_last_error=True)

TH32CS_SNAPMODULE = 0x00000008
TH32CS_SNAPMODULE32 = 0x00000010
MAX_PATH = 260
MAX_MODULE_NAME32 = 255


class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("th32ModuleID", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("GlblcntUsage", wintypes.DWORD),
        ("ProccntUsage", wintypes.DWORD),
        ("modBaseAddr", ctypes.POINTER(ctypes.c_byte)),
        ("modBaseSize", wintypes.DWORD),
        ("hModule", wintypes.HMODULE),
        ("szModule", wintypes.WCHAR * (MAX_MODULE_NAME32 + 1)),
        ("szExePath", wintypes.WCHAR * MAX_PATH),
    ]


def modules_toolhelp(pid):
    CreateToolhelp32Snapshot = k.CreateToolhelp32Snapshot
    CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    Module32FirstW = k.Module32FirstW
    Module32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(MODULEENTRY32W)]
    Module32FirstW.restype = wintypes.BOOL
    Module32NextW = k.Module32NextW
    Module32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(MODULEENTRY32W)]
    Module32NextW.restype = wintypes.BOOL
    CloseHandle = k.CloseHandle
    CloseHandle.argtypes = [wintypes.HANDLE]

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    if snap == wintypes.HANDLE(-1).value or snap == -1:
        print("  snapshot failed, err=", ctypes.get_last_error())
        return []
    me = MODULEENTRY32W()
    me.dwSize = ctypes.sizeof(MODULEENTRY32W)
    out = []
    ok = Module32FirstW(snap, ctypes.byref(me))
    while ok:
        out.append((me.szModule, me.szExePath, me.modBaseSize))
        ok = Module32NextW(snap, ctypes.byref(me))
    CloseHandle(snap)
    return out


def main():
    proj = sys.argv[1] if len(sys.argv) > 1 else None
    args = [E_EXE] + ([proj] if proj else [])
    print("launch:", args)
    proc = subprocess.Popen(args, cwd=E_DIR)
    pid = proc.pid
    print("pid:", pid)
    time.sleep(6)
    mods = modules_toolhelp(pid)
    print("module count:", len(mods))
    interesting = []
    for name, path, size in mods:
        low = name.lower()
        if low.endswith((".fne", ".fnr", ".fnl", ".run")) or "\\lib\\" in path.lower() or "\\plugin" in path.lower():
            interesting.append((name, path, size))
    print("\n-- support-lib / plugin modules (%d) --" % len(interesting))
    for name, path, size in interesting:
        print("   %-24s %8d  %s" % (name, size, path))
    print("\n-- all modules --")
    for name, path, size in mods:
        print("   %-24s %8d  %s" % (name, size, path))

    rc = subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
    print("\ntaskkill:", rc.returncode, rc.stdout.decode("gbk", "replace").strip())


if __name__ == "__main__":
    main()
