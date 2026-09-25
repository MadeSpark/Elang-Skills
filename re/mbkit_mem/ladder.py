#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ladder.py —— 阶梯二分：把 demo 里的构造逐层加进去，定位让 e.exe 崩（0xC0000005）的那一层。

L0 空壳基线 → L1 报告 → L2 置入代码工具子程序 → L3 包装子程序
 → L4 _映射映像(12参/含参考) → L5 #MbKit 资源 → L6 建窗口 → L7 消息循环(完整 demo)

每层都是一个**完整可编译的工程**（scaffold 复制 + 覆写代码文本），
用 elang-build 的 ecompile --check 跑，读 harness 里的 e_self_exit 判崩。
"""
import io
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import memgen as G                       # noqa: E402
import gen_demo as D                     # noqa: E402

PY = sys.executable
ECOMPILE = r"C:\Users\MadeSpark\.workbuddy\skills\elang-build\scripts\ecompile.py"
# ⚠️ 本机的 safe-delete 策略会拦掉 Python 的 shutil.rmtree ⇒ 一律用唯一目录名，不做删除
TAG = os.environ.get("LADDER_TAG") or __import__("time").strftime("%H%M%S")
OUTROOT = os.path.join(HERE, "ladder_out")

B = lambda xs: "置入代码 (" + G.codes_text(xs) + ")"       # noqa: E731

# 一个无参包装子程序（尾跳槽 0），返回整数型
WRAP0 = [0xFF, 0x15, 0x00, 0x00, 0x00, 0x50] + [0x8B, 0xE5, 0x5D, 0xC2, 0x00, 0x00]
# 一个带 1 个整数参数的包装子程序：push [ebp+8]; call [槽0]; 尾跳 ret 4
WRAP1 = [0xFF, 0x75, 0x08, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x50] + \
        [0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0x00]
# _映射映像 的空壳（只收尾，不做映射）
MAPPER_STUB = [0x8B, 0xE5, 0x5D, 0xC2, 0x30, 0x00]


def proj(level, body, dll=True):
    d = os.path.join(OUTROOT, "L%d_%s" % (level, TAG))
    shutil.copytree(D.SCAFFOLD, d)
    G.w(os.path.join(d, "DLL.e.txt"), D.DLL if dll else ".版本 2\n")
    G.w(os.path.join(d, "代码", "程序集1.static.e.txt"), body)
    G.w(os.path.join(d, "常量.e.txt"), ".版本 2\n")
    return d


def build_level(n):
    """返回该层的程序集文本（完整工程）。"""
    P = []
    P.append(".版本 2\n")
    P.append(".程序集 程序集1")
    P.append(".程序集变量 _池, 整数型")
    P.append(".程序集变量 _映像, 整数型")
    P.append(".程序集变量 _状态, 整数型")
    P.append(".程序集变量 _窗口, 整数型")
    P.append(".程序集变量 _日志, 文本型")
    P.append("")

    subs = []          # 子程序块（排在 _启动子程序 之后）

    # ---- L1: 报告 ----
    if n >= 1:
        subs.append(G.plain_sub("报告", [("参数_文本", "文本型")], [
            "    _日志 ＝ _日志 ＋ 参数_文本 ＋ 字符 (13) ＋ 字符 (10)",
            "    写到文件 (取运行目录 () ＋ “\\L.log”, 到字节集 (_日志))",
        ]))

    # ---- L2: 置入代码工具子程序 ----
    if n >= 2:
        subs.append(G.sub("_取字节集指针", [("数据", "字节集")], G.BI_PTR, "整数型"))
        subs.append(G.sub("_取文本指针", [("文本", "文本型")], G.TEXT_PTR, "整数型"))
        subs.append(G.sub("_写内存整数", [("地址", "整数型"), ("偏移", "整数型"), ("值", "整数型")], G.WRITE_I))
        subs.append(G.sub("_读内存整数", [("地址", "整数型"), ("偏移", "整数型")], G.READ_I, "整数型"))
        subs.append(G.sub("_写槽", [("槽号", "整数型"), ("值", "整数型")], G.SLOT_SET))
        subs.append(G.sub("_读槽", [("槽号", "整数型")], G.SLOT_GET, "整数型"))

    # ---- L3: 一个包装子程序 ----
    if n >= 3:
        subs.append(G.sub("_包1", [], WRAP0, "整数型"))
    # ---- L8/L9: 带 1 个整数参数的包装子程序（L8 = 真实长名，L9 = 短名对照） ----
    if n == 8:
        subs.append(G.sub("_DLL命令0A010113_wkeIsInitialize", [("_参数1", "整数型")],
                          WRAP1, "整数型"))
    if n == 9:
        subs.append(G.sub("_包2", [("_参数1", "整数型")], WRAP1, "整数型"))

    # ---- L4: _映射映像（12 参，含 2 个参考） ----
    if n >= 4:
        subs.append(G.sub("_映射映像", G.MAPPER_PARAMS, MAPPER_STUB))

    # ---- L6: 建窗口用到的系统 DLL 命令由 DLL.e.txt 提供 ----

    # ---- _启动子程序 ----
    m = [".子程序 _启动子程序, 整数型",
         "    .局部变量 内核32, 整数型",
         "    .局部变量 类信息, 整数型",
         "    .局部变量 消息, 整数型",
         "    .局部变量 类名, 文本型",
         "    .局部变量 标题, 文本型",
         "    .局部变量 网址, 文本型",
         "    .局部变量 类名宽, 整数型",
         "    .局部变量 标题宽, 整数型",
         "    .局部变量 帧, 整数型",
         "    .局部变量 计数, 整数型",
         "    .局部变量 轮次, 整数型",
         ""]
    if n >= 1:
        m.append("    _日志 ＝ “”")
        m.append("    报告 (“[L%d] 阶梯开始”)" % n)
    if n >= 2:
        m.append("    报告 (“[L2] 指针工具 = ” ＋ 到文本 (_取文本指针 (“abc”)))")
        m.append("    _写槽 (0, 305419896)")
        m.append("    报告 (“[L2] 读槽 = ” ＋ 到文本 (_读槽 (0)))")
    if n >= 3:
        m.append("    报告 (“[L3] 包装子程序(空壳) = ” ＋ 到文本 (_包1 ()))")
    if n == 8:
        m.append("    报告 (“[L8] 长名包装 = ” ＋ 到文本 (_DLL命令0A010113_wkeIsInitialize (1)))")
    if n == 9:
        m.append("    报告 (“[L9] 短名包装 = ” ＋ 到文本 (_包2 (1)))")
    if n >= 4:
        m.append("    _状态 ＝ -1")
        m.append("    _映射映像 (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, _映像, _状态)")
        m.append("    报告 (“[L4] 空壳映射后 映像 = ” ＋ 到文本 (_映像) ＋ “ 状态 = ” ＋ 到文本 (_状态))")
    if n >= 5:
        m.append("    报告 (“[L5] #MbKit 资源长度 = ” ＋ 到文本 (取字节集长度 (#MbKit)))")
        m.append("    报告 (“[L5] #MbKit 数据指针 = ” ＋ 到文本 (_取字节集指针 (#MbKit)))")
    if n >= 6:
        m += ["    内核32 ＝ _GMF (“kernel32.dll”)",
              "    类信息 ＝ _VA (0, 64, 12288, 4)",
              "    类名 ＝ “LadderClass”",
              "    标题 ＝ “Ladder”",
              "    类名宽 ＝ _VA (0, 256, 12288, 4)",
              "    标题宽 ＝ _VA (0, 256, 12288, 4)",
              "    _MBCW (0, 0, _取文本指针 (类名), -1, 类名宽, 128)",
              "    _MBCW (0, 0, _取文本指针 (标题), -1, 标题宽, 128)",
              "    _写内存整数 (类信息, 0, 3)",
              "    _写内存整数 (类信息, 4, _GPA (内核32, “DefWindowProcW”))",
              "    _写内存整数 (类信息, 8, 0)",
              "    _写内存整数 (类信息, 12, 0)",
              "    _写内存整数 (类信息, 16, _GMF (0))",
              "    _写内存整数 (类信息, 20, 0)",
              "    _写内存整数 (类信息, 24, _LCT (0, 32512))",
              "    _写内存整数 (类信息, 28, 6)",
              "    _写内存整数 (类信息, 32, 0)",
              "    _写内存整数 (类信息, 36, 类名宽)",
              "    _RCW (类信息)",
              "    _窗口 ＝ _CWEW (0, 类名宽, 标题宽, 13565952, 60, 60, 1000, 780, 0, 0, _GMF (0), 0)",
              "    报告 (“[L6] 窗口 = ” ＋ 到文本 (_窗口))"]
    if n >= 7:
        m += ["    消息 ＝ _VA (0, 64, 12288, 4)",
              "    .计次循环首 (3, 计数)",
              "        .判断循环首 (_PMW (消息, 0, 0, 0, 1) ≠ 0)",
              "            _TMW (消息)",
              "            _DMW (消息)",
              "        .判断循环尾 ()",
              "",
              "        _SL (25)",
              "    .计次循环尾 ()",
              "    _DW (_窗口)",
              "    报告 (“[L7] 消息循环完成”)"]
    if n >= 1:
        m.append("    报告 (“== 阶梯结束 ==”)")
    m += ["    返回 (0)", ""]

    text = "\n".join(P) + "\n" + "\n".join(subs) + "\n" + "\n".join(m) + "\n"
    return text


def run_check(d):
    wd = os.path.join(OUTROOT, "wk_" + os.path.basename(d))
    p = subprocess.run([PY, ECOMPILE, d, "--check", "--workdir", wd],
                       capture_output=True)
    raw = (p.stdout or b"") + (b"\n" + p.stderr if p.stderr else b"")
    out = raw.decode("gbk", errors="replace") if b"\xa1" in raw or b"\xb3" in raw \
        else raw.decode("utf-8", errors="replace")
    hs = [f for f in os.listdir(wd) if f.endswith("_harness.txt")] if os.path.isdir(wd) else []
    exitcode = None
    for f in hs:
        t = io.open(os.path.join(wd, f), encoding="utf-8", errors="replace").read()
        mm = re.search(r"e_self_exit=(\S+)", t)
        if mm:
            exitcode = mm.group(1)
    ok = ("语法检查通过" in out) or ("编译成功" in out) or ("编译完成" in out)
    tail = [l for l in out.strip().split("\n") if l.strip()]
    return ok, exitcode, (tail[0][:80] if tail else "(无输出)")


if __name__ == "__main__":
    levels = [int(x) for x in sys.argv[1:]] or list(range(0, 8))
    print("层  结果     e_self_exit   说明")
    print("-" * 78)
    for n in levels:
        d = proj(n, build_level(n))
        ok, code, tail = run_check(d)
        print("L%-2d %-8s %-13s %s" % (n, "通过" if ok else "崩溃", code or "-", tail))
        sys.stdout.flush()
