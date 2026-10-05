# -*- coding: utf-8 -*-
"""发布合规扫描：发布包 SKILL.md 不得含本机路径/项目专名/外部工作区引用"""
import re

PAT = re.compile(
    r'[A-Za-z]:[\\/]|/Users/|MadeSpark|Desktop[\\/]测试|work_mb/'
    r'|docs/[^\s`]*\.md|栗子虚拟机|小栗子|浏览框测试|host_orig|demo_split|work_host'
)
FILES = [
    r'Releases\Elang-AiTools\skills\elang-ai-coding\SKILL.md',
    r'Releases\Elang-AiTools\skills\qaq-etool-cli\SKILL.md',
]
bad_total = 0
for p in FILES:
    t = open(p, encoding='utf-8').read()
    hits = [(i, l.strip()[:110]) for i, l in
            enumerate(t.replace('\r\n', '\n').split('\n'), 1) if PAT.search(l)]
    print(p.split('skills')[-1], '命中', len(hits))
    for i, l in hits[:10]:
        print('  L%d %s' % (i, l))
    bad_total += len(hits)
print('合规结论:', 'PASS' if bad_total == 0 else 'FAIL(%d)' % bad_total)
