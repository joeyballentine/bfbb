#!/usr/bin/env python3
"""Compare two objects by what ends up in the DOL, ignoring debug info.

A plain `cmp` on two .o files answers the wrong question. Adding or moving a
line of source shifts every DWARF line-number entry, so the objects differ
while the code and data are byte-for-byte the same -- and DWARF is not linked
into main.dol. This compares the sections that are.

Useful when a source change is meant to be codegen-neutral and you want to know
before spending a full build and a DOL hash on it.

    tools/objsame.py old.o new.o
    tools/objsame.py --sections old.o new.o     # list every section's verdict
"""

import argparse
import pathlib
import struct
import sys

# Sections that never reach the DOL.
IGNORED_PREFIXES = (".debug", ".line", ".stab")
IGNORED_EXACT = {".comment", ".shstrtab", ".symtab", ".strtab", ""}

SHT_NOBITS = 8


def sections(path):
    """{name: bytes} for an ELF32 big-endian object. NOBITS carry no bytes."""
    d = pathlib.Path(path).read_bytes()
    if d[:4] != b"\x7fELF":
        sys.exit(f"{path}: not an ELF object")

    e_shoff = struct.unpack(">I", d[0x20:0x24])[0]
    e_shentsize = struct.unpack(">H", d[0x2E:0x30])[0]
    e_shnum = struct.unpack(">H", d[0x30:0x32])[0]
    e_shstrndx = struct.unpack(">H", d[0x32:0x34])[0]

    hdrs = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        name, typ, _flags, _addr, offset, size = struct.unpack(">IIIIII", d[off:off + 24])
        hdrs.append((name, typ, offset, size))

    stroff = hdrs[e_shstrndx][2]
    out = {}
    for name, typ, offset, size in hdrs:
        end = d.index(b"\0", stroff + name)
        n = d[stroff + name:end].decode()
        # A NOBITS section (.bss) has no contents in the file; its size is what
        # matters, so stand in for it with that.
        out[n] = b"" if typ == SHT_NOBITS else d[offset:offset + size]
        if typ == SHT_NOBITS:
            out[n] = b"nobits:%d" % size
    return out


def interesting(name):
    if name in IGNORED_EXACT:
        return False
    return not name.startswith(IGNORED_PREFIXES)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("old")
    ap.add_argument("new")
    ap.add_argument("--sections", action="store_true")
    args = ap.parse_args()

    a, b = sections(args.old), sections(args.new)
    names = sorted(n for n in set(a) | set(b) if interesting(n))
    diffs = [n for n in names if a.get(n) != b.get(n)]

    if args.sections:
        for n in names:
            print(f"  {'same' if a.get(n) == b.get(n) else 'DIFF'}  {n}")

    if diffs:
        print("DIFFERS in " + ", ".join(diffs))
        return 1

    print("identical in everything that reaches the DOL")
    return 0


if __name__ == "__main__":
    sys.exit(main())
