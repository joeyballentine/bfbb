#ifndef XSCREEN_H
#define XSCREEN_H

// The size the game renders at, for the code that both builds share.
//
// On the GameCube the framebuffer is 640x480, nothing chooses it, and these
// expand to the literals the sources have always had -- so every call site
// below preprocesses to exactly the constant it used to hold and the console's
// objects are unchanged. On the PC they reach iScreen, which config.ini sets
// once at startup. See src/SB/Core/pc/iScreen.h for why one number governs the
// whole game and why it cannot change while it is running.
//
// The reason this is a header rather than an #ifdef at each use: there are
// roughly twenty of them, spread over the cameras, the 2D layer and the shadow
// map, and a guard at every one would bury what the code is doing. The
// alternative was also the more dangerous one -- the failure mode for MISSING a
// camera site is a black screen, so they want to read alike.

#ifdef PLATFORM_PC

#include "iScreen.h"

#define xScreenWidth() iScreenWidth()
#define xScreenHeight() iScreenHeight()
#define xScreenWidthF() iScreenWidthF()
#define xScreenHeightF() iScreenHeightF()

#else

#define xScreenWidth() 640
#define xScreenHeight() 480
#define xScreenWidthF() 640.0f
#define xScreenHeightF() 480.0f

#endif

#endif
