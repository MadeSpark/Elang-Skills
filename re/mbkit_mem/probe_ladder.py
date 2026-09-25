#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""阶梯探针：按位累加特性，逐级编译，定位让 e.exe 崩的写法。

用法: probe_ladder.py <位掩码十进制> <输出目录名>
位：
  0x01  W_strlen     文本型参数 + 置入代码（尾跳槽表）→ 整数型
  0x02  W_字节集      字节集参数 + 置入代码 → 整数型
  0x04  W_memset     参考参数(void) + 置入代码
  0x08  W_tick       零参数 + 置入代码 → 整数型
  0x10  _存槽        void 子程序 + 置入代码（含动态写内存）
  0x20  读取槽_探针   整数型返回 + 置入代码，且在表达式里被调用
  0x40  kernel32 声明 + _VirtualAlloc 调用
  0x80  长整数型/双精度 子程序返回
"""
import os
import shutil
import sys

sys.stdout.reconfigure(encoding="utf-8")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SCAFFOLD = os.path.join(ROOT, "re", "opt", "e2e", "proj")
BOM = "\ufeff"
POOL = 0x50000000


def w(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write((BOM + text.replace("\n", "\r\n")).encode("utf-8"))


def bl(xs):
    return "{ " + ", ".join(str(v) for v in xs) + " }"


DLL_BASE = '''.版本 2

.DLL命令 参考_strlen, 整数型, "msvcrt.dll", "@strlen"
    .参数 文本, 文本型

.DLL命令 参考_strlen集, 整数型, "msvcrt.dll", "@strlen"
    .参数 数据, 字节集

.DLL命令 参考_memset, 整数型, "msvcrt.dll", "@memset"
    .参数 目标, 整数型, 传址
    .参数 值, 整数型
    .参数 长度, 整数型
'''

DLL_K32 = '''
.DLL命令 _VirtualAlloc, 整数型, "kernel32.dll", "VirtualAlloc"
    .参数 地址, 整数型
    .参数 长度, 整数型
    .参数 类型, 整数型
    .参数 保护, 整数型

.DLL命令 _GetModuleHandleA, 整数型, "kernel32.dll", "GetModuleHandleA"
    .参数 模块名, 文本型

.DLL命令 _GetProcAddress, 整数型, "kernel32.dll", "GetProcAddress"
    .参数 模块句柄, 整数型
    .参数 函数名, 文本型
'''

DLL_BIG = '''
.DLL命令 参考_atoi64, 长整数型, "msvcrt.dll", "@_atoi64"
    .参数 文本, 文本型

.DLL命令 参考_atof, 双精度小数型, "msvcrt.dll", "@atof"
    .参数 文本, 文本型
'''

C_TEXT = [139, 69, 8, 139, 0, 80, 255, 21, 0, 0, 0, 80, 139, 229, 93, 194, 4, 0]
C_BYTES = [139, 69, 8, 139, 0, 131, 192, 8, 80, 255, 21, 24, 0, 0, 80, 139, 229, 93, 194, 4, 0]
C_MEM = [255, 117, 16, 255, 117, 12, 255, 21, 4, 0, 0, 80, 139, 229, 93, 194, 12, 0]
C_TICK = [255, 21, 8, 0, 0, 80, 139, 229, 93, 194, 0, 0]
C_STORE = [139, 69, 8, 193, 224, 2, 5, 0, 0, 0, 80, 139, 77, 12, 137, 8, 139, 229, 93, 194, 8, 0]
C_READ = [139, 69, 8, 193, 224, 2, 5, 0, 0, 0, 80, 139, 0, 139, 229, 93, 194, 4, 0]
C_I64 = [139, 69, 8, 139, 0, 80, 255, 21, 12, 0, 0, 80, 139, 229, 93, 194, 4, 0]
C_F64 = [139, 69, 8, 139, 0, 80, 255, 21, 16, 0, 0, 80, 139, 229, 93, 194, 4, 0]
C_SELF8 = [139, 69, 8, 139, 85, 12, 139, 229, 93, 194, 8, 0]


def main():
    mask = int(sys.argv[1], 0)
    out = sys.argv[2]
    proj = os.path.join(HERE, out)
    if os.path.isdir(proj):
        shutil.rmtree(proj)
    shutil.copytree(SCAFFOLD, proj)
    res = os.path.join(proj, "资源")
    if os.path.isdir(res):
        shutil.rmtree(res)

    dll = DLL_BASE
    if mask & 0x40:
        dll += DLL_K32
    if mask & 0x80:
        dll += DLL_BIG
    w(os.path.join(proj, "DLL.e.txt"), dll)

    M = ['.版本 2', '', '.程序集 程序集1', '.子程序 _启动子程序, 整数型',
         '    .局部变量 文本, 文本型', '    .局部变量 值, 整数型', '    .局部变量 池, 整数型',
         '    .局部变量 内核, 整数型', '    .局部变量 大数, 长整数型', '    .局部变量 小1, 双精度小数型',
         '', '    文本 ＝ “掩码 ” ＋ 到文本 (' + str(mask) + ')', '    值 ＝ 12345']
    if mask & 0x40:
        M += ['    池 ＝ _VirtualAlloc (1342177280, 4096, 12288, 4)',
              '    内核 ＝ _GetModuleHandleA (“msvcrt.dll”)',
              '    文本 ＝ 文本 ＋ “ 池 ＝ ” ＋ 到文本 (池)',
              '    文本 ＝ 文本 ＋ “ 内核 ＝ ” ＋ 到文本 (内核)']
    if mask & 0x01:
        M += ['    值 ＝ W_strlen (“QQQQQQQQQQ”)', '    文本 ＝ 文本 ＋ “ strlen ＝ ” ＋ 到文本 (值)']
    if mask & 0x02:
        M += ['    值 ＝ W_字节集 ({ 81, 81, 81 })', '    文本 ＝ 文本 ＋ “ 字节集 ＝ ” ＋ 到文本 (值)']
    if mask & 0x04:
        M += ['    W_memset (值, 0, 4)', '    文本 ＝ 文本 ＋ “ memset后 ＝ ” ＋ 到文本 (值)']
    if mask & 0x08:
        M += ['    值 ＝ W_tick ()', '    文本 ＝ 文本 ＋ “ tick ＝ ” ＋ 到文本 (值)']
    if mask & 0x10:
        M += ['    _存槽 (1, 4096)']
    if mask & 0x20:
        M += ['    文本 ＝ 文本 ＋ “ 槽1 ＝ ” ＋ 到文本 (读取槽_探针 (1))']
    if mask & 0x80:
        M += ['    大数 ＝ 参考_atoi64 (“123”)', '    文本 ＝ 文本 ＋ “ i64 ＝ ” ＋ 到文本 (大数)',
              '    小1 ＝ 参考_atof (“3.25”)', '    文本 ＝ 文本 ＋ “ f64 ＝ ” ＋ 到文本 (小1)',
              '    大数 ＝ _自返8 (4294967297)', '    文本 ＝ 文本 ＋ “ 自返8 ＝ ” ＋ 到文本 (大数)',
              '    文本 ＝ 文本 ＋ “ atoi64 ＝ ” ＋ 到文本 (W_atoi64 (“123”))',
              '    文本 ＝ 文本 ＋ “ atof ＝ ” ＋ 到文本 (W_atof (“3.25”))']
    M += ['', '    写到文件 (取运行目录 () ＋ “/ladder_run.txt”, 到字节集 (文本))', '    返回 (0)', '']

    if mask & 0x01:
        M += ['.子程序 W_strlen, 整数型', '    .参数 文本, 文本型', '', '    置入代码 (' + bl(C_TEXT) + ')', '    返回 (0)', '']
    if mask & 0x02:
        M += ['.子程序 W_字节集, 整数型', '    .参数 数据, 字节集', '', '    置入代码 (' + bl(C_BYTES) + ')', '    返回 (0)', '']
    if mask & 0x04:
        M += ['.子程序 W_memset', '    .参数 目标, 整数型, 参考', '    .参数 值, 整数型',
              '    .参数 长度, 整数型', '', '    置入代码 (' + bl(C_MEM) + ')', '']
    if mask & 0x08:
        M += ['.子程序 W_tick, 整数型', '', '    置入代码 (' + bl(C_TICK) + ')', '    返回 (0)', '']
    if mask & 0x10:
        M += ['.子程序 _存槽', '    .参数 槽号, 整数型', '    .参数 值, 整数型', '',
              '    置入代码 (' + bl(C_STORE) + ')', '']
    if mask & 0x20:
        M += ['.子程序 读取槽_探针, 整数型', '    .参数 槽号, 整数型', '', '    置入代码 (' + bl(C_READ) + ')', '    返回 (0)', '']
    if mask & 0x80:
        M += ['.子程序 W_atoi64, 长整数型', '    .参数 文本, 文本型', '', '    置入代码 (' + bl(C_I64) + ')', '    返回 (0)', '']
        M += ['.子程序 W_atof, 双精度小数型', '    .参数 文本, 文本型', '', '    置入代码 (' + bl(C_F64) + ')', '    返回 (0)', '']
        M += ['.子程序 _自返8, 长整数型', '    .参数 甲, 长整数型', '', '    置入代码 (' + bl(C_SELF8) + ')', '    返回 (0)', '']

    w(os.path.join(proj, "代码", "程序集1.static.e.txt"), "\n".join(M) + "\n")
    w(os.path.join(proj, "代码", "排序.list.txt"), "程序集1\n")
    print("掩码 0x%02X -> %s" % (mask, proj))


if __name__ == "__main__":
    raise SystemExit(main())
