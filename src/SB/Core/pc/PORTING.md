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

**Ten of the eleven are now done, and seven of those ten were verbatim
copies**: iLight, iEnv, iAnim, iMorph, iParMgr, plus iMath3 and iCollideFast
from before. Two needed one hunk each -- iModel for RpAtomic's missing
interpolator, iScrFX for a RwRasterLock bracket a host must not honour -- and
iDraw is a documented no-op that loses a behaviour. iFMV is left.

`iFX` and `iFMV` are the only two that are substantially GameCube, and they went
opposite ways. `iFX` was **rewritten, not copied**: its 37 GameCube lines are a
GX texture matrix loaded from four globals, so the port added the equivalent to
the librw fork -- a texture coordinate transform and a pipeline that applies it
in a vertex shader -- and iFX.cpp is now the same twenty lines of glue the
console's is, reading the same four globals into the same eight slots. That is
the pattern for a GameCube file whose GameCube part is a *hardware feature*
rather than a hardware *interface*: find the feature's counterpart, add it below
the shim, and the platform file comes out short.

`iFMV` is not getting ported at all -- the decision taken is to convert the
videos with ffmpeg and play them back with something else rather than
reimplement Bink.

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


## Open: the level-exit prompt is never focused

`BFBB_EVENT` and `BFBB_UI` traced this end to end. Recorded so the next person
starts where it stopped rather than at the beginning.

Read from the retail asset, not inferred. In `hb02.HIP`:

    574d6e87 UIFT "WARP OUTSIDE UIF"   link src 69 -> dst 16 -> fbdb3966
    fbdb3966 PORT "TOHB01"

`src 69` is `eEventPadPressR1`, `dst 16` is `eEventTeleportPlayer`. That prompt
is wired straight to the portal out of SpongeBob's house, and the four beside it
are the room-to-room warps. **The data is right and so is the port's reading of
it**: a UIFT asset is 196 bytes with `linkCount` 1, the link sits at offset 164,
and `sizeof(zUIFontAsset)` computes to exactly 164 -- so `zUIFont_Init`'s pointer
arithmetic lands on it.

Everything up to the prompt works, and each step was measured rather than
assumed:

* XInput produces `XPAD_BUTTON_R1` (`BFBB_PAD`).
* `zUI_PreUpdate` captures it and `zUIFont_Update` dispatches
  `eEventPadPressR1` -- sixteen presses, sixteen dispatches.
* Links forward and resolve, none dropped in a session.
* Triggers detect the player; `entered` transitions.
* The portal path runs end to end: entering the house reported `zSceneSwitch`
  to `'HB02'` and the new scene's objects appeared.

What stops it is the prompt's own state. `zUI` captures only when `uiFlags` has
`0x8` **and** either `0x2` or `0x1`. The asset gives it `0x34`, which has
neither. `0x8` comes from `eEventUIFocusOn`, `0x2` from `eEventUISelect`.

**Nothing sends it either.** Its id occurs exactly ONCE in `hb02.HIP` -- its own
asset header -- so no link in the scene targets it, and the only code that sends
focus or select events is `zSaveLoad` and `zBusStop`, for their own UI.

So the question is narrow: **what focuses `WARP OUTSIDE UIF` on the console?**
It cannot be a link and it is not the code found so far.

The shape of the answer is worth noting. `uiFlags 0x34` carries `0x10` and
`0x20`, which mean *become visible when focused* and *invisible when
unfocused* -- so a prompt that is VISIBLE has normally been focused. This one is
visible on screen and was never focused in any log. Something else is making it
visible, and whatever should have focused it is the missing piece. Start by
finding what makes it visible.

## Things that have already bitten, so that they do not again

### The vptr is at offset 0 on a host and it is not on the console

CodeWarrior puts a class's vptr where its first virtual function is
**declared**, not at offset zero. A host compiler puts one at offset zero
whenever there is no polymorphic base to inherit it from. For every entity
class in this game the difference is real: `xBase`, `xEnt`, `xFactoryInst` and
`xNPCBasic` declare no virtuals between them and `zNPCCommon` declares
thirty-nine, so on the console an NPC begins with its `xBase` and on a host it
begins with a vtable pointer and everything else has moved back four bytes.

A normal C++ cast handles this; the compiler applies the adjustment. Two things
defeat it, and both are everywhere in this codebase:

* **Casting through an integer.** `(xEnt*)(U32)npc` does not adjust, where
  `(xEnt*)npc` does. `zNPCCommon` hands the sound system an identity built this
  way, with a channel number in the low bits, and xSnd.cpp masks it off and
  dereferences it -- so `xSndPlayInternal` read `ent->model` out of the vptr and
  the process died in `xModelGetFrame`. Fixed by `NPC_SND_OWNER`, which casts
  first and flattens second.

* **Reading a field positionally.** `*(U32*)obj` for `obj->id` is correct on the
  console because `xBase::id` is declared first. On a host it reads the vtable
  pointer. Both of zNPCMgr's id comparators did this, so the NPC list was sorted
  by vtable address -- and since every villager shares one vtable, they compared
  equal to each other and to nothing else. `XOrdLookup` could not find an NPC,
  and `zNPCMsg_SendMsg` drops a message it cannot resolve **without saying so**,
  which is why pressing R1 next to a villager did nothing at all.

Both fixes are `#ifdef PLATFORM_PC` and leave retail's expression untouched, the
way `XFONT_CHARIDX` does in xFont.cpp.

Making `xEnt` polymorphic so it becomes the primary base was tried and does put
it back at offset zero, but it moves every `xEnt` field, puts a vptr where the
game memsets, and the vptr it creates carries `zNPCCommon`'s thirty-nine
virtuals. It also does not help the positional reads, which want `id` at offset
zero rather than `xBase`. Guarded casts at the sites are the smaller change.

**When something does nothing at all rather than doing it wrongly, suspect this
first.** A dropped lookup is silent; a bad cast usually is not.


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
