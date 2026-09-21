#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""clean_stray_refs.py —— 一次性清理：把误同步进技能 references/ 的内部文档删掉。

起因：sync-release.sh 原来是 `cp -f docs/*.md`，把 docs/ 下的**工程内部工作文档**
（方案 / 逆向分析报告 / 开发工程说明）一并塞进了 `skills/elang-ai-coding/references/`，
而这些文档满是本机绝对路径与写死的统计数 —— 技能是要发给别人用的，不能带。

本脚本从「发布区 + 10 个安装目标」里删除这三个文件，只保留白名单内的规范全文。
改动只发生在 `Releases/` 与各安装目标的技能目录内。已改为显式名单，不会再复发。
"""
import os
import sys

HOME = os.path.expanduser("~")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 只该留这一个
ALLOW = {"易语言文本格式规范.md"}
# 被误同步进来的（删）
STRAY = ["分析-易语言主程序与官方扩展接口.md",
         "开发工程说明.md",
         "方案-无IDE运行调试易语言.md"]

TARGETS = [
    os.path.join(ROOT, "Releases", "Elang-AiTools", "skills"),
    os.path.join(HOME, ".agents", "skills"),
    os.path.join(HOME, ".claude", "skills"),
    os.path.join(HOME, ".codex", "skills"),
    os.path.join(HOME, ".dsh", "skills"),
    os.path.join(HOME, ".trae", "skills"),
    os.path.join(HOME, ".trae-cn", "skills"),
    os.path.join(HOME, ".cursor", "skills"),
    os.path.join(HOME, ".windsurf", "skills"),
    os.path.join(HOME, ".config", "opencode", "skills"),
    os.path.join(HOME, ".workbuddy", "skills"),
]


def main():
    removed = 0
    checked = 0
    for t in TARGETS:
        ref = os.path.join(t, "elang-ai-coding", "references")
        if not os.path.isdir(ref):
            continue
        checked += 1
        for name in STRAY:
            p = os.path.join(ref, name)
            if os.path.isfile(p):
                os.remove(p)
                removed += 1
                print("  删除 %s" % p)
        left = sorted(os.listdir(ref))
        extra = [n for n in left if n not in ALLOW]
        flag = "OK" if not extra else "!! 仍有非白名单文件: %s" % extra
        print("  保留 %s  ->  %s" % (os.path.dirname(ref), flag))
    print("\n扫描 %d 处 references/，删除 %d 个误同步文件。" % (checked, removed))
    return 0


if __name__ == "__main__":
    sys.exit(main())
