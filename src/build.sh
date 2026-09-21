#!/usr/bin/env bash
# 编译「导出支持库文档.exe」—— 必须是 32 位编译器（易语言支持库 .fne 都是 32 位 DLL）
#
# 关键编译参数：
#   -finput-charset=UTF-8   源码是 UTF-8
#   -fexec-charset=UTF-8    字符串字面量编成 UTF-8（写进 .md 的内容必须是 UTF-8）
#   -ladvapi32              读注册表以定位易语言安装目录
#
# 注意：Win32 的文件 API 只认 ANSI(ACP)，所以代码里对**路径**单独做了
#       UTF-8 → ACP 转换（见 elibdoc.c 的 u8_to_acp / acp_to_u8）。
#       若改成 -fexec-charset=GBK，写出的 .md 内容会变成 GBK，不要这么干。
set -e
cd "$(dirname "$0")"

GCC="${GCC:-}"
if [ -z "$GCC" ]; then
  for c in /c/msys64/mingw32/bin/gcc.exe /c/mingw32/bin/gcc.exe gcc; do
    if command -v "$c" >/dev/null 2>&1; then GCC="$c"; break; fi
  done
fi
[ -n "$GCC" ] || { echo "找不到 32 位 gcc，请设置 GCC=/path/to/mingw32/gcc.exe"; exit 1; }

# 关键：把编译器的目录加进 PATH。
# 只写 gcc 的全路径、不把它的 bin 目录放进 PATH 时，gcc 找不到自己的
# cc1/as/ld/collect2 子程序，会以 exit 1 **静默失败**（连一行错误都不打印），
# 表现出来就是「脚本跑完但 exe 没更新」。实测踩过。
GCCDIR="$(dirname "$GCC")"
case ":$PATH:" in
  *":$GCCDIR:"*) ;;
  *) PATH="$GCCDIR:$PATH"; export PATH ;;
esac

echo "使用编译器: $GCC"
"$GCC" -O2 -static -finput-charset=UTF-8 -fexec-charset=UTF-8 \
    -o elibdoc.exe elibdoc.c -ladvapi32

cp -f elibdoc.exe "../导出支持库文档.exe"
echo "已生成 $(cd .. && pwd)/导出支持库文档.exe"

# 顺手同步进发布区（发布区只放成品；它可能被挪动，逐个位置试）。
# 目标是**技能包内部**的 assets\，不是发布区根 —— 技能必须自包含：
# 装到任何 AI 工具的技能目录后，这份 exe 要跟着一起过去。
for KIT in "../Releases/Elang-AiTools" "../Elang-AiTools"; do
  if [ -d "$KIT/skills/elang-ai-coding" ]; then
    mkdir -p "$KIT/skills/elang-ai-coding/assets"
    cp -f elibdoc.exe "$KIT/skills/elang-ai-coding/assets/导出支持库文档.exe"
    echo "已同步到 $(cd "$KIT" && pwd)/skills/elang-ai-coding/assets/导出支持库文档.exe"
    break
  fi
done
