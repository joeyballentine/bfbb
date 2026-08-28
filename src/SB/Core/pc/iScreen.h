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
// **Widescreen follows from the size, with no switch of its own.** A render
// size whose aspect is not 4:3 is a request for a wider (or taller) view, so:
//
//   - The 3D frustum keeps its VERTICAL field of view and widens horizontally
//     -- more world to the left and right, not less above and below. iCamera
//     builds it from iScreenAspectF.
//   - The 2D layer keeps its 4:3 shape and is CENTRED. Everything the game
//     draws in normalized 0..1 coordinates -- text, the HUD, menus, cutscene
//     overlays -- lands in the UI box below rather than being stretched to the
//     screen. The art is authored at 640x480 and stretching it is the one
//     outcome that cannot be undone later.
//   - Full-screen effects are still full screen: the fades, the letterbox bars
//     and the safe-area frame take the screen size, not the UI box, because
//     what they are for is covering everything.
//
// See docs/RESOLUTION.md.

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

// The frustum's half-height over its half-width. 0.75 on a 4:3 screen, which is
// the constant retail's iCameraSetFOV has written into it.
F32 iScreenAspectF();

// The UI box: the largest 4:3 rectangle that fits in the render size, centred.
// On a 4:3 screen it IS the screen -- width and height are the render size and
// both origins are zero -- so nothing moves at the default.
F32 iScreenUIWidthF();
F32 iScreenUIHeightF();
F32 iScreenUIOriginXF();
F32 iScreenUIOriginYF();

// The same box as a fraction of the screen, per axis. Exactly one of the two is
// 1.0, and both are on a 4:3 screen.
//
// This is the form xModelRender2D needs. It places a model by shearing against
// the CAMERA's view window rather than in pixels, so what it has to be told is
// how much of the frustum the UI box covers -- the pixel origin and size above
// cannot answer that.
F32 iScreenUIFracXF();
F32 iScreenUIFracYF();

// How the interface is placed on a screen that is not 4:3.
//
//   PILLARBOX  everything in the centred 4:3 box. Nothing moves relative to
//              anything else; the interface simply sits in the middle with
//              black either side of it. This is the default, and it is the
//              only mode that is exactly what the console drew.
//   NATIVE     each HUD widget is anchored: the group it belongs to is carried
//              out to the edge of the real screen, so a counter authored near
//              the left edge ends up near the real left edge. Menus, textboxes
//              and cutscene overlays stay in the 4:3 box -- they are
//              full-screen art, and there is nothing in them to anchor.
//
// The two are IDENTICAL on a 4:3 render size, where the box is the screen and
// the anchor is the identity, so this only ever means anything in widescreen.
enum iScreenUIMode
{
    iSCREENUI_PILLARBOX,
    iSCREENUI_NATIVE
};

void iScreenSetUIMode(iScreenUIMode mode);

// The anchor is a TRANSLATION, and this is the widget it currently translates.
//
// The rect is the one the widget was AUTHORED at -- asset loc and size, not the
// live position, which slides around as widgets show and hide. Two things come
// out of that choice. A widget animating across the screen keeps whatever
// motion the artist gave it, because its offset does not change while it moves;
// and, more importantly, an icon and the number beside it get the SAME offset,
// so they stay the distance apart they were drawn.
//
// That is the whole reason this is not the one-liner it looks like it should
// be. Reading a position as a fraction of the real screen -- x / frac - margin,
// the obvious mapping -- moves the centre of a group correctly and pulls the
// group apart, because the gap between two widgets is a distance and gets
// scaled along with everything else. On a 16:9 screen that is a third again on
// every gap, which is a counter no longer touching its icon.
//
// So the offset is quantized: a widget belongs to the left edge, to the right
// edge, or to the middle, and every member of a group lands in the same one.
void iScreenSetAnchorRect(F32 x, F32 y, F32 w, F32 h);

// A normalized UI-space coordinate, translated by the offset the rect above
// asked for -- still in UI space, so the pillarbox mapping that follows lands
// it on the screen.
//
// Identity in PILLARBOX, and identity at 4:3 whatever the mode, where the
// margin is zero because the box IS the screen.
F32 iScreenAnchorX(F32 x);
F32 iScreenAnchorY(F32 y);

// How far outside the 0..1 box the screen reaches, in UI units. Zero when the
// box is the screen. What this is for is culling: xModelRender2D throws away a
// rect outside 0..1, which is the screen only while nothing is anchored.
F32 iScreenUIMarginXF();
F32 iScreenUIMarginYF();

// The same distance, but only as far as the anchor will actually reach: zero in
// PILLARBOX, where nothing moves and the box IS everything the HUD may touch.
//
// This is the one the culling and the clipping want. Using the geometric margin
// there would let a widget that slides in from off the box become visible in
// the pillar bars in a mode whose whole promise is that it draws what the
// console drew.
F32 iScreenAnchorMarginXF();
F32 iScreenAnchorMarginYF();

// Set by iSystem, from config.ini, before the window is opened -- and again
// with what the window actually gave, because engine_start takes the virtual
// screen from the window and the two must not disagree.
//
// A width or height that is not positive, or beyond what D3D9 will make a
// surface of, is reported and refused; the size already in force stands.
void iScreenSetSize(S32 width, S32 height);

#endif
