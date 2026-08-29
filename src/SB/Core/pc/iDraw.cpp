#include "iDraw.h"

#include <stddef.h>

// iDraw: ported from src/SB/Core/gc/iDraw.cpp. iDrawBegin and iDrawEnd are
// empty on the console too and are copied as they stand. iDrawSetFBMSK is the
// one piece of GameCube hardware in this module, and the reason this file is
// not on VERBATIM.txt.

// FBMSK is the PlayStation 2 name, kept: the GS register that masks writes to
// the frame buffer, one bit per bit of the pixel, where a SET bit means DO NOT
// WRITE. The GameCube has no per-bit mask -- GX offers one switch for the
// colour channels and one for alpha -- so the console reduces the 32-bit mask
// to those two:
//
//     abgr >> 24     == 0    -> alpha writes ON      (GXSetAlphaUpdate(TRUE))
//                    == 255  -> alpha writes OFF
//                    otherwise: alpha update left alone. A partial alpha mask
//                    has no GX spelling, so it is ignored.
//     abgr & 0xFFFFFF == 0   -> colour writes ON     (GXSetColorUpdate(TRUE))
//                    != 0    -> colour writes OFF, however few bits are set.
//
// The port forwards to librw's COLORWRITEMASK render state, which is
// D3DRS_COLORWRITEENABLE on D3D9.
//
// Six call sites use this to make a depth-priming pass invisible --
// iDrawSetFBMSK(-1), draw, iDrawSetFBMSK(0), draw again -- so that the second
// pass sorts against a z-buffer the first one filled. Ignoring the mask makes
// that first pass paint: an extra opaque copy of the out-of-bounds player, the
// Flying Dutchman and the bubble effects.
//
// Inverted per channel rather than per word. librw's mask runs the other way
// round, a set bit meaning DO write, and xFX.cpp:678 passes an arbitrary mask
// out of the bubble parameters rather than one of the two extremes; inverting
// the word would only be right for 0 and -1.
//
// The GS mask is per byte and abgr-ordered where librw's is per channel, so a
// channel counts as masked off if any bit of its byte is set. That is what
// every caller means -- none of them masks a channel partially.
//
// Emulating this with a blend was rejected. SRCBLEND ZERO / DESTBLEND ONE
// leaves the destination untouched, but the callers set their own blend modes
// around these calls, and on D3D9 without separate alpha blending it cannot
// express the colour-off, alpha-on combination that abgr == 0x00FFFFFF asks
// for.

// Defined in rw/renderstate.cpp, which owns librw. Declared here rather than in
// a header because it is the only thing the two files pass between them, and
// the values match librw's ColorWriteMask.
void rwSetColorWriteMask(U32 mask);

#define IDRAW_WRITE_RED 1
#define IDRAW_WRITE_GREEN 2
#define IDRAW_WRITE_BLUE 4
#define IDRAW_WRITE_ALPHA 8

void iDrawSetFBMSK(U32 abgr)
{
    U32 mask = 0;

    if ((abgr & 0x000000FF) == 0) mask |= IDRAW_WRITE_RED;
    if ((abgr & 0x0000FF00) == 0) mask |= IDRAW_WRITE_GREEN;
    if ((abgr & 0x00FF0000) == 0) mask |= IDRAW_WRITE_BLUE;
    if ((abgr & 0xFF000000) == 0) mask |= IDRAW_WRITE_ALPHA;

    rwSetColorWriteMask(mask);
}

void iDrawBegin()
{
    return;
}

void iDrawEnd()
{
    return;
}
