#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""标定实验：把「每个参数在帧里占多少字节」从假设变成实测。

原理（不需要反汇编器）：
  1. 按每个「唯一签名」生成一个同参数列表的 子程序，体内只放 4 字节唯一标记
     `B0 <lo> B4 <hi> 90 90`（mov al,lo / mov ah,hi / nop / nop），然后 `返回 (0)`。
  2. 静态编译成 exe。易语言给每个子程序生成的收尾固定是
         `8B E5 5D C2 <N:2字节>`  = mov esp,ebp ; pop ebp ; ret N
  3. 在 exe 字节流里搜标记，紧跟在后面的 `8B E5 5D C2` 里的 N 就是该签名的
     **参数总字节数**（重定位/对齐都不会改变它）。

跑法：
    python calib.py gen     # 生成标定工程 re/mbkit_mem/calib_proj
    python calib.py scan <exe 路径>
"""
import io
import json
import os
import re
import shutil
import sys

sys.stdout.reconfigure(encoding="utf-8")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SCAFFOLD = os.path.join(ROOT, "re", "opt", "e2e", "proj")
TXT = os.path.join(ROOT, "re", "mb", "text_kit")
PROJ = os.path.join(HERE, "calib_proj")
BOM = "\ufeff"

# 声明里出现过的结构体（含嵌套），从 struct.e.txt 原样搬进标定工程
STRUCT_TYPES = [
    "_结构41010861", "_结构41010862", "_结构41010A13", "_结构41010A14",
    "_结构41010A20", "_结构41010B08", "_结构41010C40", "_结构41010C45",
    "_结构41010C5C", "_结构41010C83", "_结构41010934", "_结构41011156",
    "_结构41010D27",
]

# 声明里出现的非标准类型名 → 易语言里的正式类型名
TYPE_ALIAS = {"子程序指针": "子程序指针型"}


def w(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write((BOM + text.replace("\n", "\r\n")).encode("utf-8"))


def read_bom(p):
    return io.open(p, encoding="utf-8-sig").read().replace("\r\n", "\n")


def struct_blocks(path):
    """.数据类型 X ... 到下一个 .数据类型 之前，原样取出（保序，含嵌套定义）。"""
    blocks, cur, name = {}, [], None
    for ln in read_bom(path).split("\n"):
        if ln.startswith(".数据类型 "):
            if name:
                blocks[name] = "\n".join(cur)
            name = ln.split()[1].split(",")[0]
            cur = [ln]
        elif name is not None:
            cur.append(ln)
    if name:
        blocks[name] = "\n".join(cur)
    return blocks


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "gen"

    sigmap = json.load(io.open(os.path.join(HERE, "out", "sigmap.json"), encoding="utf-8"))
    recs = [r for r in sigmap["recs"] if r.get("slot") is not None]

    # 唯一签名 = (返回类型, ((参数类型, 修饰符) ...))；同名同签名只测一次
    seen, sigs = {}, []
    for r in recs:
        key = (r["ret"], tuple((p["type"], p["flags"]) for p in r["params"]))
        if key in seen:
            continue
        seen[key] = len(sigs)
        sigs.append({"key": key, "ret": r["ret"], "params": r["params"], "谁": r["name"]})
    print("唯一签名 %d 个（覆盖 %d 条声明）" % (len(sigs), len(recs)))
    assert len(sigs) <= 0x10000

    if mode == "gen":
        # ---- 只补必要的：把 struct.e.txt 的 .数据类型 搬到「常量.e.txt」试放不下，
        #      这里直接生成一个独立的 结构.e.txt，看易语言是否按文件名识别 ----
        if os.path.isdir(PROJ):
            shutil.rmtree(PROJ)
        shutil.copytree(SCAFFOLD, PROJ)
        res = os.path.join(PROJ, "资源")
        if os.path.isdir(res):
            shutil.rmtree(res)

        blocks = struct_blocks(os.path.join(TXT, "struct.e.txt"))
        wanted, stack = [], list(STRUCT_TYPES)
        while stack:
            t = stack.pop()
            if t in wanted or t not in blocks:
                continue
            wanted.append(t)
            for m in re.finditer(r"\.成员\s+\S+,\s*(\S+)", blocks[t]):
                if m.group(1).startswith("_结构"):
                    stack.append(m.group(1))
        # 按 struct.e.txt 原顺序输出
        order = [k for k in blocks if k in wanted]
        print("需要结构体 %d 个：%s" % (len(order), ", ".join(order)))
        w(os.path.join(PROJ, "结构.e.txt"), "\n\n".join(blocks[k].rstrip() for k in order) + "\n")

        M = ['.版本 2', '', '.程序集 程序集1', '',
             '.子程序 _启动子程序, 整数型']
        # 每个签名一组「按类型正确的假实参」局部变量 —— 必须真的**调用**这些桩，
        # 否则易语言会把没人引用的子程序整体优化掉（实测：exe 里一个标记都搜不到）。
        local_names = {}
        for i, s in enumerate(sigs):
            for j, p in enumerate(s["params"]):
                nm = "_L%03d_%02d" % (i, j)
                typ = TYPE_ALIAS.get(p["type"], p["type"])
                arr = ', , "4"' if p["flags"] == "数组" else ""
                M.append("    .局部变量 %s, %s%s" % (nm, typ, arr))
                local_names[(i, j)] = nm
        M.append("")
        for i, s in enumerate(sigs):
            args = ", ".join(local_names[(i, j)] for j in range(len(s["params"])))
            M.append("    校%04d (%s)" % (i, args))
        M += ['    返回 (0)', '']
        for i, s in enumerate(sigs):
            s["marker"] = [0xB0, i & 0xFF, 0xB4, (i >> 8) & 0xFF, 0x90, 0x90, 0x90, 0x90]
            M.append(".子程序 校%04d, 整数型, 公开" % i)
            for p in s["params"]:
                M.append("    .参数 %s, %s%s" % (p["name"], TYPE_ALIAS.get(p["type"], p["type"]),
                                                (", " + p["flags"]) if p["flags"] else ""))
            if s["params"]:
                M.append("")
            M.append("    置入代码 ({%s})" % ", ".join(str(b) for b in s["marker"]))
            M.append("    返回 (0)")
            M.append("")
        w(os.path.join(PROJ, "代码", "程序集1.static.e.txt"), "\n".join(M))
        w(os.path.join(PROJ, "代码", "排序.list.txt"), "程序集1\n")
        w(os.path.join(PROJ, "DLL.e.txt"), ".版本 2\n")
        json.dump({"sigs": [{"i": i, "ret": s["ret"], "marker": s["marker"],
                             "params": s["params"], "n吧": None} for i, s in enumerate(sigs)]},
                  io.open(os.path.join(HERE, "out", "calib_sigs.json"), "w", encoding="utf-8"),
                  ensure_ascii=False, indent=1)
        print("写好 %s（%d 个子程序）" % (PROJ, len(sigs)))
        return 0

    if mode == "scan":
        exe = sys.argv[2]
        data = open(exe, "rb").read()
        meta = json.load(io.open(os.path.join(HERE, "out", "calib_sigs.json"), encoding="utf-8"))
        got = {}
        for s in meta["sigs"]:
            pat = bytes(s["marker"])
            pos = data.find(pat)
            if pos < 0:
                got[s["i"]] = None
                continue
            tail = data[pos + len(pat): pos + len(pat) + 40]
            m = re.search(rb"\x8b\xe5\x5d\xc2(..)", tail[:16], re.S)
            got[s["i"]] = (m.group(1)[0] | (m.group(1)[1] << 8)) if m else None
        hit = sum(1 for v in got.values() if v is not None)
        print("命中 %d / %d 个签名" % (hit, len(meta["sigs"])))
        # 反向校验：算出来的 N 与实测 N 是否一致
        sys.path.insert(0, HERE)
        import sigmap as SM
        structs = SM.parse_structs(os.path.join(TXT, "struct.e.txt"))
        bad = []
        table = {}
        for s in meta["sigs"]:
            pre = 0
            for p in s["params"]:
                sz = SM.size_of(p["type"], structs, p["flags"])
                if sz is None:
                    pre = None
                    break
                pre += sz
            table[s["i"]] = (got[s["i"]], pre)
            if got[s["i"]] is not None and pre is not None and got[s["i"]] != pre:
                bad.append((s["i"], s["ret"], got[s["i"]], pre,
                            ", ".join("%s:%s" % (p["type"], p["flags"] or "-") for p in s["params"])[:90]))
        if bad:
            print("\n✗ 计算值与实测值不一致 %d 条：" % len(bad))
            for b in bad:
                print("   校%04d ret=%-12s 实测N=%-4s 计算N=%-4s  %s" % b)
        else:
            print("\n✓ 全部一致：尺寸表（含结构体）与易语言实际帧布局吻合")
        miss = [(s["i"], s["ret"]) for s in meta["sigs"] if got[s["i"]] is None]
        if miss:
            print("✗ 未命中：%s" % miss[:20])
        with io.open(os.path.join(HERE, "out", "calib_N.json"), "w", encoding="utf-8") as f:
            json.dump({str(k): v for k, v in table.items()}, f, ensure_ascii=False, indent=1)
        return 0

    print("用法: calib.py gen | calib.py scan <exe>")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
