#ifndef XSHADOWRES_H
#define XSHADOWRES_H

// The size of the shadow render target, for the code that both builds share.
//
// Character shadows are drawn by rendering the caster into a square raster and
// projecting that raster onto the ground. `SetupShadow` in xShadow.cpp builds
// it, once, at 256x256 -- half the height of the console framebuffer it was
// sized against. A host draws the same shadow across many more pixels than the
// console did, and 256 is where the blocky edge comes from.
//
// The wrapper takes the console's number as its argument and expands to
// exactly that on the GameCube, so xShadow.cpp's console object is unchanged.
// On the PC it reaches config.ini, whose default scales the raster with the
// render size rather than pinning a number. See src/SB/Core/pc/iSystem.h.
//
// The halving loop that follows the call site is deliberately left in place on
// both builds: it holds the raster to no larger than the framebuffer, so a
// port rendering at 640x480 lands back on 256 whatever the setting says.

#ifdef PLATFORM_PC

#include "iSystem.h"

#define xShadowResolution(d) iShadowResolution()

#else

#define xShadowResolution(d) (d)

#endif

#endif
