# Rendering above 640x480

## Where things stand

The port renders at whatever `config.ini`'s `[video] width` and `height` say,
and opens its window at that size. The default is 640x480, which is retail's
framebuffer, so a fresh install behaves exactly as it did before this existed.

    [video]
    width = 1280
    height = 960

This document was an audit of what it would take. It is now an account of how it
works and of what raising the number does NOT fix.

## The one number

`src/SB/Core/pc/iScreen.h` holds the render size for the whole game.
`iSystem.cpp` sets it from `config.ini` before the window is opened, opens the
window at it, and then sets it again from the client area the window actually
gave -- because `rw/engine_start.cpp` takes the virtual screen from the window,
and the two must not disagree. It is fixed from that point on.

Shared code reaches it through `src/SB/Core/x/xScreen.h`, which expands to
`iScreen` on the PC and to the literals `640`, `480`, `640.0f` and `480.0f` on
the GameCube. Every call site therefore preprocesses to exactly the constant it
used to hold when built for the console, and the GameCube DOL is byte-identical
with the matched-function count unchanged. That is checked, not assumed:
`python tools/gcgate.py` after a `ninja`.

## Why the render size and the window size are independent

The port does not render into the back buffer. It renders into a **virtual
screen** -- a render target of a fixed size -- which `blitVirtualScreen`
(`third_party/librw/src/d3d/d3ddevice.cpp:1543`) stretches into the back buffer
at present time, keeping its aspect and filling the rest with black. The back
buffer follows the window; the picture does not. Set the render size above the
window and you get supersampling; below it and you get a scaler. Resizing the
window never resizes the picture.

`RwEngineGetVideoModeInfo` (`engine_start.cpp:585-611`) reports the virtual
screen as the current video mode rather than the adapter's desktop mode, which
is what stops `xScrFx` sizing its full-screen rectangles to a 4K desktop.
Everything that asks the video mode for the screen size is therefore correct at
any resolution with no change of its own:

    xScrFx.cpp     the fade to black, the death vignette, the letterbox bars,
                   the safe-area frame, the distort effect's extent
    iFMV.cpp:287   movie placement, via fitRect
    rw/distort.cpp the screen copy it samples
    rw/snapshot.cpp the loading-screen still
    xShadow.cpp:719 InvertRaster, which reads its own raster

## The hard constraint: every full-screen camera moves together

A `Raster::CAMERA` has no surface of its own. `setRenderSurfaces`
(`d3ddevice.cpp:1010`) sees a null texture and binds the **default** render
target -- the virtual screen -- and `setViewport` (`d3ddevice.cpp:1045`) then
takes the viewport from the camera raster's own width and height.

That alone would only mean a small camera draws into a corner. The part that
bites is the depth buffer. `rasterCreateZbuffer` (`d3d.cpp:499-503`) shares the
engine's default depth surface **only when the Z raster's size equals
`getScreenExtent()`**, and allocates a private surface otherwise. A depth surface
smaller than the render target is invalid in D3D9. So a camera raster that does
not match the virtual screen does not draw small -- it fails to bind depth and
draws nothing.

Every one of these is now built at the render size:

    zGame.cpp:400    xCameraInit, the main camera
    zGame.cpp:927    the autosave text camera
    zGame.cpp:1295   the screen-transition camera
    zMenu.cpp:69     the menu's camera
    zMain.cpp:945    zMainFirstScreen
    zMain.cpp:1296   the memory-card screens
    zMain.cpp:1311
    pc/iEnv.cpp:64   the JSP instancing camera
    pc/iModel.cpp:176 the model instancing camera

The last two are the ones to miss. They never draw a pixel -- they exist so
`RpAtomicInstance` has a camera to run under -- but they still call
`RwCameraBeginUpdate`, so they still bind a depth surface, and they are subject
to the same rule as the ones that do draw. **A new full-screen camera anywhere
in the game has to join this list.** The failure mode is a black screen, not a
small picture.

`xModelBucket.cpp:125` creates a 0x0 camera and is deliberately left alone:
nothing ever begins an update on it.

## The 2D layer

The UI is resolution independent in its coordinate system. `xFont` lays
everything out in 0..1 and converts to pixels in one shape, `r.scale(w, h)`,
which now takes the render size:

    xFont.cpp:494    every glyph the game draws
    xFont.cpp:3710   render_fill_rect
    zTextBox.cpp:56  the text box backdrop
    xCM.cpp:180-183  the cutscene overlay textures

Three sites are the INVERSE conversion -- an author's pixel measurement into the
normalized space -- and must stay at 640 and 480:

    xFont.h:287-296  NSCREENX/NSCREENY, 1/640 and 1/480
    xFont.cpp:3441   get_texture_size, raster.width / 640.0f
    zUI.cpp:915-918  the model branch, pos / 640 and pos / 480

`zUI.cpp:843-861` is the one that does not follow the pattern, and the only site
in the game that needs a `#ifdef PLATFORM_PC`. It set `w = 640.0f` and computed
`x1 = w * pos.x / w`, where the `w` cancels: UI sprite assets are authored in
640x480 pixels and drawn as raw pixels. The two 640s are different numbers that
happen to be equal on a 640x480 framebuffer, so only the numerator became the
screen.

Full-screen quads written directly in pixels, all now sized from the screen:

    zEntPlayerOOBState.cpp:224  the out-of-bounds fade
    xScrFx.cpp:583              the full-screen glare
    zGame.cpp:1444-1472         the screen-transition background
    zGame.cpp:966-973           zGame_HackDrawCard, four quarters of the screen
    zGame.cpp:993-1000          the saving icon, placed as a fraction of 640x480

## Quality, not correctness

**Glow.** `rw/glow.cpp` used to nail its chain to 320x240, 320x120 and 160x120 --
half and quarter of 640x480. The blur tap distances are in texels *of the texture
being sampled*, so what fixes the bloom's radius as a fraction of the screen is
the downsample RATIO, not the target's size: a chain left at those three numbers
would tighten the glow as the resolution went up. The chain is now computed from
the captured frame, and comes to the Xbox's own three sizes at 640x480.

**Distort.** `rw/distort.cpp` divides its 15-pixel amplitude by
`kReferenceWidth`/`kReferenceHeight` rather than by the screen. The 15 pixels
were measured against a 640x480 frame, so what they describe is a fraction of
the picture; dividing by the real screen would shrink the cruise bubble's warp
at higher resolutions.

**Shadows.** `xShadow.cpp:130` halves a 256-pixel shadow map while it exceeds
either screen dimension. That loop now reads the render size, so it means what
it says -- but 256 is still 256 on a 4K screen, and shadows stay as soft as they
are on a console. Raising the base is a deliberate look-and-memory change and
has not been made.

**Art.** Every HUD texture, font atlas and UI sprite is authored for 640x480 and
gets magnified, with linear filtering (`xFont.cpp:665`). Text and HUD go soft.
Nothing in the code fixes that; it needs assets.

## Widescreen

A render size whose aspect is not 4:3 is widescreen, with no switch of its own.

**The camera widens.** `iCameraSetFOV` was the only aspect-ratio assumption in
the game:

    vw.y = 0.75f * vw.x;

`fov` is the HORIZONTAL field of view -- `vw.x` is the frustum's half-width at
unit distance -- and 0.75 is 480/640, so that line is the vertical half-angle a
4:3 screen gives. It is the one to keep: the levels and the camera were designed
around how much is visible above and below. So `vw.y` stands and `vw.x` is
rebuilt from `xScreenAspectF()`, and a wider screen shows more of the world to
the left and right rather than cropping the top and bottom off what the console
showed. At 4:3 it comes to `vw.y / 0.75`, the `vw.x` it already had.

**The interface keeps its shape.** Everything laid out in normalized 0..1 draws
into the largest 4:3 rectangle that fits, centred -- the UI box, `iScreen`'s
`xScreenUIRect`, `xScreenUIx` and `xScreenUIy`. The art is authored at 640x480
and stretching it is the one outcome that cannot be undone later.

`xModelRender2D` needs the box in a different form. It places a HUD model by
shearing against the CAMERA's view window rather than in pixels, so on a
widescreen frustum a model would spread to the screen's own edges while the text
beside it stayed centred; it is given the box as a fraction of the frustum
instead.

**Full-screen effects are still full screen** -- the fades, the letterbox bars,
the safe-area frame, the out-of-bounds fade, the glare, the loading background
and the autosave smoke all take the screen size rather than the UI box. That
split is what makes this widescreen rather than a stretch, and it is the rule
for anything added later.

What is NOT done is anchoring the HUD to the true screen edges. At 16:9 the
interface sits inside the centred 4:3 box, about 12% in from each side. Pushing
the in-game HUD out to the corners would look more native and needs a per-widget
decision in `zHud` about which edge each thing belongs to, which is a design
change rather than a correction.

## Windowed, borderless and fullscreen

`[video] mode` is `fullscreen` (D3D9 exclusive, at whatever the desktop is
already using), `borderless` or `windowed`, and it is independent of the render
size -- the picture is scaled onto whatever surface it lands on.

Only exclusive fullscreen is visible to the RENDERER, because a D3D9 device is
created windowed or not and cannot change its mind without a reset. It is chosen
in `rw/engine_start.cpp` between `RwEngineOpen` and `RwEngineStart`: librw's mode
list is built inside `Engine::open` and read inside `Engine::start`, so that is
the one moment the answer can still be acted on. Borderless needs nothing from
the renderer at all -- it is a `WS_POPUP` the size of a monitor, and a window is
a window.

The process is per-monitor-DPI aware (`iWindowWin32.cpp`). Without that Windows
resizes the window itself when it crosses to a monitor at a different scaling,
which reads as a user resize, resets the device, and grows the window by the
ratio of the two scalings each time.

## What is not done

Changing resolution at RUN TIME. The virtual screen is read once, inside
`RwEngineOpen`, and deliberately never updated; changing it while the game is
live means recreating every camera raster in the table above at once. This is a
launch-time setting. A settings-menu version is a second piece of work.
