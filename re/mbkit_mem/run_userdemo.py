#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""run_userdemo.py —— 跑「用户自己的 demo exe」（不是自造 demo）。

与 run_demo.py 的区别：
  * 窗口不靠标题关键字识别 —— 直接按 **PID** 收本进程所有可见顶层窗口，
    因为用户 demo 的标题会被它自己的回调改成 "加载: <URL>"，没有固定关键字。
  * 不等待进程退出（GUI 程序不会自己退），采样 N 秒后**主动结束**并把
    最后一张截图留下。
  * 额外记录**磁盘模块占用**：证明 MbKit.dll 不在 exe 目录里也能跑起来。

用法：python run_userdemo.py <exe路径> [采样秒数]
"""
import ctypes
import ctypes.wintypes as wt
import json
import os
import shutil
import struct
import subprocess
import sys
import time

sys.dont_write_bytecode = True

u32 = ctypes.windll.user32
g32 = ctypes.windll.gdi32
k32 = ctypes.windll.kernel32
PW_RENDERFULLCONTENT = 2

ROOT = r"C:\Users\MadeSpark\Desktop\测试"


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
                        "rect": (r.left, r.top, r.right, r.bottom)})
        return True

    u32.EnumWindows(cb, 0)
    return out


def grab_bmp(hwnd, path, mode=0):
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
    hdr = struct.pack("<2sIHHI", b"BM", 14 + 40 + bi.biSizeImage, 0, 0, 14 + 40)
    ih = struct.pack("<IiiHHIIiiII", 40, w, -h, 1, 24, 0, bi.biSizeImage,
                     2835, 2835, 0, 0)
    with open(path, "wb") as f:
        f.write(hdr + ih + buf.raw)
    d = buf.raw
    nonwhite = sum(1 for i in range(0, len(d), 3) if d[i:i + 3] != b"\xff\xff\xff")
    # 颜色多样性：真渲染出来的网页不会只有极少种颜色
    kinds = len({d[i:i + 3] for i in range(0, len(d), 3)})
    return {"w": w, "h": h, "printwindow_ok": ok, "nonwhite": nonwhite,
            "total": len(d) // 3, "colors": kinds, "bmp": os.path.basename(path)}


def main():
    exe = os.path.abspath(sys.argv[1])
    limit = float(sys.argv[2]) if len(sys.argv) > 2 else 25.0
    cwd = os.path.dirname(exe)

    # ---- 环境布置：node.dll 放 exe 旁边（MbKit 内核路径规则 ②）----
    nd = os.path.join(cwd, "node.dll")
    src = os.path.join(ROOT, "main", "corn", "node.dll")
    if not os.path.exists(nd) and os.path.exists(src):
        shutil.copy2(src, nd)
        print("已把 node.dll 复制到 exe 旁边")
    print("目录内容:")
    for f in sorted(os.listdir(cwd)):
        p = os.path.join(cwd, f)
        print("   %-28s %10d B" % (f, os.path.getsize(p) if os.path.isfile(p) else -1))
    print("⚠ MbKit.dll 是否在 exe 目录: %s"
          % os.path.exists(os.path.join(cwd, "MbKit.dll")))

    p = subprocess.Popen([exe], cwd=cwd)
    print("\n启动 pid = %d" % p.pid)
    t0 = time.time()
    shots, timeline = [], []
    nextshot = 0.0
    main_hwnd = None
    while time.time() - t0 < limit:
        if p.poll() is not None:
            print("!! 进程已退出，退出码 %d" % p.returncode)
            break
        el = time.time() - t0
        wins = [w for w in enum_windows(p.pid) if w["w"] > 200 and w["h"] > 200]
        timeline.append({"t": round(el, 2), "windows": wins})
        if not main_hwnd and wins:
            main_hwnd = max(wins, key=lambda w: w["w"] * w["h"])["hwnd"]
            print("★ [%5.1fs] 主窗口 hwnd=0x%08X 类='%s' %dx%d 标题=%r"
                  % (el, main_hwnd, wins[0]["cls"],
                     wins[0]["w"], wins[0]["h"], wins[0]["title"]))
        if main_hwnd and el >= nextshot:
            nextshot = el + 4.0
            info = grab_bmp(main_hwnd, os.path.join(cwd, "u_scr_%05.1fs.bmp" % el), 1)
            ipw = grab_bmp(main_hwnd, os.path.join(cwd, "u_pw_%05.1fs.bmp" % el), 0)
            ttl = ""
            for w in wins:
                if w["hwnd"] == main_hwnd:
                    ttl = w["title"]
            print("   [%5.1fs] 屏幕 BitBlt: 非白 %s/%s 色数 %s | PrintWindow 非白 %s | 标题 %r"
                  % (el,
                     info and info["nonwhite"], info and info["total"], info and info["colors"],
                     ipw and ipw["nonwhite"], ttl))
            if info:
                shots.append({"t": round(el, 2), "title": ttl, **info})

    if p.poll() is None:
        p.kill()                      # 注意：Popen 没有 .handle（踩过），必须用 .kill()
        try:
            p.wait(timeout=5)
        except Exception:
            pass
        print("\n已结束 demo 进程（pid %d）" % p.pid)

    rep = {"exe": exe, "size": os.path.getsize(exe), "exitedEarly": p.returncode,
           "shots": shots, "timeline": timeline[-6:]}
    with open(os.path.join(cwd, "run_report.json"), "w", encoding="utf-8") as f:
        json.dump(rep, f, ensure_ascii=False, indent=1)
    print("\n报告: %s" % os.path.join(cwd, "run_report.json"))


if __name__ == "__main__":
    main()
