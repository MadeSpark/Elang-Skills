#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""run_demo.py —— 跑 demo exe，自动找窗口、截图、取回 demo.log。

不依赖任何第三方库：窗口枚举 / PrintWindow / GetDIBits 全走 ctypes，
截图写成 24 位 BMP（再由 bmp2png.py 转 PNG，便于直接看图核对）。

用法：python run_demo.py <exe路径> [等待上限秒]
"""
import ctypes
import ctypes.wintypes as wt
import os
import struct
import subprocess
import sys
import time

u32 = ctypes.windll.user32
g32 = ctypes.windll.gdi32
k32 = ctypes.windll.kernel32

PW_RENDERFULLCONTENT = 2
TITLE_KEY = "MbKit"


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
            out.append((hwnd, buf.value, cls.value,
                        r.right - r.left, r.bottom - r.top))
        return True

    u32.EnumWindows(cb, 0)
    return out


def grab_bmp(hwnd, path, mode=0):
    """mode 0: PrintWindow(PW_RENDERFULLCONTENT)  1: 屏幕 BitBlt 客户区"""
    r = wt.RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    if w <= 0 or h <= 0:
        return None
    hdc = u32.GetDC(hwnd)
    mdc = g32.CreateCompatibleDC(hdc)
    bmp = g32.CreateCompatibleBitmap(hdc, w, h)
    old = g32.SelectObject(mdc, bmp)
    if mode == 0:
        ok = u32.PrintWindow(hwnd, mdc, PW_RENDERFULLCONTENT)
    else:
        ok = g32.BitBlt(mdc, 0, 0, w, h, hdc, 0, 0, 0x00CC0020)
    # GetDIBits
    class BMIH(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", ctypes.c_long),
                    ("biHeight", ctypes.c_long), ("biPlanes", wt.WORD),
                    ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                    ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", ctypes.c_long),
                    ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wt.DWORD),
                    ("biClrImportant", wt.DWORD)]

    bi = BMIH()
    bi.biSize = ctypes.sizeof(BMIH)
    bi.biWidth, bi.biHeight = w, -h          # 负高度 = 自顶向下
    bi.biPlanes, bi.biBitCount = 1, 24
    bi.biCompression = 0
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
    # 写 24 位 BMP
    hdr = struct.pack("<2sIHHI", b"BM", 14 + 40 + bi.biSizeImage, 0, 0, 14 + 40)
    ih = struct.pack("<IiiHHIIiiII", 40, w, -h, 1, 24, 0, bi.biSizeImage,
                     2835, 2835, 0, 0)
    with open(path, "wb") as f:
        f.write(hdr + ih + buf.raw)
    # 统计非白像素
    d = buf.raw
    nonwhite = sum(1 for i in range(0, len(d), 3)
                   if d[i:i + 3] != b"\xff\xff\xff")
    return {"w": w, "h": h, "printwindow_ok": ok, "nonwhite": nonwhite,
            "total": len(d) // 3, "bmp": path}


def main():
    exe = os.path.abspath(sys.argv[1])
    limit = float(sys.argv[2]) if len(sys.argv) > 2 else 60.0
    cwd = os.path.dirname(exe)
    logp = os.path.join(cwd, "demo.log")
    if os.path.exists(logp):
        os.remove(logp)
    print("启动 %s\n工作目录 %s" % (exe, cwd))
    p = subprocess.Popen([exe], cwd=cwd)
    print("pid = %d" % p.pid)
    t0 = time.time()
    found = None
    shots = []
    nextshot = 0.0
    while time.time() - t0 < limit:
        if p.poll() is not None:
            break
        if not found:
            for hwnd, title, cls, w, h in enum_windows(p.pid):
                if TITLE_KEY in title:
                    found = (hwnd, title, cls, w, h)
                    print("\n★ 找到窗口 hwnd=0x%08X 类='%s' 尺寸=%dx%d 标题=%r"
                          % (hwnd, cls, w, h, title))
                    break
        if found and time.time() >= nextshot:
            nextshot = time.time() + 1.5
            el = time.time() - t0
            for mode, tag in ((0, "pw"), (1, "scr")):
                out = os.path.join(os.path.dirname(exe),
                                   "shot_%s_%04.1fs.bmp" % (tag, el))
                info = grab_bmp(found[0], out, mode)
                if info:
                    shots.append((el, tag, info))
                    print("   [%5.1fs] %-3s %dx%d  非白像素 %d/%d  PrintWindow=%s"
                          % (el, tag, info["w"], info["h"], info["nonwhite"],
                             info["total"], info["printwindow_ok"]))
        time.sleep(0.2)
    rc = p.wait()
    print("\n进程退出码 = %d（运行 %.1f 秒）" % (rc, time.time() - t0))
    print("\n==== demo.log ====")
    if os.path.exists(logp):
        t = open(logp, "rb").read()
        for enc in ("utf-8", "gbk"):
            try:
                print(t.decode(enc)); break
            except Exception:
                pass
        else:
            print(t.decode("utf-8", errors="replace"))
    else:
        print("(没有 demo.log)")
    print("截图 %d 张（%s）" % (len(shots), os.path.dirname(exe)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
