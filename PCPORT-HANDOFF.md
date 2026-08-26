# PC port — hand-off

For whoever picks this up next. `PCPORT.md` is the design record and the
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

Note that `cmake --build` currently fails on Windows -- the platform layer's own
implementation is POSIX-only. See 5(f).

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

`src/SB/Core/pc/tests/selftest.cpp` is the other half: **86 checks** over every
implemented interface that does not need a renderer, so "implemented" is a
measurement rather than a claim.

```sh
cmake -S . -B build-pc -G Ninja && cmake --build build-pc && ./build-pc/pc_selftest
```

---

## 4. What exists

`src/SB/Core/pc/` — 10 implementation files, 72 of the 178 interface functions
game code actually calls.

**Done:** `iTime` `iMath` `iMemMgr` `iFile` `iPad` `iSystem` `iTRC` `iColor`
`isavegame`, plus `intrin.cpp` (`__fabs`/`__fabsf` — glibc declares both and
defines neither).

**Header only, no body:** `iSnd` (22 fns, the largest group after rendering),
`iModel` (19), `iMath3` (10), `iCollide` (10), `iScrFX` (10), `iEnv` (7),
`iLight` (7), `iAnim` (5), `iCutscene` (4), `iParMgr` (4), `iDraw` (3),
`iMorph` (2), `iFX` (1), `iCollideFast` (1), `iFMV` (1). All but `iSnd`,
`iMath3` and `iCollide` need librw.

**Not interfaces:**

- `compat/` shadows `<math.h>` `<string.h>` `<cmath>` `<mem.h>` `<intrin.h>`
  to supply the CodeWarrior extensions the sources use, chaining with
  `include_next`. Must come first on the include path. Note that
  `src/PowerPC_EABI_Support/include/math.h` defines `__fabs` as an **identity**
  macro for non-CodeWarrior compilers to quiet clangd — a port picking that up
  gets `FABS(-3) == -3`. The selftest checks this specifically.
- `iPadHost.h` / `iPadHostNull.cpp` — the device end of input, behind a seam.
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

### (b) `iSnd` — 22 functions

The largest interface group that does not need a renderer. Follow the `iPad`
shape: put the device end behind a backend seam with a null implementation that
always compiles, and keep the mapping onto the game's semantics on this side.
Do **not** make the layer depend on an audio library being present at build
time.

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

Gates the other 14 interfaces. `rwcore.h` already parses under GCC, so game
code compiles against RenderWare's declarations today; only linking needs
librw. re3/reVC is the precedent.

---

### (f) The platform layer implementation is Linux-only

Distinct from (a), and not visible to `pcprogress.py`. The 10 implementation
files were written in a Linux container and use POSIX directly: `sys/mman.h`,
`CLOCK_MONOTONIC`, `localtime_r`, `mmap`/`munmap`, `opendir`/`readdir`. On
Windows `cmake --build` fails immediately.

26 sites across 5 files — `isavegame.cpp` (8), `iMemMgr.cpp` (6),
`iFile.cpp` (6), `iTime.cpp` (4), `iSystem.cpp` (2).

This is bounded and well isolated, and it is exactly the seam `iPadHost.h`
already models: put the host calls behind a small backend interface with one
implementation per platform, rather than `#ifdef`-ing each call site. Until
then, the layer builds on Linux only, and "72 of 178 interface functions
implemented" should be read as *implemented for Linux*.

## 6. Conventions that are not negotiable

From `DUPLOTRON.md` and the `bfbb-decomp` skill, and they apply here too:

- **Never fabricate.** No stub that returns a constant to make something pass,
  no dead code, no `#if 0`. `PCPORT.md` names a live example of the hazard:
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
