#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""判定「交付 out_mbkit/miniblink.e」与「本次 t2e 产物 build_txt_base.e」的
1077 字节差异到底是内容差异还是时间戳类元数据差异。

判据沿用项目既有约定：把两份 .e 都 e2t 回文本，逐文件比 md5。
文本完全一致 => 差异只在二进制元数据（时间戳/内部 id），代码是同一份。
"""
import hashlib
import pathlib
import shutil
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent               # .../测试（HERE = 测试/re/opt）
E2TXT = "D:/Tools/e2txt/e2txt.exe"

A = ROOT / "out_mbkit" / "miniblink.e"
B = HERE / "ecomrep1work" / "build_txt_base.e"
OUT = HERE / "cmp"


def e2t(src: pathlib.Path, dst: pathlib.Path):
    if dst.exists():
        shutil.rmtree(dst)
    dst.mkdir(parents=True)
    cp = subprocess.run([E2TXT, "-mode", "e2t", "-src", str(src), "-dst", str(dst)],
                        capture_output=True)
    out = (cp.stdout or b"").decode("gbk", "replace")
    ok = "SUCC" in out
    print(f"  e2t {src.name:28} -> {'SUCC' if ok else 'ERROR'}")
    if not ok:
        print("   ", out.strip()[:300])
    return ok


def tree_md5(d: pathlib.Path):
    r = {}
    for p in sorted(d.rglob("*")):
        if p.is_file():
            r[str(p.relative_to(d)).replace("\\", "/")] = hashlib.md5(p.read_bytes()).hexdigest()
    return r


def prj_diff_is_benign(da: pathlib.Path, db: pathlib.Path) -> bool:
    """`项目.etprj`（英文名 project.etprj）不是工程内容 —— 那是 e2txt 自己的配置缓存，
    只存 Dest/Source/Level/NameStyle/Encoding 等。两次 e2t 的 -dst 不同，它就必然不同。
    ⇒ 只要差异仅限 `Source`/`Dest` 这类路径键，就算「无内容差异」。"""
    import json
    pa, pb = da / "project.etprj", db / "project.etprj"
    if not (pa.exists() and pb.exists()):
        return False
    try:
        ja = json.loads(pa.read_bytes().decode("gbk", "replace"))
        jb = json.loads(pb.read_bytes().decode("gbk", "replace"))
    except Exception:
        return False
    keys = set(ja) | set(jb)
    diff = {k for k in keys if ja.get(k) != jb.get(k)}
    if diff - {"Source", "Dest"}:
        return False
    print("  (project.etprj 差异仅限路径键 %s —— 属 e2txt 配置缓存，非工程内容)" % sorted(diff))
    return True


def main() -> int:
    for f in (A, B):
        if not f.exists():
            print("缺文件:", f)
            return 2
    print("A 交付 .e   :", A, A.stat().st_size)
    print("B 本次 t2e  :", B, B.stat().st_size)
    print()

    da, db = OUT / "a", OUT / "b"
    if not (e2t(A, da) and e2t(B, db)):
        return 1

    ta, tb = tree_md5(da), tree_md5(db)
    sa, sb = set(ta), set(tb)
    print()
    print("A 文件数", len(ta), " B 文件数", len(tb))
    only_a, only_b = sorted(sa - sb), sorted(sb - sa)
    if only_a:
        print("只在 A:", only_a)
    if only_b:
        print("只在 B:", only_b)

    diff = [k for k in sorted(sa & sb) if ta[k] != tb[k]]
    benign = [k for k in diff if k.endswith(".etprj") and prj_diff_is_benign(da, db)]
    real = [k for k in diff if k not in benign]
    print("二进制不同的文件数:", len(diff), "  其中属配置缓存:", len(benign))
    for k in real:
        print("  ! ", k)

    if not only_a and not only_b and not real:
        print()
        print("结论：两份 .e 的**文本内容逐字节一致**（%d 个内容文件全部相同）" % (len(ta) - len(benign)))
        print("      => 1077 字节差异全部落在二进制元数据（时间戳 / 内部 id），代码同一份。")
        return 0
    print()
    print("结论：文本内容存在真实差异，需要人工看")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
