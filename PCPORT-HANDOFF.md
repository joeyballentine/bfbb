# PC port — hand-off

For whoever picks this up next. `PCPORT.md` is the design record and the
argument; this is the operating manual. Read this first, then
`src/SB/Core/pc/README.md` for the layer itself.

Branch: **`claude/pc-port-platform-layer`**, off `duplicatotron-3000`.

---

## 0. Before anything else: the container rolls back

This happened **six times** in one session. The working tree reverts to commit
`3b74efe` and everything uncommitted is gone, including files under `/tmp`.

Symptoms: `git log --oneline -1` shows `3b74efe`, `src/SB/Core/pc/` does not
exist, `tools/objsame.py` is missing.

Recovery, and run this **at the start of every session** before trusting
anything:

```sh
cd /home/user/bfbb
SC=/tmp/claude-0/-home-user-bfbb/<session-id>/scratchpad     # your scratchpad

git fetch origin claude/pc-port-platform-layer
git checkout -B claude/pc-port-platform-layer origin/claude/pc-port-platform-layer

rm -rf $SC/compilers/GC/2.0p1a
python3 tools/patch_compiler.py $SC/compilers
python3 configure.py --compilers $SC/compilers
```

Then confirm three things before you measure anything:

```sh
git log --oneline -1                            # a6b4929 or later
sha1sum $SC/compilers/GC/2.0p1a/mwcceppc.exe    # 5c6862b641adb8845f0fc09a6569902df068a83f
ls src/SB/Core/pc/*.cpp | wc -l                 # 10
```

**Commit and push often.** Anything not pushed is one rollback from gone. Two
of the six rollbacks cost real work.

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
$ python3 tools/pcprogress.py
compiles            169 / 198 units   85.35%
pointer width only   26 / 198 units   13.13%   (see PCPORT.md, Asset caveats)
needs other work      3 / 198 units
```

- `--list` per-unit verdict, `--errors` first error per failing unit,
  `--drift` checks the 16 headers copied verbatim from `gc/` for divergence.
- Positional args filter: `pcprogress.py xEnt zPlayer`.

**The middle line is one decision, not 26 defects.** See §5.

**Do not add `-fpermissive` back.** It was there once and inflated the number
by 34 units — it read 97.5% where the truth was 80.3%, and it disagreed with
`CMakeLists.txt`, which never passed it. If you widen the flags, make the two
agree.

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
  `null` is a real configuration (no controllers), not a placeholder. No SDL2
  in this container and `apt` could not reach a mirror.
- `VERBATIM.txt` — the 16 headers copied unchanged from `gc/`, with the hash
  each was copied at. `pcprogress.py --drift` reports divergence.

---

## 5. Open work, ranked

### (a) The 32-bit decision — 26 units, one call

The largest blocker is `(U32)somePointer`. `xMemInitHeap` does arithmetic on
`gMemInfo.DRAM.addr`, a `U32`; asset-overlaid structs address memory in 32
bits throughout. On LP64 every such cast truncates.

**This is not 26 fixes.** It is the open question in `PCPORT.md` →
**Asset caveats**: build 32-bit, or separate on-disk formats from in-memory
structs. Changing `(U32)` to `(uintptr_t)` at the cast sites does *not* solve
it — the *storage* is 32 bits wide.

The cheapest experiment: **build with `-m32` and re-run `pcprogress.py`.** If
the 26 clear, "build 32-bit" is validated cheaply. **This was never tested** —
this container has no multilib (`g++ -m32` cannot find `bits/libc-header-start.h`).
Install `gcc-multilib` and try it; it is the single highest-value next step.

`iMemInit` already assumes the answer: it reserves its arena with
`mmap(MAP_32BIT)` and refuses to start above 4 GB rather than truncating
silently. That buys the allocator only, not asset layouts.

The 26: `xAnim xCM xClumpColl xCutscene xHud xMemMgr xMorph xPartition
xShadowSimple xSnd xpkrsvc xserializer zAnimList zAssetTypes zCollGeom
zCutsceneMgr zGame zLOD zNPCHazard zNPCMessenger zNPCSupport
zNPCTypeBossPlankton zNPCTypeCommon zParPTank zScene zTalkBox`.

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

## 6. Conventions that are not negotiable

From `DUPLICATOTRON.md` and the `bfbb-decomp` skill, and they apply here too:

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
