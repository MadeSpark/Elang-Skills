#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# build.sh —— 用 MinGW-w64 (msys2, 32 位) 编译「AI调试宿主」易语言支持库
#
# 关键点：
#   1. 必须用 32 位 g++（e.exe 是 32 位）；且要先把 gcc 所在目录前置进 PATH，
#      否则 cc1plus / 汇编器找不到。
#   2. -D__GCC_ 让官方 lib2.h / PublicIDEFunctions.h 进入 GCC 兼容分支
#      （去掉 C++ 默认参数、去掉 #pragma pack(1) 区域）。
#   3. -fexec-charset=GBK：把源码中的中文（UTF-8）字面量转成 GBK 字节，
#      这样 e.exe（GBK 环境）里显示的库名「AI调试宿主」才不乱码。
#   4. 官方 SDK 的 VC6 约定是 __cdecl（mtypes.h 里 `#define WINAPI` 为空），
#      sdk_compat.h 已将其还原为空宏，故导出名保持未修饰即可。
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

MINGW_BIN="/c/msys64/mingw32/bin"
SDK_ELIB="/d/ides/e/sdk/cpp/elib"

GXX="$MINGW_BIN/g++.exe"

export PATH="$MINGW_BIN:$PATH"

echo "[build] g++ -> $("$GXX" --version | head -1)"
echo "[build] SDK   -> $SDK_ELIB"

# --- 1) 生成一份“偏移量自证”小程序并运行（把关键字段偏移打出来，供汇报核对）---
echo "[build] step 1/2: compile & run offset self-test"
"$GXX" -m32 -O2 -D__GCC_ -DWIN32 -fpermissive \
    -I"$SDK_ELIB" \
    src/offset_probe.cpp \
    -o offset_probe.exe \
    -static-libgcc -static-libstdc++ -static
./offset_probe.exe | tee offset_dump.txt

# --- 2) 编译支持库本体 ---
echo "[build] step 2/2: compile elang_addin.fne"
"$GXX" -m32 -shared -O2 -Wall -Wextra -Wno-unused-parameter \
    -D__GCC_ -DWIN32 -fpermissive \
    -finput-charset=UTF-8 -fexec-charset=GBK \
    -I"$SDK_ELIB" \
    src/elang_addin.cpp src/elang_addin.def \
    -o elang_addin.fne \
    -static-libgcc -static-libstdc++ -static -s

echo "[build] OK -> $(pwd)/elang_addin.fne"
ls -la elang_addin.fne
