#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""find_plugin.py —— 在机器上找「谁是宿主、插件 DLL 在不在加载器链表里」。

判据来源：AILI·LIFE 是 Windows动态链接库 工程，由 小栗子虚拟机 之类的宿主加载。
如果宿主的模块表里**找不到** AILI·LIFE.dll，就说明宿主是**手工映射**插件的 ——
那 MbKit 的 `mod_of()` 查不到业务模块，回调会被「保守丢弃」，事件全哑且不崩。

用法：python find_plugin.py [关键字...]（默认：AILI、xlz、小栗子、XiaoLiZi、miniblink）
"""
import ctypes
import ctypes.wintypes as wt
import sys
import os

sys.dont_write_bytecode = True
k32 = ctypes.windll.kernel32
psapi = ctypes.windll.psapi
u32 = ctypes.windll.user32

psapi.EnumProcesses.argtypes = [ctypes.POINTER(wt.DWORD), wt.DWORD, ctypes.POINTER(wt.DWORD)]
psapi.EnumProcessModulesEx.argtypes = [wt.HANDLE, ctypes.POINTER(wt.HMODULE), wt.DWORD,
                                       ctypes.POINTER(wt.DWORD), wt.DWORD]
psapi.GetModuleBaseNameW.argtypes = [wt.HANDLE, wt.HMODULE, wt.LPWSTR, wt.DWORD]
psapi.GetModuleFileNameExW.argtypes = [wt.HANDLE, wt.HMODULE, wt.LPWSTR, wt.DWORD]

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
LIST_MODULES_ALL = 0x03


def mods(pid):
    h = k32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
    if not h:
        return None
    arr = (wt.HMODULE * 2048)()
    need = wt.DWORD()
    ok = psapi.EnumProcessModulesEx(h, arr, ctypes.sizeof(arr), ctypes.byref(need), LIST_MODULES_ALL)
    out = []
    if ok:
        for i in range(need.value // ctypes.sizeof(wt.HMODULE)):
            if not arr[i]:
                continue
            b = ctypes.create_unicode_buffer(512)
            psapi.GetModuleBaseNameW(h, arr[i], b, 512)
            p = ctypes.create_unicode_buffer(1024)
            psapi.GetModuleFileNameExW(h, arr[i], p, 1024)
            out.append((b.value, arr[i] or 0, p.value))
    k32.CloseHandle(h)
    return out


def main():
    keys = sys.argv[1:] or ["AILI", "xlz", "小栗子", "XiaoLiZi", "miniblink", "MbKit"]
    n = 4096
    buf = (wt.DWORD * n)()
    need = wt.DWORD()
    psapi.EnumProcesses(buf, ctypes.sizeof(buf), ctypes.byref(need))
    cnt = need.value // ctypes.sizeof(wt.DWORD)
    print("进程总数 %d，关键字 %s\n" % (cnt, keys))
    for i in range(cnt):
        pid = buf[i]
        if not pid:
            continue
        ms = mods(pid)
        if ms is None:
            continue
        names = [m[0] for m in ms]
        hit = any(any(k.lower() in (nm or "").lower() for k in keys) for nm in names)
        if not hit:
            continue
        print("=" * 78)
        print("PID %d  模块数 %d" % (pid, len(ms)))
        # 进程自身的可执行名
        exe = ms[0][2] if ms else "?"
        print("   主模块: %s" % exe)
        for nm, base, path in ms:
            low = nm.lower()
            if any(k.lower() in low for k in keys) or low.endswith(".dll") and (
                    "lz" in low or "aili" in low or "miniblink" in low or "mbkit" in low):
                print("   ★ %-24s base=0x%08X  %s" % (nm, base, path))
        # 判定
        ail = [m for m in ms if "aili" in (m[0] or "").lower()]
        print("   → 命中 AILI 模块: %s" % ("有" if ail else "★ 没有（疑似手工映射/不在链表）"))
    print("\n完成")


if __name__ == "__main__":
    main()
