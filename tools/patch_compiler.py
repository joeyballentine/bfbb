#!/usr/bin/env python3
"""Derive the patched CodeWarrior used to build the SB library.

GC/2.0p1a
---------
mwcceppc.exe GC/2.0p1 disambiguates memory references in the instruction
scheduler through a 3x3 operand-kind dispatch table at VA 0x5bd0bc, consulted by
the may-alias predicate at 0x511fc0. Case 0 -- both references opaque -- answers
"may alias" only when the two descriptors are literally the same object. That is
more aggressive than the compiler that built the retail DOL: a float constant
loaded from the .sdata2 literal pool gets hoisted above a store it is assumed not
to touch. This is the long-standing "float meme", e.g. in zEntCruiseBubble's
hide_hud:

    retail  lis li addi stw lwz lfs stfs blr
    stock   lis li addi lfs stw lwz stfs blr

Answering "may alias" unconditionally for case 0 recovers most of the retail
schedules but costs eleven translation units. tools/patch_compiler.py instead
installs a narrower predicate and points three dispatch entries at it: entry 0
(whole-object vs whole-object) and entries 1 and 3 (whole-object vs subrange,
in both operand orders). Anything not matching a clause falls through to the
stock test for that entry, which the stubs replicate exactly.

Clause A -- differing opcodes, entry 0 only:

    sizeof(A) <= 4 and sizeof(B) <= 4
    and opcode(A) != opcode(B)
    and both instructions are plain loads/stores (flags & ~0x6 == 0)

Clause B -- identical opcodes, store/store; entries 0, 1 and 3:

    sizeof(A) <= 4 and sizeof(B) <= 4        (entry 0 only; 1 and 3 omit it)
    and opcode(A) == opcode(B)
    and both instructions are plain stores (flags == 4)
    and both memrefs carry an object link (memref+0x08 != 0)
    and neither is a computed-address access (memref+0x0c == 0)

Clause B exists because the scheduler emits no WAW edge between two stores to
distinct named globals: for a block [stw A][li][stw B], tiebreak level 2
(successors with one remaining predecessor) then hoists the li between them.
That the missing edge is specifically store-store is derived from a trace of
the dependency builder and the pick loop on a live compile. Entries 1 and 3
carry the same clause because the same shape occurs when one side is a whole
object and the other a subrange of one -- zGameExtras_NewGameReset stores an
SDA static and five members of a large global, and lands on entry 1.

Clause C -- clause A for static storage; all three entries, checked first:

    both memrefs carry a base expression whose first word is exactly 5
        (an object node with no storage flags: globals, SDA scalars and
        literal-pool entries qualify; frame/stack objects carry 0x00010005
        and are excluded)
    and sizeof(A) <= 4 and sizeof(B) <= 4
    and opcode(A) != opcode(B)
    and both instructions are plain loads/stores (flags & ~0x86 == 0)

The 0x80 bit in that mask is volatility: a volatile access sets it, so the
plain-load/store test of clauses A and B rejects every volatile reference.
That is why zMenu, whose timers are `static volatile F32`, kept hoisting a
literal load across a store to a different small static even with clause C
installed -- the instructions never reached the clause. Tolerating the bit is
only safe behind the static-storage gate; widening entry 0's clause A the same
way pins volatile frame locals and measures -4.

Clause C is what fixes zThrown_Setup and its family: retail keeps a load of a
small global or float literal on its source-order side of a store to a
different small global (stock entries 1/3 only test for the same base object,
so the load hoists). The static-storage gate is what makes it safe: without
it, the same predicate also pins integer-conversion stack traffic
(stw-to-frame-slot vs lfd-of-magic-double) and costs 50 currently-exact
functions; with it the whole tree shows +41 exact functions and no losses.
The frame-object encoding of the gate (base-expr word 0x00010005 vs
0x00000005) was read out of a live compile with the query logger.

The object-link and computed-address conditions are fitted: they separate named
objects from spill slots and computed-address locals, which is a coherent
reading, but it was not derived from the retail compiler. The differing-opcode
condition in clause A is likewise fitted, and clause C is clause A plus a
fitted storage-class gate. Extending clause B to entries 1 and 3
adds no new condition -- it is the same predicate on two more dispatch cases.

Do not refit the obvious next clause. A directional rule -- an `stfs` to a
declared frame local may not be crossed by a *later* small static load, on
entry 3 only -- looks compelling, because four otherwise-finished functions
reduce to exactly that motion. It measures +22 functions to 100% against 18
functions whose match percent drops, one of them (xFont's get_texture_size)
from 100%. The gain and loss populations are indistinguishable in every field
the predicate can see: same opcodes, same sizes, overlapping offsets, same
storage classes, same base-expression words. What separates them is ready-time
and pick-order context -- whether the stored slots are reloaded in the same
block, whether the literal feeds arithmetic or a store -- none of which the
alias predicate is given. stfs-only, the declared-local gate, entry-3-only,
offset thresholds and a literal-only static side were all measured; none
separates them.

The predicate is assembled into the run of zero padding at the tail of .text:
the section declares VirtualSize 0x17da4c but occupies 0x17dc00 bytes on disk,
leaving file 0x17de4c..0x17e000 (VA 0x57ea4c, 436 bytes) mapped executable,
zeroed and unreachable. Writing there leaves the file size, the section table
and every existing address untouched.

The injected code is position-independent: it inlines the stock identity test
rather than re-entering it, and reaches the epilogue through rel32 jumps. The
only absolute value written is the
dispatch table entry itself, and all nine entries in that table already carry
HIGHLOW relocations, so the new one is fixed up exactly as its neighbours are.

Both writes are guarded by the SHA-1 of the input and by the expected bytes at
each offset, so an unexpected build fails loudly instead of being corrupted.
"""

import hashlib
import os
import shutil
import struct
import sys
from pathlib import Path

BASE_VERSION = "GC/2.0p1"
PATCHED_VERSION = "GC/2.0p1a"

BASE_SHA1 = "74bc177b10d1bbe8a60a21a6c0aa86d2dd9c0668"
PATCHED_SHA1 = "7d3ff244fb371e3b15b0becd41ac04b627869ae8"

# Narrow may-alias predicate, assembled at VA 0x57ea50.
CAVE_OFFSET = 0x17DE50
CAVE_BYTES = bytes.fromhex(
    "8b481883f904775a8b4a1883f9047752"
    "668b4e20663b4d2074188b4e14f7c1f9"
    "ffffff753d8b4d14f7c1f9ffffff7532"
    "eb26837e1404752a837d140475248378"
    "0800741e837a0800741883780c007512"
    "837a0c00750ceb00bb01000000e95936"
    "f9ff39d00f94c383e301e94c36f9ff83"
    "7e14047528837d140475228378080074"
    "1c837a0800741683780c007510837a0c"
    "00750abb01000000e91e36f9ff8b5810"
    "3b5a100f94c383e301e90d36f9ff"
)

# Clause C, assembled at VA 0x57eb00, immediately after the first cave block:
# the entries 1/3 copy first, falling through (rel32 jump) to the clause-B
# handler at 0x57eabf, then the entry 0 copy at 0x57eb52, falling through to
# the shipped entry-0 handler at 0x57ea50. Both copies end in the stock test
# for their entry.
CAVE2_OFFSET = 0x17DF00
CAVE2_BYTES = bytes.fromhex(
    "8b481085c9744683390575418b4a1085"
    "c9743a83390575358b481883f904772d"
    "8b4a1883f9047725668b4e20663b4d20"
    "741b8b4e14f7c179ffffff75108b4d14"
    "f7c179ffffff7505e93435f9ffe96dff"
    "ffff8b481085c9744683390575418b4a"
    "1085c9743a83390575358b481883f904"
    "772d8b4a1883f9047725668b4e20663b"
    "4d20741b8b4e14f7c179ffffff75108b"
    "4d14f7c179ffffff7505e9e234f9ffe9"
    "acfeffff"
)

# Entries of the dispatch table at VA 0x5bd0bc that get redirected, as
# (index, expected stock handler, replacement). Entry 0 is whole-object vs
# whole-object; entries 1 and 3 are whole-object vs subrange, in both operand
# orders. Cases 2 and 4-8 are left alone (extending clause B to entry 4 was
# measured at -199 exact functions; even gated to static storage it loses 21).
DISPATCH_OFFSET = 0x1BA6BC
DISPATCH = (
    (0, 0x00511FF2, 0x0057EB52),  # stock: cmp eax,edx; sete bl  (ref identity)
    (1, 0x00511FFF, 0x0057EB00),  # stock: same base object
    (3, 0x00511FFF, 0x0057EB00),
)


def sha1(path: Path) -> str:
    return hashlib.sha1(path.read_bytes()).hexdigest()


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

    if dst.exists() and sha1(dst) == PATCHED_SHA1:
        return True

    # The whole directory is needed: mwcceppc.exe will not start without
    # lmgr326b.dll sitting next to it.
    dst_dir.mkdir(parents=True, exist_ok=True)
    for f in src_dir.iterdir():
        if f.is_file():
            shutil.copy2(f, dst_dir / f.name)

    data = bytearray(src.read_bytes())

    for off, blob in ((CAVE_OFFSET, CAVE_BYTES), (CAVE2_OFFSET, CAVE2_BYTES)):
        if any(data[off:off + len(blob)]):
            sys.exit(f"{src}: padding at {off:#x} is not zero, refusing to overwrite")
        data[off:off + len(blob)] = blob

    for index, stock, replacement in DISPATCH:
        offset = DISPATCH_OFFSET + 4 * index
        found = struct.unpack_from("<I", data, offset)[0]
        if found != stock:
            sys.exit(
                f"{src}: dispatch entry {index} at {offset:#x} is {found:#x}, "
                f"expected {stock:#x}"
            )
        struct.pack_into("<I", data, offset, replacement)

    tmp = dst.with_suffix(".exe.tmp")
    tmp.write_bytes(bytes(data))
    result = sha1(tmp)
    if result != PATCHED_SHA1:
        tmp.unlink()
        sys.exit(f"patched compiler has SHA-1 {result}, expected {PATCHED_SHA1}")
    os.replace(tmp, dst)
    print(f"Patched compiler written to {dst}")
    return True


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("usage: patch_compiler.py <compilers dir | patched mwcceppc.exe>")
    arg = Path(sys.argv[1])
    # Invoked from ninja with $out, i.e. <compilers>/GC/2.0p1a/mwcceppc.exe
    root = arg.parents[2] if arg.name.endswith(".exe") else arg
    if not patch_compiler(root):
        sys.exit(f"{root / BASE_VERSION} not found")
