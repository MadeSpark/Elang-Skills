#!/bin/bash
# 验证门自检：确认 gate.exe 对「坏 blob」有鉴别力。
#   期望：旧 blob（1952 B 出厂版）→ 崩；-O1 miscompile 版 → 崩；
#        新构建产出的候选（1840 B）→ 通过。
# 用法： bash gate_selftest.sh
cd "$(dirname "$0")" || exit 1
export PATH="/c/msys64/mingw32/bin:$PATH"

T=/c/Users/MadeSpark/AppData/Local/Temp
GATE="$T/mbkit_build/gate.exe"
PROBE_O="$T/mbkit_build/probe.o"

if [ ! -f "$GATE" ]; then echo "先跑 build_mapkit.py 生成 gate.exe"; exit 1; fi

judge() {
  local label="$1"; shift
  local out; out=$("$@" 2>&1)
  if echo "$out" | grep -q "code = 0xC"; then
    printf "  %-44s ❌ 崩（门会拦住）\n" "$label"
  elif echo "$out" | grep -qE "\[8\] RESULT .* PASS"; then
    printf "  %-44s ✅ 通过（门会放行）\n" "$label"
  else
    local r; r=$(echo "$out" | grep -oE "\[8\] RESULT.*" | head -1)
    printf "  %-44s ❌ FAIL（门会拦住）  %s\n" "$label" "${r:-无 RESULT 行}"
  fi
}

echo "===== 门自检：prol 模式（易语言真实调用路径）====="
judge "旧 blob（1952 B 出厂版，已知崩）"  "$GATE" prol mapkit_text.bin

echo "--- 现编一个 -O1 miscompile 版 ---"
gcc -m32 -O1 -c mapkit.c -o "$T/o1.o" 2>/dev/null
objcopy -O binary -j .text "$T/o1.o" "$T/o1.bin" 2>/dev/null
gcc -m32 -o "$T/gate_o1.exe" "$PROBE_O" "$T/o1.o" -lkernel32 2>/dev/null
judge "-O1 版 blob（miscompile，已知崩）"  "$T/gate_o1.exe" prol "$T/o1.bin"

echo "--- 现编一个 -O2 固化参数版 ---"
gcc -m32 -O2 -fno-tree-loop-distribute-patterns -fno-omit-frame-pointer -fno-strict-aliasing -fno-stack-protector -c mapkit.c -o "$T/fix.o" 2>/dev/null
objcopy -O binary -j .text "$T/fix.o" "$T/fix.bin" 2>/dev/null
gcc -m32 -o "$T/gate_fix.exe" "$PROBE_O" "$T/fix.o" -lkernel32 2>/dev/null
judge "-O2 固化参数版（修复版）"  "$T/gate_fix.exe" prol "$T/fix.bin"

echo "===== 结束 ====="
