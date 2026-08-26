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

void null_func();

#endif
