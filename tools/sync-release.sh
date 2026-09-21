#!/usr/bin/env bash
# 把开发侧的工具与文档同步进发布区（发布区只放成品，手工拷容易漏）
#
#   tools/{mkproj,rtcheck,efix}.py  ->  $KIT/skills/elang-ai-coding/scripts/
#   docs/*.md                       ->  $KIT/skills/elang-ai-coding/references/
#   tools/mk-cmd.py                 ->  $KIT/安装技能.cmd（GBK 落盘）
#
# 规范全文进**技能包内部**的 references\（不是发布区根的 docs\）——
# 技能必须自包含：装到任何 AI 工具的技能目录后，脚本、exe、规范都要跟着过去。
# 编译产物 导出支持库文档.exe 由 src/build.sh 自己同步到 skills/.../assets/。
# 最后调用 install-skills.py，把技能刷到本机**所有** AI 工具的技能目录
# （~/.agents/skills、~/.claude/skills、~/.trae/skills … 见该脚本顶部目标表）。
#
# 注意：tools/ 里还有 pack-skill.py、sync-release.sh 这类**开发脚本**，
#       它们不是技能的一部分，不要拷进 scripts/，所以下面用显式名单。
set -e
cd "$(dirname "$0")/.."          # 工程根

SKILL_SCRIPTS="mkproj.py rtcheck.py efix.py echeck.py"
PY=python

# 发布区可能被挪动，按顺序找
find_kit() {
  for c in "Releases/Elang-AiTools" "Elang-AiTools" "release/Elang-AiTools"; do
    if [ -d "$c/skills" ]; then echo "$c"; return 0; fi
  done
  return 1
}
KIT="$(find_kit)" || { echo "找不到发布区（应含 skills/ 子目录）"; exit 1; }
echo "发布区: $KIT"

S="skills/elang-ai-coding"
mkdir -p "$KIT/$S/scripts" "$KIT/$S/references"
for f in $SKILL_SCRIPTS; do
  [ -f "tools/$f" ] && cp -f "tools/$f" "$KIT/$S/scripts/"
done
cp -f docs/*.md "$KIT/$S/references/"

echo "已同步:"
echo "  tools/{$(echo $SKILL_SCRIPTS | tr ' ' ',')}  ->  $KIT/$S/scripts/"
echo "  docs/*.md  ->  $KIT/$S/references/"
echo

# 重新生成 Windows 安装器（GBK；源模板在 tools/mk-cmd.py，别手改产物）
if command -v $PY >/dev/null 2>&1; then
  $PY tools/mk-cmd.py
  echo
  # 刷新本机所有 AI 工具的技能安装副本
  $PY "$KIT/install-skills.py"
else
  echo "!! 没找到 python，跳过 mk-cmd.py 与技能安装副本刷新"
fi
