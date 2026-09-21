# 判定 .e 里支持库记录到底是「原始 GUID 字节」还是「ASCII key」
import sys, os, hashlib, re

BASE = r"C:/Users/MadeSpark/Desktop/测试/re/projD_base.e"
NEW = r"C:/Users/MadeSpark/Desktop/测试/re/projD/代码.e"
DEMO = r"C:/Users/MadeSpark/Desktop/测试/demos/01-C盘结构输出/项目/代码.e"

OURS_HEX = "7A1E4F22C3B0499E8D6A0011223344FE"
KRNLN_HEX = "d09f2340818511d396f6aaf844c7e325".upper()


def load(p):
    if not os.path.isfile(p):
        print(f"  [缺失] {p}")
        return None
    b = open(p, "rb").read()
    print(f"  {os.path.basename(p):16s} size={len(b)} md5={hashlib.md5(b).hexdigest()[:12]}")
    return b


print("=== 1. 三个 .e 的大小与 md5 ===")
a, b, c = load(BASE), load(NEW), load(DEMO)
if a and b:
    print("  base 与 projD/代码.e 逐字节相同 ?", a == b)
if b and c:
    print("  projD/代码.e 与 demos/01/代码.e 逐字节相同 ?", b == c)

print()
print("=== 2. 关键 GUID 的出现次数 ===")
for name, blob in [("projD_base.e", a), ("projD/代码.e", b), ("demos/01/代码.e", c)]:
    if blob is None:
        continue
    kn = bytes.fromhex(KRNLN_HEX)
    ou = bytes.fromhex(OURS_HEX)
    print(f"  {name:18s} krnln-GUID(原始字节)={blob.count(kn):2d}  我们的GUID(原始字节)={blob.count(ou):2d}"
          f"  b'krnln'={blob.count(b'krnln'):2d}  b'elang_addin'={blob.count(b'elang_addin'):2d}")

print()
print("=== 3. 前 400 字节的十六进制（看文件头结构）===")
if b:
    head = b[:400]
    for off in range(0, len(head), 32):
        chunk = head[off:off + 32]
        print(f"  {off:06X}  " + " ".join(f"{x:02X}" for x in chunk))
