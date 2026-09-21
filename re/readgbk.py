#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Dump a file (default GBK) to stdout with line numbers, read-only."""
import sys
import io

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")


def dump(path, enc="gbk", start=1, end=None, only=None):
    with open(path, "rb") as f:
        raw = f.read()
    try:
        text = raw.decode(enc, errors="replace")
    except Exception as e:  # noqa: BLE001
        text = raw.decode("latin-1", errors="replace")
        print("DECODE-FALLBACK:", e)
    lines = text.splitlines()
    n = len(lines)
    print("### FILE:", path, "lines=", n, "enc=", enc)
    for i, ln in enumerate(lines, 1):
        if i < start:
            continue
        if end is not None and i > end:
            break
        print(f"{i}\t{ln}")


if __name__ == "__main__":
    p = sys.argv[1]
    enc = sys.argv[2] if len(sys.argv) > 2 else "gbk"
    start = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    end = int(sys.argv[4]) if len(sys.argv) > 4 else None
    dump(p, enc, start, end)
