#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""探测：把 e.exe 起在**独立 Windows 桌面**上，能否做到「用户屏幕上完全看不见」。

    python probe_desktop.py [秒数]

判据：
  1. 用户所在桌面（Default）上，全程枚举不到该 pid 的任何可见窗口；
  2. 独立桌面上确实有窗口（证明 IDE 真的起来了，不是没启动）；
  3. 用户屏幕截图在期间无变化（配合 shot.py）。
"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shot import capture, fingerprint  # noqa: E402

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

EnumWindows = user32.EnumWindows
EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
EnumDesktopWindows = user32.EnumDesktopWindows
GetWindowThreadProcessId = user32.GetWindowThreadProcessId
IsWindowVisible = user32.IsWindowVisible
GetClassNameA = user32.GetClassNameA
GetWindowRect = user32.GetWindowRect

CreateDesktopW = user32.CreateDesktopW
CreateDesktopW.restype = wt.HANDLE
CreateDesktopW.argtypes = [wt.LPCWSTR, wt.LPCWSTR, ctypes.c_void_p, wt.DWORD, wt.DWORD, ctypes.c_void_p]
OpenDesktopW = user32.OpenDesktopW
OpenDesktopW.restype = wt.HANDLE
OpenDesktopW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.BOOL, wt.DWORD]
CloseDesktop = user32.CloseDesktop
CloseDesktop.argtypes = [wt.HANDLE]

GENERIC_ALL = 0x10000000
DESKTOP_ALL = 0x000F01FF

E_EXE = r"D:\ides\e\e.exe"
E_DIR = r"D:\ides\e"
PROJ = r"C:\Users\MadeSpark\Desktop\测试\re\up_test\console_proj.e"


def enum_win(pid, hdesk=None):
    out = []

    @EnumWindowsProc
    def cb(hwnd, _):
        p = wt.DWORD()
        GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value != pid:
            return True
        cls = ctypes.create_string_buffer(256)
        GetClassNameA(hwnd, cls, 256)
        r = wt.RECT()
        GetWindowRect(hwnd, ctypes.byref(r))
        out.append((hwnd, cls.value.decode("gbk", "replace"), bool(IsWindowVisible(hwnd)),
                    (r.left, r.top, r.right, r.bottom)))
        return True

    if hdesk:
        EnumDesktopWindows(hdesk, cb, 0)
    else:
        EnumWindows(cb, 0)
    return out


def main():
    dur = float(sys.argv[1]) if len(sys.argv) > 1 else 16.0
    dname = "ElangAIHeadless"
    print("== probe_desktop name=%s dur=%ss ==" % (dname, dur))
    print("   当前桌面: %s" % (os.environ.get("SESSIONNAME", "?") if False else "Default(WinSta0)"))

    hdesk = OpenDesktopW(dname, 0, False, DESKTOP_ALL)
    if not hdesk:
        hdesk = CreateDesktopW(dname, None, None, 0, DESKTOP_ALL, None)
    err = ctypes.get_last_error()
    print("   OpenDesktop/CreateDesktop -> 0x%08X (err=%d)" % (hdesk or 0, err))
    if not hdesk:
        print("   !! 创建/打开桌面失败，无法继续")
        return 1

    base = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_shot_base.png")
    capture(base)
    fp0 = fingerprint(base)

    si = subprocess.STARTUPINFO()
    si.lpDesktop = dname
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 1          # 故意 SHOWNORMAL：靠桌面隔离，不靠 SW_HIDE

    t0 = time.time()
    proc = subprocess.Popen([E_EXE, PROJ], cwd=E_DIR, startupinfo=si,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pid = proc.pid
    print("   pid = %d   (故意用 SW_SHOWNORMAL 起，验证桌面隔离本身够不够)" % pid)

    on_user_desktop = []
    on_ai_desktop = []
    shot_fps = []
    last_shot = 0.0
    try:
        while time.time() - t0 < dur:
            if proc.poll() is not None:
                print("   [%.2fs] 进程退出 rc=%s" % (time.time() - t0, proc.returncode))
                break
            el = time.time() - t0
            for hwnd, cls, vis, rect in enum_win(pid):
                if vis and cls not in [c for c, *_ in on_user_desktop]:
                    on_user_desktop.append((cls, hwnd, el))
                    print("   [%.2fs] !!用户桌面上可见 0x%08X %-22s %s" % (el, hwnd, cls[:22], rect))
            for hwnd, cls, vis, rect in enum_win(pid, hdesk):
                if vis and cls not in [c for c, *_ in on_ai_desktop]:
                    on_ai_desktop.append((cls, hwnd, el))
                    print("   [%.2fs]   独立桌面可见 0x%08X %-22s %s" % (el, hwnd, cls[:22], rect))
            if el - last_shot > 1.0:
                last_shot = el
                p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_shot_t%02d.png" % int(el))
                capture(p)
                shot_fps.append((el, fingerprint(p)))
            time.sleep(0.01)
    finally:
        if proc.poll() is None:
            subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)

    print("---- 结论 ----")
    print("   用户桌面可见窗口 : %d  %s" % (len(on_user_desktop), [c for c, *_ in on_user_desktop]))
    print("   独立桌面可见窗口 : %d  %s" % (len(on_ai_desktop), [c for c, *_ in on_ai_desktop]))
    changed = [t for t, f in shot_fps if f != fp0]
    print("   用户屏幕截图变化 : %d 处（时刻 %s）" % (len(changed), ["%.0fs" % t for t in changed]))
    print("   截图前缀         : _shot_*.png (工作区 %s)" % os.path.dirname(base))
    CloseDesktop(hdesk)
    return 0


if __name__ == "__main__":
    sys.exit(main())
