#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
e2e_setup.py —— 搭建「MbKit 内存载入 + 自定义 node 路径」端到端验证工程

被验证的三条链路（全部在真实易语言编译产物上跑）：
  ① 内嵌资源 → 写到宿主程序目录 → LoadLibraryA(完整路径) 预载
  ② `.DLL命令` 库名写**裸名** "MbKit.dll" → 命中已预载的那一份（按基名复用）
  ③ MbKit 从**自定义目录**加载 node.dll（WKE设置node路径 = MbKit_SetNodePath）

探针程序只做一件事：把每一步结果追加进 <exe目录>\probe.log，同时打 stdout。
用环境变量 MBKIT_TEST_NODE 驱动：
  · 设了   → 走「自定义路径」分支（用例 A）
  · 没设   → 走默认分支（宿主程序根目录）（用例 B）
"""
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
E2E = os.path.join(HERE, "e2e")
PROJ = os.path.join(E2E, "proj")
DLL_BIN = os.path.abspath(os.path.join(HERE, "..", "..", "src", "mbkit", "MbKit.dll"))
NODE_SRC = os.path.abspath(os.path.join(HERE, "..", "..", "main", "corn", "node.dll"))

sys.stdout.reconfigure(encoding="utf-8")
BOM = "\ufeff"

# ---------------------------------------------------------------------------
# 探针主体
# ---------------------------------------------------------------------------
PROG = """\ufeff.版本 2

.程序集 程序集1
.程序集变量 _日志, 文本型

.子程序 _启动子程序, 整数型
    .局部变量 运行目录, 文本型
    .局部变量 目标, 文本型
    .局部变量 句柄, 整数型
    .局部变量 自定义, 文本型

    _日志 ＝ “”
    运行目录 ＝ 取运行目录 ()
    报告 (“01 启动；宿主程序目录 = ” ＋ 运行目录)
    报告 (“02 内嵌 MbKit 资源长度 = ” ＋ 到文本 (取字节集长度 (#MbKit)))
    目标 ＝ 运行目录 ＋ “\\MbKit.dll”
    报告 (“03 释放内嵌宿主库 -> ” ＋ 目标 ＋ “ ；写到文件 = ” ＋ 到文本 (写到文件 (目标, #MbKit)))
    报告 (“04 落盘文件尺寸 = ” ＋ 到文本 (取文件尺寸 (目标)))
    句柄 ＝ _P_LoadLibraryA (目标)
    报告 (“05 LoadLibraryA(完整路径) 句柄 = ” ＋ 到文本 (句柄))
    报告 (“06 [裸名命令] 预载后自检：”)
    报告 (指针到文本 (_P_Diag ()))
    自定义 ＝ 读环境变量 (“MBKIT_TEST_NODE”)
    报告 (“07 环境变量 MBKIT_TEST_NODE = [” ＋ 自定义 ＋ “]”)
    .如果真 (自定义 ≠ “”)
        报告 (“08 SetNodePath 返回 = ” ＋ 到文本 (_P_SetNodePath (自定义)))
    .如果真结束
    报告 (“09 配置回读 ConfigNodePath = [” ＋ 指针到文本 (_P_ConfigNodePath ()) ＋ “]”)
    报告 (“10 加载前 KernelLoaded = ” ＋ 到文本 (_P_KernelLoaded ()))
    报告 (“11 [裸名命令] wkeIsInitialize = ” ＋ 到文本 (_P_wkeIsInitialize ()))
    _P_wkeInitialize ()
    报告 (“12 已调用 wkeInitialize；wkeIsInitialize = ” ＋ 到文本 (_P_wkeIsInitialize ()))
    报告 (“13 [裸名命令] wkeGetVersion = ” ＋ 到文本 (_P_wkeGetVersion ()))
    报告 (“14 KernelPath = ” ＋ 指针到文本 (_P_KernelPath ()))
    报告 (“15 加载后自检：”)
    报告 (指针到文本 (_P_Diag ()))
    报告 (“== 探测结束 ==”)
    返回 (0)

.子程序 报告
    .参数 参数_文本, 文本型

    _日志 ＝ _日志 ＋ 参数_文本 ＋ 字符 (13) ＋ 字符 (10)
    写到文件 (取运行目录 () ＋ “\\probe.log”, 到字节集 (_日志))
    标准输出 (1, 参数_文本)
"""

DLLCMD = """\ufeff.版本 2

.DLL命令 _P_LoadLibraryA, 整数型, "kernel32.dll", "LoadLibraryA"
    .参数 _参数1, 文本型

.DLL命令 _P_SetNodePath, 整数型, "MbKit.dll", "MbKit_SetNodePath"
    .参数 _参数1, 文本型

.DLL命令 _P_ConfigNodePath, 整数型, "MbKit.dll", "MbKit_ConfigNodePath"

.DLL命令 _P_KernelPath, 整数型, "MbKit.dll", "MbKit_KernelPath"

.DLL命令 _P_KernelLoaded, 整数型, "MbKit.dll", "MbKit_KernelLoaded"

.DLL命令 _P_Diag, 整数型, "MbKit.dll", "MbKit_Diag"

.DLL命令 _P_wkeIsInitialize, 字节型, "MbKit.dll", "@wkeIsInitialize"

.DLL命令 _P_wkeInitialize, , "MbKit.dll", "@wkeInitialize"

.DLL命令 _P_wkeGetVersion, 整数型, "MbKit.dll", "@wkeGetVersion"
"""

CONST = "\ufeff.版本 2\n\n.常量 应用名称, \"“MbKit 内存载入探针”\"\n"
LIST = "\ufeff程序集1\n"

ETPRJ = {
    "AsyncFile": False,
    "E2TXT-EFile": "",
    "E2TXT-InSourceDir": False,
    "E2TXT-IsCreateE": False,
    "Encoding": "UTF-8",
    "Level": 1,
    "NameStyle": 2,
    "Password": "",
    "ResetNames": [],
    "Source": "代码.e",
    "TXT2E-CreateLog": False,
    "TXT2E-EFile": "",
    "TXT2E-InSourceDir": False,
}

RES_DESC = {"MbKit": {"Comment": "MbKit 宿主层（内嵌验证）", "IsPublic": False}}


def w(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(text.replace("\n", "\r\n").encode("utf-8"))


def main():
    if os.path.isdir(PROJ):
        shutil.rmtree(PROJ)
    w(os.path.join(PROJ, "代码", "程序集1.static.e.txt"), PROG)
    w(os.path.join(PROJ, "代码", "排序.list.txt"), LIST)
    w(os.path.join(PROJ, "DLL.e.txt"), DLLCMD)
    w(os.path.join(PROJ, "常量.e.txt"), CONST)
    with open(os.path.join(PROJ, "项目.etprj"), "w", encoding="gbk") as f:
        json.dump(ETPRJ, f, indent=4, ensure_ascii=False)

    src_cfg = os.path.join(HERE, "timing", "proj", "配置")
    shutil.copytree(src_cfg, os.path.join(PROJ, "配置"))

    res_dir = os.path.join(PROJ, "资源")
    os.makedirs(res_dir, exist_ok=True)
    shutil.copyfile(DLL_BIN, os.path.join(res_dir, "MbKit.image.dat"))
    with open(os.path.join(res_dir, "资源.desc.json"), "wb") as f:
        f.write((BOM + json.dumps(RES_DESC, indent=4, ensure_ascii=False)).encode("utf-8"))

    # 用例目录（exe 编译出来后由 run 脚本投放）
    for d in ("A", "B"):
        os.makedirs(os.path.join(E2E, d), exist_ok=True)
    # 用例 A 的自定义内核目录
    kx = os.path.join(E2E, "kernel_x")
    os.makedirs(kx, exist_ok=True)
    dst = os.path.join(kx, "node.dll")
    if not os.path.exists(dst):
        shutil.copyfile(NODE_SRC, dst)
    # 用例 B 的宿主程序根目录里放内核
    dstb = os.path.join(E2E, "B", "node.dll")
    if not os.path.exists(dstb):
        shutil.copyfile(NODE_SRC, dstb)

    print("探针工程：%s" % PROJ)
    for dp, dn, fn in os.walk(PROJ):
        for f in sorted(fn):
            p = os.path.join(dp, f)
            print("  %9d  %s" % (os.path.getsize(p), os.path.relpath(p, E2E)))
    print("用例 A 自定义内核目录：%s  (node.dll %d B)" % (kx, os.path.getsize(dst)))
    print("用例 B 宿主程序根目录：%s  (node.dll %d B)" % (os.path.join(E2E, "B"), os.path.getsize(dstb)))


if __name__ == "__main__":
    main()
