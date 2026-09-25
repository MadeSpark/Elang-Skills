#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""max_pw.py —— 最大化/缩放回归（PrintWindow 抓图版）。

为什么要重写 run_max.py：
  run_max.py 用 BitBlt(GetDC(hwnd)) 抓**父窗口**，而网页内容是渲染在**子窗口**
  （miniblink 的 webview hwnd）里的 ⇒ 抓出来只有 2 种颜色（实测 max_02_maximized.png
  颜色数 = 2）⇒ 那个报告的「非白 97%」是**假象**，不能用来判撕裂。

本脚本改用 PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT=2)，实测能抓到子窗口内容
（u_pw_024.6s.png 颜色数 11049）。

判据（三条一起看，缺一不可）：
  1. colors        真渲染 ≥ 数千色；白屏/未绘制 ≤ 2 色
  2. contentRight  最右有色像素 x；应接近 客户区宽度-边距
  3. contentBottom 最下有颜色像素 y；应接近 客户区高度-边距
  「撕裂」= 最大化后 contentRight / contentBottom **没有跟着窗口变大**，
  或窗口右侧/下方出现大片单一色带（网页没重绘）。

用法：python max_pw.py <exe路径> [首屏等待秒]
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
PW_RENDERFULLCONTENT = 2


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
    ok = u32.PrintWindow(hwnd, mdc, PW_RENDERFULLCONTENT)

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
    bi.biPlanes, bi.biBitCount, bi.biCompression = 1, 24, 0
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
    px = [d[i:i + 3] for i in range(0, len(d), 3)]
    nonwhite = sum(1 for p in px if p != b"\xff\xff\xff")
    colors = len(set(px))

    def colored(p):
        b0, g0, r0 = p[0], p[1], p[2]
        return abs(r0 - g0) > 12 or abs(g0 - b0) > 12 or abs(r0 - b0) > 12

    right = bottom = 0
    for y in range(0, h, 2):
        row = d[y * stride:(y + 1) * stride]
        for x in range(0, w, 2):
            if colored(row[x * 3:x * 3 + 3]):
                if x > right:
                    right = x
                if y > bottom:
                    bottom = y
    # 「未绘制色带」：右侧 8% / 下方 8% 内的非白像素占比（撕裂时会是同一种纯色）
    band_r = sum(1 for y in range(0, h, 4) for x in range(int(w * 0.92), w, 2)
                 if d[y * stride + x * 3:y * stride + x * 3 + 3] != b"\xff\xff\xff")
    band_b = sum(1 for y in range(int(h * 0.92), h, 2) for x in range(0, w, 4)
                 if d[y * stride + x * 3:y * stride + x * 3 + 3] != b"\xff\xff\xff")
    return {"w": w, "h": h, "pw_ok": ok, "nonwhite": nonwhite, "total": len(px),
            "colors": colors, "contentRight": right, "contentBottom": bottom,
            "bandR_nonwhite": band_r, "bandB_nonwhite": band_b,
            "bmp": os.path.basename(path)}


def main():
    exe = os.path.abspath(sys.argv[1])
    first = float(sys.argv[2]) if len(sys.argv) > 2 else 12.0
    cwd = os.path.dirname(exe)
    for f in sorted(os.listdir(cwd)):
        p = os.path.join(cwd, f)
        print("   %-30s %10d B" % (f, os.path.getsize(p) if os.path.isfile(p) else -1))
    print("⚠ MbKit.dll 在 exe 目录: %s"
          % os.path.exists(os.path.join(cwd, "MbKit.dll")))

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
            info = grab(main_w["hwnd"], os.path.join(cwd, "mx_%s.bmp" % tag))
            if info is None:
                print("  [%-18s] 抓图失败" % tag)
                return None
            print("  [%-18s] %4sx%-4s 色数 %-6s 非白 %s/%s  内容右 %s 下 %s"
                  % (tag, info["w"], info["h"], info["colors"], info["nonwhite"],
                     info["total"], info["contentRight"], info["contentBottom"]))
            steps.append({"tag": tag, "alive": p.poll(), **info})
            return info

        snap("01_initial")
        u32.ShowWindow(main_w["hwnd"], SW_MAXIMIZE)
        time.sleep(3.5)
        snap("02_maximized")
        u32.ShowWindow(main_w["hwnd"], SW_RESTORE)
        time.sleep(2.5)
        snap("03_restored")
        r = wt.RECT()
        u32.GetWindowRect(main_w["hwnd"], ctypes.byref(r))
        u32.SetWindowPos(main_w["hwnd"], 0, r.left, r.top, 1000, 700, 0x0004)
        time.sleep(3.5)
        snap("04_resized1000x700")
        print("进程是否仍存活: %s  退出码 %s" % (p.poll() is None, p.returncode))
        with open(os.path.join(cwd, "max_pw_report.json"), "w", encoding="utf-8") as f:
            json.dump({"exe": exe, "exeSize": os.path.getsize(exe),
                       "exeMd5dir": sorted(os.listdir(cwd)), "steps": steps},
                      f, ensure_ascii=False, indent=1)
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
