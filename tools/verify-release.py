#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""发布区总检（只读，不改任何文件）。

    python tools/verify-release.py

检查七件事：

  1. 发布区只含成品（不该有 .venv / pyproject.toml / uv.lock / 生成物 / __pycache__）
  2. 技能符合 **Agent Skills 开放规范**：frontmatter 字段白名单、`name` 正则与目录一致、
     `description` 长度；正文不出现某家工具专有的工具名；技能**自包含**（查命令的 exe 与
     规范全文都在技能包内，且与工程侧源本逐字节一致）
  3. **全部安装点**（`~/.agents`、`~/.claude`、`~/.trae` …）与发布区逐文件 md5 一致
  4. 三个安装入口存在且 `安装技能.cmd` 是 GBK+CRLF（否则 cmd 里中文乱码）
  5. dist/ 里的技能分发包结构（zip 根即 SKILL.md）
  6. 文档里不再有失效的绝对路径
  7. **技能 / 安装器 / 使用说明里没有本机专有信息**（路径、统计数、模块版本、个人信息）

目标目录表直接取自 `install-skills.py`，本机信息规则取自 `scan-machineinfo.py`，
保证三处不会漂移。
"""
from __future__ import annotations

import hashlib
import importlib.util
import io
import os
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KIT = ROOT / "Releases" / "Elang-AiTools"
SKILLS_SRC = KIT / "skills"
DIST = ROOT / "dist"

# --- 规范（agentskills.io）-------------------------------------------------- #
NAME_RE = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")
ALLOWED_FM = {"name", "description", "license", "compatibility", "metadata"}
DESC_MAX = 1024
# 某家工具专有的工具名，出现在正文里会让别的工具困惑
# （"Git Bash" 是 shell 名，不算）
TOOLISH = re.compile(r"(?<!Git )\b(Bash|Read|Write|Edit|Grep|Glob|TodoWrite|WebFetch|WebSearch)\b")

PASS = FAIL = 0
FAILURES = []


def ok(msg):
    global PASS
    PASS += 1
    print(f"   [OK] {msg}")


def bad(msg):
    global FAIL
    FAIL += 1
    FAILURES.append(msg)
    print(f"   [!!] {msg}")


def head(n, title):
    print()
    print("=" * 66)
    print(f"{n}) {title}")
    print("=" * 66)


def snap(root: Path):
    out = {}
    if not root.is_dir():
        return out
    for dp, dn, fn in os.walk(root):
        dn[:] = [d for d in dn if d != "__pycache__"]
        for f in fn:
            if f.endswith((".pyc", ".pyo")):
                continue
            p = Path(dp) / f
            out[str(p.relative_to(root)).replace(os.sep, "/")] = \
                hashlib.md5(p.read_bytes()).hexdigest()
    return out


def load_installer():
    """导入 install-skills.py（文件名带连字符，只能走 importlib）。"""
    spec = importlib.util.spec_from_file_location("_install_skills", KIT / "install-skills.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def parse_frontmatter(p: Path):
    raw = p.read_bytes()
    s = raw.decode("utf-8")
    if not s.startswith("---\n"):
        return None, raw, "首行不是 ---"
    end = s.find("\n---\n", 4)
    if end < 0:
        return None, raw, "frontmatter 没有收尾 ---"
    fm = {}
    for line in s[4:end].splitlines():
        m = re.match(r"^([A-Za-z_][\w-]*):[ \t]*(.*)$", line)
        if m:
            fm[m.group(1)] = m.group(2).strip()
    return fm, raw, s[end + 5:]


# --------------------------------------------------------------------------- #
head(1, "发布区纯度（应只有成品）")
files = sorted(p for p in KIT.rglob("*") if p.is_file())
for p in files:
    print(f"   {str(p.relative_to(KIT)).replace(os.sep, '/'):42s} {p.stat().st_size:>8,} B")
for junk in (".venv", "pyproject.toml", "uv.lock", "支持库文档", "__pycache__", "uv.lock"):
    hits = [p for p in KIT.rglob("*") if p.name == junk]
    if hits:
        bad(f"发布区混进了 {junk}: {[str(h.relative_to(KIT)) for h in hits]}")
    else:
        ok(f"不含 {junk}")

head(2, "技能规范符合性（Agent Skills 开放规范）")
skill_dirs = sorted(d for d in SKILLS_SRC.iterdir() if d.is_dir() and (d / "SKILL.md").is_file())
if not skill_dirs:
    bad("skills/ 下没有找到任何技能")
for d in skill_dirs:
    md = d / "SKILL.md"
    fm, raw, body = parse_frontmatter(md)
    if fm is None:
        bad(f"{d.name}: {body}")
        continue
    name = fm.get("name", "")
    desc = fm.get("description", "")
    problems = []
    if not name:
        problems.append("缺 name")
    elif not NAME_RE.match(name):
        problems.append(f"name={name!r} 不符合 ^[a-z0-9]+(-[a-z0-9]+)*$")
    elif name != d.name:
        problems.append(f"name={name!r} 与目录名 {d.name!r} 不一致")
    if len(name) > 64:
        problems.append(f"name 超 64 字符（{len(name)}）")
    if not desc:
        problems.append("缺 description")
    elif len(desc) > DESC_MAX:
        problems.append(f"description 超 {DESC_MAX} 字符（{len(desc)}）")
    extra = set(fm) - ALLOWED_FM
    if extra:
        problems.append(f"含规范外字段 {sorted(extra)}（部分工具会报错，建议删掉）")
    if body and TOOLISH.search(body):
        problems.append(f"正文出现工具专有名词: {sorted(set(TOOLISH.findall(body)))}")
    if problems:
        bad(f"{d.name}: " + "; ".join(problems))
    else:
        ok(f"{d.name:18s} name 合规 / desc {len(desc):>4} 字符 / 字段 {sorted(fm)} / "
           f"{len(snap(d))} 文件")

# 技能必须**自包含**：查命令的 exe、规范全文都要在技能包里，且与工程侧源本一致。
# 否则技能装到别的 AI 工具目录后，正文引用的东西就不存在了（这正是曾经的坑）。
SELF_CONTAINED = [
    ("elang-ai-coding/assets/导出支持库文档.exe",
     ROOT / "导出支持库文档.exe"),
    ("elang-ai-coding/references/易语言文本格式规范.md",
     ROOT / "docs" / "易语言文本格式规范.md"),
]
for rel, src in SELF_CONTAINED:
    p = SKILLS_SRC / rel.replace("/", os.sep)
    if not p.is_file():
        bad(f"技能包缺少 {rel}（装到别的工具后就找不到了）")
        continue
    if not src.is_file():
        bad(f"工程侧源本缺失，无法比对: {src}")
        continue
    if p.read_bytes() != src.read_bytes():
        bad(f"{rel} 与工程侧源本不一致（跑 src/build.sh 或 tools/sync-release.sh 同步）")
    else:
        ok(f"{rel:46s} {p.stat().st_size:>9,} B  与工程侧源本一致")

head(3, "安装点一致性")
try:
    inst = load_installer()
    targets = inst.global_targets()
except Exception as e:                                        # noqa: BLE001
    bad(f"无法读取 install-skills.py 的目标表: {e}")
    targets = []

for key, desc, path, why in targets:
    for d in skill_dirs:
        a, b = snap(d), snap(path / d.name)
        if not b:
            bad(f"{key}/{d.name}: 未安装（{path / d.name}）")
        elif a != b:
            bad(f"{key}/{d.name}: 与发布区不一致")
        else:
            ok(f"{key:9s}/{d.name:18s} {len(b)} 文件一致")

head(4, "安装入口")
for f, need in (("install-skills.py", "utf-8"), ("install-skills.sh", "utf-8"),
                ("安装技能.cmd", "gbk")):
    p = KIT / f
    if not p.is_file():
        bad(f"缺少 {f}")
        continue
    raw = p.read_bytes()
    if need == "gbk":
        try:
            raw.decode("gbk")
            note = "GBK 可解码"
        except Exception:                                     # noqa: BLE001
            bad(f"{f}: 不是 GBK（cmd 里中文会乱码）")
            continue
        if raw[:3] == b"\xef\xbb\xbf":
            bad(f"{f}: 带 UTF-8 BOM（cmd 会报错）")
            continue
        if raw.count(b"\n") != raw.count(b"\r\n"):
            bad(f"{f}: 不是纯 CRLF")
            continue
        ok(f"{f:20s} {len(raw):>6,} B  {note} + CRLF + 无 BOM")
    else:
        try:
            raw.decode("utf-8")
        except Exception:                                     # noqa: BLE001
            bad(f"{f}: 不是合法 UTF-8")
            continue
        ok(f"{f:20s} {len(raw):>6,} B  UTF-8")

head(5, "技能分发包 dist/")
if not DIST.is_dir():
    print("   （没有 dist/，跳过 —— 跑 python tools/pack-skill.py 生成）")
for z in sorted(DIST.glob("*.zip")):
    with zipfile.ZipFile(z) as f:
        names = f.namelist()
    if names and names[0] == "SKILL.md":
        ok(f"{z.name:26s} {z.stat().st_size:>7,} B  根=SKILL.md  共 {len(names)} 项")
    else:
        bad(f"{z.name}: zip 根不是 SKILL.md（首个是 {names[0] if names else '空包'}）")

head(6, "文档里的失效路径")
DOCS = ["README.md",
        "Releases/Elang-AiTools/使用说明.md",
        "Releases/Elang-AiTools/skills/elang-ai-coding/SKILL.md",
        "Releases/Elang-AiTools/skills/e2txt-cli/SKILL.md",
        "docs/易语言文本格式规范.md",
        "Releases/Elang-AiTools/skills/elang-ai-coding/references/易语言文本格式规范.md"]
PAT = re.compile(
    r"elang-ai/"                        # 已废弃的旧开发目录
    r"|%USERPROFILE%[\\/]\.workbuddy"    # 写死的安装绝对路径
    r"|<KIT>"                            # 已废弃的工具箱占位符
    r"|Elang-AiTools[/\\]docs[/\\]"      # 规范已移进技能包 references/
    r"|workbuddy[/\\]skills[/\\]elang-ai-coding")
for rel in DOCS:
    p = ROOT / rel
    if not p.is_file():
        bad(f"缺失 {rel}")
        continue
    lines = io.open(p, encoding="utf-8", errors="replace").read().splitlines()
    hits = [(i + 1, l.strip()[:80]) for i, l in enumerate(lines) if PAT.search(l)]
    if hits:
        for i, l in hits:
            bad(f"{rel}:{i}  {l}")
    else:
        ok(f"{rel}  无残留")

head(7, "技能 / 安装器 / 使用说明里的本机专有信息")
try:
    spec = importlib.util.spec_from_file_location(
        "_scan_machineinfo", ROOT / "tools" / "scan-machineinfo.py")
    smi = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(smi)
    hits = smi.scan(smi.default_targets())
except Exception as e:                                        # noqa: BLE001
    bad(f"无法加载 tools/scan-machineinfo.py: {e}")
    hits = []
for f, line, label, text in hits:
    bad(f"{os.path.relpath(f, ROOT)}:{line} [{label}] {text}")
if not hits:
    ok("无本机路径 / 统计数 / 模块版本 / 个人信息（技能可以原样发给任何人）")

print()
print("-" * 66)
print(f"结果：{PASS} 项通过，{FAIL} 项失败")
if FAILURES:
    print()
    for m in FAILURES:
        print(f"  !! {m}")
print("-" * 66)
sys.exit(1 if FAIL else 0)
