@echo off
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
