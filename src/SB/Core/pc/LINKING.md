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

## What is left: 81 unresolved symbols

    platform modules not yet ported    71
    RenderWare the shim lacks           2
    game code                           8

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

## The 8 "game code" ones are the interesting result

Six of them are **not missing functions**. They are defined, and they are
defined with a **different return type than the code calling them declares**:

| symbol | defined | declared |
|---|---|---|
| `zEntPlayerDyingInGoo` | `S32` (zEntPlayer.cpp:16276) | `U8` (zNPCTypeBossPatrick.cpp:43) |
| `xSndStreamReady` | `U32` (xSnd.cpp:834) | `U8` (zTalkBox.cpp:20) |
| `NPCC_LineHitsBound` | `U32` (zNPCSupport.cpp:1087) | `S32` (zNPCGoalRobo.cpp:60) |
| `zMenuCardCheckStartup` | `bool` (zMenu.cpp:344, and zMenu.h) | `S32` (zMain.cpp:94, a local re-declaration) |
| `LERP` | -- (zNPCGoalRobo.o on the console) | `xVec3*` (zNPCFXCinematic.cpp:55) vs `void` (zNPCGoalAmbient.cpp:121) |

**PowerPC CodeWarrior does not encode the return type in a mangled name, and
MSVC does.** So on the console every one of these resolves to the same symbol
and links; here they are two different symbols and one of them is undefined.

That makes this link a decomp correctness check that the GameCube build cannot
perform. In each row one side is wrong about what retail's function returns,
and the disagreement has been invisible. None is fixed here: changing a return
type can change codegen, so each needs verifying against the DOL on its own.

**`xBoulderGenerator_Init` is a different thing** -- a portability defect, not a
decomp one. `xEntBoulder.h:83` declares the third parameter `size_t` and
`xEntBoulder.cpp:1080` defines it `unsigned long`. `include/types.h` typedefs
`size_t` to `unsigned long` for the console, so they agree there; on 32-bit
Windows `size_t` is `unsigned int`, and `unsigned int` and `unsigned long` are
distinct types for overload resolution even though both are 32 bits.

**`bungee_state::load` and `menu_fmv_played`** are referenced and are defined
nowhere -- not here and not in the GameCube build either. They want looking at
separately.

## Running it

    cmake -S . -B build-pc -G Ninja -DCMAKE_CXX_COMPILER=clang++
    cmake --build build-pc
    python tools/pclink.py            # summary
    python tools/pclink.py --list     # every symbol
    python tools/pclink.py --raw      # the linker's own output

A duplicate symbol stops the linker before it resolves anything, so while any
are reported the undefined count means nothing. `pclink.py` says so rather than
printing a number that is not yet true.
