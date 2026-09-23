#!/usr/bin/env bash
# 构建并运行「MbKit 自定义内核位置」验证
#
# 三个用例：
#   A 自定义路径 —— exe 在 e2e/A（该目录没有 node.dll），内核放在 e2e/kernel_x
#   B 默认位置   —— exe 在 e2e/B（node.dll 就在 exe 同目录）
#   C 设了但没命中 —— 指向一个没有 node.dll 的目录，应**明确失败**且不静默回退
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
E2E="$ROOT/re/opt/e2e"
MBKIT="$ROOT/src/mbkit/MbKit.dll"

MINGW_BIN="${MINGW_BIN:-/c/msys64/mingw32/bin}"
GCC="${GCC:-$MINGW_BIN/gcc.exe}"
[ -x "$GCC" ] || { echo "找不到 32 位 gcc: $GCC"; exit 1; }
GCCDIR="$(dirname "$GCC")"
case ":$PATH:" in *":$GCCDIR:"*) ;; *) PATH="$GCCDIR:$PATH"; export PATH ;; esac

[ -f "$E2E/kernel_x/node.dll" ] || { echo "缺少 $E2E/kernel_x/node.dll"; exit 1; }
[ -f "$E2E/B/node.dll" ]       || { echo "缺少 $E2E/B/node.dll"; exit 1; }
mkdir -p "$E2E/A" "$E2E/B" "$E2E/C" "$E2E/C/empty_kernel"   # C/empty_kernel: 故意没有 node.dll

echo "[build] $("$GCC" --version | head -1)"
set -e
# ⚠️ 必须 cd 到输出目录、用**相对**输出名：本机 gcc/ld 遇到含中文的绝对输出路径会
#    报 "cannot open output file ...: No such file or directory"（目录明明存在）。
( cd "$E2E/A" && "$GCC" -m32 -O2 -Wall -finput-charset=UTF-8 -fexec-charset=UTF-8 \
      -o node_path_test.exe "$HERE/node_path_test.c" -static -s )
cp -f "$E2E/A/node_path_test.exe" "$E2E/B/node_path_test.exe"
cp -f "$E2E/A/node_path_test.exe" "$E2E/C/node_path_test.exe"
set +e

export MBKIT_SILENT=1

run() {  # run <标题> <工作目录> <参数...>
  local title="$1"; shift
  local wd="$1"; shift
  echo
  echo "================ $title ================"
  ( cd "$wd" && ./node_path_test.exe "$@" ) 2>&1
  echo "---- exit=$? ----"
}

run "CASE A: custom kernel dir (SetNodePath)" "$E2E/A" "$MBKIT" "$E2E/kernel_x"
run "CASE B: not set => default = host exe dir"  "$E2E/B" "$MBKIT"
run "CASE C: set but not found (must FAIL loudly)" "$E2E/C" "$MBKIT" "$E2E/C/empty_kernel"
