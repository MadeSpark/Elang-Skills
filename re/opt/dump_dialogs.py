#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""枚举顶层窗口，找出易语言编译保存框的当前状态（诊断用）。"""
import ctypes
import ctypes.wintypes as w
import sys

sys.stdout.reconfigure(encoding="utf-8")
u = ctypes.windll.user32


def text(h):
    n = u.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u.GetWindowTextW(h, b, n + 1)
    return b.value


def cls(h):
    b = ctypes.create_unicode_buffer(256)
    u.GetClassNameW(h, b, 256)
    return b.value


EW = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
tops = []


def cb(h, l):
    tops.append(h)
    return True


u.EnumWindows(EW(cb), 0)

print("顶层窗口总数:", len(tops))
print()
for h in tops:
    c = cls(h)
    t = text(h)
    if c != "#32770":
        continue
    pid = w.DWORD()
    u.GetWindowThreadProcessId(h, ctypes.byref(pid))
    r = w.RECT()
    u.GetWindowRect(h, ctypes.byref(r))
    print("#32770 hwnd=%#x pid=%d visible=%d title=%r rect=(%d,%d,%d,%d)"
          % (h, pid.value, u.IsWindowVisible(h), t, r.left, r.top, r.right, r.bottom))
    ch = []

    def cbc(x, l):
        ch.append(x)
        return True

    u.EnumChildWindows(h, EW(cbc), 0)
    for x in ch:
        cc = cls(x)
        if cc in ("Edit", "Button", "ComboBox", "ComboBoxEx32"):
            print("      child %#x %-14s text=%r" % (x, cc, text(x)[:200]))
