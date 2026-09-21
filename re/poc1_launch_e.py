#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
poc1_launch_e.py —— 任务 D 的决定性实验

命题：**若一个 .e 工程自身声明使用我们的支持库（Key=elang_addin），
      e.exe 打开该 .e 时会不会按需从 lib\\ 加载我们的 .fne？—— 从而「免登记」。**

判据（唯一）：带 -DPOC_SELFCHECK 的 .fne 会在 GetNewInf() 里无条件往
`%TEMP%\elang_addin_selfcheck.txt` 追一行。只要出现“本次 e.exe 的 pid”，
就证明 e.exe 真的加载并调用了我们的库。**不用别的现象推断。**

做法：
  1) 备份 lib\\ 快照
  2) 把 elang_addin_diag.fne 投放为 lib\\elang_addin.fne
  3) 把待测 .e 拷到独立工作目录（避免污染）
  4) 启动 e.exe <该.e>（带 ELANG_AI_TRACE/ELANG_AI_TASK）
  5) 轮询 selfcheck / trace
  6) finally：精确 PID 杀 + 移除 .fne + lib\\ 与工作目录 diff 自证
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
FNE_NAME = "elang_addin.fne"
DST = os.path.join(LIB_DIR, FNE_NAME)
TEMP = os.environ.get("TEMP", r"C:\Windows\Temp")
SELFCHK = os.path.join(TEMP, "elang_addin_selfcheck.txt")
TRACE = os.path.join(TEMP, "elang_ai_poc1_trace.txt")
WORK = os.path.join(HERE, "ework")
SRC_E = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "projD_lib.e")
WAIT_SEC = 45


def snap(d):
    out = {}
    for root, _dn, fn in os.walk(d):
        for f in fn:
            p = os.path.join(root, f)
            rel = os.path.relpath(p, d).replace("\\", "/")
            try:
                st = os.stat(p)
                out[rel] = (st.st_size, int(st.st_mtime))
            except OSError:
                pass
    return out


def diff(a, b):
    rows = []
    for k in sorted(set(a) | set(b)):
        if a.get(k) != b.get(k):
            rows.append("  %-50s before=%s after=%s" % (k, a.get(k), b.get(k)))
    return "\n".join(rows) or "  (无差异)"


def main():
    if not os.path.isfile(FNE_DIAG):
        print("[FAIL] 缺少 %s" % FNE_DIAG)
        return 2
    if not os.path.isfile(SRC_E):
        print("[FAIL] 找不到待测 .e: %s" % SRC_E)
        return 2
    if os.path.exists(DST):
        print("[FAIL] %s 已存在，拒绝覆盖" % DST)
        return 2

    before_lib = snap(LIB_DIR)
    if os.path.exists(WORK):
        shutil.rmtree(WORK)
    os.makedirs(WORK)
    work_e = os.path.join(WORK, "probeD.e")
    shutil.copy2(SRC_E, work_e)
    before_work = snap(WORK)

    for f in (SELFCHK, TRACE):
        if os.path.exists(f):
            os.remove(f)

    pid = None
    proc = None
    try:
        shutil.copy2(FNE_DIAG, DST)
        print("[drop] %s -> %s (%d B)" % (os.path.basename(FNE_DIAG), DST, os.path.getsize(DST)), flush=True)

        env = os.environ.copy()
        env["ELANG_AI_TRACE"] = TRACE
        env["ELANG_AI_TASK"] = work_e
        env["ELANG_AI_HIDE"] = "0"
        print("[launch] %s \"%s\"" % (E_EXE, work_e), flush=True)
        proc = subprocess.Popen([E_EXE, work_e], cwd=E_DIR, env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        pid = proc.pid
        print("[launch] pid=%d" % pid, flush=True)

        deadline = time.time() + WAIT_SEC
        hit = False
        saw_ide_ready = False
        while time.time() < deadline:
            if os.path.exists(SELFCHK):
                try:
                    txt = open(SELFCHK, "r", encoding="utf-8", errors="replace").read()
                    if ("pid=%d" % pid) in txt:
                        hit = True
                except OSError:
                    pass
            if os.path.exists(TRACE):
                try:
                    txt = open(TRACE, "r", encoding="utf-8", errors="replace").read()
                except OSError:
                    txt = ""
                if "NL_IDE_READY" in txt:
                    saw_ide_ready = True
                # 等到我们的只读探测跑完（STEP3）再收工；否则等满超时
                if "STEP3 read-only probe end" in txt:
                    break
            if proc.poll() is not None:
                print("[warn] e.exe 提前退出 exit=%s" % proc.returncode, flush=True)
                break
            time.sleep(0.5)

        print("[result] selfcheck 命中本次 pid: %s" % hit, flush=True)
        print("[result] 是否收到 NL_IDE_READY: %s" % saw_ide_ready, flush=True)
        if os.path.exists(SELFCHK):
            print("---- selfcheck ----")
            print(open(SELFCHK, "r", encoding="utf-8", errors="replace").read())
        else:
            print("---- selfcheck 文件不存在 ----")
        if os.path.exists(TRACE):
            print("---- trace ----")
            print(open(TRACE, "r", encoding="utf-8", errors="replace").read())
        else:
            print("---- trace 文件不存在 ----")
    finally:
        if pid:
            print("[cleanup] kill pid=%d" % pid, flush=True)
            subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True, text=True)
            if proc is not None:
                try:
                    proc.wait(timeout=10)
                except Exception:
                    pass
        if os.path.exists(DST):
            os.remove(DST)
            print("[cleanup] removed %s" % DST, flush=True)
        print("[cleanup] lib\\ diff:")
        print(diff(before_lib, snap(LIB_DIR)))
        print("[cleanup] work dir diff:")
        print(diff(before_work, snap(WORK)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
