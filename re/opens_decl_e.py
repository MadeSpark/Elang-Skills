#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
opens_decl_e.py —— 紧凑版「e.exe 打开声明我们库的 .e」试验台，供 sweep 扫参数用。

用法: python opens_decl_e.py <declaring.e> [wait_sec]
输出一行结论: ALIVE/CRASH + 退出码 + selfcheck命中 + trace 最后一行。
finally 精确 PID 杀 + 移除投放的 .fne。
"""
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ADDIN = os.path.join(HERE, "addin")
E_EXE = r"D:\ides\e\e.exe"
E_DIR = r"D:\ides\e"
LIB_DIR = r"D:\ides\e\lib"
FNE_DIAG = os.path.join(ADDIN, "elang_addin_diag.fne")
DST = os.path.join(LIB_DIR, "elang_addin.fne")
TEMP = os.environ.get("TEMP", r"C:\Windows\Temp")
SELFCHK = os.path.join(TEMP, "elang_addin_selfcheck.txt")
TRACE = os.path.join(TEMP, "elang_ai_poc1_trace.txt")

SRC_E = sys.argv[1]
WAIT = float(sys.argv[2]) if len(sys.argv) > 2 else 18.0


def main():
    if os.path.exists(DST):
        os.remove(DST)
    for f in (SELFCHK, TRACE):
        if os.path.exists(f):
            os.remove(f)
    work = os.path.join(HERE, "ework_decl")
    if os.path.exists(work):
        shutil.rmtree(work)
    os.makedirs(work)
    e_copy = os.path.join(work, "probe.e")
    shutil.copy2(SRC_E, e_copy)

    pid = None
    proc = None
    try:
        shutil.copy2(FNE_DIAG, DST)
        env = os.environ.copy()
        env["ELANG_AI_TRACE"] = TRACE
        env["ELANG_AI_TASK"] = e_copy
        env["ELANG_AI_HIDE"] = "0"
        proc = subprocess.Popen([E_EXE, e_copy], cwd=E_DIR, env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        pid = proc.pid
        t0 = time.time()
        rc = None
        while time.time() - t0 < WAIT:
            rc = proc.poll()
            if rc is not None:
                break
            tr = ""
            if os.path.exists(TRACE):
                try:
                    tr = open(TRACE, "r", encoding="utf-8", errors="replace").read()
                except OSError:
                    tr = ""
            if "STEP3 read-only probe end" in tr:
                break
            time.sleep(0.5)
        alive = (rc is None)
        sc = ""
        if os.path.exists(SELFCHK):
            sc = open(SELFCHK, "r", encoding="utf-8", errors="replace").read()
        hit = ("pid=%d" % pid) in sc
        tr = ""
        if os.path.exists(TRACE):
            tr = open(TRACE, "r", encoding="utf-8", errors="replace").read()
        last = tr.strip().splitlines()[-1] if tr.strip() else "(空)"
        status = "ALIVE" if alive else ("CRASH" if rc == 3221225477 else "EXIT")
        print("  -> %-6s exit=%s selfcheck=%s trace_lines=%d"
              % (status, ("0x%08X" % (rc & 0xFFFFFFFF)) if rc is not None else "-",
                 hit, len(tr.strip().splitlines())))
        print("     trace_last: %s" % last[:160])
        print("     NL_IDE_READY=%s  STEP3_done=%s"
              % ("NL_IDE_READY" in tr, "STEP3 read-only probe end" in tr))
    finally:
        if pid:
            subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True, text=True)
            if proc is not None:
                try:
                    proc.wait(timeout=10)
                except Exception:
                    pass
        if os.path.exists(DST):
            os.remove(DST)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
