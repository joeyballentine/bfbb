#ifndef XDRAWDIST_H
#define XDRAWDIST_H

// The distances past which the game stops drawing, for the code that both
// builds share.
//
// On the GameCube nothing chooses these -- the console has one budget and the
// artists spent it -- so both macros expand to their own argument and every
// call site below preprocesses to exactly the expression it used to hold. The
// console's objects are unchanged, zLOD.cpp included, which matters because
// zLOD.cpp is a Matching unit. On the PC they reach iDrawDist, which config.ini
// sets once at startup. See src/SB/Core/pc/iDrawDist.h for what the switch
// covers and, more usefully, what it leaves alone.
//
// **Why a wrapper around each distance rather than a flag to branch on.** The
// same LOD table feeds the renderer and the update manager: `zSceneSetup` takes
// `zLODTable::noRenderDist` and makes it the distance an entity stops THINKING
// at. Rewriting the table -- the obvious implementation, and one line -- would
// leave every NPC in the level running, which costs more than drawing them and
// changes when things happen. Wrapping the value at each use puts the switch on
// the render side only, and makes each site say which side it is on.

#ifdef PLATFORM_PC

#include "iDrawDist.h"

// A distance the renderer gives up at -- a no-render distance, an LOD swap
// distance, a fade -- expands to a distance nothing ever reaches when the
// switch is on. 1e38f is the same "never" sentinel zEntSimpleObj.cpp already
// uses for a table entry of zero.
//
// Every distance in these systems is stored SQUARED, and is compared against a
// squared camera distance, so 1e38f is used as it stands and nothing squares it
// again. zLOD is the one that does arithmetic on it: it takes the square root,
// adds 10 and squares that back, which lands on 1e38f again rather than on an
// infinity. `test_drawdist` in the self-test pins that round trip, because an
// infinity would still make every comparison come out false -- by accident, and
// only until someone wrote a subtraction.
#define xDrawDistCull(d) (iDrawDistUnlimited() ? 1.0e38f : (d))

// The camera far clip to build a camera with, and to fall back to when
// iCameraSetNearFarClip is handed a zero.
#define xDrawDistFarClip() iDrawDistFarClip()

#else

#define xDrawDistCull(d) (d)
#define xDrawDistFarClip() 400.0f

#endif

#endif
