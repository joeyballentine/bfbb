# PC port

The decompiled game code, compiled with a host toolchain against a PC platform
layer, rendering through librw and loading the Xbox release's assets. It runs on
Windows, Linux, macOS (x86_64) and Android.

This file covers how the branch is built, gated and kept apart from the
GameCube build, and the design facts a change is likely to run into.
`src/SB/Core/pc/README.md` documents the layer itself and `config.ini`. Related:
`docs/RENDERING.md`, `docs/ANDROID.md`, `docs/UNCAPPED.md`,
`src/SB/Core/pc/rw/README.md`.

## Branches

`treedome` is the port. It is rebased on `duplotron` and inherits its
CodeWarrior patch. The PC build never invokes CodeWarrior; the patch matters
only for the GameCube gate below.

- `duplotron`: decomp work, the compiler patch, `tools/gcgate.py`. Anything both
  branches need goes here.
- `treedome`: `src/SB/Core/pc/`, `CMakeLists.txt`, the port's tools and docs.

## Building

GameCube:

```sh
python tools/patch_compiler.py build/compilers
python configure.py
ninja > /tmp/gc.log 2>&1; echo "exit: $?"
python tools/gcgate.py
```

PC:

```sh
cmake -S . -B build-pc -G Ninja
cmake --build build-pc
ctest --test-dir build-pc
python tools/pcprogress.py --m32
```

`build-release.bat` wraps `tools/pcbuild.bat` on Windows
(`build-release.bat D3D9 x64`). Linux needs SDL's build dependencies; the `unix`
job in `.github/workflows/pc-port.yml` lists them.

### Word size

32-bit is the default on Windows (`BFBB_BUILD_32BIT`). Everywhere else the build
is 64-bit: macOS has no 32-bit runtime, and 32-bit Linux needs a multilib copy
of every library SDL links. On Android `ANDROID_ABI` picks. Both widths compile
and run. `tools/pcprogress.py --m32` measures the Windows default; keep the two
in step.

`BFBB_BUILD_32BIT` is read before `project()` through
`CMAKE_<LANG>_FLAGS_INIT`, so flipping it in an existing build directory does
nothing. Configure a fresh one.

### Render backends

`BFBB_RENDER_BACKENDS` is a list, and one executable carries all of it. The
default is `D3D9;D3D11;GL3` on Windows and `GL3` elsewhere. `VULKAN` is
optional. `NULL` alone is the headless configuration the self-tests use.
`video.backend` in `config.ini` picks one at startup.

D3D9, D3D11 and Vulkan are three implementations of librw's `rw::d3d`, each in
its own namespace; `third_party/librw/src/d3d/d3ddispatch.cpp` picks between
them per call. `src/SB/Core/pc/rw/backend.cpp` reads the setting.

### Other parts

- SDL3 is the platform layer: window (`iWindowSDL.cpp`), audio
  (`iSndHostSDL.cpp`, `iFMVAudioSDL.cpp`), input (`iPadHostSDL.cpp`,
  `iPadKeyboardSDL.cpp`). `BFBB_SDL` picks where SDL comes from.
- `BFBB_INPUT_BACKEND` and `BFBB_AUDIO_BACKEND` take `sdl` or `null`. `null`
  audio is silent and still keeps time.
- FFmpeg is optional. Without it, movies are skipped.
- `bfbb_config` is the settings front end, on wxWidgets
  (`src/SB/Core/pc/configurator/`). `BFBB_BUILD_CONFIGURATOR` turns it off.

## Matching and portable are different goals

Matching reproduces CodeWarrior's codegen for PowerPC. The port needs source that
is complete and behaves correctly. A `NonMatching` function that behaves right
is portable. A missing function is not.

`report.json` does not show a stub that matches but does the wrong thing, such
as a body returning `NULL` because the data it reads is not in our source.
`tools/stubs.py` finds candidates: `TODO`s, `return NULL;` one-liners, empty
bodies.

rwsdk, bink, MSL and dolphin are replaced on PC, not ported. Matching them does
nothing for the port.

## The GameCube gate

The GameCube build must stay byte-identical and must not lose matched functions.
Check both after every change to shared source:

```sh
ninja > /tmp/gc.log 2>&1; echo "exit: $?"
python tools/gcgate.py
```

```
  PASS  DOL       306526d90b48e99894c3138f5fc8f2716d9fecf6
  PASS  functions <baseline> / <percent>
```

The baseline is `tools/gcgate.json`. `gcgate.py --update` records a new one.
`gcgate.py --which <known-good report.json>` names the functions that
regressed.

Nothing else counts as proof: not "the edit is inside `#else`", not "a
declaration emits no code".

### Why the hash alone is not enough

Units marked `NonMatching` in `configure.py` link from the extracted object, not
from ours. Their source can drop to 0% while the DOL stays byte-identical. That
is where in-progress decomp work lives.

Editing a namespace, a qualifier or a `static` edits linkage. Inside
`namespace cruise_bubble { namespace { ... } }`, `void cruise_bubble::init()`
defines the outer-namespace function with external linkage; `void init()`
defines a different, internal one. Check the symbol table:

```sh
build/binutils/powerpc-eabi-objdump.exe -t build/GQPE78/src/SB/Game/zFoo.o | grep ' g  *F .text'
```

### Reading the result

- Never pipe `ninja` into `tail` or `head`. A pipeline reports the last
  command's exit status, and `head` truncates the build. A failed build leaves
  the previous `main.dol` and `report.json` in place, and `gcgate.py` passes on
  them.
- Confirm a FAIL before believing it, especially after a branch switch. An
  incremental build can hand back a stale object. Delete the named unit's `.o`,
  rebuild, and read again.

### One object first

A full build takes minutes; one object takes seconds. Build the baseline from
clean source:

```sh
git stash push -q <files>
ninja build/GQPE78/src/SB/Game/zFoo.o && cp build/GQPE78/src/SB/Game/zFoo.o /tmp/base.o
git stash pop -q
ninja build/GQPE78/src/SB/Game/zFoo.o
python tools/objsame.py /tmp/base.o build/GQPE78/src/SB/Game/zFoo.o
```

- Use `tools/objsame.py`, not `cmp`. Moving a source line shifts DWARF line
  entries, which never reach the DOL. `objsame.py` compares only the sections
  that do.
- Build the baseline. An object found lying in `build/` is not a baseline.

An object-level pass does not replace the full build and `gcgate.py`.

## How the two builds are kept apart

Game code includes platform headers unqualified (`#include "iTime.h"`), so the
include path decides which layer it gets.

| | GameCube | PC |
|---|---|---|
| driver | `configure.py` -> `build.ninja` -> decomp-toolkit | `CMakeLists.txt` |
| compiler | CodeWarrior `GC/2.0p1a` (patched) | host clang or g++ |
| platform layer | `-i src/SB/Core/gc` | `-I src/SB/Core/pc` |
| define | `GAMECUBE` | `PLATFORM_PC` |

Neither directory is on the other build's path. `configure.py` lists every
object explicitly, so a `pc/` file cannot enter the GameCube build.

### Which macro to gate on

- `__MWERKS__`: can this compiler parse it. MSL headers, `namespace std`
  re-declarations, PPC intrinsics, placement `new`, CodeWarrior extensions.
  `src/dolphin` and `src/bink` are compiled by CodeWarrior without `-DGAMECUBE`
  (only `cflags_bfbb` defines it). Gating a compiler question on `GAMECUBE`
  sends them into the host branch.
- `GAMECUBE`: is this the console. Dolphin includes, hardware behaviour,
  `.sdata2` placement, the memory-card screen.

`src/SB/Core/gc/iFile.h` and `src/SB/Core/x/xFont.cpp` show the house idiom:
`#ifdef GAMECUBE / #else #ifdef PS2 / #else`.

### Platform differences go in the i* layer

When the PC needs different behaviour in shared game code, add a function to the
`i*` header pair (`gc/iPad.h`, `pc/iPad.h`) and call it from the game file. Do
not add `#ifdef PLATFORM_PC` arms to `src/SB/Game` or `src/SB/Core/x`.

On the GameCube side an identity mapping must be a macro. An `inline` identity
function still changes CodeWarrior's code for the caller.
`iPadTalkBoxButtons` is the example: a macro in `gc/iPad.h`, an out-of-line
function in `pc/iPad.cpp`.

Use enum names (`eEventPadPress*`, `XPAD_BUTTON_*`), not numbers, including in
retail-shaped code.

Host-OS `#ifdef`s belong in an `iHost*.cpp`, never above one. `iTime`,
`iMemMgr`, `iFile`, `iSystem` and `isavegame` sit above `iHost.h` and are
identical on every OS.

## Measuring the port

- `tools/pcprogress.py`: does each unit of `src/SB` compile against the PC
  headers. `--m32` measures the Windows default. `--list` per-unit verdict,
  `--errors` first error per failing unit, `--cc` picks the compiler, positional
  args filter (`pcprogress.py xEnt zPlayer`).
- `pcprogress.py --drift`: checks the files listed in
  `src/SB/Core/pc/VERBATIM.txt`, copied unchanged from `gc/`, against the gc
  originals.
- `pcprogress.py --host`: both `iHost` backends implement everything `iHost.h`
  declares. Only one backend is built on any machine, so nothing else notices a
  divergence. Run it after touching either.
- `tools/pclink.py`: links the archives and reports unresolved symbols.
  Compiling each unit alone cannot see cross-object defects;
  `src/SB/Core/pc/LINKING.md` lists what the first link found.
- `pc_selftest`, `rw_selftest`, `fps_selftest`: run by `ctest`. `rw_selftest`
  runs against a live librw engine.
- `tools/mirrors.py`: lists structs declared in two places. A mirrored copy that
  drifts from the real one corrupts memory silently.

If a flag changes the `pcprogress.py` number, check that it changed the port and
not the diagnostic policy:

- Do not add `-fpermissive`. It downgrades real errors and once inflated the
  count by 34 units.
- Do not use `-w` with clang. clang makes `(U32)pointer` a warning, so `-w` hides
  the pointer-width class. clang's `-w` cannot be overridden by a later
  `-Werror=`; `-Wno-everything` can.

## The RenderWare shim

librw reimplements RenderWare's API in its own `rw::` namespace and defines no
`Rw*`/`Rp*` C functions. The game calls the C API, and has to, because the
GameCube build matches those call sites. `src/SB/Core/pc/rw` implements the C
API on top of `rw::`. `src/SB/Core/pc/rw/README.md` is its design record and
`TODO.md` its list.

In a PC build the RenderWare types in `include/rwsdk` are declared with librw's
field order under RenderWare's field names. An `RwFrame*` is an `rw::Frame*`;
nothing converts at the seam.

**A type mirrored in `include/rwsdk` gets its size and every field offset
asserted in `src/SB/Core/pc/rw/layout*.cpp`, in the same commit.** Take the
offsets from the compiler, not from reading librw's header. A wrong offset
compiles and reads the wrong field.

### World sectors

librw has no world sectors: no `RpWorldSector` counterpart, no plane sectors, no
world chunk reader. `rw::World` holds clumps and lights. `World::render` walks
the clump list.

The Xbox assets need none of it. All 121 packs carry 0 BSP and 110 JSP assets.
A JSP's payload is a clump, loaded through `RpClumpStreamRead`.
`src/SB/Core/gc/iEnv.cpp` gives a JSP level an empty world
(`RpWorldCreate(&tmpbbox)`) and takes collision from the JSP's own tree
through `xClumpColl_InstancePointers`. `xCollide.cpp` branches on the JSP
before it reaches `RpCollisionWorldForAllIntersections`.

- `RpWorldStreamRead` reads only the bounding box and returns an empty world.
  Mods ship placeholder BSPs for scenes with no level, and `iCameraAssignEnv`
  adds the camera to `env->world` without a NULL check. A world with triangles
  is reported, since its geometry, materials and collision are dropped.
- `RpCollisionWorldForAllIntersections` returns NULL. There is no sector tree to
  walk.

A BSP level would need a sector implementation in librw, including a pipeline
for GameCube display-list sector geometry (`iFX.cpp` reads
`_rpDlWorldVtxFmtOffset` off the current world).

## Word size and the low-4 GB arena

Retail addresses memory in 32 bits. `xMemInitHeap` does arithmetic on
`gMemInfo.DRAM.addr`, a `U32`. Pointer casts go through `UPtr`
(`include/types.h`): `U32` on the GameCube, `uintptr_t` on a host.

`iMemInit` (`iMemMgr.cpp`) reserves the game arena below 4 GB through
`iHostReserveLow` and refuses to start if it lands above ("outside the low
4 GB"). That keeps every game-allocator pointer valid in a `U32` at both widths.
64-bit therefore buys no extra memory.

The arena is reserved at twice `IMEM_DRAM_SIZE`. `xMemInit` places `gxHeap[1]`
and `gxHeap[2]` past the end of the DRAM block; on the console that is unclaimed
OS arena.

librw is outside the arena. `iSystem.cpp` starts it with a NULL memory-function
table, so it allocates with `malloc` and can return a high pointer. Fields that
hold a RenderWare pointer are widened instead: `xShadowSimpleCache::raster`,
`xSndVoiceInfo::parentID`, `RpCollisionTriangle::index`. `xModel::shadowID`
still truncates one; it is only compared against a sentinel.

macOS needs `__PAGEZERO` shrunk to get a low arena (`bfbb_host_link_options` in
`CMakeLists.txt`). arm64 macOS requires a full-size `__PAGEZERO`, so there is no
low 4 GB on Apple Silicon; the port stops in `iMemInit`. The x86_64 build runs
under Rosetta. `docs/ANDROID.md` covers the same question on Android.

## Asset caveats

The port reads the Xbox release's assets. Xbox is little-endian x86, so the code
that casts raw asset buffers to structs reads correct values. Against GameCube
assets every such read is byte-swapped.

**The HIP container is big-endian on every platform.** The payloads inside an
Xbox pack are little-endian. `src/SB/Core/x/xbinio.cpp` already swaps
(`ReadMShorts`, `ReadMLongs`, `ReadMFloats` behind `ENDIAN`, selected by
`GAMECUBE`). Do not add a second swap; the selftest checks this.

**The code is GameCube-derived; the assets are Xbox-derived.** Struct layouts
were recovered from the CodeWarrior binary. Xbox assets were serialized from
MSVC layouts. Padding, bitfield order and enum width can differ. Validate per
asset type.

**Version and content drift.** The decomp targets `GQPE78`. The Xbox release is
a different SKU. Do not assume asset IDs and contents correspond 1:1.

**Pointer width.** Asset-overlaid structs assume 4-byte pointers. A 64-bit
build handles five classes:

- Round-trip arithmetic (`(U32)ptr + n` cast back to a pointer,
  `RwRenderStateSet(state, (void*)value)`): through `UPtr`.
- Allocation arithmetic that says 4 where it means `sizeof(void*)`, such as
  `count << 2` for an array of pointers. No compiler warns about it.
- Pointers parked in 32-bit fields: valid because of the low arena; RenderWare
  pointers are widened. See the section above.
- Asset structs that contain pointers: ATBL, LKIT, CTOC, CRDT, COLL, the JSP node
  list and an ANIM asset's morph sequence are copied at load into allocations
  laid out for the build's structs, all but ATBL and the JSP list through
  `readXForm`. The retail asset set has no morph sequence, so that transform is
  untested.
- Struct declarations mirrored in two headers: `tools/mirrors.py` lists them.

## Latent retail bugs, and NON_MATCHING

Some retail code reads uninitialised stack, and retail's frame contents happen to
make the read harmless. A faithful decomp inherits the read without the luck. A
host never reproduces PowerPC frame layout, so the port needs the real fix
regardless of match percentage.

`configure.py --non-matching` appends `-DNON_MATCHING` to `cflags_bfbb`. Source
guarded by it is absent from the matching build and present in every build that
runs, including the port.

`NPCC_ANIM_LIST_END` in `zNPCTypeCommon.h` expands to `, 0` under
`NON_MATCHING`. It terminates `ZNPC_AnimTable_Dutchman`'s and
`ZNPC_AnimTable_Prawn`'s anim lists, which retail left unterminated;
`NPCC_BuildStandardAnimTran` scans until it finds a 0.

A new use must leave the matching `.o` byte-identical and pass `gcgate.py`. Use
it only for defects that are provably retail's.

## Conventions

- Never fabricate. No stub that returns a constant to make something pass, no
  dead code, no `#if 0`.
- Correctness outranks the match percentage.
- Never state a percentage you did not measure in this session.
- Say what a stub does not do. Deliberate divergences from retail are listed
  under "Known differences from retail, on purpose" in
  `src/SB/Core/pc/README.md`; add to it.
- Delete debug probes (`BFBB_*` getenv + printf) before committing the fix.
- One change, one measurement, one decision.

## Other things that will bite

**Strict aliasing.** The source is full of `*(U32*)&someFloat`.
`-fno-strict-aliasing` is mandatory.

**Floating point divergence.** Gekko has paired singles, fused multiply-add and
`fres`/`frsqrte` estimates with non-IEEE precision. A host computes the exact
value, which is a different value. Anything tuned against the estimate's error
drifts slightly. `compat/intrin.h` implements `__frsqrte`.

**CodeWarrior extensions.** `src/SB/Core/pc/compat/` shadows `<math.h>`,
`<string.h>`, `<cmath>`, `<mem.h>` and `<intrin.h>` and chains with
`include_next`; it must come first on the include path.
`src/PowerPC_EABI_Support/include/math.h` defines `__fabs` as an identity macro
for non-CodeWarrior compilers; the selftest checks the port does not pick it up.
glibc declares `__fabs`/`__fabsf` without defining them; `intrin.cpp` supplies
the bodies.

**Case-sensitive includes.** CodeWarrior runs under wibo, which resolves
includes case-insensitively. A wrong-case include builds on the GameCube and
fails on Linux.

**`u32` and `size_t`** are both `unsigned long` on the GameCube ABI and differ on
a host. Declarations and definitions that mix them stop matching at link time.

**GCC diagnostics** quote identifiers with Unicode `‘ ’`. A regex over compiler
output that assumes ASCII `'` matches nothing.

**`.gitignore`** has `/*.txt`, which covers `CMakeLists.txt`. Keep the
`!/CMakeLists.txt` negation.

**Measure the object you think you are measuring.** `zEntSimpleObj_Render`
enqueues into a model bucket; the draw happens at flush time in
`xModelBucket.cpp`. Key probes by object and prove a probe fires on its subject
before trusting what it reports.

## Distribution

Code only. The user supplies their own copy of the Xbox release (`[assets] path`
in `config.ini`, or `BFBB_ASSETS`). RenderWare and Bink are proprietary: librw
replaces the first, and the port plays the Xbox `.xmv` movies through FFmpeg
instead of decoding Bink.
