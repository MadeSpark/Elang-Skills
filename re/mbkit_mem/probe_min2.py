#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""第二组最小探针：定位「无返回类型子程序 + 返回(0)」以及「参考参数」各自是否致命。

用法: probe_min2.py <变体名>
变体：
  noret-ret     无返回类型子程序，体内写 返回(0)，无参考参数
  noret-noret   无返回类型子程序，体内不写返回，无参考参数
  ref-ret       无返回类型子程序，参考参数 + 体内写 返回(0)
  ref-noret     无返回类型子程序，参考参数 + 体内不写返回
  int-ret       整数型返回子程序，参考参数 + 体内写 返回(0)
"""
import os
import shutil
import sys

sys.stdout.reconfigure(encoding="utf-8")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SCAFFOLD = os.path.join(ROOT, "re", "opt", "e2e", "proj")
BOM = "\ufeff"

HEAD = [
    '.版本 2',
    '',
    '.程序集 程序集1',
    '.子程序 _启动子程序, 整数型',
    '    .局部变量 文本, 文本型',
    '    .局部变量 值, 整数型',
    '',
    '    值 ＝ 12345',
]
TAIL = [
    '    文本 ＝ 文本 ＋ “ 值 ＝ ” ＋ 到文本 (值)',
    '',
    '    写到文件 (取运行目录 () ＋ “/min2_run.txt”, 到字节集 (文本))',
    '    返回 (0)',
    '',
]

VARIANTS = {
    "noret-ret": (['    W_空 ()'], ['.子程序 W_空', '', '    返回 (0)', '']),
    "noret-noret": (['    W_空 ()'], ['.子程序 W_空', '']),
    "ref-ret": (['    W_空 (值)'], ['.子程序 W_空', '    .参数 目标, 整数型, 参考', '', '    返回 (0)', '']),
    "ref-noret": (['    W_空 (值)'], ['.子程序 W_空', '    .参数 目标, 整数型, 参考', '']),
    "int-ret": (['    W_空 (值)'], ['.子程序 W_空, 整数型', '    .参数 目标, 整数型, 参考', '', '    返回 (0)', '']),
    "int-body-ret": (['    值 ＝ W_空 (值)'], ['.子程序 W_空, 整数型', '    .参数 目标, 整数型', '',
                      '    置入代码 ({ 139, 69, 8, 139, 229, 93, 194, 4, 0 })', '    返回 (0)', '']),
    "int-body-noret": (['    值 ＝ W_空 (值)'], ['.子程序 W_空, 整数型', '    .参数 目标, 整数型', '',
                        '    置入代码 ({ 139, 69, 8, 139, 229, 93, 194, 4, 0 })', '']),
    "void-body-noret": (['    W_空 (值)'], ['.子程序 W_空', '    .参数 目标, 整数型', '',
                         '    置入代码 ({ 139, 229, 93, 194, 4, 0 })', '']),
}


def main():
    name = sys.argv[1]
    call, sub = VARIANTS[name]
    proj = os.path.join(HERE, "p2_" + name)
    if os.path.isdir(proj):
        shutil.rmtree(proj)
    shutil.copytree(SCAFFOLD, proj)
    res = os.path.join(proj, "资源")
    if os.path.isdir(res):
        shutil.rmtree(res)
    text = "\n".join(HEAD + call + TAIL + sub) + "\n"
    with open(os.path.join(proj, "代码", "程序集1.static.e.txt"), "wb") as f:
        f.write((BOM + text.replace("\n", "\r\n")).encode("utf-8"))
    with open(os.path.join(proj, "代码", "排序.list.txt"), "wb") as f:
        f.write((BOM + "程序集1\r\n").encode("utf-8"))
    print("变体 %s -> %s" % (name, proj))
    print("-" * 40)
    print(text)


if __name__ == "__main__":
    raise SystemExit(main())
