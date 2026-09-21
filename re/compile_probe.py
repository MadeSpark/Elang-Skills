# -*- coding: utf-8 -*-
"""静态编译实弹探针：带环境变量启动加壳工程，枚举弹窗 + 等产物出现，最后按 PID 清理。"""
import io
import os
import subprocess
import sys
import time
import ctypes
from ctypes import wintypes

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

E_EXE = r"D:\ides\e\e.exe"
user32 = ctypes.WinDLL("user32", use_last_error=True)
WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def wcls(hwnd):
    b = ctypes.create_unicode_buffer(512)
    user32.GetClassNameW(hwnd, b, 512)
    return b.value


def wtext(hwnd):
    ln = user32.GetWindowTextLengthW(hwnd)
    b = ctypes.create_unicode_buffer(ln + 2)
    user32.GetWindowTextW(hwnd, b, ln + 2)
    return b.value


def main():
    in_e = sys.argv[1]
    expect_out = sys.argv[2]
    wait_s = int(sys.argv[3]) if len(sys.argv) > 3 else 90
    env = dict(os.environ)
    env["ELANG_AI_COMPILE"] = "static_console"
    env["ELANG_AI_EXPECT_OUT"] = expect_out
    env["ELANG_AI_CAPTURE"] = os.path.join(os.path.dirname(expect_out), "compile_capture.txt")
    env["ELANG_AI_HIDE"] = "0"          # 首探：可见窗口，便于观察
    env["ELANG_AI_TRACE"] = os.path.join(os.path.dirname(expect_out), "compile_trace.txt")
    env["ELANG_AI_TASK"] = "ai-compile"

    cmd = [E_EXE, in_e]
    print("launch:", cmd)
    print("expect_out:", expect_out)
    p = subprocess.Popen(cmd, env=env)
    print("pid =", p.pid)

    t0 = time.time()
    printed = set()
    seen_out = False
    while time.time() - t0 < wait_s:
        found = []

        @WNDENUMPROC
        def cb(hwnd, _lp):
            wpid = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
            if wpid.value == p.pid and user32.IsWindowVisible(hwnd):
                found.append((wcls(hwnd), wtext(hwnd)))
            return True

        user32.EnumWindows(cb, 0)
        for c, t in found:
            k = (c, t)
            if k not in printed and (t or c == "#32770"):
                printed.add(k)
                print("[WND] cls=%r title=%r" % (c, t))
        if os.path.exists(expect_out) and not seen_out:
            seen_out = True
            print("[OUT-SEEN]", expect_out, os.path.getsize(expect_out), "B")
            break
        time.sleep(1.0)

    print("result:", "OUT-SEEN" if seen_out else "no-output-in-%ds" % wait_s)
    # 收尾：精确 PID 终止自己的实例
    subprocess.run(["taskkill", "/F", "/PID", str(p.pid)], capture_output=True)
    print("killed own pid", p.pid)


main()
