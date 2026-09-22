#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
echeck —— 易语言文本源码静态检查（不依赖易语言 IDE）

与「导出支持库文档」走同一条数据路：用 commands.jsonl（按本机已装支持库导出，
库数与命令数取决于本机环境）
校验代码里每个命令调用的「命令是否存在、参数个数是否超界、留空是否可空」，
再做块结构配对检查（判断/如果/如果真/循环 的 块首块尾配对与缩进）。

用法：
    python echeck.py <文本目录|单个 .e.txt> [--lib <commands.jsonl>] [--json]

退出码：0 = 无错误；1 = 有错误（警告不算）。
"""

import argparse
import glob
import json
import os
import re
import sys

# ---------------- 块结构定义 ----------------
# 块首（可再分：起块 / 中段标记）
HEAD_KIND = {
    "判断开始": "judge", "判断": "judge_mid", "默认": "judge_default",
    "如果": "if", "否则": "if_else", "如果真": "iftrue",
    "计次循环首": "count", "判断循环首": "while",
    "变量循环首": "for", "循环判断首": "dowhile",
}
TAIL_KIND = {
    "判断结束": "judge", "如果结束": "if", "如果真结束": "iftrue",
    "计次循环尾": "count", "判断循环尾": "while",
    "变量循环尾": "for", "循环判断尾": "dowhile",
}
# 中段标记必须匹配的栈顶块
MID_EXPECT = {"judge_mid": "judge", "judge_default": "judge", "if_else": "if"}

DECL_RE = re.compile(r"^\s*\.(子程序|参数|局部变量|程序集变量|常量|支持库|程序集|版本)\b")
SUB_RE = re.compile(r"^\.子程序\s+([^\s,，]+)")
CALL_RE = re.compile(r"([^\s(（]+?)\s*\(")


def strip_comment(line):
    """去掉 ' 注释（不处理全角引号内的半角单引号——易语言字符串里极少见）"""
    i = line.find("'")
    return line if i < 0 else line[:i]


def split_args(text):
    """按顶层逗号切参数；返回 (字段列表, 是否嵌套出错)"""
    out, depth, cur, i = [], 0, "", 0
    in_q = False
    while i < len(text):
        c = text[i]
        if c == "“":
            in_q = True
        elif c == "”":
            in_q = False
        elif not in_q:
            if c in "([":
                depth += 1
            elif c in ")]":
                depth -= 1
            elif c == "," and depth == 0:
                out.append(cur)
                cur = ""
                i += 1
                continue
        cur += c
        i += 1
    out.append(cur)
    return out, depth != 0


def parse_calls(line):
    """提取一行里的命令调用：[(名字, 参数字段列表)]"""
    res, i, n = [], 0, len(line)
    while i < n:
        m = CALL_RE.search(line, i)
        if not m:
            break
        name, j = m.group(1), m.end()
        depth, k, in_q = 1, j, False
        while k < n and depth:
            c = line[k]
            if c == "“":
                in_q = True
            elif c == "”":
                in_q = False
            elif not in_q:
                if c == "(":
                    depth += 1
                elif c == ")":
                    depth -= 1
            k += 1
        inner = line[j:k - 1] if depth == 0 else line[j:]
        if re.match(r"^[\u4e00-\u9fffA-Za-z_][\u4e00-\u9fffA-Za-z0-9_]*$", name):
            args, bad = split_args(inner)
            if not bad and not (len(args) == 1 and args[0].strip() == ""):
                res.append((name, args))
        i = k
    return res


def check_blocks(path, lines, errs, warns):
    """块首/块尾配对 + 缩进 + 中段标记合法性"""
    stack = []
    for ln, raw in enumerate(lines, 1):
        line = strip_comment(raw)
        if DECL_RE.match(line):
            continue
        m = re.match(r"^(\s*)\.([^\s(（]+)", line)
        if not m:
            continue
        kw, indent = m.group(2), len(m.group(1))
        if kw in HEAD_KIND:
            kind = HEAD_KIND[kw]
            if kind in MID_EXPECT:
                if not stack or stack[-1][0] != MID_EXPECT[kind]:
                    errs.append(f"{path}:{ln} 「.{kw}」不在对应块内（栈顶=" + (stack[-1][0] if stack else "空") + "）")
                elif stack[-1][1] != indent:
                    warns.append(f"{path}:{ln} 「.{kw}」缩进 {indent} 与块首 {stack[-1][1]} 不一致")
            else:
                stack.append((kind, indent, ln))
        elif kw in TAIL_KIND:
            kind = TAIL_KIND[kw]
            if not stack:
                errs.append(f"{path}:{ln} 「.{kw}」没有可配对的块首")
            elif stack[-1][0] != kind:
                errs.append(f"{path}:{ln} 「.{kw}」与栈顶块不匹配（栈顶={stack[-1][0]}，第{stack[-1][2]}行起）")
            elif stack[-1][1] != indent:
                errs.append(f"{path}:{ln} 「.{kw}」缩进 {indent} ≠ 块首缩进 {stack[-1][1]}（流程线会串）")
            else:
                stack.pop()
    for kind, indent, ln in stack:
        errs.append(f"{path}:{ln} 块「{kind}」缺块尾（.判断结束 / .如果结束 / .计次循环尾 等）")


def load_lib(jsonl):
    """commands.jsonl → {命令名: [(参数flags列表, 库名)]}"""
    table = {}
    if not jsonl or not os.path.isfile(jsonl):
        return table
    with open(jsonl, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                d = json.loads(line)
            except ValueError:
                continue
            flags = [a.get("flags", "") for a in d.get("args", [])]
            table.setdefault(d.get("name", ""), []).append((flags, d.get("lib", "")))
    return table


def collect_subs(lines, subs):
    """收集本工程子程序：名 → 参数个数"""
    cur, cnt = None, 0
    for raw in lines:
        line = strip_comment(raw)
        m = SUB_RE.match(line)
        if m:
            if cur:
                subs[cur] = cnt
            cur, cnt = m.group(1), 0
            continue
        if cur and re.match(r"^\s*\.参数\b", line):
            cnt += 1
        elif cur and not DECL_RE.match(line):
            subs[cur] = cnt
            cur = None
    if cur:
        subs[cur] = cnt


def check_one_call(path, ln, name, args, subs, lib, errs, warns):
    if "." in name:
        return  # 对象方法调用（如 文档.解析）不在支持库清单里，无法静态校验
    if name in subs:
        if len(args) > subs[name]:
            errs.append(f"{path}:{ln} 子程序「{name}」只要 {subs[name]} 个参数，给了 {len(args)} 个")
        return
    if name not in lib:
        warns.append(f"{path}:{ln} 命令「{name}」不在支持库清单里（可能是子程序/常量，也可能写错名）")
        return
    cands = lib[name]
    best = max(cands, key=lambda c: len(c[0]))
    for flags, libname in cands:
        if len(args) <= len(flags):
            best = (flags, libname)
            break
    flags, libname = best
    if len(args) > len(flags):
        errs.append(f"{path}:{ln} 「{name}」({libname}) 最多 {len(flags)} 个参数，给了 {len(args)} 个")
    elif len(args) < len(flags):
        need = flags[len(args):]
        if not all("可空" in f for f in need):
            warns.append(f"{path}:{ln} 「{name}」有 {len(flags)} 个参数，只给了 {len(args)} 个，"
                         f"缺的位置不是「可空」")


def check_calls(path, lines, subs, lib, errs, warns):
    for ln, raw in enumerate(lines, 1):
        line = strip_comment(raw)
        if DECL_RE.match(line) or not line.strip() or line.strip().startswith("."):
            continue
        work = [line]
        while work:
            for name, args in parse_calls(work.pop()):
                check_one_call(path, ln, name, args, subs, lib, errs, warns)
                work.extend(a for a in args if "(" in a)


def main():
    ap = argparse.ArgumentParser(description="易语言文本源码静态检查（块结构 + 命令/参数）")
    ap.add_argument("src", help="文本目录或单个 .e.txt")
    ap.add_argument("--lib", help="commands.jsonl 路径（缺省自动找 ../支持库文档 或 技能 assets 产物）")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    src = os.path.abspath(args.src)
    files = ([src] if os.path.isfile(src) else
             sorted(glob.glob(os.path.join(src, "**", "*.e.txt"), recursive=True)))
    if not files:
        raise SystemExit("没有找到 .e.txt 文件")

    lib_path = args.lib
    if not lib_path:
        for cand in (os.path.join(os.getcwd(), "支持库文档", "commands.jsonl"),
                     os.path.join(os.path.dirname(src), "支持库文档", "commands.jsonl")):
            if os.path.isfile(cand):
                lib_path = cand
                break
    lib = load_lib(lib_path)

    errs, warns, subs = [], [], {}
    texts = {}
    for f in files:
        rel = os.path.relpath(f)
        with open(f, "r", encoding="utf-8-sig") as fh:
            lines = fh.read().replace("\r\n", "\n").split("\n")
        texts[rel] = lines
        collect_subs(lines, subs)

    for rel, lines in texts.items():
        check_blocks(rel, lines, errs, warns)
        check_calls(rel, lines, subs, lib, errs, warns)

    if args.json:
        print(json.dumps({"errors": errs, "warnings": warns, "lib": bool(lib)},
                         ensure_ascii=False, indent=2))
    else:
        print(f"检查文件 {len(files)} 个；支持库清单: {'已加载 ' + str(lib_path) if lib else '未找到（跳过命令校验）'}")
        for e in errs:
            print("  ✗ " + e)
        for w in warns:
            print("  ⚠ " + w)
        print(f"结论: {'✓ 通过' if not errs else '✗ 存在错误'}（错误 {len(errs)}，警告 {len(warns)}）")
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main())



