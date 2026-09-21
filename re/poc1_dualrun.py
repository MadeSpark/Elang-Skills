#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
poc1_dualrun.py —— 判定 e.exe 的“库列表”到底是
      (A) 每次启动实时扫描 lib\\*.fne                -> 单次运行就该加载
      (B) 启动时扫描、干净退出时把列表缓存起来        -> 需要“先干净退一次，再启动”
      (C) 只在“工具→支持库配置”确认后才登记          -> 两次运行都不会加载

做法：投放 .fne 后连续启动两次 e.exe：
  第一次：等主窗口出现，然后**优雅关闭**（taskkill 不带 /F = 发 WM_CLOSE），
          让它有机会把 lib 列表写回缓存；
  第二次：再启动，观察我们的 trace / selfcheck 是否出现。
全程 finally 清理（杀进程 + 移除 .fne + diff 自证）。
"""
import os
import shutil
import subprocess
import time

BASE = os.path.dirname(os.path.abspath(__file__))
ADDIN = os.path.join(BASE, "addin")
E_EXE = r"D:\ides\e\e.exe"
E_DIR = r"D:\ides\e"
LIB_DIR = r"D:\ides\e\lib"
FNE_SRC = os.path.join(ADDIN, "elang_addin.fne")
FNE_NAME = "elang_addin.fne"
DST = os.path.join(LIB_DIR, FNE_NAME)
TRACE = os.path.join(os.environ.get("TEMP", r"C:\Windows\Temp"), "elang_ai_poc1_trace.txt")
SELFCHK = os.path.join(os.environ.get("TEMP", r"C:\Windows\Temp"), "elang_addin_selfcheck.txt")


def snap():
    d = {}
    for root, _dn, fn in os.walk(LIB_DIR):
        for f in fn:
            p = os.path.join(root, f)
            rel = os.path.relpath(p, LIB_DIR).replace("\\", "/")
            try:
                st = os.stat(p)
                d[rel] = (st.st_size, int(st.st_mtime))
            except OSError:
                pass
    return d


def diff(a, b):
    out = []
    for k in sorted(set(a) | set(b)):
        if a.get(k) != b.get(k):
            out.append("  %s  before=%s after=%s" % (k, a.get(k), b.get(k)))
    return "\n".join(out) or "  (无差异)"


def launch_and_wait(tag, graceful: bool, wait_trace=40):
    env = os.environ.copy()
    env["ELANG_AI_TRACE"] = TRACE
    env["ELANG_AI_TASK"] = "PLACEHOLDER_TASK_PoC1"
    env["ELANG_AI_HIDE"] = "0"
    p = subprocess.Popen([E_EXE], cwd=E_DIR, env=env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("[%s] PID=%d" % (tag, p.pid), flush=True)
    deadline = time.time() + wait_trace
    got_trace = False
    while time.time() < deadline:
        if os.path.exists(TRACE):
            got_trace = True
            break
        if p.poll() is not None:
            break
        time.sleep(0.5)
    print("[%s] trace_exists=%s" % (tag, got_trace), flush=True)
    # 关闭
    if p.poll() is None:
        if graceful:
            subprocess.run(["taskkill", "/PID", str(p.pid)],
                           capture_output=True, text=True)
            for _ in range(40):
                if p.poll() is not None:
                    break
                time.sleep(0.5)
            print("[%s] graceful close -> exit=%s" % (tag, p.poll()), flush=True)
        if p.poll() is None:
            subprocess.run(["taskkill", "/F", "/PID", str(p.pid)],
                           capture_output=True, text=True)
            try:
                p.wait(timeout=10)
            except Exception:
                pass
    return got_trace


def main():
    if os.path.exists(DST):
        print("目标已存在，拒绝覆盖:", DST)
        return 2
    before = snap()
    for f in (TRACE, SELFCHK):
        if os.path.exists(f):
            os.remove(f)
    res = {}
    try:
        shutil.copy2(FNE_SRC, DST)
        print("已投放:", DST, flush=True)
        res["run1_trace"] = launch_and_wait("run1", graceful=True)
        print("run1 之后 selfcheck 存在:", os.path.exists(SELFCHK), flush=True)
        if os.path.exists(SELFCHK):
            print(open(SELFCHK, encoding="utf-8", errors="replace").read())
        res["run2_trace"] = launch_and_wait("run2", graceful=False)
        print("run2 之后 selfcheck 存在:", os.path.exists(SELFCHK), flush=True)
        if os.path.exists(SELFCHK):
            print(open(SELFCHK, encoding="utf-8", errors="replace").read())
    finally:
        if os.path.exists(DST):
            os.remove(DST)
            print("已移除:", DST)
        after = snap()
        print("lib\\ diff:")
        print(diff(before, after))
    print("\n==== 判定 ====")
    print("run1 触发加载(实时扫描):", res.get("run1_trace"))
    print("run2 触发加载(缓存登记):", res.get("run2_trace"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
