#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""probe_enum.py —— 不抓像素、不调用会阻塞的窗口 API 的轻量可见性验证。

背景：probe_wfp 的 sweep 用 `GetWindowTextA`（对别的进程窗口 = 发 WM_GETTEXT 并等待），
桌面上一旦出现「挂起的全屏窗口」每点阻塞数毫秒，整个探针瘫痪（2026-09-23 实测 3.3s/次）。
本脚本只用 EnumWindows / GetClassName / IsWindowVisible / GetWindowRect —— 全部不发消息。

用法：python probe_enum.py <exe名> <观察秒>
判据：目标新 pid 的窗口 IsWindowVisible=TRUE 且 rect 与屏幕相交 ⇒ 「画到屏幕上」的必要条件。
"""
import ctypes
import os
import sys
import time
from ctypes import wintypes as wt

u32 = ctypes.WinDLL("user32", use_last_error=True)
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)

u32.GetClassNameA.argtypes = [wt.HWND, ctypes.c_char_p, ctypes.c_int]
u32.IsWindowVisible.argtypes = [wt.HWND]
u32.IsWindowVisible.restype = wt.BOOL
u32.GetWindowRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
u32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]


def proc_name(pid):
    k32.OpenProcess.restype = wt.HANDLE
    h = k32.OpenProcess(0x1000, False, pid)
    if not h:
        return None
    try:
        buf = ctypes.create_unicode_buffer(1024)
        n = wt.DWORD(len(buf))
        if not k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n)):
            return None
        return os.path.basename(buf.value).lower()
    finally:
        k32.CloseHandle(h)


def all_pids():
    arr = (wt.DWORD * 4096)()
    need = wt.DWORD()
    if not psapi.EnumProcesses(arr, ctypes.sizeof(arr), ctypes.byref(need)):
        return set()
    return {arr[i] for i in range(need.value // 4) if arr[i]}


WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
u32.EnumWindows.argtypes = [WNDENUMPROC, wt.LPARAM]


def snapshot(target_pids):
    """返回 {pid: [(cls, visible, rect), ...]}"""
    out = {}

    @WNDENUMPROC
    def cb(h, lp):
        pid = wt.DWORD()
        u32.GetWindowThreadProcessId(h, ctypes.byref(pid))
        if pid.value in target_pids:
            cls = ctypes.create_string_buffer(64)
            u32.GetClassNameA(h, cls, 64)
            r = wt.RECT()
            u32.GetWindowRect(h, ctypes.byref(r))
            out.setdefault(pid.value, []).append(
                (cls.value.decode("gbk", "replace"), bool(u32.IsWindowVisible(h)),
                 (r.left, r.top, r.right, r.bottom)))
        return True

    u32.EnumWindows(cb, 0)
    return out


def main():
    want = sys.argv[1].lower()
    secs = float(sys.argv[2])
    vx, vy, vw, vh = 0, 0, u32.GetSystemMetrics(78), u32.GetSystemMetrics(79)

    base = {p for p in all_pids() if proc_name(p) == want}
    targets = set(base)
    t_end = time.time() + secs
    visible_hits = []
    while time.time() < t_end:
        targets |= {p for p in all_pids() if proc_name(p) == want}
        if targets - base:
            snap = snapshot(targets - base)
            for pid, wins in snap.items():
                for cls, vis, rect in wins:
                    if vis and rect[2] > vx and rect[3] > vy and rect[0] < vx + vw and rect[1] < vy + vh:
                        visible_hits.append((round(time.time() % 1000, 1), pid, cls, rect))
        time.sleep(0.15)

    print("目标新 pid = %s（启动前已有 %s 不计入）" % (sorted(targets - base) or "未起", sorted(base) or "无"))
    if visible_hits:
        print("❌ 发现可见且与屏幕相交的窗口 %d 窗次：" % len(visible_hits))
        seen = set()
        for _, pid, cls, rect in visible_hits:
            if (pid, cls, rect) not in seen:
                seen.add((pid, cls, rect))
                print("   pid=%d cls=%s rect=%s" % (pid, cls, rect))
    else:
        print("✅ 观察窗内无「可见且与屏幕相交」的目标窗口")


if __name__ == "__main__":
    main()
