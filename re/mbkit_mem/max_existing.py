#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""max_existing.py —— 对**已经在运行**的窗口做「截图 → 最大化 → 截图 → 还原 → 拉尺寸」。

与 run_max.py 的区别：不自己启动 exe，而是按类名/标题找现成窗口
（用于 elaunch 调试运行起来的那个进程）。
用法：python max_existing.py <窗口类名> <标题关键字> [输出前缀]
"""
import ctypes
import ctypes.wintypes as wt
import os
import struct
import sys
import time

sys.dont_write_bytecode = True
u32 = ctypes.windll.user32
g32 = ctypes.windll.gdi32
SW_MAXIMIZE, SW_RESTORE = 3, 9


def find(cls_want, title_key):
    hits = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        cls = ctypes.create_unicode_buffer(256)
        u32.GetClassNameW(hwnd, cls, 256)
        n = u32.GetWindowTextLengthW(hwnd)
        b = ctypes.create_unicode_buffer(n + 2)
        u32.GetWindowTextW(hwnd, b, n + 2)
        if cls.value == cls_want and title_key in b.value and u32.IsWindowVisible(hwnd):
            r = wt.RECT()
            u32.GetWindowRect(hwnd, ctypes.byref(r))
            pid = wt.DWORD()
            u32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            hits.append((hwnd, pid.value, cls.value, b.value,
                         r.right - r.left, r.bottom - r.top))
        return True

    u32.EnumWindows(cb, 0)
    return hits


def grab(hwnd, path):
    r = wt.RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    hdc = u32.GetDC(hwnd)
    mdc = g32.CreateCompatibleDC(hdc)
    bmp = g32.CreateCompatibleBitmap(hdc, w, h)
    old = g32.SelectObject(mdc, bmp)
    g32.BitBlt(mdc, 0, 0, w, h, hdc, 0, 0, 0x00CC0020)

    class BMIH(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", ctypes.c_long),
                    ("biHeight", ctypes.c_long), ("biPlanes", wt.WORD),
                    ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                    ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", ctypes.c_long),
                    ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wt.DWORD),
                    ("biClrImportant", wt.DWORD)]

    bi = BMIH()
    bi.biSize = ctypes.sizeof(BMIH)
    bi.biWidth, bi.biHeight = w, -h
    bi.biPlanes, bi.biBitCount = 1, 24
    stride = (w * 3 + 3) & ~3
    bi.biSizeImage = stride * h
    buf = ctypes.create_string_buffer(bi.biSizeImage)
    got = g32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
    g32.SelectObject(mdc, old)
    g32.DeleteObject(bmp)
    g32.DeleteDC(mdc)
    u32.ReleaseDC(hwnd, hdc)
    if got == 0:
        return None
    with open(path, "wb") as f:
        f.write(struct.pack("<2sIHHI", b"BM", 14 + 40 + bi.biSizeImage, 0, 0, 14 + 40))
        f.write(struct.pack("<IiiHHIIiiII", 40, w, -h, 1, 24, 0, bi.biSizeImage,
                            2835, 2835, 0, 0))
        f.write(buf.raw)
    d = buf.raw
    nw = sum(1 for i in range(0, len(d), 3) if d[i:i + 3] != b"\xff\xff\xff")

    def colored(px):
        return abs(px[2] - px[1]) > 12 or abs(px[1] - px[0]) > 12 or abs(px[2] - px[0]) > 12

    right = bottom = 0
    for y in range(0, h, 4):
        row = d[y * stride:(y + 1) * stride]
        for x in range(0, w, 4):
            if colored(row[x * 3:x * 3 + 3]):
                right = max(right, x)
                bottom = max(bottom, y)
    return {"w": w, "h": h, "nonwhite": nw, "total": len(d) // 3,
            "contentRight": right, "contentBottom": bottom}


def snap(hwnd, tag, prefix):
    p = "%s_%s.bmp" % (prefix, tag)
    i = grab(hwnd, p)
    print("   [%-11s] %sx%s 非白 %s/%s 内容右 %s 下 %s"
          % (tag, i["w"], i["h"], i["nonwhite"], i["total"],
             i["contentRight"], i["contentBottom"]))


def main():
    cls, key = sys.argv[1], sys.argv[2]
    prefix = sys.argv[3] if len(sys.argv) > 3 else "dbgwin"
    outdir = os.path.dirname(os.path.abspath(prefix))
    for _ in range(40):
        hits = find(cls, key)
        if hits:
            break
        time.sleep(1)
    if not hits:
        print("!! 没找到窗口（类=%s 标题含 %s）" % (cls, key))
        return 1
    hwnd, pid, c, title, w, h = max(hits, key=lambda x: x[4] * x[5])
    print("找到 %s pid=%d %dx%d 标题=%r" % (c, pid, w, h, title))
    snap(hwnd, "01_initial", prefix)
    print("→ 最大化")
    u32.ShowWindow(hwnd, SW_MAXIMIZE)
    time.sleep(3.0)
    snap(hwnd, "02_maximized", prefix)
    print("→ 还原")
    u32.ShowWindow(hwnd, SW_RESTORE)
    time.sleep(2.0)
    snap(hwnd, "03_restored", prefix)
    r = wt.RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(r))
    print("→ 拉成 1000x700")
    u32.SetWindowPos(hwnd, 0, r.left, r.top, 1000, 700, 0x0004)
    time.sleep(3.0)
    snap(hwnd, "04_resized", prefix)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
