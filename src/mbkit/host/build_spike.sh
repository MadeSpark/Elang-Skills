#!/bin/bash
# MbKit 辅助进程原型编译（32 位 mingw）
#
# ⚠️ 必须把 gcc 所在目录前置进 PATH，否则 gcc 找不到自己的 cc1/as/ld：
#    表现是 exit 1 且**没有任何错误输出**（本项目踩过多次）。
set -e
GCC="${GCC:-/c/msys64/mingw32/bin/gcc.exe}"
export PATH="$(dirname "$GCC"):$PATH"
cd "$(dirname "$0")"

echo "== gcc: $("$GCC" -dumpmachine) =="
for t in spike_host spike_cli; do
    "$GCC" -O2 -Wall -Wno-unused-parameter -o "$t.exe" "$t.c" \
        -luser32 -lgdi32 -lkernel32 -Wl,--kill-at
    echo "   built $t.exe  $(stat -c%s "$t.exe") B"
done
echo "== ok =="
