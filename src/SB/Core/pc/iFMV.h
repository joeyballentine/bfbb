#ifndef IFMV_H
#define IFMV_H

#include <types.h>

#include <stddef.h>

// The GameCube build plays its full-motion video with Bink, decoding into a
// GX framebuffer through RAD's rad3d. Bink is proprietary middleware and
// cannot be redistributed, so the port does not decode Bink at all -- see
// "Distribution" in docs/PCPORT.md. That removes iFMV::InitGX/InitVI/Suspend/
// Resume, Decompress_frame, iFMVmalloc and iFMVfree, none of which anything
// outside src/SB/Core/gc calls.
//
// What survives is the one entry point game code uses. zFMV.cpp calls it with
// a filename and the buttons that skip playback, and expects back which button
// ended it.

// Returns the button that ended playback, or 0 if it ran to the end.
U32 iFMVPlay(char* filename, U32 buttons, F32 time, bool skippable, bool lockController);

#endif
