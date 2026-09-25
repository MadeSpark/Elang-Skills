#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ladder2.py —— 在 L5 基线上做两条轴的细粒度二分：

  轴 A（窗口）：L5 + 建窗口块的前 k 行   → 定位具体是哪一条语句让 e.exe 崩
  轴 B（blob）：L5 + 真 mapper_code()   → 2007 B 的「置入代码」是否也崩

用法：python ladder2.py A1 A4 A8 ...  /  B0 B1
"""
import io
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import memgen as G                       # noqa: E402
import gen_demo as D                     # noqa: E402
import ladder as L                       # noqa: E402

PY = sys.executable
ECOMPILE = r"C:\Users\MadeSpark\.workbuddy\skills\elang-build\scripts\ecompile.py"
TAG = os.environ.get("L2TAG") or time.strftime("%H%M%S")
OUTROOT = os.path.join(HERE, "ladder2_out")

# 建窗口块的 21 行（照抄 ladder.py 的 n>=6 分支，逐行可切）
W6 = [
    "    内核32 ＝ _GMF (“kernel32.dll”)",                                     # 1
    "    类信息 ＝ _VA (0, 64, 12288, 4)",                                     # 2
    "    类名 ＝ “LadderClass”",                                               # 3
    "    标题 ＝ “Ladder”",                                                    # 4
    "    类名宽 ＝ _VA (0, 256, 12288, 4)",                                    # 5
    "    标题宽 ＝ _VA (0, 256, 12288, 4)",                                    # 6
    "    _MBCW (0, 0, _取文本指针 (类名), -1, 类名宽, 128)",                   # 7
    "    _MBCW (0, 0, _取文本指针 (标题), -1, 标题宽, 128)",                   # 8
    "    _写内存整数 (类信息, 0, 3)",                                          # 9
    "    _写内存整数 (类信息, 4, _GPA (内核32, “DefWindowProcW”))",            # 10
    "    _写内存整数 (类信息, 8, 0)",                                          # 11
    "    _写内存整数 (类信息, 12, 0)",                                         # 12
    "    _写内存整数 (类信息, 16, _GMF (0))",                                  # 13
    "    _写内存整数 (类信息, 20, 0)",                                         # 14
    "    _写内存整数 (类信息, 24, _LCT (0, 32512))",                           # 15
    "    _写内存整数 (类信息, 28, 6)",                                         # 16
    "    _写内存整数 (类信息, 32, 0)",                                         # 17
    "    _写内存整数 (类信息, 36, 类名宽)",                                    # 18
    "    _RCW (类信息)",                                                       # 19
    "    _窗口 ＝ _CWEW (0, 类名宽, 标题宽, 13565952, 60, 60, 1000, 780, 0, 0, _GMF (0), 0)",  # 20
    "    报告 (“[L6] 窗口 = ” ＋ 到文本 (_窗口))",                             # 21
]


def build(nwin, real_blob):
    """L5 基线 + 可选真 blob + 建窗口前 nwin 行。"""
    P = []
    P.append(".版本 2\n")
    P.append(".程序集 程序集1")
    for v in ("_池", "_映像", "_状态", "_窗口"):
        P.append(".程序集变量 %s, 整数型" % v)
    P.append(".程序集变量 _日志, 文本型")
    P.append("")

    subs = []
    subs.append(G.plain_sub("报告", [("参数_文本", "文本型")], [
        "    _日志 ＝ _日志 ＋ 参数_文本 ＋ 字符 (13) ＋ 字符 (10)",
        "    写到文件 (取运行目录 () ＋ “\\L2.log”, 到字节集 (_日志))",
    ]))
    subs.append(G.sub("_取字节集指针", [("数据", "字节集")], G.BI_PTR, "整数型"))
    subs.append(G.sub("_取文本指针", [("文本", "文本型")], G.TEXT_PTR, "整数型"))
    subs.append(G.sub("_写内存整数", [("地址", "整数型"), ("偏移", "整数型"), ("值", "整数型")], G.WRITE_I))
    subs.append(G.sub("_读内存整数", [("地址", "整数型"), ("偏移", "整数型")], G.READ_I, "整数型"))
    subs.append(G.sub("_写槽", [("槽号", "整数型"), ("值", "整数型")], G.SLOT_SET))
    subs.append(G.sub("_读槽", [("槽号", "整数型")], G.SLOT_GET, "整数型"))
    subs.append(G.sub("_包1", [], L.WRAP0, "整数型"))
    body = D.mapper_code() if real_blob else L.MAPPER_STUB
    subs.append(G.sub("_映射映像", G.MAPPER_PARAMS, body))

    m = [".子程序 _启动子程序, 整数型"]
    for v, t in (("内核32", "整数型"), ("类信息", "整数型"), ("消息", "整数型"),
                 ("类名", "文本型"), ("标题", "文本型"), ("网址", "文本型"),
                 ("类名宽", "整数型"), ("标题宽", "整数型"), ("帧", "整数型"),
                 ("计数", "整数型"), ("轮次", "整数型")):
        m.append("    .局部变量 %s, %s" % (v, t))
    m.append("")
    m.append("    _日志 ＝ “”")
    m.append("    报告 (“[base] L5 基线”)")
    m.append("    报告 (“[L2] 指针工具 = ” ＋ 到文本 (_取文本指针 (“abc”)))")
    m.append("    _写槽 (0, 305419896)")
    m.append("    报告 (“[L2] 读槽 = ” ＋ 到文本 (_读槽 (0)))")
    m.append("    报告 (“[L3] 包装子程序(空壳) = ” ＋ 到文本 (_包1 ()))")
    m.append("    _状态 ＝ -1")
    m.append("    _映射映像 (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, _映像, _状态)")
    m.append("    报告 (“[L4] 空壳映射后 映像 = ” ＋ 到文本 (_映像) ＋ “ 状态 = ” ＋ 到文本 (_状态))")
    m.append("    报告 (“[L5] #MbKit 资源长度 = ” ＋ 到文本 (取字节集长度 (#MbKit)))")
    m.append("    报告 (“[L5] #MbKit 数据指针 = ” ＋ 到文本 (_取字节集指针 (#MbKit)))")
    m += W6[:nwin]
    m.append("    报告 (“== 结束 ==”)")
    m += ["    返回 (0)", ""]

    return "\n".join(P) + "\n" + "\n".join(subs) + "\n" + "\n".join(m) + "\n"


def proj(name, nwin, real_blob):
    d = os.path.join(OUTROOT, "%s_%s" % (name, TAG))
    shutil.copytree(D.SCAFFOLD, d)
    G.w(os.path.join(d, "DLL.e.txt"), D.DLL)
    G.w(os.path.join(d, "代码", "程序集1.static.e.txt"), build(nwin, real_blob))
    G.w(os.path.join(d, "常量.e.txt"), ".版本 2\n")
    return d


def decode(raw):
    """两种编码都试，取替换字符少的那个。"""
    cands = []
    for enc in ("utf-8", "gbk"):
        try:
            cands.append((raw.decode(enc).count("\ufffd"), raw.decode(enc)))
        except Exception:
            pass
    cands.sort(key=lambda x: x[0])
    return cands[0][1] if cands else ""


def run_check(d):
    wd = os.path.join(OUTROOT, "wk_" + os.path.basename(d))
    p = subprocess.run([PY, ECOMPILE, d, "--check", "--workdir", wd],
                       capture_output=True)
    raw = (p.stdout or b"") + (b"\n" + p.stderr if p.stderr else b"")
    out = decode(raw)
    hs = [f for f in os.listdir(wd) if f.endswith("_harness.txt")] if os.path.isdir(wd) else []
    exitcode = None
    for f in hs:
        t = io.open(os.path.join(wd, f), encoding="utf-8", errors="replace").read()
        mm = re.search(r"e_self_exit=(\S+)", t)
        if mm:
            exitcode = mm.group(1)
    ok = ("语法检查通过" in out) or ("编译成功" in out) or ("编译完成" in out)
    lines = [l.strip() for l in out.split("\n") if l.strip()]
    return ok, exitcode, (lines[0][:78] if lines else "(无输出)")


if __name__ == "__main__":
    specs = sys.argv[1:] or ["A0", "A21", "B1"]
    print("用例       结果     e_self_exit   说明")
    print("-" * 84)
    for s in specs:
        if s.startswith("A"):
            nwin, blob = int(s[1:]), False
        elif s.startswith("B"):
            nwin, blob = 0, bool(int(s[1:]))
        else:
            continue
        d = proj(s, nwin, blob)
        ok, code, tail = run_check(d)
        print("%-10s %-8s %-13s %s" % (s, "通过" if ok else "崩溃", code or "-", tail))
        sys.stdout.flush()
