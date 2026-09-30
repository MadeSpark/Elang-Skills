@echo off
REM No-encoding-issue quick launcher for fix + verify + snapshot.
REM Double-click this or run from anywhere. No PowerShell -File arguments needed.
setlocal enabledelayedexpansion
set "TOOLS=%~dp0"
cd /d "%TOOLS%"
echo ====== Quick launcher: fix -> verify -> snapshot (ASCII-safe, no mojibake) ======
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%TOOLS%fix-eide-kb-freeze.ps1"
set F=%errorlevel%
echo.
echo [QuickLaunch] FIX exit=%F%
echo.
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%TOOLS%verify-eide-kb-freeze.ps1"
set V=%errorlevel%
echo.
echo [QuickLaunch] VERIFY exit=%V%
echo.
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%TOOLS%snapshot-still-freezing.ps1"
set S=%errorlevel%
echo.
echo [QuickLaunch] SNAPSHOT exit=%S%
echo.
echo ====== Summary: FIX=%F% VERIFY=%V% SNAPSHOT=%S% (all zero = 100%% PASS) ======
endlocal
exit /b 0
