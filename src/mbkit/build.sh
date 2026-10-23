#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# build.sh —— 编译 MbKit 常驻宿主层 + 热重载对照实验套件（全部 32 位）
#
# 产物：
#   MbKit.dll           常驻宿主层（自 PIN、持有 node.dll 与全部 wke 回调）
#   biz_v1.dll v2       业务薄层（走 MbKit，可卸载重载）；故意给不同 image-base，
#                       模拟「重新编译后的业务 DLL 被载入到不同基址」这一最坏情况
#   legacy_v1.dll v2    旧做法对照（自己相对路径加载内核 + 不 finalize）
#   host_reload.exe     压测宿主
#
# 关键点：
#   1) 必须 32 位 gcc（与 e.exe 同架构）。**必须先把 gcc 所在目录前置进 PATH**，
#      否则 gcc 找不到自己的 cc1/as/ld —— 表现是 exit 1 且没有任何错误输出。
#   2) 导出名要朴素（易语言 .DLL命令 依赖朴素名）：__declspec(dllexport) 会
#      生成 _Name@N，用 -Wl,--kill-at 去掉 @N。
#   3) 显式 --image-base 让各 DLL 落在不同基址，对照组才具备确定性
#      （否则 Windows 会把重载的 DLL 映回同一地址，野指针可能「碰巧能用」）。
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

MINGW_BIN="${MINGW_BIN:-/c/msys64/mingw32/bin}"
GCC="${GCC:-}"
if [ -z "$GCC" ]; then
  for c in "$MINGW_BIN/gcc.exe" /c/msys64/mingw32/bin/gcc.exe gcc; do
    if command -v "$c" >/dev/null 2>&1; then GCC="$c"; break; fi
  done
fi
[ -n "$GCC" ] || { echo "找不到 32 位 gcc：请设置 GCC=/path/to/mingw32/gcc.exe"; exit 1; }
GCCDIR="$(dirname "$GCC")"
case ":$PATH:" in *":$GCCDIR:"*) ;; *) PATH="$GCCDIR:$PATH"; export PATH ;; esac

CFLAGS_COMMON=(-m32 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type -I.
               -finput-charset=UTF-8 -fexec-charset=GBK)
CXXFLAGS_COMMON=(-m32 -O2 -Wall -Wno-unused-parameter -Wno-cast-function-type -I.
                 -Ivendor/webview2
                 -finput-charset=UTF-8 -fexec-charset=GBK -std=c++17)
DLLFLAGS=(-shared -Wl,--kill-at -static -s)
EXEFLAGS=(-static -s -luser32 -lgdi32)
# mbk_webview.cpp 用到的 Windows 库：COM(ole32/oleaut32)、shell32(SHGetFolderPathW)、
# uuid(__uuidof 的 IID_IUnknown)、stdc++(C++ 运行时)
WVLIBS=(-lole32 -loleaut32 -lshell32 -luuid -lstdc++ -static-libgcc -static-libstdc++)

echo "[build] gcc -> $("$GCC" --version | head -1)"

PY="${PY:-C:/Users/MadeSpark/.workbuddy/binaries/python/versions/3.13.12/python.exe}"

echo "[build] 0/6 重新生成转发层（gen_thunk.c/.def/.h）+ edgewebview 绑定表"
# ★ 必须重生成：gen_fwd.def 决定导出表。2026-09-25 发现它漏导出 19 个 MbKit_* 接口，
#   导致 host_reload.exe 的 GetProcAddress 失败、run_tests.sh 整条回归链静默失效。
#   重生成后立刻核对「mbkit_api.h 里声明的每个 MbKit_* 都在 .def 里」，漏一个就中止。
"$PY" gen_fwd.py >/dev/null
"$PY" gen_webview.py >/dev/null
missing="$(sed -n 's/.*__stdcall[[:space:]]\{1,\}\(MbKit_[A-Za-z0-9_]*\)[[:space:]]*(.*/\1/p' \
             mbkit_api.h mbkit.c mbkit_fwd.c mbkit_cb.c 2>/dev/null | sort -u \
           | while read -r n; do grep -qx "    $n" gen_fwd.def || echo "$n"; done)"
if [ -n "$missing" ]; then
  echo "!! gen_fwd.def 缺少以下导出（应从 mbkit_api.h 自动抽取）："
  echo "$missing" | sed 's/^/     /'
  exit 1
fi
# 导出边界编组自检：gen_thunk.c 必须已生成且执行体表非空，否则中止
[ -s gen_thunk.c ] || { echo "!! gen_fwd.py 没有产出 gen_thunk.c"; exit 1; }
grep -q "mbk_tramps" gen_thunk.c || { echo "!! gen_thunk.c 缺少 mbk_tramps 执行体表"; exit 1; }
[ -s gen_webview_bind.h ] || { echo "!! gen_webview.py 没有产出 gen_webview_bind.h"; exit 1; }

echo "[build] 1/6 编译 WebView2 后端 + 内嵌资源"
# windres 把 WebView2Loader.dll 打成 RCDATA 资源（id=101），供 mbk_webview.cpp
# 首次使用时释放到磁盘再 LoadLibrary —— 保持「模块只释放 MbKit144.dll 一个文件」。
WINDRES="${WINDRES:-$GCCDIR/windres.exe}"
if [ ! -x "$WINDRES" ] && command -v windres >/dev/null 2>&1; then WINDRES=windres; fi
"$WINDRES" -i webview2.rc -o webview2_res.o -O coff
# C++ 后端（WebView2 适配层）。-Ivendor/webview2 指向 NuGet 里 vendor 的头文件。
"$GCCDIR/g++.exe" "${CXXFLAGS_COMMON[@]}" -c mbk_webview.cpp -o mbk_webview.o

echo "[build] 2/6 MbKit.dll"
# C 单元先用 gcc 编成 .o（g++ 会把 .c 当 C++ 编，gen_thunk.c 的指定初始化器
# 在 C++ 下是 "sorry, unimplemented"），最后用 g++ 链接（拉进 libstdc++）。
"$GCC" "${CFLAGS_COMMON[@]}" -c mbkit.c      -o mbkit.o
"$GCC" "${CFLAGS_COMMON[@]}" -c mbkit_fwd.c  -o mbkit_fwd.o
"$GCC" "${CFLAGS_COMMON[@]}" -c mbkit_cb.c   -o mbkit_cb.o
"$GCC" "${CFLAGS_COMMON[@]}" -c mbk_thread.c -o mbk_thread.o
"$GCC" "${CFLAGS_COMMON[@]}" -c mbkit_veh.c  -o mbkit_veh.o
"$GCC" "${CFLAGS_COMMON[@]}" -c gen_thunk.c  -o gen_thunk.o
# 导出名由 gen_fwd.def 显式指定 —— 易语言 .DLL命令 依赖朴素名（无 _ 前缀、无 @N）
# 注意：库必须在 .o 之后（GCC 左→右链接，否则 -loleaut32/-luuid 不生效）
"$GCCDIR/g++.exe" "${DLLFLAGS[@]}" -o MbKit.dll \
    mbkit.o mbkit_fwd.o mbkit_cb.o mbk_thread.o mbkit_veh.o gen_thunk.o \
    mbk_webview.o webview2_res.o gen_fwd.def \
    "${WVLIBS[@]}"

echo "[build] 3/6 biz_v1.dll / biz_v2.dll"
"$GCC" "${CFLAGS_COMMON[@]}" "${DLLFLAGS[@]}" -Wl,--image-base,0x62000000 -o biz_v1.dll mb_biz.c
"$GCC" "${CFLAGS_COMMON[@]}" "${DLLFLAGS[@]}" -Wl,--image-base,0x63000000 -o biz_v2.dll mb_biz.c

echo "[build] 4/6 legacy_v1.dll / legacy_v2.dll"
"$GCC" "${CFLAGS_COMMON[@]}" "${DLLFLAGS[@]}" -Wl,--image-base,0x64000000 -o legacy_v1.dll mb_legacy.c
"$GCC" "${CFLAGS_COMMON[@]}" "${DLLFLAGS[@]}" -Wl,--image-base,0x65000000 -o legacy_v2.dll mb_legacy.c

echo "[build] 5/6 host_reload.exe"
# 注意：-l 库必须排在源文件**之后**，否则链接器先处理库、此时符号还没被引用，会报 undefined
"$GCC" "${CFLAGS_COMMON[@]}" -o host_reload.exe host_reload.c "${EXEFLAGS[@]}"

echo "[build] 6/6 产物核对"
ls -la MbKit.dll biz_v1.dll biz_v2.dll legacy_v1.dll legacy_v2.dll host_reload.exe

echo
echo "[build] 导出名核对（应为朴素名，不带 @N）"
if [ -x "$PY" ]; then
  "$PY" ../../re/mb/pe_exports.py MbKit.dll 2>/dev/null | head -6 || true
fi

echo
echo "[build] image-base 核对"
for f in biz_v1.dll biz_v2.dll legacy_v1.dll legacy_v2.dll; do
  printf "  %-16s ImageBase=%s\n" "$f" \
    "$(objdump -p "$f" 2>/dev/null | awk '/ImageBase/{print $2; exit}')"
done

echo "[build] OK"
