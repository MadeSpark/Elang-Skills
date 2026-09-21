#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Static analysis of e.exe: PE headers, sections, imports, exports, strings.
Read-only. Outputs UTF-8 text.
"""
import sys
import io
import math
import hashlib

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

import pefile  # noqa: E402

PATH = r"D:\ides\e\e.exe"


def entropy(b: bytes) -> float:
    if not b:
        return 0.0
    counts = [0] * 256
    for x in b:
        counts[x] += 1
    n = len(b)
    e = 0.0
    for c in counts:
        if c:
            p = c / n
            e -= p * math.log2(p)
    return e


def section_entropy(pe, sec) -> float:
    data = pe.get_data(sec.VirtualAddress, max(sec.Misc_VirtualSize, sec.SizeOfRawData))
    return entropy(data)


def main() -> None:
    with open(PATH, "rb") as f:
        raw = f.read()
    print("=== FILE ===")
    print("path      :", PATH)
    print("size      :", len(raw), "bytes")
    print("sha256    :", hashlib.sha256(raw).hexdigest())
    print("md5       :", hashlib.md5(raw).hexdigest())

    pe = pefile.PE(PATH, fast_load=False)
    ch = pe.FILE_HEADER
    oh = pe.OPTIONAL_HEADER

    print("\n=== FILE_HEADER / OPTIONAL_HEADER ===")
    print("Machine            : 0x%04X %s" % (ch.Machine, pefile.MACHINE_TYPE.get(ch.Machine, "?")))
    print("NumberOfSections   :", ch.NumberOfSections)
    print("TimeDateStamp      : 0x%08X" % ch.TimeDateStamp)
    import datetime
    try:
        print("TimeDateStamp(UTC) :", datetime.datetime.utcfromtimestamp(ch.TimeDateStamp).isoformat())
    except Exception:  # noqa: BLE001
        pass
    print("Characteristics    : 0x%04X" % ch.Characteristics)
    print("Magic              : 0x%04X" % oh.Magic)
    print("Subsystem          : 0x%04X %s" % (oh.Subsystem, pefile.SUBSYSTEM_TYPE.get(oh.Subsystem, "?")))
    print("DllCharacteristics : 0x%04X" % oh.DllCharacteristics)
    print("LinkerVersion      : %d.%d" % (oh.MajorLinkerVersion, oh.MinorLinkerVersion))
    print("OperatingSystemVer : %d.%d" % (oh.MajorOperatingSystemVersion, oh.MinorOperatingSystemVersion))
    print("ImageBase          : 0x%08X" % oh.ImageBase)
    print("AddressOfEntryPoint: 0x%08X" % oh.AddressOfEntryPoint)
    print("SizeOfImage        : 0x%08X" % oh.SizeOfImage)
    print("SizeOfHeaders      : 0x%08X" % oh.SizeOfHeaders)
    print("CheckSum           : 0x%08X" % oh.CheckSum)
    print("NumberOfRvaAndSizes:", oh.NumberOfRvaAndSizes)

    print("\n=== SECTIONS ===")
    print("%-10s %10s %10s %10s %10s %6s %s" % (
        "Name", "VirtAddr", "VirtSize", "RawPtr", "RawSize", "Ent", "Flags"))
    for sec in pe.sections:
        name = sec.Name.rstrip(b"\x00").decode("latin-1")
        print("%-10s 0x%08X 0x%08X 0x%08X 0x%08X %6.3f 0x%08X" % (
            name, sec.VirtualAddress, sec.Misc_VirtualSize, sec.PointerToRawData,
            sec.SizeOfRawData, section_entropy(pe, sec), sec.Characteristics))
    print("Total raw size:", sum(s.SizeOfRawData for s in pe.sections))

    # TLS / Relocation / Debug
    print("\n=== DIRECTORIES ===")
    for entry in oh.DATA_DIRECTORY:
        if entry.VirtualAddress or entry.Size:
            print("%-24s RVA=0x%08X Size=0x%08X" % (
                pefile.DIRECTORY_ENTRY.get(entry.name if hasattr(entry, 'name') else '?', '?'),
                entry.VirtualAddress, entry.Size))
    for dname in ("DIRECTORY_ENTRY_TLS", "DIRECTORY_ENTRY_BASERELOC",
                  "DIRECTORY_ENTRY_DEBUG", "DIRECTORY_ENTRY_LOAD_CONFIG",
                  "DIRECTORY_ENTRY_IMPORT", "DIRECTORY_ENTRY_EXPORT",
                  "DIRECTORY_ENTRY_RESOURCE", "DIRECTORY_ENTRY_BOUND_IMPORT"):
        v = getattr(pe, dname, None)
        print("  %-28s : %s" % (dname, "present" if v else "-"))

    # Imports
    print("\n=== IMPORTS (by DLL) ===")
    if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            dll = entry.dll.decode("latin-1")
            funcs = []
            for imp in entry.imports:
                if imp.name:
                    funcs.append(imp.name.decode("latin-1"))
                else:
                    funcs.append("Ordinal_%d" % imp.ordinal)
            print("\n[%s] (%d funcs)" % (dll, len(funcs)))
            # print 4 per line for compactness
            line = "    "
            for i, fn in enumerate(funcs):
                token = fn + "  "
                if len(line) + len(token) > 140:
                    print(line.rstrip())
                    line = "    "
                line += token
            if line.strip():
                print(line.rstrip())
    else:
        print("(no imports)")

    # Exports
    print("\n=== EXPORTS ===")
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        exp = pe.DIRECTORY_ENTRY_EXPORT
        print("Module name:", exp.name.decode("latin-1") if exp.name else "-")
        print("Number of exports:", len(exp.symbols))
        for s in exp.symbols:
            nm = s.name.decode("latin-1") if s.name else ("Ordinal_%d" % s.ordinal)
            print("  0x%08X %s" % (s.address, nm))
    else:
        print("(no export directory)")

    # Version info
    print("\n=== VERSION INFO ===")
    try:
        if hasattr(pe, "FileInfo"):
            for fileinfo_list in pe.FileInfo:
                for fi in fileinfo_list:
                    if fi.Key.decode("latin-1") == "StringFileInfo":
                        for st in fi.StringTable:
                            for k, v in st.entries.items():
                                print("  %-20s = %s" % (k.decode("latin-1"), v.decode("latin-1")))
    except Exception as e:  # noqa: BLE001
        print("  (version parse error)", e)

    pe.close()


if __name__ == "__main__":
    main()
