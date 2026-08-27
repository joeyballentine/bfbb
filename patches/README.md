# Local changes to third_party/librw

`third_party/librw` is a submodule of **upstream** `aap/librw`, which this
project cannot push to. Anything the port needs from librw itself therefore has
nowhere to live in the submodule's history, and a local commit there would leave
the parent repository pointing at a hash nobody else can fetch.

So the changes live here as patches, and the submodule working tree carries them
applied. **This is a stopgap, not a design.** See "The open question" below.

## Applying them

From the repository root, on a clean submodule:

    cd third_party/librw
    git apply ../../patches/*.patch

`tools/pcprogress.py` does not check for these, and neither does any gate. A
build with the submodule freshly checked out and the patches unapplied fails at
compile time rather than misbehaving at run time -- `COLORWRITEMASK` is a missing
identifier -- which is the right way round, but it is still a trap for anyone
cloning this repository for the first time.

## What each one is for

### librw-colorwritemask.patch

Adds a `COLORWRITEMASK` render state and its `ColorWriteMask` bits, implemented
on D3D9 as `D3DRS_COLORWRITEENABLE`.

The game needs it. Four sites -- `xModelBucket.cpp:559`,
`zEntPlayerOOBState.cpp:252`, `zNPCTypeDutchman.cpp:679` and `xFX.cpp:727` --
use the idiom `iDrawSetFBMSK(-1)`, draw, `iDrawSetFBMSK(0)`, draw again. The
first of those draws is meant to be **invisible**: it writes depth only, priming
the z-buffer so the second pass sorts against itself. With no way to express
that, the first pass painted, and `src/SB/Core/pc/iDraw.cpp` had to document the
loss instead of implementing the function.

`iDrawSetFBMSK` now forwards to it through `rwSetColorWriteMask` in
`rw/renderstate.cpp`. Note the inversion: the GameCube/PS2 register is a mask
where a set bit means *do not write*, and librw's is the opposite, so
`iDraw.cpp` inverts per channel rather than inverting the word -- the two
extremes would survive either treatment, but `xFX.cpp:678` passes an arbitrary
mask out of the bubble parameters and only per-channel is right for it.

Covered by `rw_selftest`, which sets the mask through the shim and reads it back
through librw.

## The open question

Three ways to carry this properly, none of them chosen yet:

1. **Fork librw** and point the submodule at the fork. Cleanest for anyone
   cloning, and it makes the changes reviewable and rebasable against upstream.
   Needs somewhere to push.
2. **Vendor librw** into the repository outright, dropping the submodule. Makes
   the tree self-contained and the changes ordinary commits, at the cost of
   pulling upstream fixes by hand.
3. **Keep patching**, and add a build step that applies them and a gate that
   fails when the submodule is unpatched.

Whichever it becomes, it should be settled before there are several of these.
