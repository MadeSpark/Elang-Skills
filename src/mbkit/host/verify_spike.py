# -*- coding: utf-8 -*-
"""验证 MbKit 辅助进程原型的三条硬命题（Toolhelp32 枚举，无需额外依赖）：

  命题 A：内核 node.dll 只出现在**辅助进程**里，客户端(插件)进程里没有。
  命题 B：杀掉辅助进程后，该 pid 连同 node.dll 一起消失（"锁"随之释放）。
  命题 C：同一时刻可以有两个客户端 ↔ 两个辅助进程，各自独立。

用法: python verify_spike.py [客户端个数，默认 2]
"""
import ctypes
import ctypes.wintypes as wt
import subprocess
import sys
import time
import os

k = ctypes.windll.kernel32
HERE = os.path.dirname(os.path.abspath(__file__))
NODE = r"D:\DevelopmentFolder\插件开发目录\E\插件\框架-爱丽\main\corn\node.dll"
URL = "file:///" + HERE.replace("\\", "/") + "/spike.html"
INVALID = ctypes.c_void_p(-1).value

TH32CS_SNAPPROCESS = 0x00000002
TH32CS_SNAPMODULE = 0x00000008
TH32CS_SNAPMODULE32 = 0x00000010


class PROCESSENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)), ("th32ModuleID", wt.DWORD),
                ("cntThreads", wt.DWORD), ("th32ParentProcessID", wt.DWORD),
                ("pcPriClassBase", ctypes.c_long), ("dwFlags", wt.DWORD),
                ("szExeFile", ctypes.c_char * 260)]


class MODULEENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("th32ModuleID", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("GlblcntUsage", wt.DWORD), ("ProccntUsage", wt.DWORD),
                ("modBaseAddr", ctypes.POINTER(ctypes.c_byte)), ("modBaseSize", wt.DWORD),
                ("hModule", wt.HMODULE), ("szModule", ctypes.c_char * 256),
                ("szExePath", ctypes.c_char * 260)]


def procs_named(name):
    out = []
    snap = k.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snap == INVALID or snapshot_failed(snap):
        return out
    e = PROCESSENTRY32(); e.dwSize = ctypes.sizeof(e)
    ok = k.Process32First(snap, ctypes.byref(e))
    while ok:
        try:
            exe = e.szExeFile.decode("gbk", "ignore")
        except Exception:
            exe = ""
        if exe.lower() == name.lower():
            out.append(e.th32ProcessID)
        ok = k.Process32Next(snap, ctypes.byref(e))
    k.CloseHandle(snap)
    return out


def snapshot_failed(snap):
    try:
        return snap == 0 or snap == INVALID
    except Exception:
        return True


def modules(pid, needle="node.dll"):
    found = []
    snap = k.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    if snap == INVALID or snapshot_failed(snap):
        return found
    e = MODULEENTRY32(); e.dwSize = ctypes.sizeof(e)
    ok = k.Module32First(snap, ctypes.byref(e))
    while ok:
        try:
            nm = e.szModule.decode("gbk", "ignore")
        except Exception:
            nm = ""
        if needle.lower() in nm.lower():
            found.append((nm, int(ctypes.cast(e.modBaseAddr, ctypes.c_void_p).value or 0),
                          e.modBaseSize))
        ok = k.Module32Next(snap, ctypes.byref(e))
    k.CloseHandle(snap)
    return found


def main():
    n_cli = int(sys.argv[1]) if len(sys.argv) > 1 else 2
    exe_cli = os.path.join(HERE, "spike_cli.exe")
    node_in_client = []       # [(tag, module)]
    seen = {}                 # pid -> [(t, node.dll 命中)]
    timelines = {}            # tag -> [(t, hostpid, 是否已死)]
    clients = []

    print("== 起 %d 个客户端（每个都在 3.5s 后杀掉自己的辅助进程并重新拉起）==" % n_cli)
    for i in range(n_cli):
        tag = "v%d" % i
        p = subprocess.Popen([exe_cli, NODE, URL, "700", "420", "16000", tag, "--restart"],
                             cwd=HERE)
        clients.append((tag, p))
        print("   client %s pid=%d" % (tag, p.pid))
    t0 = time.time()

    while time.time() - t0 < 18:
        el = time.time() - t0
        hpids = procs_named("spike_host.exe")
        for pid in hpids:
            mods = modules(pid)
            seen.setdefault(pid, []).append((round(el, 1), bool(mods),
                                             mods[0][1] if mods else 0))

        for tag, p in clients:
            if p.poll() is None:
                mods = modules(p.pid)
                if mods:
                    node_in_client.append((tag, p.pid, mods))
        time.sleep(0.25)

    for tag, p in clients:
        p.wait(timeout=30)

    print("\n== 辅助进程 / node.dll 时间线 ==")
    print("  （'曾命中'= 采样期间任一时刻在模块表里看到 node.dll；首个采样可能落在内核还没载入完的启动瞬间）")
    for pid, rec in sorted(seen.items()):
        r0 = rec[0][0]
        r1 = rec[-1][0]
        ever = any(r[1] for r in rec)
        base = next((r[2] for r in rec if r[2]), 0)
        print("  host pid=%-6d 观测 %5.1fs..%5.1fs  曾命中=%s  base=0x%08X"
              % (pid, r0, r1, "是" if ever else "否", base))
    n_hit = sum(1 for rec in seen.values() if any(r[1] for r in rec))

    print("\n== 客户端进程里的 node.dll ==")
    if node_in_client:
        for tag, pid, mods in node_in_client:
            print("  !! client %s pid=%d 里发现了 node.dll: %s" % (tag, pid, mods))
    else:
        print("  客户端进程**从未**出现 node.dll  ✔（内核完全在辅助进程里）")

    still = procs_named("spike_host.exe")
    print("\n== 收尾 ==")
    print("  结束时仍存活的辅助进程: %s" % (still or "无 ✔"))
    ok = (not node_in_client) and (not still) and len(seen) >= n_cli and n_hit == len(seen)
    print("\n结论: %s" % ("PASS ✔" if ok else "FAIL ✘"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
