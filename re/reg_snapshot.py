# A1：快照 HKCU\Software\FlySky 全树（含原始字节十六进制 dump）
# 用途：为「支持库配置」的 before/after 字节级 diff 留证据
import sys, os, json, winreg, hashlib, datetime

OUT = sys.argv[1] if len(sys.argv) > 1 else r"C:/Users/MadeSpark/Desktop/测试/re/reg_before"
os.makedirs(OUT, exist_ok=True)

ROOT = r"Software\FlySky"


def dump_key(hkey, path, acc, hexdir, prefix=""):
    try:
        k = winreg.OpenKey(hkey, path, 0, winreg.KEY_READ)
    except OSError as e:
        acc.append({"path": path, "error": str(e)})
        return
    with k:
        n_sub, n_val, _ = winreg.QueryInfoKey(k)
        # 枚举值
        for i in range(n_val):
            try:
                name, data, typ = winreg.EnumValue(k, i)
            except OSError:
                break
            rec = {"path": path, "name": name, "type": typ}
            if isinstance(data, bytes):
                rec["size"] = len(data)
                fn = os.path.join(hexdir, (prefix + path + "__" + (name or "(default)")).replace("\\", "_").replace("/", "_") + ".hex")
                with open(fn, "w", encoding="utf-8") as f:
                    b = data
                    for off in range(0, len(b), 16):
                        chunk = b[off:off + 16]
                        f.write(f"{off:08X}  " + " ".join(f"{c:02X}" for c in chunk) + "\n")
                rec["hex_file"] = os.path.basename(fn)
                rec["sha1"] = hashlib.sha1(data).hexdigest()
                rec["head"] = data[:32].hex().upper()
            else:
                rec["value"] = data
            acc.append(rec)
        # 递归子键
        subs = []
        for i in range(n_sub):
            try:
                subs.append(winreg.EnumKey(k, i))
            except OSError:
                break
    for s in subs:
        acc.append({"path": path + "\\" + s, "_key": True})
        dump_key(hkey, path + "\\" + s, acc, hexdir, prefix)


def main():
    hexdir = os.path.join(OUT, "hex")
    os.makedirs(hexdir, exist_ok=True)
    acc = []
    dump_key(winreg.HKEY_CURRENT_USER, ROOT, acc, hexdir)
    meta = {
        "captured_at": datetime.datetime.now().isoformat(timespec="seconds"),
        "root": "HKCU\\" + ROOT,
        "entries": acc,
    }
    with open(os.path.join(OUT, "dump.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=1)
    # 人类可读摘要
    lines = [f"# HKCU\\{ROOT} 快照  {meta['captured_at']}", ""]
    for e in acc:
        if e.get("_key"):
            lines.append(f"[KEY ] {e['path']}")
        elif "error" in e:
            lines.append(f"[ERR ] {e['path']}: {e['error']}")
        elif "size" in e:
            lines.append(f"[BIN ] {e['path']} :: {e['name']}  type={e['type']} size={e['size']} sha1={e['sha1'][:12]} head={e['head']}")
        else:
            v = str(e.get("value"))
            lines.append(f"[STR ] {e['path']} :: {e['name']}  type={e['type']} = {v[:120]}")
    with open(os.path.join(OUT, "summary.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines))
    print(f"\n-> {OUT}")


main()
