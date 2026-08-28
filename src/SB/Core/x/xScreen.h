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

// The rest of the file is the UI box: the 4:3 rectangle, centred in the render
// size, that everything laid out in normalized 0..1 coordinates draws into.
// It IS the screen on a 4:3 render size and on the console, so `xScreenUIRect`
// preprocesses to the `scale(640.0f, 480.0f)` these sites have always had and
// `xScreenUIx` to the same multiply.
//
// Full-screen effects -- the fades, the letterbox bars, the safe-area frame --
// use xScreenWidthF/HeightF instead, because what they are for is covering
// everything. That split is what makes widescreen work rather than stretch.

#ifdef PLATFORM_PC

#include "iScreen.h"

#define xScreenWidth() iScreenWidth()
#define xScreenHeight() iScreenHeight()
#define xScreenWidthF() iScreenWidthF()
#define xScreenHeightF() iScreenHeightF()

#define xScreenAspectF() iScreenAspectF()
#define xScreenUIWidthF() iScreenUIWidthF()
#define xScreenUIHeightF() iScreenUIHeightF()
#define xScreenUIOriginXF() iScreenUIOriginXF()
#define xScreenUIOriginYF() iScreenUIOriginYF()
#define xScreenUIFracXF() iScreenUIFracXF()
#define xScreenUIFracYF() iScreenUIFracYF()

#define xScreenUIx(n) (iScreenUIOriginXF() + iScreenUIWidthF() * (n))
#define xScreenUIy(n) (iScreenUIOriginYF() + iScreenUIHeightF() * (n))

#define xScreenUIRect(r)                                                                      \
    ((r).scale(iScreenUIWidthF(), iScreenUIHeightF())                                         \
         .move(iScreenUIOriginXF(), iScreenUIOriginYF()))

// The HUD's anchor. Applied where a widget's normalized position becomes a
// draw, and nowhere else -- see iScreen.h for why menus are left alone.
#define xScreenAnchorX(x) iScreenAnchorX(x)
#define xScreenAnchorY(y) iScreenAnchorY(y)

// Full-bleed menu art: a UI sprite authored to cover the whole 640x480.
//
// Nothing in the asset says a sprite is a background -- it is an ordinary UI
// entity with a texture -- so the test is the only thing that distinguishes one:
// it covers the authored screen. Measured against the real menus, that is true
// of exactly the two backdrops and of nothing else; every other panel is inset
// and keeps the box it has always had.
//
// PC only, and called from inside a PLATFORM_PC branch. The console has no
// screen for art to be full-bleed against -- the box IS the screen there.
#define xScreenUIRectFullBleed(x1, y1, x2, y2)                                                \
    ((x1) <= 0.0f && (y1) <= 0.0f && (x2) >= 1.0f && (y2) >= 1.0f)

#define xScreenStretchX(n) iScreenStretchX(n)
#define xScreenStretchY(n) iScreenStretchY(n)

// Is this normalized UI-space rect entirely off the screen?
//
// One macro rather than four bounds, so that the console compiles the exact
// comparison it always did. The bounds are 0..1 only while nothing is anchored;
// once the HUD reaches past the box, the screen does too.
#define xScreenUIRectOffscreen(r)                                                             \
    ((r).x + (r).w < -iScreenAnchorMarginXF() || (r).x > 1.0f + iScreenAnchorMarginXF() || \
     (r).y + (r).h < -iScreenAnchorMarginYF() || (r).y > 1.0f + iScreenAnchorMarginYF())

// Grow a clip rect from the 0..1 box out to the whole screen.
//
// For the one thing that CLIPS rather than culls: the font, which is handed the
// box when it is built because on the console the box is all there is. A
// counter anchored out to a real screen edge is outside that rect, and would be
// cut in half by the box it used to live in.
#define xScreenWidenToScreen(r)                                                               \
    ((r).x = -iScreenAnchorMarginXF(), (r).y = -iScreenAnchorMarginYF(),                      \
     (r).w = 1.0f + 2.0f * iScreenAnchorMarginXF(),                                           \
     (r).h = 1.0f + 2.0f * iScreenAnchorMarginYF())

#else

#define xScreenWidth() 640
#define xScreenHeight() 480
#define xScreenWidthF() 640.0f
#define xScreenHeightF() 480.0f

#define xScreenAspectF() 0.75f
#define xScreenUIWidthF() 640.0f
#define xScreenUIHeightF() 480.0f
#define xScreenUIOriginXF() 0.0f
#define xScreenUIOriginYF() 0.0f
#define xScreenUIFracXF() 1.0f
#define xScreenUIFracYF() 1.0f

#define xScreenUIx(n) (640.0f * (n))
#define xScreenUIy(n) (480.0f * (n))

#define xScreenUIRect(r) ((r).scale(640.0f, 480.0f))

#define xScreenAnchorX(x) (x)
#define xScreenAnchorY(y) (y)

#define xScreenUIRectOffscreen(r)                                                             \
    ((r).x + (r).w < 0.0f || (r).x > 1.0f || (r).y + (r).h < 0.0f || (r).y > 1.0f)

#define xScreenWidenToScreen(r) ((void)0)

#endif

#endif
