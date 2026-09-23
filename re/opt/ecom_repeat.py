#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""连跑 N 次易模块编译，检查产物是否**每次都**落在期望路径。

背景：保存框是 Vista 风格，目录框和文件名框的「编辑框」类名/控件号完全一样，
按「取第 N 个编辑框」的启发式约 50% 概率把完整路径写进**目录框** ⇒ 易语言退回
「上次目录 + 默认文件名」⇒ 产物落到别处、脚本判 timeout（其实编译是成功的）。

⚠️ 2026-09-24 更正：该对话框**不实现 `CDM_GETFILEPATH`**（恒返回空），
所以「用 CDM 当判据」这条路是**错的**（试过，回退成永不点击、连续 timeout）。
宿主库最终用的是**几何判据** —— 宽 >= 40 的编辑框中**屏幕 y 最靠下的那个**，
写到它之后**立即回读自证**，取不到文本就拒绝点确定。
修后本脚本连跑 3 次，3/3 都落在期望路径（尺寸恒 263957 B）。
"""
import pathlib
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")

ROOT = pathlib.Path(__file__).resolve().parent          # .../re/opt
PY = "C:/Users/MadeSpark/.workbuddy/binaries/python/versions/3.13.12/python.exe"
ECOMPILE = "C:/Users/MadeSpark/.workbuddy/skills/elang-build/scripts/ecompile.py"
SRC = ROOT / "build_txt"                                 # 新模块的文本目录
N = int(sys.argv[1]) if len(sys.argv) > 1 else 3


def main() -> int:
    import time
    bad = 0
    for i in range(1, N + 1):
        t0 = time.time()
        out = ROOT / ("ecomrep%d" % i) / "miniblink.ec"
        work = ROOT / ("ecomrep%dwork" % i)
        subprocess.run(["rm", "-rf", str(out.parent), str(work)])
        cp = subprocess.run(
            [PY, ECOMPILE, str(SRC), str(out), "--kind", "ecom", "--json",
             "--workdir", str(work), "--timeout", "200"],
            capture_output=True)
        text = (cp.stdout or b"").decode("utf-8", "replace")
        ok = status = "?"
        for line in text.splitlines():
            line = line.strip()
            if line.startswith("{"):
                import json
                d = json.loads(line)
                ok, status = d.get("ok"), d.get("status")
        landed = out.is_file()
        print("第 %d 次: ok=%s status=%s 落点正确=%s  尺寸=%s"
              % (i, ok, status, landed, out.stat().st_size if landed else "-"))
        if not landed:
            bad += 1
            # 「落到别处」的同名文件就是那个 bug 的指纹
            strays = []
            for p in ROOT.glob("*/miniblink.ec"):
                try:
                    if p != out and p.stat().st_mtime > t0:
                        strays.append(str(p))
                except OSError:
                    pass
            print("        ✗ 产物不在期望位置；本轮新出现的同名文件: %s"
                  % ("、".join(strays) if strays else "无"))
    print("\n结论：%d/%d 次产物落在期望路径" % (N - bad, N))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
