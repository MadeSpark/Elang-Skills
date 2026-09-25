# -*- coding: utf-8 -*-
"""UB 归因矩阵（Python 版）

为什么要 Python 版：Windows 原生 exe 不响应 `timeout` 发的 SIGTERM，
一旦某个 flag 让 mk_map 跑飞，bash 版会永久挂起。
subprocess 的 timeout 在 Windows 上走 TerminateProcess（硬杀），可靠。

用法： python ubmatrix.py
"""
import os
import sys
import time
import subprocess

HERE  = os.path.dirname(os.path.abspath(__file__))
os.chdir(HERE)

MINGW = r"C:\msys64\mingw32\bin"
GCC   = os.path.join(MINGW, "gcc.exe")
OBJC  = os.path.join(MINGW, "objcopy.exe")
OBJD  = os.path.join(MINGW, "objdump.exe")

ENV = dict(os.environ)
ENV["PATH"] = MINGW + os.pathsep + ENV.get("PATH", "")

WORK = "_ubk"
os.makedirs(WORK, exist_ok=True)
PROBE_OBJ = os.path.join(WORK, "probe.o")
PROBE_EXE = os.path.join(WORK, "case.exe")


def run(cmd, timeout=15):
    try:
        p = subprocess.run(cmd, env=ENV, capture_output=True, timeout=timeout, cwd=HERE)
        return (p.returncode,
                (p.stdout or b"").decode("utf-8", "replace"),
                (p.stderr or b"").decode("utf-8", "replace"))
    except subprocess.TimeoutExpired:
        return 124, "", "(hard timeout)"


CASES = [
    ("-O0  基线",                                    "-O0"),
    ("-O1  （已知崩）",                               "-O1"),
    ("-O1 -fno-strict-aliasing",                     "-O1 -fno-strict-aliasing"),
    ("-O1 -fno-strict-overflow",                     "-O1 -fno-strict-overflow"),
    ("-O1 -fwrapv",                                  "-O1 -fwrapv"),
    ("-O1 -fno-aggressive-loop-optimizations",       "-O1 -fno-aggressive-loop-optimizations"),
    ("-O1 -fno-tree-loop-distribute-patterns",       "-O1 -fno-tree-loop-distribute-patterns"),
    ("-O1 -fno-strict-aliasing -fno-tree-loop-dist", "-O1 -fno-strict-aliasing -fno-tree-loop-distribute-patterns"),
    ("-O1 -fno-tree-vrp -fno-tree-ccp",              "-O1 -fno-tree-vrp -fno-tree-ccp"),
    ("-O1 -fno-inline",                              "-O1 -fno-inline"),
    ("-O1 -fno-inline-small-functions",              "-O1 -fno-inline-small-functions"),
    ("-O1 -fno-tree-sra",                            "-O1 -fno-tree-sra"),
    ("-O1 -fno-ipa-sra",                             "-O1 -fno-ipa-sra"),
    ("-O1 -fno-tree-dominator-opts",                 "-O1 -fno-tree-dominator-opts"),
    ("-O1 -fno-omit-frame-pointer",                  "-O1 -fno-omit-frame-pointer"),
    ("-O1 -fno-tree-loop-optimize",                  "-O1 -fno-tree-loop-optimize"),
    ("-O1 -fno-dce -fno-dse",                        "-O1 -fno-dce -fno-dse"),
    ("-O1 -fno-forward-propagate",                   "-O1 -fno-forward-propagate"),
    ("-O2  裸",                                       "-O2"),
    ("-O2 -fno-tree-loop-distribute-patterns 已知OK", "-O2 -fno-tree-loop-distribute-patterns"),
    ("-O2 -fno-tree-loop-distribute-patterns -fno-strict-aliasing", "-O2 -fno-tree-loop-distribute-patterns -fno-strict-aliasing"),
    ("-Os  裸",                                       "-Os"),
    ("-O3  （已知崩）",                               "-O3"),
]


def main():
    # 1) 探针固定 -O0 编译一次
    rc, o, e = run([GCC, "-m32", "-O0", "-c", "probe_prol.c", "-o", PROBE_OBJ])
    if rc != 0:
        print("探针编译失败：\n%s\n%s" % (o, e))
        return 1

    print("=" * 96)
    print("%-50s %-9s %-8s %-6s %-7s %s" % ("编译参数", "结果", "blob大小", "重定位", "耗时", "备注"))
    print("-" * 96)

    rows = []
    for label, flags in CASES:
        obj = os.path.join(WORK, "ub.o")
        binp = os.path.join(WORK, "ub.bin")
        t0 = time.time()
        rc, o, e = run([GCC, "-m32"] + flags.split() + ["-c", "mapkit.c", "-o", obj])
        if rc != 0:
            print("%-50s %-9s %s" % (label, "编译失败", e.strip()[:40])); rows.append(None); continue
        rc2, _, _ = run([OBJC, "-O", "binary", "-j", ".text", obj, binp])
        sz = os.path.getsize(binp) if os.path.exists(binp) else 0
        rc3, dis, _ = run([OBJD, "-r", obj])
        reloc = dis.count("R_386")
        rc4, _, _ = run([GCC, "-m32", "-o", PROBE_EXE, PROBE_OBJ, obj, "-lkernel32"])
        if rc4 != 0:
            print("%-50s %-9s %-8d %-6d %s" % (label, "链接失败", sz, reloc, "")); rows.append(None); continue
        rc5, out, _ = run([PROBE_EXE, "o"], timeout=8)
        dt = time.time() - t0
        resline = ""
        for ln in out.splitlines():
            if ln.startswith("[8] RESULT"):
                resline = ln.strip(); break
        if rc5 == 124:
            res, note = "超时", "rc=124"
        elif "code = 0xC" in out:
            res = "崩溃"
            note = ""
            for ln in out.splitlines():
                if "code = 0x" in ln:
                    note = ln.strip()[:34]; break
        elif resline and " PASS" in resline:
            res, note = "通过", resline
        else:
            res, note = "未完成", (resline or "rc=%d" % rc5)
        rows.append((label, res, sz, reloc, dt, note))
        print("%-50s %-9s %-8d %-6d %-7.1f %s" % (label, res, sz, reloc, dt, note))

    print("=" * 96)
    ok = [r for r in rows if r and r[1] == "通过"]
    print("通过 %d / %d" % (len(ok), len(CASES)))
    if ok:
        print("可用的编译参数（按 blob 从小到大）：")
        for r in sorted(ok, key=lambda x: x[2]):
            print("   %-50s blob=%-6d reloc=%d" % (r[0], r[2], r[3]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
