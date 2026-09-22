#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 skills\\ 下的技能装到本机各个 AI 编码工具的技能目录 —— 跨工具通用。

依据 **Agent Skills 开放规范**（agentskills.io）安装，不绑定任何一家产品：

  * **总是**装到 `~/.agents/skills/` —— 规范定义的共享根。DHS、Codex CLI、
    OpenCode、Cursor、Amp、Gemini CLI 等都会扫描它，一份文件多处生效。
  * **探测式补齐**：某工具的家目录存在，就同时装到它自己的技能目录。
    Claude Code 与 Trae 不读共享根，只能装各自目录，所以这一步不能省。

用法::

    python install-skills.py                  # 探测式安装（推荐）
    python install-skills.py --list           # 只看会装到哪，不动文件
    python install-skills.py --all            # 装到全部已知目录
    python install-skills.py --targets claude,trae
    python install-skills.py --project .      # 额外装进项目级目录
    python install-skills.py --dry-run        # 演练
    python install-skills.py --uninstall      # 移除本包技能

脚本零第三方依赖，Python 3.9+ 即可。Windows 上可双击同目录的 `安装技能.cmd`。
"""
from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE / "skills"
HOME = Path.home()

# key / 显示名 / 相对 $HOME 的技能目录 / 探测信号（$HOME 下的那个条目）/ 默认策略
GLOBAL_TARGETS = [
    ("agents", "Agent Skills 通用根（DHS/Codex/OpenCode/Cursor/Amp/Gemini…）",
     ".agents/skills", ".agents", "always"),
    ("claude", "Claude Code", ".claude/skills", ".claude", "probe"),
    ("codex", "OpenAI Codex CLI", ".codex/skills", ".codex", "probe"),
    ("dsh", "DeepSeek Harness (DSH)", ".dsh/skills", ".dsh", "probe"),
    ("trae", "Trae（国际版）", ".trae/skills", ".trae", "probe"),
    ("trae-cn", "Trae（国内版）", ".trae-cn/skills", ".trae-cn", "probe"),
    ("cursor", "Cursor", ".cursor/skills", ".cursor", "probe"),
    ("windsurf", "Windsurf", ".windsurf/skills", ".windsurf", "probe"),
    ("opencode", "OpenCode", ".config/opencode/skills", ".config/opencode", "probe"),
    ("workbuddy", "WorkBuddy", ".workbuddy/skills", ".workbuddy", "probe"),
]

# 项目级（相对项目根）；用 --project 时生效
PROJECT_TARGETS = [
    (".agents/skills", "Agent Skills 通用根"),
    (".claude/skills", "Claude Code"),
    (".codex/skills", "Codex CLI"),
    (".dsh/skills", "DeepSeek Harness"),
    (".opencode/skills", "OpenCode"),
    (".trae/skills", "Trae"),
    (".cursor/skills", "Cursor"),
    (".github/skills", "GitHub Copilot / VS Code"),
]

MARKET_META = "_skillhub_meta.json"   # 技能市场装的技能带这个标记，不去覆盖
# 支持库文档是 exe 的**生成物**，不该跟着技能到处跑（有人可能就地双击过 exe）
SKIP_NAMES = {"__pycache__", ".DS_Store", "Thumbs.db", "支持库文档"}
SKIP_EXT = {".pyc", ".pyo"}


# --------------------------------------------------------------------------- #
# 基础工具
# --------------------------------------------------------------------------- #
def say(msg=""):
    print(msg, flush=True)


def setup_stdout():
    """按输出目标自适应编码。

    * **真控制台**（双击 `安装技能.cmd` 的场景）：不动。Windows 上 Python 直接把
      宽字符写进控制台，与代码页/`chcp` 无关，中文一定正常。
    * **管道 / 重定向到文件**（AI、脚本消费）：统一转 UTF-8，结果可预测。
      不这么做的话，双击环境（无 `PYTHONUTF8`）会退化成 GBK 字节，
      而 AI 侧按 UTF-8 读就会乱码。
    """
    for s in (sys.stdout, sys.stderr):
        if not s.isatty():
            try:
                s.reconfigure(encoding="utf-8")
            except Exception:
                pass                      # 有的是 WindowsConsoleIO，没有 reconfigure
        try:
            s.reconfigure(errors="replace")   # 兜底：GBK 编不出的符号降级成 ? 而不是崩
        except Exception:
            pass


def scan_skills(src: Path):
    """返回 [(技能名, 目录)]，只认含 SKILL.md 的一级子目录。"""
    if not src.is_dir():
        return []
    out = []
    for d in sorted(src.iterdir()):
        if d.is_dir() and (d / "SKILL.md").is_file():
            out.append((d.name, d))
    return out


def md5_tree(root: Path):
    """目录内全部文件的 {相对路径: md5}，用于安装后逐文件比对。"""
    out = {}
    for dp, dns, fns in os.walk(root):
        dns[:] = [d for d in dns if d not in SKIP_NAMES]
        for fn in fns:
            if fn in SKIP_NAMES or Path(fn).suffix.lower() in SKIP_EXT:
                continue
            p = Path(dp) / fn
            rel = str(p.relative_to(root)).replace(os.sep, "/")
            out[rel] = hashlib.md5(p.read_bytes()).hexdigest()
    return out


def copy_tree(src: Path, dst: Path):
    """把技能目录同步到 dst：**非破坏式**覆盖 + 补齐 + 清理陈旧文件。

    以前是 `rmtree(dst)` 再整棵拷贝：每次安装都要先删掉目标整目录（几十个文件），
    既慢、又会触发系统/终端的「批量删除」保护。改成先 `copytree(覆盖+新增)`，
    再只删除「目标里有、源里没有」的少数陈旧文件 —— 结果目录与源逐文件一致，
    但正常情况下**零删除**。
    """
    def ignore(_dir, names):
        return [n for n in names if n in SKIP_NAMES or Path(n).suffix.lower() in SKIP_EXT]

    dst.mkdir(parents=True, exist_ok=True)
    shutil.copytree(src, dst, ignore=ignore, dirs_exist_ok=True)

    # 源目录里应当存在的相对路径集合
    keep = set()
    for dp, dns, fns in os.walk(src):
        dns[:] = [d for d in dns if d not in SKIP_NAMES]
        for f in fns:
            if f in SKIP_NAMES or Path(f).suffix.lower() in SKIP_EXT:
                continue
            keep.add(str((Path(dp) / f).relative_to(src)).replace(os.sep, "/"))

    # 删掉目标里多余的文件（源里已删除的），并清理空目录
    for dp, dns, fns in os.walk(dst, topdown=False):
        for f in fns:
            rel = str((Path(dp) / f).relative_to(dst)).replace(os.sep, "/")
            if rel not in keep:
                try:
                    os.remove(Path(dp) / f)
                except OSError:
                    pass
        for d in dns:
            try:
                os.rmdir(Path(dp) / d)
            except OSError:
                pass


# --------------------------------------------------------------------------- #
# 目标解析
# --------------------------------------------------------------------------- #
def global_targets(only=None, use_all=False):
    """返回 [(key, 显示名, 路径, 来源说明)]。"""
    picked = []
    for key, desc, rel, probe, policy in GLOBAL_TARGETS:
        if only is not None:
            if key not in only:
                continue
            why = "指定"
        elif use_all:
            why = "全部"
        elif policy == "always":
            why = "规范共享根"
        elif (HOME / probe).exists():
            why = "检测到工具"
        else:
            continue
        picked.append((key, desc, HOME / rel, why))
    return picked


def project_targets(proj: Path):
    """项目级：装到通用根 + 项目里已存在的各工具目录。"""
    picked = []
    for rel, desc in PROJECT_TARGETS:
        p = proj / rel
        if rel == ".agents/skills" or p.parent.exists():
            picked.append((rel, desc, p, "项目级"))
    return picked


# --------------------------------------------------------------------------- #
# 安装 / 卸载
# --------------------------------------------------------------------------- #
def install_into(dst_root: Path, skills, dry=False, verify=True):
    """把技能装进 dst_root，返回 (完成数, 跳过数, 失败数)。"""
    ok = skip = bad = 0
    dst_root.mkdir(parents=True, exist_ok=True)
    for name, src in skills:
        dst = dst_root / name
        if (dst / MARKET_META).exists():
            say(f"    [跳过] {name} —— 该技能来自技能市场，不覆盖")
            skip += 1
            continue
        if dry:
            say(f"    [演练] {name} → {dst}")
            ok += 1
            continue
        try:
            copy_tree(src, dst)
        except Exception as e:                                  # noqa: BLE001
            say(f"    [失败] {name}: {e}")
            bad += 1
            continue
        if verify and md5_tree(src) != md5_tree(dst):
            say(f"    [失败] {name}: 复制后校验不一致")
            bad += 1
            continue
        say(f"    [完成] {name}" + ("（已校验）" if verify else ""))
        ok += 1
    return ok, skip, bad


def uninstall_from(dst_root: Path, skills, dry=False):
    n = 0
    for name, _src in skills:
        dst = dst_root / name
        if not (dst / "SKILL.md").exists():
            continue
        if dry:
            say(f"    [演练] 移除 {dst}")
        else:
            try:
                shutil.rmtree(dst)
            except Exception as e:                              # noqa: BLE001
                say(f"    [失败] {name}: {e}")
                continue
            say(f"    [移除] {name}")
        n += 1
    return n


# --------------------------------------------------------------------------- #
# 入口
# --------------------------------------------------------------------------- #
def main():
    setup_stdout()
    ap = argparse.ArgumentParser(
        description="把本包技能装到本机各 AI 编码工具的技能目录（Agent Skills 开放规范）",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true", help="只列出目标与技能，不写文件")
    ap.add_argument("--all", action="store_true", help="装到全部已知目录（含未安装的工具）")
    ap.add_argument("--targets", help="只装这些目标，逗号分隔（见 --list）")
    ap.add_argument("--skills", help="只装这些技能，逗号分隔（默认全部）")
    ap.add_argument("--project", metavar="DIR", help="额外装进该项目的项目级技能目录")
    ap.add_argument("--dry-run", action="store_true", help="只打印打算做什么")
    ap.add_argument("--uninstall", action="store_true", help="从目标目录移除本包技能")
    ap.add_argument("--no-verify", action="store_true", help="安装后不逐文件校验")
    a = ap.parse_args()

    skills = scan_skills(SRC)
    if not skills:
        say(f"!! 在 {SRC} 下没找到任何含 SKILL.md 的技能目录")
        return 2

    if a.skills:
        want = {s.strip() for s in a.skills.split(",") if s.strip()}
        skills = [(n, p) for n, p in skills if n in want]
        if not skills:
            say(f"!! 没有匹配的技能: {a.skills}")
            return 2

    only = None
    if a.targets:
        only = {t.strip() for t in a.targets.split(",") if t.strip()}
        known = {k for k, *_ in GLOBAL_TARGETS}
        unknown = only - known
        if unknown:
            say(f"!! 未知目标: {', '.join(sorted(unknown))}（用 --list 看可用 key）")
            return 2

    targets = global_targets(only=only, use_all=a.all)

    say("=" * 66)
    say("  Elang-AiTools —— 跨工具安装技能" + ("（演练）" if a.dry_run else ""))
    say("=" * 66)
    say(f"技能源: {SRC}")
    say(f"技能:   " + "、".join(n for n, _ in skills))
    say()

    if a.list:
        say("可安装目标（* = 本次会装）:")
        for key, desc, rel, probe, policy in GLOBAL_TARGETS:
            mark = "*" if any(k == key for k, *_ in targets) else " "
            exists = "有" if (HOME / rel).exists() else "-"
            sig = "有" if (HOME / probe).exists() else "-"
            say(f"  {mark} {key:10s} {str(HOME / rel):<52s} 目录:{exists} 工具:{sig}")
            say(f"      └ {desc}")
        if a.project:
            say()
            say(f"项目级（{a.project}）:")
            for rel, desc, p, _ in project_targets(Path(a.project).resolve()):
                say(f"     {rel:22s} {desc}")
        return 0

    if not targets:
        say("没有可安装的目标。用 --all 强制全装，或 --targets 指定。")
        return 0

    ok = skip = bad = 0
    for key, desc, path, why in targets:
        say(f"  {desc}   [{why}]")
        say(f"    → {path}")
        if a.uninstall:
            uninstall_from(path, skills, dry=a.dry_run)
        else:
            o, s, b = install_into(path, skills, dry=a.dry_run, verify=not a.no_verify)
            ok, skip, bad = ok + o, skip + s, bad + b
        say()

    if a.project:
        proj = Path(a.project).resolve()
        if not proj.is_dir():
            say(f"!! 项目目录不存在: {proj}")
        else:
            say(f"  项目级安装: {proj}")
            for rel, desc, path, _why in project_targets(proj):
                say(f"    {desc} → {path}")
                if a.uninstall:
                    uninstall_from(path, skills, dry=a.dry_run)
                else:
                    o, s, b = install_into(path, skills, dry=a.dry_run, verify=not a.no_verify)
                    ok, skip, bad = ok + o, skip + s, bad + b
            say()

    say("-" * 66)
    if a.uninstall:
        say(f"完成：{ok} 处已移除。")
    else:
        say(f"完成：{ok} 处已安装，{skip} 处跳过，{bad} 处失败。")
        if not a.dry_run and bad == 0:
            say()
            say("下一步：在工具里新开一个对话，说「用易语言帮我写个程序」或提到 e2txt，")
            say("看技能是否被自动调起；也可用工具自己的技能列表命令确认（如 Codex 的 /skills）。")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
