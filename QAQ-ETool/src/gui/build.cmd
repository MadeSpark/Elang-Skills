@echo off
rem Build qaq-etool-gui.exe with 32-bit MinGW (MSYS2 mingw32).
rem Output: qaq-etool-gui.exe next to this script.
setlocal
set "MINGW=C:\msys64\mingw32\bin"
rem windres calls gcc for preprocessing, so mingw32\bin must be on PATH.
set "PATH=%MINGW%;%PATH%"
cd /d "%~dp0" || exit /b 1

"%MINGW%\windres.exe" -O coff -i res.rc -o res.o
if errorlevel 1 goto fail

"%MINGW%\gcc.exe" -O2 -s -static -municode -mwindows -Wall -Wextra -o qaq-etool-gui.exe main.c core.c selftest.c res.o -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lshlwapi
if errorlevel 1 goto fail

del /q res.o 2>nul
echo BUILD OK: %~dp0qaq-etool-gui.exe
exit /b 0

:fail
del /q res.o 2>nul
echo BUILD FAILED
exit /b 1
