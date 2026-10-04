@echo off
rem 与 build_mbkit.cmd 完全同参数，唯一区别是不加 -s（保留符号表用于 nm 反解地址）
setlocal
set MINGW_BIN=C:\msys64\mingw32\bin
set PATH=%MINGW_BIN%;%PATH%
cd /d "%~dp0"
if not exist build_new mkdir build_new
gcc -m32 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type -I. ^
    -finput-charset=UTF-8 -fexec-charset=GBK -ffunction-sections -fdata-sections -shared -Wl,--kill-at -Wl,-Map=build_new\MbKit_dbg.map -static ^
    -o build_new\MbKit_dbg.dll ^
    mbkit.c mbkit_fwd.c mbkit_cb.c mbk_thread.c mbkit_veh.c gen_thunk.c gen_fwd.def
if errorlevel 1 ( echo DBG BUILD FAILED & exit /b 1 )
echo DBG BUILD OK
