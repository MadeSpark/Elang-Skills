import struct, sys

path = sys.argv[1] if len(sys.argv) > 1 else "../../src/mbkit/MbKit.dll"
d = open(path, "rb").read()
print("file=%s size=%d" % (path, len(d)))
print("MZ:", d[:2])
e_lfanew = struct.unpack_from("<I", d, 0x3C)[0]
print("e_lfanew=0x%X" % e_lfanew)
assert d[e_lfanew:e_lfanew+4] == b"PE\0\0", d[e_lfanew:e_lfanew+4]
machine, nsec = struct.unpack_from("<HH", d, e_lfanew + 4)
print("machine=0x%04X nsec=%d" % (machine, nsec))
opt = e_lfanew + 0x18
magic, = struct.unpack_from("<H", d, opt)
print("magic=0x%04X" % magic)
entry, = struct.unpack_from("<I", d, opt + 0x10)
imageBase, = struct.unpack_from("<I", d, opt + 0x1C)
sizeImage, = struct.unpack_from("<I", d, opt + 0x38)
sizeHdr, = struct.unpack_from("<I", d, opt + 0x3C)
ndir, = struct.unpack_from("<I", d, opt + 0x5C)
print("entry=0x%08X imageBase=0x%08X sizeOfImage=0x%X sizeOfHeaders=0x%X ndir=%d"
      % (entry, imageBase, sizeImage, sizeHdr, ndir))
print("=> base+entry = 0x%08X" % (imageBase + entry))
NAMES = {0: "EXPORT", 1: "IMPORT", 2: "RESOURCE", 5: "BASERELOC", 9: "TLS", 13: "DELAYIMP"}
for i in range(ndir):
    rva, sz = struct.unpack_from("<II", d, opt + 0x60 + i * 8)
    if rva or sz:
        print("  dir[%2d] %-10s RVA=0x%08X size=%d  => VA=0x%08X"
              % (i, NAMES.get(i, "?"), rva, sz, imageBase + rva))
secTab = opt + struct.unpack_from("<H", d, e_lfanew + 0x14)[0]
print("--- sections ---")
for s in range(nsec):
    o = secTab + s * 40
    nm = d[o:o+8].rstrip(b"\0").decode("latin1")
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", d, o + 8)
    ch, = struct.unpack_from("<I", d, o + 0x24)
    print("  %-8s VA=0x%08X VSize=0x%-6X Raw=0x%-6X RSize=0x%-6X Ch=0x%08X"
          % (nm, imageBase + vaddr, vsize, raddr, rsize, ch))
