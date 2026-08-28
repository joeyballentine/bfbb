# Rendering above 640x480: what it would take

## Where things stand

The port opens its window at 640x480 (`iSystem.cpp:157`) and renders at that
size. Nothing chooses it at run time, and nothing else in the port disagrees with
it, which is the whole reason it works.

This is a much smaller job than [UNCAPPED.md](UNCAPPED.md). That one is about 250
scattered gameplay constants, each needing a judgement call about whether it is a
rate or a unit conversion. This is about twenty call sites, all of them in
rendering, none of them ambiguous. The risk is not getting a decision wrong, it
is missing one -- and the failure mode is a black screen rather than a subtle
one, for the reason two sections down.

This is a static audit. Nothing below has been built or run.

## What already works, and why

The port does not render into the back buffer. It renders into a **virtual
screen** -- a render target of a fixed size -- which `blitVirtualScreen`
(`third_party/librw/src/d3d/d3ddevice.cpp:1543`) stretches into the back buffer at
present time, keeping its aspect and filling the rest with black. The back buffer
follows the window; the picture does not.

So the render resolution and the window size are **already independent**. Set the
render size above the window and you get supersampling; below it and you get a
scaler. That part needs no work at all.

`engine_start.cpp:352-361` sets the virtual screen from the window, once, before
the D3D device is made. `RwEngineGetVideoModeInfo` (`engine_start.cpp:585-611`)
then reports the virtual screen as the current video mode rather than the
adapter's desktop mode, which is what stops `xScrFx` sizing its full-screen
rectangles to a 4K desktop.

Everything that asks the video mode for the screen size is therefore correct at
any resolution, today, with no change:

    xScrFx.cpp:171-173, 270-272    the fade to black, the death vignette
    xScrFx.cpp:340-343             the letterbox bars
    xScrFx.cpp:355-373             the safe-area frame
    xScrFx.cpp:451                 the distort effect's extent
    iFMV.cpp:287-291               movie placement, via fitRect
    rw/distort.cpp:111-122         the screen copy it samples
    rw/snapshot.cpp:138            the loading-screen still
    xShadow.cpp:718-720            InvertRaster, which reads its own raster

## The hard constraint

A `Raster::CAMERA` has no surface of its own. `setRenderSurfaces`
(`d3ddevice.cpp:1010`) sees a null texture and binds the **default** render target
-- the virtual screen -- and `setViewport` (`d3ddevice.cpp:1045`) then takes the
viewport from the camera raster's own width and height.

That alone would only mean a small camera draws into a corner. The part that bites
is the depth buffer. `rasterCreateZbuffer` (`d3d.cpp:499-503`) shares the engine's
default depth surface **only when the Z raster's size equals
`getScreenExtent()`**, and allocates a private surface otherwise. A depth surface
smaller than the render target is invalid in D3D9. So a camera raster that does
not match the virtual screen does not draw small -- it fails to bind depth and
draws nothing.

Every full-screen camera therefore has to move together:

    zGame.cpp:399         xCameraInit(&globals.camera, 640, 480)   the main camera
    zMenu.cpp:68          the same, written 0x280, 0x1e0
    zGame.cpp:1013        the autosave text camera
    zGame.cpp:1368        the screen-transition camera
    zMain.cpp:944         zMainFirstScreen
    zMain.cpp:1295, 1310  the memory-card screens
    pc/iEnv.cpp:63        the JSP instancing camera
    pc/iModel.cpp:175     the model instancing camera

The last two are the ones to miss. They never draw a pixel -- they exist so
`RpAtomicInstance` has a camera to run under -- but they still call
`RwCameraBeginUpdate`, so they still bind a depth surface, and they are subject to
the same rule as the ones that do draw.

The size itself comes from `iSystem.cpp:157-158`, where the window is opened. An
environment variable there matches the `BFBB_ASSETS` idiom the port already uses, and `engine_start.cpp:360` picks it up
for the virtual screen with no further change.

The six game-code sites need the `#ifdef PLATFORM_PC` treatment the fixed-step
loop used in `zGame.cpp`, so the GameCube build still compiles the literals it
always had and stays byte identical.

## The 2D layer

The UI is already resolution independent in its coordinate system. `xFont` lays
everything out in 0..1 and converts to pixels at exactly one place:

    r.scale(640.0f, 480.0f);

That line, or the same shape, is the entire conversion:

    xFont.cpp:493      every glyph the game draws
    xFont.cpp:3709     render_fill_rect
    zTextBox.cpp:55    the text box backdrop
    xCM.cpp:179-182    the cutscene overlay textures

`zUI.cpp:824-842` is the one that does not follow the pattern. It sets
`w = 640.0f` and then computes `x1 = w * pos.x / w`, where the `w` cancels: UI
sprite assets are authored in 640x480 pixels and drawn as raw pixels. The fix is
not to swap the constant but to put the real screen width in the numerator and
leave 640 in the denominator. `zUI.cpp:901-904`, the model branch a few lines
down, already normalises by 640/480 and is correct as it stands.

Three sites must be **left alone**, or the UI stops scaling proportionally:

    xFont.h:288-294    NSCREENX/NSCREENY, 1/640 and 1/480
    xFont.cpp:3439     get_texture_size, raster.width / 640.0f

These convert an author's pixel measurement *into* the normalized space. They are
the inverse of the conversion above, not another instance of it.

Full-screen quads written directly in pixels:

    zEntPlayerOOBState.cpp:223    the out-of-bounds fade
    xScrFx.cpp:582                the full-screen glare
    zGame.cpp:1514-1546           the screen-transition background
    zGame.cpp:1050-1053           zGame_HackDrawCard, four 320x240 quarters
    zGame.cpp:1069                a 90x90 icon at 275,350

## Scales, but does not look right

These are quality, not correctness. Nothing breaks if they are left.

**Shadows.** `xShadow.cpp:123-129` sets `res = 256` and then halves it while
`res > 640 || res > 480`. That loop derives the shadow map's size from the
display, and on a 640x480 framebuffer it has never once executed. It is the hook
the original code left for exactly this.

**Glow.** `rw/glow.cpp:43-45` nails the chain to 320x240, 320x120 and 160x120 --
half and quarter of 640x480 -- and the blur tap distances are in texels *of the
texture being sampled*. The bright pass samples the whole frame, so raising the
render size changes the downsample ratio and the bloom's radius as a fraction of
the screen shrinks. Scaling the three sizes with the screen preserves the look.

**Distort.** `rw/distort.cpp:210-211` divides the amplitude by the screen width
and height, so the cruise bubble's warp shrinks the same way for the same reason.

**Art.** Every HUD texture, font atlas and UI sprite is authored for 640x480 and
gets magnified, with linear filtering (`xFont.cpp:664`). Text and HUD go soft.
Nothing in the code fixes that; it needs assets.

## Widescreen is a separate job

`iCamera.cpp:229` is the only aspect-ratio assumption in the game:

    vw.y = 0.75f * vw.x;

Changing it to the real height over width gives a genuine widescreen frustum, one
line. But every 2D thing in the section above lives in a 4:3 normalized space, so
the UI would need its own aspect correction, and the safe-area and letterbox maths
in `xScrFx` would have to be reread against a non-4:3 screen. Raising the
resolution does not require any of it.

## One caveat for shipping it

The virtual screen is read once, at boot, and deliberately never updated
(`engine_start.cpp:352-361`). Changing resolution at run time means recreating
every camera raster in the table above while the game is live. Make it a
launch-time setting first; a settings-menu version is a second piece of work.
