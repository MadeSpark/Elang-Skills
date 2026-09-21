#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
opens_e_probe.py —— 判定「e.exe 打开某个 .e 会不会崩」

给一组 .e，逐个：拷到独立工作目录 → 启动 e.exe <该.e> → 观察存活/退出码（12s）→
精确 PID 杀 → 报告。用于把「崩溃是 .e 造成的」与「崩溃是我们的库造成的」分开。
退出码 0xC0000005(3221225477)=ACCESS_VIOLATION。

用法: python opens_e_probe.py <e1> [e2 ...]
"""
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
E_EXE = r"D:\ides\e\e.exe"
E_DIR = r"D:\ides\e"
WORKBASE = os.path.join(HERE, "ework_probe")
OBSERVE_SEC = 12


def run_one(src_e):
    name = os.path.splitext(os.path.basename(src_e))[0]
    work = os.path.join(WORKBASE, name)
    if os.path.exists(work):
        shutil.rmtree(work)
    os.makedirs(work)
    dst = os.path.join(work, os.path.basename(src_e))
    shutil.copy2(src_e, dst)

    before = os.listdir(work)
    p = subprocess.Popen([E_EXE, dst], cwd=E_DIR,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pid = p.pid
    t0 = time.time()
    rc = None
    while time.time() - t0 < OBSERVE_SEC:
        rc = p.poll()
        if rc is not None:
            break
        time.sleep(0.5)
    alive = (rc is None)
    if alive:
        subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True, text=True)
        try:
            p.wait(timeout=10)
        except Exception:
            pass
    after = os.listdir(work)
    new = sorted(set(after) - set(before))
    return {
        "e": src_e,
        "size": os.path.getsize(src_e),
        "alive_after_%ds" % OBSERVE_SEC: alive,
        "exit_code": rc,
        "exit_hex": ("0x%08X" % (rc & 0xFFFFFFFF)) if rc is not None else None,
        "new_files": new,
    }


def main():
    if len(sys.argv) < 2:
        print("usage: opens_e_probe.py <e1> [e2 ...]")
        return 2
    os.makedirs(WORKBASE, exist_ok=True)
    for e in sys.argv[1:]:
        if not os.path.isfile(e):
            print("SKIP (missing): %s" % e)
            continue
        r = run_one(e)
        verdict = "OK(存活)" if r["alive_after_%ds" % OBSERVE_SEC] else \
                  ("CRASH" if r["exit_code"] == 3221225477 else "EXIT")
        print("[%s] %-40s size=%d exit=%s new_files=%s"
              % (verdict, os.path.basename(e), r["size"], r["exit_hex"], r["new_files"]), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
