#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""readlog.py —— 读「调试输出面板」日志（`elaunch.py --log` 落的那种），
支持一次性 dump 与**增量跟随**（运行中边跑边读）。

面板文本是 **GBK**；本脚本默认把它转成 **UTF-8** 打到标准输出 —— **显式按 UTF-8 字节
写出**（不走控制台代码页），这样「默认 GBK → UTF-8」的契约在任何终端/管道下都成立，
AI 可以直接读、直接搜中文（拿原始 GBK 字节去 grep 中文会「看起来什么都没抓到」）。

用法
====
    python readlog.py <日志路径>
      --raw            原样输出原始字节（不转码；与 --business 同用时仅对全文输出生效）
      --business       只保留被调试程序**自己输出**的行（`输出调试文本 ()` / `调试输出 ()`）。
                       **两类都收**（缺一会漏）：
                       ① 独立行 `[HH:MM:SS] * …` —— 增量块第 2 行起都是这种；
                       ② 增量块**首行** —— 它被记在包装行里：
                          `[HH:MM:SS.mmm][PANEL-DELTA] DELTA: [HH:MM:SS] * …`
                          （实测：只输出一行的程序，业务行**只**出现在包装行里）
      --follow         先 dump，再**增量跟随**后续追加的内容（运行中读取）
      --idle <秒>      --follow 时空闲多久就停（默认 0 = 一直跟到 Ctrl-C）
      --timeout <秒>   --follow 的总时长上限（默认 0 = 不设上限）
      --json           打印结构化结果（文件、字节数、行数、业务行、退出码、结束语）。
                       与 --follow 同用时：先打印一次快照 JSON，再开始跟随

退出码：0 = 正常（含 Ctrl-C 结束跟随）；2 = 文件不存在。

实现要点（评审后修正，勿退化）
==============================
  * 文本模式一律**显式写 UTF-8 字节**到 stdout（不经 locale/text 层）。
  * `--follow` 按**字节**累积余量、只在 `b"\n"` 处切行：业务行跨块不会丢，
    GBK 双字节字符跨块也不会被截成替换符（多字节编码不含 0x0A，按字节切行安全）。
  * `--raw` 模式保持**纯字节透传**（不切行），保证逐字节保真。
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time

# 独立业务行：`[HH:MM:SS] * 内容`
BIZ_RE = re.compile(r"^\[\d{2}:\d{2}:\d{2}\]\s\*\s?")
# DELTA 包装行：`[HH:MM:SS(.mmm)][PANEL-DELTA] DELTA: <面板文本的一行>`
DELTA_RE = re.compile(r"^\[\d{2}:\d{2}:\d{2}(?:\.\d+)?\]\[PANEL-DELTA\]\s*DELTA:\s?(.*)$")
EXIT_RE = re.compile(r"ExitCode:\s*(-?\d+)")
END_PHRASE = "被调试易程序运行完毕"


def decode(b: bytes) -> str:
    return b.decode("gbk", errors="replace")


def _w_text(s: str) -> None:
    """文本模式统一出口：显式 UTF-8 字节（不受控制台代码页影响）。"""
    sys.stdout.buffer.write(s.encode("utf-8", errors="replace"))
    sys.stdout.buffer.flush()


def read_bytes(p: str) -> bytes:
    if not os.path.isfile(p):
        print("找不到日志文件: %s" % p, file=sys.stderr)
        raise SystemExit(2)
    with open(p, "rb") as f:
        return f.read()


def biz_from_line(ln: str):
    """单行 → 业务行（无则 None）。独立行与 DELTA 包装行内嵌的块首行都算。"""
    m = DELTA_RE.match(ln)
    if m:
        payload = m.group(1)
        return payload if BIZ_RE.match(payload) else None
    return ln if BIZ_RE.match(ln) else None


def business_lines(text: str) -> list:
    out = []
    for ln in text.splitlines():
        b = biz_from_line(ln)
        if b is not None:
            out.append(b)
    return out


def snapshot(a, raw: bytes, text: str) -> str:
    ms = EXIT_RE.findall(text)
    return json.dumps({
        "file": os.path.abspath(a.log),
        "bytes": len(raw),
        "lines": len(text.splitlines()),
        "businessLines": business_lines(text),
        "targetExitCode": int(ms[-1]) if ms else None,
        "ended": END_PHRASE in text,
    }, ensure_ascii=False, indent=2)


def main():
    ap = argparse.ArgumentParser(
        description="读调试面板日志（GBK → UTF-8），支持增量跟随",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", help="elaunch.py --log 落盘的日志路径")
    ap.add_argument("--raw", action="store_true", help="原样输出原始字节（不转码）")
    ap.add_argument("--business", action="store_true",
                    help="只保留被调试程序自己输出的行（含 DELTA 包装行内嵌的块首行）")
    ap.add_argument("--follow", action="store_true", help="dump 后增量跟随追加内容")
    ap.add_argument("--idle", type=float, default=0.0, help="跟随时空闲多久停（0=不停）")
    ap.add_argument("--timeout", type=float, default=0.0, help="跟随总时长上限（0=不限）")
    ap.add_argument("--json", action="store_true", dest="as_json", help="打印结构化 JSON")
    a = ap.parse_args()

    raw = read_bytes(a.log)
    text = decode(raw)

    if not a.follow:
        if a.as_json:
            _w_text(snapshot(a, raw, text) + "\n")
        elif a.business:
            biz = business_lines(text)
            _w_text("\n".join(biz) + ("\n" if biz else ""))
        elif a.raw:
            sys.stdout.buffer.write(raw)
            sys.stdout.buffer.flush()
        else:
            _w_text(text)
        return 0

    # ---- 增量跟随 ----
    if a.as_json:
        _w_text(snapshot(a, raw, text) + "\n")
    elif a.business:
        biz = business_lines(text)
        if biz:
            _w_text("\n".join(biz) + "\n")
    elif a.raw:
        sys.stdout.buffer.write(raw)
        sys.stdout.buffer.flush()
    else:
        _w_text(text)

    pos = len(raw)
    carry = b""                 # 未成行的字节余量（跨块保持，防半行/半字符）
    t0 = time.time()
    last = time.time()
    try:
        while True:
            time.sleep(0.2)
            try:
                with open(a.log, "rb") as f:
                    f.seek(pos)
                    chunk = f.read()
                    pos = f.tell()
            except OSError:
                break           # 日志文件没了（被清理/改名）→ 停止跟随
            if chunk:
                last = time.time()
                if a.raw:
                    sys.stdout.buffer.write(chunk)      # raw：纯透传，不切行
                    sys.stdout.buffer.flush()
                    continue
                parts = (carry + chunk).split(b"\n")
                carry = parts.pop()                     # 最后一段可能是半行，留到下一块
                lines = [decode(p).rstrip("\r") for p in parts]
                if not lines:
                    continue
                if a.business:
                    biz = [b for b in (biz_from_line(ln) for ln in lines)
                           if b is not None]
                    if biz:
                        _w_text("\n".join(biz) + "\n")
                else:
                    _w_text("\n".join(lines) + "\n")
            elif a.idle and (time.time() - last) >= a.idle:
                break
            if a.timeout and (time.time() - t0) >= a.timeout:
                break
    except KeyboardInterrupt:
        pass                    # Ctrl-C 是 --follow 的正常结束方式，不是错误
    return 0


if __name__ == "__main__":
    sys.exit(main())
