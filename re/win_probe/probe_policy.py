#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""策略 A/B/C：几种「无头」手段各自能不能做到屏幕上看不见。

    python probe_policy.py <none|startupinfo|pollhide|parkmain|parkall> [秒数]

严格判据（每次采样，对 e.exe 的每个顶层窗口）：
    可见(WS_VISIBLE) 且 面积>0 且 与虚拟屏幕矩形有交集   →  算一次「露脸」
只要全程 0 次露脸，就算「用户屏幕上看不见」。

策略：
  none        什么都不做（= 当前 elaunch 的行为，基线）
  startupinfo CreateProcess 的 STARTUPINFO.wShowWindow = SW_HIDE（纯 ctypes 原生调用）
  pollhide    startupinfo + 轮询把所有可见顶层窗口 SW_HIDE
  parkmain    startupinfo + 轮询把主窗口 ENewFrame 隐藏并移到屏幕外
  parkall     startupinfo + 轮询把所有顶层窗口隐藏并移到屏幕外（最激进）
"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shot import capture  # noqa: E402
from probe_desktop_raw import STARTUPINFOA, PROCESS_INFORMATION, CreateProcessW, k32  # noqa: E402

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

u32 = ctypes.WinDLL("user32", use_last_error=True)
EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
EnumWindows = u32.EnumWindows
GetWindowThreadProcessId = u32.GetWindowThreadProcessId
IsWindowVisible = u32.IsWindowVisible
GetClassNameA = u32.GetClassNameA
GetWindowTextA = u32.GetWindowTextA
GetWindowRect = u32.GetWindowRect
ShowWindow = u32.ShowWindow
SetWindowPos = u32.SetWindowPos
SW_HIDE = 0
SWP_NOSIZE, SWP_NOZORDER, SWP_NOACTIVATE = 0x0001, 0x0004, 0x0010
OFFSCREEN = -32000

E_EXE = r"D:\ides\e\e.exe"
PROJ = r"C:\Users\MadeSpark\Desktop\测试\re\up_test\console_proj.e"
MAIN_CLASS = "ENewFrame"


def tops(pid):
    out = []

    @EnumWindowsProc
    def cb(hwnd, _):
        p = wt.DWORD()
        GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value != pid:
            return True
        c = ctypes.create_string_buffer(256)
        GetClassNameA(hwnd, c, 256)
        t = ctypes.create_string_buffer(512)
        GetWindowTextA(hwnd, t, 512)
        r = wt.RECT()
        GetWindowRect(hwnd, ctypes.byref(r))
        out.append({"hwnd": hwnd, "cls": c.value.decode("gbk", "replace"),
                    "title": t.value.decode("gbk", "replace"),
                    "vis": bool(IsWindowVisible(hwnd)),
                    "rect": (r.left, r.top, r.right, r.bottom)})
        return True

    EnumWindows(cb, 0)
    return out


def overlaps_screen(rect):
    l, t, r, b = rect
    if r <= l or b <= t:
        return False                       # 零面积
    vx = u32.GetSystemMetrics(76)
    vy = u32.GetSystemMetrics(77)
    vw = u32.GetSystemMetrics(78)
    vh = u32.GetSystemMetrics(79)
    return not (r <= vx or l >= vx + vw or b <= vy or t >= vy + vh)


def launch(exe, args, hide):
    si = STARTUPINFOA()
    si.cb = ctypes.sizeof(si)
    if hide:
        si.dwFlags = 0x00000001
        si.wShowWindow = SW_HIDE
    pi = PROCESS_INFORMATION()
    cmd = " ".join(['"%s"' % exe] + ['"%s"' % a for a in args])
    ok = CreateProcessW(None, ctypes.create_unicode_buffer(cmd), None, None, False, 0,
                        None, None, ctypes.byref(si), ctypes.byref(pi))
    if not ok:
        raise OSError("CreateProcessW failed err=%d" % ctypes.get_last_error())
    return pi


def main():
    policy = (sys.argv[1] if len(sys.argv) > 1 else "none").lower()
    dur = float(sys.argv[2]) if len(sys.argv) > 2 else 16.0
    here = os.path.dirname(os.path.abspath(__file__))

    hide = policy != "none"
    print("== probe_policy policy=%s dur=%ss ==" % (policy, dur))
    t0 = time.time()
    pi = launch(E_EXE, [PROJ], hide)
    pid = pi.dwProcessId
    print("   pid = %d" % pid)

    violations = []           # (t, cls, rect)
    first_seen = {}           # cls -> 首次出现(含不可见)
    shots = []
    next_shot = 0.0
    try:
        while time.time() - t0 < dur:
            if k32.WaitForSingleObject(pi.hProcess, 0) == 0:
                print("   [%.2fs] 进程退出" % (time.time() - t0))
                break
            el = time.time() - t0
            ws = tops(pid)
            for w in ws:
                first_seen.setdefault(w["cls"], el)
            for w in ws:
                if w["vis"] and overlaps_screen(w["rect"]):
                    key = (w["cls"], w["rect"])
                    if len(violations) < 40:
                        violations.append((el, w["cls"], w["rect"]))
            # 施加策略
            if policy == "pollhide":
                for w in ws:
                    if w["vis"]:
                        ShowWindow(w["hwnd"], SW_HIDE)
            elif policy == "parkmain":
                for w in ws:
                    if w["cls"] == MAIN_CLASS:
                        if w["vis"]:
                            ShowWindow(w["hwnd"], SW_HIDE)
                        SetWindowPos(w["hwnd"], 0, OFFSCREEN, OFFSCREEN, 0, 0,
                                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)
            elif policy == "parkall":
                for w in ws:
                    if w["vis"]:
                        ShowWindow(w["hwnd"], SW_HIDE)
                    SetWindowPos(w["hwnd"], 0, OFFSCREEN, OFFSCREEN, 0, 0,
                                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)
            if el >= next_shot:
                next_shot = el + 2.0
                p = os.path.join(here, "shots", "pol_%s_t%02d.png" % (policy, int(el)))
                os.makedirs(os.path.dirname(p), exist_ok=True)
                capture(p)
                shots.append(p)
            time.sleep(0.002)
    finally:
        if k32.WaitForSingleObject(pi.hProcess, 0) != 0:
            k32.TerminateProcess(pi.hProcess, 0)
        k32.CloseHandle(pi.hThread)
        k32.CloseHandle(pi.hProcess)

    print("---- 结果 ----")
    print("   首次出现（含不可见）:")
    for cls, el in first_seen.items():
        print("      %-26s %.2fs" % (cls[:26], el))
    print("   露脸次数（可见 且 与屏幕相交）: %d" % len(violations))
    for t, cls, rect in violations[:12]:
        print("      [%.2fs] %-26s rect=%s" % (t, cls[:26], rect))
    print("   截图: %d 张 -> %s" % (len(shots), os.path.dirname(shots[0]) if shots else "-"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
