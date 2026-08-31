#!/usr/bin/env python3
"""Derive the patched CodeWarrior used to build the SB library.

GC/2.0p1a
---------
mwcceppc.exe GC/2.0p1 answers memory-disambiguation questions more
aggressively than the compiler that built the retail DOL: a float constant
loaded from the .sdata2 literal pool gets hoisted above a store it is assumed
not to touch (the long-standing "float meme"), a store to a small static does
not kill a cached literal load, and a loop-invariant literal load is hoisted
out of a loop that stores to a static array. This patch narrows those answers
back toward retail's.

The narrowing is expressed as C, not as hand-assembled bytes. It lives in the
CodeWarrior decompilation repo:

    src/compiler_and_linker/BackEnd/PowerPC/GlobalOptimizer/AliasPatch.c

That file holds ONLY the clause predicates -- the part that is the patch. The
compiler's own stock answers (the 3x3 may_alias_alias switch, and the
whole-object kill walks in update_alias_value) are left as its own bytes and
reached by fall-through. Nothing stock is reimplemented, so nothing stock can
diverge: a query the patch does not claim is answered by the identical original
code. This is what the older eight-clause byte cave (A, B, C, C+, E3n, V, F, H)
could not do -- it carried four hand-derived copies of the same switch, one per
inlined call site, because a byte patch cannot call a shared function.

HOW IT IS INJECTED
------------------
Three sites are hooked, each dispatched by an operand-kind table in Alias.c:

    0x5bd068  update_alias_value (0x511a30)  entry 0 -> clauses V and F
    0x5bd074  may_alias_object   (0x511cb0)  pre-dispatch -> clause H
    0x5bd0bc  may_alias          (0x511fc0)  entries 0,1,3 -> A/B/C/C+/E3n

`may_alias` and `may_alias_object` keep the original PCode arguments in esi/ebp
at the dispatch point, so the predicates read opcode and flags directly; eax and
edx hold the two memrefs.

The injected image is one page grown onto the tail of .text (SizeOfRawData
0x17dc00 -> 0x17e000 -> 0x17f000; no VirtualSize, VA or RVA changes, exactly as
the loader already maps the padding past VirtualSize). It holds:

  * five register-marshalling stubs (tools/aliaspatch_asm.py) that hand each
    query to the right C predicate in cdecl form and act on the answer -- jump
    to the compiler's own "may alias" answer tail on a hit, fall into the
    compiler's own stock test on a miss;
  * the C blob. Its linked bytes are checked in as tools/aliaspatch_blob.py,
    so a bare bfbb checkout derives the compiler with no external repo. When
    the mwcc-gc repo is present (MWCC_GC env var, or its default location),
    tools/aliaspatch_link.py recompiles AliasPatch.c with that repo's own
    Metrowerks mwcc.exe and refuses to build if the fresh link differs from
    the artefact -- the C stays the source of truth, and a stale artefact is
    a loud failure, not a silent divergence. `aliaspatch_link.py --refresh`
    regenerates the artefact after an intended C change.

The blob is position-independent: every relocation is a REL32 (inter-function
calls and the one call to killmemory), which survives the sjiswrap rebase
because caller and callee move together. The alias-list head is loaded PC-
relatively by the VN stub and passed in, so the blob carries no absolute word
and needs no base relocations of its own. The three redirected dispatch entries
and the CodeMotion hook are the only absolute edits; the dispatch entries
already carry HIGHLOW relocations that rebase their new in-image targets, and
the hook is a rel32 jump with the displaced table-operand relocation retyped to
a padding no-op.

The original 436-byte .text tail cave (VA 0x57ea4c) is left pristine.

All writes are guarded by the SHA-1 of the input and by the expected bytes at
each offset, so an unexpected build fails loudly instead of being corrupted.
The whole clause set now reads as C; changing AliasPatch.c changes the derived
compiler's SHA-1, so re-measure with tools/patchcost.py after any change.
"""

import hashlib
import os
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aliaspatch_link
import aliaspatch_asm

BASE_VERSION = "GC/2.0p1"
PATCHED_VERSION = "GC/2.0p1a"

BASE_SHA1 = "74bc177b10d1bbe8a60a21a6c0aa86d2dd9c0668"
# The C-sourced GC/2.0p1a. This is a NEW hash: the old byte-cave p1a was
# 5c6862b641adb8845f0fc09a6569902df068a83f. The derived-compiler bytes differ;
# the OBJECTS it produces do not (verified byte-identical on 450 SB units).
PATCHED_SHA1 = "e71f58023db2a11619a99ebd3ee411bd0ac72bc6"

# ---- where the injected code goes ---------------------------------------
# The executable tail of .text is the run from VirtualSize's end to the next
# page boundary, which the loader maps executable (the section's raw data stops
# earlier). It spans VA 0x57ea4c..0x57f000 -- 1460 bytes -- and .rdata begins
# at 0x57f000, so nothing may reach that address.
#
#   0x57ea4c  CAVE1  the original 436-byte tail cave, holds the five stubs
#   0x57ec00  the grown page, holds the C blob
#
# CAVE1's file bytes are already inside .text's raw data; the blob's are added
# by growing SizeOfRawData one page (as clause H did), which does not change
# any VA -- the blob must simply fit below EXEC_LIMIT.
CAVE1_VA = 0x0057EA4C
CAVE1_FILE = 0x17DE4C
CAVE1_LEN = 0x1B4                   # 436 bytes to the grown page
PAGE_VA = 0x0057EC00               # VA of the grown page
TEXT_SECTION_GROW = 0x400          # raw bytes added for the blob
TEXT_RAW_INSERT_AT = 0x17E000      # first byte past the original raw .text
EXEC_LIMIT = 0x0057F000            # .rdata starts here; injected code must end below
ALIAS_LIST_HEAD_VA = 0x005E1FD8    # global holding the Alias list head

# ---- scheduler may-alias dispatch table (0x5bd0bc) ----------------------
# Entries 0/1/3 are redirected to the three sched stubs; 2 and 4-8 stay stock
# (extending to entry 4 measures -199 exact functions).
SCHED_DISPATCH_OFFSET = 0x1BA6BC
SCHED_STOCK = {0: 0x00511FF2, 1: 0x00511FFF, 3: 0x00511FFF}

# ---- value-numbering store-kill table (0x5bd068) ------------------------
# Entry 0 (whole object) only; entry 1 measures +1/-30 and entry 2 is inert.
VN_DISPATCH_OFFSET = 0x1BA668
VN_STOCK_E0 = 0x00511A53

# ---- CodeMotion loop-invariance hook (0x511ce5) -------------------------
# Rewrite the 7-byte `jmp [ebx*4+0x5bd074]` into a rel32 jump to the licm stub
# and retype the displaced table-operand HIGHLOW relocation to a no-op.
CM_HOOK_OFFSET = 0x1110E5           # VA 0x511ce5
CM_HOOK_OLD = bytes.fromhex("ff249d74d05b00")
CM_RELOC_OFFSET = 0x1E6A98          # pre-insert file offset of the reloc u16
CM_RELOC_OLD = 0x3CE8               # HIGHLOW @ RVA 0x111ce8
CM_RELOC_NEW = 0x0CE8               # ABSOLUTE (padding no-op), offset kept


def sha1_bytes(b: bytes) -> str:
    return hashlib.sha1(b).hexdigest()


def sha1(path: Path) -> str:
    return hashlib.sha1(path.read_bytes()).hexdigest()


def build_injection():
    """Lay out the C blob and the five stubs in the .text tail.

    The blob comes from aliaspatch_link.blob_for: the checked-in artefact,
    verified against a fresh compile of AliasPatch.c whenever the mwcc-gc repo
    is present. It is placed at PAGE_VA and the stubs, which call into it, at
    CAVE1. Both must end below EXEC_LIMIT (.rdata). Returns (cave1_bytes,
    page_bytes, stub_vas)."""
    A = aliaspatch_asm

    blob, exp = aliaspatch_link.blob_for(PAGE_VA)
    if PAGE_VA + len(blob) > EXEC_LIMIT:
        sys.exit(f"C blob ends at {PAGE_VA + len(blob):#x}, past the executable "
                 f"limit {EXEC_LIMIT:#x}; reduce its size")
    sched = exp["_sb_sched_clause"]
    licm = exp["_sb_licm_clause"]
    vn = exp["_sb_vn_store_kill"]

    at = CAVE1_VA
    parts = []
    vas = {}

    def emit(name, bts):
        nonlocal at
        vas[name] = at
        parts.append(bts)
        at += len(bts)

    emit("s0", A.sched_stub(at, sched, 0, A.STOCK_E0))
    emit("s1", A.sched_stub(at, sched, 1, A.STOCK_E1_E3))
    emit("s3", A.sched_stub(at, sched, 3, A.STOCK_E1_E3))
    emit("licm", A.licm_stub(at, licm))
    emit("vn", A.vn_stub(at, vn, ALIAS_LIST_HEAD_VA))

    stub_len = at - CAVE1_VA
    if stub_len > CAVE1_LEN:
        sys.exit(f"stubs are {stub_len} bytes, cave1 holds {CAVE1_LEN}")

    cave1 = b"".join(parts)
    page = bytearray(TEXT_SECTION_GROW)
    page[0:len(blob)] = blob
    return cave1, bytes(page), vas


def _apply(data: bytearray) -> bytes:
    cave1, page, vas = build_injection()

    # 0. write the stubs into the pristine cave1 padding
    if any(data[CAVE1_FILE:CAVE1_FILE + len(cave1)]):
        sys.exit(f"cave1 padding at {CAVE1_FILE:#x} is not zero, refusing to overwrite")
    data[CAVE1_FILE:CAVE1_FILE + len(cave1)] = cave1

    # 1. redirect the scheduler dispatch entries (0,1,3)
    for index, stock in SCHED_STOCK.items():
        o = SCHED_DISPATCH_OFFSET + 4 * index
        found = struct.unpack_from("<I", data, o)[0]
        if found != stock:
            sys.exit(f"sched dispatch entry {index} at {o:#x} is {found:#x}, "
                     f"expected {stock:#x}")
        struct.pack_into("<I", data, o, vas[{0: "s0", 1: "s1", 3: "s3"}[index]])

    # 2. redirect the VN store-kill entry 0
    o = VN_DISPATCH_OFFSET
    found = struct.unpack_from("<I", data, o)[0]
    if found != VN_STOCK_E0:
        sys.exit(f"vn dispatch entry 0 at {o:#x} is {found:#x}, "
                 f"expected {VN_STOCK_E0:#x}")
    struct.pack_into("<I", data, o, vas["vn"])

    # 3. hook the CodeMotion alias dispatch -> licm stub, retype its reloc
    if data[CM_HOOK_OFFSET:CM_HOOK_OFFSET + 7] != CM_HOOK_OLD:
        sys.exit(f"CM hook bytes at {CM_HOOK_OFFSET:#x} are "
                 f"{data[CM_HOOK_OFFSET:CM_HOOK_OFFSET + 7].hex()}, expected "
                 f"{CM_HOOK_OLD.hex()}")
    rel = vas["licm"] - (0x00511CE5 + 5)
    data[CM_HOOK_OFFSET:CM_HOOK_OFFSET + 7] = b"\xE9" + struct.pack("<i", rel) + b"\x90\x90"
    found = struct.unpack_from("<H", data, CM_RELOC_OFFSET)[0]
    if found != CM_RELOC_OLD:
        sys.exit(f"reloc word at {CM_RELOC_OFFSET:#x} is {found:#x}, "
                 f"expected {CM_RELOC_OLD:#x}")
    struct.pack_into("<H", data, CM_RELOC_OFFSET, CM_RELOC_NEW)

    # 4. grow .text's raw data by one page and bump later sections' raw
    #    pointers, then insert the page image (done LAST so every offset above
    #    was written at its pristine location)
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    sec0 = pe + 24 + optsz
    for i in range(nsec):
        so = sec0 + 40 * i
        name = bytes(data[so:so + 8]).rstrip(b"\0").decode()
        rsz, rptr = struct.unpack_from("<II", data, so + 16)
        if name == ".text":
            if rsz != 0x17DC00:
                sys.exit(f".text raw size is {rsz:#x}, expected 0x17dc00")
            struct.pack_into("<I", data, so + 16, rsz + TEXT_SECTION_GROW)
        elif rptr >= TEXT_RAW_INSERT_AT and rptr != 0:
            struct.pack_into("<I", data, so + 20, rptr + TEXT_SECTION_GROW)
    data[TEXT_RAW_INSERT_AT:TEXT_RAW_INSERT_AT] = page
    return bytes(data)


def patch_compiler(compilers: Path) -> bool:
    """Create the patched compiler directory. Returns True if it is present."""
    src_dir = compilers / BASE_VERSION
    dst_dir = compilers / PATCHED_VERSION
    src = src_dir / "mwcceppc.exe"
    dst = dst_dir / "mwcceppc.exe"

    if not src.exists():
        return False

    actual = sha1(src)
    if actual != BASE_SHA1:
        sys.exit(
            f"{src} has unexpected SHA-1 {actual}\n"
            f"  expected {BASE_SHA1}; refusing to patch an unknown build"
        )

    if PATCHED_SHA1 and dst.exists() and sha1(dst) == PATCHED_SHA1:
        return True

    dst_dir.mkdir(parents=True, exist_ok=True)
    # The whole directory is needed: mwcceppc.exe will not start without
    # lmgr326b.dll sitting next to it.
    for f in src_dir.iterdir():
        if f.is_file():
            shutil.copy2(f, dst_dir / f.name)

    # That copy just put the STOCK exe at the PATCHED name. Remove it before
    # doing any work: everything below can sys.exit -- a drifted AliasPatch.c,
    # a moved dispatch entry, a blob past EXEC_LIMIT -- and leaving it behind
    # would strand an unpatched compiler wearing the patched version number.
    # solo.py takes the compiler path from build.ninja without checking its
    # sha1, so that file would quietly hand back stock-compiler numbers. The
    # real write below is atomic, so nothing else recreates it on failure.
    if dst.exists():
        dst.unlink()

    data = bytearray(src.read_bytes())
    out = _apply(data)

    result = sha1_bytes(out)
    if PATCHED_SHA1 is None:
        print(f"NOTE: PATCHED_SHA1 unset; this build is {result}")
    elif result != PATCHED_SHA1:
        sys.exit(f"patched compiler has SHA-1 {result}, expected {PATCHED_SHA1}")

    tmp = dst.with_suffix(".exe.tmp")
    tmp.write_bytes(out)
    os.replace(tmp, dst)
    print(f"Patched compiler written to {dst}  (sha1 {result})")
    return True


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("usage: patch_compiler.py <compilers dir | patched mwcceppc.exe>")
    arg = Path(sys.argv[1])
    # Invoked from ninja with $out, i.e. <compilers>/GC/2.0p1a/mwcceppc.exe
    root = arg.parents[2] if arg.name.endswith(".exe") else arg
    if not patch_compiler(root):
        sys.exit(f"{root / BASE_VERSION} not found")
