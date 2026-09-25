#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""核对 MbKit.dll 的导出表与模块里的 252 条声明：
   ① 有没有「转发导出」（RVA 落在导出目录区间内的那种）—— 有的话手工映射必须额外处理；
   ② 模块里每条声明的入口名（去掉 @ 前缀）能不能在导出表里找到，并给出 RVA。
   产出 out/slots.json：每条声明的 槽号 / 声明名 / 入口名 / 导出名 / RVA。
"""
import io
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
DLL = os.path.join(ROOT, "src", "mbkit", "MbKit.dll")
TXT = os.path.join(ROOT, "re", "mb", "text_kit", "dll.e.txt")
OUT = os.path.join(HERE, "out")

sys.stdout.reconfigure(encoding="utf-8")


def rd(data, off, n):
    return struct.unpack_from("<" + "I" * n, data, off) if n > 1 else struct.unpack_from("<I", data, off)[0]


def exports(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    szopt = struct.unpack_from("<H", d, pe + 0x14)[0]
    opt = pe + 0x18
    image_base = struct.unpack_from("<I", d, opt + 0x1C)[0]
    ddir = opt + 0x60
    ed_rva, ed_size = struct.unpack_from("<II", d, ddir + 0 * 8)
    secs = []
    for i in range(nsec):
        sh = opt + szopt + i * 40
        name = d[sh:sh + 8].rstrip(b"\0").decode()
        vs, va, rs, ra = struct.unpack_from("<IIII", d, sh + 8)
        secs.append((name, va, vs, ra, rs))

    def rva2off(rva):
        for name, va, vs, ra, rs in secs:
            if va <= rva < va + max(vs, rs):
                return ra + (rva - va)
        return None

    eoff = rva2off(ed_rva)
    nfun, nnam = struct.unpack_from("<II", d, eoff + 0x14)
    a_fun, a_nam, a_ord = struct.unpack_from("<III", d, eoff + 0x1C)
    funs = struct.unpack_from("<%dI" % nfun, d, rva2off(a_fun))
    names = struct.unpack_from("<%dI" % nnam, d, rva2off(a_nam))
    ords = struct.unpack_from("<%dH" % nnam, d, rva2off(a_ord))
    out = []
    for i in range(nnam):
        nm = d[rva2off(names[i]):d.index(b"\0", rva2off(names[i]))].decode("latin1")
        out.append({"name": nm, "ordinal": ords[i], "rva": funs[ords[i]]})
    return {"image_base": image_base, "exports": out, "ed_rva": ed_rva, "ed_size": ed_size, "nfun": nfun}


def decls():
    t = io.open(TXT, encoding="utf-8-sig").read().replace("\r\n", "\n")
    cur = None
    for ln in t.split("\n"):
        m = re.match(r'^\.DLL命令\s+(\S+),\s*([^,]*),\s*"([^"]*)",\s*"([^"]*)"', ln)
        if m:
            cur = {"decl": m.group(1), "ret": m.group(2).strip(), "lib": m.group(3), "entry": m.group(4)}
            if "MbKit" in cur["lib"]:
                yield cur
            else:
                cur = None


def main():
    info = exports(DLL)
    ex = {e["name"]: e for e in info["exports"]}
    print("导出数 %d / 具名 %d / ImageBase 0x%08X" % (info["nfun"], len(ex), info["image_base"]))

    fwd = [e for e in info["exports"]
           if info["ed_rva"] <= e["rva"] < info["ed_rva"] + info["ed_size"]]
    print("转发导出（RVA 落在导出目录内）：%d %s"
          % (len(fwd), [x["name"] for x in fwd[:10]]))

    mb = list(decls())
    print("模块里指向 MbKit 的声明：%d" % len(mb))

    slots, miss, fwds = [], [], []
    for i, d in enumerate(mb):
        en = d["entry"]
        plain = en[1:] if en.startswith("@") else en
        e = ex.get(plain)
        if e is None:
            miss.append((d["decl"], en))
            continue
        if info["ed_rva"] <= e["rva"] < info["ed_rva"] + info["ed_size"]:
            fwds.append(plain)
        slots.append({"slot": i, "decl": d["decl"], "entry": en, "export": plain,
                      "rva": e["rva"], "va": info["image_base"] + e["rva"],
                      "ret": d["ret"]})
    print("可解析 %d / 缺导出 %d / 其中转发 %d" % (len(slots), len(miss), len(fwds)))
    for m in miss[:20]:
        print("   缺：%s -> %s" % m)

    # 模块没用到、但导出了的（信息性）
    used = {s["export"] for s in slots}
    extra = sorted(set(ex) - used)
    print("导出表里模块未使用的：%d 个 %s%s" % (len(extra), extra[:12], " ..." if len(extra) > 12 else ""))

    os.makedirs(OUT, exist_ok=True)
    with io.open(os.path.join(OUT, "slots.json"), "w", encoding="utf-8") as f:
        json.dump({"image_base": info["image_base"], "slots": slots,
                   "missing": miss, "forwarded": fwds}, f, ensure_ascii=False, indent=1)
    print("写出 out/slots.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
