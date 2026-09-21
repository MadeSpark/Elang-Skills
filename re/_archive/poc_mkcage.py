#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
mkcage.py —— 给「用户的 .e」加壳：让它在被 e.exe 打开时自动加载 lib\elang_addin.fne

原理（已由 re/verify_libsource.py 受控实验证实）：
  t2e 读「支持库清单」的**唯一有效通道**是工程文本里的 `config/lib.config.json`
  （中文布局时为 `配置/支持库.config.json`）；在文本 `类/*.e.txt` 里写 `.支持库 <key>`
  **不生效**。

步骤（不做任何 e.exe/GUI 操作，纯离线）：
  1) e2t 把「用户 .e 的副本」转成文本目录（-level 2 -enc UTF-8）；此时尚未声明本库，
     不会触发「加载支持库失败」。
  2) 在 <文本目录>/config/lib.config.json 追加一项本库记录（保留 BOM、sort_keys+indent=4）。
  3) t2e 输出到 <输出 .e>（父目录先建）。
  4) 双判据验收：① 产物 md5 != 用户原件；② 产物二进制含 ASCII 'elang_addin'。
  5) ⚠️ 加壳之后**不要再 e2t**（该 .e 声明了本机 lib\ 里可能没装的库，e2t 会失败）。

用法：
  python re/mkcage.py <user.e> [<out.e>] [--keep] [--fne <elang_addin.fne>]
  E2TXT 环境变量可指定 e2txt.exe 路径。

★ 加固（team-lead 2026-09-21 要求）：本库的 Guid / 库名 / 版本**从 .fne 现取**
  （调用 `offset_probe.exe --dump-libinfo <fne>`），避免 .fne 重建后 Guid 漂移
  导致“加壳产物声明旧 Guid → e.exe 加载失败”。取不到时才回退到内置常量。
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

E2TXT = os.environ.get("E2TXT") or r"D:\Tools\e2txt\e2txt.exe"

# --- 本库“加壳”默认参数（回退值；正常应从 .fne 现取） -----------------------
LIBKEY = "elang_addin"
LIBGUID = "7a1e4f22c3b0499e8d6a0011223344fe"
LIBNAME = "AI调试宿主"
LIBVER = {"Major": 1, "Minor": 0}

HERE = os.path.dirname(os.path.abspath(__file__))
PROBE = os.path.join(HERE, "addin", "offset_probe.exe")
DEFAULT_FNE = os.path.join(HERE, "addin", "elang_addin.fne")

# 候选配置文件相对路径（英文布局优先，兼容中文布局）
CFG_CANDIDATES = [
    os.path.join("config", "lib.config.json"),
    os.path.join("配置", "支持库.config.json"),
]


def md5f(p):
    h = hashlib.md5()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(65536), b""):
            h.update(c)
    return h.hexdigest()


def dump_libinfo(fne):
    """从 .fne 现取 (key, guid, name, major, minor)；失败则回退到内置常量。

    通过 offset_probe.exe --dump-libinfo <fne> 读取 —— 它 LoadLibrary 该 .fne
    并调用其 GetNewInf()，打印机器可读的 KEY/GUID/NAME/MAJOR/MINOR。
    GUID 统一小写（与 e2t/t2e 写 config 的约定一致；e.exe 对大小写不敏感，
    已由任务D 证实：config 用小写、.fne 声明大写，仍成功加载）。
    """
    info = {"key": LIBKEY, "guid": LIBGUID, "name": LIBNAME,
            "major": LIBVER["Major"], "minor": LIBVER["Minor"], "src": "fallback"}
    if not os.path.exists(PROBE):
        print("      [加固] 未找到 %s，使用内置回退值" % PROBE)
        return info
    if not os.path.exists(fne):
        print("      [加固] 未找到 .fne: %s，使用内置回退值" % fne)
        return info
    try:
        p = subprocess.run([PROBE, "--dump-libinfo", fne],
                           capture_output=True, timeout=60)
        kv = {}
        for ln in p.stdout.decode("gbk", errors="replace").splitlines():
            if "=" in ln:
                k, _, v = ln.partition("=")
                kv[k.strip()] = v.strip()
        if not kv.get("GUID"):
            raise RuntimeError("probe 无 GUID 输出")
        info.update({
            "key": kv.get("KEY") or LIBKEY,
            "guid": (kv.get("GUID") or LIBGUID).lower(),
            "name": kv.get("NAME") or LIBNAME,
            "major": int(kv.get("MAJOR") or LIBVER["Major"]),
            "minor": int(kv.get("MINOR") or LIBVER["Minor"]),
            "src": os.path.basename(PROBE),
        })
    except Exception as e:  # noqa: BLE001 - 任何失败都回退，不阻断加壳
        print("      [加固] probe 失败(%s)，使用内置回退值" % e)
    return info


def run_e2txt(mode, src, dst, extra=None):
    cmd = [E2TXT, "-mode", mode, "-src", src, "-dst", dst, "-level", "2"]
    if extra:
        cmd += extra
    p = subprocess.run(cmd, capture_output=True, timeout=900)
    so = p.stdout.decode("gbk", errors="replace")
    se = p.stderr.decode("gbk", errors="replace")
    return p.returncode, so, se


def find_cfg(txt_dir):
    for rel in CFG_CANDIDATES:
        p = os.path.join(txt_dir, rel)
        if os.path.exists(p):
            return p
    # 兜底：递归找 lib/config 相关的 .json
    for root, _dirs, files in os.walk(txt_dir):
        for fn in files:
            low = fn.lower()
            if low.endswith(".json") and ("lib" in low or "支持库" in fn):
                return os.path.join(root, fn)
    return None


def patch_cfg(cfg_path, info):
    """追加本库记录（用 info 提供的 key/guid/name/version）；返回 (是否新增, 记录数, 是否已存在)"""
    raw = open(cfg_path, "rb").read()
    had_bom = raw.startswith(b"\xef\xbb\xbf")
    with open(cfg_path, "r", encoding="utf-8-sig") as f:
        data = json.load(f)
    if not isinstance(data, list):
        raise RuntimeError("lib config 不是数组: %r" % type(data))
    existed = any(isinstance(it, dict) and it.get("Key") == info["key"] for it in data)
    if not existed:
        data.append({
            "CmdCount": 0,
            "Guid": info["guid"],
            "Key": info["key"],
            "MaxRefConstPos": 0,
            "MaxRefObjectPos": 0,
            "Name": info["name"],
            "Version": {"Major": info["major"], "Minor": info["minor"]},
        })
    with open(cfg_path, "w", encoding="utf-8-sig" if had_bom else "utf-8", newline="\n") as f:
        json.dump(data, f, ensure_ascii=False, indent=4, sort_keys=True)
        f.write("\n")
    return (not existed), len(data), existed


def mkcage(user_e, out_e=None, keep=False, fne=None):
    user_e = os.path.abspath(user_e)
    if not os.path.exists(user_e):
        raise SystemExit("找不到输入: %s" % user_e)
    if out_e is None:
        stem, ext = os.path.splitext(user_e)
        out_e = stem + "_ai" + (ext or ".e")
    out_e = os.path.abspath(out_e)
    if fne is None:
        fne = DEFAULT_FNE
    info = dump_libinfo(fne)
    print("[0/4] 本库信息(来自 %s): KEY=%s GUID=%s NAME=%s VER=%d.%d"
          % (info["src"], info["key"], info["guid"], info["name"],
             info["major"], info["minor"]))

    tmp = tempfile.mkdtemp(prefix="mkcage_")
    txt_dir = os.path.join(tmp, "text")
    try:
        print("[1/4] e2t: %s -> %s" % (user_e, txt_dir))
        rc, so, se = run_e2txt("e2t", user_e, txt_dir, ["-enc", "UTF-8"])
        if rc != 0 or "[错误]" in se or not os.path.isdir(txt_dir):
            raise SystemExit("e2t 失败 rc=%s\nSTDOUT:%s\nSTDERR:%s" % (rc, so[-400:], se[-400:]))

        cfg = find_cfg(txt_dir)
        if not cfg:
            raise SystemExit("在文本目录里找不到支持库配置文件")
        print("[2/4] patch lib config: %s" % cfg)
        added, total, existed = patch_cfg(cfg, info)
        print("      已存在=%s 新增=%s 现有记录数=%d" % (existed, added, total))

        os.makedirs(os.path.dirname(out_e), exist_ok=True)
        print("[3/4] t2e: %s -> %s" % (txt_dir, out_e))
        rc, so, se = run_e2txt("t2e", txt_dir, out_e)
        errs = se.count("[错误]")
        # 注意：此时 lib\ 里通常**没有**装 elang_addin.fne，t2e 会报一条
        #   「引入支持库失败！…[原因] 支持库不存在」——这是**预期且非致命**的：
        #   它仍然会把库记录写进 .e（SUCC:）。真正判据见下面双判据。
        if rc != 0 or "SUCC:" not in so or not os.path.exists(out_e):
            raise SystemExit("t2e 失败 rc=%s errs=%d\nSTDOUT:%s\nSTDERR:%s"
                             % (rc, errs, so[-400:], se[-400:]))
        if errs:
            for ln in se.strip().splitlines():
                if "[错误]" in ln:
                    print("      (t2e 提示，非致命) %s" % ln.strip()[:160])

        m_user = md5f(user_e)
        m_out = md5f(out_e)
        blob = open(out_e, "rb").read()
        has = info["key"].encode("ascii") in blob
        print("[4/4] 验收：")
        print("      用户原件 大小=%d md5=%s" % (os.path.getsize(user_e), m_user))
        print("      加壳产物 大小=%d md5=%s" % (os.path.getsize(out_e), m_out))
        print("      ① md5 不同 = %s" % (m_out != m_user))
        print("      ② 含 ASCII '%s' = %s" % (info["key"], has))
        ok = (m_out != m_user) and has
        print("      == %s ==" % ("PASS" if ok else "FAIL"))
        if not ok:
            raise SystemExit("加壳验收未通过")
        print("产物: %s" % out_e)
        print("提醒：加壳后**不要**再 e2t 该 .e（其中声明的库本机可能未安装）。")
        return out_e
    finally:
        if not keep:
            shutil.rmtree(tmp, ignore_errors=True)
        else:
            print("(保留临时文本目录: %s)" % tmp)


def main():
    argv = sys.argv[1:]
    keep = "--keep" in argv
    fne = None
    pos = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--keep":
            i += 1; continue
        if a == "--fne":
            fne = argv[i + 1]; i += 2; continue
        if a.startswith("--"):
            i += 1; continue
        pos.append(a); i += 1
    if not pos:
        print(__doc__)
        return 2
    user_e = pos[0]
    out_e = pos[1] if len(pos) > 1 else None
    mkcage(user_e, out_e, keep, fne)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
