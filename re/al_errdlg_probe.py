# -*- coding: utf-8 -*-
"""读到「错误：」对话框后抓它的全部子窗口文本，然后关闭它继续观察。"""
import sys
import io
import time
import ctypes
import subprocess
from ctypes import wintypes

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

E_EXE = r"D:\ides\e\e.exe"
IN_E = sys.argv[1] if len(sys.argv) > 1 else IN_E
OUT_EXE = r"C:\Users\MadeSpark\Desktop\测试\re\up_test\build\demo02.exe"
RES = r"C:\Users\MadeSpark\Desktop\测试\re\up_test\build\demo02.result.json"

user32 = ctypes.WinDLL("user32", use_last_error=True)
WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
WM_CLOSE = 0x0010
BM_CLICK = 0x00F5


def wcls(hwnd):
    b = ctypes.create_unicode_buffer(512)
    user32.GetClassNameW(hwnd, b, 512)
    return b.value


def wtext(hwnd):
    ln = user32.GetWindowTextLengthW(hwnd)
    b = ctypes.create_unicode_buffer(ln + 2)
    user32.GetWindowTextW(hwnd, b, ln + 2)
    return b.value


def is_vis(hwnd):
    return bool(user32.IsWindowVisible(hwnd))


def main():
    cmd = [E_EXE, IN_E,
           "--autolinker-headless-compile",
           "--autolinker-output", OUT_EXE,
           "--autolinker-target", "auto",
           "--autolinker-result", RES,
           "--autolinker-startup-timeout", "120",
           "--autolinker-show-window"]
    p = subprocess.Popen(cmd)
    print("pid =", p.pid)
    deadline = time.time() + 60
    errdlg = None
    tops = []
    while time.time() < deadline and errdlg is None:
        found = []

        @WNDENUMPROC
        def cb(hwnd, _lp):
            wpid = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
            if wpid.value == p.pid and is_vis(hwnd):
                found.append((hwnd, wcls(hwnd), wtext(hwnd)))
            return True

        user32.EnumWindows(cb, 0)
        for h, c, t in found:
            if t and ("错误" in t or "error" in t.lower() or "提示" in t):
                errdlg = h
                print("[ERRDLG] cls=%r title=%r" % (c, t))
                break
        if errdlg is None:
            tops = found
            time.sleep(1.0)

    if errdlg is None:
        print("no error dialog in 60s; top windows:")
        for h, c, t in tops:
            if t:
                print("  cls=%r title=%r" % (c, t))
        subprocess.run(["taskkill", "/F", "/PID", str(p.pid)], capture_output=True)
        return

    print("children of error dialog:")
    kids = []

    @WNDENUMPROC
    def cb2(hwnd, _lp):
        kids.append((hwnd, wcls(hwnd), wtext(hwnd), is_vis(hwnd), user32.GetDlgCtrlID(hwnd)))
        return True

    user32.EnumChildWindows(errdlg, cb2, 0)
    for h, c, t, v, cid in kids:
        print("  child cls=%r id=%s vis=%s text=%r" % (c, cid, v, t))

    # 点「确定」类按钮（Button 且有文本）
    for h, c, t, v, cid in kids:
        if c.lower() == "button" and t:
            print("clicking button:", t)
            user32.PostMessageW(h, BM_CLICK, 0, 0)
            time.sleep(0.6)
            break

    # 继续观察 30 秒
    deadline2 = time.time() + 30
    printed = set()
    while time.time() < deadline2:
        found = []

        @WNDENUMPROC
        def cb3(hwnd, _lp):
            wpid = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
            if wpid.value == p.pid and is_vis(hwnd):
                found.append((wcls(hwnd), wtext(hwnd)))
            return True

        user32.EnumWindows(cb3, 0)
        for c, t in found:
            k = (c, t)
            if k not in printed and t:
                printed.add(k)
                print("[TOP2] cls=%r title=%r" % (c, t))
        import os
        if os.path.exists(RES):
            print("RESULT JSON APPEARED")
            break
        time.sleep(1.0)

    import os
    print("result exists:", os.path.exists(RES),
          "| exe exists:", os.path.exists(OUT_EXE))
    subprocess.run(["taskkill", "/F", "/PID", str(p.pid)], capture_output=True)


main()
