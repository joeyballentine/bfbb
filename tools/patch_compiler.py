#!/usr/bin/env python3
"""Derive the patched CodeWarrior used to build the SB library.

GC/2.0p1a
---------
mwcceppc.exe GC/2.0p1 answers memory-disambiguation questions in two places,
and this patch narrows both. Both live in Alias.c (the file names are still in
the binary, in the CError_FATAL call sites, which is how the modules below
were identified):

  * the **instruction scheduler**'s may-alias predicate at 0x511fc0, which
    dispatches through a 3x3 operand-kind table at VA 0x5bd0bc. Clauses A, B,
    C, C+ and E3n below all hang off that table.
  * the **code generator's redundant-load elimination** -- local value
    numbering, ValueNumbering.c around 0x509010 -- whose store-kill routine is
    0x511a30, dispatching through a *3*-entry table at VA 0x5bd068 keyed on the
    stored memref's kind. Clause V hangs off that table.

The scheduler's case 0 -- both references opaque -- answers "may alias" only
when the two descriptors are literally the same object. That is more
aggressive than the compiler that built the retail DOL: a float constant
loaded from the .sdata2 literal pool gets hoisted above a store it is assumed
not to touch. This is the long-standing "float meme", e.g. in
zEntCruiseBubble's hide_hud:

    retail  lis li addi stw lwz lfs stfs blr
    stock   lis li addi lfs stw lwz stfs blr

Answering "may alias" unconditionally for case 0 recovers most of the retail
schedules but costs eleven translation units, so the patch installs narrow
predicates instead and points selected dispatch entries at them. Anything not
matching a clause falls through to the stock test for that entry, which the
injected code replicates exactly.

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

Clause C -- clause A for static storage; reached on entry 1, and on entry 3
when clause E3n declines. Entry 0 runs clause C+ instead. Checked first:

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

Clause C+ -- clause C with the indirect-access bit tolerated, entry 0 only:

    as clause C, except that flags bit 0x20 is permitted (mask ~0xa6) and
    the size test applies only to the operand that is not the store

Instruction flags bit 0x20 appears on an access made *through* a base object
rather than at a fixed offset in one -- `stw r0, 0x4(r31)`, r31 loaded from a
static pointer, sets it -- and such a memref reports the size of the whole
pointee rather than the width of the access. That reading is inferred from
the measurement, not read out of the compiler: of the four bits clause C
excludes (0x08, 0x10, 0x20, 0x40) only 0x20 unlocks anything, and every site
it unlocks is an indirect store. Clause C's "flags & ~0x86 == 0" and
"sizeof <= 4" each independently reject those pairs (measured: adding either
one back to clause C+ costs all 19 functions), which is why retail's refusal
to hoist a small static load across such a store was not reproduced.
Tolerating 0x20 and capping only the load's size recovers 19 functions
(xFXShineUpdate, xFXRingUpdate, xFXAuraAdd, xFXStreakUpdate, xPadUpdate,
xSndPlayInternal, HAZ_Acquire, zLOD_UseCustomTable, ...) with no function
dropping from 100.0 anywhere in the tree, and no object of any complete unit
changing at all.

It is entry 0 only because the same relaxation on entries 1 and 3 measures
+0/-3: it drops zPickupTableInit, iSndPrepStream and zEntPlayer_SNDStop, all
three by *sinking* a `lwz` of a pointer field out of a global -- retail
scheduled that load earlier, and the extra edge loses it a tiebreak. The
split was measured by installing the relaxed clause on one entry at a time:
every one of the 19 gains is on entry 0 and every one of the 3 losses is on
entries 1/3.

Clause E3n -- entry 3 only, replacing clause C there:

    the first instruction is a plain store and the second a plain load
    and sizeof(A) <= 4 and sizeof(B) <= 4
    and A's base expression is a declared frame object (word 0x00010005)
        with a non-zero field at +0x18
    and B's base expression is a plain static object (word 5)

A directional rule: an stfs to a declared frame local may not be crossed by a
*later* small static load. Installed on entry 3; worth +82 exact functions
net against the same build with entry 3 back on clause C (measured 2026-08-21:
removing it is +6/-88), so despite the earlier warning about refitting this
shape it stays.

Clause V -- the redundant-load path, value-numbering store kill, entry 0 of
the table at 0x5bd068 only:

    the stored memref's base expression is a plain static object (word 5)
    ->  bump the value number of *every* object in the value-numbering
        object list (head at 0x5e1fd8) that is <= 4 bytes and whose base
        expression is likewise a plain static object

The scheduler patch cannot reach this: a load hoisted by the *scheduler* is a
reordering, but the defect here is that consecutive statements share one
literal load. A 20-line repro (`extern F32 a1..a3;` + `a1 = DEG2RAD(a1); a2 =
...`) shows it: retail emits `lfs f2,@PI / lfs f1,val / lfs f0,@180 / fmuls /
fdivs / stfs` per statement, stock loads @PI and @180 once for the whole
block. mwcc's local value numbering caches each object's value number in
memref+0x1c; a store bumps its own object's number (and its precomputed alias
sets) through 0x511a30, so the constant pool entry survives the store and the
second statement reuses the register. Retail's compiler kills it. Clause V
makes a store to a small static do so, and only then.

Both halves of clause V are needed and both are narrow:

  * gating the *store* on a static base is what admits the interesting
    population. Gating it on "size <= 4" instead measures +11 rather than
    +25; adding "size <= 4 or the store is indirect" on top of the static
    gate changes nothing (measured identical), so it is not shipped.
  * filtering the *killed* objects to small statics is what makes it free.
    Killing the whole list (i.e. calling the stock kill-everything routine at
    0x511a00) measures +26/-24; killing every static regardless of size is
    +10/-0 -- it spares the constants, which are what the gains need;
    filtering on size alone without the static test is +25/-5, losing
    iSphereHitsEnv x3, xPadUpdate and xtextbox::read_tag, all of which cache a
    load through a pointer across a store to a small static, which retail also
    keeps. Filtering on the object's kind byte (+0x2c == 0) instead of its
    base expression is +25/-1 (xPadUpdate).

Entry 0 (a whole object) only. Entry 1 -- a subrange of a larger object, i.e.
`globals.player.g.slideAngle = DEG2RAD(...)` -- measures +1/-30, which is the
compiler agreeing with the observation that made this clause findable: retail
shares the two literals across three consecutive stores into a large named
object and reloads them for stores into small statics. Entry 2 is inert
(measured: no object in the tree changes).

Clause V is worth +25 exact functions and -0 (measured 2026-08-21 over all
451 units): zMainParseINIGlobals, zEntPlayerReset, zEntPlayerDriveUpdate,
LCopterCB, BubbleBounceCB, zEntPlayer_SNDPlayDelayed, zCameraReset,
xScrFxLetterBoxInit, xScrFxLetterboxReset, xScrFxDistortionUpdate,
xSndDelayedUpdate, xSndPlay3DFade, xSndStopFade, xCameraFXAlloc,
xCutscene_Init, xDecal's register_emitter, xFX's activate_ribbon,
xFXStreakStart, xShadowSimple_Init, zEntPickup_UpdateFlyToInterface,
AddToLODList, zNPCBPatrick::Reset, zNPCFodBzzt::Init,
zParCmdFindClipVolumes and zFruit_Update. 27 of 451 objects change and none
belongs to a complete unit.

Do not relax clause C's static-storage gate on the store side. "Skip the
base-expression test for whichever operand is the store, keep it for the
load" is the obvious reading of the retail behaviour above, and it measures
-80 (+29/-109) across the tree. The reason is that it cannot reach the
motion it was aimed at: the indirect stores in question never satisfy clause
C's size and flags tests in the first place (see clause C+), and a store that
does reach clause C with a non-static base is always a declared frame object
(measured: allowing every non-frame store changes nothing at all, allowing a
store with no base expression changes nothing at all). All the relaxation
admits is stack traffic, which is the population the gate exists to exclude.

The object-link and computed-address conditions are fitted: they separate named
objects from spill slots and computed-address locals, which is a coherent
reading, but it was not derived from the retail compiler. The differing-opcode
condition in clause A is likewise fitted, and clause C is clause A plus a
fitted storage-class gate. Extending clause B to entries 1 and 3
adds no new condition -- it is the same predicate on two more dispatch cases.

The predicate is assembled into the run of zero padding at the tail of .text:
the section declares VirtualSize 0x17da4c but occupies 0x17dc00 bytes on disk,
leaving file 0x17de4c..0x17e000 (VA 0x57ea4c, 436 bytes) mapped executable,
zeroed and unreachable. Writing there leaves the file size, the section table
and every existing address untouched.

The injected code is position-independent, and has to be: sjiswrap loads the
image at 0x110000 rather than its preferred 0x400000, so an absolute address
written into the cave is not relocated and faults. It inlines the stock tests
rather than re-entering them, reaches the epilogue through rel32 jumps, and
clause V reads the object-list head through a `call/pop` PC-relative
displacement. The only absolute values written are the dispatch table entries
themselves, and every entry in both tables already carries a HIGHLOW
relocation, so the new ones are fixed up exactly as their neighbours are.

Padding is the binding constraint -- 436 bytes for everything, 413 in use --
so nothing is duplicated. Clause B's six tests are one body reached by CALL
from the entry-0 and entry-1/3 handlers, clause C's strict conditions are one
body shared by the two stubs, and the two stock answers share one
`sete bl / and ebx,1 / jmp` tail. A clause answers "may alias" by discarding
its return address and jumping to the caller's epilogue, and declines with
`ret`. The predicates run with esp inside the may-alias frame, which has no
locals live at the dispatch, so the pushed return address is harmless.

That compaction was validated before clause V was added: rebuilt from
GC/2.0p1 with the new layout and the same three scheduler entries, all 451
units compile to objects with **identical SHA-1s** and not one symbol's match
percentage moves.

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
PATCHED_SHA1 = "918652d8063c37ff4d172244f4fcbfa88e0ea062"

# Everything the patch injects, assembled as one position-independent block at
# VA 0x57ea4c -- the whole of the .text tail padding. 413 bytes of 436. The
# layout, in order:
#
#   0x57ea4c  entry-0 handler: clause A inline, clause B by CALL, then the
#             stock reference-identity test
#   0x57ea85  the shared `sete bl / and ebx,1 / jmp 0x51210b` answer tail
#   0x57ea90  the shared "may alias" answer (mov ebx,1)
#   0x57ea9a  entry-1/3 handler: clause B by CALL, then the stock same-base
#             test
#   0x57eaa7  clause B, shared; answers by discarding the return address
#   0x57ead1  entry-1/3 stub: CALL clause C's strict conditions, else fall
#             through to the entry-1/3 handler
#   0x57ead8  entry-0 stub: CALL clause C+, else fall through to the entry-0
#             handler
#   0x57eae2  clause C's conditions that clause C+ drops (neither instruction
#             indirect, both memrefs <= 4 bytes); falls into clause C+
#   0x57eb03  clause C+: differing opcodes, plain load/store with the indirect
#             bit tolerated, load-side size <= 4, static base on both sides
#   0x57eb5e  clause E3n (entry 3); declines into the entry-1/3 stub
#   0x57eba5  clause V: the value-numbering store kill, which falls through to
#             the stock whole-object kill at 0x511a53 either way
CAVE_OFFSET = 0x17DE4C
CAVE_BYTES = bytes.fromhex(
    "8b481883f904772f8b4a1883f9047727668b4e20663b4d2074188b4e14f7c1f9"
    "ffffff75128b4d14f7c1f9ffffff7507eb12e82400000039d00f94c383e301e9"
    "7b36f9ffbb01000000e97136f9ffe8080000008b58103b5a10ebde837e140475"
    "23837d1404751d837808007417837a0800741183780c00750b837a0c00750583"
    "c404ebc0c3e80c000000ebc2e826000000e96affffff8b4e14f6c12075188b4d"
    "14f6c12075108b481883f90477088b4a1883f9047601c3668b4e20663b4d2074"
    "508b4e14f7c159ffffff7545f6c10475088b481883f90477388b481085c97431"
    "833905752c8b4d14f7c159ffffff7521f6c10475088b4a1883f90477148b4a10"
    "85c9740d833905750883c404e92435f9ffc3837e1404753c837d140275368b48"
    "1883f904772e8b4a1883f90477268b481085c9741f8139050001007517837918"
    "0074118b4a1085c9740a8339057505e9e134f9ffe92cffffff8b4b1085c97438"
    "833905753355e8000000005d8bad2134060085ed7421837d180477168b4d1085"
    "c9740f833905750a6a0055e8e4b6f8ff59598b6d00ebdb5de96a2ef9ff"
)

# Entries of the scheduler's may-alias dispatch table at VA 0x5bd0bc that get
# redirected, as (index, expected stock handler, replacement). Entry 0 is
# whole-object vs whole-object; entries 1 and 3 are whole-object vs subrange,
# in both operand orders. Cases 2 and 4-8 are left alone (extending clause B
# to entry 4 was measured at -199 exact functions; even gated to static
# storage it loses 21).
DISPATCH_OFFSET = 0x1BA6BC
DISPATCH = (
    (0, 0x00511FF2, 0x0057EAD8),  # stock: cmp eax,edx; sete bl  (ref identity)
    (1, 0x00511FFF, 0x0057EAD1),  # stock: same base object
    (3, 0x00511FFF, 0x0057EB5E),  # clause E3n instead of clause C
)

# Entries of the value-numbering store-kill dispatch table at VA 0x5bd068,
# consulted by 0x511a30 on the kind byte of the stored memref. Entry 0 is a
# whole object; entry 1 (a subrange) measures +1/-30 and entry 2 is inert, so
# both are left alone.
VN_DISPATCH_OFFSET = 0x1BA668
VN_DISPATCH = (
    (0, 0x00511A53, 0x0057EBA5),  # stock: kill this object and its alias sets
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

    off, blob = CAVE_OFFSET, CAVE_BYTES
    if any(data[off:off + len(blob)]):
        sys.exit(f"{src}: padding at {off:#x} is not zero, refusing to overwrite")
    data[off:off + len(blob)] = blob

    for table, entries in ((DISPATCH_OFFSET, DISPATCH),
                           (VN_DISPATCH_OFFSET, VN_DISPATCH)):
        for index, stock, replacement in entries:
            offset = table + 4 * index
            found = struct.unpack_from("<I", data, offset)[0]
            if found != stock:
                sys.exit(
                    f"{src}: dispatch entry {index} at {offset:#x} is "
                    f"{found:#x}, expected {stock:#x}"
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
