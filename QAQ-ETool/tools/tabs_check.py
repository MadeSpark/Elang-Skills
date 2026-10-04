#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""打开标签页（编辑信息段2 0x09007319）的文本化往返验收。

步骤:
  0. Probe --tabs 原.e                        取原标签页基线（无该段则整组跳过并提示）
  1. e2t 原.e → t1（带旁路）                  断言 class/tabs.desc.json（或 代码/标签页.desc.json）生成，
                                              Tabs 数 = 原基线数
  2. e2t 原.e → t1ns（-nosidecar）            同样断言（文本载体独立于旁路存在）
  3. t2e t1ns → ns.e（纯文本组包，无旁路）    Probe --tabs ns.e 与基线逐项一致（kind/名字/顺序/光标）
  4. t2e t1 → sc.e（带旁路组包）              Probe --secbytes 原.e sc.e 全段 same（字节级回归）
  5. 编辑场景：复制 t1 → t1ed，global.e.txt 加一个全局变量（IdsNew>0 → 旧逻辑整段丢弃 0x09），
     t2e t1ed → ed.e → Probe --tabs ed.e 与基线逐项一致（按文本重建恢复）
任一步失败，后续依赖步骤跳过；退出码: 0=全部通过 1=有失败 2=参数错误。

用法: python tools/tabs_check.py <原.e> [--work 目录] [--timeout 秒] [--no-cursor]
"""
import argparse
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "src/QAQ.ETool.Cli/bin/Release/net8.0/QAQ-ETool.exe"
PROBE = ROOT / "src/Probe/bin/Release/net8.0/Probe.exe"

OK, WARN, FAIL, SKIP, INFO = "通过", "通过(有警告)", "失败", "跳过", "信息"


def run(args, timeout):
    t = time.time()
    try:
        p = subprocess.run([str(a) for a in args], capture_output=True, timeout=timeout)
        out = (p.stdout + p.stderr).decode("utf-8", "replace")
        return p.returncode, out, time.time() - t
    except subprocess.TimeoutExpired:
        return None, f"超时（>{timeout}s）", time.time() - t
    except OSError as e:
        return None, f"无法启动: {e}", time.time() - t


def tail(text, n=10):
    lines = [l for l in text.splitlines() if l.strip()]
    return "\n".join("      " + l for l in lines[-n:])


TAB_RE = re.compile(r"^TAB (\d+): (.+)$")


def probe_tabs(e, timeout):
    """Probe --tabs：返回 (标签行列表[去掉序号], 原始输出)。文件没有 0x09 段时返回 ([], 'NONE')。"""
    code, out, _ = run([PROBE, "--tabs", e], timeout)
    if code != 0:
        return None, out
    if "无编辑信息段2" in out:
        return [], "NONE"
    lines = []
    for l in out.splitlines():
        m = TAB_RE.match(l.strip())
        if m:
            lines.append(m.group(2).strip())
    return lines, out


def tab_key(line, with_cursor=True):
    """归一化一行 TAB 描述：with_cursor=False 时只比 kind/目标，不比光标数字。"""
    if not with_cursor:
        # code/form/table 的光标段从 " offset=" 或 " units=" 开始截断
        for sep in (" offset=", " units=["):
            i = line.find(sep)
            if i >= 0:
                return line[:i]
        return line
    return line


def find_tabs_file(text_dir):
    for rel in ("class/tabs.desc.json", "代码/标签页.desc.json"):
        p = text_dir / rel
        if p.is_file():
            return p
    return None


def main():
    ap = argparse.ArgumentParser(description="打开标签页文本化往返验收")
    ap.add_argument("src", help="原始 .e 文件")
    ap.add_argument("--work", default=None, help="工作目录（默认 tests/scratch/tabs/<夹具名>）")
    ap.add_argument("--timeout", type=int, default=600, help="每步超时秒数")
    ap.add_argument("--no-cursor", action="store_true", help="不比对光标/选区（只比标签页本身）")
    a = ap.parse_args()

    src = Path(a.src).resolve()
    if not src.is_file():
        print(f"原文件不存在: {src}")
        return 2
    for exe in (CLI, PROBE):
        if not exe.is_file():
            print(f"缺少可执行文件: {exe}")
            return 2
    work = Path(a.work).resolve() if a.work else ROOT / "tests/scratch/tabs" / src.stem
    work.mkdir(parents=True, exist_ok=True)
    for name in ("t1", "t1ns", "t1ed"):
        d = work / name
        if d.exists():
            shutil.rmtree(d)
    for f in ("ns.e", "sc.e", "ed.e"):
        p = work / f
        if p.exists():
            p.unlink()

    results = []

    def step(name, status, note="", out=""):
        results.append(status)
        mark = {"通过": "✓", "通过(有警告)": "✓", "失败": "✗", "跳过": "-", "信息": "·"}[status]
        print(f"[{status}] {mark} {name}  {note}")
        if status == FAIL and out:
            print(tail(out))
        sys.stdout.flush()

    print(f"原文件: {src}\n工作目录: {work}\n")

    # 0. 原始基线
    base, out0 = probe_tabs(src, a.timeout)
    if base is None:
        step("0 Probe --tabs 原.e", FAIL, "Probe 失败", out0)
        return 1
    if out0 == "NONE":
        print(f"[信息] {src.name} 没有编辑信息段2（打开标签页），本工具只验证不报错")
        return 0
    step(f"0 原基线 {len(base)} 个标签页", INFO, "；".join(base[:3]) + ("…" if len(base) > 3 else ""))

    # 1/2. 拆包（带/不带旁路）→ 标签页文本文件
    t1_files = {}
    for name, dst, extra in (("1 e2t → t1（带旁路）", work / "t1", []),
                             ("2 e2t → t1ns（-nosidecar）", work / "t1ns", ["-nosidecar"])):
        code, out, _ = run([CLI, "-mode", "e2t", "-src", src, "-dst", dst] + extra, a.timeout)
        if code not in (0, 2):
            step(name, FAIL, f"exit={code}", out)
            t1_files[dst.name] = None
            continue
        tf = find_tabs_file(dst)
        if tf is None:
            step(name, FAIL, f"exit={code}，但未生成 标签页.desc.json", out)
            t1_files[dst.name] = None
            continue
        data = json.loads(tf.read_text(encoding="utf-8-sig"))
        tabs = data.get("Tabs", [])
        ok = len(tabs) == len(base)
        step(f"{name} → {tf.relative_to(work)}", OK if ok else FAIL,
             f"Tabs {len(tabs)} / 原基线 {len(base)}" + ("" if ok else "，数量不一致"))
        t1_files[dst.name] = dst if ok else None

    # 3. 纯文本组包（无旁路）→ 标签页按名字重建
    if t1_files.get("t1ns"):
        code, out, _ = run([CLI, "-mode", "t2e", "-src", work / "t1ns", "-dst", work / "ns.e"], a.timeout)
        if code not in (0, 2) or not (work / "ns.e").is_file():
            step("3 t2e t1ns → ns.e（无旁路）", FAIL, f"exit={code}", out)
        else:
            got, outp = probe_tabs(work / "ns.e", a.timeout)
            exp = [tab_key(x, not a.no_cursor) for x in base]
            gotk = [tab_key(x, not a.no_cursor) for x in (got or [])]
            if got is None:
                step("3 纯文本组包还原标签页", FAIL, "Probe --tabs 失败", outp)
            elif gotk == exp:
                step("3 纯文本组包还原标签页（无旁路）", OK, f"{len(gotk)} 个标签页逐项一致")
            else:
                step("3 纯文本组包还原标签页（无旁路）", FAIL, "与基线不一致")
                for i in range(max(len(exp), len(gotk))):
                    e = exp[i] if i < len(exp) else "(缺)"
                    g = gotk[i] if i < len(gotk) else "(缺)"
                    if e != g:
                        print(f"      #{i}\n        基线: {e}\n        实际: {g}")
    else:
        step("3 t2e t1ns → ns.e（无旁路）", SKIP, "依赖第 2 步")

    # 4. 带旁路组包 → 0x09 字节级回归（全段 same）
    if t1_files.get("t1"):
        code, out, _ = run([CLI, "-mode", "t2e", "-src", work / "t1", "-dst", work / "sc.e"], a.timeout)
        if code not in (0, 2) or not (work / "sc.e").is_file():
            step("4 t2e t1 → sc.e（带旁路）", FAIL, f"exit={code}", out)
        else:
            code, out, _ = run([PROBE, "--secbytes", src, work / "sc.e"], a.timeout)
            # 本步只回归「编辑信息段2」的旁路字节保真；其他段的差异（如 0x0C 易模块记录段的
            # FileSize/FileTime 随磁盘上 .ec 变化）是环境因素，降级为警告
            same09 = [l for l in out.splitlines() if l.startswith("same") and "0x09007319" in l]
            diff09 = [l for l in out.splitlines() if l.startswith("DIFF") and "0x09007319" in l]
            other = [l for l in out.splitlines() if l.startswith("DIFF") and "0x09007319" not in l]
            if not same09 or diff09:
                step("4 带旁路组包：编辑信息段2 字节一致", FAIL, f"{len(diff09)} 段 0x09 DIFF / 0 段 same", out)
            elif other:
                step("4 带旁路组包：编辑信息段2 字节一致", WARN,
                     f"0x09 段 same；另有 {len(other)} 段 DIFF（多为 .ec 大小/时间等环境差异）")
            else:
                step("4 带旁路组包：编辑信息段2 字节一致", OK, "全部段 same")
    else:
        step("4 t2e t1 → sc.e（带旁路）", SKIP, "依赖第 1 步")

    # 5. 编辑场景：加一个全局变量（IdsNew>0，旧逻辑会整段丢弃 0x09）→ 标签页应按文本重建
    if t1_files.get("t1"):
        ed = work / "t1ed"
        shutil.copytree(work / "t1", ed)
        gfile = None
        for rel in ("global.e.txt", "代码/全局变量.e.txt"):
            p = ed / rel
            if p.is_file():
                gfile = p
                break
        if gfile is None:
            step("5 编辑场景（新增实体后组包）", SKIP, "找不到 global.e.txt")
        else:
            text = gfile.read_text(encoding="utf-8-sig")
            text += ".全局变量 标签页验收_新增变量, 整数型\r\n"
            gfile.write_text(text, encoding="utf-8-sig", newline="")
            code, out, _ = run([CLI, "-mode", "t2e", "-src", ed, "-dst", work / "ed.e"], a.timeout)
            if code not in (0, 2) or not (work / "ed.e").is_file():
                step("5 编辑场景（新增实体后组包）", FAIL, f"exit={code}", out)
            else:
                got, outp = probe_tabs(work / "ed.e", a.timeout)
                exp = [tab_key(x, not a.no_cursor) for x in base]
                gotk = [tab_key(x, not a.no_cursor) for x in (got or [])]
                if got is None:
                    step("5 编辑场景还原标签页", FAIL, "Probe --tabs 失败", outp)
                elif gotk == exp:
                    step("5 编辑场景还原标签页（新增实体后）", OK,
                         f"{len(gotk)} 个标签页逐项一致（旧逻辑此处整段丢失）")
                else:
                    step("5 编辑场景还原标签页（新增实体后）", FAIL, "与基线不一致")
                    for i in range(max(len(exp), len(gotk))):
                        e = exp[i] if i < len(exp) else "(缺)"
                        g = gotk[i] if i < len(gotk) else "(缺)"
                        if e != g:
                            print(f"      #{i}\n        基线: {e}\n        实际: {g}")
    else:
        step("5 编辑场景（新增实体后组包）", SKIP, "依赖第 1 步")

    print("\n=== 汇总 ===")
    bad = [s for s in results if s == FAIL]
    skip = results.count(SKIP)
    print(f"结果: {'未通过' if bad else '全部通过'}（失败 {len(bad)}，跳过 {skip}）")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
