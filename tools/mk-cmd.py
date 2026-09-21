#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""生成 `Releases\Elang-AiTools\安装技能.cmd`。

为什么要有这个生成器：Windows 批处理必须用 **系统 ANSI（简体中文即 GBK）+ CRLF**
保存，中文在 cmd 里才不乱码。但 GBK 文件用普通文本编辑器/AI 工具去改会读成乱码，
所以把**可读的 UTF-8 源**放在这里，产物由本脚本生成。

改安装流程 -> 改本文件的 CMD 模板 -> 跑一次本脚本。

用法：
    python tools/mk-cmd.py            # 生成到发布区（自动查找 Releases\Elang-AiTools）
    python tools/mk-cmd.py --check    # 只校验产物是否与模板一致（不写）
"""
from __future__ import annotations

import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KIT_CANDIDATES = ("Releases/Elang-AiTools", "Elang-AiTools", "release/Elang-AiTools")
OUT_NAME = "安装技能.cmd"

# --------------------------------------------------------------------------- #
# 模板：全是 ASCII 反斜杠，原样写入（raw 字符串，不做转义）
# --------------------------------------------------------------------------- #
CMD = r'''@echo off
setlocal enabledelayedexpansion
chcp 936 >nul 2>nul
title Elang-AiTools - 安装易语言 AI 技能（跨工具）

set "HERE=%~dp0"
set "SRC=%HERE%skills"

echo ==================================================================
echo   Elang-AiTools  --  安装易语言 AI 技能（跨工具通用）
echo ==================================================================
echo.
echo   按 Agent Skills 开放规范安装：一份技能，多个 AI 工具都能用。
echo.

if not exist "%SRC%\" (
  echo [X] 找不到 skills 目录:
  echo     %SRC%
  echo     请把本文件和 skills 文件夹放在同一个目录里。
  goto :done
)

rem ---------- 优先用 Python 跑完整安装器（探测更全、装完自动校验）----------
set "PYEXE="
where py >nul 2>nul
if not errorlevel 1 (
  py -3 -c "import sys" >nul 2>nul
  if not errorlevel 1 set "PYEXE=py -3"
)
if not defined PYEXE (
  where python >nul 2>nul
  if not errorlevel 1 (
    python -c "import sys" >nul 2>nul
    if not errorlevel 1 set "PYEXE=python"
  )
)

if defined PYEXE (
  echo 使用 Python 运行完整安装器 ...
  echo.
  %PYEXE% "%HERE%install-skills.py" %*
  goto :done
)

echo 未检测到 Python -- 使用内置安装逻辑（装到通用根 + 已安装的工具）
echo.

rem ---------- 内置逻辑：通用根 + 探测到的各工具目录 ----------
call :inst "%USERPROFILE%\.agents\skills" "Agent Skills 通用根"

if exist "%USERPROFILE%\.claude\"          call :inst "%USERPROFILE%\.claude\skills" "Claude Code"
if exist "%USERPROFILE%\.codex\"           call :inst "%USERPROFILE%\.codex\skills" "Codex CLI"
if exist "%USERPROFILE%\.dsh\"             call :inst "%USERPROFILE%\.dsh\skills" "DeepSeek Harness"
if exist "%USERPROFILE%\.trae\"            call :inst "%USERPROFILE%\.trae\skills" "Trae 国际版"
if exist "%USERPROFILE%\.trae-cn\"         call :inst "%USERPROFILE%\.trae-cn\skills" "Trae 国内版"
if exist "%USERPROFILE%\.cursor\"          call :inst "%USERPROFILE%\.cursor\skills" "Cursor"
if exist "%USERPROFILE%\.windsurf\"        call :inst "%USERPROFILE%\.windsurf\skills" "Windsurf"
if exist "%USERPROFILE%\.config\opencode\" call :inst "%USERPROFILE%\.config\opencode\skills" "OpenCode"
if exist "%USERPROFILE%\.workbuddy\"       call :inst "%USERPROFILE%\.workbuddy\skills" "WorkBuddy"

echo.
echo 安装完成。在任意一个已安装的工具里新开对话，说「用易语言帮我写个程序」
echo 或提到 e2txt，看技能是否被自动调起。
goto :done

rem ======================= 子程序 =======================

:inst
setlocal
set "DST=%~1"
set "LABEL=%~2"
if not exist "%DST%\" mkdir "%DST%" 2>nul
echo   %LABEL%
echo     安装到: %DST%
for /d %%S in ("%SRC%\*") do call :one "%%~fS" "%%~nxS" "%DST%"
echo.
endlocal
goto :eof

:one
setlocal
set "FROM=%~1"
set "NAME=%~2"
set "DST=%~3"
if not exist "%FROM%\SKILL.md" (
  endlocal & goto :eof
)
if exist "%DST%\%NAME%\_skillhub_meta.json" (
  echo     [跳过] %NAME%  -- 来自技能市场，不覆盖
  endlocal & goto :eof
)
if exist "%DST%\%NAME%\" rd /s /q "%DST%\%NAME%"
xcopy "%FROM%" "%DST%\%NAME%\" /E /I /Y >nul
if errorlevel 1 (echo     [失败] %NAME%) else (echo     [完成] %NAME%)
rem 支持库文档是 exe 跑出来的生成物，不该跟着技能到处跑
if exist "%DST%\%NAME%\支持库文档\" rd /s /q "%DST%\%NAME%\支持库文档"
endlocal & goto :eof

:done
echo.
if /i not "%ELANG_NO_PAUSE%"=="1" pause
endlocal
exit /b 0
'''


def find_kit(explicit=None):
    cands = ([explicit] if explicit else []) + [
        os.path.join(ROOT, c.replace('/', os.sep)) for c in KIT_CANDIDATES]
    for p in cands:
        if p and os.path.isdir(os.path.join(p, 'skills')):
            return p
    return None


def render():
    """统一换行 + 编码成 GBK 字节。"""
    body = CMD.replace('\r\n', '\n').replace('\n', '\r\n')
    return body.encode('gbk')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--kit', help='发布区根目录（默认自动查找）')
    ap.add_argument('--check', action='store_true', help='只校验，不写')
    a = ap.parse_args()

    kit = find_kit(a.kit)
    if not kit:
        print('!! 找不到发布区（应含 skills\\ 子目录）')
        return 2

    out = os.path.join(kit, OUT_NAME)
    data = render()

    if a.check:
        if not os.path.exists(out):
            print(f'!! 产物不存在: {out}')
            return 1
        cur = open(out, 'rb').read()
        if cur == data:
            print(f'OK  {os.path.relpath(out, ROOT)}  ({len(data)} B, GBK+CRLF) 与模板一致')
            return 0
        print(f'!! {os.path.relpath(out, ROOT)} 与模板不一致'
              f'（现有 {len(cur)} B / 应为 {len(data)} B）')
        return 1

    with open(out, 'wb') as f:
        f.write(data)
    print(f'已生成 {os.path.relpath(out, ROOT)}  {len(data)} B  (GBK, CRLF)')

    # 自检：可 GBK 解码、有 CRLF、无 UTF-8 BOM
    raw = open(out, 'rb').read()
    assert raw[:3] != b'\xef\xbb\xbf', '不该有 UTF-8 BOM'
    assert b'\r\n' in raw, '应该有 CRLF'
    raw.decode('gbk')
    print('自检通过：GBK 可解码 / CRLF / 无 BOM')
    return 0


if __name__ == '__main__':
    sys.exit(main())
