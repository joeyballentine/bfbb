# The first full link, and what it found

`tools/pcprogress.py` reports 198 / 198 units compiling. That number is real
and it is not the same claim as "the port links". pcprogress compiles each
translation unit **alone**, with `-fsyntax-only`; it is structurally incapable
of seeing anything that only exists when two objects are put together.

`tools/pclink.py` is the stricter measurement. It builds the four archives and
asks a linker to resolve them. The first run found five defects in one pass,
none of which any amount of compiling would have shown.

## What the first link found

**1. `WEAK` expanded to nothing on every compiler that is not CodeWarrior.**
`include/types.h` stubs `__declspec(x)` out for compilers that cannot parse it,
and `WEAK` was defined as `__declspec(weak)` *after* that stub -- so it expanded
to the empty string. Every `WEAK` definition in the game was a strong
definition on PC: `iFileAsyncService`, `xVec3LengthFast`, `xSCurve`,
`xVec3DistFast`, `xMat3x3MulRotC`, `xVec3ScaleC`, `xModelAnimCollStop`. They
are `WEAK` because CodeWarrior emitted one copy into whichever object it picked
and the decomp reproduces that placement. `iFileAsyncService` was the one that
fired, against the port's own `iFile.cpp`. `WEAK` is now
`__attribute__((weak))` on GCC and Clang, which is a COFF weak external and the
semantics the definitions were asking for.

**2. `globals` is defined in both `zMain.cpp` and `xCamera.cpp`.** CodeWarrior
merges the two as common symbols -- one allocation, both objects carrying the
definition because both need the symbol where their own layouts expect it. C++
has no common symbols. `zMain.cpp` is the owner on PC and `xCamera.cpp` takes a
declaration, under `#ifdef PLATFORM_PC`; the console keeps both definitions.

**3. `xSndPlay3D` has one out-of-line body and N inline ones.** That is
deliberate and load-bearing on the console -- `zEnt.cpp` and
`zEntDestructObj.cpp` define `XSNDPLAY3D_OUT_OF_LINE` so the body lands where
their object layouts need it, and both are Matching units. A host linker sees a
strong definition in one object and a COMDAT in each of the others and refuses.
PC opts out of the trick and lets every unit inline it.

**4. Three RenderWare functions had the wrong linkage.**
`RwGameCubeSetMinRetraceCount`, `RwGameCubeSetAlphaCompare` and
`_rwDlRenderStateSetZCompLoc` were defined with C++ linkage while their callers
declare them `extern "C"`. Worth recording that the second and third were
written the same day the first was found and documented, and repeated it
exactly: the fix is not vigilance, it is that `engine.cpp` now includes
`rwsdk/driver/gcn/dlrendst.h`, the header that declares them, so the compiler
checks the linkage instead of the linker.

## What the link left behind

**iFMV will not be a port of gc/iFMV.cpp.** It is the one unported module whose
GameCube source is mostly Bink calls, and the decision taken is to convert the
videos with ffmpeg ahead of time and play them back with something else, rather
than to reimplement or license Bink. So its symbols in the count below are a
placeholder for a different piece of work, not for a translation of the
existing file. Nothing else in the port depends on it -- FMV playback is
skippable -- so it is last.

**The 71 are the known worklist**, not news: eleven modules under
`src/SB/Core/gc` have a header in `src/SB/Core/pc` and no implementation.
`iModel` (21) and `iParMgr` (12) dominate. Four of them are globals whose names
do not carry the module -- `gLightWorld`, `gRenderArr`, `gRenderBuffer`,
`giAnimScratch` -- and `pclink.py` maps those explicitly, from asking which
object defines them in the GameCube build.

**The 2 RenderWare ones** are `_rpCollisionGeometryDataOffset` (the collision
BSP plugin, the same missing subsystem that makes `RpAtomicForAllIntersections`
a linear scan -- see `rw/TODO.md`) and `AtomicDefaultRenderCallBack`, which
retail has in `rwsdk/world/baclump.c` and the shim does not provide at all.

## The 8 "game code" ones, and what they turned out to be (FIXED)

All eight are resolved and the gate did not move: DOL
`306526d90b48e99894c3138f5fc8f2716d9fecf6`, 7249 / 80.66999%.

Six were **not missing functions**. They were defined, and defined with a
different type than the code calling them declared:

| symbol | defined | declared |
|---|---|---|
| `zEntPlayerDyingInGoo` | `S32` (zEntPlayer.cpp) | `U8` (zNPCTypeBossPatrick.cpp) |
| `xSndStreamReady` | `U32` (xSnd.cpp) | `U8` (zTalkBox.cpp) |
| `NPCC_LineHitsBound` | `U32` (zNPCSupport.cpp) | `S32` (zNPCGoalRobo.cpp) |
| `zMenuCardCheckStartup` | `bool` (zMenu.cpp, zMenu.h) | `S32` (zMain.cpp) |
| `LERP` (4-arg) | `xVec3*` (zNPCHazard.cpp) | `void` (zNPCGoalAmbient.cpp) |
| `menu_fmv_played` | `bool` (zMenu.cpp) | `U8` (zDispatcher.cpp) |

**PowerPC CodeWarrior does not encode the return type in a mangled name, and
MSVC does.** So retail resolves both spellings of each to one symbol and links;
here they are two symbols and one of them has no definition.

**An earlier draft of this file said "in each row one side is wrong about what
retail returns". That was wrong, and the repository already contained the
evidence.** Retail itself had these splits, and at least two are load-bearing
for matching, with the measurements recorded at the call sites:

  - `zNPCTypeBossPatrick.cpp` carries a note saying that declaring
    `zEntPlayerDyingInGoo` as `S32` there to agree with the definition costs a
    whole function -- the unit drops 70/71 to 69/71 and the 6040-byte
    `zNPCBPatrick::Process` is what falls out, because it tests only the low
    byte of the result. It ends "Do not 'fix' it."
  - `zMain.cpp` carries a note saying retail's `zMain.o` assigns
    `zMenuCardCheckStartup`'s result with a plain `mr`, which only an int-typed
    declaration produces.

So the disagreement is faithful, not a defect, and the console must keep it.
Each declaration is now guarded: `#ifdef PLATFORM_PC` takes the definition's
type, `#else` keeps retail's. The console's preprocessed output is unchanged by
construction, which is why matching cannot regress here -- and the gate was run
anyway.

The remaining two were a different thing, and are a real portability defect
rather than a faithfulness one: `xBoulderGenerator_Init`
(xEntBoulder.cpp) and `bungee_state::load` (zEntPlayerBungeeState.cpp) each
declared a parameter `size_t` in the header and defined it `unsigned long`.
`include/types.h` typedefs `size_t` to `unsigned long` for the console, so the
two agree there; on 32-bit Windows `size_t` is `unsigned int`, and `unsigned
int` and `unsigned long` are distinct types for overload resolution even at the
same width. Both definitions now say `size_t`, which is the same declaration on
the console and needs no guard.

## What is left after that: 73

    platform modules not yet ported    71
    RenderWare the shim lacks           2
    game code                           0

## Running it

    cmake -S . -B build-pc -G Ninja -DCMAKE_CXX_COMPILER=clang++
    cmake --build build-pc
    python tools/pclink.py            # summary
    python tools/pclink.py --list     # every symbol
    python tools/pclink.py --raw      # the linker's own output

A duplicate symbol stops the linker before it resolves anything, so while any
are reported the undefined count means nothing. `pclink.py` says so rather than
printing a number that is not yet true.
