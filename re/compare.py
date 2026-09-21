#!/usr/bin/env python
# -*- coding: utf-8 -*-
import sys
import io
import os
import glob

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

Libtoken = "\\lib\\"


def main():
    txt = open("mods_noproj.txt", encoding="utf-8", errors="replace").read()
    loaded = set()
    loaded_paths = {}
    for line in txt.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        name = parts[0]
        path = parts[-1]
        if not path.lower().startswith("d:\\ides\\e\\lib"):
            continue
        if name.lower().endswith((".fne", ".fnr", ".fnl", ".run")):
            loaded.add(name.lower())
            loaded_paths[name.lower()] = path
    libfne = set(os.path.basename(p).lower() for p in glob.glob(r"D:\ides\e\lib\*.fne"))
    print("loaded support libs:", len(loaded))
    print("lib/*.fne:", len(libfne))
    print("lib/*.fne NOT loaded:", sorted(libfne - loaded))
    print("loaded but not top-level lib/*.fne:")
    for x in sorted(loaded - libfne):
        print("   ", x, "->", loaded_paths.get(x))


if __name__ == "__main__":
    main()
