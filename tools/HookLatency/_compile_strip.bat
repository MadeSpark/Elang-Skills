@echo off
cd /d C:\Users\MadeSpark\Desktop\测试\tools\HookLatency
echo Compiling StripHooks.c -> StripHooks.dll...
C:\msys64\mingw32\bin\gcc.exe -shared -m32 -o StripHooks.dll StripHooks.c -luser32 -lkernel32 -s
echo GCC exit code: %ERRORLEVEL%
if exist StripHooks.dll (
  dir StripHooks.dll
  echo DLL OK. Now compile injector...
)
