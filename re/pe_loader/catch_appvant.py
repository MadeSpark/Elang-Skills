#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""抓 Appvant.exe 的真身：全路径 + 父进程 pid。

用 ctypes 直接走 kernel32/ntdll：
  CreateToolhelp32Snapshot 枚举进程拿到 pid
  OpenProcess + QueryFullProcessImageNameW 拿到全路径
  NtQueryInformationProcess(ProcessBasicInformation) 拿父 pid
"""
import ctypes
import ctypes.wintypes as wt
import pathlib
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8")

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
ntdll = ctypes.WinDLL("ntdll")

TH32CS_SNAPPROCESS = 0x00000002
PROC_QUERY_LIMITED = 0x1000
PROCESS_QUERY_INFORMATION = 0x0400
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD),
                ("th32ProcessID", wt.DWORD), ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)),
                ("th32ModuleID", wt.DWORD), ("cntThreads", wt.DWORD),
                ("th32ParentProcessID", wt.DWORD), ("pcPriClassBase", ctypes.c_long),
                ("dwFlags", wt.DWORD), ("szExeFile", wt.WCHAR * 260)]


class PROCESS_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [("Reserved1", ctypes.c_void_p),
                ("PebBaseAddress", ctypes.c_void_p),
                ("Reserved2", ctypes.c_void_p * 2),
                ("UniqueProcessId", ctypes.c_void_p),
                ("InheritedFromUniqueProcessId", ctypes.c_void_p)]


def enum_procs():
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snap == INVALID_HANDLE_VALUE:
        return []
    pe = PROCESSENTRY32W()
    pe.dwSize = ctypes.sizeof(pe)
    out = []
    ok = k32.Process32FirstW(snap, ctypes.byref(pe))
    while ok:
        out.append((pe.th32ProcessID, pe.szExeFile, pe.th32ParentProcessID))
        ok = k32.Process32NextW(snap, ctypes.byref(pe))
    k32.CloseHandle(snap)
    return out


def image_path(pid):
    for acc in (PROC_QUERY_LIMITED, PROCESS_QUERY_INFORMATION):
        h = k32.OpenProcess(acc, False, pid)
        if h:
            buf = ctypes.create_unicode_buffer(1024)
            size = wt.DWORD(1024)
            if k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size)):
                k32.CloseHandle(h)
                return buf.value
            k32.CloseHandle(h)
    return None


def parent_of(pid):
    h = k32.OpenProcess(PROCESS_QUERY_INFORMATION, False, pid)
    if not h:
        return None
    pbi = PROCESS_BASIC_INFORMATION()
    rl = wt.ULONG(0)
    st = ntdll.NtQueryInformationProcess(h, 0, ctypes.byref(pbi),
                                         ctypes.sizeof(pbi), ctypes.byref(rl))
    k32.CloseHandle(h)
    if st != 0:
        return None
    return int(pbi.InheritedFromUniqueProcessId or 0)


def main():
    here = pathlib.Path(__file__).resolve().parent
    share = here / "share"
    kernel = share / "kernel" / "node.dll"
    env = {"MBKIT_SILENT": "1", "MBKIT_TEST_NODE": str(kernel),
           "SystemRoot": r"C:\Windows", "PATH": r"C:\Windows\System32;C:\Windows"}

    base = {p[0] for p in enum_procs()}
    print("基线进程数 =", len(base))

    kids = []
    for tag in ("A", "B"):
        d = share / tag
        kids.append((tag, subprocess.Popen([str(d / "probe.exe")], cwd=str(d), env=env,
                                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)))

    print("已启动 A / B，开始追踪新进程…\n")
    seen, t0 = {}, time.time()
    while any(p.poll() is None for _, p in kids) and time.time() - t0 < 90:
        for pid, name, ppid in enum_procs():
            if pid in base or pid in seen:
                continue
            seen[pid] = name
            path = image_path(pid)
            gp = parent_of(pid)
            print("[+%.2fs] pid=%d  %s" % (time.time() - t0, pid, name))
            print("          路径     : %s" % path)
            print("          父进程pid: %s" % gp)
            for t, pp in kids:
                if pp.pid == gp:
                    print("          ★ 父是本实验的探针 %s" % t)
            print("          父进程名 : %s" % next((n for p, n, _ in enum_procs() if p == gp), "?"))
        time.sleep(0.05)

    for tag, p in kids:
        print("%s 退出码=%s" % (tag, p.wait()))
    print("\n本次共观察到 %d 个新进程" % len(seen))


if __name__ == "__main__":
    main()
