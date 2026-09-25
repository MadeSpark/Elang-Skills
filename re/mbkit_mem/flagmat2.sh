#!/usr/bin/env bash
# flagmat2.sh —— 在 -O1（已知会崩）上逐个关优化开关，找元凶
set -u
export PATH="/c/msys64/mingw32/bin:$PATH"
cd "$(dirname "$0")"

printf "%-64s %6s %s\n" "FLAGS" "BLOB" "RESULT"
echo "------------------------------------------------------------------------------------------"

run_one() {
  local F="$1"
  local O=/tmp/mk2.o P=/tmp/mk2_probe.exe
  rm -f "$O" "$P"
  gcc -m32 $F -c mapkit.c -o "$O" 2>/dev/null || { printf "%-64s %6s %s\n" "$F" "-" "COMPILE-FAIL"; return; }
  objcopy -O binary -j .text "$O" /tmp/mk2.bin 2>/dev/null
  local SZ; SZ=$(stat -c%s /tmp/mk2.bin 2>/dev/null)
  gcc -m32 -O0 -o "$P" probe_prol.c "$O" -lkernel32 2>/dev/null || { printf "%-64s %6s %s\n" "$F" "$SZ" "LINK-FAIL"; return; }
  local OUT RC BASE ST V
  OUT=$("$P" o 2>&1); RC=$?
  BASE=$(printf '%s' "$OUT" | grep -o '基址 = 0x[0-9A-F]*' | head -1)
  ST=$(printf '%s' "$OUT" | grep -o '状态码 = [0-9]*' | head -1)
  if [ "$RC" -ne 0 ] || [ -z "$BASE" ]; then V="CRASH(rc=$RC)"
  elif [ "$BASE" = "基址 = 0x6C2C0000" ] && [ "$ST" = "状态码 = 0" ]; then V="OK"
  else V="WRONG  $BASE  $ST"; fi
  printf "%-64s %6s %s\n" "$F" "$SZ" "$V"
}

B="-O1 -fno-tree-loop-distribute-patterns"
run_one "$B"
run_one "$B -fno-strict-aliasing"
run_one "$B -fno-strict-overflow"
run_one "$B -fwrapv"
run_one "$B -fno-tree-sra"
run_one "$B -fno-ipa-sra"
run_one "$B -fno-ivopts"
run_one "$B -fno-tree-dominator-opts"
run_one "$B -fno-gcse"
run_one "$B -fno-dce"
run_one "$B -fno-tree-ccp"
run_one "$B -fno-inline"
run_one "$B -fno-tree-vrp"
run_one "$B -fno-tree-pre"
run_one "$B -fno-ipa-pure-const"
run_one "$B -fno-ipa-reference"
run_one "$B -fno-ipa-icf"
run_one "$B -fno-tree-dse"
run_one "$B -fno-tree-ter"
run_one "$B -fno-tree-fre"
echo "------------------------------------------------------------------------------------------"
