#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dump_wins.py —— 启动 exe，枚举它**所有**顶层窗口 + 对话框子控件文字。

用于看清「运行时弹了个什么错误框」。用法：python dump_wins.py <exe> [等待秒]
"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

sys.dont_write_bytecode = True
u32 = ctypes.windll.user32


def text_of(hwnd):
    n = u32.GetWindowTextLengthW(hwnd)
    b = ctypes.create_unicode_buffer(n + 2)
    u32.SendMessageW(hwnd, 0x000D, n + 1, ctypes.byref(b))   # WM_GETTEXT
    return b.value


def walk(hwnd, depth, acc, pid):
    wpid = wt.DWORD()
    u32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
    cls = ctypes.create_unicode_buffer(256)
    u32.GetClassNameW(hwnd, cls, 256)
    r = wt.RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(r))
    vis = bool(u32.IsWindowVisible(hwnd))
    t = text_of(hwnd)
    if t or vis:
        acc.append("  " * depth + "hwnd=0x%08X pid=%d vis=%s cls=%-22s %dx%d text=%r"
                   % (hwnd, wpid.value, vis, cls.value,
                      r.right - r.left, r.bottom - r.top, t))
    if depth < 4:
        children = []

        @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
        def cb(h, _):
            children.append(h)
            return True

        u32.EnumChildWindows(hwnd, cb, 0)
        for c in children:
            walk(c, depth + 1, acc, pid)


def main():
    exe = os.path.abspath(sys.argv[1])
    wait = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
    p = subprocess.Popen([exe], cwd=os.path.dirname(exe))
    print("pid = %d" % p.pid)
    try:
        time.sleep(wait)
        acc = []

        @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
        def cb(hwnd, _):
            wpid = wt.DWORD()
            u32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
            if wpid.value == p.pid:
                walk(hwnd, 0, acc, p.pid)
            return True

        u32.EnumWindows(cb, 0)
        print("本进程窗口树：")
        for line in acc:
            print(line)
        print("\n进程状态: %s" % ("存活" if p.poll() is None else "已退出 %s" % p.returncode))
    finally:
        if p.poll() is None:
            p.kill()
            try:
                p.wait(timeout=5)
            except Exception:
                pass


if __name__ == "__main__":
    main()
