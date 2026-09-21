#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Controlled single launch of e.exe: enumerate its windows, then kill it.
Read-only w.r.t. files. Does NOT let e.exe persist.
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

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

EnumWindows = user32.EnumWindows
EnumChildWindows = user32.EnumChildWindows
GetWindowThreadProcessId = user32.GetWindowThreadProcessId
GetClassNameA = user32.GetClassNameA
GetWindowTextA = user32.GetWindowTextA
GetWindowTextLengthA = user32.GetWindowTextLengthA
IsWindowVisible = user32.IsWindowVisible
IsWindowEnabled = user32.IsWindowEnabled

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

# Optional: DPI awareness so class names are as the app made them (irrelevant but harmless)
try:
    ctypes.WinDLL("shcore").SetProcessDpiAwareness(2)
except Exception:  # noqa: BLE001
    pass


def classname(hwnd):
    buf = ctypes.create_unicode_buffer(512)
    GetClassNameA(hwnd, buf, 512)
    return buf.value


def title(hwnd):
    ln = GetWindowTextLengthA(hwnd)
    buf = ctypes.create_string_buffer(ln + 2)
    GetWindowTextA(hwnd, buf, ln + 2)
    try:
        return buf.value.decode("gbk", errors="replace")
    except Exception:  # noqa: BLE001
        return buf.value.decode("latin-1", errors="replace")


def pid_of(hwnd):
    p = wintypes.DWORD()
    GetWindowThreadProcessId(hwnd, ctypes.byref(p))
    return p.value


def collect():
    rows = []

    def top_cb(hwnd, lparam):
        rows.append((hwnd, 0, pid_of(hwnd), classname(hwnd), title(hwnd),
                     bool(IsWindowVisible(hwnd)), bool(IsWindowEnabled(hwnd))))
        return True

    cb = WNDENUMPROC(top_cb)
    EnumWindows(cb, 0)
    return rows, cb


def children_of(parent):
    rows = []

    def child_cb(hwnd, lparam):
        rows.append(hwnd)
        return True

    cb = WNDENUMPROC(child_cb)
    EnumChildWindows(parent, cb, 0)
    cb  # keep ref
    return rows


def main():
    # record pre-existing e.exe PIDs so we never touch them
    pre = subprocess.run(["tasklist", "/FI", "IMAGENAME eq e.exe", "/FO", "CSV", "/NH"],
                         capture_output=True)
    pre_txt = pre.stdout.decode("gbk", "replace")
    pre_pids = set()
    for line in pre_txt.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        for p in parts:
            if p.isdigit():
                pre_pids.add(int(p))
    print("pre-existing e.exe pids:", sorted(pre_pids))

    print("launching:", E_EXE)
    proc = subprocess.Popen([E_EXE], cwd=E_DIR)
    print("pid:", proc.pid)
    time.sleep(5)
    rc0 = proc.poll()
    if rc0 is not None:
        print("!! launched process already exited, code=%s (possible single-instance / license gate)" % rc0)

    rows, cb = collect()
    mypid = proc.pid
    print("\n=== ALL top-level windows (pid filter marked) ===")
    for hwnd, depth, pid, cn, ti, vis, en in rows:
        mark = "***E***" if pid == mypid else "       "
        print("%s hwnd=0x%08X pid=%-6d vis=%d en=%d class=%-30s title=%r" % (
            mark, hwnd, pid, vis, en, cn, ti))

    print("\n=== windows belonging to e.exe pid=%d ===" % mypid)
    mine = [r for r in rows if r[2] == mypid]
    if not mine:
        print("  (none found at top level)")
    for hwnd, depth, pid, cn, ti, vis, en in mine:
        print("  top hwnd=0x%08X class=%-30s vis=%d title=%r" % (hwnd, cn, vis, ti))
        kids = children_of(hwnd)
        print("    children: %d" % len(kids))
        for kh in kids:
            kcn = classname(kh)
            kti = title(kh)
            kvis = IsWindowVisible(kh)
            kpid = pid_of(kh)
            print("      child hwnd=0x%08X pid=%-6d vis=%d class=%-28s title=%r" % (
                kh, kpid, kvis, kcn, kti))

    # Kill ONLY the pid we launched (never the pre-existing instance)
    print("\nterminating pid:", mypid)
    try:
        h = kernel32.OpenProcess(0x0001 | 0x0008, False, mypid)  # TERMINATE|VM
        if h:
            kernel32.TerminateProcess(h, 0)
            kernel32.CloseHandle(h)
    except Exception as e:  # noqa: BLE001
        print("terminate error:", e)
    time.sleep(1)
    rc = subprocess.run(["taskkill", "/F", "/PID", str(mypid)], capture_output=True)
    print("taskkill rc:", rc.returncode, rc.stdout.decode("gbk", "replace").strip(),
          rc.stderr.decode("gbk", "replace").strip())


if __name__ == "__main__":
    main()
