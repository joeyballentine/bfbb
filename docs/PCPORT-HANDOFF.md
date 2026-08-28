# PC port — hand-off

For whoever picks this up next. `docs/PCPORT.md` is the design record and the
argument; this is the operating manual. Read this first, then
`src/SB/Core/pc/README.md` for the layer itself.

Branch: **`treedome`**, off `duplotron`.

---

## 0. Setup, and what this branch sits on

`treedome` branches off `duplotron` and **inherits the CodeWarrior
patch on purpose**. That costs you nothing if you only want to build the port:
the PC build is clang/cmake and never invokes CodeWarrior. The patch matters
only for running the GameCube regression gate in section 1, and inheriting it
keeps that gate on the best baseline available (7249 matched functions rather
than roughly 7180 against a stock compiler).

The division of labour matters when you add something:

- **`duplotron`** is the shared base -- decomp work, the compiler
  patch, and `tools/gcgate.py`. Anything both branches need goes there, and
  `treedome` is rebased on top, so the port never carries a duplicate copy.
- **`treedome`** is the port and nothing else: `src/SB/Core/pc/`,
  `CMakeLists.txt`, `tools/pcprogress.py`, and the docs.

```sh
git checkout treedome
python tools/patch_compiler.py build/compilers
python configure.py
ninja
python tools/gcgate.py           # must pass before you trust any measurement
```

The PC side is separate and needs neither of the above:

```sh
cmake -S . -B build-pc -G Ninja && cmake --build build-pc && ./build-pc/pc_selftest
python tools/pcprogress.py --m32
```

Both build on Linux and on Windows. The OS half of the layer is behind
`src/SB/Core/pc/iHost.h`, with one backend per platform; `pcprogress.py --host`
checks that the two stay in step.

---

## 1. The one rule

The GameCube build must stay byte-identical **and must not lose matched
functions**. Both, every time:

```sh
ninja > /tmp/gc.log 2>&1; echo "exit: $?"
python3 tools/gcgate.py
```

```
  PASS  DOL       306526d90b48e99894c3138f5fc8f2716d9fecf6
  PASS  functions 7249 / 80.66999%
```

Nothing else counts as proof. Not "this edit is inside `#else`", not "a
declaration emits no code", not reasoning about scope. Build it and check it.

### Why the hash alone is not enough

An earlier version of this document said the DOL hash was the only thing that
counted. That is false, and it cost 16 functions before anyone noticed.

Units marked `NonMatching` in `configure.py` link from the **extracted** object,
not from ours. Their source can regress all the way to 0% and the DOL still
comes out byte-identical, because our object never reached the link. That is
exactly where all in-progress decomp work lives, so the hash is blind to the
work most likely to break.

What it missed here: the CodeWarrior-tolerance pass rewrote

```cpp
void cruise_bubble::init()      ->      void init()
```

inside `namespace cruise_bubble { namespace { ... } }`. It reads as removing a
redundant qualifier. It is not. The qualified form defines the function in the
**outer** namespace with external linkage; the bare form defines a *different*
function with internal linkage in the anonymous one. Sixteen zEntCruiseBubble
functions went 100% -> 0%, eight other translation units lost the symbols they
call — the `--non-matching` playtest build would no longer link — and the DOL
hash stayed green throughout.

The fix, which keeps both compilers happy and is byte-identical to retail: close
the anonymous namespace, define the function unqualified at `cruise_bubble`
scope, reopen it. `tools/gcgate.py --which <known-good report.json>` names
anything that slips.

**Any time you touch a namespace, a qualifier, or a `static`, you are editing
linkage, not formatting.** Check the symbol table:

```sh
build/binutils/powerpc-eabi-objdump.exe -t build/GQPE78/src/SB/Game/zFoo.o | grep ' g  *F .text'
```

### Run the build so you can see it fail

```sh
ninja > /tmp/gc.log 2>&1; echo "exit: $?"
sha1sum build/GQPE78/main.dol
```

**Never `ninja | tail`.** A pipeline reports the *last* command's exit status,
so a failing build followed by `&& sha1sum` prints the hash of the DOL left
over from the last successful run. It looks exactly like a pass. This produced
two consecutive false "verified" reports before it was caught.

### Check one object cheaply first

A full build is minutes; one object is seconds. Build a baseline **from clean
source** and compare:

```sh
git stash push -q <files>
ninja build/GQPE78/src/SB/Game/zFoo.o && cp build/GQPE78/src/SB/Game/zFoo.o /tmp/base.o
git stash pop -q
ninja build/GQPE78/src/SB/Game/zFoo.o
python3 tools/objsame.py /tmp/base.o build/GQPE78/src/SB/Game/zFoo.o
```

Two things about this, both learned the hard way:

- **Use `tools/objsame.py`, not `cmp`.** Adding or moving a source line shifts
  every DWARF line-number entry, so the objects differ while the code is
  identical — and DWARF is not linked into the DOL. `cmp` reported 4 false
  regressions on a change that was provably neutral. `objsame.py` compares only
  the sections that reach the DOL.
- **A baseline has to be *built*, not *found*.** Copying whatever `.o` happens
  to be sitting in `build/` and calling it "before" gave a false difference
  twice. Stash, build, unstash, build, compare.

An object-level pass is necessary but not sufficient — still run the full
build and hash the DOL before committing.

---

## 2. How the two builds are kept apart

Game code includes its platform headers unqualified (`#include "iTime.h"`), so
the **include path alone** decides which layer it gets:

| | GameCube | PC |
|---|---|---|
| driver | `configure.py` → `build.ninja` → decomp-toolkit | `CMakeLists.txt` (root) |
| compiler | CodeWarrior `GC/2.0p1a` (patched) | host g++ / clang |
| platform layer | `-i src/SB/Core/gc` | `-I src/SB/Core/pc` |
| define | `-DGAMECUBE` | — |

Neither directory is ever on the other build's path. The PC build never invokes
`ninja`, `configure.py` or CodeWarrior, and `configure.py` lists every object
explicitly so a `pc/` file cannot enter it.

### Which macro to gate on

This distinction matters and getting it wrong breaks 500 objects:

- **`__MWERKS__`** — "can this compiler parse it". Use for MSL headers,
  `namespace std` re-declarations, PPC intrinsics, placement `new`, anything
  CodeWarrior-specific. **`src/dolphin` and `src/bink` are compiled by
  CodeWarrior *without* `-DGAMECUBE`** (only `cflags_bfbb` defines it), so
  gating a compiler question on `GAMECUBE` drops them into the host branch.
  That is exactly how `include/types.h` was broken once.
- **`GAMECUBE`** — "is this the console". Use for dolphin includes, hardware
  behaviour, `.sdata2` placement devices, the memory-card screen.

`src/SB/Core/gc/iFile.h` and `src/SB/Core/x/xFont.cpp` show the house idiom: `#ifdef GAMECUBE / #else
#ifdef PS2 / #else`. Follow it.

---

## 3. The metric

There is no `report.json` for a port — nothing to score bytes against.
`tools/pcprogress.py` asks the question that *can* be answered: does each unit
of `src/SB` compile, on a modern toolchain, against the PC platform headers?

```
$ python tools/pcprogress.py --m32
clang++ -m32
compiles            195 / 198 units   98.48%
pointer width only    0 / 198 units    0.00%
needs other work      3 / 198 units
```

**Always pass `--m32`.** 32-bit is the project's answer to the pointer-width
question, not an experiment — see §5(a). Without it the same tree reads
162/198, and the 33-unit gap is entirely pointers truncating on LP64.

- `--list` per-unit verdict, `--errors` first error per failing unit,
  `--drift` checks the 16 headers copied verbatim from `gc/` for divergence.
- `--cc` picks the host compiler; the default is `g++`, else `clang++`.
- Positional args filter: `pcprogress.py xEnt zPlayer`.

The remaining 3 are the structural units in §5(c). There is nothing else left
in the "would compile if only" category.

### Two ways this metric has already lied

**`-fpermissive`.** It was on once and inflated the number by 34 units — read
97.5% where the truth was 80.3% — and it disagreed with `CMakeLists.txt`, which
never passed it. Do not add it back. If you widen the flags, make the two agree.

**`-w`, on clang.** Subtler and worse. g++ makes `(U32)somePointer` a hard error
in C++ mode, so `-w` is harmless there. clang makes the same cast a *warning*,
so under `-w` the entire pointer-width class vanishes and every unit whose only
problem is pointer width silently passes — the tool reads ~195/198 in **64-bit**
and means nothing by it. clang's `-w` cannot be overridden by a later
`-Werror=`; `-Wno-everything` can, so that is the idiom, with the pointer casts
promoted back to errors. The rule underneath both: if a flag changes the number,
check that it changed the *port* and not the *diagnostic policy*.

`src/SB/Core/pc/tests/selftest.cpp` is the other half: **113 checks** over every
implemented interface that does not need a renderer, so "implemented" is a
measurement rather than a claim. (Two further `check()` call sites are the
could-not-make-a-temp-directory arms, which do not run when it works.)

It also checks `sizeof(xSndVoiceInfo) == 100`, which is only true in a 32-bit
build -- `iSndPlay` divides a byte offset by it. That check is what caught
`CMakeLists.txt` still building 64-bit after 5(a) settled the question; the two
now agree, and `BFBB_BUILD_32BIT` is on by default.

`--host` is the third: both `iHost` backends must implement everything
`iHost.h` declares. Nothing else notices when they diverge, because the layer
is only ever built for one host at a time -- adding a function to the POSIX
backend and forgetting the Win32 one builds clean on Linux and fails to link on
Windows.

```sh
cmake -S . -B build-pc -G Ninja && cmake --build build-pc && ./build-pc/pc_selftest
```

---

## 4. What exists

`src/SB/Core/pc/` — 18 implementation files, 117 of the 178 interface functions
game code actually calls, on Linux and on Windows.

**Done:** `iTime` `iMath` `iMemMgr` `iFile` `iPad` `iSystem` `iTRC` `iColor`
`isavegame` `iSnd`, plus `intrin.cpp` (`__fabs`/`__fabsf` — glibc declares both
and defines neither).

**Copied unchanged from `gc/`, because they turned out to be portable as they
stand:** `iMath3` (13 fns, pure geometry), `iCollide` (10), `iCollideFast` (1).
Recorded in `VERBATIM.txt`, so `--drift` reports it if the gc original moves.
`iCollide` needs three RenderWare collision calls and so cannot link until
phase 3; it is in the library, not the selftest. `iAnimSKB` is *ported* rather
than copied and so is not in `VERBATIM.txt`: it defines `std::fabsf` with
`__declspec(weak)` to put retail's one copy of that symbol where the DOL has
it, which collides with `compat/_stdfloatmath.h` on a host. The definition is
guarded for `__MWERKS__` in the port's copy only. It wants
`RtQuatSetupSlerpCache`, so it links when librw does. None of the three has selftest
coverage, and the reason is argued in `CMakeLists.txt` at the target: they are
copies of shipping code, so what is unproven is only that they build on a host.

**Header only, no body:** `iModel` (19), `iMath3` (10), `iCollide` (10), `iScrFX` (10), `iEnv` (7),
`iLight` (7), `iAnim` (5), `iCutscene` (4), `iParMgr` (4), `iDraw` (3),
`iMorph` (2), `iFX` (1), `iFMV` (1). All of these need librw.

**Not interfaces:**

- `iHost.h` / `iHostPosix.cpp` / `iHostWin32.cpp` — the OS seam: 21 functions
  covering the monotonic clock, frame pacing, local time, the low-memory arena,
  and the filesystem. Everything above it is host-independent, which is what
  makes the five files that used to call POSIX directly readable as game
  semantics rather than as one OS's spelling. `pcprogress.py --host` checks the
  two backends stay in step. See 5(f) for the three things Windows does
  differently that this exists to absorb.
- `compat/` shadows `<math.h>` `<string.h>` `<cmath>` `<mem.h>` `<intrin.h>`
  to supply the CodeWarrior extensions the sources use, chaining with
  `include_next`. Must come first on the include path. Note that
  `src/PowerPC_EABI_Support/include/math.h` defines `__fabs` as an **identity**
  macro for non-CodeWarrior compilers to quiet clangd — a port picking that up
  gets `FABS(-3) == -3`. The selftest checks this specifically.
- `iPadHost.h` / `iPadHostNull.cpp` — the device end of input, behind a seam of
  its own, older than `iHost` and the model for it.
- `iSndHost.h` / `iSndHostNull.cpp` — the device end of audio, same shape.
  `null` is silent and still keeps time; see 5(b) for why that matters. Select
  with `-DBFBB_AUDIO_BACKEND=`, as with `BFBB_INPUT_BACKEND`.
  `null` is a real configuration (no controllers), not a placeholder. Nothing
  is wired to a real device yet; SDL2 is the obvious first backend.
- `VERBATIM.txt` — the 16 headers copied unchanged from `gc/`, with the hash
  each was copied at. `pcprogress.py --drift` reports divergence.

---

## 5. Open work, ranked

### (a) The 32-bit decision — ANSWERED: build 32-bit

The blocker was `(U32)somePointer`. `xMemInitHeap` does arithmetic on
`gMemInfo.DRAM.addr`, a `U32`; asset-overlaid structs address memory in 32 bits
throughout, so on LP64 every such cast truncates. This was listed as one open
decision, never tested, and the single highest-value next step.

**Tested 2026-08-26 on Windows with clang 16. It clears the whole class.**

Controlled: same compiler, same flags, same compat headers, one variable.

| | compiles | failing | pointer-width |
|---|---|---|---|
| 64-bit | 162 / 198 (81.82%) | 36 | 8 |
| **`--m32`** | **195 / 198 (98.48%)** | **3** | **0** |

33 units fixed, **0 broken**, and the 3 survivors are exactly the structural
units already named in (c) below — `xFX`, `xFont`, `xModelBucket`. Nothing else
is left in this class.

```sh
python3 tools/pcprogress.py --m32
```

So `iMemInit` reserving its arena with `mmap(MAP_32BIT)` is not a hedge any
more, it is the design. Treat 32-bit as a project-wide constraint rather than
an open question, and keep `CMakeLists.txt` in step with it.

Two cautions about the measurement, both of which cost a wrong number first:

- **`pcprogress.py` hardcoded `g++`** and its pointer-width regex only matched
  g++'s phrasing. It now detects the compiler and accepts both. More
  importantly, **g++ makes `(U32)ptr` a hard error while clang makes it a
  warning**, so under the old `-w` the entire pointer-width class vanished and
  the tool read ~195/198 in *64-bit* — measuring the diagnostic policy, not the
  port. clang's `-w` cannot be overridden by a later `-Werror=`;
  `-Wno-everything` can, so that is the idiom now.
- The tool compiles game code against the pc/ *headers*. It does not build the
  pc/ *implementation*, which is a separate question — see (f).

### (b) `iSnd` — DONE, with a silent backend

All 29 entry points are implemented in `src/SB/Core/pc/iSnd.cpp`, with the
device end behind `iSndHost.h` and `iSndHostNull.cpp` under it. The GameCube
original is 2051 lines, nearly all of it AX, MIX, ARAM and DVD; none of that
has a host counterpart, so what is here is the part with the game's semantics
in it -- the voice table, the handle scheme, the six-stream/58-sound division,
the lookup and its id ranges.

**The null backend is silent but keeps time**, and that is the design decision
worth knowing. The obvious null audio backend reports nothing ever playing.
That is wrong here in a way that gets blamed on the game rather than the audio:
zTalkBox holds a line on screen until its clip finishes, cutscenes gate on
`iSndIsPlayingByHandle`, and NPCs stagger barks by asking whether the last one
is done. Finish every sound instantly and dialogue flashes past and cutscenes
desynchronise -- which reads as the port getting the game code wrong. So the
backend records each sample's length and reports the voice as playing for
exactly that long, scaled by pitch. No sound, every sound-derived timing still
correct.

29 selftest checks cover it, including that a paused voice does not finish on
its own and that `iSndUpdate` clears both the platform slot and the game's.

**What a real backend still has to add:** `iSndLoadSounds` registers the table
so lookup and timing work, but does not open the HIP holding the sample data or
convert loop points to nibble addresses. That is said at the site. Reverb
(`iSndSetEnvironmentalEffect`) is deliberately not on the seam -- a backend with
reverb should be handed the game's parameters, not a GameCube DSP preset index.

### (c) The 3 structural units — a design question, not a grind

- **`xFX.cpp`** and **`xFont.cpp`** define member specializations
  (`tier_queue<xFXRibbon::joint_data>`, `basic_rect<F32>`) *below* the point
  where their own headers instantiate them. The declaration must be visible
  earlier. The obvious home — the header — is shared with 71 and 100+ units,
  where the declaration lands *after* instantiation instead. **This was tried:
  putting them in `xFX.h` took the count from 191 units to 120.** It needs
  somewhere those includers do not see. Unsolved.
- **`xModelBucket.cpp`** includes `rwsdk/driver/gcn/dlrendst.h`, a GameCube
  RenderWare driver header. Phase 3 (librw).

### (d) `isavegame` is written but never exercised by the game

The 29 functions pass 40-odd selftest checks against a temp directory. Nothing
has run the real save path end to end, because that needs a linking game.
Treat it as unproven under real use.

### (e) Phase 3 — librw

`rwcore.h` already parses under clang, so game code compiles against
RenderWare's declarations today; only linking needs librw. re3/reVC is the
precedent.

Measured 2026-08-26, so the size of the prize is known rather than guessed.

**The link gap.** 208 of 212 units compiled to real 32-bit objects and linked
against each other leaves 392 distinct undefined symbols:

| | |
|---|---|
| game code — absent, or in a unit that fails | 146 |
| **RenderWare** | **105** |
| `i*` interfaces not implemented | 91 |
| C/C++ runtime | 50 |

The first and last buckets are inflated: they include Win32 calls that resolve
against kernel32 and CRT symbols that resolve at a real link. So librw is
roughly a quarter of what is left, and necessary rather than sufficient.

**What arrives with it, though, is more than those 105 symbols.** Copying each
remaining `gc/*.cpp` into `pc/` and compiling it against the PC headers says
seven of them are *already portable* and blocked only on linking:

    iAnim.cpp       1 RW symbol
    iMorph.cpp      5
    iParMgr.cpp     6
    iScrFX.cpp      7
    iEnv.cpp       10
    iLight.cpp     10
    iModel.cpp     37

That is 44 more interface functions that need no porting work at all -- copy,
record in `VERBATIM.txt`, link. Do that sweep again before writing any of them
by hand.

Three do NOT compile as they stand, and want real work rather than a copy:
`iDraw.cpp`, `iFMV.cpp` and `iFX.cpp` include `dolphin/gx.h` or `dolphin/os.h`
directly.

`iCutscene.cpp` is blocked on something else entirely, and it is worth knowing
which: it computes where a cutscene starts inside its HIP as
`(ainfo.sector - File.ps.fileInfo.startAddr) << 5` -- a **disc sector** delta,
from `DVDFileInfo`, which the host `tag_iFile` has no equivalent of and should
not grow one. What it needs is the asset's byte offset within the HIP, which is
an asset-pipeline question (docs/PCPORT.md, phase 4), not a librw one. `iSnd`'s
loader wants the same number for the same reason. Answer it once, for both.

**Do not start until the asset question is answered.** The plan is Xbox assets
(see docs/PCPORT.md), and librw's GameCube format support is its weakest -- which is
*why* that decision was made. Linking librw against GameCube-native big-endian
assets buys a renderer that cannot read anything.

**The toolchain question is answered: librw builds.** Spiked 2026-08-26 --
`aap/librw` at `LIBRW_PLATFORM=NULL`, clang 16, `-m32`, MSVC ABI: configures,
compiles and links to a static library with **zero errors**, confirmed i386.
So the ABI worry that motivated the spike is retired.

**But it is not a drop-in, and that is the real finding.** librw is a
reimplementation in namespace `rw::`, not an implementation of the RenderWare C
API. Of its 1325 defined text symbols, 1124 are `rw::` C++ and **zero** are
`Rw*`/`Rp*` C functions:

    RpClumpStreamRead   0        ?create@AnimInterpolator@rw@@SAPAU12@HH@Z
    RwFrameTransform    0        ?conj@rw@@YA?AUQuat@1@ABU21@@Z
    RwEngineInit        0        ...

Our situation differs from re3/reVC, which is why the precedent misleads here:
they *wrote* their code against librw's API. Ours is decompiled and calls the
RenderWare C API, and it has to keep calling it -- those call sites are what the
GameCube build matches against.

So phase 3 is not "link librw". It is **write a shim implementing the
RenderWare C API on top of `rw::`**, and the size of that is now known rather
than guessed: **99 distinct `Rw*`/`Rp*` functions** across the 208 units that
compile today, concentrated in a few areas.

    RwCamera 12   RpGeometry 8   RwIm 7   RwFrame 7   RpWorld 7
    RpSkin 6      RpClump 6      RpLight 5   RwTex 4   RwStream 4

Expect it to grow as the seven librw-gated `i*` units above come in, though
they overlap heavily with this set.

That shim is a real project, but bounded and very parallelisable, and it keeps
the decompiled call sites untouched so the GameCube build and the port stay one
codebase.

**How it works, decided and in place:** the port makes the two libraries share
objects rather than convert between them. In a PC build each RenderWare struct
is declared in `include/rwsdk/` with **librw's field ORDER under RenderWare's
field NAMES**, guarded by `GAMECUBE` so the console keeps retail's own layout.
An `RwFrame*` is then literally an `rw::Frame*`, every shim function is a cast
and a call, and **game code compiles unmodified** -- 197/198 before the reorder
and after.

Converting at the seam was the alternative and does not work: the game holds an
`RwFrame*` for an entity's lifetime and writes `->modelling` directly at dozens
of sites, while librw mutates the same objects through its own parent/child
links. Two copies would need syncing both ways, and direct field access offers
no call boundary to hook.

**The rule: a type mirrored in `include/rwsdk` gets its size and every field
offset asserted in `src/SB/Core/pc/rw/layout*.cpp`, in the same commit.** Take
the offsets from the compiler, not from reading librw's header -- it has macros
and static members that make eyeballing it unreliable. The failure this guards
against is silent: game code keeps compiling and starts reading the wrong field.

**Status: of the 124 RenderWare symbols the PC build references, 1 is defined by
the game itself, 119 are defined here, and 4 are left. The shim runs.** `src/SB/Core/pc/rw/README.md`
is the design record, `TODO.md` tracks the list, and
`src/SB/Core/pc/rw/tests/selftest.cpp` is 451 checks against a live librw engine.

Of the four, three are not this directory's job: one is guarded out on PC
already, and two are GameCube driver calls made UNGUARDED from portable code in
xModelBucket.cpp -- which is the single unit of 198 that does not compile, so
that number and these two symbols are the same fact reported twice. The fourth,
`_rpCollisionGeometryDataOffset`, is RenderWare's collision-BSP plugin and is
the same missing subsystem that makes `RpAtomicForAllIntersections` a linear
scan.

Separately from those, two functions LINK and return NULL --
`RpWorldStreamRead` and `RpCollisionWorldForAllIntersections`. They are one job
rather than two:
**librw has no world sectors at all.** That is the largest thing between here
and a playable port and it is written up in docs/PCPORT.md -- read it before planning
phase 4, because the way out is a project-level decision, not a function to
write.

Three more are missing that the old regeneration command was hiding, because its
`sed 's/^_*//'` stripped the leading underscore off names like
`_rwObjectHasFrameSetFrame` and left something the `^(Rw|Rp|Rt|Rx)` filter
dropped. The command in TODO.md is fixed. They will fail the link the moment
there is one:

  - `_rwFrameSyncDirty` -- 7 call sites; librw has `Frame::syncDirty`
  - `_rwInvSqrt` -- 3 call sites; no librw counterpart, write it out
  - `_rpMeshHeaderForAllMeshes` -- 1 call site, xJSP.cpp

`LIBRW_PLATFORM=NULL` is what was tested, which is the core with no renderer
backend. `GL3` or `D3D9` will want OpenGL or Direct3D present; neither was
needed to answer the question this spike asked.

### (f) The platform layer implementation -- RESOLVED: behind iHost

The 10 implementation files used POSIX directly at 26 sites: `sys/mman.h`,
`CLOCK_MONOTONIC`, `clock_nanosleep`, `localtime_r`, `mmap`/`munmap`,
`opendir`/`readdir`, `statvfs`, `mkdir`, `unlink`. `cmake --build` failed
immediately on Windows.

Those are now behind **`src/SB/Core/pc/iHost.h`** -- 21 functions covering
time, virtual memory and the filesystem -- with `iHostPosix.cpp` and
`iHostWin32.cpp` under it. Verified on Windows: the layer builds, links, and
the selftest passes 84/84.

The rule that keeps it from growing back: **an `#ifdef` for the host OS belongs
in an `iHost*.cpp`, never above it.** The files above the seam -- `iTime`,
`iMemMgr`, `iFile`, `iSystem`, `isavegame` -- hold the mapping onto the game's
semantics, which is the part worth reading and the part that is identical
everywhere. Run `pcprogress.py --host` after touching either backend.

Three things the port would have got wrong, which only surfaced by building it:

- **`rename()` is not portable.** POSIX `rename()` replaces the destination
  atomically; the Windows CRT's *fails* when the destination exists.
  `isavegame` writes to a temporary and renames it over the real save, exactly
  so a crash mid-write cannot destroy the previous one -- so on Windows every
  save after the first would have failed. `iHostRenameReplace` is
  `MOVEFILE_REPLACE_EXISTING`.
- **`Sleep()` is far too coarse to pace frames.** It rounds to the scheduler
  tick, about 15.6 ms by default -- most of a frame. `iHostSleepUntilNs` uses a
  high-resolution waitable timer, and takes an absolute deadline rather than a
  duration so the pacing cannot drift by the cost of computing the interval.
- **Windows has no `MAP_32BIT`.** The arena still has to live below 4 GB
  because `gMemInfo.DRAM.addr` is a `U32`. The Win32 backend asks
  `VirtualAlloc` for successive low base addresses and takes the first that is
  free, falling back to letting `iMemInit` refuse to start rather than
  truncating silently.

**Still Linux-only in one respect:** this was written and tested on Windows,
and `iHostPosix.cpp` is the original code moved rather than rewritten -- but it
has not been compiled since the move, because there is no Linux toolchain on
this machine. Build it there once before trusting it. `--host` confirms the two
backends declare the same 21 functions, which is the mismatch most likely to
bite, but it is not a compile.

## 6. Conventions that are not negotiable

From `docs/DUPLOTRON.md` and the `bfbb-decomp` skill, and they apply here too:

- **Never fabricate.** No stub that returns a constant to make something pass,
  no dead code, no `#if 0`. `docs/PCPORT.md` names a live example of the hazard:
  `zNPCFXCutscenePickTable` returns `NULL` to satisfy the diff, and in a port
  that means cutscene effects silently never play.
- **Never state a percentage you did not personally measure this session.**
  After a rollback, every number in your context is suspect.
- **Correctness outranks the number.** Several changes here were kept because
  they were right and cost percentage.
- **Say what a stub does not do.** Every deliberate divergence in the PC layer
  is argued at the site — see the "Known differences from retail, on purpose"
  section of `src/SB/Core/pc/README.md`, and add to it.
- One change, one measurement, one decision.

## 7. Things that will waste your time if you don't know them

- `xUtil.h` and `xScrFX.h` do not exist; the files are `xutil.h` and
  `xScrFx.h`. The GameCube build only works because CodeWarrior runs under
  wibo, which resolves includes case-insensitively. Both are fixed, but expect
  more of this shape.
- `u32` and `size_t` are the **same type** on the GameCube ABI (both
  `unsigned long`) and different types on LP64. Signatures that used them
  interchangeably stop matching. One was found (`ztalkbox::load`); there are
  likely more, and they only surface when a unit gets far enough to link.
- GCC quotes identifiers in diagnostics with Unicode `‘ ’`, not ASCII `'`. A
  regex over compiler output that assumes ASCII silently matches nothing —
  this cost a debugging round.
- `.gitignore` has `/*.txt`, which swallows `CMakeLists.txt`. There is a
  `!/CMakeLists.txt` negation; do not remove it.
- The `report.json` key names differ from what you may expect; the DOL hash is
  the gate, not the report.

## 8. Bugs hunted from a diagnostic, and what the diagnostics found

The `BFBB_*` environment variables are how this port is debugged: a `getenv`
inside `#ifdef PLATFORM_PC`, a `printf`, and usually a small static seen-array
so it reports once per distinct object rather than once per frame. The ones
that answered their question are deleted once the fix lands, because a probe
that survives its bug is noise the next reader has to rule out. What they
*found* is written down here instead, since that is the part worth keeping.

### Measure the object you think you are measuring

The most expensive mistake made on this branch, by a wide margin: a probe was
hung off `zEntSimpleObj_Render` to investigate why trees render too dark, and
it never fired for a tree even once. `zEntSimpleObj_Render` does not draw --
it **enqueues into a model bucket**, and the draw happens later at flush time
in `xModelBucket.cpp`, right before `xModelRenderSingle`. Every reading taken
that way came from some unrelated object that was working correctly, so every
reading said "everything checks out", and several confident hypotheses were
built on top of it over multiple playtest rounds.

What broke the deadlock was a correlation check that **returned zero** -- a
counter of how many tagged simple objects reached the render callback. Zero
tagged objects is not a null result; it is proof the probe and the subject
never coincided. Before trusting a diagnostic, spend one round proving it
fires on the actual subject. A probe that cannot report "I saw nothing" cannot
be trusted when it reports something.

### The dark trees -- FOUND, and the earlier account here was wrong

**librw lit every object in proportion to its own scale.** `uploadMatrices` set
the normal matrix to the world matrix, under a `// TODO: inverse transpose` that
had been sitting in librw's source the whole time. Normals do not transform by
the matrix: under a scale of s the world matrix lengthens a unit normal to s,
where the correct matrix shortens it to 1/s. Neither is unit, and the shaders do
not normalise -- the normal goes straight into `max(0, dot(N, -L))` -- so the
scale multiplied every directional term.

Bikini Bottom's props run from 0.11 to 9.0. The 47 objects at exactly 1.0 always
looked right; `plant_purple_curly` at 0.6756 and `plant_purple_dots` at 0.2500
went dark; objects at 9.0 saturated and looked FLAT AND UNSHADED. That is the
whole of the original report: "PC: tree shaded, missile not, GC: missile shaded,
tree not" was ONE bug sampled at two scales, not two bugs. Ambient was never
affected because it does not touch the normal, and the GameCube is right because
GX normalises in the transform unit. Fixed in the fork as `d3d9: take the scale
out of the normal matrix`.

**Everything the previous version of this section said about trees was measured
off the wrong object.** It recorded the tree as `PipeFlags 00980002`, `kit
1412C810`, `geo flags 00000037`. `00980002` is `bubble_buddy_bind.dff` in hb01's
PIPT -- the translucent figure standing a few feet from the plant. The plant is
`plant_purple_curly`, `PipeFlags 80986500`, and its real numbers are `geo flags
00000077`, no prelight at all (`colors 0`), ambient 0, four directionals with
correct colours and directions. This is the second time this exact mistake was
made on this bug, directly below the section warning about it.

Cleared by measurement, each after being believed: the pipeline split (80
objects on the default pipeline, 29 on skin) is a real correlation and NOT the
cause, most of the 29 being props that merely carry skin data; the frozen vertex
declaration; the `c4` collision between `uploadMatrices` and the shaders' `def`
block (real in the disassembly, but `def` constants are baked by the driver, so
writes to c4 never mattered); the shader-constant caches; the skin bone matrices
and hierarchy; the blend indices; and the light enumeration.

**What worked was a binary split, not another hypothesis.** Forcing ambient to
white lit the objects, proving the whole chain -- constants, shader selection,
vertex colour, pixel shader, blending -- and leaving only terms that touch the
normal. Forcing `surfDiffuse` to 20 lit them again, proving the directional loop
ran and the dot products were merely small. Between those two readings the
answer was two lines of C++. Reach for the test that halves the space before
reading further up the chain.
