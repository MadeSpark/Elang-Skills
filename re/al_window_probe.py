# -*- coding: utf-8 -*-
"""带窗口跑一次 AutoLinker 无头编译，枚举 e.exe 的顶层窗口找阻塞对话框，最后按 PID 清理。"""
import sys
import io
import time
import ctypes
import subprocess
from ctypes import wintypes

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

E_EXE = r"D:\ides\e\e.exe"
IN_E = r"C:\Users\MadeSpark\Desktop\测试\re\up_test\demo02_al.e"
OUT_EXE = r"C:\Users\MadeSpark\Desktop\测试\re\up_test\build\demo02.exe"
RES = r"C:\Users\MadeSpark\Desktop\测试\re\up_test\build\demo02.result.json"

user32 = ctypes.WinDLL("user32", use_last_error=True)
WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def classname(hwnd):
    buf = ctypes.create_unicode_buffer(512)
    user32.GetClassNameA(hwnd, buf, 512)
    return buf.value


def title(hwnd):
    ln = user32.GetWindowTextLengthA(hwnd)
    buf = ctypes.create_string_buffer(ln + 2)
    user32.GetWindowTextA(hwnd, buf, ln + 2)
    return buf.value.decode("gbk", errors="replace")


def enum_for(pid, seconds):
    deadline = time.time() + seconds
    seen = {}
    while time.time() < deadline:
        found = []

        @WNDENUMPROC
        def cb(hwnd, _lp):
            wpid = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
            if wpid.value == pid:
                found.append((hwnd, classname(hwnd), title(hwnd),
                              bool(user32.IsWindowVisible(hwnd)),
                              bool(user32.IsWindowEnabled(hwnd))))
            return True

        user32.EnumWindows(cb, 0)
        for f in found:
            key = (f[1], f[2], f[3], f[4])
            if key not in seen:
                seen[key] = time.time()
                print("[TOP] cls=%r title=%r vis=%s en=%s" % f[:4])
        time.sleep(1.0)
    return seen


def main():
    wait_s = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    cmd = [E_EXE, IN_E,
           "--autolinker-headless-compile",
           "--autolinker-output", OUT_EXE,
           "--autolinker-target", "auto",
           "--autolinker-result", RES,
           "--autolinker-startup-timeout", "120",
           "--autolinker-show-window"]
    print("launch:", cmd)
    p = subprocess.Popen(cmd)
    print("pid =", p.pid)
    try:
        enum_for(p.pid, wait_s)
    finally:
        print("killing", p.pid)
        subprocess.run(["taskkill", "/F", "/PID", str(p.pid)], capture_output=True)
    print("result exists:", __import__("os").path.exists(RES))


main()
