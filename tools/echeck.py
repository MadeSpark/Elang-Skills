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
    python echeck.py <文本目录|单个 .e.txt> --branch-only [--json]
    # --branch-only 只跑流程分支门禁；ecompile.py 在编译前强制调用它。

退出码：0 = 通过；1 = 不通过（有错误，或「流程分支」门禁有任何错误）。

流程分支门禁（2026-10-24 起，本文件最高优先级、编译前强制）：
    流程分支类检查 —— 块结构配对/缩进、隐藏块尾后的空行、块尾回退到更浅隐藏块尾、
    子程序末尾块尾的空行、空分支占位空行、`.如果` 缺 `.否则`、循环块首参数 —— 一律**错误级**：
    只进 errs，绝不进 warns；只要报出任何一条，退出码就是 1。
    **流程分支不允许出现警告**：不再存在「流程分支警告」这一档，也不允许把
    流程分支问题降级成提示后放行。
    原因：2026-10-20 的 A/B demo 事故里 check_sub_tail_gap 报了缺陷，结论却打印
    「✓ 通过（错误 0，警告 3）」、退出码 0；2026-10-24 又发现同一检查对
    「子程序末行是显式块尾」只报警告，导致 14 处真实缺陷仍能带着警告进入编译。
    现在这些检查全部按错误处理，且 ecompile.py 在编译前用 --branch-only 强制门禁，
    不解决就不允许编译。

其余检查的分级口径：
    「错误」= 有实证的、IDE 加载后可以直接看见后果的写法（丢块尾、红线越界、
             空分支框画不出、命令/参数不存在…）。
    「警告」= 经验上可疑但尚无「IDE 一定画错」实证的写法；只有**非流程分支**
             的警告可以伴随「✓ 通过」出现（例如支持库清单缺项）。
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


def check_blocks(path, lines, errs):
    """块首/块尾配对 + 缩进 + 中段标记合法性（全部为流程分支错误）"""
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
                    errs.append(f"{path}:{ln} 「.{kw}」缩进 {indent} 与块首 {stack[-1][1]} 不一致（流程线会串）")
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


# ---------------- 流程线空行规则检查 ----------------
# 隐藏块尾：IDE 不给它们画可见收尾线，流程线的走向全靠其后的空行
HIDDEN_TAIL_KIND = {"如果真结束", "如果结束"}

BLOCK_KW_RE = re.compile(r"^(\s*)\.([^\s(（]+)")


def parse_kw(raw):
    """提取块语句的 (缩进, 关键字)；非块语句（空行/普通语句/声明行）返回 (None, None)"""
    line = strip_comment(raw)
    if DECL_RE.match(line):
        return None, None
    m = BLOCK_KW_RE.match(line)
    if not m:
        return None, None
    return len(m.group(1)), m.group(2)


def check_hidden_tail_gap(path, lines, errs):
    """规则③+⑤：隐藏块尾（.如果真结束 / .如果结束）后面那段空行的检查。

    ⚠️ 本检查属于「流程分支门禁」，报出的每一条都是**错误**（见文件头 docstring）。

    IDE 不给这两种块尾画可见收尾线，流程线走向依赖块尾后面那行空行：
    ③ 隔在中间的空行缩进必须与隐藏块尾自身缩进一致，否则流程线对不齐 → 错误；
    ⑤ 隐藏块尾紧贴中段标记（.否则 / .默认 / .判断）→ 整个 .判断 的框画不出来
       → 错误（2026-10-03 深夜实证：A/B demo 的 case5/case6；原生代码 0 处紧贴）。
    ① 原规则「隐藏块尾紧贴下一条块尾 → 串线」**2026-10-20 删除**：
       那是合法的嵌套收口写法（A/B demo 的 `.如果真结束` 紧接 `.计次循环尾`），
       IDE 实开无弹框、剪贴板控制块模型 87/87 与 107/107 行完整、无丢行断线。
       原生代码里少见只是书写习惯，继续当缺陷查会让**正确的交付件永远报 ⚠**，
       与「流程分支 0 警告」的硬规则直接冲突，故整条删除而不是降级成提示。
    只查隐藏块尾后的衔接：显式块尾（判断结束/计次循环尾等）→块首/中段/显式块尾
    仍不查（IDE 会画可见收尾线，查了就是误报）。但「任意块尾→更浅隐藏块尾」
    是另一条规则⑥，由 check_unwind_tail_gap 单独判，不在这里重复。
    隐藏块尾 → 空行 → .子程序 等声明行、隐藏块尾紧贴同缩进块首都是合法写法
    （用户 IDE 负对照实测：case7 隐藏块尾紧贴块首渲染正常）。
    （统计基准：未经打包往返的原生代码里，「隐藏块尾→中段标记」0 处紧贴、
    0 处缩进不符。）
    """
    n = len(lines)
    for ln, raw in enumerate(lines, 1):
        indent, kw = parse_kw(raw)
        if kw not in HIDDEN_TAIL_KIND:
            continue
        j = ln  # 下一行下标（0 基）
        gaps = []
        while j < n and lines[j].strip() == "":
            gaps.append(lines[j])
            j += 1
        if j >= n:
            continue  # 文件末尾，无需检查
        _, next_kw = parse_kw(lines[j])
        if next_kw not in TAIL_KIND and next_kw not in ("否则", "默认", "判断"):
            continue  # 只查「隐藏块尾 → 块尾/中段标记」衔接，其余写法合法
        if not gaps:
            # 相邻块尾：本函数不判（反方向「隐藏尾→显式尾」合法）；
            # 「任意块尾→更浅隐藏块尾」由 check_unwind_tail_gap 判。
            if next_kw not in ("否则", "默认", "判断"):
                continue
            errs.append(
                f"{path}:{ln} 隐藏块尾「.{kw}」紧贴着中段标记「.{next_kw}」（中间无空行）"
                f"→ 整个 .判断 的框在 IDE 里画不出来（该行后面补一个 {indent} 空格缩进的空行）")
            continue
        for g in gaps:
            g_ind = len(g) - len(g.lstrip())
            if g_ind != indent:
                errs.append(
                    f"{path}:{ln} 「.{kw}」后的空行缩进 {g_ind} ≠ 本行缩进 {indent}"
                    f"→ IDE 流程线对不齐（把空行改成 {indent} 空格缩进）")
                break  # 同一位置只报一条，避免刷屏淹掉真问题


def check_unwind_tail_gap(path, lines, errs):
    """规则⑥：块尾 A 后紧贴更浅的隐藏块尾 B 时，中间必须有一条与 A 同缩进的空行。

    2026-10-24 实证（会话 session-e0f8c9bd-... 的「格式化字节数」）：
        .如果结束          ← 8 空格（内层块尾）
        .如果真结束        ← 4 空格（外层隐藏块尾），中间没有空行
    IDE 会把外层隐藏块尾的收口箭头挂到内层最后一个返回语句上，红线从
    格式化字节数 的两个 KB/MB 分支一路串到后面；补一条 8 空格空行后恢复。

    规则只看块尾与缩进关系，不匹配子程序名：
      · A 是任意块尾（.判断结束 / .如果结束 / .如果真结束 / .计次循环尾 …）；
      · B 是隐藏块尾（.如果真结束 / .如果结束），且 B 的缩进比 A 更浅；
      · 要求 A、B 之间至少有一条与 A 同缩进的空行；没有空行或缩进不对 → 错误。

    全量修复工程（274 个文件）实测：该形态 129 处全部带 A 缩进空行；
    修复前 11 处紧贴全部是缺陷，补空行后 0 处。大项目基线（同一工程的旧版）
    命中 8 处，经会话逐条核对都是同一类未修复缺陷，不是误报；全量修复版 0 处。
    """
    n = len(lines)
    for ln, raw in enumerate(lines, 1):
        indent, kw = parse_kw(raw)
        if kw not in TAIL_KIND:
            continue
        j = ln  # 下一行下标（0 基）
        gaps = []
        while j < n and lines[j].strip() == "":
            gaps.append(lines[j])
            j += 1
        if j >= n:
            continue
        next_indent, next_kw = parse_kw(lines[j])
        if next_kw not in HIDDEN_TAIL_KIND:
            continue
        if next_indent is None or next_indent >= indent:
            continue
        if gaps and kw in HIDDEN_TAIL_KIND:
            # 隐藏块尾→隐藏块尾的空行缩进错误已由 check_hidden_tail_gap 报过，
            # 这里只补它没管的「无空行」分支，避免同一位置刷两条。
            continue
        indents = [len(g) - len(g.lstrip()) for g in gaps]
        if any(g == indent for g in indents):
            continue
        if not gaps:
            reason = "中间没有空行"
        else:
            reason = f"中间的空行缩进为 {indents}，不是 {indent}"
        errs.append(
            f"{path}:{ln} 块尾「.{kw}」与更浅的隐藏块尾「.{next_kw}」（第{j + 1}行）之间{reason}"
            f" → IDE 会把隐藏块尾的收口挂到内层最后一条语句上（流程线串线）；"
            f"在该行后补一个 {indent} 空格缩进的空行")


def check_sub_tail_gap(path, lines, errs):
    """规则②：子程序末行是块尾时，其后到下一个 .子程序 之间必须存在
    至少一条与块尾同缩进的空行，否则 IDE 流程线会断开并串到下一个子程序。
    （末行是普通语句 → 不检查。）

    ⚠️ 2026-10-24 起本检查的**每一档都是错误**，不再有「只报警告」的分级：
      · 文件最后一个子程序 + 隐藏块尾（.如果真结束 / .如果结束）→ 错误；
      · 隐藏块尾后面还跟着 .子程序 → 错误；
      · 显式块尾（.判断循环尾 / .计次循环尾 等）→ 错误；
      · 有同缩进空行但缩进不对 → 错误。

    为什么显式块尾/中段也要升错误：2026-10-24 的实测工程里，同一检查对
    「子程序末行是 .判断循环尾 / .计次循环尾」只报 ⚠，结果 14 处缺空行仍能带着
    警告进入编译。用户明确要求流程线不允许出现任何警告；流程分支问题必须按错误
    处理，ecompile.py 也会在编译前用 --branch-only 强制拦下。

    证据（两类都已在 IDE 侧确认会串线/丢块尾）：
      · 隐藏块尾在文件末尾：demos\\下载接管 的 A/B demo 最后那个子程序末行是
        `.如果真结束`，其后只有 0 缩进空行；IDE 打开弹「控制块有格式错误」，
        流程线拖到窗口底，剪贴板模型丢掉最后一行块尾。
      · 显式块尾在子程序末尾：QAQ-ETool 大项目语料 消息cmd解析及自检等.static.e.txt
        有 14 处 `.判断循环尾` / `.计次循环尾` 后没有同缩进空行；历史版本
        「能渲染」不能作为放行依据，流程分支门禁从 2026-10-24 起不再接受该形态。
    """
    n = len(lines)
    body_start = None
    for i, raw in enumerate(lines):
        if SUB_RE.match(strip_comment(raw)):
            if body_start is not None:
                _check_sub_tail_body(path, lines, body_start, i, errs,
                                     closed_by_sub=True)
            body_start = i + 1
    if body_start is not None:
        _check_sub_tail_body(path, lines, body_start, n, errs,
                             closed_by_sub=False)


def _check_sub_tail_body(path, lines, start, end, errs, closed_by_sub):
    """检查 [start, end) 范围内单个子程序体的末尾空行规则（错误级）"""
    last = None
    for i in range(end - 1, start - 1, -1):
        if lines[i].strip() != "":
            last = i
            break
    if last is None:
        return  # 空子程序体
    indent, kw = parse_kw(lines[last])
    if kw not in TAIL_KIND:
        return  # 末行是普通语句 → 不检查
    has_blank = False
    for i in range(last + 1, end):
        if lines[i].strip() == "":
            has_blank = True
            g_ind = len(lines[i]) - len(lines[i].lstrip())
            if g_ind == indent:
                return  # 找到同缩进空行 → 合格
    where = "下一个 .子程序" if closed_by_sub else "文件末尾"
    tail = ("→ IDE 流程线会断开并串到下一个子程序；若是文件最后一个子程序，"
            "红线会越过最后一行画到窗口底部")
    if not has_blank:
        errs.append(
            f"{path}:{last + 1} 子程序末行是块尾「.{kw}」，其后到{where}之间没有空行 {tail}"
            f"（在该行后面补一个 {indent} 空格缩进的空行）")
    else:
        errs.append(
            f"{path}:{last + 1} 子程序末行是块尾「.{kw}」，其后到{where}之间没有 {indent} 空格缩进的空行 {tail}"
            f"（把空行补成 {indent} 空格缩进）")


def check_empty_branch_gap(path, lines, errs):
    """规则④：.否则 / .默认 与 .判断结束 之间没有占位空行（空分支）时，
    整个 .判断 的框在 IDE 里画不出来，必须插占位空行。

    ⚠️ 2026-10-20 起本检查直接算**错误**（不是警告）：后果是「整个 .判断 的框
    消失」，属于有实证的流程分支缺陷，且原生代码 42 处空分支全都带占位空行。
    查下一非空行时必须跳过空行——只看紧邻一行会被占位空行挡住而漏报。
    ⚠️ 占位空行的缩进【不做检查】：原生代码 42 处空分支的占位空行
    全部是「中段缩进 + 4」（.默认 在 4、空行在 8），而 A/B demo 的 v4 版
    用「中段自身缩进」（4）也渲染正常 ⇒ 两种缩进都合法，只要求存在。
    （第三轮曾断言「占位空行缩进 = 中段自身缩进，42 处全部如此」——错，
    当时没逐条验缩进；按它做检查会对原生代码误报 42 处。）
    """
    n = len(lines)
    for ln, raw in enumerate(lines, 1):
        indent, kw = parse_kw(raw)
        if kw not in ("否则", "默认"):
            continue
        j = ln  # 下一行下标（0 基），跳过空行找第一条非空行
        gaps = []
        while j < n and lines[j].strip() == "":
            gaps.append(lines[j])
            j += 1
        if j >= n:
            continue
        _, next_kw = parse_kw(lines[j])
        if next_kw != "判断结束":
            continue  # 分支有内容 → 不报
        if gaps:
            continue  # 已有占位空行（原生惯例缩进=中段+4，不强制）→ 不报
        errs.append(
            f"{path}:{ln} 「.{kw}」与「.判断结束」之间没有占位空行（空分支）"
            f"→ 整个 .判断 的框在 IDE 里画不出来（插一个 {indent} 空格缩进的占位空行）")


def check_if_needs_else(path, lines, errs):
    """检查 .如果 块是否配有 .否则（.如果真 不需要）。

    独立维护一个只跟踪 .如果 的小栈（不动 check_blocks 的栈）：
    .如果 入栈时记「未见否则」标记，遇到 .否则 清除栈顶标记，
    弹出 .如果结束 时标记还在 → error（t2e 会静默把无否则的 .如果
    改写成 .如果真，语义悄悄变了）。
    """
    stack = []  # [起始行号, 是否未见否则]
    for ln, raw in enumerate(lines, 1):
        _, kw = parse_kw(raw)
        if kw == "如果":
            stack.append([ln, True])
        elif kw == "否则":
            if stack:
                stack[-1][1] = False
        elif kw == "如果结束":
            if stack:
                start_ln, no_else = stack.pop()
                if no_else:
                    errs.append(
                        f"{path}:{ln} 「.如果」（第{start_ln}行）没有配对的「.否则」"
                        f"（t2e 会静默把它改写成 .如果真）")


def _extract_head_args(line):
    """提取块首行第一个括号内的参数文本；没有括号返回 None。

    不能复用 parse_calls：块首行以「.关键字」开头，CALL_RE 会把整段
    「.变量循环首」连点号一起当命令名而拒绝识别，只认出行内的普通调用，
    导致块首自身参数被漏掉。这里直接定位第一个括号，跳过全角引号内的
    内容并按嵌套括号深度找配对右括号。
    """
    start = -1
    for idx, c in enumerate(line):
        if c in "(（":
            start = idx
            break
    if start < 0:
        return None
    depth, in_q = 0, False
    for k in range(start, len(line)):
        c = line[k]
        if c == "“":
            in_q = True
        elif c == "”":
            in_q = False
        elif not in_q:
            if c in "([{":
                depth += 1
            elif c in ")]}":
                depth -= 1
                if depth == 0:
                    return line[start + 1:k]
    return line[start + 1:]  # 括号不闭合（坏行），取到行尾


def check_loop_head_args(path, lines, errs):
    """循环块首参数个数检查：
    .判断循环首 只接受 1 个参数（条件）；.变量循环首 需要 2 个参数（起始值、终止值）。
    参数切分复用 split_args（已处理全角引号与嵌套括号）；
    整行没有括号参数的情况按 0 个参数处理。
    """
    for ln, raw in enumerate(lines, 1):
        _, kw = parse_kw(raw)
        if kw not in ("判断循环首", "变量循环首"):
            continue
        inner = _extract_head_args(strip_comment(raw))
        n_args = 0
        if inner is not None:
            args, bad = split_args(inner)
            if not bad and not (len(args) == 1 and args[0].strip() == ""):
                n_args = len(args)
        if kw == "判断循环首" and n_args >= 2:
            errs.append(f"{path}:{ln} 「.判断循环首」只接受 1 个参数（条件），给了 {n_args} 个")
        elif kw == "变量循环首" and n_args < 2:
            errs.append(f"{path}:{ln} 「.变量循环首」需要 2 个参数（起始值、终止值），只给了 {n_args} 个")


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


def safe_relpath(path):
    """os.path.relpath 的安全封装。

    脚本工作目录与被检文件不在同一盘符时（如在 D: 工作区检查解包到
    C: 临时目录的文件），relpath 会抛 ValueError 直接崩溃；
    此时退回绝对路径，保证检查流程不中断。
    """
    try:
        return os.path.relpath(path)
    except ValueError:
        return os.path.abspath(path)


def read_texts(files):
    """读取全部 .e.txt，返回 {相对路径: 行列表}。"""
    texts = {}
    for f in files:
        rel = safe_relpath(f)
        with open(f, "r", encoding="utf-8-sig") as fh:
            lines = fh.read().replace("\r\n", "\n").split("\n")
        texts[rel] = lines
    return texts


def run_branch_checks(texts, errs):
    """跑全部流程分支检查，**只产生错误**，绝不产生警告。

    返回的 branch 字典同时供人读输出、JSON 和 ecompile.py 的编译前门禁使用：
      {"errors": N, "warnings": 0, "passed": bool, "messages": [...]}
    """
    branch = {"errors": 0, "warnings": 0, "passed": False, "messages": []}
    for rel, lines in texts.items():
        be = len(errs)
        check_blocks(rel, lines, errs)
        check_hidden_tail_gap(rel, lines, errs)
        check_unwind_tail_gap(rel, lines, errs)
        check_sub_tail_gap(rel, lines, errs)
        check_empty_branch_gap(rel, lines, errs)
        check_if_needs_else(rel, lines, errs)
        check_loop_head_args(rel, lines, errs)
        new = errs[be:]
        branch["errors"] += len(new)
        branch["messages"].extend(new)
    branch["warnings"] = 0
    branch["passed"] = branch["errors"] == 0
    return branch


def main():
    ap = argparse.ArgumentParser(description="易语言文本源码静态检查（块结构 + 命令/参数）")
    ap.add_argument("src", help="文本目录或单个 .e.txt")
    ap.add_argument("--lib", help="commands.jsonl 路径（缺省自动找 ../支持库文档 或 技能 assets 产物）")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--branch-only", action="store_true",
                    help="只跑流程分支门禁（ecompile.py 编译前强制调用）；退出码只由流程分支决定")
    args = ap.parse_args()

    src = os.path.abspath(args.src)
    files = ([src] if os.path.isfile(src) else
             sorted(glob.glob(os.path.join(src, "**", "*.e.txt"), recursive=True)))
    if not files:
        raise SystemExit("没有找到 .e.txt 文件")

    texts = read_texts(files)
    errs = []
    branch = run_branch_checks(texts, errs)
    branch_ok = branch["passed"]

    # --branch-only：编译前门禁专用。不做命令/库表检查，避免把检查器噪音带进编译链。
    if args.branch_only:
        if args.json:
            print(json.dumps({"errors": errs, "warnings": [],
                              "branch": branch, "lib": False,
                              "branchOnly": True},
                             ensure_ascii=False, indent=2))
        else:
            print(f"检查文件 {len(files)} 个；仅流程分支门禁")
            for e in errs:
                print("  ✗ " + e)
            if branch_ok:
                print("流程分支: ✓ 0 错误（硬规则：流程分支问题一律为错误，任一即不通过）")
            else:
                print(f"流程分支: ✗ 错误 {branch['errors']}"
                      "（硬规则：流程分支问题一律为错误，不解决不准编译）")
        return 0 if branch_ok else 1

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

    warns, subs = [], {}
    for lines in texts.values():
        collect_subs(lines, subs)
        collect_dllcmds(lines, subs)

    for rel, lines in texts.items():
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
        print(json.dumps({"errors": errs, "warnings": warns,
                          "branch": branch, "lib": bool(lib),
                          "branchOnly": False},
                         ensure_ascii=False, indent=2))
    else:
        print(f"检查文件 {len(files)} 个；支持库清单: {'已加载 ' + str(lib_path) if lib else '未找到（跳过命令校验）'}")
        for e in errs:
            print("  ✗ " + e)
        for w in warns:
            print("  ⚠ " + w)
        if branch_ok:
            print("流程分支: ✓ 0 错误（硬规则：流程分支问题一律为错误，任一即不通过）")
        else:
            print(f"流程分支: ✗ 错误 {branch['errors']}"
                  "（硬规则：流程分支问题一律为错误，不解决不准编译）")
        if not branch_ok:
            print(f"结论: ✗ 流程分支不合格（总错误 {len(errs)}，总警告 {len(warns)}）")
        elif errs:
            print(f"结论: ✗ 存在错误（错误 {len(errs)}，警告 {len(warns)}）")
        else:
            print(f"结论: ✓ 通过（错误 0，警告 {len(warns)}）")
    return 0 if (branch_ok and not errs) else 1


if __name__ == "__main__":
    sys.exit(main())
