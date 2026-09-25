#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""static_lint.py —— 不进 e.exe 的静态自检（廉价、零副作用）

检查项：
  1) 每条语句行圆括号平衡（跳过声明行 / 置入代码 / 注释）
  2) 被调用的 `名 (` 是否都有声明（子程序 / .DLL命令 / 局部变量 / 程序集变量 / 参数
     / 白名单里的 krlnln 内置命令）
  3) 声明重名
  4) 是否残留已被删掉的旧 DLL 命令引用
用法：python static_lint.py <工程目录>
"""
import io
import os
import re
import sys

CN = "“”"
KRNLN = {  # 本工程用到的系统核心支持库命令（白名单）
    "写到文件", "取运行目录", "到字节集", "字符", "取字节集长度", "到文本",
    "取十六进制文本", "指针到文本", "取字节集数据", "取数组成员数", "读环境变量",
    "标准输出", "置入代码", "取文本长度", "取变量地址", "延时", "处理事件",
    "取启动时间", "取现行时间", "输出调试文本", "调试输出", "四舍五入",
    "取绝对值", "取整", "求次方", "取文本中间", "取文本左边", "取文本右边",
    "寻找文本", "替换文本", "分割文本", "数组加入成员", "清除数组", "重定义数组",
    "复制数组", "取数组下标", "交换变量", "取空白文本", "取重复文本", "到数值",
    "到整数", "取符号", "取代码", "取文本", "比较文本", "取字节集左边",
    "取字节集右边", "取字节集中间", "字节集替换", "取字节集长度2",
}

DECL_SUB = re.compile(r"^\.子程序\s+([^,\s]+)")
DECL_DLL = re.compile(r"^\.DLL命令\s+([^,\s]+)")
DECL_VAR = re.compile(r"^\s*\.(?:局部变量|程序集变量|参数|全局变量)\s+([^,\s]+)")
DECL_GLOBAL = re.compile(r"^\s*\.程序集变量\s+([^,\s]+)")
CALL = re.compile(r"([\u4e00-\u9fffA-Za-z_][\u4e00-\u9fffA-Za-z0-9_]*)\s*\(")


def strip_comments(line):
    """去掉整行注释与半角 ' 之后的注释（易语言注释是 '）"""
    s = line.strip()
    if s.startswith("'"):
        return ""
    if s.startswith("."):
        return ""      # 声明行不参与调用检查
    return line


def main():
    proj = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "demo_proj")
    files = []
    for root, _, fs in os.walk(proj):
        for f in fs:
            if f.endswith(".e.txt") and "静态" not in f:
                files.append(os.path.join(root, f))
    if not files:
        print("没找到 .e.txt"); return 2

    text_all = ""
    for f in sorted(files):
        t = io.open(f, encoding="utf-8-sig", errors="replace").read().replace("\r\n", "\n")
        text_all += "\n" + t

    declared, dup, globals_seen = set(), [], set()
    for ln in text_all.split("\n"):
        m = DECL_SUB.match(ln)
        if m:
            n = m.group(1)
            if n in declared:
                dup.append(n)
            declared.add(n)
            continue
        m = DECL_DLL.match(ln)
        if m:
            n = m.group(1)
            if n in declared:
                dup.append(n)
            declared.add(n)
            continue
        m = DECL_VAR.match(ln)
        if m:
            n = m.group(1).strip()
            declared.add(n)
            # ★ 只有「程序集变量」是同一作用域，重名才算错；
            #   参数/局部变量各自独立作用域，跨子程序重名是正常写法。
            if DECL_GLOBAL.match(ln):
                if n in globals_seen:
                    dup.append(n)
                globals_seen.add(n)
            continue

    errs = []
    # 1) 括号平衡
    n_stmt = 0
    for f in sorted(files):
        for i, ln in enumerate(io.open(f, encoding="utf-8-sig",
                                       errors="replace").read()
                               .replace("\r\n", "\n").split("\n"), 1):
            s = strip_comments(ln).strip()
            if not s or "置入代码" in s:
                continue
            n_stmt += 1
            # 去掉字符串字面量再数括号（易语言字符串用全角引号）
            body = re.sub(r"“[^”]*”", "S", s)
            if body.count("(") != body.count(")"):
                errs.append("括号不平衡  %s:%d  %s" % (os.path.basename(f), i, s[:130]))

    # 2) 调用是否已声明
    for f in sorted(files):
        for i, ln in enumerate(io.open(f, encoding="utf-8-sig",
                                       errors="replace").read()
                               .replace("\r\n", "\n").split("\n"), 1):
            s = strip_comments(ln)
            if not s or "置入代码" in s:
                continue
            body = re.sub(r"“[^”]*”", "S", s)
            for name in CALL.findall(body):
                if name in declared or name in KRNLN:
                    continue
                if name in ("如果", "如果真", "判断", "判断循环", "计次循环",
                            "循环", "返回", "结束", "跳出循环", "到循环尾",
                            "如果真结束", "如果结束", "判断结束", "计次循环首",
                            "计次循环尾", "判断循环首", "判断循环尾"):
                    continue
                errs.append("未声明就调用  %s:%d  %s   ← %s" % (os.path.basename(f), i, name, s.strip()[:90]))

    # 3) 旧命令残留
    for old in ("_MBCW", "_RCW", "_CWEW", "_LCT"):
        if re.search(r"(?<![\w\u4e00-\u9fff])%s\s*\(" % old, text_all):
            errs.append("残留已删除的旧 DLL 命令调用：%s" % old)

    # 4) ★ 名字恰好撞易语言关键字（实测会让 e.exe 解析阶段 0xC0000005 崩溃）
    RESERVED = {"参数", "窗口", "菜单", "类型", "实例", "命令", "方法", "事件",
                "属性", "组件", "对象", "位置", "尺寸", "可视", "禁止"}
    for f in sorted(files):
        for i, ln in enumerate(io.open(f, encoding="utf-8-sig",
                                       errors="replace").read()
                               .replace("\r\n", "\n").split("\n"), 1):
            m = re.match(r"^\s*\.(?:参数|局部变量|程序集变量|全局变量|子程序|DLL命令)\s+([^,\s]+)", ln)
            if not m:
                continue
            n = m.group(1).strip()
            if n in RESERVED:
                errs.append("名字撞关键字（会让 e.exe 崩）  %s:%d  %s" % (os.path.basename(f), i, n))

    print("扫描 %d 个文本文件；声明 %d 个名字；语句行 %d 条" % (len(files), len(declared), n_stmt))
    if dup:
        print("\n⚠️ 重名 %d 个：%s" % (len(dup), ", ".join(sorted(set(dup))[:20])))
    if errs:
        print("\n❌ 问题 %d 条：" % len(errs))
        for e in errs[:60]:
            print("   " + e)
        return 1
    print("\n✅ 静态自检全部通过")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
