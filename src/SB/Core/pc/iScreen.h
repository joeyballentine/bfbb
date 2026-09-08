#ifndef ISCREEN_H
#define ISCREEN_H

#include <types.h>

// PC-only: the size the game renders at. There is no GameCube counterpart --
// a console's framebuffer is 640x480 and nothing chooses it -- so shared code
// reaches this through src/SB/Core/x/xScreen.h, which preprocesses to the
// literals on the console.
//
// This is the render size, not the window size. The port draws into a virtual
// screen that blitVirtualScreen (third_party/librw/src/d3d/d3ddevice.cpp)
// stretches into the back buffer at present time, keeping its aspect. The two
// are independent: a render size above the window supersamples, below it scales
// up. iSystem opens the window at the render size because that is the least
// surprising thing to do, not because anything requires it.
//
// One number serves the whole game because a Raster::CAMERA has no surface of
// its own. setRenderSurfaces binds the default render target for it, and
// rasterCreateZbuffer shares the engine's depth surface only when the Z
// raster's size equals the screen extent, allocating a private one otherwise. A
// depth surface smaller than the render target is invalid in D3D9, so a camera
// raster that does not match the virtual screen does not draw small -- it fails
// to bind depth and draws nothing. Every full-screen camera has to be built at
// this size, including the two instancing cameras in iEnv.cpp and iModel.cpp
// that never draw a pixel but still call RwCameraBeginUpdate.
//
// It is fixed at boot. The virtual screen is set once, inside RwEngineOpen, and
// changing it while the game is live would mean recreating every camera raster
// at once, so nothing calls the setter after startup.
//
// Widescreen follows from the size, with no switch of its own. A render size
// whose aspect is not 4:3 is a request for a wider or taller view:
//
//   - The 3D frustum keeps its vertical field of view and widens horizontally
//     -- more world to the left and right, not less above and below. iCamera
//     builds it from iScreenAspectF.
//   - The 2D layer keeps its 4:3 shape and is centred. Everything drawn in
//     normalized 0..1 coordinates -- text, the HUD, menus, cutscene overlays --
//     lands in the UI box below rather than being stretched. The art is
//     authored at 640x480, and stretching it is the one outcome that cannot be
//     undone later.
//   - Full-screen effects are still full screen: the fades, the letterbox bars
//     and the safe-area frame take the screen size, not the UI box.
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

// Degrees to add to every field of view the game asks for, config.ini's
// video.fov less the 75 the game is built around. Zero unless something says
// otherwise.
//
// An offset rather than a value because the game varies the FOV constantly --
// cutscene cameras carry their own, the Cruise Bubble zooms, zCamera lerps
// between two -- and replacing the number would flatten all of that into one
// angle. It is applied at iCameraSetFOV, the one place a frustum is built, so
// xCameraGetFOV still reads back what the game asked for and the camera logic
// that measures against it is unchanged.
F32 iScreenFOVOffset();
void iScreenSetFOV(F32 degrees);

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

// Where a normalized coordinate lands when the thing holding it is meant to
// fill the screen rather than sit in the box -- full-bleed menu art, authored
// to cover the whole 640x480.
//
// The box mapping is wrong for those: it leaves the art in the middle with the
// widened 3D menu scene showing beside it, which is the one place the pillar
// bars are not black. This stretches instead: the authored screen is mapped
// onto the real one, both axes independently.
//
// Stretching rather than scaling uniformly and cropping is deliberate, and it
// is a judgement about what the art IS. The full-bleed layer in these menus is
// the caustics -- a drifting pattern of underwater light with no subject, no
// horizon and no edge that has to stay put. Widening it by a third is invisible
// on a pattern like that, while cropping an eighth off the top and the bottom
// would throw away light that is meant to reach the corners.
//
// Identity at 4:3, where the box already is the screen; and in PILLARBOX these
// are the box mapping exactly, because that mode's whole promise is the frame
// the console drew.
F32 iScreenStretchX(F32 n);
F32 iScreenStretchY(F32 n);

// The next UI model is a full-screen overlay, not an object placed in the box.
//
// xModelRender2D shrinks the camera's view window to the 4:3 box so that a HUD
// model lands beside the HUD text it belongs to. That is right for everything
// placed IN the interface and wrong for the one thing drawn OVER it: the menu's
// caustics, an additive quad whose job is to put moving light on the whole
// picture. Shrunk to the box it lights the middle of a widescreen menu and
// stops, with a visible edge where the water simply ends.
//
// Set around such a draw, so the model reaches the frustum's own edges. Note it
// covers rather than stretches -- xModelRender2D takes one scale from the
// rect's width and applies it to both axes -- which for a drifting pattern with
// no subject is a difference nobody can see.
void iScreenSetUICover(S32 on);
S32 iScreenUICover();

// Set by iSystem, from config.ini, before the window is opened -- and again
// with what the window actually gave, because engine_start takes the virtual
// screen from the window and the two must not disagree.
//
// A width or height that is not positive, or beyond what D3D9 will make a
// surface of, is reported and refused; the size already in force stands.
void iScreenSetSize(S32 width, S32 height);

// Samples per pixel the frame is rendered with. 1 is off.
S32 iScreenMultiSample();
void iScreenSetMultiSample(S32 samples);

S32 iScreenPerPixelLighting();
void iScreenSetPerPixelLighting(S32 on);

// Which of the two D3D9 paths draws.
//
//   SHADER  vertex and pixel shaders, Shader Model 2.0. Everything the port
//           adds -- the glow, the distortion, per-pixel lighting, the shadow
//           map -- is a shader and exists only here.
//   FIXED   D3D9's own transform, lighting, texture stages and fog. The bar
//           this lowers the port to is DX7-class hardware T&L, which is the
//           generation the game shipped on. It is a look-alike, not a match;
//           librw's d3d9ff.cpp lists where the two disagree.
//   AUTO    fixed only where the adapter cannot run ps_2_0. Resolved to one of
//           the two above before the device is made, so nothing downstream
//           ever sees AUTO.
//
// D3D9 only. The GL3 and D3D11 backends have no fixed function to fall back
// to, and asking for one there is reported and ignored.
enum iScreenPipeline
{
    iSCREENPIPE_AUTO,
    iSCREENPIPE_SHADER,
    iSCREENPIPE_FIXED
};

iScreenPipeline iScreenGetPipeline();
void iScreenSetPipeline(iScreenPipeline pipeline);

// Which render backend draws.
//
// The executable carries every backend BFBB_RENDER_BACKENDS asked for, and one
// of them opens the device. RW_D3D9, RW_D3D11 and RW_GL3 say which are LINKED
// and are still what guards each backend's own code; this says which is
// RUNNING, and is what the parts written against more than one read.
//
//   D3D9   Direct3D 9. Windows only, and the only backend with the
//          fixed-function path above.
//   D3D11  Direct3D 11. Windows only, and it cannot be in the same build as
//          D3D9 -- the two are one namespace in librw.
//   GL3    OpenGL 3.3. The only backend that runs off Windows.
//   NULL   No device at all, which is what a build with no render backend
//          resolves to. Headless, for compiling and for the self-tests -- NOT
//          something video.backend offers, because librw's null driver asserts
//          the first time anything asks it for a raster.
//
// AUTO takes the first one the build has, in the order listed here. It is
// resolved in RenderWareInit -- which is the only place that knows what was
// linked -- so nothing downstream ever sees AUTO, exactly as with the pipeline
// above.
enum iScreenBackend
{
    iSCREENBACKEND_AUTO,
    iSCREENBACKEND_D3D9,
    iSCREENBACKEND_D3D11,
    iSCREENBACKEND_GL3,
    iSCREENBACKEND_NULL
};

iScreenBackend iScreenGetBackend();
void iScreenSetBackend(iScreenBackend backend);

// For the messages. "auto" before RenderWareInit has resolved it.
const char* iScreenBackendName(iScreenBackend backend);

// Light the world geometry at run time instead of reading the colour baked
// into its vertices.
//
// **Groundwork, and off is the better setting on the shipped levels.** Their
// lighting was painted vertex by vertex, and some of it exists ONLY as paint:
// bb01's building shadows are ordinary rock-textured ground darkened by hand,
// so lighting that ground fresh turns its shadows back into bright rock. A rig
// fitted to the average cannot know about anything local, and dropping the
// prelight throws all of it away. This is here for levels authored to be lit,
// and for the work that needs a world whose light can move -- a shadow the
// world casts on itself, a sun that travels.
//
// Off is what the consoles did and what the artists shipped. Otherwise
// iEnvNormals generates the normals the level never stored -- two thirds of
// them ship none -- and zScene enables a rig over the world, so its lighting is
// computed rather than looked up, and so able to move and to be occluded.
//
// Which rig is what the two modes choose between:
//
// AUTO takes the level's own bspLightKit where there is one. That is the kit
// the artists authored FOR THE WORLD, and it has never been rendered by
// anything: zScene loads it into xEnv::lightKit and no code path has ever
// enabled it. 17 of the 55 levels carry one.
//
// BAKE always uses the reconstruction instead -- an ambient and four
// directionals fitted to the baked vertex colour, per channel. It is the only
// option on the other 38 levels, and on the 17 it is what the authored kit is
// worth comparing against.
//
// A level whose bake cannot be fit keeps its paint either way; the prelight is
// only dropped when there is something to replace it with. See
// iEnv::prelightDropped and iEnvNormals.h.
enum iWorldLightMode
{
    IWORLDLIGHT_OFF,
    IWORLDLIGHT_AUTO,
    IWORLDLIGHT_BAKE
};

S32 iScreenWorldLighting();
void iScreenSetWorldLighting(S32 mode);

// How far apart to pull the two ends of the FIT, as a multiple.
//
// The authored kit is left alone -- its lights are the artists' numbers and
// there is nothing in them to scale against.
//
// 1.0 is the rig as measured, and it holds the level's average brightness to
// within a thousandth: bb01's bake averages 0.643 and the rig renders 0.642.
// Above 1.0 the directionals are scaled and the ambient is taken down by what
// they gain on the average vertex, so the average stays put while the lit and
// shaded ends separate.
//
// **The useful range ends around 1.5**, and both reasons are measured on bb01.
// The ambient runs out at a swing of 1.54 and clamps at zero, after which
// nothing is left to hold the average down and the level simply gets brighter.
// The lit end saturates as well: 0.4% of the level's vertices clip at 1.0, 5%
// at 1.5, and 45% at 2.0. Past that a level goes flatter and paler rather than
// more contrasty.
F32 iScreenWorldLightContrast();
void iScreenSetWorldLightContrast(F32 contrast);

#endif
