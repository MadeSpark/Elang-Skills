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
DLLFLAGS=(-shared -Wl,--kill-at -static -s)
EXEFLAGS=(-static -s -luser32 -lgdi32)

echo "[build] gcc -> $("$GCC" --version | head -1)"

echo "[build] 1/5 MbKit.dll"
# 三个 C 单元 + 一个汇编转发层：
#   mbkit.c      常驻层与高层接口（view 管理、事件转发、自 PIN）
#   mbkit_fwd.c  透明转发层：懒加载 node.dll、填 _mbk_slots[]（进程内唯一持有内核）
#   mbkit_cb.c   22 个回调注册类的拦截：内核只见 MbKit 的 wrapper，业务 thunk 只作数据
#   gen_fwd.S    230 个同签名转发桩（纯 jmp，不碰栈）
# 导出名由 gen_fwd.def 显式指定 —— 易语言 .DLL命令 依赖朴素名（无 _ 前缀、无 @N）
"$GCC" "${CFLAGS_COMMON[@]}" "${DLLFLAGS[@]}" -o MbKit.dll \
    mbkit.c mbkit_fwd.c mbkit_cb.c gen_fwd.S gen_fwd.def

echo "[build] 2/5 biz_v1.dll / biz_v2.dll"
"$GCC" "${CFLAGS_COMMON[@]}" "${DLLFLAGS[@]}" -Wl,--image-base,0x62000000 -o biz_v1.dll mb_biz.c
"$GCC" "${CFLAGS_COMMON[@]}" "${DLLFLAGS[@]}" -Wl,--image-base,0x63000000 -o biz_v2.dll mb_biz.c

echo "[build] 3/5 legacy_v1.dll / legacy_v2.dll"
"$GCC" "${CFLAGS_COMMON[@]}" "${DLLFLAGS[@]}" -Wl,--image-base,0x64000000 -o legacy_v1.dll mb_legacy.c
"$GCC" "${CFLAGS_COMMON[@]}" "${DLLFLAGS[@]}" -Wl,--image-base,0x65000000 -o legacy_v2.dll mb_legacy.c

echo "[build] 4/5 host_reload.exe"
# 注意：-l 库必须排在源文件**之后**，否则链接器先处理库、此时符号还没被引用，会报 undefined
"$GCC" "${CFLAGS_COMMON[@]}" -o host_reload.exe host_reload.c "${EXEFLAGS[@]}"

echo "[build] 5/5 产物核对"
ls -la MbKit.dll biz_v1.dll biz_v2.dll legacy_v1.dll legacy_v2.dll host_reload.exe

echo
echo "[build] 导出名核对（应为朴素名，不带 @N）"
PY="${PY:-C:/Users/MadeSpark/.workbuddy/binaries/python/versions/3.13.12/python.exe}"
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
