#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""probe_proc.py —— 启动 exe，列出它**真实加载的模块** + 全部窗口 + 子窗口文字。

用来回答：node.dll 到底加载上没有？MbKit 是不是手工映射（不在模块表）？
有没有弹错误框？  用法：python probe_proc.py <exe> [等待秒]
"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

sys.dont_write_bytecode = True
u32 = ctypes.windll.user32
k32 = ctypes.windll.kernel32
psapi = ctypes.windll.psapi

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
LIST_MODULES_ALL = 0x03


def modules(pid):
    # 必须显式声明 argtypes：HMODULE 是 64 位指针，靠默认推断会报
    # OverflowError: int too long to convert（踩过）
    psapi.EnumProcessModulesEx.argtypes = [
        wt.HANDLE, ctypes.POINTER(wt.HMODULE), wt.DWORD,
        ctypes.POINTER(wt.DWORD), wt.DWORD]
    psapi.EnumProcessModulesEx.restype = wt.BOOL
    psapi.GetModuleBaseNameW.argtypes = [wt.HANDLE, wt.HMODULE, wt.LPWSTR, wt.DWORD]
    psapi.GetModuleBaseNameW.restype = wt.DWORD
    psapi.GetModuleFileNameExW.argtypes = [wt.HANDLE, wt.HMODULE, wt.LPWSTR, wt.DWORD]
    psapi.GetModuleFileNameExW.restype = wt.DWORD

    h = k32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
    if not h:
        return None
    arr = (wt.HMODULE * 1024)()
    need = wt.DWORD()
    if not psapi.EnumProcessModulesEx(h, arr, ctypes.sizeof(arr),
                                      ctypes.byref(need), LIST_MODULES_ALL):
        k32.CloseHandle(h)
        return None
    n = need.value // ctypes.sizeof(wt.HMODULE)
    out = []
    for i in range(n):
        base = arr[i]
        if base is None:
            continue
        buf = ctypes.create_unicode_buffer(512)
        psapi.GetModuleBaseNameW(h, base, buf, 512)
        p = ctypes.create_unicode_buffer(1024)
        psapi.GetModuleFileNameExW(h, base, p, 1024)
        out.append((buf.value, base or 0, p.value))
    k32.CloseHandle(h)
    return out


def text_of(hwnd):
    n = u32.GetWindowTextLengthW(hwnd)
    b = ctypes.create_unicode_buffer(n + 2)
    u32.SendMessageW(hwnd, 0x000D, n + 1, ctypes.byref(b))
    return b.value


def walk(hwnd, depth, acc):
    cls = ctypes.create_unicode_buffer(256)
    u32.GetClassNameW(hwnd, cls, 256)
    r = wt.RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(r))
    t = text_of(hwnd)
    acc.append("  " * depth + "hwnd=0x%08X vis=%s cls=%-20s %dx%d text=%r"
               % (hwnd, bool(u32.IsWindowVisible(hwnd)), cls.value,
                  r.right - r.left, r.bottom - r.top, t))
    if depth < 3:
        ch = []

        @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
        def cb(h, _):
            ch.append(h)
            return True

        u32.EnumChildWindows(hwnd, cb, 0)
        for c in ch:
            walk(c, depth + 1, acc)


def main():
    exe = os.path.abspath(sys.argv[1])
    wait = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
    cwd = os.path.dirname(exe)
    env = dict(os.environ)
    if len(sys.argv) > 3:
        for kv in sys.argv[3:]:
            k, v = kv.split("=", 1)
            env[k] = v
            print("env %s=%s" % (k, v))
    p = subprocess.Popen([exe], cwd=cwd, env=env)
    print("pid = %d  cwd = %s" % (p.pid, cwd))
    try:
        time.sleep(wait)
        ms = modules(p.pid)
        print("\n==== 已加载模块（只列非系统）====")
        if ms is None:
            print("  (打不开进程)")
        else:
            for name, base, path in ms:
                low = name.lower()
                if low.startswith(("api-ms", "ext-ms")) or low in (
                        "ntdll.dll", "kernel32.dll", "kernelbase.dll", "user32.dll",
                        "gdi32.dll", "gdi32full.dll", "msvcrt.dll", "ucrtbase.dll",
                        "advapi32.dll", "shell32.dll", "shlwapi.dll", "ole32.dll",
                        "oleaut32.dll", "combase.dll", "rpcrt4.dll", "sechost.dll",
                        "imm32.dll", "win32u.dll", "msctf.dll", "comctl32.dll",
                        "version.dll", "ws2_32.dll", "winmm.dll", "crypt32.dll",
                        "bcrypt.dll", "ncrypt.dll", "dwmapi.dll", "uxtheme.dll",
                        "mswsock.dll", "dnsapi.dll", "iphlpapi.dll", "userenv.dll",
                        "profapi.dll", "cfgmgr32.dll", "bcryptprimitives.dll",
                        "msvcp_win.dll", "psapi.dll", "kernel.appcore.dll"):
                    continue
                print("   %-28s base=0x%08X  %s" % (name, base, path))
        print("   ———— 关键判定 ————")
        if ms is not None:
            names = {m[0].lower() for m in ms}
            print("   node.dll 加载: %s" % ("是" if "node.dll" in names else "★ 否"))
            print("   MbKit.dll 在模块表: %s（应为「否」= 手工映射）"
                  % ("★ 是" if "mbkit.dll" in names else "否"))
        acc = []

        @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
        def cb(hwnd, _):
            wpid = wt.DWORD()
            u32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
            if wpid.value == p.pid:
                walk(hwnd, 0, acc)
            return True

        u32.EnumWindows(cb, 0)
        print("\n==== 窗口树 ====")
        for line in acc:
            print("  " + line)
        print("\n进程状态: %s" % ("存活" if p.poll() is None else "已退出 %s" % p.returncode))
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
