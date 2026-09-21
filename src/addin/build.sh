#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# build.sh —— 用 MinGW-w64 (msys2, 32 位) 编译「AI调试宿主」易语言支持库 .fne
#             （elang-debug 技能的宿主库本体）
#
# 产物（与 re/ 下的验证版**逐字节一致**）：
#   elang_addin.fne      —— 支持库本体（导出 GetNewInf 的 32 位 DLL）
#   elang_addin_diag.fne —— 诊断版（POC_SELFCHECK + POC_CRASHTRAP）
#   offset_probe.exe     —— 结构体偏移自证 + `--dump-libinfo <fne>` 读库信息
#   load_test.exe        —— 独立加载 .fne 读 LIB_INFO 的小工具
# 并把 elang_addin.fne 同步进技能包 assets\（技能必须自包含）。
#
# 关键编译参数：
#   1. 必须用 32 位 g++（e.exe 是 32 位）；且要先把 g++ 所在目录前置进 PATH，
#      否则 cc1plus / 汇编器找不到。
#   2. -D__GCC_ 让官方 lib2.h / PublicIDEFunctions.h 进入 GCC 兼容分支。
#   3. -fexec-charset=GBK：源码里的中文（UTF-8）字面量转成 GBK 字节，
#      这样 e.exe（GBK 环境）里显示的库名「AI调试宿主」才不乱码。
#   4. 官方 SDK 的 VC6 约定是 __cdecl（mtypes.h 里 `#define WINAPI` 为空），
#      sdk_compat.h 已将其还原为空宏，故导出名保持未修饰即可。
#
# 可覆盖的环境变量：GXX（32 位 g++ 路径）、MINGW_BIN（其 bin 目录）、
#                   ELANG_HOME（易语言安装目录，用于定位 elib SDK）、SDK_ELIB（直接指定）。
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# --- 定位 32 位 g++ ---------------------------------------------------------- #
MINGW_BIN="${MINGW_BIN:-/c/msys64/mingw32/bin}"
GXX="${GXX:-}"
if [ -z "$GXX" ]; then
  for c in "$MINGW_BIN/g++.exe" /c/msys64/mingw32/bin/g++.exe \
           /c/mingw32/bin/g++.exe /mingw32/bin/g++.exe g++; do
    if [ -n "$c" ] && command -v "$c" >/dev/null 2>&1; then GXX="$c"; break; fi
  done
fi
[ -n "$GXX" ] || { echo "找不到 32 位 g++：请设置 GXX=/path/to/mingw32/g++.exe"; exit 1; }
GXXDIR="$(dirname "$GXX")"
case ":$PATH:" in *":$GXXDIR:"*) ;; *) PATH="$GXXDIR:$PATH"; export PATH ;; esac

# --- 定位易语言官方扩展接口（elib）头文件目录 ------------------------------- #
SDK_ELIB="${SDK_ELIB:-}"
if [ -z "$SDK_ELIB" ] && [ -n "${ELANG_HOME:-}" ] && [ -d "$ELANG_HOME/sdk/cpp/elib" ]; then
  SDK_ELIB="$ELANG_HOME/sdk/cpp/elib"
fi
[ -n "$SDK_ELIB" ] || {
  echo "找不到 elib SDK：设 ELANG_HOME（易语言安装目录）或直接设 SDK_ELIB=<...>/sdk/cpp/elib"
  exit 1
}

CXXFLAGS_COMMON=(-m32 -O2 -D__GCC_ -DWIN32 -fpermissive -I"$SDK_ELIB")
LINK_STATIC=(-static-libgcc -static-libstdc++ -static)

# windres 与 g++ 同目录
WINDRES="${WINDRES:-$(dirname "$GXX")/windres.exe}"
[ -x "$WINDRES" ] || WINDRES=windres

echo "[build] g++      -> $("$GXX" --version | head -1)"
echo "[build] SDK elib -> $SDK_ELIB"

# --- 0) 版本资源（RT_VERSION）——真实库(cncnv/dp1/etools)都带，缺了可能被 e.exe 读崩 --- #
# GNU windres 默认输出不是 coff，必须显式 -O coff 才能被 ld 接受（否则 "file format not recognized"）。
echo "[build] step 0: version.res"
"$WINDRES" -i version.rc -o version.res -O coff

# --- 1) 偏移量自证小程序（编译并运行，把关键字段偏移落地为可审计文本）-------- #
echo "[build] step 1: offset_probe.exe"
"$GXX" "${CXXFLAGS_COMMON[@]}" offset_probe.cpp -o offset_probe.exe "${LINK_STATIC[@]}"
./offset_probe.exe | tee offset_dump.txt

# --- 2) 独立加载测试小程序 --------------------------------------------------- #
echo "[build] step 2: load_test.exe"
"$GXX" "${CXXFLAGS_COMMON[@]}" load_test.cpp -o load_test.exe "${LINK_STATIC[@]}"

# --- 3) 支持库本体（clean + diag）------------------------------------------- #
# 注意：输出名必须写成相对名 `elang_addin.fne`（不要带目录）。
#   MinGW ld 默认 --enable-auto-image-base，会把**输出路径**哈希成 PE 映像基址；
#   写成带目录的路径会让基址漂移、产物字节随之变化（功能等价，但不再是同一份）。
echo "[build] step 3: elang_addin.fne / elang_addin_diag.fne"
"$GXX" -m32 -shared -O2 -Wall -Wextra -Wno-unused-parameter \
    -D__GCC_ -DWIN32 -fpermissive \
    -finput-charset=UTF-8 -fexec-charset=GBK \
    -I"$SDK_ELIB" \
    elang_addin.cpp elang_addin.def version.res \
    -o elang_addin.fne \
    -static-libgcc -static-libstdc++ -static -s

"$GXX" -m32 -shared -O2 -Wall -Wextra -Wno-unused-parameter \
    -D__GCC_ -DWIN32 -fpermissive \
    -DPOC_SELFCHECK -DPOC_CRASHTRAP \
    -finput-charset=UTF-8 -fexec-charset=GBK \
    -I"$SDK_ELIB" \
    elang_addin.cpp elang_addin.def version.res \
    -o elang_addin_diag.fne \
    -static-libgcc -static-libstdc++ -static

ls -la elang_addin.fne elang_addin_diag.fne

# --- 4) 库信息 sidecar（UTF-8）——供技能里的 mkcage 现取 Guid/库名/版本 ------------- #
# `offset_probe.exe --dump-libinfo` 读 .fne 的 LIB_INFO；库名是 GBK，转成 UTF-8 落盘，
# 这样**发布版技能**（不含 offset_probe.exe）也能拿到正确的 Guid/库名，不会因 .fne
# 重建后 Guid 漂移而导致“加壳产物声明旧 Guid → e.exe 加载失败”。
echo "[build] step 4: elang_addin.libinfo.txt"
./offset_probe.exe --dump-libinfo elang_addin.fne | iconv -f GBK -t UTF-8 > elang_addin.libinfo.txt
cat elang_addin.libinfo.txt

# --- 5) 同步进发布区技能包 assets\（发布区只放成品；它可能被挪动，逐个位置试）-- #
echo "[build] step 5: sync -> skill assets"
for KIT in "../../Releases/Elang-AiTools" "../../Elang-AiTools" "../../release/Elang-AiTools"; do
  if [ -d "$KIT/skills/elang-debug" ]; then
    mkdir -p "$KIT/skills/elang-debug/assets"
    cp -f elang_addin.fne "$KIT/skills/elang-debug/assets/elang_addin.fne"
    cp -f elang_addin.libinfo.txt "$KIT/skills/elang-debug/assets/elang_addin.libinfo.txt"
    echo "       已同步 -> $(cd "$KIT" && pwd)/skills/elang-debug/assets/{elang_addin.fne,elang_addin.libinfo.txt}"
    break
  fi
done

echo "[build] OK"
