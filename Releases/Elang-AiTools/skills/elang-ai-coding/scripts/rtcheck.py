#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
rtcheck —— 易语言文本格式自检（双次往返收敛检验）

为什么是「双次往返」而不是「一次往返」：
    e2txt 会把文本规范化（空行按规则重写、丢掉 .支持库 行、补 4 空格占位行…），
    所以「原始输入 == 回读结果」永远不成立，不能作为判据。
    正确判据是**收敛性**：文本 → t2e → e2t 得到规范形；规范形再走一遍应当**逐字节不变**。
    收敛即说明文本格式正确。

用法：
    python rtcheck.py <文本目录> [--tmp <工作目录>] [--json]

退出码：0 = 收敛且无其它异常；1 = 存在问题。
"""

import argparse
import difflib
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

def find_e2txt():
    """定位 e2txt：环境变量 E2TXT → PATH。**不假定任何安装路径**（技能要给任何机器用）。"""
    env = os.environ.get("E2TXT")
    if env and os.path.exists(env):
        return env
    for name in ("e2txt", "e2txt.exe", "e2txt.cmd", "e2txt.bat"):
        p = shutil.which(name)
        if p:
            return p
    return env or "e2txt"        # 都没找到就交回系统，让报错信息保持可读


E2TXT = find_e2txt()
E2TXT_DIR = os.path.dirname(os.path.abspath(E2TXT))


def _drop_stray(work):
    """e2txt 的 t2e 会在**自身目录**里建一个与「-dst 父目录名」同名的空目录。

    这是 e2txt 的固定行为（实测：-dst 落在 x\\y\\p1.e 就会多出 <e2txt目录>\\y\\）。
    这里只删**空目录**，且只在它确实是本次 work 目录的同名副本时动手。
    """
    stray = os.path.join(E2TXT_DIR, os.path.basename(os.path.normpath(work)))
    try:
        if os.path.isdir(stray) and not os.listdir(stray):
            os.rmdir(stray)
    except OSError:
        pass

DECL_RE = re.compile(r"^\s*\.(局部变量|程序集变量|参数)\s+(.+?)\s*$")
# `.常量 名, "值"` 的值字段本身就是半角引号（值若是文本，再在内部用全角 “ ”），
# 属于规范写法，不参与引号 lint。
CONST_RE = re.compile(r"^\s*\.常量\s")


# ---------------- 基础 ----------------

def run_e2txt(argv, timeout=900):
    """e2txt 退出码恒为 0，成败信息分在 stdout(SUCC:) 与 stderr(ERROR:/[错误])"""
    p = subprocess.run([E2TXT] + argv, capture_output=True, timeout=timeout)
    so = p.stdout.decode("gbk", errors="replace")
    se = p.stderr.decode("gbk", errors="replace")
    return "SUCC:" in so, so, se


def read_tree(root):
    out = {}
    for f in sorted(glob.glob(os.path.join(root, "**", "*.e.txt"), recursive=True)):
        rel = os.path.relpath(f, root)
        raw = open(f, "rb").read()
        out[rel] = {
            "raw": raw,
            "bom": raw[:3] == b"\xef\xbb\xbf",
            "crlf": b"\r\n" in raw,
            "tab": b"\t" in raw,
            "lines": raw.decode("utf-8-sig", errors="replace").split("\r\n"),
        }
    return out


def decl_map(tree):
    d = {}
    for rel, info in tree.items():
        cur, k = None, 0
        for line in info["lines"]:
            m = re.match(r"\s*\.子程序\s+(\S+)", line)
            if m:
                cur, k = m.group(1), 0
                continue
            m = DECL_RE.match(line)
            if m:
                k += 1
                d[(rel, cur, k)] = (m.group(1), m.group(2))
    return d


def arr_mod(decl):
    """声明文本的第 3 个字段（修饰位）里是否带 `数组`"""
    parts = decl.split(",")
    return len(parts) > 2 and "数组" in parts[2]


def canon_decl(text):
    fields = [f.strip() for f in text.replace("数组", "").split(",")]
    return tuple(f for f in fields if f)


def canon_line(line):
    if line.strip().startswith(".支持库"):
        return None
    return line.rstrip()


def norm_lines(tree, rel):
    return [x for x in (canon_line(l) for l in tree[rel]["lines"]) if x is not None]


def detect_ns(src):
    """检测文本目录用的是哪种命名风格。

    NameStyle=2 是「中文」风格（`代码/` `配置/` `窗口/` `资源/` `常量.e.txt` `排序.list.txt`），
    e2txt GUI 默认就是这套；不传 `-ns` 时 e2t 会输出英文风格（`class/` `config/` …），
    两者文件名对不上，会把全部文件误报成「丢失」。所以这里按实际目录名自动选择。
    """
    for d in ('代码', '配置', '窗口', '资源'):
        if os.path.isdir(os.path.join(src, d)):
            return '2'
    return None


def roundtrip(src, work, tag, ns=None):
    """src 文本目录 → t2e → e2t → 新文本目录；返回 (成功, 新目录, t2e错误数, 错误样本)"""
    e = os.path.join(work, f"{tag}.e")
    out = os.path.join(work, f"{tag}_txt")
    ns_args = ["-ns", ns] if ns else []
    ok, so, se = run_e2txt(["-mode", "t2e", "-src", src, "-dst", e,
                            "-enc", "UTF-8", "-level", "2"] + ns_args)
    errs = [l for l in se.splitlines() if "[错误]" in l]
    if not ok or not os.path.exists(e):
        return False, None, len(errs), errs[:5]
    ok2, so2, se2 = run_e2txt(["-mode", "e2t", "-src", e, "-dst", out,
                               "-enc", "UTF-8", "-level", "2"] + ns_args)
    if not ok2:
        return False, None, len(errs), errs[:5]
    return True, out, len(errs), errs[:5]


def diff_lines(a, b, rel, limit=20):
    out = []
    sm = difflib.SequenceMatcher(None, a, b)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            continue
        for x in a[i1:i2]:
            out.append(f"{rel}: 规范形1 - {x!r}")
        for y in b[j1:j2]:
            out.append(f"{rel}: 规范形2 + {y!r}")
    return out[:limit]


# ---------------- 主流程 ----------------

def lint_source(T):
    """对原文做风格 lint：Tab 缩进、半角引号、半角运算符（易语言要求全角）

    注意：`.局部变量 xxx, 文本型, 数组, "8"` 这类声明里的初值/尺寸字段，
    引号是**半角**且属规范写法（实测往返原样保留），因此声明行不检查引号/运算符。
    """
    issues = []
    for rel, info in T.items():
        for i, line in enumerate(info["lines"]):
            s = line.strip()
            if not s or s.startswith("'"):
                continue
            where = f"{rel}:{i+1}"
            if "\t" in line:
                issues.append(f"{where} 缩进用了 Tab，应为 4 空格")
            if CONST_RE.match(line):
                continue            # `.常量 名, "值"`：值字段用半角引号是规范写法
            if DECL_RE.match(line):
                # 局部变量/程序集变量表达数组要用「尺寸位」，不能写 `数组`（那是参数的写法）。
                # 只看第 3 个字段（修饰位），避免把备注里的「数组」二字误判。
                m = DECL_RE.match(line)
                parts = m.group(2).split(",")
                if m.group(1) in ("局部变量", "程序集变量") and len(parts) > 2 \
                        and "数组" in parts[2]:
                    issues.append(
                        f"{where} 局部变量/程序集变量请不要写 `数组`，"
                        f"改用尺寸位：`.{m.group(1)} 名, 类型, , \"0\"`：{s!r}")
                continue            # 声明行：初值/尺寸用半角引号是规范写法
            if '"' in line:
                issues.append(f"{where} 字符串引号应为全角 “ ”：{s!r}")
            for op, full in [(" = ", "＝"), (" + ", "＋"), (" - ", "－"),
                             (" * ", "×"), (" / ", "÷")]:
                if op in line:
                    issues.append(f"{where} 运算符应写作全角 {full}：{s!r}")
    return issues


def main():
    ap = argparse.ArgumentParser(description="易语言文本格式双次往返收敛检验")
    ap.add_argument("src", help="待检查的文本目录")
    ap.add_argument("--tmp", help="临时工作目录（默认系统临时目录）")
    ap.add_argument("--keep", action="store_true", help="保留临时产物")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    src = os.path.abspath(args.src)
    if not os.path.isdir(src):
        raise SystemExit(f"目录不存在: {src}")

    work = args.tmp or tempfile.mkdtemp(prefix="rtcheck_")
    os.makedirs(work, exist_ok=True)
    if not args.tmp and not args.keep:
        # 自动创建的临时目录用完即删，别在工具目录里留垃圾
        import atexit

        def _cleanup():
            shutil.rmtree(work, ignore_errors=True)
            _drop_stray(work)

        atexit.register(_cleanup)

    rep = {"src": src, "work": work, "ns": detect_ns(src)}

    # --- 第一遍：原文 → 规范形1 ---
    ns = rep["ns"]
    ok, t1, nerr, esample = roundtrip(src, work, "p1", ns)
    rep["pass1"] = {"succ": ok, "t2e_errors": nerr, "error_sample": esample}
    if not ok:
        rep["fatal"] = "第一遍往返失败"
        return _emit(rep, args)

    # --- 第二遍：规范形1 → 规范形2 ---
    ok2, t2, nerr2, esample2 = roundtrip(t1, work, "p2", ns)
    rep["pass2"] = {"succ": ok2, "t2e_errors": nerr2, "error_sample": esample2}
    if not ok2:
        rep["fatal"] = "第二遍往返失败"
        return _emit(rep, args)

    T0, T1, T2 = read_tree(src), read_tree(t1), read_tree(t2)
    rep["files"] = {"src": len(T0), "form1": len(T1), "form2": len(T2),
                    "lost_in_pass1": sorted(set(T0) - set(T1))[:10]}

    # --- 收敛性：规范形1 vs 规范形2（这是核心判据）---
    converge = True
    conv_diffs = []
    for rel in T1:
        if rel not in T2:
            converge = False
            conv_diffs.append(f"{rel}: 第二遍后文件缺失")
            continue
        a, b = norm_lines(T1, rel), norm_lines(T2, rel)
        if a != b:
            converge = False
            conv_diffs.extend(diff_lines(a, b, rel))
    rep["convergence"] = {"ok": converge, "diffs": conv_diffs[:20]}

    # --- 数组污染统计（在规范形1上，与源文本对照）---
    A, B = decl_map(T0), decl_map(T1)
    polluted, other = [], []
    for k, (kind, txt) in A.items():
        if k not in B:
            other.append((str(k), txt, None))
            continue
        tb = B[k][1]
        if not arr_mod(txt) and arr_mod(tb):
            # 只看第 3 个字段（修饰位），避免变量名/备注里带「数组」二字造成误判
            polluted.append(f"{kind} {txt}  →  {tb}")
        elif canon_decl(txt) != canon_decl(tb):
            other.append((str(k), txt, tb))
    rep["array_defect"] = {"polluted": len(polluted), "sample": polluted[:8],
                           "other_decl_diff": len(other), "other_sample": other[:8]}

    # --- 风格检查（在规范形1上）---
    indent_bad, tabs, no_bom, no_crlf = [], [], [], []
    for rel, info in T1.items():
        if info["tab"]:
            tabs.append(rel)
        if not info["bom"]:
            no_bom.append(rel)
        if not info["crlf"]:
            no_crlf.append(rel)
        for i, line in enumerate(info["lines"]):
            lead = len(line) - len(line.lstrip(" "))
            if line.strip() and lead % 4:
                indent_bad.append(f"{rel}:{i+1} 缩进 {lead} 不是 4 的倍数")
    rep["style"] = {"tab_files": tabs[:5], "bom_missing": no_bom[:5],
                    "crlf_missing": no_crlf[:5], "indent_bad": indent_bad[:10]}

    # --- 原文 vs 规范形1（信息性：e2txt 改写了哪些写法；用多重集合差集，避免对不齐的噪声）---
    from collections import Counter
    norm_src = []
    for rel in T0:
        if rel not in T1:
            continue
        a = Counter(x for x in (canon_line(l) for l in T0[rel]["lines"]) if x is not None)
        b = Counter(norm_lines(T1, rel))
        for line, n in (a - b).items():
            norm_src.append(f"{rel}: 原文有、规范形无  - {line!r}")
        for line, n in (b - a).items():
            norm_src.append(f"{rel}: 规范形有、原文无  + {line!r}")
    rep["source_normalized"] = {"count": len(norm_src), "sample": norm_src[:14]}

    # --- 原文风格 lint ---
    lint = lint_source(T0)
    rep["source_lint"] = {"count": len(lint), "items": lint[:15]}

    rep["passed"] = (converge and not indent_bad and not tabs and not no_bom
                     and not no_crlf and not rep["files"]["lost_in_pass1"])
    return _emit(rep, args)


def _emit(rep, args):
    if args.json:
        print(json.dumps(rep, ensure_ascii=False, indent=2))
    else:
        print(f"来源目录: {rep['src']}")
        if rep.get("fatal"):
            print(f"✗ {rep['fatal']}")
            return 1
        p1, p2 = rep["pass1"], rep["pass2"]
        print(f"第一遍往返: SUCC={p1['succ']}  t2e[错误]={p1['t2e_errors']}")
        for e in p1["error_sample"]:
            print(f"      {e}")
        print(f"第二遍往返: SUCC={p2['succ']}  t2e[错误]={p2['t2e_errors']}")
        f = rep["files"]
        print(f"文件数: 原文 {f['src']} → 规范形1 {f['form1']} → 规范形2 {f['form2']}"
              + (f"  丢失 {f['lost_in_pass1']}" if f["lost_in_pass1"] else ""))

        c = rep["convergence"]
        print(f"\n★ 收敛检验（核心判据）: {'✓ 两遍规范形完全一致' if c['ok'] else '✗ 未收敛'}")
        for d in c["diffs"]:
            print(f"      {d}")

        a = rep["array_defect"]
        print(f"\n数组位（e2t 回读时的解读口径，不是错误；交付时原样保留）: 受影响声明 {a['polluted']} 条")
        for s in a["sample"]:
            print(f"      {s}")
        if a["other_decl_diff"]:
            print(f"其它声明差异 {a['other_decl_diff']} 条（需要你检查）:")
            for k, x, y in a["other_sample"]:
                print(f"      {k}: {x}  →  {y}")

        s = rep["style"]
        print(f"\n风格: Tab {len(s['tab_files'])} | 缺BOM {len(s['bom_missing'])} | "
              f"缺CRLF {len(s['crlf_missing'])} | 缩进异常 {len(s['indent_bad'])}")
        for x in s["indent_bad"]:
            print(f"      {x}")

        n = rep["source_normalized"]
        if n["count"]:
            print(f"\ne2txt 规范化了你的原文 {n['count']} 处（信息性，不是错误）:")
            for x in n["sample"]:
                print(f"      {x}")

        lt = rep["source_lint"]
        if lt["count"]:
            print(f"\n⚠️ 原文风格问题 {lt['count']} 处（建议按规范改写）:")
            for x in lt["items"]:
                print(f"      {x}")

        ok_all = rep["passed"] and lt["count"] == 0
        print("\n结论: " + ("✓ 格式正确（文本已收敛，往返稳定，无风格问题）" if ok_all
                          else "✓ 往返已收敛" if rep["passed"]
                          else "✗ 存在问题，见上"))

        # 数组位提示：命令行版 e2txt.exe 会给局部变量/程序集变量无条件加数组位（见 规范 §7）
        arr = a.get("polluted", 0)
        if arr:
            print(f"\n注意: 上面有 {arr} 条变量回读成了「数组」。这只是 e2txt 自己的解读口径，**不要去清** ——")
            print("      实测把这一位清掉之后，易语言 IDE 会直接打不开该 .e；原样交付则能打开、能编译运行。")
            print("      唯一的判据是：用户能不能在 IDE 里打开它并编译运行。")
    return 0 if (rep.get("passed") and rep.get("source_lint", {}).get("count", 0) == 0) else 1


if __name__ == "__main__":
    sys.exit(main())
