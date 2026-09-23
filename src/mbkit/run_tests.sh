#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_tests.sh —— 跑完整套热重载验证，把证据落到 re/mbkit_run/evidence/
#
# 用法：bash run_tests.sh [输出目录]
#
# 六个模式（全部 32 位子进程，各自独立进程 —— 崩溃不会互相污染）：
#   wildcard   自证「模块卸载后回调地址失效」可被检测（确定性，不依赖崩溃）
#   ansi       ANSI(GBK) 变体接口功能验证（中文往返）
#   paintprobe 测绘 wkeOnPaintUpdated 的触发条件（结论：绑窗口后根本不触发）
#   mbkit      新架构 5 轮热重载（期望 20 通过 / 0 失败，退出码 0）
#   fault      故障注入：业务层忘记 Detach 就卸载（期望 8 通过 / 0 失败）
#   legacy     旧做法对照（期望崩溃，退出码 139）
#
# ⚠️ 退出码约定：139 = SIGSEGV。对照组崩溃是**预期结果**，不是脚本失败。
# ---------------------------------------------------------------------------
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${1:-$SCRIPT_DIR/../../re/mbkit_run/evidence}"
mkdir -p "$OUT"
cd "$SCRIPT_DIR"

EXE=./host_reload.exe
[ -x "$EXE" ] || { echo "run build.sh first to produce host_reload.exe"; exit 1; }

run() {                       # run <模式> <参数> <日志名>
  local mode="$1" arg="$2" log="$3"
  timeout 200 "$EXE" "$mode" "$arg" "$OUT/$log" >/dev/null 2>&1
  local code=$?
  local summary
  summary="$(grep -a ' SUMMARY' "$OUT/$log" 2>/dev/null | tr -d '\r' | sed 's/^ *//')"
  printf '  %-24s exit=%-4s %s\n' "$mode" "$code" "${summary:-(no SUMMARY line - process died early)}"
  return 0
}

echo "output dir: $OUT"
echo
echo "== 1) mechanism self-check =="
run wildcard   1 ev_1_wildcard.log
run ansi       1 ev_2_ansi.log
run paintprobe 1 ev_3_paintprobe.log

echo
echo "== 2) new design (MbKit) =="
run mbkit      5 ev_4_mbkit_5rounds.log
run fault      1 ev_5_fault_injection.log

echo
echo "== 3) old design control (crash expected) =="
for n in 1 2 3; do
  timeout 200 "$EXE" legacy 2 "$OUT/ev_6_legacy_crash_$n.log" >/dev/null 2>&1
  code=$?
  tailmsg="$(tail -1 "$OUT/ev_6_legacy_crash_$n.log" 2>/dev/null | tr -d '\r' | sed 's/^ *//')"
  printf '  %-24s exit=%-4s last line: %s\n' "legacy #$n" "$code" "$tailmsg"
done

echo
echo "== 4) repeatability (new design x3, expect all 0) =="
for n in 1 2 3; do
  timeout 200 "$EXE" mbkit 2 "$OUT/ev_7_mbkit_repeat_$n.log" >/dev/null 2>&1
  printf '  %-24s exit=%-4s %s\n' "mbkit #$n" "$?" \
    "$(grep -a ' SUMMARY' "$OUT/ev_7_mbkit_repeat_$n.log" | tr -d '\r' | sed 's/^ *//')"
done

echo
echo "evidence files:"
ls -la "$OUT" | sed 's/^/  /'
echo
echo "verdict:"
echo "  * ev_4_mbkit_5rounds.log should be 20/0; ev_5_fault_injection.log should be 8/0;"
echo "    every round in ev_4 should show kernel-init-count=1  => hot reload works."
echo "  * ev_6_legacy_crash_*.log x3 should all exit 139 and stop at the 1st window msg;"
echo "    that is the deterministic crash of the old design."
