#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""对照「demo 内嵌的模块公开接口快照」vs「改造后模块自己的公开接口」。

用途：换 .ec 前先静态预判 —— 若公开接口出现「消失/改名」，demo 必崩；
多出来的一般无害。
"""
import sys
import re
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parent


def rd(p):
    b = Path(p).read_bytes()
    if b.startswith(b"\xef\xbb\xbf"):
        b = b[3:]
    return b.decode("utf-8", "replace").replace("\r\n", "\n")


def scan(path):
    """返回 (程序集公开性 dict, 每个程序集的子程序签名 dict, 常量, 结构体)"""
    sets = {}          # set name -> is_public
    subs = {}          # set name -> [(name, ret, params_sig, is_public)]
    cur = None
    cur_sub = None
    text = rd(path)
    for line in text.split("\n"):
        m = re.match(r"^\.程序集\s+([^,]+)\s*(?:,\s*([^,]*))?\s*(?:,\s*(.*))?$", line)
        if m:
            name = m.group(1).strip()
            pub = (m.group(3) or "").strip() == "公开"
            sets[name] = pub
            subs[name] = []
            cur, cur_sub = name, None
            continue
        m = re.match(r"^\.子程序\s+(.+)$", line)
        if m and cur:
            parts = [x.strip() for x in m.group(1).split(",")]
            nm = parts[0]
            ret = parts[1] if len(parts) > 1 else ""
            pub = "公开" in parts[2:]
            cur_sub = {"name": nm, "ret": ret, "pub": pub, "params": []}
            subs[cur].append(cur_sub)
            continue
        m = re.match(r"^\s*\.参数\s+(.+)$", line)
        if m and cur_sub is not None:
            parts = [x.strip() for x in m.group(1).split(",")]
            cur_sub["params"].append(tuple(parts[1:]))
    return sets, subs


def collect(d):
    """目录里所有 class/*.e.txt + const/struct 汇总"""
    d = Path(d)
    sets, subs = {}, {}
    for f in sorted((d / "class").glob("*.e.txt")):
        if f.name == "order.list.txt":
            continue
        s, u = scan(f)
        for k, v in s.items():
            sets.setdefault(k, v)
            subs.setdefault(k, []).extend(u.get(k, []))
    consts = set()
    cp = d / "const.e.txt"
    if cp.is_file():
        for line in rd(cp).split("\n"):
            if line.startswith(".常量"):
                consts.add(line.split()[1].rstrip(","))
    structs = set()
    sp = d / "struct.e.txt"
    if sp.is_file():
        for line in rd(sp).split("\n"):
            if line.startswith(".数据类型"):
                structs.add(line.split()[1].rstrip(","))
    return sets, subs, consts, structs


def sig(s):
    return "%s(%s) -> %s" % (s["name"], ",".join("|".join(p) for p in s["params"]), s["ret"])


def main():
    snap = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "../../.tmp/demo_txt/module/miniblink")
    new = sys.argv[2] if len(sys.argv) > 2 else str(ROOT / ".rt")

    for label, d in (("DEMO快照", snap), ("改造后模块", new)):
        s, u, c, st = collect(d)
        print("=== %s (%s) ===" % (label, d))
        for k in sorted(s):
            pubs = [x for x in u.get(k, []) if x["pub"]]
            print("  程序集 %-28s 公开=%s  子程序=%d(公开 %d)"
                  % (k, s[k], len(u.get(k, [])), len(pubs)))
        print("  常量 %d 个 / 结构体 %d 个" % (len(c), len(st)))
        print()

    s1, u1, c1, st1 = collect(snap)
    s2, u2, c2, st2 = collect(new)

    # 公开子程序总表
    def pubset(s, u):
        out = {}
        for k, lst in u.items():
            if not s.get(k):
                continue
            for x in lst:
                if x["pub"]:
                    out[x["name"]] = sig(x)
        return out

    p1, p2 = pubset(s1, u1), pubset(s2, u2)
    lost = sorted(set(p1) - set(p2))
    chg = sorted(n for n in (set(p1) & set(p2)) if p1[n] != p2[n])
    add = sorted(set(p2) - set(p1))
    print("★ 公开子程序：快照 %d / 新 %d" % (len(p1), len(p2)))
    print("  消失(致命) %d: %s" % (len(lost), lost[:40]))
    print("  签名变化(致命) %d: %s" % (len(chg), [(n, p1[n], p2[n]) for n in chg[:20]]))
    print("  新增(一般无害) %d: %s" % (len(add), add[:40]))
    print()
    print("★ 公开类：", sorted(k for k in s1 if s1[k]), "|", sorted(k for k in s2 if s2[k]))
    print("★ 常量差异：快照独有 %s / 新独有 %s" % (sorted(c1 - c2)[:20], sorted(c2 - c1)[:20]))
    print("★ 结构体差异：快照独有 %s / 新独有 %s" % (sorted(st1 - st2)[:20], sorted(st2 - st1)[:20]))


if __name__ == "__main__":
    main()
