#ifndef ISYSTEM_H
#define ISYSTEM_H

#include <types.h>

#include "iTime.h"

#include <stdlib.h>

// The GameCube header also declares dlFile, dlFSUnkGlobals and
// psGetMemoryFunctions. Nothing outside src/SB/Core/gc references any of them
// -- they are the DVD filesystem shim RenderWare installs -- so the host build
// does not carry them.

// Retail reads the console's bus clock out of the OS globals at 0x800000F8 and
// divides it by four to get the timebase. Five sites in src/SB do that
// arithmetic inline instead of calling iTimeDiffSec, so this must agree with
// iTimeGet()'s tick rate or every one of them is wrong by the ratio.
#define GET_BUS_FREQUENCY() ((U32)(ITIME_TICKS_PER_SECOND * 4))

// The GameCube region/maker code from the disc header. No code outside
// src/SB/Core/gc reads it; kept so the header's contract is unchanged.
#define GET_MAKER_CODE() ((U32)0)

void iVSync();

void iSystemInit(U32 options);
void iSystemExit();

// PC-only: the size of the square raster character shadows are rendered into,
// from config.ini's video.shadow_resolution. There is no GameCube counterpart
// -- 256 was sized against a 640x480 framebuffer -- so shared code reaches it
// through src/SB/Core/x/xShadowRes.h.
//
// Default is auto, which is half the render height rounded up to a power of
// two: the ratio the console drew at, held at any render size. A power of two
// from 64 to 4096 in the file pins it instead.
//
// Pulled rather than pushed, unlike the other render settings, for two reasons:
// xShadowInit builds the shadow camera from game code at a moment iSystem does
// not control, and auto has to see the size the window actually opened at
// rather than the one config.ini asked for. `SetupShadow` still holds the
// result to no more than the render size, so this is a ceiling and not
// necessarily the number that ends up being used.
S32 iShadowResolution();

void null_func();

#endif
