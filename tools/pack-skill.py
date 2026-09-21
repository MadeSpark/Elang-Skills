#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把发布区里的技能打成可分发的 zip 包（发给别人 / 导入任意 AI 工具 / 上传技能市场）。

打包前按 **Agent Skills 开放规范**（agentskills.io）校验，不通过就不打包：

  * 首行 `---`，frontmatter 有收尾 `---`
  * `name` 必填、匹配 `^[a-z0-9]+(-[a-z0-9]+)*$`、≤64 字符、与目录名一致
  * `description` 必填、≤1024 字符
  * 不含规范外字段（厂商私有的 `agent_created` 之类，部分工具在打包/上传时会直接报错）
  * 正文引用的 `scripts/*`、`references/*`、`assets/*` 文件真实存在
  * 正文不出现某家工具专有的工具名（Bash / Read / Write …）

产物布局跟 SkillHub 一致：**zip 根直接就是 SKILL.md**，没有外层包装目录
（解压到任意 `<工具技能目录>/<技能名>/` 即可）。

用法：
    python tools/pack-skill.py                 # 打包发布区全部技能 -> dist/
    python tools/pack-skill.py --out out       # 指定输出目录
    python tools/pack-skill.py elang-ai-coding # 只打一个
"""
from __future__ import annotations

import argparse
import io
import os
import re
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 发布区可能被挪动，按顺序自动查找；也可用 --kit 显式指定
KIT_CANDIDATES = ('Releases/Elang-AiTools', 'Elang-AiTools', 'release/Elang-AiTools')

# --- Agent Skills 规范 ------------------------------------------------------ #
NAME_RE = re.compile(r'^[a-z0-9]+(-[a-z0-9]+)*$')
ALLOWED_FM = {'name', 'description', 'license', 'compatibility', 'metadata'}
DESC_MAX = 1024
NAME_MAX = 64
TOOLISH = re.compile(r'(?<!Git )\b(Bash|Read|Write|Edit|Grep|Glob|WebFetch|WebSearch)\b')

# 不进包的东西（支持库文档是 exe 的生成物，不是技能内容）
SKIP_NAMES = {'__pycache__', '.DS_Store', 'Thumbs.db', '支持库文档'}
SKIP_EXT = {'.pyc', '.pyo'}


def find_kit(explicit=None):
    """返回发布区根目录（含 skills\\ 的那层）；找不到返回 None。"""
    cands = ([explicit] if explicit else [])
    cands += [os.path.join(ROOT, c.replace('/', os.sep)) for c in KIT_CANDIDATES]
    for p in cands:
        if p and os.path.isdir(os.path.join(p, 'skills')):
            return p
    return None


def read_frontmatter(p):
    """读 SKILL.md 的 YAML frontmatter（只做轻量校验，不依赖 pyyaml）。"""
    s = io.open(p, encoding='utf-8').read()
    if not s.startswith('---\n'):
        return None, '首行不是 ---（frontmatter 缺失或前面有 BOM）'
    end = s.find('\n---\n', 4)
    if end < 0:
        return None, 'frontmatter 没有收尾的 ---'
    fm = {}
    for line in s[4:end].splitlines():
        m = re.match(r'^([A-Za-z_][\w-]*):[ \t]*(.*)$', line)
        if m:
            fm[m.group(1)] = m.group(2).strip()
    return fm, None


def validate(skill_dir):
    """按开放规范校验，返回 (name, 错误列表, 警告列表)。"""
    errs, warns = [], []
    name = os.path.basename(skill_dir)
    md = os.path.join(skill_dir, 'SKILL.md')
    if not os.path.isfile(md):
        return name, ['缺少 SKILL.md'], warns
    fm, err = read_frontmatter(md)
    if err:
        return name, [err], warns

    v = fm.get('name', '')
    if not v:
        errs.append('frontmatter 缺 name')
    elif not NAME_RE.match(v):
        errs.append(f'name={v!r} 不匹配 ^[a-z0-9]+(-[a-z0-9]+)*$')
    elif v != name:
        errs.append(f'name={v!r} 与目录名 {name!r} 不一致')
    elif len(v) > NAME_MAX:
        errs.append(f'name 超 {NAME_MAX} 字符（{len(v)}）')

    d = fm.get('description', '')
    if not d:
        errs.append('frontmatter 缺 description')
    elif len(d) > DESC_MAX:
        errs.append(f'description 超 {DESC_MAX} 字符（{len(d)}）')

    extra = sorted(set(fm) - ALLOWED_FM)
    if extra:
        errs.append(f'含规范外字段 {extra}（部分工具打包/上传时会报错，应删掉）')

    s = io.open(md, encoding='utf-8').read()
    body = s[s.find('\n---\n', 4) + 5:]
    toolish = sorted(set(TOOLISH.findall(body)))
    if toolish:
        warns.append(f'正文出现工具专有名词 {toolish}（其他工具可能不理解）')

    for rel in sorted(set(re.findall(r'(?:scripts|references|assets)/[\w\-.]+', body))):
        fn = os.path.basename(rel)
        if re.search(r'x{3,}', fn, re.I) or '<' in rel:
            continue                      # 文档里的占位符示例（如 scripts/xxx.py）
        if os.path.exists(os.path.join(skill_dir, rel.replace('/', os.sep))):
            continue                      # 本技能自带
        root = os.path.dirname(skill_dir)
        sibs = [d for d in sorted(os.listdir(root))
                if os.path.isdir(os.path.join(root, d))
                and os.path.exists(os.path.join(root, d, rel.replace('/', os.sep)))]
        if sibs:
            warns.append(f'引用了同级技能 {sibs[0]} 的 {rel}（跨技能引用，合法）')
        else:
            errs.append(f'SKILL.md 里引用了不存在的文件: {rel}')

    return name, errs, warns


def zip_skill(skill_dir, out_dir):
    name = os.path.basename(skill_dir)
    out = os.path.join(out_dir, f'{name}.zip')
    n = 0
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        for dp, dns, fns in os.walk(skill_dir):
            dns[:] = [d for d in dns if d not in SKIP_NAMES]
            for fn in sorted(fns):
                if fn in SKIP_NAMES or os.path.splitext(fn)[1].lower() in SKIP_EXT:
                    continue
                full = os.path.join(dp, fn)
                rel = os.path.relpath(full, skill_dir).replace(os.sep, '/')
                z.write(full, rel)          # 根直接是 SKILL.md
                n += 1
    return out, n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('names', nargs='*', help='只打包这些技能（默认全部）')
    ap.add_argument('--kit', help='发布区根目录（默认自动查找 Releases/Elang-AiTools）')
    ap.add_argument('--out', default=os.path.join(ROOT, 'dist'))
    a = ap.parse_args()

    kit = find_kit(a.kit)
    if not kit:
        tried = ([a.kit] if a.kit else []) + list(KIT_CANDIDATES)
        print('!! 找不到发布区（应含 skills\\ 子目录）。试过: ' + '、'.join(tried))
        return 2
    skills_src = os.path.join(kit, 'skills')
    print(f'发布区: {os.path.relpath(kit, ROOT)}')

    names = a.names or sorted(d for d in os.listdir(skills_src)
                              if os.path.isdir(os.path.join(skills_src, d)))
    os.makedirs(a.out, exist_ok=True)

    bad = 0
    for want in names:
        d = os.path.join(skills_src, want)
        if not os.path.isdir(d):
            print(f'  [X] {want}: 不是目录')
            bad += 1
            continue
        name, errs, warns = validate(d)
        for w in warns:
            print(f'  [!] {name}: {w}')
        if errs:
            print(f'  [X] {name}: ' + '; '.join(errs))
            bad += 1
            continue
        out, n = zip_skill(d, a.out)
        print(f'  [OK] {name:20s} {n} 个文件 -> {os.path.relpath(out, ROOT)}'
              f'  ({os.path.getsize(out) / 1024:.1f} KB)'
              f'  规范校验通过')

    print()
    print(f'输出目录: {a.out}')
    print('（zip 根即 SKILL.md；解压到任意 <工具技能目录>/<技能名>/ 即可用）')
    if bad:
        print(f'{bad} 个技能未通过校验，未打包')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
