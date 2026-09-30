#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""e2txt 基线采集器 —— QAQ-ETool 的对照答案与验收基线。

做三件事：
  1. e2t：对夹具跑 e2txt 转文本（英文布局 + 中文布局），记录产物清单/md5/
     错误分类/占位符统计。这是判据① (零占位符) 的基线数字。
  2. t2e：把文本目录回写成 .e，记录产物大小/md5/错误数。这是判据③ (回编译等价)
     的基线数字。
  3. 输出 tests/baseline/report.json，供 QAQ-ETool 逐文件比对。

e2txt 调用约定（出处：技能 e2txt-cli/SKILL.md，均为实测结论）：
  - 退出码恒为 0 —— **不能**看 returncode，只能看 stdout 里有没有 "SUCC:"
  - SUCC: / [信息] 走 stdout；ERROR: / [错误] 走 stderr。两个流都必须读
  - 输出是 GBK
  - 必须显式 -level 2，默认的 1 会静默丢掉空的 const/dll/struct/icon
  - 不加 -log：t2e 时 log/ 会落在 src 文本目录里，污染基线
  - -src / -dst 必须绝对路径，相对路径报 "ERROR:来源文件不存在"

重要保护：t2e 会把 <文本目录>\项目.etprj 重写成只剩 4 个 TXT2E-* 键，
所以 t2e 一律在**文本目录的副本**上跑，绝不碰 tests/baseline 里的基线产物。
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent          # QAQ-ETool/
FIXTURES = ROOT / "测试源码"
BASELINE = ROOT / "tests" / "baseline"
SCRATCH = ROOT / "tests" / "scratch"

E2TXT = os.environ.get("E2TXT") or r"D:\Tools\e2txt\e2txt.exe"

# stderr 里出现过的已知错误来源，逐个计数用
KNOWN_ERR_TAGS = [
    "ParseParams",
    "ParseMethodCode",
    "ParseExpression",
    "ParseCodeRange",
    "TxtCodeWriter",
    "TxtSourceLoader",
    "解析【类型】名称",
    "加载支持库失败",
    "未定义的常量",
    "支持库不存在",
]

PLACEHOLDER_RE = re.compile(r"<\?.{0,80}?\?>")
ERRLINE_RE = re.compile(r"^.*\[错误\].*$", re.MULTILINE)


def md5_of(path: Path) -> str:
    h = hashlib.md5()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def run_e2txt(args, timeout=600):
    """跑一次 e2txt，返回结构化结果。绝不看 returncode。"""
    argv = [E2TXT] + args
    t0 = time.time()
    try:
        p = subprocess.run(argv, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return {
            "ok": False, "timeout": True, "elapsed": timeout,
            "argv": argv, "stdout": "", "stderr": "<TIMEOUT>",
        }
    elapsed = round(time.time() - t0, 3)
    so = p.stdout.decode("gbk", errors="replace")
    se = p.stderr.decode("gbk", errors="replace")
    return {
        "ok": "SUCC:" in so,
        "timeout": False,
        "elapsed": elapsed,
        "argv": argv,
        "returncode": p.returncode,
        "stdout": so,
        "stderr": se,
    }


def classify_errors(stderr: str) -> dict:
    lines = ERRLINE_RE.findall(stderr)
    tags = {t: stderr.count(t) for t in KNOWN_ERR_TAGS if stderr.count(t)}
    return {"error_line_count": len(lines), "tag_counts": tags}


def snapshot_tree(d: Path) -> list:
    """产物清单：相对路径 + 大小 + md5，排序后便于逐文件 diff。"""
    out = []
    for p in sorted(d.rglob("*"), key=lambda x: str(x).lower()):
        if p.is_file():
            out.append({
                "rel": str(p.relative_to(d)).replace("\\", "/"),
                "size": p.stat().st_size,
                "md5": md5_of(p),
            })
    return out


def scan_placeholders(d: Path) -> dict:
    """扫描文本产物里的 <?...?> 占位符。判据①要求这个数为 0。"""
    hits = {}
    total = 0
    for p in sorted(d.rglob("*")):
        if not p.is_file() or p.suffix.lower() not in (".txt", ".json", ".etprj"):
            continue
        try:
            text = p.read_text(encoding="utf-8-sig")
        except (UnicodeDecodeError, OSError):
            try:
                text = p.read_text(encoding="gbk", errors="replace")
            except OSError:
                continue
        found = PLACEHOLDER_RE.findall(text)
        if found:
            rel = str(p.relative_to(d)).replace("\\", "/")
            per_file = {}
            for f in found:
                per_file[f] = per_file.get(f, 0) + 1
            hits[rel] = per_file
            total += len(found)
    kinds = {}
    for per_file in hits.values():
        for k, v in per_file.items():
            kinds[k] = kinds.get(k, 0) + v
    return {"total": total, "kinds": kinds, "by_file": hits}


def write_logs(log_dir: Path, tag: str, res: dict):
    log_dir.mkdir(parents=True, exist_ok=True)
    (log_dir / f"{tag}.stdout.txt").write_text(res["stdout"], encoding="utf-8")
    (log_dir / f"{tag}.stderr.txt").write_text(res["stderr"], encoding="utf-8")


def do_e2t(src: Path, dst: Path, ns: str, log_dir: Path, tag: str) -> dict:
    if dst.exists():
        shutil.rmtree(dst)
    dst.mkdir(parents=True, exist_ok=True)
    args = ["-mode", "e2t", "-src", str(src), "-dst", str(dst),
            "-level", "2", "-enc", "UTF-8"]
    if ns:
        args += ["-ns", ns]
    res = run_e2txt(args)
    write_logs(log_dir, tag, res)
    rec = {
        "ok": res["ok"],
        "elapsed": res["elapsed"],
        "argv": res["argv"][1:],
        "stdout_lines": res["stdout"].count("\n"),
        "errors": classify_errors(res["stderr"]),
        "dst": str(dst),
    }
    if res["ok"]:
        files = snapshot_tree(dst)
        rec["file_count"] = len(files)
        rec["total_bytes"] = sum(f["size"] for f in files)
        rec["files"] = files
        rec["placeholders"] = scan_placeholders(dst)
    else:
        rec["fatal_stderr"] = res["stderr"].strip()[:2000]
    return rec


def do_t2e(txt_dir: Path, out_e: Path, log_dir: Path, tag: str) -> dict:
    """在 txt_dir 的副本上跑 t2e，保护基线的 项目.etprj 不被重写。"""
    work = SCRATCH / f"t2e-{tag}"
    if work.exists():
        shutil.rmtree(work)
    work.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(txt_dir, work)

    out_e.parent.mkdir(parents=True, exist_ok=True)
    if out_e.exists():
        out_e.unlink()
    res = run_e2txt(["-mode", "t2e", "-src", str(work), "-dst", str(out_e)])
    write_logs(log_dir, tag, res)
    rec = {
        "ok": res["ok"],
        "elapsed": res["elapsed"],
        "argv": res["argv"][1:],
        "stdout_lines": res["stdout"].count("\n"),
        "errors": classify_errors(res["stderr"]),
        "out": str(out_e),
    }
    if out_e.exists():
        rec["out_size"] = out_e.stat().st_size
        rec["out_md5"] = md5_of(out_e)
    else:
        rec["out_size"] = 0
        rec["out_md5"] = None
        rec["fatal_stderr"] = res["stderr"].strip()[:2000]
    return rec


def process(fixture: Path, do_back: bool) -> dict:
    stem = fixture.stem
    base = BASELINE / stem
    logs = base / "logs"
    rec = {
        "src": {
            "path": str(fixture),
            "size": fixture.stat().st_size,
            "md5": md5_of(fixture),
        },
        "e2t": {},
    }

    print(f"\n{'='*66}\n夹具: {fixture.name}  ({fixture.stat().st_size:,} B)\n{'='*66}")

    for layout, ns in (("en", None), ("cn", "2")):
        print(f"  [e2t {layout}] ...", end="", flush=True)
        r = do_e2t(fixture, base / layout, ns, logs, f"e2t.{layout}")
        rec["e2t"][layout] = r
        if r["ok"]:
            ph = r["placeholders"]["total"]
            print(f" SUCC  {r['elapsed']}s  {r['file_count']} 文件"
                  f"  {r['total_bytes']:,} B"
                  f"  [错误]{r['errors']['error_line_count']}"
                  f"  占位符{ph}")
        else:
            print(f" FAIL  {r['elapsed']}s  -> {r.get('fatal_stderr','')[:120]}")

    if do_back:
        rec["t2e"] = {}
        for layout in ("en", "cn"):
            if not rec["e2t"].get(layout, {}).get("ok"):
                continue
            print(f"  [t2e {layout}] ...", end="", flush=True)
            r = do_t2e(base / layout, base / "back" / f"from-{layout}.e",
                       logs, f"t2e.{layout}")
            rec["t2e"][layout] = r
            delta = ""
            if r["out_size"]:
                d = r["out_size"] - rec["src"]["size"]
                pct = 100.0 * r["out_size"] / rec["src"]["size"]
                delta = f"  ({d:+,} B, 原文的 {pct:.1f}%)"
            print(f" {'SUCC' if r['ok'] else 'FAIL'}  {r['elapsed']}s"
                  f"  {r['out_size']:,} B{delta}"
                  f"  [错误]{r['errors']['error_line_count']}")

    return rec


def main():
    ap = argparse.ArgumentParser(description="e2txt 基线采集器")
    ap.add_argument("--only", action="append", default=None,
                    help="只跑名字含该子串的夹具，可多次给")
    ap.add_argument("--no-back", action="store_true",
                    help="跳过 t2e 回写（只采 e2t 基线）")
    args = ap.parse_args()

    if not Path(E2TXT).exists():
        sys.exit(f"找不到 e2txt: {E2TXT}（可用环境变量 E2TXT 指定）")
    if not FIXTURES.is_dir():
        sys.exit(f"找不到夹具目录: {FIXTURES}")

    fixtures = sorted(FIXTURES.glob("*.e"))
    if args.only:
        fixtures = [f for f in fixtures
                    if any(s in f.name for s in args.only)]
    if not fixtures:
        sys.exit("没有匹配的夹具")

    BASELINE.mkdir(parents=True, exist_ok=True)
    report = {
        "generated": time.strftime("%Y-%m-%d %H:%M:%S"),
        "e2txt": {"path": E2TXT, "size": Path(E2TXT).stat().st_size},
        "fixtures": {},
    }
    for f in fixtures:
        report["fixtures"][f.stem] = process(f, not args.no_back)

    out = BASELINE / "report.json"
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2),
                   encoding="utf-8")
    print(f"\n报告: {out}")

    # 摘要
    print(f"\n{'='*66}\n摘要（判据①基线 = 占位符总数，目标 0）\n{'='*66}")
    print(f"{'夹具':<24}{'布局':<6}{'e2t':<6}{'文件':>6}{'[错误]':>8}{'占位符':>8}")
    for stem, rec in report["fixtures"].items():
        for layout, r in rec["e2t"].items():
            if r["ok"]:
                print(f"{stem:<24}{layout:<6}{'SUCC':<6}{r['file_count']:>6}"
                      f"{r['errors']['error_line_count']:>8}"
                      f"{r['placeholders']['total']:>8}")
            else:
                print(f"{stem:<24}{layout:<6}{'FAIL':<6}{'-':>6}"
                      f"{r['errors']['error_line_count']:>8}{'-':>8}")


if __name__ == "__main__":
    main()
