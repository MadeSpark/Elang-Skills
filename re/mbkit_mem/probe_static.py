#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""静态探针：判定易语言「子程序」与「.DLL命令」的参数压栈布局是否一致。

方法：同一个参数列表，分别
  ① 直接用 `.DLL命令` 调用     —— 参照物
  ② 先调用一个同签名的「易语言子程序」，该子程序内部再把参数原样转交同一个 `.DLL命令`
编译成 exe 后反汇编，比对两处 call site 的 push 序列，并读出子程序自身的
`ret N`（= 它的参数区总字节数）。

判据：
  · 两处 push 序列一致  -> 易语言子程序帧 == .DLL命令帧，可以「原样搬运参数」
  · 不一致             -> 必须按类型生成搬运机器码（那就同时读出处方）
"""
import os
import shutil
import sys

sys.stdout.reconfigure(encoding="utf-8")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SCAFFOLD = os.path.join(ROOT, "re", "opt", "e2e", "proj")
PROJ = os.path.join(HERE, "static")

BOM = "\ufeff"


def w(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write((BOM + text.replace("\n", "\r\n")).encode("utf-8"))


DLL = '''.版本 2

.DLL命令 _A_dummy, 整数型, "user32.dll", "GetDesktopWindow"
    .参数 a, 整数型
    .参数 b, 文本型
    .参数 c, 长整数型
    .参数 d, 双精度小数型
    .参数 e, 字节型

.DLL命令 _A_dummy2, 整数型, "user32.dll", "GetDesktopWindow"
    .参数 a, 字节集
    .参数 b, 整数型, 传址

.DLL命令 _A_dummy3, 整数型, "user32.dll", "GetDesktopWindow"
    .参数 a, 逻辑型
    .参数 b, 小数型
    .参数 c, 整数型

'''

MAIN = '''.版本 2

.程序集 程序集1

.子程序 _启动子程序, 整数型
    .局部变量 t, 文本型
    .局部变量 r, 整数型

    t ＝ “QQQQ”

    ' ① 参照物：直接 .DLL命令（哨兵：a=0x11111111  c=4294967297 d=1.5 e=68）
    r ＝ _A_dummy (286331153, “QQQQ”, 4294967297, 1.5, 68)
    ' ② 同签名易语言子程序（同一套哨兵）
    r ＝ W_dummy (286331153, “QQQQ”, 4294967297, 1.5, 68)
    ' ③ 字节集 + 传址
    r ＝ _A_dummy2 ({ 1, 2, 3 }, 286331153)
    r ＝ W_dummy2 ({ 1, 2, 3 }, 286331153)
    ' ④ 逻辑型 / 小数型
    r ＝ _A_dummy3 (真, 1.5, 286331153)
    r ＝ W_dummy3 (真, 1.5, 286331153)

    返回 (0)

.子程序 W_dummy, 整数型
    .参数 a, 整数型
    .参数 b, 文本型
    .参数 c, 长整数型
    .参数 d, 双精度小数型
    .参数 e, 字节型
    .局部变量 r, 整数型

    r ＝ _A_dummy (a, b, c, d, e)
    返回 (r)

.子程序 W_dummy2, 整数型
    .参数 a, 字节集
    .参数 b, 整数型, 传址
    .局部变量 r, 整数型

    r ＝ _A_dummy2 (a, b)
    返回 (r)

.子程序 W_dummy3, 整数型
    .参数 a, 逻辑型
    .参数 b, 小数型
    .参数 c, 整数型
    .局部变量 r, 整数型

    r ＝ _A_dummy3 (a, b, c)
    返回 (r)
'''


def main():
    if os.path.isdir(PROJ):
        shutil.rmtree(PROJ)
    shutil.copytree(SCAFFOLD, PROJ)
    # 去掉脚手架自带的资源，避免干扰
    res = os.path.join(PROJ, "资源")
    if os.path.isdir(res):
        shutil.rmtree(res)
    w(os.path.join(PROJ, "DLL.e.txt"), DLL)
    w(os.path.join(PROJ, "代码", "程序集1.static.e.txt"), MAIN)
    w(os.path.join(PROJ, "代码", "排序.list.txt"), "程序集1\n")
    print("写好:", PROJ)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
