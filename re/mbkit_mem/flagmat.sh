#!/usr/bin/env bash
# flagmat.sh —— 编译参数矩阵：判定哪组 flag 编出的 mapkit blob 是「正确」的
# 判据 = 原生侧直调 mk_map 的返回（base 必须 = 0x6C2C0000 且 status = 0）
# 用法：bash flagmat.sh
set -u
export PATH="/c/msys64/mingw32/bin:$PATH"
cd "$(dirname "$0")"

TARGET_MD5=$(md5sum mapkit_text.bin | cut -d' ' -f1)
TARGET_SZ=$(stat -c%s mapkit_text.bin)
echo "出厂 blob: size=$TARGET_SZ md5=$TARGET_MD5"
echo "================================================================================"
printf "%-62s %7s %-34s %s\n" "FLAGS" "BLOB" "RESULT" "MATCH"
echo "================================================================================"

run_one() {
  local F="$1"
  local O=/tmp/mk_flag.o B=/tmp/mk_flag.bin P=/tmp/mk_flag_probe.exe
  rm -f "$O" "$B" "$P"
  if ! gcc -m32 $F -c mapkit.c -o "$O" 2>/tmp/mk_err.txt; then
    printf "%-62s %7s %-34s %s\n" "$F" "-" "COMPILE-FAIL" "-"; return
  fi
  objcopy -O binary -j .text "$O" "$B" 2>/dev/null || { printf "%-62s %s\n" "$F" "OBJCOPY-FAIL"; return; }
  local SZ MD
  SZ=$(stat -c%s "$B")
  MD=$(md5sum "$B" | cut -d' ' -f1)
  if [ "$MD" = "$TARGET_MD5" ]; then MATCH="<<< 同出厂"; else MATCH=""; fi
  if ! gcc -m32 -O0 -o "$P" probe_prol.c "$O" -lkernel32 2>/tmp/mk_lerr.txt; then
    printf "%-62s %7s %-34s %s\n" "$F" "$SZ" "LINK-FAIL" "$MATCH"; sed -n 1,3p /tmp/mk_lerr.txt; return
  fi
  local OUT RC
  OUT=$("$P" o 2>&1)
  RC=$?
  local BASE ST
  BASE=$(printf '%s' "$OUT" | grep -o '基址 = 0x[0-9A-F]*' | head -1)
  ST=$(printf '%s' "$OUT" | grep -o '状态码 = [0-9]*' | head -1)
  local VERDICT
  if [ "$RC" -ne 0 ] || [ -z "$BASE" ]; then
    VERDICT="CRASH(rc=$RC)"
  elif [ "$BASE" = "基址 = 0x6C2C0000" ] && [ "$ST" = "状态码 = 0" ]; then
    VERDICT="OK  $BASE  $ST"
  else
    VERDICT="WRONG $BASE  $ST"
  fi
  printf "%-62s %7s %-34s %s\n" "$F" "$SZ" "$VERDICT" "$MATCH"
}

run_one "-O0"
run_one "-O0 -fno-tree-loop-distribute-patterns"
run_one "-O1 -fno-tree-loop-distribute-patterns"
run_one "-O2 -fno-tree-loop-distribute-patterns"
run_one "-O2 -fno-tree-loop-distribute-patterns -fno-omit-frame-pointer"
run_one "-O2 -fno-tree-loop-distribute-patterns -fomit-frame-pointer"
run_one "-O2 -fno-tree-loop-distribute-patterns -fno-omit-frame-pointer -fno-stack-protector"
run_one "-Os -fno-tree-loop-distribute-patterns"
run_one "-Os -fno-tree-loop-distribute-patterns -fno-omit-frame-pointer"
run_one "-O3 -fno-tree-loop-distribute-patterns"
run_one "-Og -fno-tree-loop-distribute-patterns"
echo "================================================================================"
