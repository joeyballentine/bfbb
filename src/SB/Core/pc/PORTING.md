# Porting a platform module

Eleven modules under `src/SB/Core/gc` have a PC header and, until recently, no
PC implementation. This is how one gets ported, written after doing `iLight`.

## The finding that shapes all of this

**Most of these modules are not GameCube code.** They are RenderWare C API calls
and game logic that happen to live in the platform layer. `gc/iLight.cpp` has
zero GX, OS, DVD or AX calls in it -- it is `RpLightCreate`, `RwFrameTransform`,
`RpLightSetColor` and so on from top to bottom, all of which the shim already
provides. So the port was a **verbatim copy**, and that is the first thing to
check rather than the last.

`iMath3.cpp` and `iCollideFast.cpp` were already copied this way. `iLight.cpp`
makes three. Measured GameCube-specific line counts for the rest:

    iModel   1087 lines, 1     iEnv    214, 0     iDraw    41, 4
    iParMgr   986 lines, 0     iMorph  303, 0     iFX     213, 37
    iScrFX    210 lines, 2     iAnim   234, 0     iFMV    422, 70

**Nine of the eleven are now done, and seven of those nine were verbatim
copies**: iLight, iEnv, iAnim, iMorph, iParMgr, plus iMath3 and iCollideFast
from before. Two needed one hunk each -- iModel for RpAtomic's missing
interpolator, iScrFX for a RwRasterLock bracket a host must not honour -- and
iDraw is a documented no-op that loses a behaviour. iFX and iFMV are left.

`iFX` and `iFMV` are the only two that are substantially GameCube. `iFMV` is not
getting ported at all -- the decision taken is to convert the videos with ffmpeg
and play them back with something else rather than reimplement Bink.

## If you are working in a fresh git worktree

Two things bite immediately, both reported by agents who hit them:

  * **The submodule is not populated.** `cmake` refuses with "third_party/librw
    is empty". Run `git submodule update --init --recursive` first; it checks
    out the pinned commit and leaves `git status` clean.
  * **Check what your worktree was branched from.** Both agents in one round
    were handed a worktree cut from a master-line commit rather than from
    `treedome`, where `src/SB/Core/pc/` does not exist at all. `git log --oneline -1`
    before you start, and `git reset --hard treedome` if it is wrong.

## The procedure

1. **Read `src/SB/Core/gc/<module>.cpp`.** Grep it for `GX`, `OS`, `DVD`, `AX`,
   `dolphin/`, `_rwDl`, `GameCube`. If nothing turns up, expect a copy.

2. **Copy it to `src/SB/Core/pc/<module>.cpp`** and try to build. What usually
   fails is not the module's own code but a header it includes transitively --
   `dolphin/os.h` and friends. That is the thing to solve, and solving it once
   helps every other module.

3. **If it needs real changes**, guard them with `#ifdef PLATFORM_PC` when the
   file is shared with the console, and say WHY at the site. Never edit
   `src/SB/Core/gc` -- that build is scored by a byte-identical DOL.

4. **Check the enumerations.** A module that passes a RenderWare enum through to
   librw depends on the two agreeing. `rw/layout_camera.cpp` and its siblings
   assert these at compile time; if the one you need is not asserted, add it in
   the same commit. `iLight` passes `rpLIGHTPOINT` and `rpLIGHTLIGHTATOMICS`,
   both already covered.

5. **Wire it into `CMakeLists.txt`**: take the file out of the STUBS block in
   `bfbb_platform` and put it with the real sources. If it is a verbatim copy,
   add its sha1 to `VERBATIM.txt` -- `tools/pcprogress.py --drift` then reports
   it when the gc copy changes underneath and the two silently diverge.

## What "done" means -- all five, every time

    ninja && python tools/gcgate.py     # DOL 306526d90b48e99894c3138f5fc8f2716d9fecf6
                                        # and 7249 / 80.66999%. NON-NEGOTIABLE.
    cmake --build build-pc              # builds clean
    python tools/pclink.py              # 0 unresolved
    build-pc/rw_selftest.exe            # 0 failures. The COUNT rises as the shim
                                        # grows -- 502 at the time of writing --
                                        # so read the failure count, not the total.
    python tools/pcprogress.py --drift  # 0 drifted

`gcgate.py` is the one that matters most and the one it is easiest to skip.
**Read ninja's exit status** -- a failed build leaves the previous DOL and
report.json in place and the gate then passes on stale output.

## Things that have already bitten, so that they do not again

**A global that overrides a CRT function takes the whole process with it.**
`xSpline.cpp` defined a global `double sqrt(double)` -- retail's frsqrte
refinement -- and on a host it replaced the CRT's sqrt for librw too. The port's
`__frsqrte` was written in terms of a square root, so sqrt called __frsqrte
called sqrt, and the boot recursed until the stack died. If a module defines a
function the C library also has, guard it with `#ifdef __MWERKS__`.

**Runaway recursion does not look like a stack problem.** It presents as
`EXCEPTION_ACCESS_VIOLATION` past the guard page, with no stack left to report
on: no unhandled-exception filter, no vectored handler, no `__except`, and a
bigger stack does not help. `llvm-nm` over the archives is what finds it -- a
real definition sits at a nonzero offset, a COMDAT inline sits at 0.

**The selftest does not link the game.** `rw_selftest` links only `bfbb_rw`, so
anything the game defines that shadows a library function is invisible to it. A
green selftest says nothing about the boot.

**Check what the module needs at STARTUP versus at RUNTIME.** `iLight` is not
reached during boot at all -- it is called when a level loads its lights. So a
green boot after porting it proves the build, not the module. Say which you have
shown.
