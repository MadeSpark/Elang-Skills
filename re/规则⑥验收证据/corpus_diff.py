#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""旧/新 echeck.py 全量误报对比（规则⑥ 扩到显式尾的回归证据）。

用法（仓库根目录）：
    git show HEAD~1:tools/echeck.py > "$TEMP/old_echeck.py"
    PYTHONIOENCODING=utf-8 PYTHONUTF8=1 python "re/规则⑥验收证据/corpus_diff.py" "$TEMP/old_echeck.py" tools/echeck.py

做法：直接 import 两个脚本，用同一份语料逐文件跑 run_branch_checks()，比较 (相对路径, 报错文本) 集合。
只 old 有 = 新版漏报；只 new 有 = 新版新增误报（扩展规则时这两项都必须为 0，除非另有 IDE 实证）。
结果 JSON 写到本脚本同目录 corpus_diff.json。
"""

import importlib.util
import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
# fixtures/ 是负对照（故意写错），不参与「有没有新增误报」的扫描
SKIP = {".git", ".tmp", "node_modules", "__pycache__", ".local", "dist", "fixtures"}


def key_of(msg):
    """报错文本归一化：只比「位置 + 命中的规则措辞」，不比后半句解释。

    2026-10-27 那次改动改写了提示语（隐藏尾→…（流程线串线/崩坏）），
    提示语变化不是漏报/误报，不该污染检测口径的对比。"""
    return msg.split(" → ", 1)[0]


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    old_path, new_path = (os.path.abspath(p) for p in argv)
    old = load(old_path, "echeck_old_cmp")
    new = load(new_path, "echeck_new_cmp")

    files = []
    for root, dirs, names in os.walk(REPO):
        dirs[:] = [d for d in dirs if d not in SKIP]
        for n in names:
            if n.endswith(".e.txt"):
                files.append(os.path.join(root, n))
    files.sort()

    res_old, res_new, read_fail = {}, {}, []
    for f in files:
        rel = os.path.relpath(f, REPO)
        try:
            texts = new.read_texts([f])
        except Exception as exc:                      # 读取失败要显式留痕，不能静默跳过
            read_fail.append(rel + " READ-FAIL " + str(exc))
            continue
        errs_old, errs_new = [], []
        old.run_branch_checks(texts, errs_old)
        new.run_branch_checks(texts, errs_new)
        if errs_old:
            res_old[rel] = errs_old
        if errs_new:
            res_new[rel] = errs_new

    set_old = {(k, key_of(m)) for k, v in res_old.items() for m in v}
    set_new = {(k, key_of(m)) for k, v in res_new.items() for m in v}
    only_old = sorted(set_old - set_new)
    only_new = sorted(set_new - set_old)
    summary = {
        "files": len(files),
        "read_fail": read_fail,
        "old_total": len(set_old), "new_total": len(set_new),
        "old_files": len(res_old), "new_files": len(res_new),
        "only_old": len(only_old), "only_new": len(only_new),
    }
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    print("--- only_old（新版漏报，应为 0）")
    for path, msg in only_old[:20]:
        print(path, "::", msg[:200])
    print("--- only_new（新版新增命中，应为 0 或全部有 IDE 实证）")
    for path, msg in only_new[:40]:
        print(path, "::", msg[:220])

    with io.open(os.path.join(HERE, "corpus_diff.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump({"summary": summary,
                   "only_old": [list(x) for x in only_old],
                   "only_new": [list(x) for x in only_new]},
                  fh, ensure_ascii=False, indent=1)
    return 0 if not only_old else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
