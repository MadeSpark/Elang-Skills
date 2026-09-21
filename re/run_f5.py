#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Controlled compile-run experiment: trigger 'compile & run' in the IDE and observe
   (a) child 'debugged' process, (b) new windows (output pane). Then kill e.exe + children.
"""
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
DEMO_SRC = r"C:\Users\MadeSpark\Desktop\测试\demos\02-嵌套控制流压测\项目"
DEMO_DST = r"C:\Users\MadeSpark\Desktop\测试\re\proj02"

u = ctypes.WinDLL("user32", use_last_error=True)
k = ctypes.WinDLL("kernel32", use_last_error=True)
PROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

WM_COMMAND = 0x0111
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
VK_F5 = 0x74


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


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD), ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)),
        ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD), ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", wintypes.DWORD), ("szExeFile", wintypes.WCHAR * 260)]


def all_procs():
    TH32CS_SNAPPROCESS = 0x02
    k.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    snap = k.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    pe = PROCESSENTRY32W()
    pe.dwSize = ctypes.sizeof(PROCESSENTRY32W)
    out = []
    if k.Process32FirstW(snap, ctypes.byref(pe)):
        while True:
            out.append((pe.th32ProcessID, pe.th32ParentProcessID, pe.szExeFile))
            if not k.Process32NextW(snap, ctypes.byref(pe)):
                break
    k.CloseHandle(snap)
    return out


def walk_menu(hm, prefix=""):
    items = []
    n = u.GetMenuItemCount(hm)
    for i in range(n):
        u.GetMenuStringA(hm, i, ctypes.create_string_buffer(0), 0, 0)  # noop
        buf = ctypes.create_string_buffer(256)
        u.GetMenuStringA(hm, i, buf, 256, 0x400)  # MF_BYPOSITION
        try:
            txt = buf.value.decode("gbk", "replace")
        except Exception:  # noqa: BLE001
            txt = buf.value.decode("latin-1", "replace")
        sub = u.GetSubMenu(hm, i)
        if sub:
            items += walk_menu(sub, prefix + txt + ">")
        else:
            mid = u.GetMenuItemID(hm, i)
            items.append((mid, prefix + txt))
    return items


def main():
    if not os.path.exists(DEMO_DST):
        shutil.copytree(DEMO_SRC, DEMO_DST)
    efile = os.path.join(DEMO_DST, "代码.e")
    print("demo file:", efile, os.path.exists(efile))

    proc = subprocess.Popen([E_EXE, efile], cwd=E_DIR)
    pid = proc.pid
    print("e.exe pid:", pid)
    time.sleep(8)

    tops_before = set(enum_all())
    main_hwnds = [h for h in enum_all() if pid_of(h) == pid and cn(h) == "ENewFrame"]
    print("main frames:", ["0x%08X %r" % (h, ti(h)) for h in main_hwnds])
    if not main_hwnds:
        print("no main frame; abort")
        subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
        return
    mf = main_hwnds[0]

    # try menu command discovery (informational)
    hm = u.GetMenu(mf)
    if hm:
        try:
            items = walk_menu(hm)
            run_items = [(m, t) for m, t in items if any(x in t for x in ("编译", "运行", "执行"))]
            print("candidate run menu items:")
            for m, t in run_items[:20]:
                print("   id=%s  %s" % (m, t))
        except Exception as e:  # noqa: BLE001
            print("menu walk err", e)

    print("\n>> posting F5 (compile & run) to main frame")
    u.PostMessageW(mf, WM_KEYDOWN, VK_F5, 0)
    u.PostMessageW(mf, WM_KEYUP, VK_F5, 0)
    time.sleep(2)
    # also try WM_COMMAND with the classic 'run' id if any found
    time.sleep(8)

    print("\n--- process tree (children of e.exe pid=%d) ---" % pid)
    procs = all_procs()
    kids = [(p, pp, ex) for (p, pp, ex) in procs if pp == pid]
    for p, pp, ex in kids:
        print("   child pid=%d exe=%s" % (p, ex))
    # grandchildren
    kidpids = set(p for p, pp, ex in kids)
    for p, pp, ex in procs:
        if pp in kidpids:
            print("   grandchild pid=%d parent=%d exe=%s" % (p, pp, ex))

    print("\n--- new top-level windows since launch ---")
    for h in enum_all():
        if h not in tops_before:
            print("   NEW 0x%08X pid=%d class=%r title=%r vis=%d" % (
                h, pid_of(h), cn(h), ti(h), u.IsWindowVisible(h)))

    print("\n--- title hints in process (输出/调试/编译) ---")
    for h in enum_all():
        if pid_of(h) != pid:
            continue
        t = ti(h)
        if t and any(x in t for x in ("输出", "调试", "编译", "输出夹", "状态夹")):
            print("   0x%08X class=%r title=%r" % (h, cn(h), t))

    print("\n--- killing e.exe tree ---")
    subprocess.run(["taskkill", "/F", "/T", "/PID", str(pid)], capture_output=True)
    for p, pp, ex in kids:
        subprocess.run(["taskkill", "/F", "/PID", str(p)], capture_output=True)
    print("done")


if __name__ == "__main__":
    main()
