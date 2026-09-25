#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""最小可用探针（用于二分定位「哪种写法会让 e.exe 崩」）。

用法: probe_min.py <级别 1..6> <输出目录名>
级别含义（累加）：
  1  只调用一个无参子程序（无置入代码）
  2  + 一个带「置入代码」的包装子程序（文本型参数 / 整数型返回）
  3  + 传址参数
  4  + 长整数型 / 双精度小数型 返回
  5  + 字节集参数
  6  + 在 _启动子程序 体内也放「置入代码」（存/读固定 VA 槽）
"""
import os
import shutil
import sys

sys.stdout.reconfigure(encoding="utf-8")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SCAFFOLD = os.path.join(ROOT, "re", "opt", "e2e", "proj")
BOM = "\ufeff"


def w(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write((BOM + text.replace("\n", "\r\n")).encode("utf-8"))


def bl(xs):
    return "{ " + ", ".join(str(v) for v in xs) + " }"


DLL1 = '''.版本 2

.DLL命令 参考_strlen, 整数型, "msvcrt.dll", "@strlen"
    .参数 文本, 文本型
'''

DLL2 = DLL1 + '''
.DLL命令 参考_memset, 整数型, "msvcrt.dll", "@memset"
    .参数 目标, 整数型, 传址
    .参数 值, 整数型
    .参数 长度, 整数型
'''

DLL3 = DLL2 + '''
.DLL命令 参考_atoi64, 长整数型, "msvcrt.dll", "@_atoi64"
    .参数 文本, 文本型

.DLL命令 参考_atof, 双精度小数型, "msvcrt.dll", "@atof"
    .参数 文本, 文本型
'''

TAIL = "139, 229, 93, 194"
W_TEXT = [139, 69, 8, 139, 0, 80, 255, 21, 0, 0, 0, 80, 139, 229, 93, 194, 4, 0]
W_BYTES = [139, 69, 8, 139, 0, 131, 192, 8, 80, 255, 21, 24, 0, 0, 80, 139, 229, 93, 194, 4, 0]
W_MEM = [255, 117, 16, 255, 117, 12, 141, 69, 8, 80, 255, 21, 4, 0, 0, 80, 139, 229, 93, 194, 12, 0]
W_ATOI = [139, 69, 8, 139, 0, 80, 255, 21, 12, 0, 0, 80, 139, 229, 93, 194, 4, 0]
W_ATOF = [139, 69, 8, 139, 0, 80, 255, 21, 16, 0, 0, 80, 139, 229, 93, 194, 4, 0]


def gen(level, outdir):
    # 35 = 无返回类型子程序 + 返回(0)（不带参考）
    # 36 = 无返回类型子程序 + 返回(0)（带参考参数）
    # 37 = 无返回类型子程序 + 无返回语句（带参考参数）
    if level in (35, 36, 37):
        w(os.path.join(proj, "DLL.e.txt"), DLL1)
        main = ['.版本 2', '', '.程序集 程序集1', '.子程序 _启动子程序, 整数型',
                '    .局部变量 文本, 文本型', '    .局部变量 值, 整数型', '',
                '    文本 ＝ “子级别 %d”' % level, '    值 ＝ 12345']
        if level == 35:
            main += ['    W_空 ()']
        else:
            main += ['    W_空 (值)']
        main += ['    文本 ＝ 文本 ＋ “ 值 ＝ ” ＋ 到文本 (值)',
                 '', '    写到文件 (取运行目录 () ＋ “/min_run.txt”, 到字节集 (文本))', '    返回 (0)', '']
        if level == 35:
            main += ['.子程序 W_空', '', '    返回 (0)', '']
        else:
            main += ['.子程序 W_空', '    .参数 目标, 整数型, 参考', '']
            if level == 36:
                main += ['    返回 (0)']
            main += ['']
        w(os.path.join(proj, "代码", "程序集1.static.e.txt"), "\n".join(main) + "\n")
        w(os.path.join(proj, "代码", "排序.list.txt"), "程序集1\n")
        print("子级别 %d 写好 -> %s" % (level, proj))
        return proj

    proj = os.path.join(HERE, outdir)
    if os.path.isdir(proj):
        shutil.rmtree(proj)
    shutil.copytree(SCAFFOLD, proj)
    res = os.path.join(proj, "资源")
    if os.path.isdir(res):
        shutil.rmtree(res)

    # 31 = 只有 .DLL命令 带传址（直接调用）
    # 32 = 子程序参数带传址，体内无置入代码
    # 33 = 子程序参数带传址，体内有置入代码
    if level in (31, 32, 33, 34):
        if level == 31:
            dll = DLL2
        else:
            dll = DLL1
        w(os.path.join(proj, "DLL.e.txt"), dll)
        main = ['.版本 2', '', '.程序集 程序集1', '.子程序 _启动子程序, 整数型',
                '    .局部变量 文本, 文本型', '    .局部变量 值, 整数型', '',
                '    文本 ＝ “子级别 %d”' % level, '    值 ＝ 12345']
        if level == 31:
            main += ['    参考_memset (值, 0, 4)']
        else:
            main += ['    W_memset (值, 0, 4)']
        main += ['    文本 ＝ 文本 ＋ “ memset后 ＝ ” ＋ 到文本 (值)',
                 '', '    写到文件 (取运行目录 () ＋ “/min_run.txt”, 到字节集 (文本))', '    返回 (0)', '']
        if level in (32, 33, 34):
            mod = '参考' if level == 34 else '传址'
            main += ['.子程序 W_memset', '    .参数 目标, 整数型, ' + mod, '    .参数 值, 整数型',
                     '    .参数 长度, 整数型', '']
            if level in (33, 34):
                main += ['    置入代码 (' + bl(W_MEM) + ')']
            main += ['    返回 (0)', '']
        w(os.path.join(proj, "代码", "程序集1.static.e.txt"), "\n".join(main) + "\n")
        w(os.path.join(proj, "代码", "排序.list.txt"), "程序集1\n")
        print("子级别 %d 写好 -> %s" % (level, proj))
        return proj

    dll = DLL1 if level < 3 else (DLL2 if level < 4 else DLL3)
    w(os.path.join(proj, "DLL.e.txt"), dll)

    main = ['.版本 2', '', '.程序集 程序集1', '.子程序 _启动子程序, 整数型']
    main += ['    .局部变量 文本, 文本型', '    .局部变量 值, 整数型', '']
    main += ['    文本 ＝ “级别 ” ＋ 到文本 (%d)' % level]
    if level >= 6:
        main += ['    值 ＝ 读取槽_探针 (0)']
    if level >= 2:
        main += ['    值 ＝ W_strlen (“QQQQQQQQQQ”)']
        main += ['    文本 ＝ 文本 ＋ “ strlen ＝ ” ＋ 到文本 (值)']
    if level >= 5:
        main += ['    值 ＝ W_字节集 (到字节集 (“QQQQ”))']
        main += ['    文本 ＝ 文本 ＋ “ 字节集 ＝ ” ＋ 到文本 (值)']
    if level >= 3:
        main += ['    值 ＝ 12345', '    W_memset (值, 0, 4)']
        main += ['    文本 ＝ 文本 ＋ “ memset后 ＝ ” ＋ 到文本 (值)']
    if level >= 4:
        main += ['    文本 ＝ 文本 ＋ “ atoi64 ＝ ” ＋ 到文本 (W_atoi64 (“123”))']
        main += ['    文本 ＝ 文本 ＋ “ atof ＝ ” ＋ 到文本 (W_atof (“3.25”))']
    main += ['', '    写到文件 (取运行目录 () ＋ “/min_run.txt”, 到字节集 (文本))', '    返回 (0)', '']

    if level >= 2:
        main += ['.子程序 W_strlen, 整数型', '    .参数 文本, 文本型', '',
                 '    置入代码 (' + bl(W_TEXT) + ')', '    返回 (0)', '']
    if level >= 5:
        main += ['.子程序 W_字节集, 整数型', '    .参数 数据, 字节集', '',
                 '    置入代码 (' + bl(W_BYTES) + ')', '    返回 (0)', '']
    if level >= 3:
        main += ['.子程序 W_memset', '    .参数 目标, 整数型, 传址', '    .参数 值, 整数型', '    .参数 长度, 整数型', '',
                 '    置入代码 (' + bl(W_MEM) + ')', '    返回 (0)', '']
    if level >= 4:
        main += ['.子程序 W_atoi64, 长整数型', '    .参数 文本, 文本型', '',
                 '    置入代码 (' + bl(W_ATOI) + ')', '    返回 (0)', '']
        main += ['.子程序 W_atof, 双精度小数型', '    .参数 文本, 文本型', '',
                 '    置入代码 (' + bl(W_ATOF) + ')', '    返回 (0)', '']
    if level >= 6:
        READ = [139, 69, 8, 193, 224, 2, 5, 0, 0, 0, 80, 139, 0, 139, 229, 93, 194, 4, 0]
        main += ['.子程序 读取槽_探针, 整数型', '    .参数 槽号, 整数型', '',
                 '    置入代码 (' + bl(READ) + ')', '    返回 (0)', '']

    w(os.path.join(proj, "代码", "程序集1.static.e.txt"), "\n".join(main) + "\n")
    w(os.path.join(proj, "代码", "排序.list.txt"), "程序集1\n")
    print("级别 %d 写好 -> %s" % (level, proj))
    return proj


if __name__ == "__main__":
    lv = int(sys.argv[1])
    od = sys.argv[2] if len(sys.argv) > 2 else "pmin%d" % lv
    gen(lv, od)
