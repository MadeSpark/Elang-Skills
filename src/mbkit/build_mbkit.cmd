@echo off
rem ===========================================================================
rem build_mbkit.cmd —— 只编 MbKit.dll（32 位），产物进 build_new\
rem 与 build.sh 的 1/5 步等价；少了 gen_fwd.py 重生成（要重生成先跑 python gen_fwd.py）
rem ===========================================================================
setlocal
set MINGW_BIN=C:\msys64\mingw32\bin
set PATH=%MINGW_BIN%;%PATH%
cd /d "%~dp0"
if not exist build_new mkdir build_new
gcc -m32 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type -I. ^
    -finput-charset=UTF-8 -fexec-charset=GBK -ffunction-sections -fdata-sections -shared -Wl,--kill-at -Wl,-Map=build_new\MbKit.map -static -s ^
    -o build_new\MbKit.dll ^
    mbkit.c mbkit_fwd.c mbkit_cb.c mbk_thread.c mbkit_veh.c gen_thunk.c gen_fwd.def
if errorlevel 1 ( echo BUILD FAILED & exit /b 1 )
echo BUILD OK
