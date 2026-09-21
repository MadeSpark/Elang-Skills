# 独立复核工程师(F1..F6)的关键断言
import sys, os, re
sys.path.insert(0, r"C:/Users/MadeSpark/Desktop/测试/re/pylib")
import pefile

def imp(dll):
    pe = pefile.PE(dll, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
    names = []
    for e in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        for i in e.imports:
            names.append((e.dll.decode(errors="replace"), (i.name or b"").decode(errors="replace")))
    return pe, names

print("=== F2: krnln.fnr / krnln.fne 是否导入 OutputDebugStringA ===")
for f in [r"D:/ides/e/lib/krnln.fnr", r"D:/ides/e/lib/krnln.fne"]:
    if not os.path.isfile(f):
        print(f"  {f}: 不存在"); continue
    pe, n = imp(f)
    hit = [d for d in n if "OutputDebugString" in d[1]]
    print(f"  {os.path.basename(f)}: 导入总数={len(n)}  OutputDebugString*={hit}")

print()
print("=== F4: e.exe 是否无导出表 / 关键导入 ===")
pe, n = imp(r"D:/ides/e/e.exe")
print("  导出表存在:", hasattr(pe, "DIRECTORY_ENTRY_EXPORT"))
want = ["WaitForDebugEvent", "ContinueDebugEvent", "DebugActiveProcessStop",
        "OutputDebugStringA", "CreateRemoteThread", "OpenFileMappingA", "RegisterWindowMessageA",
        "SetWindowsHookExA", "CallWindowProcA", "SetWindowLongA"]
for w in want:
    hits = sorted({d for d, x in n if x == w})
    print(f"  {w:26s} -> {hits if hits else '未导入'}")

print()
print("=== F5: 主窗口类名串 ENewFrame 是否在 e.exe 中 ===")
data = open(r"D:/ides/e/e.exe", "rb").read()
for s in [b"ENewFrame", b"ScintillaForEIDETools", b"eOutPutControl", b"OutputDebugText"]:
    print(f"  {s.decode():24s} 出现次数={data.count(s)}")

print()
print("=== F3: lib 下 .fne 总数（供核对 77/77）===")
fnes = [f for f in os.listdir(r"D:/ides/e/lib") if f.lower().endswith(".fne")]
print("  lib/*.fne 数量 =", len(fnes))
