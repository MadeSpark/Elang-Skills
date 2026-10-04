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
# skills/<技能>/assets/，**不经过本脚本**；QAQ-ETool 的 3 个 exe 例外，见下 ASSETS。
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
# etool.py（定位/调用 QAQ-ETool）与 ec_guard.py（转换红线门禁）是共享模块，
# 被 import 的技能各带一份，保证每个技能单独装也能 import 成功。
SKILLS=(
  "elang-ai-coding|mkproj.py rtcheck.py efix.py echeck.py elibs.py etool.py|易语言文本格式规范.md 置入代码与机器码.md Win32与DLL互操作.md 程序入口与编译验证.md 黑月编译与黑月界面类.md"
  "qaq-etool-cli||"
  "elang-debug|mkcage.py elaunch.py readlog.py ec_guard.py etool.py|"
  "elang-build|ecompile.py ec_guard.py etool.py|"
)

# 第三方成品 exe：<技能名>|<来源目录>|<文件>。来源目录不在（例如没发布过）就跳过，
# 发布区里已有的 exe 保持不动。成品只保留 Releases：exe 用 publish 产物 + src/gui 产物，
# 不再有 QAQ-ETool/dist / 根 dist 等中转副本。
ASSETS=(
  "qaq-etool-cli|QAQ-ETool/src/QAQ.ETool.Cli/bin/Release/net8.0/win-x64/publish|QAQ-ETool.exe qaq-libinfo.exe"
  "qaq-etool-cli|QAQ-ETool/src/gui|qaq-etool-gui.exe"
)

# 已退役的技能：发布区里残留就删掉（安装点由 install-skills.py 的退役表清理）
RETIRED=(e2txt-cli)

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
for entry in "${ASSETS[@]}"; do
  IFS='|' read -r name srcdir files <<< "$entry"
  if [ ! -d "$srcdir" ]; then echo "技能 $name: 跳过 assets（没有 $srcdir）"; continue; fi
  mkdir -p "$KIT/skills/$name/assets"
  for f in $files; do
    if [ -f "$srcdir/$f" ]; then cp -f "$srcdir/$f" "$KIT/skills/$name/assets/"; echo "  $srcdir/$f -> skills/$name/assets/"
    else echo "  !! 缺 $srcdir/$f"; fi
  done
done
for name in "${RETIRED[@]}"; do
  if [ -d "$KIT/skills/$name" ]; then rm -rf "$KIT/skills/$name"; echo "已删除退役技能 skills/$name"; fi
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

# Python 导入安装器会再生 __pycache__（verify-release 会拦），收尾清掉
find "$KIT" -name "__pycache__" -type d -prune -exec rm -rf {} + 2>/dev/null || true
