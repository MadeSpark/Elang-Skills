# -*- coding: utf-8 -*-
"""同步 2026-10-05 复盘经验到全部分发目录：Releases(权威) -> live + 各 AI 工具目录"""
import shutil, hashlib, os, filecmp

REL = r'C:\Users\MadeSpark\Desktop\测试\Releases\Elang-AiTools\skills'
FILES = [
    r'elang-ai-coding\SKILL.md',
    r'qaq-etool-cli\SKILL.md',
]
TARGETS = [
    r'C:\Users\MadeSpark\.workbuddy\skills',
    r'C:\Users\MadeSpark\.agents\skills',
    r'C:\Users\MadeSpark\.claude\skills',
    r'C:\Users\MadeSpark\.cursor\skills',
    r'C:\Users\MadeSpark\.codex\skills',
    r'C:\Users\MadeSpark\.dsh\skills',
    r'C:\Users\MadeSpark\.trae\skills',
    r'C:\Users\MadeSpark\.trae-cn\skills',
    r'C:\Users\MadeSpark\.config\opencode\skills',
]

def md5(p):
    return hashlib.md5(open(p, 'rb').read()).hexdigest()

ok = fail = skip = 0
for f in FILES:
    src = os.path.join(REL, f)
    for t in TARGETS:
        dst = os.path.join(t, f)
        if not os.path.isdir(os.path.dirname(dst)):
            print('SKIP(无目录) %s' % dst); skip += 1; continue
        shutil.copy2(src, dst)
        if md5(src) == md5(dst):
            ok += 1
        else:
            print('FAIL %s' % dst); fail += 1
print('同步完成: ok=%d fail=%d skip=%d' % (ok, fail, skip))
