#!/usr/bin/env bash
# 把开发侧的工具与文档同步进发布区（发布区只放成品，手工拷容易漏）
#
# 真源与去向：
#   tools/<脚本>.py        ->  $KIT/skills/<技能>/scripts/
#   docs/<文档>.md         ->  $KIT/skills/<技能>/references/
#   tools/mk-cmd.py        ->  $KIT/安装技能.cmd（GBK 落盘）
#
# 规范全文进**技能包内部**的 references\（不是发布区根的 docs\）——
# 技能必须自包含：装到任何 AI 工具的技能目录后，脚本、exe、规范都要跟着过去。
# 编译产物（支持库 .fne / 导出支持库文档.exe）由各自的 src/*/build.sh 自己同步到
# skills/<技能>/assets/，**不经过本脚本**。
# 最后调用 install-skills.py，把技能刷到本机**所有** AI 工具的技能目录
# （~/.agents/skills、~/.claude/skills、~/.trae/skills … 见该脚本顶部目标表）。
#
# ⚠️ 每技能一张表（见下 SKILLS）。只列**成品**：
#   - tools/ 里还有 pack-skill.py、scan-machineinfo.py、sync-release.sh 这类**开发脚本**，
#     它们不是技能的一部分，不要拷进 scripts/，所以用显式名单。
#   - docs/ 里躺着工程内部的**工作文档**（方案、逆向分析报告、开发工程说明），
#     它们满是本机路径与统计数，属于「给协作者看的过程材料」，**不是成品**，
#     绝不能进技能包 —— 否则技能装到别人机器上就带了一堆只对本机成立的路径。
#     verify-release.py 的 references 白名单也**按技能**设，防止退化。
set -e
cd "$(dirname "$0")/.."          # 工程根

PY=python

# 每技能一行： <技能名>|<scripts（真源 tools/）>|<references（真源 docs/）>
SKILLS=(
  "elang-ai-coding|mkproj.py rtcheck.py efix.py echeck.py|易语言文本格式规范.md"
  "e2txt-cli||"
  "elang-debug|mkcage.py elaunch.py readlog.py|"
)

# 发布区可能被挪动，按顺序找
find_kit() {
  for c in "Releases/Elang-AiTools" "Elang-AiTools" "release/Elang-AiTools"; do
    if [ -d "$c/skills" ]; then echo "$c"; return 0; fi
  done
  return 1
}
KIT="$(find_kit)" || { echo "找不到发布区（应含 skills/ 子目录）"; exit 1; }
echo "发布区: $KIT"

for entry in "${SKILLS[@]}"; do
  IFS='|' read -r name scripts docs <<< "$entry"
  S="skills/$name"
  echo "技能 $name:"
  if [ -n "$scripts" ]; then
    mkdir -p "$KIT/$S/scripts"
    for f in $scripts; do
      if [ -f "tools/$f" ]; then cp -f "tools/$f" "$KIT/$S/scripts/"; echo "  tools/$f -> $S/scripts/"
      else echo "  !! 缺 tools/$f"; fi
    done
  fi
  if [ -n "$docs" ]; then
    mkdir -p "$KIT/$S/references"
    for f in $docs; do
      if [ -f "docs/$f" ]; then cp -f "docs/$f" "$KIT/$S/references/"; echo "  docs/$f -> $S/references/"
      else echo "  !! 缺 docs/$f"; fi
    done
  fi
done
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
