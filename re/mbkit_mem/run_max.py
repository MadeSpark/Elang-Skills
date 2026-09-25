#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""run_max.py —— 复现「窗口最大化时画面撕裂」。

流程：启动 exe → 等页面加载 → 截图 → 最大化 → 等 3 秒 → 截图 →
      还原 → 拉成指定尺寸 → 等 3 秒 → 截图。每步都统计非白像素与颜色数。

用法：python run_max.py <exe路径> [首屏等待秒]
"""
import ctypes
import ctypes.wintypes as wt
import json
import os
import struct
import subprocess
import sys
import time

sys.dont_write_bytecode = True

u32 = ctypes.windll.user32
g32 = ctypes.windll.gdi32

SW_MAXIMIZE, SW_RESTORE = 3, 9


def enum_windows(pid):
    out = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        wpid = wt.DWORD()
        u32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
        if wpid.value == pid and u32.IsWindowVisible(hwnd):
            n = u32.GetWindowTextLengthW(hwnd)
            buf = ctypes.create_unicode_buffer(n + 1)
            u32.GetWindowTextW(hwnd, buf, n + 1)
            cls = ctypes.create_unicode_buffer(256)
            u32.GetClassNameW(hwnd, cls, 256)
            r = wt.RECT()
            u32.GetWindowRect(hwnd, ctypes.byref(r))
            out.append({"hwnd": hwnd, "title": buf.value, "cls": cls.value,
                        "w": r.right - r.left, "h": r.bottom - r.top,
                        "rect": [r.left, r.top, r.right, r.bottom]})
        return True

    u32.EnumWindows(cb, 0)
    return out


def grab(hwnd, path):
    r = wt.RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    if w <= 0 or h <= 0:
        return None
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
    nonwhite = sum(1 for i in range(0, len(d), 3) if d[i:i + 3] != b"\xff\xff\xff")
    # 「网页内容区」探测：找最右侧/最下方出现「明显有色像素」的位置
    def colored(px):
        r0, g0, b0 = px[2], px[1], px[0]
        return abs(r0 - g0) > 12 or abs(g0 - b0) > 12 or abs(r0 - b0) > 12

    right = 0
    bottom = 0
    for y in range(0, h, 4):
        row = d[y * stride:(y + 1) * stride]
        for x in range(0, w, 4):
            px = row[x * 3:x * 3 + 3]
            if colored(px):
                if x > right:
                    right = x
                if y > bottom:
                    bottom = y
    return {"w": w, "h": h, "nonwhite": nonwhite, "total": len(d) // 3,
            "contentRight": right, "contentBottom": bottom,
            "bmp": os.path.basename(path)}


def main():
    exe = os.path.abspath(sys.argv[1])
    first = float(sys.argv[2]) if len(sys.argv) > 2 else 12.0
    cwd = os.path.dirname(exe)
    p = subprocess.Popen([exe], cwd=cwd)
    print("pid = %d" % p.pid)
    try:
        time.sleep(first)
        wins = [w for w in enum_windows(p.pid) if w["w"] > 200 and w["h"] > 200]
        if not wins:
            print("!! 没找到窗口；进程退出码 %s" % p.poll())
            return
        main_w = max(wins, key=lambda w: w["w"] * w["h"])
        print("主窗口: %r %dx%d" % (main_w["title"], main_w["w"], main_w["h"]))
        steps = []

        def snap(tag):
            info = grab(main_w["hwnd"], os.path.join(cwd, "max_%s.bmp" % tag))
            wins2 = [w for w in enum_windows(p.pid) if w["hwnd"] == main_w["hwnd"]]
            print("  [%-9s] %sx%s  非白 %s/%s  内容右边界 %s 下边界 %s"
                  % (tag, info["w"], info["h"], info["nonwhite"], info["total"],
                     info["contentRight"], info["contentBottom"]))
            steps.append({"tag": tag, "alive": p.poll(), **info})
            return info

        snap("01_initial")
        r = wt.RECT()
        u32.GetWindowRect(main_w["hwnd"], ctypes.byref(r))
        print("\n→ 最大化")
        u32.ShowWindow(main_w["hwnd"], SW_MAXIMIZE)
        time.sleep(3.5)
        snap("02_maximized")
        print("\n→ 还原")
        u32.ShowWindow(main_w["hwnd"], SW_RESTORE)
        time.sleep(2.5)
        snap("03_restored")
        print("\n→ 手工拉到 1000x700")
        r = wt.RECT()
        u32.GetWindowRect(main_w["hwnd"], ctypes.byref(r))
        u32.SetWindowPos(main_w["hwnd"], 0, r.left, r.top, 1000, 700, 0x0004)
        time.sleep(3.5)
        snap("04_resized1000x700")
        print("\n进程是否仍存活: %s" % (p.poll() is None))
        with open(os.path.join(cwd, "max_report.json"), "w", encoding="utf-8") as f:
            json.dump({"exe": exe, "steps": steps}, f, ensure_ascii=False, indent=1)
    finally:
        if p.poll() is None:
            p.kill()
            try:
                p.wait(timeout=5)
            except Exception:
                pass
            print("已结束 pid %d" % p.pid)


if __name__ == "__main__":
    main()
