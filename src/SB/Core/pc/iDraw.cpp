#include "iDraw.h"

#include <stddef.h>

// iDraw: ported from src/SB/Core/gc/iDraw.cpp.
//
// Three functions. Two of them -- iDrawBegin and iDrawEnd -- are empty on the
// console as well, and are copied as they stand. The third is the one piece of
// real GameCube hardware in this module, and it is the reason this file is not
// on VERBATIM.txt.
//
// This is the FIRST module in the port where the honest answer is "the host
// cannot do this yet", rather than "the host does this a different way". Read
// the comment on iDrawSetFBMSK before assuming a rendering bug is elsewhere.

// The PlayStation 2 name, kept: FBMSK is the GS register that masks writes to
// the frame buffer, one bit per bit of the pixel, and a SET bit means DO NOT
// WRITE. The GameCube has no per-bit mask -- GX offers only two switches, one
// for the colour channels and one for alpha -- so the console reduces the
// 32-bit mask to those two:
//
//     abgr >> 24     == 0    -> alpha writes ON      (GXSetAlphaUpdate(TRUE))
//                    == 255  -> alpha writes OFF
//                    otherwise: alpha update LEFT ALONE. Not a mistake; a
//                    partial alpha mask has no GX spelling, so it is ignored.
//     abgr & 0xFFFFFF == 0   -> colour writes ON     (GXSetColorUpdate(TRUE))
//                    != 0    -> colour writes OFF, however few bits are set.
//
// **On PC this is a no-op, and that is visible.** Neither RenderWare's portable
// render state vector nor librw's extension of it has a colour or alpha write
// mask: rwcore.h declares no such rwRENDERSTATE (the game never used a portable
// spelling because there was not one), and librw's rw::RenderState -- see
// third_party/librw/src/rwrender.h -- stops at stencil and alpha test. There is
// nothing to forward to, so nothing is forwarded.
//
// What that costs, at the six call sites that reach it:
//
//   xModelBucket.cpp:559, zEntPlayerOOBState.cpp:252, zNPCTypeDutchman.cpp:679
//   and xFX.cpp:727 all use the same idiom -- iDrawSetFBMSK(-1), draw, then
//   iDrawSetFBMSK(0), draw again. The first draw is meant to be INVISIBLE: it
//   writes only depth, priming the z-buffer so the second pass sorts against
//   itself. With the mask ignored, that first pass paints. Expect the
//   out-of-bounds "ghost" player, the Flying Dutchman and the bubble effects to
//   render one extra opaque copy of themselves over the correct one.
//
//   xFX.cpp:678 passes an arbitrary mask out of the bubble parameters
//   (bp->pass1_fbmsk) rather than one of the two extremes, so its consequence
//   depends on the data.
//
// Emulating it with a blend -- SRCBLEND ZERO, DESTBLEND ONE leaves the
// destination untouched -- was considered and rejected: the callers set blend
// modes around these calls and this would silently overwrite theirs, and on
// D3D9 without separate alpha blending it cannot express the colour-off,
// alpha-on combination that abgr == 0x00FFFFFF asks for. Guessing wrong here is
// worse than doing nothing visibly, because a wrong blend looks like a shader
// bug and a missing mask looks like exactly what it is.
//
// The fix is a colour write mask in librw (D3DRS_COLORWRITEENABLE on D3D9,
// glColorMask on GL3) surfaced as an rw::RenderState, then this function
// forwards to it. That is a change to third_party/librw and to the shim's
// renderstate.cpp, not to this file.
void iDrawSetFBMSK(U32 abgr)
{
    (void)abgr;
}

void iDrawBegin()
{
    return;
}

void iDrawEnd()
{
    return;
}
