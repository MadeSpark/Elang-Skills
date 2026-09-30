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
            if c in "([{":
                depth += 1
            elif c in ")]}":
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


def mask_quoted(line):
    """把**全角引号字符串内部**的字符换成 \\x01（保持长度不变）。

    用途：命令识别必须**跳过字符串字面量**，否则一句说明文字
    `报告 (“12 GetModuleHandleA(expld.dll) = 0x”)` 会被当成真的调用了
    `GetModuleHandleA`，报「命令不在支持库清单里」这种噪音警告。
    保持等长是为了让匹配偏移仍能用来切**原始行**（取参数要看原文）。"""
    out, in_q = [], False
    for c in line:
        if c == "“":
            in_q = True
            out.append(c)
        elif c == "”":
            in_q = False
            out.append(c)
        else:
            out.append("\x01" if in_q else c)
    return "".join(out)


def parse_calls(line):
    """提取一行里的命令调用：[(名字, 参数字段列表)]"""
    res, i, n = [], 0, len(line)
    masked = mask_quoted(line)          # 只用来定位，切片仍取原始 line
    while i < n:
        m = CALL_RE.search(masked, i)
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
                # ⚠️ 必须把 {} 也算进深度：`置入代码 ({ 139, 69, 8 })` 里的大括号是
                #    **字节集字面量**，里面的逗号不是参数分隔符。只算圆括号会让它被
                #    切成 N 个参数，误报「最多 1 个参数，给了 N 个」（踩过：一个
                #    手写机器码模块会一次报 7 个 ✗，把真问题淹掉）。
                if c in "([{":
                    depth += 1
                elif c in ")]}":
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


DLLCMD_RE = re.compile(r"^\.(?:DLL|Dll|dll)命令\s+([^\s,，]+)")


def collect_dllcmds(lines, subs):
    """把 `.DLL命令` 声明的名字也收成「已知命令」（带参数个数）。

    它们的实现在外部 DLL 里，**不在支持库清单里**；不单独收的话，工程里每一处
    调用都会报「命令不在支持库清单里」的噪音警告（踩过：一个 12 条声明的探针
    报出 6 个警告，把真问题淹掉）。"""
    cur, cnt = None, 0
    for raw in lines:
        line = strip_comment(raw)
        m = DLLCMD_RE.match(line)
        if m:
            if cur:
                subs[cur] = cnt
            cur, cnt = m.group(1), 0
            continue
        if cur:
            if re.match(r"^\s*\.参数\b", line):
                cnt += 1
            elif not DECL_RE.match(line):
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

    # 支持库文档：--lib → elibs 自动定位（./支持库文档、缓存；都没有就调技能自带导出器生成）
    idx, lib_path = None, args.lib
    try:
        import elibs
        docs = elibs.find_docs(args.lib, src)
        if docs:
            idx = elibs.LibIndex(docs)
            lib_path = os.path.join(docs, "commands.jsonl")
    except SystemExit as e:
        print(f"（支持库文档不可用：{e}）", file=sys.stderr)
    lib = idx.cmds if idx else load_lib(lib_path)

    errs, warns, subs = [], [], {}
    texts = {}
    for f in files:
        rel = os.path.relpath(f)
        with open(f, "r", encoding="utf-8-sig") as fh:
            lines = fh.read().replace("\r\n", "\n").split("\n")
        texts[rel] = lines
        collect_subs(lines, subs)
        collect_dllcmds(lines, subs)

    for rel, lines in texts.items():
        check_blocks(rel, lines, errs, warns)
        check_calls(rel, lines, subs, lib, errs, warns)

    # 库表检查：命令/类型/常量存在于本机支持库，但所属库不在工程库表里
    # → t2e 报「未定义的子程序或命令」、IDE 编译报找不到命令。
    if idx:
        cfg_path, _cfg, _have, missing = elibs.analyze(src, idx)
        if cfg_path is None and os.path.isdir(src):
            warns.append("没找到 配置/支持库.config.json（或 config/lib.config.json），跳过库表检查")
        for name, (libs, where) in sorted(missing.items()):
            f, ln, kind = where[0]
            more = f" 等 {len(where)} 处" if len(where) > 1 else ""
            errs.append(f"{f}:{ln} {kind}「{name}」属于支持库 {'/'.join(sorted(libs))}，"
                        f"但库表未引用{more} → elibs.py <文本目录> --fix")

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



