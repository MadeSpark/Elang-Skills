#!/bin/bash
# UB 归因矩阵：崩溃到底由哪个编译开关引起？
#   判据全部用纯 ASCII 特征（避开中文编码坑）：
#     输出含 "code = 0xC"        -> 崩
#     输出含 "0x6C2C0000"        -> 映射成功
#     退出码 124                 -> 超时（跑飞/死循环）
#   探针固定 -O0 编译，只有被测的 mapkit.c 走实验 flag。
# 用法： bash ubmatrix.sh
cd "$(dirname "$0")" || exit 1
export PATH="/c/msys64/mingw32/bin:$PATH"

gcc -m32 -O0 -c probe_prol.c -o /tmp/pr.o 2>/dev/null || { echo "探针编译失败"; exit 1; }

run() {
  local label="$1"; shift
  local flags="$1"; shift
  gcc -m32 $flags -c mapkit.c -o /tmp/ub.o 2>/dev/null || { printf "%-54s %s\n" "$label" "COMPILE-FAIL"; return; }
  objcopy -O binary -j .text /tmp/ub.o /tmp/ub.bin 2>/dev/null || { printf "%-54s %s\n" "$label" "OBJCOPY-FAIL"; return; }
  local sz;  sz=$(stat -c%s /tmp/ub.bin 2>/dev/null)
  local rel; rel=$(objdump -r /tmp/ub.o 2>/dev/null | grep -c "R_386")
  gcc -m32 -o /tmp/ub.exe /tmp/pr.o /tmp/ub.o -lkernel32 2>/dev/null || { printf "%-54s %s\n" "$label" "LINK-FAIL"; return; }
  local out; out=$(timeout 8 /tmp/ub.exe o 2>&1); local rc=$?
  if [ "$rc" = "124" ]; then
    printf "%-54s %-10s size=%-6s reloc=%-3s %s\n" "$label" "⏱ TIMEOUT" "$sz" "$rel" "rc=124"
  elif echo "$out" | grep -q "code = 0xC"; then
    local c; c=$(echo "$out" | grep -oE "code = 0x[0-9A-F]{8}" | head -1)
    printf "%-54s %-10s size=%-6s reloc=%-3s %s\n" "$label" "❌ CRASH" "$sz" "$rel" "$c"
  elif echo "$out" | grep -q "0x6C2C0000"; then
    printf "%-54s %-10s size=%-6s reloc=%-3s %s\n" "$label" "✅ OK" "$sz" "$rel" "base=0x6C2C0000"
  else
    printf "%-54s %-10s size=%-6s reloc=%-3s %s\n" "$label" "⚠️ UNKNOWN" "$sz" "$rel" "rc=$rc"
  fi
}

echo "===== UB 归因矩阵（原生 o 模式直调 mk_map）====="
run "-O0（基线，已知 OK）"                        "-O0"
run "-O1（已知崩）"                               "-O1"
run "-O1 -fno-strict-aliasing"                    "-O1 -fno-strict-aliasing"
run "-O1 -fno-strict-overflow"                    "-O1 -fno-strict-overflow"
run "-O1 -fwrapv"                                 "-O1 -fwrapv"
run "-O1 -fno-aggressive-loop-optimizations"      "-O1 -fno-aggressive-loop-optimizations"
run "-O1 -fno-tree-loop-distribute-patterns"      "-O1 -fno-tree-loop-distribute-patterns"
run "-O1 -fno-strict-aliasing -fno-tree-loop-distribute-patterns" "-O1 -fno-strict-aliasing -fno-tree-loop-distribute-patterns"
run "-O1 -fno-tree-vrp -fno-tree-ccp"             "-O1 -fno-tree-vrp -fno-tree-ccp"
run "-O1 -fno-inline"                             "-O1 -fno-inline"
run "-O1 -fno-inline-small-functions"             "-O1 -fno-inline-small-functions"
run "-O1 -fno-tree-sra"                           "-O1 -fno-tree-sra"
run "-O1 -fno-ipa-sra"                            "-O1 -fno-ipa-sra"
run "-O1 -fno-tree-dominator-opts"                "-O1 -fno-tree-dominator-opts"
run "-O1 -fno-omit-frame-pointer"                 "-O1 -fno-omit-frame-pointer"
run "-O2（不带 fno-tree-loop）"                     "-O2"
run "-O2 -fno-tree-loop-distribute-patterns（已知 OK）" "-O2 -fno-tree-loop-distribute-patterns"
run "-O2 -fno-strict-aliasing"                    "-O2 -fno-strict-aliasing"
run "-Os"                                         "-Os"
run "-O3（已知崩）"                               "-O3"
echo "===== 结束 ====="
