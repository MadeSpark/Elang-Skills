#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gen_module.py —— 把 miniblink 模块里 258 条 MbKit `.DLL命令` 改造成
「**内存加载**（手工映射 PE，不落 LoadLibrary）+ **函数取地址**调用」。

设计要点（全部有实测依据，见同级 out/ANSWER-Q.txt 的标定）：

  ① 每个 `.DLL命令 X` 换成一个**同名同签名的子程序 X**，体内是一条置入代码：
        push 参数…（cdecl 逆序）; call dword ptr [池 + 4*槽号]; mov esp,ebp; pop ebp; ret N
     —— 名字一模一样 ⇒ **全模块 500+ 个调用点一个字都不用改**。
     `mov esp,ebp` 让收尾与调用约定无关（MbKit 的 6 个 `MbKit_*` 导出是 stdcall，其余 252
     个 wke*/js* 是 cdecl，同一套尾巴通吃）。

  ② 函数真实地址在启动时写进固定 VA 池：池 = VirtualAlloc(0x50000000, 8192, 0x3000, 4)。
     地址 = 映像基址 + 该导出在 PE 导出表里的 RVA（RVA 构建期算好，运行期零名字解析）。
     池出参区布局：+0xD00=映像基址、+0xD04=映射错误码、+0xD08=重载恢复魔数 "MBK1"(0x314B424D)。
     ★ 业务 DLL 被宿主释放重载后，模块静态变量全部复位、但池/映像/内核无人释放 ⇒
       新实例 VirtualAlloc(0x50000000) 必失败；失败时探测出参区魔数即可恢复复用（09-25）。

  ③ 映像由 mapkit 机器码片段手工映射。该片段**随资源 `#Mapper` 走**（1840 B），
     运行时 RtlMoveMemory 拷到 池+0x800 再调用（**不塞超长置入代码**）。

  ④ 宿主库字节取 `#MbKit` 资源（56320 B，09-23 那版已经在用）；资源拿不到时回退读磁盘。
"""
import io
import json
import os
import re
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import memgen as G                                     # noqa: E402

sys.stdout.reconfigure(encoding="utf-8")

SRC = os.path.join(ROOT, ".tmp", "chk")                # 输入：09-23 的 MbKit 版模块文本
OUT = os.path.join(HERE, "mod_src")                    # 输出：改造后的模块文本

# 池布局（8192 B @ 0x50000000）
#   0x0000..0x0407  258 个函数地址槽
#   0x0800..0x0F30  映射器 blob（1840 B）
#   0x1000..0x1020  浮点返回搬运临时区（4 个浮点返回函数 × 8 B）
#   0x1800/0x1804   映射器出参（映像基址 / 状态码）
NOODLE = 0x0400 * 0 + 1032                             # 槽区大小（自证用）
FTMP_BASE = G.POOL + 0x1000
MAPPER_COPY_DST = G.POOL + 0x800

# ---------------------------------------------------------------- 工具子程序机器码
# 由 memgen 提供并已标定：SLOT_SET / SLOT_GET / BI_PTR / TEXT_PTR / WRITE_I / READ_I
SLOT_SET = G.SLOT_SET
READ_I = G.READ_I

WRAP_DOC = """\
# 包装子程序参数约定（实测标定，见 out/ANSWER-Q.txt）

| 参数类型 | 包装体怎么压栈 | 标定证据 |
|---|---|---|
| 整数型 / 字节型 / 逻辑型 / 子程序指针 / 结构体 | `push [ebp+off]`（槽里就是值或地址） | 3+4=7 ✓；字节型 120 ✓ |
| 文本型 | `mov eax,[ebp+off]; mov eax,[eax]; push eax`（解引用一次） | strlen 参考 10 / 包装 10 ✓ |
| 字节集 | `mov eax,[ebp+off]; mov eax,[eax]; add eax,8; push eax` | 原样推 = 3，解引用+8 = 10 ✓ |
| 长整数型 | 压两个 4 字节槽（高地址先压） | 1234567890123 ✓ |

# 返回值约定

| 返回类型 | 收尾 | 标定证据 |
|---|---|---|
| 整数型 / 字节型 / 逻辑型 / 长整数型 | 直接 `mov esp,ebp; pop ebp; ret N` | 8 字节自返 4294967297 ✓ |
| **小数型** | 追加 `fstp dword [池+0x1000+n]` + `mov eax,[该地址]` | 裸 EAX = -4.1e-33（错），搬运后 = 3 ✓ |
| **双精度小数型** | 追加 `fstp qword [池+0x1000+n]` + `mov eax,[..]` + `mov edx,[..+4]` | 裸 = 3.5e-303（错），搬运后 = 3.25 ✓ |

> `返回 (0)` 放在置入代码之后**不会覆盖 EAX** —— 3+4=7 就是带着这行跑出来的。
"""


def _entry(d):
    e = d["entry"]
    return e[1:] if e.startswith("@") else e


def param_decl(name, typ, flags):
    """生成一行 `.参数`。flags 原样保留 `数组`；`传址` 改写成 `参考`
    （`传址` 是 `.DLL命令` 的写法，子程序侧等价形式是 `参考`）。"""
    if flags == "数组":
        return "    .参数 %s, %s, 数组" % (name, typ)
    if flags == "传址":
        return "    .参数 %s, %s, 参考" % (name, typ)
    return "    .参数 %s, %s" % (name, typ)


def wrap_sub(name, params, code, ret):
    """同名同签名的包装子程序。★ 铁律：声明块之后必须有一个真空行。"""
    L = [".子程序 " + name + (", " + ret if ret else "")]
    for p in params:
        L.append(param_decl(p["name"], p["type"], p["flags"]))
    if params:
        L.append("")
    L.append("    置入代码 (" + G.codes_text(code) + ")")
    if ret:
        L.append("    返回 (0)")
    L.append("")
    return "\n".join(L)


def plain(name, params, body, ret="", pub_note=""):
    """普通子程序。`params` = [(名, 类型[, 标志])]，`body` = 语句行列表。

    `pub_note`：`公开` 与**备注**（子程序的第 3/4 字段）。改写公开子程序时必须原样带上，
    否则会静默丢掉模块的对外文档、甚至丢掉 `公开`（类/模块外就看不到了）。
    """
    L = [".子程序 " + name + (", " + ret if ret else "") + pub_note]
    for p in params:
        if len(p) == 3 and p[2]:
            L.append("    .参数 %s, %s, %s" % p)
        else:
            L.append("    .参数 %s, %s" % (p[0], p[1]))
    if params:
        L.append("")
    L += body
    L.append("")
    return "\n".join(L)


# ================================================================ ① dll.e.txt
def build_dll(src_dir):
    raw = io.open(os.path.join(src_dir, "dll.e.txt"), encoding="utf-8-sig").read()
    raw = raw.replace("\r\n", "\n")
    lines = raw.split("\n")

    kept, dropping = [], False
    n_drop = 0
    for ln in lines:
        if ln.startswith(".DLL命令"):
            m = re.match(r'^\.DLL命令\s+\S+,\s*[^,]*,\s*"([^"]*)"', ln)
            if m and "MbKit" in m.group(1):
                dropping, n_drop = True, n_drop + 1
                continue                                  # 丢掉声明行本身
            dropping = False
            kept.append(ln)
            continue
        if dropping:
            if ln.startswith("    .参数"):
                continue                                  # 丢掉参数行
            if ln.strip() == "":
                dropping = False                          # 连同后缀空行一起丢
                continue
            dropping = False
        kept.append(ln)

    text = "\n".join(kept).rstrip("\n") + "\n"
    # 追加两条新的系统库命令：只为取到 GetModuleHandleA / GetProcAddress 的**函数地址**，
    # 交给手工映射器解析 MbKit 的导入表用。
    text += "\n" + "\n".join([
        '.DLL命令 _内存_GetModuleHandleA, 整数型, "kernel32.dll", "GetModuleHandleA"',
        "    .参数 _参数1, 文本型",
        "",
        '.DLL命令 _内存_GetProcAddress, 整数型, "kernel32.dll", "GetProcAddress"',
        "    .参数 _参数1, 整数型",
        "    .参数 _参数2, 文本型",
        "",
    ])
    return text, n_drop


# ================================================================ ② 包装程序集
def build_wrappers(mb, ex):
    slots, blocks = [], []
    ftmp = FTMP_BASE
    for i, d in enumerate(mb):
        params, nbytes, risks = G.layout(d, structs)
        if params is None or risks:
            raise SystemExit("布局失败：%s %s" % (d["name"], risks))
        e = _entry(d)
        if e not in ex:
            raise SystemExit("导出表里没有 %s" % e)
        code = G.wrap_code(params, nbytes, i, d["ret"], ftmp)
        if d["ret"] in ("小数型", "双精度小数型"):
            ftmp += 8
        slots.append((i, d["name"], e, ex[e]))
        blocks.append(wrap_sub(d["name"], params, code, d["ret"]))
    return slots, blocks, ftmp


def build_assembly(mb, slots, blocks):
    head = "\n".join([
        ".版本 2",
        "",
        ".程序集 程序集_内存映射",
        ".程序集变量 _内存池, 整数型",
        ".程序集变量 _内存基址, 整数型",
        ".程序集变量 _内存状态, 整数型",
        "",
    ])

    tools = "\n".join([
        # ---- 机器码小工具 -------------------------------------------------
        G.sub("_内存取字节集指针", [("_参数1", "字节集")], G.BI_PTR, "整数型"),
        G.sub("_内存写槽", [("_参数1", "整数型"), ("_参数2", "整数型")], SLOT_SET),
        G.sub("_内存读槽", [("_参数1", "整数型")], G.SLOT_GET, "整数型"),
        G.sub("_内存读整数", [("_参数1", "整数型"), ("_参数2", "整数型")], READ_I, "整数型"),
        # ---- 12 参映射器：49 B 前导，blob 已在 池+0x800 ---------------------
        G.sub("_内存调映射器", [("_参数%d" % i, "整数型") for i in range(1, 13)],
              G.mapper_code()),
    ])

    init_body = [
        "    .局部变量 内核32, 整数型",
        "    .局部变量 宿主库, 字节集",
        "    .局部变量 魔数字节, 字节集",
        "    .局部变量 函数VA, 整数型",
        "    .局部变量 函数VF, 整数型",
        "    .局部变量 函数LLA, 整数型",
        "    .局部变量 函数GPA, 整数型",
        "    .局部变量 函数TA, 整数型",
        "    .局部变量 函数TG, 整数型",
        "    .局部变量 函数TS, 整数型",
        "    .局部变量 函数GM, 整数型",
        "",
        "    ' 1 = 已就绪；2 = 已失败（记住失败，别每次重试再炸一次）",
        "    .如果真 (_内存状态 ＝ 1)",
        "        返回 (真)",
        "    .如果真结束",
        "    .如果真 (_内存状态 ＝ 2)",
        "        返回 (假)",
        "    .如果真结束",
        "    _内存状态 ＝ 2",
        "",
    "    ' ---- ① 固定 VA 池（槽区 + 映射器 + 浮点搬运区 + 出参，见文件头布局表）----",
    "    .如果真 (_内存池 ＝ 0)",
    "        _内存池 ＝ _DLL命令0A0100BF_VirtualAlloc (1342177280, 8192, 12288, 4)",
    "    .如果真结束",
    "    .如果真 (_内存池 ＝ 0)",
    "        ' ★ 09-25 重载恢复：业务 DLL 被释放重载后，旧实例的固定池(0x50000000)没人释放，",
    "        '   仍被占用 ⇒ VirtualAlloc 返回 0。此时若旧池出参区有魔数(0x50000D08)，",
    "        '   说明上实例的槽表 / MbKit 映像 / 内核线程都还活着 —— 直接恢复复用，不再重映射。",
    "        .如果真 (_内存读整数 (1342180616, 0) ≠ 827408461)",
    "            返回 (假)",
    "        .如果真结束",
    "        _内存基址 ＝ _内存读整数 (1342180608, 0)",
    "        .如果真 (_内存基址 ＝ 0 或 _内存读整数 (_内存基址, 0) ≠ 23117 或 _内存读槽 (0) ＝ 0)",
    "            返回 (假)",
    "        .如果真结束",
    "        _内存池 ＝ 1342177280",
    "        _内存状态 ＝ 1",
    "        返回 (真)",
    "    .如果真结束",
    "",
        "    ' ---- ② 宿主库字节：优先内嵌资源，拿不到再回退读磁盘 ----",
        "    宿主库 ＝ #MbKit",
        "    .如果真 (取字节集长度 (宿主库) ＝ 0)",
        "        宿主库 ＝ _内存读磁盘宿主库 ()",
        "    .如果真结束",
        "    .如果真 (取字节集长度 (宿主库) ＝ 0)",
        "        返回 (假)",
        "    .如果真结束",
        "",
        "    ' ---- ③ 映射器 blob 拷进 池+0x800 ----",
        "    _DLL命令0A0100C1_RtlMoveMemory (1342179328, _内存取字节集指针 (#Mapper), 取字节集长度 (#Mapper))",
        "",
        "    ' ---- ④ 取 8 个 API 的函数地址，交给映射器自己用 ----",
        "    内核32 ＝ _内存_GetModuleHandleA (“kernel32.dll”)",
        "    .如果真 (内核32 ＝ 0)",
        "        返回 (假)",
        "    .如果真结束",
        "    函数VA ＝ _内存_GetProcAddress (内核32, “VirtualAlloc”)",
        "    函数VF ＝ _内存_GetProcAddress (内核32, “VirtualFree”)",
        "    函数LLA ＝ _内存_GetProcAddress (内核32, “LoadLibraryA”)",
        "    函数GPA ＝ _内存_GetProcAddress (内核32, “GetProcAddress”)",
        "    函数TA ＝ _内存_GetProcAddress (内核32, “TlsAlloc”)",
        "    函数TG ＝ _内存_GetProcAddress (内核32, “TlsGetValue”)",
        "    函数TS ＝ _内存_GetProcAddress (内核32, “TlsSetValue”)",
        "    函数GM ＝ _内存_GetProcAddress (内核32, “GetModuleHandleA”)",
        "    .如果真 (函数VA ＝ 0 或 函数GPA ＝ 0 或 函数GM ＝ 0)",
        "        返回 (假)",
        "    .如果真结束",
        "",
        "    ' ---- ⑤ 手工映射（拷节 / 重定位 / 导入表 / TLS / 调入口点）----",
        "    ' ★ 易语言命令行不允许跨行 ⇒ 必须写成一整行",
        "    _内存调映射器 (函数VA, 函数VF, 函数LLA, 函数GPA, 函数TA, 函数TG, 函数TS, 函数GM, _内存取字节集指针 (宿主库), 取字节集长度 (宿主库), 1342180608, 1342180612)",
        "    _内存基址 ＝ _内存读整数 (1342180608, 0)",
        "    .如果真 (_内存基址 ＝ 0 或 _内存读整数 (1342180612, 0) ≠ 0)",
        "        返回 (假)",
        "    .如果真结束",
        "",
        "    ' ---- ⑥ 把每个导出的实际地址写进槽位 ----",
    ]
    fill = ["    _内存写槽 (%d, _内存基址 ＋ %d)" % (s, rva) for s, _, _, rva in slots]
    init_body += fill
    init_body += [
        "",
        "    ' ★ 09-25 写重载恢复魔数到出参区(0x50000D08)：业务 DLL 重载后的新实例据此识别并复用本实例的池/映像/内核",
        "    魔数字节 ＝ { 77, 66, 75, 49 }",
        "    _DLL命令0A0100C1_RtlMoveMemory (1342180616, _内存取字节集指针 (魔数字节), 4)",
        "    _内存状态 ＝ 1",
        "    返回 (真)",
    ]

    init = plain("_内存载入宿主库", [], init_body, "逻辑型")

    disk = plain("_内存读磁盘宿主库", [], [
        "    .局部变量 候选, 文本型",
        "    .局部变量 结果, 字节集",
        "",
        "    候选 ＝ 取运行目录 () ＋ “\\main\\corn\\MbKit.dll”",
        "    结果 ＝ 读入文件 (候选)",
        "    .如果真 (取字节集长度 (结果) ＞ 0)",
        "        返回 (结果)",
        "    .如果真结束",
        "    候选 ＝ 取运行目录 () ＋ “\\MbKit.dll”",
        "    结果 ＝ 读入文件 (候选)",
        "    返回 (结果)",
    ], "字节集")

    diag = plain("_内存自检文本", [], [
        "    返回 (“[内存加载] 池=0x” ＋ 取十六进制文本 (_内存池) ＋ “ 映像基址=0x” ＋ 取十六进制文本 (_内存基址) ＋ “ 状态=” ＋ 到文本 (_内存状态) ＋ “ 槽0=0x” ＋ 取十六进制文本 (_内存读槽 (0)) ＋ “ 槽%d=0x” ＋ 取十六进制文本 (_内存读槽 (%d)))"
        % (len(slots) - 1, len(slots) - 1),
    ], "文本型")

    parts = [head, tools, init, disk, diag, "\n".join(blocks)]
    return "\n".join(parts), diag


# ================================================================ ③ 程序集_全局
SUB_RE = re.compile(r"^\.子程序\s", re.M)


def replace_sub(text, name, new_block):
    """把 `text` 里名为 name 的子程序整块（含其声明/参数/体，到下一个 .子程序 前）替换掉。"""
    pat = re.compile(r"^\.子程序\s+" + re.escape(name) + r"\s*(?:,|$).*?(?=^\.子程序\s|\Z)",
                     re.M | re.S)
    m = pat.search(text)
    if not m:
        raise SystemExit("找不到子程序 %s" % name)
    # ★ 块与块之间必须有**空行**分隔（易语言文本规范形），新块自身以 "\n" 结尾，
    #   这里再补一个，正好形成空行。
    return text[:m.start()] + new_block + "\n" + text[m.end():]


def build_global(src_dir, nslots):
    p = os.path.join(src_dir, "class", "程序集_全局.static.e.txt")
    text = io.open(p, encoding="utf-8-sig").read().replace("\r\n", "\n")

    text = replace_sub(text, "_确保宿主库已载入", plain(
        "_确保宿主库已载入", [], [
            "    ' ★★ 2026-09-24：改成「内存加载 + 函数取地址」。",
            "    '   原实现是「把内嵌的 MbKit.dll 释放到磁盘 + LoadLibraryA 预载」——",
            "    '   那只是为了绕开易语言 `.DLL命令` 只能按库名解析的限制。",
            "    '   现在 258 条 wke*/js* 命令全部改成按地址调用，绕开了该限制，",
            "    '   于是可以真正的手工映射：不进加载器链表、不落盘、不占模块名。",
            "    返回 (_内存载入宿主库 ())",
        ], "逻辑型"))

    text = replace_sub(text, "WKE宿主自检", plain(
        "WKE宿主自检", [], [
            "    .局部变量 自行, 文本型",
            "",
            "    自行 ＝ _内存自检文本 ()",
            "    .如果真 (_确保宿主库已载入 () ＝ 假)",
            "        返回 (自行 ＋ “ / 宿主库载入失败”)",
            "    .如果真结束",
            "    返回 (自行 ＋ “ / ” ＋ 指针到文本 (_MbKit_Diag ()))",
        ], "文本型",
        ", 公开, 取宿主层（MbKit）与内存映射的一行式诊断信息：映射池 / 映像基址 / 映射状态 / "
        "函数地址槽，以及内核状态、内核路径、缺失导出、加载期错误。"
        "排查「内核为什么没加载 / 网页为什么是空白」时先看它。"))

    # 顺手修正被上一版改造写进备注里的旧说法（现在是内存映射，不再往磁盘释放）
    text = text.replace(
        "把本模块内置的 MbKit 宿主库释放到磁盘并预载入进程。",
        "把本模块内置的 MbKit 宿主库手工映射进内存（不落盘、不 LoadLibrary）。")
    return text


# ================================================================ 主流程
def main():
    global structs
    info = G.pe_exports(G.DLL)
    ex = info["exports"]

    decls = G.parse_decls(os.path.join(SRC, "dll.e.txt"))
    mb = [d for d in decls if "MbKit" in d["lib"]]
    assert len(mb) == 260, len(mb)   # 252 wke/js + 8 _MbKit_（09-25 +KernelState+FwdError）

    structs = {}
    t = io.open(os.path.join(SRC, "struct.e.txt"), encoding="utf-8-sig").read().replace("\r\n", "\n")
    for ln in t.split("\n"):
        m = re.match(r"^\.数据类型\s+(\S+?)(?:,|$)", ln.strip())
        if m:
            structs[m.group(1)] = 1
    G.SIZE_OF["子程序指针"] = 4                        # 易语言写的是 `子程序指针`（无「型」）

    slots, blocks, ftmp_end = build_wrappers(mb, ex)
    print("包装子程序 %d 个，槽 0..%d，浮点搬运区用掉 %d B（0x%X..0x%X）"
          % (len(blocks), len(slots) - 1, ftmp_end - FTMP_BASE, FTMP_BASE, ftmp_end))
    assert ftmp_end <= G.POOL + 0x1800, "浮点搬运区越界"

    asm, diag = build_assembly(mb, slots, blocks)

    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    shutil.copytree(SRC, OUT)

    dll_text, n_drop = build_dll(SRC)
    G.w(os.path.join(OUT, "dll.e.txt"), dll_text)
    G.w(os.path.join(OUT, "class", "程序集_内存映射.static.e.txt"), asm)
    G.w(os.path.join(OUT, "class", "程序集_全局.static.e.txt"), build_global(SRC, len(slots)))

    # order.list.txt：追加新程序集（保留原 BOM/CRLF）
    ol = os.path.join(OUT, "class", "order.list.txt")
    old = io.open(ol, encoding="utf-8-sig").read().replace("\r\n", "\n")
    names = [x for x in old.split("\n") if x.strip()]
    if "程序集_内存映射" not in names:
        names.append("程序集_内存映射")
    io.open(ol, "w", encoding="utf-8", newline="").write(
        "\ufeff" + "\r\n".join(names) + "\r\n")

    # 资源：新增 #Mapper（1840 B），保留原有 #MbKit
    rd = os.path.join(OUT, "resource")
    with open(os.path.join(rd, "Mapper.image.dat"), "wb") as f:
        f.write(G.MAPPER_BLOB)
    desc = json.loads(io.open(os.path.join(rd, "resource.desc.json"),
                              encoding="utf-8-sig").read())
    desc["Mapper"] = {
        "Comment": "PE 手工映射器机器码（%d B，运行时拷到 池+0x800）" % len(G.MAPPER_BLOB),
        "IsPublic": False,
    }
    io.open(os.path.join(rd, "resource.desc.json"), "w",
            encoding="utf-8-sig", newline="\r\n").write(
        json.dumps(desc, ensure_ascii=False, indent=4) + "\n")

    io.open(os.path.join(HERE, "out", "包装子程序参数与返回约定.md"), "w",
            encoding="utf-8", newline="").write(WRAP_DOC)

    print("模块文本已生成：%s" % OUT)
    print("  dll.e.txt 丢掉 MbKit 声明 %d 条，追加 2 条（GetModuleHandleA / GetProcAddress）" % n_drop)
    print("  宿主库 %d B（#MbKit）、映射器 %d B（#Mapper）、SizeOfImage 0x%X"
          % (os.path.getsize(os.path.join(SRC, "resource", "MbKit.image.dat")),
             len(G.MAPPER_BLOB), info["size_of_image"]))
    print("  自检子程序：_内存自检文本 () 已生成")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
