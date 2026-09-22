#!/bin/sh
# Elang-AiTools —— 跨工具安装技能（macOS / Linux / Git Bash）
#
# 按 Agent Skills 开放规范安装：总是装到 ~/.agents/skills/（规范共享根），
# 再补齐本机已安装的各工具自己目录（Claude Code、Trae 等不读共享根）。
#
# 用法：
#   ./install-skills.sh              # 探测式安装
#   ./install-skills.sh --dry-run    # 演练
#   ./install-skills.sh --list       # 只看会装到哪
#   ./install-skills.sh --uninstall  # 移除
#
# 有 Python 时本脚本只是薄壳，转发给 install-skills.py（功能完整、装完校验）。

set -u

HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/skills"

echo "=================================================================="
echo "  Elang-AiTools  --  安装易语言 AI 技能（跨工具通用）"
echo "=================================================================="
echo
echo "  按 Agent Skills 开放规范安装：一份技能，多个 AI 工具都能用。"
echo

if [ ! -d "$SRC" ]; then
    echo "[X] 找不到 skills 目录: $SRC"
    echo "    请把本脚本和 skills 文件夹放在同一个目录里。"
    exit 1
fi

# ---------- 优先转发给 Python 完整版 ----------
PY=""
if command -v python3 >/dev/null 2>&1; then
    PY="python3"
elif command -v python >/dev/null 2>&1; then
    PY="python"
fi

if [ -n "$PY" ] && [ -f "$HERE/install-skills.py" ]; then
    echo "使用 $PY 运行完整安装器 ..."
    echo
    exec "$PY" "$HERE/install-skills.py" "$@"
fi

echo "未检测到 Python -- 使用内置安装逻辑（装到通用根 + 已安装的工具）"
echo "(内置逻辑只做安装；--all / --targets / --uninstall / 逐文件校验 需要 Python)"
echo

# ---------- 内置逻辑 ----------
DRY=0
for a in "$@"; do
    [ "$a" = "--dry-run" ] && DRY=1
done

install_to() {
    dst="$1"
    label="$2"
    echo "  $label"
    echo "    安装到: $dst"
    if [ "$DRY" != 1 ]; then
        mkdir -p "$dst" 2>/dev/null || { echo "    [失败] 无法创建 $dst"; echo; return; }
    fi
    for s in "$SRC"/*/; do
        [ -f "$s/SKILL.md" ] || continue
        name=$(basename "$s")
        if [ -f "$dst/$name/_skillhub_meta.json" ]; then
            echo "    [跳过] $name  -- 来自技能市场，不覆盖"
            continue
        fi
        if [ "$DRY" = 1 ]; then
            echo "    [演练] $name -> $dst/$name"
            continue
        fi
        rm -rf "$dst/$name"
        if cp -R "$s" "$dst/$name" 2>/dev/null; then
            echo "    [完成] $name"
        else
            echo "    [失败] $name"
        fi
    done
    echo
}

install_to "$HOME/.agents/skills" "Agent Skills 通用根"

[ -d "$HOME/.claude" ]          && install_to "$HOME/.claude/skills"          "Claude Code"
[ -d "$HOME/.codex" ]           && install_to "$HOME/.codex/skills"           "Codex CLI"
[ -d "$HOME/.dsh" ]             && install_to "$HOME/.dsh/skills"             "DeepSeek Harness"
[ -d "$HOME/.trae" ]            && install_to "$HOME/.trae/skills"            "Trae"
[ -d "$HOME/.trae-cn" ]         && install_to "$HOME/.trae-cn/skills"         "Trae 国内版"
[ -d "$HOME/.cursor" ]          && install_to "$HOME/.cursor/skills"          "Cursor"
[ -d "$HOME/.windsurf" ]        && install_to "$HOME/.windsurf/skills"        "Windsurf"
[ -d "$HOME/.config/opencode" ] && install_to "$HOME/.config/opencode/skills" "OpenCode"
[ -d "$HOME/.workbuddy" ]       && install_to "$HOME/.workbuddy/skills"       "WorkBuddy"

echo "安装完成。在任意一个已安装的工具里新开对话，说「用易语言帮我写个程序」"
echo "或提到 e2txt，看技能是否被自动调起。"
