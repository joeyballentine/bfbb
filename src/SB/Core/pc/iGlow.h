#ifndef IGLOW_H
#define IGLOW_H

#include <types.h>

// PC-only: the Xbox release's full-screen glow -- what the community calls its
// bloom. Nothing in the shipped code names it, and it has no bucket in the
// Xbox build's own render profiler, which is why two passes over that binary
// looking for it by name found nothing. It was found by following the
// unidentified callees of xScrFxInit and xScrFxRender.
//
// **Where it hangs.** On the Xbox, xScrFxInit (va 0x152960) calls the setup at
// 0x171250 next to the cruise-bubble distortion's, and xScrFxRender (0x152a00)
// calls the pass at 0x171e50 immediately after xScrFxDistortionRender. The port
// hangs it in the same place, for the same reason.
//
// **The chain**, from that setup and pass:
//
//   1. bright pass, 640x480 -> 320x240   (shader def va 0x286698)
//   2. blur down the vertical axis, 320x240 -> 320x120
//   3. blur down the horizontal axis, 320x120 -> 160x120   (def va 0x286788)
//   4. composite over the frame, SRCBLEND SRCALPHA / DESTBLEND ONE, linear and
//      clamped, so the quarter-size buffer scales back up smoothly
//
// The three targets are made by the same render-to-texture camera factory the
// distortion uses (0x170660), at 0x370544, 0x370540 and 0x370530.
//
// **The kernel is exact, not chosen.** The Xbox hands its blur helper two
// tables, at va 0x286878 and 0x2868a8, each four entries of weight and offset:
//
//   vertical    1/3 at (0,+1)  1/6 at (0,+3)  1/3 at (0,-1)  1/6 at (0,-3)
//   horizontal  1/3 at (+1,0)  1/6 at (+3,0)  1/3 at (-1,0)  1/6 at (-3,0)
//
// Weights summing to one, offsets in texels of whatever is being sampled.
//
// **Why it reads as a glow over everything rather than on bright things.** The
// threshold is mid grey -- the bright pass is `2c - 1` clamped -- and this game
// is a bright, saturated cartoon, so most of the frame clears it. The same code
// in a darker game would look like selective bloom.
//
// On by default. It changes how the whole game looks, which is a larger
// divergence than anything else in this layer, but it is what the Xbox release
// does and the port follows it. `xbox.glow` in config.ini turns it off, which
// leaves the frame exactly as the GameCube release draws it.

// Draw the glow over the frame. Called from xScrFxDistortionRender's neighbour
// in xScrFxRender, with the camera being rendered; its update is ended and
// begun again inside, because the passes render into their own cameras.
//
// `strength` is the scene's, 0 to 1. It becomes the composite quad's vertex
// alpha, which is where the Xbox puts it -- `(int)(strength * 255)` at va
// 0x172148, against a SRCALPHA blend. At zero nothing is drawn at all, which
// is not an optimisation: five scenes ask for exactly that.
void iGlowRender(RwCamera* cam, F32 strength);

// Whether iGlowRender does anything. Pushed down from iSystem.cpp once, at
// startup, rather than read here from iConfig: this file compiles into
// bfbb_rw, which does not link the platform layer and must not start -- the
// RenderWare shim's own test links bfbb_rw alone, and keeping that possible is
// what stops the shim growing a dependency on the game's settings. The default
// is on, so a caller that never sets it gets the Xbox behaviour.
void iGlowSetEnabled(S32 enabled);

#endif
