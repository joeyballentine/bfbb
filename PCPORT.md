# PC port — follow-up plan

Phase 2 started. **If you are picking this work up, read
`PCPORT-HANDOFF.md` first** -- it is the operating manual: how to recover the
environment, how to verify a change without fooling yourself, and what is open.
This file is the design record and the argument; `src/SB/Core/pc/README.md`
documents the layer itself. The rest of this is the design record, written
while the decomp is still in progress. Nothing here should change what we work
on day to day except one thing, flagged under **Priority consequence** below.

Target shape: the decompiled game code compiled with a modern toolchain against
a PC platform layer, rendering through **librw**, loading **Xbox-extracted
assets**, shipping as code only.

## The premise this rests on: matching and portable are different goals

Matching means reproducing CodeWarrior's exact codegen for big-endian PowerPC.
A port needs code that is *semantically complete and correct*. Most functions
we have marked `NonMatching` are already fine for a port, and byte-exactness
buys it nothing.

So the gate for phase 1 is **not** the project's headline percentage. It is
"every function in `src/SB/**` is written and behaves correctly." Today that is
**7529 / 7673 game-code functions** (tools/srcprogress.py, 2026-08-26): 7243
exact + 257 codegen-only + 29 reload-only = 92.37% by bytes, against 80.41%
exact. Only 144 functions still need source written, and 153 of 221 units are
source-complete. The remaining library work —
`rwsdk` (1039 functions, 4% matched), `bink` (336, 1.9%), `MSL` (324, 49%) —
is *irrelevant to the port*, and in the case of rwsdk and bink actively so,
since we replace one and delete the other.

**Priority consequence:** finishing rwsdk matching would be ~1039 functions of
effort a port discards. Every `src/SB/**` function is dual-purpose. This is an
independent argument for the game-code-first rule already in DUPLOTRON.md,
and against ever treating rwsdk as the biggest lever just because its function
count is large.

## Phases

### Phase 1 — finish the game code
Gate: all of `src/SB/**` written and semantically correct. Matching status per
function does not matter here; *completeness* does. A `NonMatching` function
that behaves right is portable. A missing function is not.

Worth doing as part of this phase, because it is nearly free while the code is
fresh and expensive later: note every place the code casts a raw asset buffer
to a struct. Those are the phase-4 risk sites (see **Asset caveats**).

**The hazard this gate exists to catch: stubs that match but lie.** Matching
rewards a function that produces the right instructions, and sometimes the
cheapest way to get there is a body that is semantically wrong. A live example
is `zNPCFXCutscenePickTable` in `zNPCFXCinematic.cpp`, currently a placeholder
returning `NULL` because the `g_cutmap` table and its 24 per-cutscene
`NCINEntry` tables (~24 KB of `.data`) do not exist in our source. It is
correctly flagged with a TODO and it lets three neighbouring functions reach
100% — but in a port it means cutscene effects silently never play.

These do not show up as missing functions and they do not show up in
`report.json`. Any function whose body was written to satisfy the diff rather
than to do the job needs finding before phase 2. Grepping for `TODO`,
`return NULL;` one-liners, and empty `{ }` bodies (`tools/stubs.py`) is the
starting point.

### Phase 2 — PC implementations of the interfaces
The codebase already has the seam. `src/SB/Core/x/` is platform-agnostic game
code; `src/SB/Core/gc/` is the GameCube implementation behind 25 `i*` headers.
A port is largely `src/SB/Core/pc/` written against those same headers.

This is a much better starting position than SM64 or OoT had — the abstraction
already exists and is already the boundary the game code respects.

| group | headers | notes |
|---|---|---|
| math | `iMath` `iMath3` `iColor` | likely portable near-verbatim; watch PPC `fres`/`frsqrte` estimate semantics |
| collision | `iCollide` `iCollideFast` | pure computation, should port directly |
| animation | `iAnim` `iAnimSKB` | mostly computation |
| system | `iSystem` `iTime` `iMemMgr` | thin; host clock + allocator |
| input | `iPad` | SDL_GameController |
| audio | `iSnd` | SDL_audio / OpenAL / miniaudio |
| files | `iFile` `isavegame` | host filesystem; saves become files on disk |
| rendering | `iModel` `iDraw` `iLight` `iEnv` `iFX` `iScrFX` `iMorph` `iParMgr` `iCutscene` | **the bulk of the work**, all of it via librw |
| video | `iFMV` | FFmpeg (optional); the Xbox `.xmv` play directly |
| cert | `iTRC` | Nintendo TRC compliance; almost entirely stubbable |
| GC-only | `ngcrad3d` | GameCube radiosity; no PC counterpart, drop or reimplement |

`src/dolphin/` (26 subsystems) is replaced rather than ported:

- `os` → threads, allocation, timing
- `pad`, `si` → SDL input
- `dvd` → filesystem
- `card` → save files
- `ai`, `ax`, `dsp`, `ar` → host audio
- `gx`, `vi` → librw backend
- `mtx` → portable math
- `thp` → GameCube video, dropped
- `exi`, `eth`, `ip`, `upnp`, `hio`, `db`, `gd`, `lg`, `OdemuExi2`,
  `odenotstub`, `amcstubs` → debug/dev/network, dropped

`src/PowerPC_EABI_Support` and `src/runtime_libs` are deleted outright — the
port uses the host libc/libc++.

### Phase 3 — librw
BFBB is RenderWare 3.x. We cannot ship a decomp of Criterion middleware even if
we finish one, so rwsdk gets replaced, not completed.

The precedent is exact: **re3 / reVC** (GTA III, Vice City) are RenderWare games
ported to PC via **librw**, an open-source RW reimplementation. Same middleware
generation, same problem.

This is the phase with the most unknowns. It is also the reason phase 4 picks
Xbox assets.

### Phase 4 — Xbox assets
Use assets extracted from the **Xbox** release rather than the GameCube one.

This is the single highest-leverage decision in the plan, because it solves two
problems at once:

1. **Endianness.** Xbox is little-endian x86. The decompiled code constantly
   casts raw asset buffers straight into structs — placement-new over asset
   data, `(xEntAsset*)`, `(xDynAsset*)`, literal offset arithmetic like
   `stringBase + 0x2e2`. Against big-endian GameCube assets every one of those
   is a byte-order bug. Against Xbox assets they are not.
2. **librw format support.** librw's native-format support is strongest for
   PS2/Xbox/PC and weakest for GameCube. Xbox RenderWare models and textures
   are formats librw already understands; GameCube's are not. Choosing Xbox
   assets aligns the asset pipeline with the renderer we are adopting instead
   of fighting it.

Tooling exists — Industrial Park handles HIP/HOP across platforms.

## Asset caveats — what Xbox assets do NOT solve

Recording these now so nobody rediscovers them at phase 4.

**Pointer width is untouched.** Xbox is 32-bit x86. Asset-overlaid structs still
assume 4-byte pointers. On x86-64 every such struct changes size and layout.
Two options, and this is still open:
- build 32-bit — cheap, layouts line up almost exactly with Xbox assets, but
  caps the port's future;
- separate on-disk formats from in-memory structs — correct and invasive.

**The code is GameCube-derived; the assets would be Xbox-derived.** This is the
new risk the decision introduces, and it needs validating per asset type rather
than assumed. Our struct definitions were reverse-engineered from the CodeWarrior
GameCube binary. Xbox asset files serialize layouts produced by MSVC. Padding,
bitfield ordering and enum width can differ between those compilers. Anything
memory-mapped is exposed.

The split is roughly:
- *Gameplay / logical* assets (entity placement, cutscenes, dialog, config) are
  structurally shared across platforms and differ mainly by byte order — these
  are the ones our code casts directly, so this is where the layout risk lives;
- *Renderable* assets (models, textures) are genuinely platform-native and
  different — but that is fine, because librw wants the Xbox ones anyway.

**The HIP container is big-endian even on Xbox.** Verified 2026-08-26 against a
real Xbox dump. This qualifies the "Xbox assets solve endianness" claim above,
which is true of asset *payloads* and not of the *container* they arrive in:

    boot.HIP:  HIPA ........ PACK 0000009c  PVER 0000000c  PFLG 00000004

Those chunk sizes are only sensible read big-endian, and the format is the same
on every platform -- it is the packer's container, not the console's. The
payloads inside genuinely are little-endian: scanning one level's HOP finds 171
plausible RenderWare chunk headers little-endian and zero big-endian, at library
version 0x1400FFFF (RW 3.4), including type 0x16 texture dictionaries, which is
the Xbox-native form librw handles best.

**And the engine already handles it.** `src/SB/Core/x/xbinio.cpp` has carried
`ReadMShorts` / `ReadMLongs` / `ReadMFloats` behind `#if ENDIAN ==
LITTLE_ENDIAN` since retail, with `ENDIAN` selected by `GAMECUBE` -- Heavy Iron
shipped this engine on Xbox and PS2 as well, so the container reader was always
byte-order aware, and the decomp preserved the mechanism. The port gets it for
free.

Verified rather than assumed: the unmodified reader walks a retail Xbox
`boot.HIP` on Windows and returns HIPA, PACK, PVER, DICT and ATOC correctly.
There is a check for it in the selftest, because this looks exactly like
something a port would have to add -- and adding it double-swaps, which is what
happened before that check existed to say so.

**Version and content drift.** The decomp targets `GQPE78` (GameCube NTSC-U).
The Xbox release is a different SKU and may carry different revisions, fixes or
content. Do not assume asset IDs and contents correspond 1:1.

**The alternative we are not taking, and why to remember it.** Offline-convert
the *GameCube* assets to little-endian. That guarantees layouts match the
GC-derived code exactly, trading the librw format problem back in. If phase 4
hits layout mismatches that are worse than expected, this is the fallback, and
a hybrid is legitimate: GC-converted logical assets, Xbox renderable assets.

## librw has no world sectors, and BFBB's levels are world sectors

Found 2026-08-26 while writing `RpWorldStreamRead`, and it is the largest thing
standing between this port and a playable one, so it goes here rather than in a
TODO.

**librw does not implement world sectors at all.** No `WorldSector`, no
`PlaneSector`, no `rootSector`, no world chunk reader. Its `World::render` walks
the clump list and carries the comment:

    // this is very wrong, we really want world sectors

`rw::World` is an `Object` and three `LinkList`s -- 32 bytes against
RenderWare's fifteen members. It is a container for clumps and lights, not a
level.

BFBB's level geometry is a BSP of world sectors, so `RpWorldStreamRead` has
nothing to read into and `RpCollisionWorldForAllIntersections` has no tree to
descend. Both return NULL in the shim rather than reporting a success that
hands back an empty level -- `BSP_Read` then prints that it failed, which is
the truthful outcome. **The consequence at runtime is no level geometry: the
player falls through the floor.** Model-to-model collision is unaffected, since
that goes through `RpAtomicForAllIntersections`.

### The Xbox assets do not contain a world at all

Checked against the dump rather than assumed, and it changes the shape of the
problem. Scanning all 121 packs:

    BSP assets:   0
    JSP assets: 110

Level geometry in the Xbox build is `JSP`, and a JSP's payload is RW chunk type
**0x10 (`rwID_CLUMP`)**, not 0x0B (`rwID_WORLD`). `xJSP_MultiStreamRead` loads
it with `RwStreamFindChunk(stream, rwID_CLUMP)` + `RpClumpStreamRead` -- a path
the shim already implements and tests.

So `RpWorldStreamRead` is registered for asset type `'BSP '` and never fed
anything. **Implementing world sectors would not help against Xbox assets: there
would be no BSP to read.**

That is not the good news it first looks like, because the world is not only
scenery. `iEnv` carries four of them --

    struct iEnv { RpWorld *world, *collision, *fx, *camera; xJSPHeader *jsp; ... }

-- and `BSP_Read` is the only thing in this codebase that produces any. The
collision world is what `xScene`'s nearest-floor query walks
(`RpCollisionWorldForAllIntersections`), so without it the player falls through
the floor for a second, unrelated reason: not "librw cannot render the level"
but "nothing built the collision world".

This is the risk this document already named under "The code is GameCube-derived;
the assets would be Xbox-derived", now realised in a specific place. The Xbox
build clearly loaded its environment some other way, and that code is not what
we decompiled -- ours is the GameCube build's, and it expects BSPs.

**Correction, and it reverses what this section first concluded.** The paragraph
that used to sit here recommended switching to GameCube assets. That was wrong,
and wrong in a specific way worth recording: it found a gap -- `RpWorldStreamRead`
needs sectors -- and assumed it was on the critical path without checking whether
the Xbox asset path reaches it.

It does not. `src/SB/Core/gc/iEnv.cpp:61` shows what a JSP level actually does:

    env->world = RpWorldCreate(&tmpbbox);        // an EMPTY world
    env->jsp   = jsp;
    xClumpColl_InstancePointers(env->jsp->colltree, env->jsp->clump);

The world is a container, not the level, and collision comes from the JSP's own
`colltree`. `xCollide.cpp` branches on it: with a JSP it calls
`xClumpColl_ForAllCapsuleLeafNodeIntersections(env->geom->jsp->colltree, ...)`
and only *else* touches `RpCollisionWorldForAllIntersections`. So on Xbox assets
the engine never reads a BSP, never walks a sector, and the two functions the
shim cannot implement are not on the path.

Corroborated externally: github.com/seilc/bfbbpc runs this same GameCube-derived
code against Xbox assets. It uses the real RenderWare 3.4 SDK for D3D8 rather
than librw -- and the reason that pairing works is worth knowing, because it is
not "the SDK has world sectors". **Xbox is a D3D8 platform**, so Xbox RenderWare
assets are D3D8-native and RW 3.4 for D3D8 reads them with no conversion.

Which leaves the live question as a format one rather than a feature one: whether
librw reads Xbox-native textures and geometry as faithfully as the SDK does.
That is measurable against the assets themselves, and is being measured.

A second problem sits behind that one either way: GameCube sector geometry is a
display list in a platform extension chunk, which is why `iFX.cpp` reads
`_rpDlWorldVtxFmtOffset` off the current world. librw has PS2, D3D and GL
pipelines and no GameCube one, so the sector work includes a pipeline that can
consume that geometry.

Three ways out, none of them small, and this is the decision phase 4 actually
turns on:

1. **Implement world sectors in librw.** The honest fix and useful upstream.
   Sector tree, the BSP chunk reader, and a pipeline for the sector geometry.
2. **Convert levels offline into clumps.** Flatten each BSP into atomics the
   existing path already loads. Loses the sector culling the engine expects, so
   whether it performs acceptably is an open question, and collision would need
   its own answer.
3. **Do not use librw for world rendering.** Keep it for models and textures and
   write the level renderer directly.

Nothing above this line is blocked on it -- models, textures, animation and
collision between models all work through the shim as it stands.

## Latent retail bugs, and the NON_MATCHING escape hatch

A category that is **not** a decomp error and still has to be fixed for a port:
retail code that reads uninitialised stack, where retail's own frame contents
happen to make the read harmless. We reproduce the code exactly, inherit the
read, and do not inherit the luck.

**Confirmed instance.** The Flying Dutchman boss level crashed with
`Invalid read from 0xbed60419, PC = 0x8004c540`, which is `lbz r8,0(r9)` in
`xStrTokBuffer`. `ZNPC_AnimTable_Dutchman` declares a 13-entry `ourAnims` with
no terminator; `NPCC_BuildStandardAnimTran` scans `while (ourAnims[i] != 0)`.
The scan always reads one word past the array, uses it to index
`g_strz_subbanim[23]`, and hands the resulting pointer to the tokenizer.

All three functions are faithful: the target's initialiser blob is 0x34 (13
words, no zero), the target's loop is the same `lwzx / cmpwi 0 / bne+`, and
`ZNPC_AnimTable_Dutchman` and `ZNPC_AnimTable_Prawn` are both **100% matching**.
The array sits at 24(r1)..72(r1) and `stmw r20,80(r1)` starts the saved
registers, so 76(r1) is a hole in retail's frame too. Retail simply had 0 there.

**Why matching harder does not fix it.** The value in that hole is whatever the
preceding execution left at that address, so it is a property of the whole
build, not of any one function. 39 of 44 `ZNPC_AnimTable_*` builders already
match, including the one that runs immediately before Dutchman, and it still
crashed. Only a 100% tree would inherit retail's stack history -- and a PC port
never reproduces PowerPC frame layout at all, so the port needs the real fix
regardless of what the percentage says.

**The escape hatch.** `configure.py` appends `-DNON_MATCHING` to `cflags_bfbb`
when configured with `--non-matching`. Source guarded by it is absent from the
matching build -- which is required, since adding a terminator changes codegen --
and present in every build that has to actually run, including the port.

`NPCC_ANIM_LIST_END` in `zNPCTypeCommon.h` is the first user: it expands to
`, 0` under `NON_MATCHING` and to nothing otherwise. Applied to the Dutchman
and Prawn lists, whose blobs go 0x34 -> 0x38 and 0x28 -> 0x2c in the playtest
build while both objects stay byte-identical in the matching build.

Verify any future use the same way: the matching `.o` must be byte-identical
before and after, and `main.dol` must stay 306526d90b48e99894c3138f5fc8f2716d9fecf6.

Use this sparingly. It is for defects that are provably retail's, where the
matching build must not change. It is not a way to avoid fixing our own bugs.

## Other things that will bite

**Strict aliasing.** The source is full of `*(U32*)&someFloat` casts that exist
precisely because they made CodeWarrior emit the right instructions. Modern
GCC/Clang at `-O2` will miscompile some of it. `-fno-strict-aliasing` is
mandatory, not optional.

**Floating point divergence.** GameCube PPC has paired singles, fused
multiply-add, and `fres`/`frsqrte` estimate instructions with defined but
non-IEEE precision. Physics and gameplay can drift subtly. Usually tolerable;
occasionally the cause of a bug that looks like a logic error and is not.

**CodeWarrior-isms in the source.** `__declspec(weak)`, placement-new over asset
buffers, and the small-data-area (`sdata`/`sdata2`) assumptions are all things
the port's toolchain has to tolerate or have removed.

## Distribution

Code only. The port requires the user to supply their own copy and extracts
assets at first run — the Ship of Harkinian model. RenderWare and Bink are both
proprietary: librw replaces the first, and the FMVs must be re-encoded to a free
format (or the port ships without them) because Bink cannot be redistributed.

## First concrete step, when we get there

Before committing to phase 4, spike it: take one HIP/HOP archive from the Xbox
release, parse a handful of *logical* asset types with our GC-derived struct
definitions, and check the fields land where we expect. That single experiment
resolves the largest open question in this plan — whether GC-derived structs can
read Xbox-serialized data — and it can be done with a standalone tool long
before any of the porting work starts.


## Phase 2 progress

### The seam held up

The claim above — that `src/SB/Core/x` is platform-agnostic and `gc` is behind
25 `i*` headers — was worth checking rather than trusting, and it checks out.
Of the 198 units in `src/SB`, **three** include a dolphin header, and one of
those (`xpkrsvc.h`) does not use what it includes. The 49 units that include
RenderWare headers are not a leak: librw reimplements that same API, and
`rwcore.h` as it stands already parses under GCC.

So the port did not need `#ifdef` scattered through game code. It needed a
second directory and an include path.

### How the two builds are kept apart

Game code includes its platform headers unqualified, so the include path alone
decides which layer it gets: `configure.py` puts `src/SB/Core/gc` on
CodeWarrior's path, `CMakeLists.txt` puts `src/SB/Core/pc` on the host's, and
neither is ever on the other's. Shared files that genuinely must differ branch
on `GAMECUBE`, which only the CodeWarrior build defines.

The gate that actually proves it is the DOL's SHA-1. Every edit to shared
source was followed by a GameCube rebuild confirming
`306526d90b48e99894c3138f5fc8f2716d9fecf6`, and it has not moved.

### What can be measured

There is no `report.json` for a port — nothing to score bytes against. The
substitute is `tools/pcprogress.py`: does each unit of `src/SB` compile, on a
modern toolchain, against the PC platform headers? That is a real gate, not a
proxy, and anything failing it names its reason.

It reports three numbers, and the split is the interesting part:

    compiles            169 / 198 units   85.35%
    pointer width only   26 / 198 units   13.13%
    needs other work      3 / 198 units

**The percentage was wrong for two rounds and is corrected here.** The tool
passed `-fpermissive`, which downgrades real errors to warnings, so it counted
34 units that no build would accept -- it read 97.5% where the honest figure
was 80.3%. `CMakeLists.txt` never passed that flag, so the two disagreed and
only the looser one was being reported. The flag is gone.

What it hid is worth having found: the largest remaining class is **casting a
pointer to `U32`**. That is not a defect to fix unit by unit, it is the open
question under **Asset caveats** arriving as a compile error, so it gets its
own line. 26 units fail for that reason and no other -- 195 of 198 are either
compiling or waiting on a decision that has not been made.

`-m32` was tested on 2026-08-26 with clang 16, and it clears the whole class:
162/198 units compile 64-bit against these headers, 195/198 under `-m32`, with
the pointer-width count at zero — 33 units fixed, none broken. So the answer to
this section is **build 32-bit**; the alternative, separating on-disk formats
from in-memory structs, is not needed and should not be started.

None of the remainder is waiting on librw, because compiling is not linking.

`src/SB/Core/pc/tests/selftest.cpp` is the other half: 37 checks over every
implemented interface, so that "implemented" is measured rather than asserted.

### CodeWarrior-isms, now counted

This section predicted these. The actual inventory, all fixed:

- **44 MSL-path includes** across 38 files, spelled
  `<PowerPC_EABI_Support\MSL_C\MSL_Common\cmath>`. Backslashes are not path
  separators on a host. Gated on `__MWERKS__`, with the standard header on the
  other branch.
- **Two case-sensitivity bugs.** `xUtil.h` (7 files) and `xScrFX.h` (1) do not
  exist; the files are `xutil.h` and `xScrFx.h`. The GameCube build only works
  because CodeWarrior runs under wibo, which resolves include paths
  case-insensitively the way Windows does. Nothing in the matching build could
  ever have reported this.
- **Tentative definitions with no bound.** `xTRC.h` declares
  `_tagTRCPadInfo gTrcPad[];`, which `-common on` accepts and standard C++
  rejects. Gated; the real sizes were already in `xTRC.cpp`.
- **MSL extensions** — `FABS`, `stricmp`, `std::floorf` — supplied by
  `src/SB/Core/pc/compat`, which shadows `<math.h>`, `<string.h>` and `<cmath>`
  and chains on with `include_next`. Note that
  `src/PowerPC_EABI_Support/include/math.h` defines `__fabs` as *identity* for
  non-CodeWarrior compilers, to quiet clangd; a port that picked that up would
  have `FABS(-3) == -3`. The compat header defines it properly and the selftest
  checks it.

### The 32-bit question, answered for the allocator at least

The open question above — build 32-bit, or separate on-disk formats from
in-memory structs — did not have to be settled to get the memory manager
working, but it did have to be confronted. `xMemInitHeap` does its arithmetic
on `gMemInfo.DRAM.addr`, which is a `U32`, so every address the game allocator
hands out must fit in 32 bits and survive the round trip back to a pointer.

`iMemInit` therefore reserves its arena with `mmap(MAP_32BIT)` and refuses to
start if the result is above 4 GB, rather than truncating and corrupting later.
That is not a decision about asset layouts — it only buys the allocator. The
choice in **Asset caveats** is still open.

While doing it: `xMemInit` places `gxHeap[1]` and `gxHeap[2]` at
`DRAM.addr + DRAM.size`, *past* the block retail allocated for DRAM. On the
console those addresses land in unclaimed OS arena and nothing notices. On a
host that is a wild write into whatever the allocator put next, so the arena is
reserved at twice the size. One more for the latent-retail-bugs list.

### What is next

1. ~~**`isavegame`**~~ — done. 29 functions, a directory per target standing in
   for a memory card. Most of the GameCube's 2048 lines were CARD itself:
   mounting, sector probing, formatting, repairing, and the 8 KB banner-and-icon
   blob every card file carries ahead of its payload. None of that survives.
   What survives is the shape, because `xsavegame.cpp` is shared and asks all
   the same questions.
2. **`iSnd`** — 22 functions, and the largest group after rendering.
3. **The 26 pointer-width units**, which are one decision rather than 26 fixes.
4. **The 3 that need other work**, all structural rather than mechanical:
   - `xFX.cpp` and `xFont.cpp` define member specializations
     (`tier_queue<joint_data>`, `basic_rect<F32>`) below the point where their
     own headers instantiate them. The declaration has to be visible earlier,
     and the obvious home -- the header -- is shared with 71 and 100+ other
     units respectively, where the declaration would then land *after*
     instantiation. Putting them in `xFX.h` was tried and took the count from
     191 units to 120. Needs somewhere those includers do not see.
   - `xModelBucket.cpp` includes `rwsdk/driver/gcn/dlrendst.h`, a GameCube
     RenderWare driver header. Phase 3.
4. **librw**, which is phase 3 and gates the other 14 interfaces.

### `jump to case label`, and what measuring it cost

13 units declared a variable in a `case` body that later labels skip past.
CodeWarrior accepts it; standard C++ does not.

Bracing the case body is the clean fix, and the question was whether it moves
CodeWarrior's stack frame. `cmp` on the object said it did, in 4 of 12 units.
That was wrong: adding a line shifts every DWARF line-number entry, so the
objects differ while the code does not, and DWARF is not linked into the DOL.
`tools/objsame.py` compares only the sections that reach the DOL, and by that
measure all 13 are identical -- one uniform fix, no stack frames moved.

The same mistake in a different costume as the `ninja | tail` one: **a baseline
has to be built, not found.** Copying whatever object happens to be on disk and
calling it "before" gave a false difference twice before the stash-and-rebuild
comparison settled it.

One site could not be braced at all. `zEntPlayer.cpp` has a `goto do_bounce`
that jumps forward over `xSurface* surf = ...`, and no block can contain both
the jump and the label; that one splits the declaration from its initializer
instead, which a jump is allowed to cross.

### The 32-bit question, as a measurement

`xVec3Dot` was the last piece of PowerPC assembly in `src/SB`: Gekko
paired-single, two floats per register. The scalar body preserves what the
assembly actually computes -- `ps_sum0` gives `(x*x2 + y*y2) + z*z2`, and float
addition is not associative, so the order is the part worth keeping. The console
also gets the first two terms from one `ps_madd`, one rounding where the scalar
version has two.

### CodeWarrior-isms, second pass

- **`namespace std` re-declarations.** Eight files declare or define MSL's
  f-suffixed math functions -- `std::floorf`, `std::powf`, `std::atan2f` --
  because MSL puts them there and leaves the bodies to whoever needs them. A
  host has them globally, with an exception specification that makes any
  redeclaration a conflict. Gated on `__MWERKS__`; `compat/cmath` brings the
  global names into `std` instead. Two of them were already guarded with
  `#ifndef INLINE`, which is defined in neither build and so guarded nothing.
- **`<mem.h>`**, MSL's split-out half of `<string.h>`, and **`strcmpi`**, the
  other spelling of `stricmp`. Both in `compat/`.
- **`<dolphin.h>` in `xAnim.cpp`**, which the file does not use -- no OS, DVD
  or CARD call anywhere in it. Gated, like `xpkrsvc.h` before it. That leaves
  `zMain.cpp` as the only unit in `src/SB` that genuinely reaches for dolphin.


### Third pass

- **Explicit specializations declared after they were instantiated.**
  `range_limit` (4 specializations, declared in `xMath.h` now),
  `auto_tweak::load_param` (4 headers), `xListItem<NPCConfig>` members.
  CodeWarrior resolves these regardless of order; standard C++ requires the
  declaration first. Note that `range_limit`'s primary template has no
  definition anywhere -- every use always resolved to a specialization, so
  declaring them changes nothing about what runs.
- **Redundant qualification.** `void zhud::init()` written *inside*
  `namespace zhud`, and `X::X(...)` written inside `struct X`. 34 sites.
- **`u32` and `size_t` are the same type on the GameCube ABI** -- both
  `unsigned long` -- and are not on LP64. `ztalkbox::load` is declared taking
  one and defined taking the other, which only became a mismatch here.
- **Placement `operator new`,** defined in `xHud.cpp` because MSL's `<new>`
  does not declare it. libstdc++ does.
- **`__fabs` and `__fabsf`.** glibc *declares* both -- they are its internal
  aliases for `fabs` -- and does not define them, so the host layer supplies
  the bodies in `src/SB/Core/pc/intrin.cpp`. Defining them in a header instead
  is a static redeclaration of an extern, which is how this was found.

### No game code reaches for the console any more

`src/SB` is now free of `dolphin`. The last three were:

- **`zMainMemCardSpaceQuery`**, the memory-card startup screen -- 250 lines of
  `CARDMount`, `CARDFormatAsync`, progress bars and `OSResetSystem`. Every state
  it exists to resolve is a property of a card: not inserted, not formatted,
  formatted for another region, damaged, or holding a file whose checksum does
  not hold up. A save directory has none of them, and a host has nowhere to
  reset to. The host body keeps the one question still worth asking -- is there
  room -- so a full disk is reported at startup rather than found at the first
  autosave.
- **`CARDProbeEx` in zSaveLoad.cpp**, one call deciding whether to show the
  saving icon. `CARD_RESULT_READY` and `CARD_RESULT_BUSY` both mean go ahead.
- **`void main`.** CodeWarrior accepts it; standard C++ does not. The port's
  entry point returns `int` now.

### Third pass, continued

- **`extern` contradicted by `static`.** `sCameraFXMatOld` declared extern in
  `xCamera.h` and defined static in the .cpp with no other reader;
  `screen_bounds` declared extern in `zHud.h` while four .cpp files each define
  a private `static const` copy and nothing links to an external one; `_930`
  and `_932_0` in `zUIFont.cpp`, declared extern at the top and defined static
  at the bottom with an explicit `.sdata2` placement -- a device for getting
  two constants into the right section in the right order, which a host needs
  neither of.
- **`&(T)expr`.** CodeWarrior treats a C-style cast of an lvalue as an lvalue,
  so this is the address of the object; standard C++ makes a temporary and
  points at that. A reference cast, `&(T&)expr`, says what was meant.
- **Two-phase lookup.** `sound_queue::pop` and its neighbours call `xSndStop`
  and `xSndIsPlayingByHandle`, both declared 100 lines below the template that
  calls them. A call with no dependent arguments binds where the template is
  defined, not where it is instantiated.
- **`TL_CACHE_COUNT`** had a `GAMECUBE` branch and a `PS2` branch and no other.
  It is how many laid-out textboxes are kept -- a miss re-runs the layout, it
  does not change what is drawn -- so the host takes the GameCube's 1 rather
  than the PS2's 3, on the grounds that this code is GameCube-derived.