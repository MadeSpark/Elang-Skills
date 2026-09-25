# -*- coding: utf-8 -*-
"""
回滚保险 —— 快照「MbKit 内存加载」链路的全部关键资产，并支持一键还原。

用法：
    python mkrollback.py                           新建快照
    python mkrollback.py --list                    列出全部快照
    python mkrollback.py --verify <快照>            校验快照完整性
    python mkrollback.py --restore <快照>           还原（默认只打印计划）
    python mkrollback.py --restore <快照> --yes     确认还原

设计原则：
  * 只做「复制」，**绝不删除或改写任何源文件**。
  * 每个文件记录 sha1；还原前校验快照自身完整性。
  * 还原时先把「将被覆盖的当前文件」另存到 <快照>/_before_restore/，
    即使还原方向搞反也能再反转回来。
  * 快照目录：<repo>/re/_rollback/<时间戳>/
"""

import os
import sys
import json
import time
import shutil
import hashlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))      # 测试\
RB   = os.path.join(ROOT, "re", "_rollback")

# ---------------------------------------------------------------- 清单
# ★ 用户原始文件永远只备份、不修改。改动一律在副本上做。
FILES = [
    # ---- 用户原始资产（只读保护）----
    "miniblink.e",
    # ---- 映射器源码与产物 ----
    "re/mbkit_mem/mapkit.c",
    "re/mbkit_mem/mapkit.o",
    "re/mbkit_mem/mapkit_text.bin",
    # ---- 生成器 ----
    "re/mbkit_mem/memgen.py",
    "re/mbkit_mem/gen_demo.py",
    # ---- 探针与诊断脚本 ----
    "re/mbkit_mem/probe_prol.c",
    "re/mbkit_mem/flagmat.sh",
    "re/mbkit_mem/flagmat2.sh",
    "re/mbkit_mem/chk1.py",
    "re/mbkit_mem/static_lint.py",
    "re/mbkit_mem/scan_stages.py",
    "re/mbkit_mem/run_demo.py",
    # ---- 上一版改造产物 ----
    "out_mbkit/miniblink.e",
    "out_mbkit/miniblink.ec",
    "out_mbkit/使用说明.md",
]

DIRS = [
    "re/mbkit_mem/demo_proj",
]

# 永不进快照的东西（体积大 / 可再生 / 纯垃圾）
SKIP_DIRS  = {"_rollback", "__pycache__", ".git", "node_modules"}
SKIP_NAMES = {"raw.bin", "out.gbk.txt", "out.utf-8.txt"}


def sha1_file(path, bufsize=1 << 20):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        while True:
            b = f.read(bufsize)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def walk_files(top):
    """列出目录下所有文件（相对 top 的路径）。"""
    out = []
    for dp, dns, fns in os.walk(top):
        dns[:] = [d for d in dns if d not in SKIP_DIRS]
        for fn in fns:
            if fn in SKIP_NAMES:
                continue
            out.append(os.path.relpath(os.path.join(dp, fn), top))
    return sorted(out)


def collect():
    """返回 [(相对仓库根的路径, 绝对路径, 是否目录), ...]"""
    items = []
    for rel in FILES:
        ap = os.path.join(ROOT, rel.replace("/", os.sep))
        if os.path.isfile(ap):
            items.append((rel, ap, False))
        else:
            print("  [跳过] 不存在: %s" % rel)
    for rel in DIRS:
        ap = os.path.join(ROOT, rel.replace("/", os.sep))
        if os.path.isdir(ap):
            items.append((rel, ap, True))
        else:
            print("  [跳过] 不存在: %s" % rel)
    return items


def do_backup():
    items = collect()
    if not items:
        print("没有可备份的文件。"); return 1
    ts  = time.strftime("%Y%m%d-%H%M%S")
    dst = os.path.join(RB, ts)
    os.makedirs(dst, exist_ok=True)

    manifest = {"created": ts, "root": ROOT, "entries": []}
    total = 0
    for rel, ap, isdir in items:
        tgt = os.path.join(dst, "files", rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(tgt), exist_ok=True)
        if isdir:
            shutil.copytree(ap, tgt, dirs_exist_ok=True)
            for sub in walk_files(tgt):
                fp = os.path.join(tgt, sub)
                sz = os.path.getsize(fp)
                total += sz
                manifest["entries"].append({
                    "path": rel + "/" + sub.replace(os.sep, "/"),
                    "sha1": sha1_file(fp), "size": sz, "dir": True})
            print("  [目录] %-40s -> %s" % (rel, tgt))
        else:
            shutil.copy2(ap, tgt)
            sz = os.path.getsize(tgt)
            total += sz
            manifest["entries"].append({
                "path": rel, "sha1": sha1_file(tgt),
                "size": sz, "dir": False})
            print("  [文件] %-40s %8d B  %s" % (rel, sz, sha1_file(tgt)[:12]))

    with open(os.path.join(dst, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)

    print()
    print("=" * 66)
    print("快照已建立：%s" % dst)
    print("  条目 %d 个，原始体积 %.2f MB" % (len(manifest["entries"]), total / 1048576.0))
    print("  清单  %s" % os.path.join(dst, "manifest.json"))
    print("=" * 66)
    print("还原： python mkrollback.py --restore %s --yes" % ts)
    return 0


def load_manifest(ts):
    p = os.path.join(RB, ts, "manifest.json")
    if not os.path.isfile(p):
        print("找不到快照清单：%s" % p); return None
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def do_list():
    if not os.path.isdir(RB):
        print("（还没有任何快照）"); return 0
    for name in sorted(os.listdir(RB)):
        mf = os.path.join(RB, name, "manifest.json")
        if os.path.isfile(mf):
            with open(mf, encoding="utf-8") as f:
                m = json.load(f)
            tot = sum(e["size"] for e in m["entries"])
            print("  %-18s  条目 %3d   %8.2f MB" % (name, len(m["entries"]), tot / 1048576.0))
    return 0


def do_verify(ts):
    m = load_manifest(ts)
    if not m:
        return 1
    bad = 0
    for e in m["entries"]:
        fp = os.path.join(RB, ts, "files", e["path"].replace("/", os.sep))
        if not os.path.isfile(fp):
            print("  [缺失] %s" % e["path"]); bad += 1; continue
        s = sha1_file(fp)
        if s != e["sha1"]:
            print("  [损坏] %s  %s != %s" % (e["path"], s[:12], e["sha1"][:12])); bad += 1
    print("校验完成：%d 项，%d 项异常。" % (len(m["entries"]), bad))
    return 0 if not bad else 1


def do_restore(ts, yes):
    m = load_manifest(ts)
    if not m:
        return 1
    # 1) 先校验快照自身
    if do_verify(ts) != 0:
        print("快照自身不完整，拒绝还原。"); return 1

    print()
    print("将把下列文件从快照复制回仓库：")
    for e in m["entries"]:
        tgt = os.path.join(ROOT, e["path"].replace("/", os.sep))
        flag = "覆盖" if os.path.exists(tgt) else "新建"
        print("  [%s] %s" % (flag, e["path"]))
    if not yes:
        print()
        print("这只是计划预览。确认无误后加 --yes 真正执行。")
        return 0

    # 2) 反向保险：把当前文件先另存
    before = os.path.join(RB, ts, "_before_restore",
                          time.strftime("%Y%m%d-%H%M%S"))
    n_before = 0
    for e in m["entries"]:
        tgt = os.path.join(ROOT, e["path"].replace("/", os.sep))
        if os.path.isfile(tgt):
            b = os.path.join(before, e["path"].replace("/", os.sep))
            os.makedirs(os.path.dirname(b), exist_ok=True)
            shutil.copy2(tgt, b)
            n_before += 1
    print()
    print("还原前已把 %d 个现存文件另存到：%s" % (n_before, before))

    # 3) 执行还原
    n = 0
    for e in m["entries"]:
        src = os.path.join(RB, ts, "files", e["path"].replace("/", os.sep))
        tgt = os.path.join(ROOT, e["path"].replace("/", os.sep))
        os.makedirs(os.path.dirname(tgt), exist_ok=True)
        shutil.copy2(src, tgt)
        n += 1
    print("已还原 %d 个文件。" % n)
    return 0


def main():
    args = sys.argv[1:]
    if not args:
        print("正在建立快照 ……")
        return do_backup()
    if args[0] == "--list":
        return do_list()
    if args[0] == "--verify" and len(args) >= 2:
        return do_verify(args[1])
    if args[0] == "--restore" and len(args) >= 2:
        return do_restore(args[1], "--yes" in args)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
