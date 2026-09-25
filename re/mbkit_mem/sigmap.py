#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""解析 dll.e.txt 里全部指向 MbKit.dll 的 .DLL命令，算出「尾跳包装子程序」需要的全部信息。

产出 JSON（re/mbkit_mem/out/sigmap.json）：
  · 每条声明：名字 / 返回类型 / 导出名 / 参数列表（名字, 类型, 修饰符）
  · 帧布局：每个参数在 [ebp+off] 里的偏移与占几个槽（依据实测规则，见下）
  · 池槽号：每条函数一个唯一槽（池基址 0x50000000，4 字节一个槽）
  · 机器码：push 序列 + call [槽] + 收尾 + ret N（浮点返回额外加搬运）
  · 风险标记：需要人工确认的签名（含未知尺寸类型 / 数组 / 结构体）

实测依据（re/mbkit_mem/probe_call.py 的产物 out/ANSWER-Q.txt）：
  整数型/字节型/逻辑型/小数型 → 值本身，1 槽
  长整数型/双精度小数型      → 值本身，2 槽（低 dword 在低偏移）
  文本型                    → 指向「存 char* 的槽」的指针，需解引用一次
  字节集                    → 描述符指针；描述符 [0]=引用计数 [1]=长度 [2]=数据指针（+8）
  参考/传址/数组             → 槽内容本身就是目标地址，直接 push
  返回：4 字节走 eax；8 字节（长整数型/双精度）走 edx:eax；**浮点必须从 st(0) 搬到 edx:eax**
"""
import collections
import io
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
TXT = os.path.join(ROOT, "re", "mb", "text_kit")
OUT = os.path.join(HERE, "out")
POOL = 0x50000000

sys.stdout.reconfigure(encoding="utf-8")

# ---------------------------------------------------------------- 尺寸表
SIZE_OF = {
    "整数型": 4, "字节型": 4, "逻辑型": 4, "小数型": 4, "子程序指针型": 4, "子程序指针": 4,
    "文本型": 4, "字节集": 4, "日期时间型": 4, "窗口组件": 4, "通用型": 4,
    "长整数型": 8, "双精度小数型": 8,
    "短整数型": 2,
}
# 需要解引用一次才拿到「值」的类型（帧里存的是指向值单元的指针）
DEREF1 = {"文本型"}
# 需要解引用再 +8（描述符 → 数据指针）
DEREF8 = {"字节集"}


def read_bom(p):
    return io.open(p, encoding="utf-8-sig").read().replace("\r\n", "\n")


def parse_structs(path):
    """从 struct.e.txt 算出每个 _结构XXXX 的字节大小（成员递归求和）。
    成员行例：    .成员 名, 类型, 公开, "数组长度"
    数组长度是半角引号包的数字文本 ⇒ 类型尺寸 × 长度。"""
    sizes = {}
    if not os.path.exists(path):
        return sizes
    cur = None
    for ln in read_bom(path).split("\n"):
        s = ln.strip()
        m = re.match(r"^\.数据类型\s+(\S+?)(?:,|$)", s)
        if m:
            cur = m.group(1)
            sizes.setdefault(cur, 0)
            continue
        m = re.match(r"^\.成员\s+(.+)$", s)
        if m and cur:
            parts = [x.strip() for x in m.group(1).split(",")]
            if len(parts) < 2:
                continue
            typ = parts[1]
            cnt = 1
            if len(parts) >= 4 and re.fullmatch(r'"?\d+"?', parts[3] or ""):
                cnt = int(parts[3].strip('"'))
            sizes[cur] = sizes.get(cur, 0) + None if False else sizes.get(cur, 0)
            sizes[cur] += (SIZE_OF.get(typ) or sizes.get(typ) or 0) * cnt
    return sizes


def parse_dll(path):
    """返回 [(name, ret, lib, entry, [(pname, ptype, pflags)]), ...]"""
    out = []
    cur = None
    for ln in read_bom(path).split("\n"):
        s = ln.rstrip()
        m = re.match(r'^\.DLL命令\s+(\S+),\s*([^,]*),\s*"([^"]*)",\s*"([^"]*)"\s*$', s)
        if m:
            cur = {"name": m.group(1), "ret": m.group(2).strip(),
                   "lib": m.group(3), "entry": m.group(4), "params": []}
            out.append(cur)
            continue
        m = re.match(r"^ {4}\.参数\s+(\S+),\s*([^,]+)\s*(?:,\s*(.*))?$", s)
        if m and cur is not None:
            flags = (m.group(3) or "").strip()
            cur["params"].append((m.group(1), m.group(2).strip(), flags))
    return out


def size_of(typ, structs, flags=""):
    """★ 实测修正（re/mbkit_mem/calib.py 的 93/95 命中结果）：
       结构体参数在子程序帧里只占 **4 字节**（按地址传，不是按值展开成员）；
       `数组` 参数同样只占 4 字节（地址）。其余类型按 SIZE_OF。"""
    if typ.startswith("_结构") or (typ in structs):
        return 4
    if flags == "数组":
        return 4
    if typ in SIZE_OF:
        return SIZE_OF[typ]
    return None


def layout(decl, structs):
    """给出每个参数的 [ebp+off] 与槽数，以及总字节数 N。返回 (params, N, risks)"""
    off = 8          # [ebp]=old ebp, [ebp+4]=返回地址, 参数从 +8 起
    out = []
    risks = []
    for pname, ptype, pflags in decl["params"]:
        sz = size_of(ptype, structs, pflags)
        if sz is None:
            risks.append("未知尺寸类型 %s（%s）" % (ptype, pname))
            return None, None, risks
        if ptype.startswith("_结构") and not structs.get(ptype):
            risks.append("结构体 %s 无定义" % ptype)
        out.append({"name": pname, "type": ptype, "flags": pflags,
                    "off": off, "size": sz, "slots": sz // 4 if sz % 4 == 0 else None,
                    "byref": pflags in ("参考", "传址", "数组") or ptype.startswith("_结构") or ptype in structs,
                    "deref1": ptype in DEREF1, "deref8": ptype in DEREF8})
        off += sz
    return out, off - 8, risks


def pushes_for(param, imm):
    """生成把该参数「按 cdecl 顺序需要的形式」推上栈的指令（单参数，dword 从上位到下位）。"""
    off, typ = param["off"], param["type"]
    if param["deref1"]:
        # mov eax,[ebp+off] ; push dword ptr [eax]
        return ["8B 45 %02X" % off, "8B 00", "50"]
    if param["deref8"]:
        return ["8B 45 %02X" % off, "8B 00", "83 C0 08", "50"]
    out = []
    for k in range(param["size"] // 4 - 1, -1, -1):     # 高位 dword 先压
        out.append("FF 75 %02X" % (off + 4 * k))
    return out


def machine_code(decl, params, nbytes, slot, float_tmp):
    """push 全部参数（反序）→ call [槽] → 浮点搬运 → mov esp,ebp; pop ebp; ret N"""
    code = []
    for p in reversed(params):
        code += pushes_for(p, None)
    code += ["FF 15"] + ["%02X" % ((POOL + 4 * slot) >> (8 * i) & 0xFF) for i in range(4)]
    ret = decl["ret"]
    if ret == "双精度小数型":
        code += ["DD 1D"] + ["%02X" % ((float_tmp) >> (8 * i) & 0xFF) for i in range(4)]
        code += ["A1"] + ["%02X" % ((float_tmp) >> (8 * i) & 0xFF) for i in range(4)]
        code += ["8B 15"] + ["%02X" % ((float_tmp + 4) >> (8 * i) & 0xFF) for i in range(4)]
    elif ret == "小数型":
        code += ["D9 1D"] + ["%02X" % ((float_tmp) >> (8 * i) & 0xFF) for i in range(4)]
        code += ["A1"] + ["%02X" % ((float_tmp) >> (8 * i) & 0xFF) for i in range(4)]
    code += ["8B E5", "5D", "C2", "%02X" % (nbytes & 0xFF), "%02X" % ((nbytes >> 8) & 0xFF)]
    flat = []
    for x in code:
        flat += x.split()
    return [int(b, 16) for b in flat]


def main():
    structs = parse_structs(os.path.join(TXT, "struct.e.txt"))
    decls = parse_dll(os.path.join(TXT, "dll.e.txt"))
    mb = [d for d in decls if "MbKit" in d["lib"]]
    other = [d for d in decls if "MbKit" not in d["lib"]]
    print("总声明 %d（MbKit %d / 其它 %d）" % (len(decls), len(mb), len(other)))
    unknown_structs = sorted({d["params"][i][1] for d in decls for i in range(len(d["params"]))
                              if d["params"][i][1].startswith("_结构")
                              and d["params"][i][1] not in structs})
    print("结构体定义 %d 个；声明里出现但 struct.e.txt 里没有的：%s"
          % (len(structs), unknown_structs or "无"))

    slot = 0
    recs, risks_all = [], collections.Counter()
    for d in decls:
        params, nbytes, risks = layout(d, structs)
        if params is None:
            risks_all.update(risks)
            recs.append({"name": d["name"], "entry": d["entry"], "ret": d["ret"],
                         "params": d["params"], "slot": None, "machine": None,
                         "risks": risks})
            continue
        if d["ret"] not in ("", "空", "整数型", "字节型", "长整数型", "小数型", "双精度小数型", "逻辑型"):
            risks.append("未验证的返回类型 %s" % d["ret"])
        for p in params:
            if p["flags"] not in ("", "参考", "传址", "数组", "可空"):
                risks.append("未验证的参数修饰符 %r（%s）" % (p["flags"], p["name"]))
            if p["size"] % 4:
                risks.append("尺寸非 4 倍数：%s %s=%d" % (p["type"], p["name"], p["size"]))
        ftmp = POOL + 0x100 + 8 * 4      # 浮点搬运临时区（每函数 8 字节，从槽 64 起）
        recs.append({
            "name": d["name"], "entry": d["entry"], "lib": d["lib"], "ret": d["ret"],
            "params": [{"name": p["name"], "type": p["type"], "flags": p["flags"],
                        "off": p["off"], "size": p["size"]} for p in params],
            "nbytes": nbytes, "slot": slot,
            "machine": machine_code(d, params, nbytes, slot, ftmp),
            "risks": risks,
        })
        if d["ret"] in ("小数型", "双精度小数型"):
            slot += 1
            ftmp2 = POOL + 0x100 + 8 * 4
        slot += 1

    ok = [r for r in recs if r["slot"] is not None]
    print("可生成包装 %d 条；有风险 %d 条" % (len(ok), len(recs) - len(ok)))
    print("槽占用 %d / 1024" % slot)
    print("\n--- 帧字节数分布 ---")
    c = collections.Counter(r["nbytes"] for r in ok)
    for k in sorted(c):
        print("   N=%-3d 槽=%-2d  %d 条" % (k, k // 4, c[k]))
    print("\n--- 返回类型分布 ---")
    for k, v in collections.Counter(r["ret"] or "空" for r in recs).most_common():
        print("   %-14s %d" % (k, v))
    print("\n--- 参数类型分布 ---")
    tc = collections.Counter(p["type"] for r in recs for p in r["params"] if isinstance(p, dict))
    for k, v in tc.most_common():
        print("   %-16s %d" % (k, v))
    print("\n--- 参数修饰符分布 ---")
    for k, v in collections.Counter(p["flags"] for r in recs for p in r["params"] if isinstance(p, dict)).most_common():
        print("   %-10r %d" % (k, v))
    print("\n--- 有风险的声明 ---")
    for r in recs:
        if r["risks"] and r["slot"] is not None:
            print("   %-46s ret=%-12s %s" % (r["name"][:46], r["ret"] or "空", r["risks"]))
    if risks_all:
        print("\n--- 无法算布局的声明 ---")
        for k, v in risks_all.most_common():
            print("   %s  ×%d" % (k, v))

    os.makedirs(OUT, exist_ok=True)
    with io.open(os.path.join(OUT, "sigmap.json"), "w", encoding="utf-8") as f:
        json.dump({"pool": POOL, "structs": structs, "recs": recs}, f,
                  ensure_ascii=False, indent=1)
    print("\n写出 %s" % os.path.join(OUT, "sigmap.json"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
