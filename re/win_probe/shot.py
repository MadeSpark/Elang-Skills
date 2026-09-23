#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""抓全屏截图（纯 ctypes + GDI，零依赖），落 PNG。

    python shot.py <输出.png>
"""
import ctypes
import ctypes.wintypes as wt
import struct
import sys
import zlib

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

SM_XVIRTUALSCREEN, SM_YVIRTUALSCREEN, SM_CXVIRTUALSCREEN, SM_CYVIRTUALSCREEN = 76, 77, 78, 79
SRCCOPY = 0x00CC0020
DIB_RGB_COLORS = 0


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
                ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG),
                ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD)]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wt.DWORD * 3)]


def _png(path, w, h, rgb_rows):
    """rgb_rows: 每行 bytes（RGB24，自上而下）。纯 stdlib 写 PNG。"""
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    raw = b"".join(b"\x00" + r for r in rgb_rows)      # filter type 0
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)   # 8bit truecolor RGB
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def capture(path):
    x = user32.GetSystemMetrics(SM_XVIRTUALSCREEN)
    y = user32.GetSystemMetrics(SM_YVIRTUALSCREEN)
    w = user32.GetSystemMetrics(SM_CXVIRTUALSCREEN)
    h = user32.GetSystemMetrics(SM_CYVIRTUALSCREEN)

    hdc = user32.GetDC(0)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mem, bmp)
    gdi32.BitBlt(mem, 0, 0, w, h, hdc, x, y, SRCCOPY)

    bi = BITMAPINFO()
    bi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.bmiHeader.biWidth = w
    bi.bmiHeader.biHeight = -h          # top-down
    bi.bmiHeader.biPlanes = 1
    bi.bmiHeader.biBitCount = 24
    bi.bmiHeader.biCompression = 0
    stride = ((w * 3 + 3) // 4) * 4
    buf = ctypes.create_string_buffer(stride * h)
    gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bi), DIB_RGB_COLORS)

    data = buf.raw
    rows = []
    for r in range(h):
        s = data[r * stride: r * stride + w * 3]
        rows.append(bytes(b for i in range(0, len(s), 3) for b in (s[i + 2], s[i + 1], s[i])))

    _png(path, w, h, rows)

    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(0, hdc)
    return w, h


def fingerprint(path):
    """按块采样算指纹，用于「屏幕有没有变化」的定量比较。"""
    import hashlib
    return hashlib.md5(open(path, "rb").read()).hexdigest()


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "shot.png"
    print("saved %s %dx%d" % (out, *capture(out)))
