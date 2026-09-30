#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""QAQ-ETool 往返测试：.e → 文本 → .e → 文本，并做文本比对与语义比对。

步骤:
  1. e2t  原.e        → work/t1
  2. t2e  work/t1     → work/rt.e
  3. e2t  work/rt.e   → work/t2
  4. cmpdir t1 vs t2  （文本逐文件比对）
  5. Probe --cmp 原.e rt.e --ignore-order  （语义比对）
任一步失败，后续依赖步骤标记为「跳过」；最终退出码: 0=全部通过 1=有失败/差异 2=参数错误。

用法: python tools/roundtrip.py <原.e> [--work 目录] [--timeout 秒] [--max N] [--show N] [--no-code]
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "src/QAQ.ETool.Cli/bin/Release/net8.0/QAQ-ETool.exe"
PROBE = ROOT / "src/Probe/bin/Release/net8.0/Probe.exe"
CMPDIR = ROOT / "tools/cmpdir.py"

OK, WARN, FAIL, SKIP = "通过", "通过(有警告)", "失败", "跳过"


def run(args, timeout):
    """运行命令，返回 (退出码或 None=超时/无法启动, 输出文本, 耗时秒)。"""
    t = time.time()
    try:
        p = subprocess.run([str(a) for a in args], capture_output=True, timeout=timeout)
        out = (p.stdout + p.stderr).decode("utf-8", "replace")
        return p.returncode, out, time.time() - t
    except subprocess.TimeoutExpired:
        return None, f"超时（>{timeout}s）", time.time() - t
    except OSError as e:
        return None, f"无法启动: {e}", time.time() - t


def tail(text, n=8):
    lines = [l for l in text.splitlines() if l.strip()]
    return "\n".join("      " + l for l in lines[-n:])


def main():
    ap = argparse.ArgumentParser(description="QAQ-ETool 往返测试")
    ap.add_argument("src", help="原始 .e 文件")
    ap.add_argument("--work", default=None, help="工作目录（默认 tests/scratch/rt/<夹具名>）")
    ap.add_argument("--timeout", type=int, default=600, help="每步超时秒数")
    ap.add_argument("--max", type=int, default=30, help="Probe --cmp 明细条数")
    ap.add_argument("--show", type=int, default=6, help="cmpdir 展示的不一致文件数")
    ap.add_argument("--no-code", action="store_true", help="语义比对不比代码")
    ap.add_argument("--nosidecar", action="store_true", help="t2e 不使用旁路文件（等价 e2txt 目录）")
    a = ap.parse_args()

    src = Path(a.src).resolve()
    if not src.is_file():
        print(f"原文件不存在: {src}")
        return 2
    for exe in (CLI, PROBE):
        if not exe.is_file():
            print(f"缺少可执行文件: {exe}")
            return 2
    work = Path(a.work).resolve() if a.work else ROOT / "tests/scratch/rt" / src.stem
    work.mkdir(parents=True, exist_ok=True)
    t1, t2, rt = work / "t1", work / "t2", work / "rt.e"
    # 清掉上次产物，避免把旧文件当成本次结果
    for d in (t1, t2):
        if d.exists():
            shutil.rmtree(d)
    if rt.exists():
        rt.unlink()

    results = []  # (名称, 状态, 耗时, 说明)

    def step(name, status, dt, note="", out=""):
        results.append((name, status, dt, note))
        print(f"[{status}] {name}  {dt:.2f}s  {note}")
        if status == FAIL and out:
            print(tail(out))
        sys.stdout.flush()

    def e2t(name, s, d):
        code, out, dt = run([CLI, "-mode", "e2t", "-src", s, "-dst", d], a.timeout)
        if code == 0:
            step(name, OK, dt)
        elif code == 2:
            step(name, WARN, dt, "存在无法解析的内容")
        else:
            step(name, FAIL, dt, f"exit={code}", out)
            return False
        return True

    print(f"原文件: {src}\n工作目录: {work}\n")

    # 1. e2t
    s1 = e2t("1 e2t 原.e → t1", src, t1)

    # 2. t2e
    s2 = False
    if s1:
        code, out, dt = run([CLI, "-mode", "t2e", "-src", t1, "-dst", rt] + (["-nosidecar"] if a.nosidecar else []), a.timeout)
        if code in (0, 2) and rt.is_file():
            info = " | ".join(l.strip() for l in out.splitlines() if l.startswith("[信息]"))
            step("2 t2e t1 → rt.e", OK if code == 0 else WARN, dt, info)
            s2 = True
        else:
            why = f"exit={code}" + ("" if rt.is_file() else "，未生成 rt.e")
            if code is not None and re.search(r"t2e|mode|未知|不支持|unknown", out, re.I):
                why += "（CLI 可能尚未实现 t2e）"
            step("2 t2e t1 → rt.e", FAIL, dt, why, out)
    else:
        step("2 t2e t1 → rt.e", SKIP, 0, "依赖第 1 步")

    # 3. e2t rt.e
    s3 = e2t("3 e2t rt.e → t2", rt, t2) if s2 else (step("3 e2t rt.e → t2", SKIP, 0, "依赖第 2 步") or False)

    # 4. cmpdir（它总返回 0，需解析输出判定）
    if s1 and s3:
        env = dict(os.environ, PYTHONIOENCODING="utf-8")
        t = time.time()
        p = subprocess.run([sys.executable, str(CMPDIR), str(t1), str(t2), "--show", str(a.show)],
                           capture_output=True, env=env, timeout=a.timeout)
        dt = time.time() - t
        out = p.stdout.decode("utf-8", "replace")
        m = re.search(r"不一致 (\d+)", out)
        ob = re.search(r"只在基线 \((\d+)\)", out)
        oo = re.search(r"只在我们 \((\d+)\)", out)
        nd = int(m.group(1)) if m else -1
        nb = int(ob.group(1)) if ob else 0
        no = int(oo.group(1)) if oo else 0
        good = m is not None and nd == 0 and nb == 0 and no == 0
        step("4 cmpdir t1 vs t2", OK if good else FAIL, dt, f"不一致 {nd} / 只在t1 {nb} / 只在t2 {no}")
        if not good:
            print(out.rstrip())
    else:
        step("4 cmpdir t1 vs t2", SKIP, 0, "依赖第 1、3 步")

    # 5. 语义比对
    if s2:
        args = [PROBE, "--cmp", src, rt, "--ignore-order", "--max", str(a.max),
                "--json", work / "cmp.json"]
        if a.no_code:
            args.append("--no-code")
        code, out, dt = run(args, a.timeout)
        if code == 0:
            step("5 Probe --cmp 原.e vs rt.e", OK, dt, "无语义差异")
        else:
            m = re.search(r"计入差异 (\d+) 条", out)
            note = f"exit={code}" + (f"，计入差异 {m.group(1)} 条（明细见 cmp.json）" if m else "")
            step("5 Probe --cmp 原.e vs rt.e", FAIL, dt, note)
            print(out.rstrip())
    else:
        step("5 Probe --cmp 原.e vs rt.e", SKIP, 0, "依赖第 2 步")

    # 6. 逐字节（信息项，不计入通过判定）
    if s2:
        same = src.read_bytes() == rt.read_bytes()
        print(f"[信息] 6 逐字节 原.e vs rt.e  {'一致' if same else '不一致'}")

    # 汇总
    print("\n=== 汇总 ===")
    for name, st, dt, note in results:
        print(f"  {st:<10} {dt:>7.2f}s  {name}  {note}")
    bad = any(st in (FAIL, SKIP) for _, st, _, _ in results)
    print(f"结果: {'未通过' if bad else '全部通过'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
