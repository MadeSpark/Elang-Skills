#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""发布区总检（只读，不改任何文件）。

    python tools/verify-release.py

检查八件事：

  1. 发布区只含成品（不该有 .venv / pyproject.toml / uv.lock / 生成物 / __pycache__）
  2. 技能符合 **Agent Skills 开放规范**：frontmatter 字段白名单、`name` 正则与目录一致、
     `description` 长度；正文不出现某家工具专有的工具名；技能**自包含**（查命令的 exe 与
     规范全文都在技能包内，且与工程侧源本逐字节一致）
  3. **全部安装点**（`~/.agents`、`~/.claude`、`~/.trae` …）与发布区逐文件 md5 一致
  4. 三个安装入口存在且 `安装技能.cmd` 是 GBK+CRLF（否则 cmd 里中文乱码）
  5. dist/ 里的技能分发包结构（zip 根即 SKILL.md）
  6. 文档里不再有失效的绝对路径；技能包里不再出现已退役的转换工具 e2txt
  7. **技能 / 安装器 / 使用说明里没有本机专有信息**（路径、统计数、模块版本、个人信息）
  8. **跨技能运行期依赖**：技能的 `scripts/*.py` 若去读同级技能的目录/资产，那个技能
     就成了运行期硬依赖（单独分发必然缺件）。要求它登记在白名单内，**且**在消费者的
     SKILL.md 正文里声明过同装。

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
import tempfile
import zipfile
from pathlib import Path

# 本脚本会用 importlib 从发布区/工具目录导入模块（install-skills.py、scan-machineinfo.py、
# pack-skill.py），默认会在那些目录里写 __pycache__/*.pyc —— 而第 1 项检查恰恰就是
# 「发布区混进 __pycache__」。等于**每次总检都给下一次总检埋一个失败**（只有当外部恰好
# 设了 PYTHONDONTWRITEBYTECODE 时才看不出来）。这里主动关掉字节码写入，根治。
sys.dont_write_bytecode = True

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


def md5_of(p: Path) -> str:
    return hashlib.md5(Path(p).read_bytes()).hexdigest()


def zip_content(p: Path) -> dict:
    """zip 的**内容指纹**：条目名 -> 解压后内容的 md5。

    刻意不比对 zip 自身字节：① 内嵌 mtime 会随 cp/git checkout 变化；
    ② 压缩器实现不同（zlib vs zlib-ng）压出的 deflate 流不同，CRC 相同但长度不同。
    两者都会让「内容完全一致」的包字节不同，所以只比内容。
    """
    out = {}
    with zipfile.ZipFile(p) as z:
        for n in z.namelist():
            out[n] = hashlib.md5(z.read(n)).hexdigest()
    return out


def load_module(path: Path, name: str):
    """按文件路径导入模块（文件名带连字符，没法直接 import）。"""
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


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
for junk in (".venv", "pyproject.toml", "uv.lock", "支持库文档", "__pycache__"):
    hits = [p for p in KIT.rglob("*") if p.name == junk]
    if hits:
        paths = " ".join(str(h) for h in hits)
        bad(f"发布区混进了 {junk}: {[str(h.relative_to(KIT)) for h in hits]} —— 直接删: rm -rf {paths}")
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
    ("elang-ai-coding/references/置入代码与机器码.md",
     ROOT / "docs" / "置入代码与机器码.md"),
    ("elang-ai-coding/references/Win32与DLL互操作.md",
     ROOT / "docs" / "Win32与DLL互操作.md"),
    ("elang-ai-coding/references/程序入口与编译验证.md",
     ROOT / "docs" / "程序入口与编译验证.md"),
    ("elang-ai-coding/references/黑月编译与黑月界面类.md",
     ROOT / "docs" / "黑月编译与黑月界面类.md"),
    ("elang-debug/assets/elang_addin.fne",
     ROOT / "src" / "addin" / "elang_addin.fne"),
    ("elang-debug/assets/elang_addin.libinfo.txt",
     ROOT / "src" / "addin" / "elang_addin.libinfo.txt"),
]
# 脚本真源都在 tools/，由 sync-release.sh 按技能分发（与它的 SKILLS 表保持一致）
SCRIPTS = {
    "elang-ai-coding": ("mkproj.py", "rtcheck.py", "efix.py", "echeck.py", "elibs.py", "etool.py"),
    "elang-debug": ("mkcage.py", "elaunch.py", "readlog.py", "ec_guard.py", "etool.py"),
    "elang-build": ("ecompile.py", "ec_guard.py", "etool.py"),
}
for _sk, _names in SCRIPTS.items():
    for _n in _names:
        SELF_CONTAINED.append((f"{_sk}/scripts/{_n}", ROOT / "tools" / _n))
# QAQ-ETool 的 3 个 exe：源在 QAQ-ETool/dist/（独立工程，可能没检出）。没有源时只查技能包里有。
QAQ_DIST = ROOT / "QAQ-ETool" / "dist"
for _n in ("QAQ-ETool.exe", "qaq-etool-gui.exe", "qaq-libinfo.exe"):
    SELF_CONTAINED.append((f"qaq-etool-cli/assets/{_n}", QAQ_DIST / _n))
for rel, src in SELF_CONTAINED:
    p = SKILLS_SRC / rel.replace("/", os.sep)
    if not p.is_file():
        bad(f"技能包缺少 {rel}（装到别的工具后就找不到了）")
        continue
    if not src.is_file():
        if src.parent == QAQ_DIST:
            ok(f"{rel:46s} {p.stat().st_size:>9,} B  存在（没有 QAQ-ETool/dist，跳过比对）")
        else:
            bad(f"工程侧源本缺失，无法比对: {src}")
        continue
    if p.read_bytes() != src.read_bytes():
        bad(f"{rel} 与工程侧源本不一致（跑 src/build.sh 或 tools/sync-release.sh 同步）")
    else:
        ok(f"{rel:46s} {p.stat().st_size:>9,} B  与工程侧源本一致")

# references/ 只放**给用户看的成品文档**；工程内部工作文档（方案、逆向分析报告、
# 开发工程说明）满是本机路径与统计数，绝不能随技能发出去。sync-release.sh 已从
# `docs/*.md` 改成显式名单，这里再加一道闸门 **按技能** 防止退化。
REF_ALLOW = {
    "elang-ai-coding": {
        "易语言文本格式规范.md",
        "置入代码与机器码.md",
        "Win32与DLL互操作.md",
        "程序入口与编译验证.md",
        "黑月编译与黑月界面类.md",
    },
    "elang-build": set(),
    "qaq-etool-cli": set(),
    "elang-debug": {
        "加壳流程.md",
        "接口与调用约定清单.md",
        "踩坑清单.md",
    },
}
for d in sorted(SKILLS_SRC.iterdir()):
    ref = d / "references"
    if not ref.is_dir():
        continue
    allow = REF_ALLOW.get(d.name, set())
    extra = sorted(p.name for p in ref.iterdir()
                   if p.is_file() and p.name not in allow)
    if extra:
        bad(f"{d.name}/references/ 混进非白名单文件: {extra}（内部文档不要随技能发布）")
    else:
        ok(f"{d.name}/references/ 仅含白名单 {sorted(allow)}")

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
else:
    for z in sorted(DIST.glob("*.zip")):
        with zipfile.ZipFile(z) as f:
            names = f.namelist()
        if names and names[0] == "SKILL.md":
            ok(f"{z.name:26s} {z.stat().st_size:>7,} B  根=SKILL.md  共 {len(names)} 项")
        else:
            bad(f"{z.name}: zip 根不是 SKILL.md（首个是 {names[0] if names else '空包'}）")

    # 分发包必须**就是**当前发布区打出来的那个包。曾经的坑：发布区被整目录忽略，
    # git 里的 zip 与发布区内容悄悄漂移，而 git 完全看不见。这里在临时目录重打一遍，
    # 比对**条目名 + 解压内容 md5**（不比字节：mtime 与压缩器实现都会让字节变化，
    # 见 zip_content 的说明）。
    try:
        pk = load_module(ROOT / "tools" / "pack-skill.py", "_pack_skill")
    except Exception as e:                                        # noqa: BLE001
        bad(f"无法加载 tools/pack-skill.py: {e}")
        pk = None
    if pk is not None:
        with tempfile.TemporaryDirectory() as td:
            for d in skill_dirs:
                want = DIST / f"{d.name}.zip"
                fresh, n = pk.zip_skill(str(d), td)
                if not want.is_file():
                    bad(f"缺 dist/{d.name}.zip（跑 python tools/pack-skill.py 重新打包）")
                elif zip_content(fresh) != zip_content(want):
                    bad(f"dist/{d.name}.zip 内容与发布区 skills/{d.name}/ 不一致 —— "
                        f"重跑 python tools/pack-skill.py（技能改过没同步或没重打包）")
                else:
                    ok(f"dist/{d.name}.zip 内容与「按当前发布区重新打包」一致（{n} 项）")

head(6, "文档里的失效路径")
DOCS = ["README.md",
        "Releases/Elang-AiTools/使用说明.md",
        "Releases/Elang-AiTools/skills/elang-ai-coding/SKILL.md",
        "Releases/Elang-AiTools/skills/qaq-etool-cli/SKILL.md",
        "Releases/Elang-AiTools/skills/elang-debug/SKILL.md",
        "Releases/Elang-AiTools/skills/elang-build/SKILL.md",
        "Releases/Elang-AiTools/skills/elang-debug/references/加壳流程.md",
        "docs/易语言文本格式规范.md",
        "docs/置入代码与机器码.md",
        "docs/Win32与DLL互操作.md",
        "docs/程序入口与编译验证.md",
        "docs/黑月编译与黑月界面类.md",
        "Releases/Elang-AiTools/skills/elang-ai-coding/references/易语言文本格式规范.md",
        "Releases/Elang-AiTools/skills/elang-ai-coding/references/置入代码与机器码.md",
        "Releases/Elang-AiTools/skills/elang-ai-coding/references/Win32与DLL互操作.md",
        "Releases/Elang-AiTools/skills/elang-ai-coding/references/程序入口与编译验证.md",
        "Releases/Elang-AiTools/skills/elang-ai-coding/references/黑月编译与黑月界面类.md"]
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

# 转换工具已换成 QAQ-ETool：技能包、使用说明、README、规范真源里一律不许再出现 e2txt。
# 唯一例外是安装器的「退役技能」清理表（要按名字删掉旧安装点的 e2txt-cli）。
E2TXT = re.compile(r"e2txt", re.I)
RETIRED_OK = {"install-skills.py", "install-skills.sh", "安装技能.cmd"}
e2_targets = [p for p in KIT.rglob("*")
              if p.is_file() and p.suffix.lower() in (".md", ".py", ".sh", ".cmd", ".txt", ".json")]
# docs/ 只查随技能发布的规范真源；交接记录等内部工作文档是历史档案，不在此列
e2_targets += [ROOT / "README.md"] + [ROOT / "docs" / n for n in sorted(REF_ALLOW["elang-ai-coding"])]
e2_hits = []
for p in e2_targets:
    if not p.is_file():
        continue
    raw = p.read_bytes()
    enc = "gbk" if p.suffix.lower() == ".cmd" else "utf-8"
    txt = raw.decode(enc, errors="replace")
    if p.name in RETIRED_OK:
        txt = txt.replace("e2txt-cli", "")
    for i, l in enumerate(txt.splitlines()):
        if E2TXT.search(l):
            e2_hits.append(f"{os.path.relpath(p, ROOT)}:{i + 1}  {l.strip()[:80]}")
for h in e2_hits:
    bad(f"残留 e2txt: {h}")
if not e2_hits:
    ok(f"e2txt 零残留（{len(e2_targets)} 个文件；安装器只允许出现退役技能名 e2txt-cli）")

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

head(8, "跨技能运行期依赖（技能必须成套安装）")

# 技能的 scripts/ 里若去读**同级技能**的目录/资产，那个技能就成了运行期硬依赖：
# 单独分发本技能（或换一台只装了它的机器）必然缺件失败。这里把依赖图**显式化**：
# 只允许白名单内的依赖，且消费者必须在自己的 SKILL.md 正文里声明过同装 ——
# 否则用户只会看到一句「找不到文件」，无从知道还得再装一个技能。
CROSS_DEP_ALLOW = {
    "elang-ai-coding": {"qaq-etool-cli"},
    "elang-debug": {"qaq-etool-cli"},
    "elang-build": {"elang-debug", "qaq-etool-cli"},
}
SKILL_REF_RE = re.compile(r"\b(elang-ai-coding|elang-build|elang-debug|qaq-etool-cli)\b")
cross_seen = False
for d in skill_dirs:
    body = (d / "SKILL.md").read_text(encoding="utf-8", errors="replace")
    used = {}
    scripts = d / "scripts"
    if scripts.is_dir():
        for py in sorted(scripts.rglob("*.py")):
            txt = py.read_text(encoding="utf-8", errors="replace")
            for m in SKILL_REF_RE.finditer(txt):
                if m.group(1) != d.name:
                    used.setdefault(m.group(1), set()).add(py.name)
    if not used:
        ok(f"{d.name:18s} 不依赖其它技能（可单独安装）")
        continue
    cross_seen = True
    allow = CROSS_DEP_ALLOW.get(d.name, set())
    for other in sorted(used):
        where = ", ".join(sorted(used[other]))
        if other not in allow:
            bad(f"{d.name}: {where} 运行期引用了技能 {other}，但没登记进 CROSS_DEP_ALLOW")
        elif other not in body:
            bad(f"{d.name}: 运行期依赖 {other}，但 SKILL.md 正文没声明同装（单独分发会缺件）")
        else:
            ok(f"{d.name:18s} → {other:16s} 依赖已声明（出现在 {where}）")
if not cross_seen:
    print("   （全套技能都没有跨技能运行期依赖）")

print()
print("-" * 66)
print(f"结果：{PASS} 项通过，{FAIL} 项失败")
if FAILURES:
    print()
    for m in FAILURES:
        print(f"  !! {m}")
print("-" * 66)
sys.exit(1 if FAIL else 0)
