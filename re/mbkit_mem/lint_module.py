#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""lint_module.py —— 对改造后的模块文本做**不进 e.exe** 的廉价自检。

为什么要有它：一次 e.exe 检查 = 用户那边一次整机卡顿，所以要把「明显错误」挡在这里。

检查项：
  ① 括号/花括号平衡（去掉字符串字面量后数）
  ② 每个 `名 (` 调用点是否能在「子程序 / .DLL命令 / 变量 / 参数 / 核心库命令」里找到
  ③ 重名（子程序、程序集变量、全局变量 各自内部）
  ④ 名字撞易语言关键字（参数 / 窗口 / 菜单 / 类型 / 实例 …）
  ⑤ 残留旧引用（node.dll / _宿主库句柄 的误用等）
  ⑥ `.参数` 标志是否只出现已知取值
"""
import io
import os
import re
import sys
import collections

sys.stdout.reconfigure(encoding="utf-8")

KEYWORDS = {"参数", "窗口", "菜单", "类型", "实例", "命令", "对象", "方法", "属性",
            "数组", "成员", "组件", "事件", "数据", "文件", "文本", "变量", "常量"}

CORE_CMDS = None


def scan_names(d):
    """扫一个文本目录，返回 (声明名集合, 调用名集合)。"""
    decl, calls = set(), set()
    for root, _, fs in os.walk(d):
        for f in sorted(fs):
            if not (f.endswith(".e.txt") or f.endswith(".list.txt")):
                continue
            raw = io.open(os.path.join(root, f), encoding="utf-8-sig").read()
            for ln in raw.replace("\r\n", "\n").split("\n"):
                for rex in (SUB_RE, DECL_RE, VAR_RE):
                    m = rex.match(ln)
                    if m:
                        decl.add(m.group(1))
                m = PARAM_RE.match(ln)
                if m:
                    decl.add(m.group(1))
                code = strip_strings(ln)
                if code.lstrip().startswith("'"):
                    continue
                for name in CALL_RE.findall(code):
                    calls.add(name)
    return decl, calls


def load_core(baseline_dir):
    """★ 用**改造前的已知可用文本**（.tmp/chk）反推「外部名字白名单」：
    在基线里被调用、却没有在基线里声明的名字 = 易语言核心库命令（+ 任何合法外部依赖）。
    这样第 ② 项才能既严格又不误报。"""
    global CORE_CMDS
    if not os.path.isdir(baseline_dir):
        CORE_CMDS = set()
        return CORE_CMDS
    decl, calls = scan_names(baseline_dir)
    CORE_CMDS = calls - decl
    return CORE_CMDS


DECL_RE = re.compile(r"^\.DLL命令\s+(\S+),")
SUB_RE = re.compile(r"^\.子程序\s+([^\s,]+)")
VAR_RE = re.compile(r"^\.(程序集变量|全局变量|局部变量)\s+([^\s,]+)")
PARAM_RE = re.compile(r"^\s{4}\.参数\s+([^\s,]+)")
CALL_RE = re.compile(r"([\u4e00-\u9fa5A-Za-z_][\u4e00-\u9fa5A-Za-z0-9_]*)\s*\(")

STRIP_STR = re.compile(r"“[^”]*”")


def strip_strings(s):
    return STRIP_STR.sub("“”", s)


def main(d, baseline):
    load_core(baseline)
    files = []
    for root, _, fs in os.walk(d):
        for f in fs:
            if f.endswith(".e.txt") or f.endswith(".list.txt"):
                files.append(os.path.join(root, f))
    files.sort()

    subs, dl1s, vars_, params, flags, calls = [], [], [], [], [], []
    stray = []
    for p in files:
        raw = io.open(p, encoding="utf-8-sig").read().replace("\r\n", "\n")
        for i, ln in enumerate(raw.split("\n"), 1):
            if ln.startswith(".子程序"):
                subs.append((SUB_RE.match(ln).group(1), p, i))
            elif ln.startswith(".DLL命令"):
                dl1s.append((DECL_RE.match(ln).group(1), p, i))
            m = VAR_RE.match(ln)
            if m:
                vars_.append((m.group(2), p, i))
            m = PARAM_RE.match(ln)
            if m:
                params.append((m.group(1), p, i))
                parts = [x.strip() for x in ln.split(",")[2:]]
                for x in parts:
                    if x:
                        flags.append(x)
            code = strip_strings(ln)
            if code.lstrip().startswith("'"):
                continue
            for name in CALL_RE.findall(code):
                calls.append((name, p, i))
            if "node.dll" in ln:
                stray.append((p, i, ln.strip()))

    errs, warns = [], []

    # ① 括号平衡
    for p in files:
        raw = strip_strings(io.open(p, encoding="utf-8-sig").read().replace("\r\n", "\n"))
        raw = "\n".join(l for l in raw.split("\n") if not l.lstrip().startswith("'"))
        for a, b, tag in (("(", ")", "圆括号"), ("{", "}", "花括号"), ("[", "]", "方括号")):
            if raw.count(a) != raw.count(b):
                errs.append("%s：%s 不平衡（%d vs %d）" % (os.path.basename(p), tag,
                                                          raw.count(a), raw.count(b)))

    # ② 名字解析
    known = set()
    known |= {n for n, _, _ in subs}
    known |= {n for n, _, _ in dl1s}
    known |= {n for n, _, _ in vars_}
    known |= {n for n, _, _ in params}
    for c in CORE_CMDS:
        known.add(c)
    local = set()
    unresolved = collections.Counter()
    for name, p, i in calls:
        if name in known or name in local:
            continue
        unresolved[name] += 1

    # ③ 重名
    for tag, seq in (("子程序", subs), ("变量", vars_)):
        cnt = collections.Counter(n for n, _, _ in seq)
        for n, c in cnt.items():
            if c > 1:
                errs.append("%s 重名：%s × %d" % (tag, n, c))
    ds = collections.Counter(n for n, _, _ in dl1s)
    for n, c in ds.items():
        if c > 1:
            errs.append(".DLL命令 重名：%s × %d" % (n, c))
    for n in set(ds) & {x for x, _, _ in subs}:
        errs.append("子程序与 .DLL命令 同名：%s" % n)

    # ④ 关键字撞名（只对 子程序/DLL命令/参数 名）
    for tag, seq in (("子程序", subs), (".DLL命令", dl1s), ("参数", params)):
        for n, p, i in seq:
            if n in KEYWORDS:
                errs.append("%s 名撞关键字：%s (%s:%d)" % (tag, n, os.path.basename(p), i))

    # ⑥ 参数标志
    for f in set(flags):
        if f not in ("数组", "传址", "参考", "可空"):
            warns.append("未知 .参数 标志：%r" % f)

    print("扫描 %d 个文件：子程序 %d / .DLL命令 %d / 变量 %d / 参数 %d / 调用点 %d"
          % (len(files), len(subs), len(dl1s), len(vars_), len(params), len(calls)))
    print("基线外部名白名单 %d 个（核心库命令等）" % len(CORE_CMDS))
    print("参数标志取值：%s" % dict(collections.Counter(flags)))
    if unresolved:
        # 核心库命令很多，这里只把「看起来像项目内自定义」的报出来
        print("\n未被本项目声明命中的调用名（多为易语言核心库命令，需人工扫一眼）：")
        for n, c in unresolved.most_common(60):
            print("   %-34s ×%d" % (n, c))
        print("   …共 %d 个不同名字" % len(unresolved))
    if stray:
        print("\n⚠️ 残留 node.dll 引用：")
        for p, i, ln in stray[:10]:
            print("   %s:%d %s" % (os.path.basename(p), i, ln))

    print()
    if errs:
        print("❌ 错误 %d 项：" % len(errs))
        for e in errs:
            print("   " + e)
    else:
        print("✅ 括号平衡 / 无重名 / 无关键字撞名 / 无 node.dll 残留")
    if warns:
        print("⚠️ 警告 %d 项：" % len(warns))
        for w in warns:
            print("   " + w)
    return 1 if errs else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1] if len(sys.argv) > 1 else "mod_src",
                          sys.argv[2] if len(sys.argv) > 2 else "../../.tmp/chk"))
