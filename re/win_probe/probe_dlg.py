#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""probe_dlg.py —— 抓 e.exe 弹出的对话框标题与子控件文本（判死锁弹窗用）。

用法：先起本脚本，再跑被测命令（--headless off 让窗口真实存在）。
只用 GetWindowTextA / EnumChildWindows（本实验自己的进程，不会挂）。
"""
import ctypes
import os
import sys
import time
from ctypes import wintypes as wt

u32 = ctypes.WinDLL("user32", use_last_error=True)
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)

u32.GetWindowTextA.argtypes = [wt.HWND, ctypes.c_char_p, ctypes.c_int]
u32.GetClassNameA.argtypes = [wt.HWND, ctypes.c_char_p, ctypes.c_int]
u32.IsWindowVisible.argtypes = [wt.HWND]
u32.GetWindowRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
u32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
u32.EnumChildWindows.argtypes = [wt.HWND, ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM), wt.LPARAM]


def proc_name(pid):
    h = k32.OpenProcess(0x1000, False, pid)
    if not h:
        return None
    try:
        buf = ctypes.create_unicode_buffer(1024)
        n = wt.DWORD(1024)
        if not k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n)):
            return None
        return os.path.basename(buf.value).lower()
    finally:
        k32.CloseHandle(h)


def all_pids():
    arr = (wt.DWORD * 4096)()
    need = wt.DWORD()
    if not psapi.EnumProcesses(arr, ctypes.sizeof(arr), ctypes.byref(need)):
        return set()
    return {arr[i] for i in range(need.value // 4) if arr[i]}


def txt(h):
    b = ctypes.create_string_buffer(512)
    u32.GetWindowTextA(h, b, 512)
    return b.value.decode("gbk", "replace").strip()


def child_dump(hdlg):
    rows = []

    def cb(h, lp):
        cls = ctypes.create_string_buffer(64)
        u32.GetClassNameA(h, cls, 64)
        rows.append((cls.value.decode("gbk", "replace"), txt(h), bool(u32.IsWindowVisible(h))))
        return True

    u32.EnumChildWindows(hdlg, ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)(cb), 0)
    return rows


def main():
    want = sys.argv[1].lower() if len(sys.argv) > 1 else "e.exe"
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 30
    base = {p for p in all_pids() if proc_name(p) == want}
    targets = set(base)
    seen = set()
    t_end = time.time() + secs
    while time.time() < t_end:
        targets |= {p for p in all_pids() if proc_name(p) == want}
        arr = (wt.HWND * 2048)()
        cnt = u32.EnumWindows.__class__  # noqa - 用 FindWindowEx 太繁琐，改用 EnumWindows
        break
    # EnumWindows 逐窗
    WNDENUM = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    hits = []

    def cb(h, lp):
        pid = wt.DWORD()
        u32.GetWindowThreadProcessId(h, ctypes.byref(pid))
        if pid.value in targets - base:
            cls = ctypes.create_string_buffer(64)
            u32.GetClassNameA(h, cls, 64)
            c = cls.value.decode("gbk", "replace")
            if c == "#32770":
                t = txt(h)
                r = wt.RECT()
                u32.GetWindowRect(h, ctypes.byref(r))
                key = (t, r.left, r.top, r.right, r.bottom)
                if key not in seen:
                    seen.add(key)
                    hits.append((h, t, key))
        return True

    while time.time() < t_end:
        targets |= {p for p in all_pids() if proc_name(p) == want}
        u32.EnumWindows(WNDENUM(cb), 0)
        for h, t, key in hits:
            print("DIALOG cls=#32770 title=%r rect=%s" % (t, key[1:]))
            for cls, ct, vis in child_dump(h):
                if ct:
                    print("   child cls=%-16s vis=%d text=%r" % (cls, vis, ct))
        hits.clear()
        time.sleep(0.3)
    print("done, %d unique dialogs" % len(seen))


if __name__ == "__main__":
    main()
