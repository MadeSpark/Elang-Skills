# -*- coding: utf-8 -*-
"""实测 AutoLinker.fne 能否被 LoadLibrary 加载 + GetNewInf 是否导出。"""
import ctypes
import sys

path = sys.argv[1]
try:
    lib = ctypes.WinDLL(path)
except OSError as e:
    print("LOAD FAIL:", e)
    sys.exit(1)
print("LOAD OK, handle=", lib._handle)
try:
    fn = lib.GetNewInf
    print("GetNewInf exported at", hex(ctypes.cast(fn, ctypes.c_void_p).value or 0))
except AttributeError:
    print("GetNewInf NOT exported")
    names = [n for n in dir(lib) if not n.startswith("_")]
    print("exports seen by ctypes:", names[:20])
