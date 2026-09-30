#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ec_guard.py —— 「.ec 交付门禁」共享实现（mkcage.py / ecompile.py 共用）。

红线（2026-09-26 用户当面要求，写死在工具链里，违反任何一条都不准交付）：
  1. `.ec` 必须由 `.e` 经官方 IDE 编译产出（头 8 字节 = b"CNWTEPRG"）；
     `QAQ-ETool -mode t2e` 的产物只是 `.e` 源码，严禁改扩展名/伪装成 `.ec` 部署
     （IDE 会报「本模块程序未经编译，不能载入」）。
  2. t2e / e2t 出现 `[错误]` 行或退出码 2（无法解析 / 未验证代码）⇒ 代码没有被
     完整转换 ⇒ 立即失败，严禁带着错误继续编译。
  3. 任何候选 `.ec` 部署前必须反向抽取（e2t）验证关键语句仍在；
     关键语句缺失 = 丢代码，不准交付。

guard 文件格式（UTF-8 文本，mkcage/ecompile 的 --guard 参数）：
  - 空行与 `#` 开头行忽略
  - `@funcbody   <子程序名> <最少正文行数>`      函数体内非空/非注释/非声明行数下限
  - `@funcassign <子程序名> <最少条数> <行内正则>` 函数体内匹配该正则的行数下限
  - 其余每行 = 一个正则；必须在某个 *.e.txt 的「非注释行」中命中（支持跨行匹配）
"""
from __future__ import annotations

import fnmatch
import os
import re

TEXT_SUFFIX = "*.e.txt"
DECL_PREFIXES = (".局部变量", ".参数", ".变量", ".子程序", ".程序集")


# ---------------------------------------------------------------- 转换错误（红线2）
def error_lines(*streams: str) -> list:
    """合并多段 QAQ-ETool 日志，返回 `[错误]` 行。

    QAQ-ETool 的 `[错误]` 只用于「代码没能被完整转换」：e2t 的无法解析处、
    t2e 的绑定失败（会写成未验证代码）。支持库未加载之类的降级只打 `[警告]`，不算。
    """
    bad = []
    for s in streams:
        if not s:
            continue
        for ln in s.splitlines():
            if ln.startswith("[错误]"):
                bad.append(ln.rstrip())
    return bad


def _summary(*streams: str) -> str:
    """stdout 里的 [信息] 摘要行（含「无法解析 N 处」/「未验证代码 N 处」）。"""
    out = []
    for s in streams:
        for ln in (s or "").splitlines():
            if ln.startswith("[信息]"):
                out.append(ln.strip())
    return "；".join(out)


def parse_error_block(*streams: str, rc: int | None = None) -> str:
    """有 `[错误]` 行或退出码 2 时拼一条可直接打给 stderr 的失败说明；无错返回空串。

    rc 传 QAQ-ETool 的退出码：2 = 已产出但有无法解析 / 未验证的内容，同样按红线2 拦下。
    """
    errs = error_lines(*streams)
    if not errs and rc != 2:
        return ""
    head = "%d 条 [错误]" % len(errs) if errs else "退出码 2"
    info = _summary(*streams)
    return ("%s（代码未被完整转换，红线2：严禁继续）%s\n  %s"
            % (head, "：" + info if info else "", "\n  ".join(errs[:5]))).rstrip()


# ---------------------------------------------------------------- guard 规则
def load_guard(path: str):
    """读 guard 文件 → (regex_rules: [str], func_rules: [(kind, name, n, pattern)])。"""
    regex_rules, func_rules = [], []
    with open(path, encoding="utf-8-sig") as f:
        for raw in f:
            ln = raw.strip()
            if not ln or ln.startswith("#"):
                continue
            if ln.startswith("@funcbody"):
                _, name, n = ln.split()
                func_rules.append(("body", name, int(n), None))
            elif ln.startswith("@funcassign"):
                _, name, n, pat = ln.split(None, 3)
                func_rules.append(("assign", name, int(n), pat))
            else:
                re.compile(ln)                     # 先编译一次，语法错立即暴露
                regex_rules.append(ln)
    return regex_rules, func_rules


def _code_lines(path: str):
    """读一个 .e.txt → 原始行列表（统一 \n、去 BOM）。注释行=剥掉 ' 后为空的行被剔除。"""
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        lines = [ln.rstrip("\r\n") for ln in f]
    out = []
    for ln in lines:
        if ln.lstrip().startswith("'"):
            continue                               # 整行注释不参与任何匹配
        out.append(ln)
    return out


def _iter_code_files(txt_dir: str):
    for root, _dirs, files in os.walk(txt_dir):
        for fn in sorted(files):
            if fnmatch.fnmatch(fn.lower(), TEXT_SUFFIX):
                yield os.path.join(root, fn)


def _func_span(lines, name):
    """定位 `.子程序 <name>` 块（到下一个 .子程序/.程序集 为止）；找不到返回 None。"""
    start = None
    for i, ln in enumerate(lines):
        if re.match(r"^\s*\.子程序\s+" + re.escape(name) + r"\s*(,|$)", ln):
            start = i
            break
    if start is None:
        return None
    end = len(lines)
    for j in range(start + 1, len(lines)):
        if re.match(r"^\s*\.(子程序|程序集)\s", lines[j]):
            end = j
            break
    return start, end


def check_text_dir(txt_dir: str, guard_path: str) -> list:
    """对文本目录执行 guard 规则，返回失败描述列表（空列表 = 全过）。"""
    regex_rules, func_rules = load_guard(guard_path)
    joined_texts = []                              # [(文件名, 非注释行join)]
    func_hits = {}                                 # (kind,name) -> [计数]
    for p in _iter_code_files(txt_dir):
        lines = _code_lines(p)
        joined_texts.append((os.path.relpath(p, txt_dir), "\n".join(lines)))
        for kind, name, _n, _pat in func_rules:
            span = _func_span(lines, name)
            if span is None:
                continue
            s, e = span
            if kind == "body":
                n = sum(1 for ln in lines[s:e]
                        if ln.strip() and not ln.lstrip().startswith(DECL_PREFIXES))
            else:
                n = sum(1 for ln in lines[s:e]
                        if re.search(_pat, ln) and not ln.lstrip().startswith(DECL_PREFIXES))
            func_hits.setdefault((kind, name), []).append(n)

    fails = []
    for rule in regex_rules:
        hit = next((fn for fn, txt in joined_texts if re.search(rule, txt, re.M)), None)
        if hit is None:
            fails.append("关键语句缺失: /%s/（所有 *.e.txt 非注释行都未命中）" % rule)
    for kind, name, n, pat in func_rules:
        counts = func_hits.get((kind, name), [])
        best = max(counts) if counts else 0
        if best < n:
            what = "正文行数" if kind == "body" else "匹配 /%s/ 的行数" % pat
            fails.append("子程序 %s 的%s = %d < 要求 %d%s"
                         % (name, what, best, n,
                            "" if counts else "（未找到该子程序）"))
    return fails
