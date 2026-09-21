#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""扫出技能/文档里残留的「本机专有信息」（路径、统计数、模块版本号、个人信息）。

**为什么要有这个检查**：技能是要发给别人用的。别人机器上不会有
`D:\\ides\\e`、`C:\\msys64` 这些路径，也不知道你这台机器装了多少支持库 ——
写进技能只会误导对方，甚至让对方以为技能坏了。

`verify-release.py` 会 import 本模块复用同一套规则，所以规则只有一份。

用法::

    python tools/scan-machineinfo.py            # 扫发布区技能 + 安装器 + 使用说明
    python tools/scan-machineinfo.py --all      # 连工程自己的 README / 脚本一起扫
    python tools/scan-machineinfo.py <路径> …    # 扫指定文件或目录
"""
from __future__ import annotations

import argparse
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KIT = os.path.join(ROOT, 'Releases', 'Elang-AiTools')

# 只列**确定是本机专有**的东西；`D:\\out`、`D:/proj`、`C:\\path\\to` 这类
# 是通用示例，故意不拦（否则全是噪音，没人会认真看告警）。
PATTERNS = [
    ('本机绝对路径', re.compile(
        r'[A-Za-z]:[\\/](?:Users|msys64|Tools|ides|DevelopmentFolder'
        r'|Program Files|Windows|桌面)[\\/]')),
    ('已废弃的 KIT 占位符', re.compile(r'<KIT>')),
    ('已废弃的旧目录名', re.compile(r'(?<![\w-])elang-ai/')),
    ('个人信息', re.compile(r'MadeSpark')),
    ('写死的支持库统计', re.compile(
        r'77\s*(?:个|\+)?\s*(?:库|支持库)|5951|本机(?:实测|装了|已有|原本|已修好|就有一堆)')),
    ('写死的模块版本', re.compile(r'精易模块\s*v?\d+\.\d+')),
]

SCAN_EXT = {'.md', '.py', '.sh', '.cmd', '.txt'}

# 这两个文件的**本职工作就是包含这些模式**（一个是模式表，一个是失效路径正则），
# 扫到自己必然误报。--all 扫开发脚本时排除掉。
SELF_EXCLUDE = {'scan-machineinfo.py', 'verify-release.py'}


def iter_files(paths, exclude_self=True):
    """展开成文件列表（目录递归；跳过 __pycache__ 与非文本）。"""
    for p in paths:
        if os.path.isfile(p):
            yield p
        elif os.path.isdir(p):
            for dp, dn, fn in os.walk(p):
                dn[:] = [d for d in dn if d != '__pycache__']
                for f in sorted(fn):
                    if exclude_self and f in SELF_EXCLUDE:
                        continue
                    if os.path.splitext(f)[1].lower() in SCAN_EXT:
                        yield os.path.join(dp, f)


def scan(paths):
    """返回 [(文件, 行号, 标签, 该行内容)]，按文件与行号排序。"""
    hits = []
    for f in iter_files(paths):
        try:
            lines = io.open(f, encoding='utf-8').read().splitlines()
        except Exception:                                   # noqa: BLE001
            continue
        for i, line in enumerate(lines):
            for label, rx in PATTERNS:
                if rx.search(line):
                    hits.append((f, i + 1, label, line.strip()[:104]))
                    break
    return hits


def default_targets(wide=False):
    t = [os.path.join(KIT, 'skills'),
         os.path.join(KIT, '使用说明.md'),
         os.path.join(KIT, 'install-skills.py'),
         os.path.join(KIT, 'install-skills.sh'),
         os.path.join(KIT, '安装技能.cmd')]
    if wide:
        t += [os.path.join(ROOT, 'README.md'),
              os.path.join(ROOT, 'docs'),
              os.path.join(ROOT, 'tools'),
              os.path.join(ROOT, 'src')]
    return t


def main():
    ap = argparse.ArgumentParser(
        description='扫技能/文档里残留的本机专有信息（路径、统计数、版本号、个人信息）')
    ap.add_argument('paths', nargs='*', help='要扫的文件或目录（默认扫发布区）')
    ap.add_argument('--all', action='store_true', help='连 README / 开发脚本一起扫')
    a = ap.parse_args()

    paths = a.paths or default_targets(wide=a.all)
    hits = scan(paths)

    cur = None
    for f, line, label, text in hits:
        if f != cur:
            cur = f
            print(f'### {os.path.relpath(f, ROOT)}')
        print(f'  {line:4d} [{label}] {text}')

    print()
    if hits:
        print(f'合计 {len(hits)} 处本机专有信息。')
        if not a.paths and a.all:
            print('注意：--all 会连带扫工程侧的 README / 开发脚本 —— 那些**描述本机环境是有意的**，')
            print('      不必清零。技能 / 安装器 / 使用说明（不加 --all 的默认范围）必须为 0。')
        return 1
    print('干净：没有扫到本机专有信息。')
    return 0


if __name__ == '__main__':
    sys.exit(main())
