#!/usr/bin/env bash
# 编译「易语言调用方式」的端到端验证套件
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
MINGW_BIN="${MINGW_BIN:-/c/msys64/mingw32/bin}"
GCC="$MINGW_BIN/gcc.exe"
PATH="$(dirname "$GCC"):$PATH"
C=(-m32 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type)
L=(-shared -Wl,--kill-at -static -s)

cp -f ../../../src/mbkit/MbKit.dll ./MbKit.dll

echo "[1/3] nk_v1.dll / nk_v2.dll（模拟改造后的业务模块，image-base 不同）"
"$GCC" "${C[@]}" "${L[@]}" -Wl,--image-base,0x6A000000 -o nk_v1.dll nk_biz.c -luser32
"$GCC" "${C[@]}" "${L[@]}" -Wl,--image-base,0x6B000000 -o nk_v2.dll nk_biz.c -luser32

echo "[2/3] nk_test.exe"
"$GCC" "${C[@]}" -o nk_test.exe nk_test.c -static -s -luser32

echo "[3/3] 产物"
ls -la MbKit.dll nk_v1.dll nk_v2.dll nk_test.exe
echo "[build] OK"
