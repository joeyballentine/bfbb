#ifndef ISCREEN_H
#define ISCREEN_H

#include <types.h>

// PC-only: the size the game renders at. There is no GameCube counterpart --
// a console's framebuffer is 640x480 and nothing chooses it -- so shared code
// reaches this through src/SB/Core/x/xScreen.h, which preprocesses to the
// literals on the console.
//
// **This is the render size, not the window size.** The port does not draw into
// the back buffer; it draws into a virtual screen that blitVirtualScreen
// (third_party/librw/src/d3d/d3ddevice.cpp) stretches into the back buffer at
// present time, keeping its aspect. The two are already independent -- a render
// size above the window supersamples, below it scales up -- and iSystem happens
// to open the window at the render size because that is the least surprising
// thing to do, not because anything requires it.
//
// **Why one number for the whole game.** A Raster::CAMERA has no surface of its
// own: setRenderSurfaces binds the DEFAULT render target for it, and
// rasterCreateZbuffer shares the engine's depth surface only when the Z
// raster's size equals the screen extent, allocating a private one otherwise. A
// depth surface smaller than the render target is invalid in D3D9, so a camera
// raster that does not match the virtual screen does not draw small -- it fails
// to bind depth and draws NOTHING. Every full-screen camera in the game has to
// be built at this size, including the two instancing cameras in iEnv.cpp and
// iModel.cpp that never draw a pixel but still call RwCameraBeginUpdate.
//
// **Fixed at boot, deliberately.** The virtual screen is set once, inside
// RwEngineOpen, and never updated. Changing this while the game is live would
// mean recreating every camera raster in the game at once, so nothing calls the
// setter after startup and the getters answer the same thing all run.
//
// The 4:3 assumption is NOT lifted by any of this. iCamera.cpp builds a 4:3
// frustum and every 2D layer works in a 4:3 normalized space, so a resolution
// whose aspect is not 4:3 renders the same picture stretched to fit. See
// docs/RESOLUTION.md.

// The size the game renders at. 640x480 until something says otherwise, so a
// target that never calls the setter -- rw_selftest does not -- behaves exactly
// as the port did before this existed.
S32 iScreenWidth();
S32 iScreenHeight();

// The same pair as floats, for the 2D layers that measure in pixels. Separate
// functions rather than a cast at ~20 call sites, and because the shared header
// has to be able to hand the console a float literal.
F32 iScreenWidthF();
F32 iScreenHeightF();

// Set by iSystem, from config.ini, before the window is opened -- and again
// with what the window actually gave, because engine_start takes the virtual
// screen from the window and the two must not disagree.
//
// A width or height that is not positive, or beyond what D3D9 will make a
// surface of, is reported and refused; the size already in force stands.
void iScreenSetSize(S32 width, S32 height);

#endif
