#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""memgen.py —— 把易语言模块里的 MbKit `.DLL命令` 改造成「内存加载 + 函数取地址」

产物（两用）：
  demo   : 自包含演示工程（内存映射 MbKit → 绑主窗口句柄 → 载网页）
  module : 替换现有 miniblink 模块里的 258 条 MbKit 声明

原理（全部有实测物证，见 out/ANSWER-Q.txt / out/calib_N.json）：
  · 每个 `.DLL命令` 换成一个**同名同签名的包装子程序**，体内是一条 `置入代码` 机器码：
        push 参数…（cdecl 逆序）; call dword ptr [池 + 4*槽号]; mov esp,ebp; pop ebp; ret N
    `mov esp,ebp` 让收尾与调用约定无关（cdecl / stdcall 都对）。
  · 函数的真实地址在启动时写进固定 VA 池：池 = VirtualAlloc(0x50000000, 4096, …)。
    地址 = 映像基址 + 该导出在 PE 导出表里的 RVA（RVA 由本脚本在**构建期**算出，
    运行期不做任何名字解析 ⇒ 不受「手工映射不进加载器链表」影响）。
  · 映像由 mk_map 机器码片段手工映射（CRC 级的 PE 加载：拷节/重定位/导入/TLS/入口点）。
"""
import io
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

BOM = "\ufeff"
POOL = 0x50000000
POOL_SIZE = 8192        # ★ 必须 ≥ 0x1808：两个出参固定在 池+0x1800 / 池+0x1804
DLL = os.path.join(ROOT, "src", "mbkit", "MbKit.dll")
CHK = os.path.join(ROOT, ".tmp", "chk")          # 当前交付模块的 e2t 文本
SCAFFOLD = os.path.join(ROOT, "re", "opt", "e2e", "proj")

sys.stdout.reconfigure(encoding="utf-8")

# ------------------------------------------------------------------ PE 导出表
def pe_exports(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    szopt = struct.unpack_from("<H", d, pe + 0x14)[0]
    opt = pe + 0x18
    ib = struct.unpack_from("<I", d, opt + 0x1C)[0]
    soi = struct.unpack_from("<I", d, opt + 0x38)[0]
    ed_rva = struct.unpack_from("<I", d, opt + 0x60)[0]
    secs = []
    for i in range(nsec):
        sh = opt + szopt + i * 40
        vs, va, rs, ra = struct.unpack_from("<IIII", d, sh + 8)
        secs.append((va, vs, ra, rs))

    def r2o(rva):
        for va, vs, ra, rs in secs:
            if va <= rva < va + max(vs, rs):
                return ra + (rva - va)
        return None

    eo = r2o(ed_rva)
    nnam = struct.unpack_from("<I", d, eo + 0x18)[0]
    afun, anam, aord = struct.unpack_from("<III", d, eo + 0x1C)
    funs = struct.unpack_from("<%dI" % nnam, d, r2o(afun))
    nams = struct.unpack_from("<%dI" % nnam, d, r2o(anam))
    ords = struct.unpack_from("<%dH" % nnam, d, r2o(aord))
    out = {}
    for i in range(nnam):
        o = r2o(nams[i])
        nm = d[o:d.index(b"\0", o)].decode("latin1")
        out[nm] = funs[ords[i]]
    return {"image_base": ib, "size_of_image": soi, "exports": out,
            "ed_rva": ed_rva, "ed_size": struct.unpack_from("<I", d, opt + 0x60 + 4)[0]}


# ------------------------------------------------------------------ 声明解析
DECL_RE = re.compile(r'^\.DLL命令\s+(\S+),\s*([^,]*),\s*"([^"]*)",\s*"([^"]*)"\s*(.*)$')
PARAM_RE = re.compile(r"^ {4}\.参数\s+(\S+),\s*([^,]*?)\s*(?:,\s*(.*))?$")


def parse_decls(path):
    t = io.open(path, encoding="utf-8-sig").read().replace("\r\n", "\n")
    out, cur = [], None
    for ln in t.split("\n"):
        m = DECL_RE.match(ln)
        if m:
            cur = {"name": m.group(1), "ret": m.group(2).strip(), "lib": m.group(3),
                   "entry": m.group(4), "tail": m.group(5).strip(), "params": []}
            out.append(cur)
            continue
        p = PARAM_RE.match(ln)
        if p and cur is not None:
            cur["params"].append((p.group(1), p.group(2).strip(), (p.group(3) or "").strip()))
    return out


# ------------------------------------------------------------------ 帧尺寸模型
SIZE_OF = {"整数型": 4, "字节型": 4, "逻辑型": 4, "小数型": 4, "子程序指针型": 4,
           "文本型": 4, "字节集": 4, "日期时间型": 4, "窗口组件": 4, "通用型": 4,
           "长整数型": 8, "双精度小数型": 8, "短整数型": 2}
DEREF1 = {"文本型"}
DEREF8 = {"字节集"}


def size_of(typ, structs, flags=""):
    if typ.startswith("_结构") or typ in structs:
        return 4
    if flags == "数组":
        return 4
    return SIZE_OF.get(typ)


def layout(decl, structs):
    off, ps, risks = 8, [], []
    for pn, pt, pf in decl["params"]:
        sz = size_of(pt, structs, pf)
        if sz is None:
            risks.append("未知尺寸类型 %s" % pt)
            return None, None, risks
        ps.append({"name": pn, "type": pt, "flags": pf, "off": off, "size": sz,
                   "deref1": pt in DEREF1, "deref8": pt in DEREF8})
        off += sz
    return ps, off - 8, risks


def push_tokens(p):
    if p["deref1"]:
        return ["8B", "45", "%02X" % p["off"], "8B", "00", "50"]
    if p["deref8"]:
        return ["8B", "45", "%02X" % p["off"], "8B", "00", "83", "C0", "08", "50"]
    out = []
    for k in range(p["size"] // 4 - 1, -1, -1):
        out += ["FF", "75", "%02X" % (p["off"] + 4 * k)]
    return out


def call_slot_tokens(slot):
    v = POOL + 4 * slot
    return ["FF", "15"] + ["%02X" % ((v >> (8 * i)) & 0xFF) for i in range(4)]


def wrap_code(params, nbytes, slot, ret, ftmp):
    t = []
    for p in reversed(params):
        t += push_tokens(p)
    t += call_slot_tokens(slot)
    if ret == "双精度小数型":
        t += ["DD", "1D"] + ["%02X" % ((ftmp >> (8 * i)) & 0xFF) for i in range(4)]
        t += ["A1"] + ["%02X" % ((ftmp >> (8 * i)) & 0xFF) for i in range(4)]
        t += ["8B", "15"] + ["%02X" % (((ftmp + 4) >> (8 * i)) & 0xFF) for i in range(4)]
    elif ret == "小数型":
        t += ["D9", "1D"] + ["%02X" % ((ftmp >> (8 * i)) & 0xFF) for i in range(4)]
        t += ["A1"] + ["%02X" % ((ftmp >> (8 * i)) & 0xFF) for i in range(4)]
    t += ["8B", "E5", "5D", "C2", "%02X" % (nbytes & 0xFF), "%02X" % ((nbytes >> 8) & 0xFF)]
    return [int(x, 16) for x in t]


def codes_text(xs):
    return "{ " + ", ".join(str(v) for v in xs) + " }"


# ------------------------------------------------------------------ 文本工具
def w(path, text):
    """★ 用 encoding="utf-8" + 手工写 BOM：utf-8-sig 会自己再补一个 BOM，
    两者叠加会写出**双 BOM**（实测 efbbbf efbbbf），e2txt/t2e 读出来是乱码前缀。"""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with io.open(path, "w", encoding="utf-8", newline="") as f:
        f.write(BOM + text.replace("\n", "\r\n"))


def sub(name, params, code, ret="", public=False, note=""):
    """生成一个「包装子程序」文本块。
    ★ 铁律：声明块与第一条语句之间必须有一个**真空行**（实测漏掉会让 e.exe 崩）。"""
    L = [".子程序 " + name + (", " + ret if ret else "")]
    for pn, pt in params:
        L.append("    .参数 " + pn + (", " + pt if pt else ""))
    if params:
        L.append("")
    L.append("    置入代码 (" + codes_text(code) + ")")
    if ret:
        L.append("    返回 (0)")
    L.append("")
    return "\n".join(L)


def plain_sub(name, params, body, ret="", public=False, note=""):
    L = [".子程序 " + name + (", " + ret if ret else "")]
    for pn, pt in params:
        L.append("    .参数 " + pn + (", " + pt if pt else ""))
    if params:
        L.append("")
    L += body
    L.append("")
    return "\n".join(L)


# ------------------------------------------------------------------ 公共机器码
POOL_INIT = None       # 由 gen 时填：_初始化池 的机器码
SLOT_SET = [0x8B, 0x45, 0x08, 0xC1, 0xE0, 0x02, 0x05] + [(POOL >> (8 * i)) & 0xFF for i in range(4)] + \
           [0x8B, 0x4D, 0x0C, 0x89, 0x08, 0x8B, 0xE5, 0x5D, 0xC2, 0x08, 0x00]
SLOT_GET = [0x8B, 0x45, 0x08, 0xC1, 0xE0, 0x02, 0x05] + [(POOL >> (8 * i)) & 0xFF for i in range(4)] + \
           [0x8B, 0x00, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0x00]
BI_PTR = [0x8B, 0x45, 0x08, 0x8B, 0x00, 0x83, 0xC0, 0x08, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0x00]
TEXT_PTR = [0x8B, 0x45, 0x08, 0x8B, 0x00, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0x00]
WRITE_I = [0x8B, 0x45, 0x08, 0x03, 0x45, 0x0C, 0x8B, 0x4D, 0x10, 0x89, 0x08, 0x8B, 0xE5, 0x5D, 0xC2, 0x0C, 0x00]
READ_I = [0x8B, 0x45, 0x08, 0x03, 0x45, 0x0C, 0x8B, 0x00, 0x8B, 0xE5, 0x5D, 0xC2, 0x08, 0x00]

MAPPER_BLOB = open(os.path.join(HERE, "mapkit_text.bin"), "rb").read()
# 映射器包装：12 个参数 → 逆序 push → 调到 池+0x800 的 blob
MAPPER_OFFS = [0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x34]

# 池的内存布局：槽 0..257 占 池+0x0000..0x0407；映射器 blob 放 池+0x800（1952 B ⇒ 0x800..0xF9F，塞得进 4096 的池）
MAPPER_DISP = 0x800
MAPPER_VA = POOL + MAPPER_DISP


def mapper_code():
    """★★ 2026-09-24 重大改版：**不再把 1952 B 映射器塞进一条「置入代码」大数组**。

    实测（demo_mem.exe 首次运行）：池/kernel32/8 个 API 地址/内嵌宿主库 56320 B 全部正常，
    日志停在「即将调用手工映射器」⇒ 一执行那条 2007 字节的置入代码就 0xC0000005。
    结论：**e.exe 对超大置入代码数组的代码生成不可靠**（解析期能过，运行期炸）。

    改成：映射器随**资源** `#Mapper` 走（和 #MbKit 同一个资源机制，56320 B 都能内嵌，没问题），
    运行时用 RtlMoveMemory 拷到 池+0x800，本函数只留 **49 字节**：
        push 12 个参数（逆序） → mov eax, <池+0x800 绝对地址> → call eax → 收尾
    绝对地址是构建期算好的常量，所以**连相对寻址都不用**。
    """
    t = []
    for off in reversed(MAPPER_OFFS):          # cdecl：最后一个参数先压
        t += [0xFF, 0x75, off]
    t += [0xB8] + [(MAPPER_VA >> (8 * i)) & 0xFF for i in range(4)]   # mov eax, imm32
    t += [0xFF, 0xD0, 0x8B, 0xE5, 0x5D, 0xC2, 0x30, 0x00]             # call eax; mov esp,ebp; pop ebp; ret 48
    assert len(t) == 49, len(t)
    return t


MAPPER_PARAMS = [
    ("参数_VirtualAlloc", "整数型"), ("参数_VirtualFree", "整数型"),
    ("参数_LoadLibraryA", "整数型"), ("参数_GetProcAddress", "整数型"),
    ("参数_TlsAlloc", "整数型"), ("参数_TlsGetValue", "整数型"),
    ("参数_TlsSetValue", "整数型"), ("参数_GetModuleHandleA", "整数型"),
    ("参数_数据指针", "整数型"), ("参数_数据长度", "整数型"),
    ("参数_出参基址", "整数型"), ("参数_出参状态", "整数型"),
]
# ★★ 出参改成「直接把池里的两个地址当整数型实参传」。
#   原来写成 `整数型, 参考`，靠易语言往槽里放变量地址 —— 但那个语义没被证实，
#   而映射器开头就是 `mov esi,[arg11]; mov dword ptr [esi],0`：
#   万一槽里放的是值（0）而不是地址，就是**写地址 0** ⇒ 立刻 0xC0000005。
#   现在这两个出参的地址由构建期固定（池 + 0x1800 / 0x1804），调用后回来读内存即可。
OUT_DISP = 0x1800
OUT_BASE_VA = POOL + OUT_DISP
OUT_STATUS_VA = POOL + OUT_DISP + 4
