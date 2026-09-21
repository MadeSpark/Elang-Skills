#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# sweep_state.sh —— 快速扫 m_dwState 取值，定位「e.exe 打开声明我们库的 .e 会崩」
# 是否由 m_dwState 引起。
#
# ⚠️ 坑：mingw 的 ld.exe 打不开“含中文的绝对输出路径”（会报 No such file or directory）。
#    所以必须先 cd 进 addin/ 再用相对路径输出（与 build.sh 一致）。
# ---------------------------------------------------------------------------
set -uo pipefail
export PATH="/c/msys64/mingw32/bin:$PATH"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE/addin"
PROJ="$HERE/projD_lib.e"

run_case () {
  local name="$1"; local define="$2"
  echo "=================== $name   LI_LIB_STATE = $define ==================="
  if ! g++ -m32 -shared -O2 -D__GCC_ -DWIN32 -fpermissive -DPOC_SELFCHECK \
        "-DLI_LIB_STATE=$define" \
        -finput-charset=UTF-8 -fexec-charset=GBK -I"/d/ides/e/sdk/cpp/elib" \
        src/elang_addin.cpp src/elang_addin.def \
        -o elang_addin_diag.fne -static -static-libgcc -static-libstdc++ -s 2>/tmp/gxx_err.txt; then
    echo "  !! compile FAILED:"; grep -i "error" /tmp/gxx_err.txt | head -3; return
  fi
  python "$HERE/opens_decl_e.py" "$PROJ"
}

run_case "A_etools_like"      "(OS_ALL|LBS_IDE_PLUGIN|LBS_FUNC_NO_RUN_CODE)"
run_case "B_win_only"         "(__OS_WIN|LBS_IDE_PLUGIN)"
run_case "C_win_norun"        "(__OS_WIN|LBS_IDE_PLUGIN|LBS_FUNC_NO_RUN_CODE)"
run_case "D_all_norun_noflg"  "(OS_ALL|LBS_FUNC_NO_RUN_CODE)"
