#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
build_text.py —— 把 miniblink.e 改造成「自包含宿主层」版本（产出易语言文本工程）

改造内容（对应需求）：
  ① 252 处 `.DLL命令` 的库名 "main\\corn\\node.dll" → "MbKit.dll"（裸名）
     裸名会被 Windows 搜索路径第 1 站（宿主 exe 所在目录）命中，
     而 MbKit 又会在启动早期被本模块「释放 + 预载」，两条路都通往同一份。
  ② 内嵌 MbKit.dll 为二进制资源 → 代码里用 #MbKit 当字节集引用
  ③ 新增公开子程序：WKE设置node路径 / WKE取node路径 / WKE宿主自检 / WKE载入宿主库
     + 内部支撑：_确保宿主库已载入 / _取宿主库文件路径 / _尝试从目录取宿主库 / _释放宿主库文件
  ④ 全部公开子程序与两个类的 _初始化 开头插入预载调用（保证任何入口都先备好宿主层）

输入：re/opt/src_txt（原始 miniblink.e 的 e2t 产物）
输出：re/opt/build_txt（可直接喂给 e2txt -mode t2e）
"""
import io
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "src_txt")
DST = os.path.join(HERE, "build_txt")
DLL_BIN = os.path.abspath(os.path.join(HERE, "..", "..", "src", "mbkit", "MbKit.dll"))

sys.stdout.reconfigure(encoding="utf-8")

BOM = "\ufeff"
NL = "\r\n"

# ---------------------------------------------------------------------------
# ① 新增的 DLL 命令（追加到 dll.e.txt 末尾）
#
# 调用约定：易语言 .DLL命令 的「命令名」**不写 @ 前缀 = stdcall**（写 @ = cdecl）。
#   · 原模块的 node.dll 命令全部写 "@wkeXxx" ⇒ miniblink 的内核导出是 cdecl
#   · 本文件里的 MbKit_* / LoadLibraryA 都是 __stdcall ⇒ 不能写 @
# 返回指针的命令按本模块惯例声明为「整数型」，调用方再用 指针到文本 () 转。
# ---------------------------------------------------------------------------
NEW_DLL = [
    # 名, 返回类型, 库, 命令名, [(参数名, 类型, 标志)]
    ("_MbKit_LoadLibraryA", "整数型", "kernel32.dll", "LoadLibraryA",
     [("_参数1", "文本型", "")]),
    ("_MbKit_SetNodePath", "整数型", "MbKit.dll", "MbKit_SetNodePath",
     [("_参数1", "文本型", "")]),
    ("_MbKit_ConfigNodePath", "整数型", "MbKit.dll", "MbKit_ConfigNodePath", []),
    ("_MbKit_KernelPath", "整数型", "MbKit.dll", "MbKit_KernelPath", []),
    ("_MbKit_KernelLoaded", "整数型", "MbKit.dll", "MbKit_KernelLoaded", []),
    ("_MbKit_IsReady", "整数型", "MbKit.dll", "MbKit_IsReady", []),
    ("_MbKit_Diag", "整数型", "MbKit.dll", "MbKit_Diag", []),
]

LIB_OLD = '"main\\corn\\node.dll"'
LIB_NEW = '"MbKit.dll"'

PRELOAD = "    _确保宿主库已载入 ()"

# ---------------------------------------------------------------------------
# ③ 新增子程序块（插到 程序集_全局 的 WKE卸载 之后）
# ---------------------------------------------------------------------------
NEW_SUBS = """\
.子程序 WKE设置node路径, 逻辑型, 公开, 设置 miniblink 内核（node.dll）所在位置。参数可传目录，也可传 node.dll 的完整文件名；留空表示使用默认位置（宿主程序所在目录）。必须在内核加载之前调用（即 WKE初始化 或首次创建浏览框之前），否则返回假。
    .参数 参数_node路径, 文本型, 可空, 内核所在目录，或 node.dll 的完整文件名；留空 = 默认（宿主程序根目录）

    .如果真 (_确保宿主库已载入 () ＝ 假)
        返回 (假)
    .如果真结束
    返回 (_MbKit_SetNodePath (参数_node路径) ≠ 0)
    
.子程序 WKE取node路径, 文本型, 公开, 取 miniblink 内核（node.dll）当前实际使用的完整路径。注意：本命令会触发内核查找与加载。
    .局部变量 _局部变量1, 整数型

    .如果真 (_确保宿主库已载入 () ＝ 假)
        返回 (“”)
    .如果真结束
    _局部变量1 ＝ _MbKit_KernelPath ()
    .如果真 (_局部变量1 ＝ 0)
        返回 (“”)
    .如果真结束
    返回 (指针到文本 (_局部变量1))
    
.子程序 WKE宿主自检, 文本型, 公开, 取宿主层（MbKit）的一行式诊断信息：内核状态、内核路径、自定义路径、宿主程序目录、当前目录、缺失导出、加载期错误。排查「内核为什么没加载」时先看它。
    .局部变量 _局部变量1, 整数型

    .如果真 (_确保宿主库已载入 () ＝ 假)
        返回 (“MbKit 宿主库载入失败：无法把内置的 MbKit.dll 释放到磁盘”)
    .如果真结束
    _局部变量1 ＝ _MbKit_Diag ()
    .如果真 (_局部变量1 ＝ 0)
        返回 (“”)
    .如果真结束
    返回 (指针到文本 (_局部变量1))
    
.子程序 WKE载入宿主库, 逻辑型, 公开, 把本模块内置的 MbKit 宿主库释放到磁盘并预载入进程。一般无需手工调用（首次使用 MB浏览框 或调用 WKE初始化 时会自动完成）；需要确定性地提前完成时可显式调用。
    返回 (_确保宿主库已载入 ())
    
.子程序 _确保宿主库已载入, 逻辑型
    .局部变量 _局部变量1, 文本型

    .如果真 (_宿主库句柄 ≠ 0)
        返回 (真)
    .如果真结束
    _局部变量1 ＝ _取宿主库文件路径 ()
    .如果真 (_局部变量1 ＝ “”)
        返回 (假)
    .如果真结束
    _宿主库句柄 ＝ _MbKit_LoadLibraryA (_局部变量1)
    返回 (_宿主库句柄 ≠ 0)
    
.子程序 _取宿主库文件路径, 文本型
    .局部变量 _局部变量1, 文本型
    .局部变量 _局部变量2, 文本型

    _局部变量1 ＝ 取运行目录 ()
    .如果真 (_局部变量1 ＝ “”)
        _局部变量1 ＝ 取当前目录 ()
    .如果真结束
    _局部变量2 ＝ _尝试从目录取宿主库 (_局部变量1)
    .如果真 (_局部变量2 ＝ “”)
        _局部变量1 ＝ 读环境变量 (“TEMP”)
        .如果真 (_局部变量1 ＝ “”)
            _局部变量1 ＝ 读环境变量 (“TMP”)
        .如果真结束
        _局部变量1 ＝ _局部变量1 ＋ “\\ElangMbKit”
        创建目录 (_局部变量1)
        _局部变量2 ＝ _尝试从目录取宿主库 (_局部变量1)
    .如果真结束
    返回 (_局部变量2)
    
.子程序 _尝试从目录取宿主库, 文本型, , 在指定目录里备好 MbKit.dll（不存在或版本不符时从模块资源释放），成功返回完整路径，失败返回空文本。
    .参数 参数_目录, 文本型
    .局部变量 _局部变量1, 文本型

    .如果真 (参数_目录 ＝ “”)
        返回 (“”)
    .如果真结束
    _局部变量1 ＝ 参数_目录 ＋ “\\MbKit.dll”
    .如果真 (_释放宿主库文件 (_局部变量1) ＝ 假)
        _局部变量1 ＝ “”
    .如果真结束
    返回 (_局部变量1)
    
.子程序 _释放宿主库文件, 逻辑型, , 确保目标文件就是本模块内置的那份 MbKit.dll（尺寸一致即视为同一版本），否则写出去。
    .参数 参数_目标文件, 文本型
    .局部变量 _局部变量1, 逻辑型

    _局部变量1 ＝ (取文件尺寸 (参数_目标文件) ＝ 取字节集长度 (#MbKit))
    .如果真 (_局部变量1 ＝ 假)
        _局部变量1 ＝ 写到文件 (参数_目标文件, #MbKit)
    .如果真结束
    返回 (_局部变量1)
    
"""

# 子程序之间的分隔必须是「4 空格空行 + 真空行 + .子程序」（与原模块的多数写法一致）
NEW_SUBS = NEW_SUBS.replace("\n    \n.子程序", "\n    \n\n.子程序")
NEW_SUBS = NEW_SUBS.rstrip("\n") + "\n\n"

RES_DESC = {
    "MbKit": {
        "Comment": "MbKit 宿主层（常驻转发 node.dll）。内嵌在此，首次运行时释放到磁盘并预载。",
        "IsPublic": False,
    }
}


# ---------------------------------------------------------------------------
def read_bom(path):
    return io.open(path, encoding="utf-8-sig").read().replace("\r\n", "\n")


def write_bom(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write((BOM + text.replace("\n", "\n")).replace("\n", "\r\n").encode("utf-8"))


def is_decl_or_blank(line):
    s = line.strip()
    if s == "":
        return True
    if s.startswith(".参数") or s.startswith(".局部变量"):
        return True
    return False


def inject_preload(text, stats):
    """在程序集_全局 里所有 **公开** 子程序的开头插入 _确保宿主库已载入 ()"""
    lines = text.split("\n")
    out = []
    i = 0
    n = len(lines)
    while i < n:
        out.append(lines[i])
        m = lines[i].startswith(".子程序 ")
        if m:
            head = lines[i]
            pub = ", 公开" in head
            name = head.split(",", 1)[0][len(".子程序 "):].strip()
            # 找到本子程序体的第一个「非声明非空」行
            j = i + 1
            while j < n and not lines[j].startswith(".子程序 ") and not lines[j].startswith(".程序集"):
                if not is_decl_or_blank(lines[j]):
                    break
                out.append(lines[j])
                j += 1
            if pub and j < n and not lines[j].startswith(".子程序") and not lines[j].startswith(".程序集"):
                out.append(PRELOAD)
                stats.append(name)
            i = j
            continue
        i += 1
    return "\n".join(out)


def main():
    if os.path.isdir(DST):
        shutil.rmtree(DST)
    shutil.copytree(SRC, DST)

    # ---------- dll.e.txt ----------
    p = os.path.join(DST, "dll.e.txt")
    t = read_bom(p)
    cnt = t.count(LIB_OLD)
    assert cnt == 252, "库名替换数不符：%d" % cnt
    t = t.replace(LIB_OLD, LIB_NEW)
    if not t.endswith("\n"):
        t += "\n"
    t += "\n"
    for name, ret, lib, fn, params in NEW_DLL:
        t += '.DLL命令 %s, %s, "%s", "%s"\n' % (name, ret, lib, fn)
        for pn, pt, pf in params:
            t += "    .参数 %s, %s%s\n" % (pn, pt, (", " + pf) if pf else "")
        t += "\n"
    write_bom(p, t)
    print("  dll.e.txt      : 库名替换 %d 处；新增 .DLL命令 %d 条" % (cnt, len(NEW_DLL)))

    # ---------- 程序集_全局 ----------
    p = os.path.join(DST, "class", "程序集_全局.static.e.txt")
    t = read_bom(p)
    # 程序集变量
    anchor = ".程序集 程序集_全局\n"
    assert anchor in t
    t = t.replace(anchor, anchor + ".程序集变量 _宿主库句柄, 整数型\n", 1)
    # 公开子程序预载
    stats = []
    t = inject_preload(t, stats)
    # 新增子程序块：插在 WKE卸载 之后
    marker = ".子程序 WKE取版本, 整数型, 公开, wkeGetVersion\n"
    assert marker in t
    assert "\n\n" + marker in t
    # 定位：在 WKE卸载 段结束（即 marker 前的空行处）插入
    t = t.replace(marker, NEW_SUBS + marker, 1)
    write_bom(p, t)
    print("  程序集_全局   : 预载插入 %d 个子程序；新增子程序 8 个" % len(stats))
    for s in stats:
        print("                   · %s" % s)

    # ---------- 两个类的 _初始化 ----------
    # 注：MB浏览框.绑定 内部会调用 WKE初始化（已含预载），故绑定本身无需再改。
    for rel, cls in [("class/MB浏览框.class.e.txt", "MB浏览框"),
                     ("class/wke文件系统.class.e.txt", "wke文件系统")]:
        p = os.path.join(DST, rel)
        t = read_bom(p)
        anchor = ".子程序 _初始化\n    \n"
        assert anchor in t, "未找到 %s._初始化" % cls
        t = t.replace(anchor, ".子程序 _初始化\n" + PRELOAD + "\n    \n", 1)
        write_bom(p, t)
        print("  %-12s: _初始化 已插入预载" % cls)

    # ---------- 内嵌资源 ----------
    res_dir = os.path.join(DST, "resource")
    os.makedirs(res_dir, exist_ok=True)
    shutil.copyfile(DLL_BIN, os.path.join(res_dir, "MbKit.image.dat"))
    with open(os.path.join(res_dir, "resource.desc.json"), "wb") as f:
        f.write((BOM + json.dumps(RES_DESC, indent=4, ensure_ascii=False)).encode("utf-8"))
    print("  resource/     : MbKit.image.dat %d 字节（= %s）"
          % (os.path.getsize(DLL_BIN), os.path.relpath(DLL_BIN, os.path.join(HERE, "..", ".."))))

    # ---------- 自检：块尾相邻（流程线）规则 ----------
    bad = check_blockend_blank(DST)
    if bad:
        print("\n  ✗ 发现「隐藏块尾紧跟块尾」缺空行 %d 处：" % len(bad))
        for b in bad:
            print("      %s L%d: %s → %s" % b)
        return 1
    print("\n  自检：块尾相邻空行规则 —— 通过")
    return 0


def check_blockend_blank(root):
    """隐藏块尾（.如果真结束 / .如果结束）紧跟另一条块尾时，中间必须有空行。
    例外：可见块尾（.计次循环尾 ()）紧跟隐藏块尾 —— 原模块本身就是这么写的，渲染正常。"""
    import re
    HID = re.compile(r"^\.(如果真结束|如果结束)$")
    VIS = re.compile(r"^\.(计次循环尾|判断循环尾)\s*(\(\s*\))?$")
    BLKE = re.compile(r"^\.(如果真结束|如果结束|计次循环尾|判断循环尾|判断结束)\s*(\(\s*\))?$")
    bad = []
    for dp, dn, fn in os.walk(root):
        for f in fn:
            if not f.endswith(".e.txt"):
                continue
            fp = os.path.join(dp, f)
            lines = read_bom(fp).split("\n")
            for i in range(len(lines) - 1):
                a, b = lines[i].strip(), lines[i + 1].strip()
                if HID.match(a) and BLKE.match(b):
                    bad.append((os.path.relpath(fp, root), i + 1, a, b))
    return bad


if __name__ == "__main__":
    sys.exit(main())
