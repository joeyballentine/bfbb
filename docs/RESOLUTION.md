# Rendering above 640x480

The port renders at `config.ini`'s `[video] width` and `height`, and opens its
window at that size. The default is 640x480, retail's framebuffer.

    [video]
    width = 1280
    height = 960

`[video] profile` overrides these. `vanilla` is 640x480 with the console's HUD
and culling. `modern` is the display's shape at 1080 lines (or the display's
height if lower), with the HUD at the screen edges and no distance culling.
`custom`, the default off Android, reads `width`, `height`, `ui` and
`draw_distance` from their own lines.

This document covers how the render size reaches the game, why every
full-screen camera must share it, the 2D layer, widescreen, window modes, and
what is not done. [RENDERING.md](RENDERING.md) covers the renderer itself.

## The one number

`src/SB/Core/pc/iScreen.h` holds the render size for the whole game.
`iSystem.cpp` sets it from `config.ini` (through `ResolveVideoProfile`) before
the window opens, and opens the window at it. In windowed mode it then takes the
client size the window actually got, if that differs. Borderless and fullscreen
windows cover a monitor, so their client size is ignored. The size is fixed from
then on.

Shared code reaches it through `src/SB/Core/x/xScreen.h`. On the PC the macros
expand to `iScreen` calls. On the GameCube they expand to `640`, `480`, `640.0f`
and `480.0f`, so every call site compiles to the constant it always had and the
DOL is byte-identical. Check with `python tools/gcgate.py` after a `ninja`.

## Render size and window size are independent

Every backend renders into a virtual screen: an offscreen target of the render
size. At present it is scaled into the back buffer as the largest rectangle of
its shape that fits, centred, with black around it. The back buffer follows the
window; the picture does not.

- D3D9: `setVirtualScreen` and `blitVirtualScreen` in
  `third_party/librw/src/d3d/d3ddevice.cpp`.
- D3D11: the same functions in `d3d/d3d11device.cpp`.
- Vulkan: the scene image in `d3d/vkdevice.cpp` (`acquireScene`).
- GL3: a framebuffer object, `setVirtualScreen` and `blitVirtualScreen` in
  `gl/gl3device.cpp`.

The `OpenDevice*` functions in `src/SB/Core/pc/rw/engine_start.cpp` set the size
from `iScreen`, not from the window, before `Engine::open`.

A render size above the window supersamples. Below it, the picture is scaled
up. Resizing the window never changes the render size.

`RwEngineGetVideoModeInfo` (`engine_start.cpp`) reports the virtual screen's
size for the current mode instead of the desktop's. Code that asks the video
mode for the screen size is therefore correct at any render size:

    xScrFx.cpp        the fade, the death vignette, the letterbox bars,
                      the safe-area frame, the distort effect's extent
    iFMVPlay          movie placement, and the camera for the boot logos
    rw/distort.cpp    the screen copy it samples
    rw/snapshot.cpp   the loading-screen still
    xShadow.cpp       InvertRaster, which reads its own raster

## Every full-screen camera has the render size

This is a Direct3D 9 rule. The executable carries D3D9, so every camera has to
follow it.

On D3D9 a `Raster::CAMERA` has no surface of its own. `setRenderSurfaces` binds
the default render target, the virtual screen, and `setViewport` takes the
viewport from the camera raster's width and height. `rasterCreateZbuffer`
(`d3d/d3d.cpp`) shares the engine's depth surface only when the Z raster's size
equals `getScreenExtent()`, and allocates a private one otherwise. A depth
surface smaller than the render target is invalid in D3D9. A camera raster that
does not match the virtual screen therefore draws nothing.

D3D11, Vulkan and GL3 do not have this failure. They bind the virtual screen's
own depth and viewport for any `Raster::CAMERA`, whatever its raster size.

Cameras built at the render size:

    zGameInit                        xCameraInit, the main camera
    zGame_HackPostPortalAutoSaveDraw the autosave text camera
    zGameScreenTransitionBegin       the screen-transition camera
    zMenuInit                        the menu's camera
    zMainFirstScreen
    zMainMemCardQueryPost            the memory-card screens
    zMainMemCardRenderText
    iEnvLoad (pc/iEnv.cpp)           the JSP instancing camera
    iModelStreamRead (pc/iModel.cpp) the model instancing camera

The two instancing cameras never draw a pixel. They exist so `RpAtomicInstance`
has a camera, but they call `RwCameraBeginUpdate` and bind a depth surface, so
the rule applies. **A new full-screen camera must be built at the render
size.** On D3D9 the failure is a black screen.

`xModelBucket.cpp` creates a 0x0 camera and is left alone: nothing begins an
update on it.

## The 2D layer

`xFont` lays out in 0..1 and converts to pixels with `r.scale(w, h)`. On the PC
that conversion goes through `xScreenUIRect`, which maps into the UI box
(below):

    xFont                stop_tex_render, every glyph; render_fill_rect
    zTextBox             render_bk_tex_scale, the text box backdrop
    xCM                  xCMrender, the credits overlay textures

Three sites convert the other way, from an author's pixel measurement into
normalized space, and stay at 640 and 480:

    xFont.h              NSCREENX / NSCREENY, 1/640 and 1/480
    xFont                get_texture_size, raster.width / 640.0f
    zUI                  zUI_Render's model branch, pos / 640 and pos / 480

`zUI_Render`'s sprite branch has its own `#ifdef PLATFORM_PC`. Retail set
`w = 640.0f` and computed `x1 = w * pos.x / w`. UI sprites are authored in
640x480 pixels. The two 640s are different quantities that are equal on a
640x480 framebuffer, so only the numerator became the screen.

Full-screen quads written in pixels, sized from the screen:

    zEntPlayerOOBState render_fade, the out-of-bounds fade
    xScrFx             xScrFXFullScreenGlareRender
    zGame              zGameScreenTransitionUpdate, the transition background
    zGame              zGame_HackPostPortalAutoSaveDraw, the card quarters
                       and the saving icon (placed in the UI box)

## Resolution-dependent quality

**Glow.** The blur taps are in texels of the texture being sampled, so the
bloom's radius as a fraction of the screen depends on the downsample ratio.
`rw/glow.cpp` sizes its chain from the captured frame. At 640x480 it comes to
the Xbox's 320x240, 320x120 and 160x120.

**Distort.** `rw/distort.cpp` divides its 15-pixel amplitude by
`kReferenceWidth`/`kReferenceHeight` (640x480), not by the screen. The amplitude
is a fraction of the picture, so the warp keeps its size at any resolution.

**Shadows.** `video.shadow_resolution` sets the character shadow's raster.
`auto` is half the render height rounded up to a power of two: 256 at 480 lines,
1024 at 1080, 2048 at 2160. `SetupShadow` in `xShadow.cpp` halves it until it
fits inside the render size.

**Art.** HUD textures, the font atlas and UI sprites are authored for 640x480
and magnified with linear filtering. They go soft at high render sizes.
`[font] face` replaces the game's font with a TrueType face drawn at the render
size. The rest needs new assets.

## Widescreen

A render size that is not 4:3 is widescreen. There is no separate switch.

**The camera widens.** `iCameraSetFOV` (`src/SB/Core/x/iCamera.cpp`) computes
`vw.y = 0.75f * vw.x`. `fov` is the horizontal field of view and 0.75 is
480/640, so `vw.y` is the vertical half-angle of a 4:3 screen. The PC keeps
`vw.y` and rebuilds `vw.x` from `xScreenAspectF()`. A wider screen shows more
to the sides; nothing is cropped from the top or bottom. At 4:3, `vw.x` is
unchanged. `video.fov` adds an offset to every FOV the game asks for, measured
at 4:3.

**The interface.** `[video] ui` places it:

- `pillarbox` (default): everything laid out in 0..1 draws into the largest
  centred 4:3 rectangle, the UI box (`iScreenUIWidthF`, `iScreenUIOriginXF`,
  `xScreenUIRect`, `xScreenUIx`, `xScreenUIy`). This is what the console drew.
- `native`: HUD widgets move out to the real screen edges. Each widget is
  translated, never scaled: its authored rect (asset `loc`/`size`) puts it in
  the left, middle or right zone, and every widget in a group gets the same
  offset (`iScreenSetAnchorRect`, `xScreenAnchorX`). Culling and the font's
  clip rect widen with it (`xScreenUIRectOffscreen`, `xScreenWidenToScreen`).
  Menus, text boxes and cutscene overlays stay in the 4:3 box.

Both modes are identical at 4:3.

Full-bleed menu backdrops (sprites that cover the whole authored 640x480) are
stretched to the screen instead of boxed: `xScreenUIRectFullBleed`,
`iScreenStretchX`/`Y`. The menu's caustics model is drawn to the frustum's
edges with `iScreenSetUICover`.

`xModelRender2D` places HUD models by shearing against the camera's view window,
not in pixels. It is given the UI box as a fraction of the frustum
(`iScreenUIFracXF`/`YF`).

**Full-screen effects stay full screen.** The fades, letterbox bars, safe-area
frame, out-of-bounds fade, glare, loading background and autosave smoke take the
screen size, not the UI box. Anything added later follows the same split.

An effect sized by a tuning value in console pixels needs that value scaled.
The letterbox is the one case: `SB.INI`'s `ScrFxLetterBoxSize = 32` is in pixels
of a 640x480 frame, so `xScrFxUpdateLetterBox` multiplies it by
`xScreenHeightF() / 480`. The slide-in rate stays a fixed 100 px/s in console
pixels.

## Window modes

`[video] mode` is `fullscreen`, `borderless` or `windowed`. It is independent of
the render size; the picture is scaled onto whatever surface it lands on.

- `borderless` is a window the size of the monitor. The renderer does not know
  it is one.
- `fullscreen` is exclusive fullscreen at the desktop's resolution.
  `SelectFullscreenVideoMode` (`engine_start.cpp`) runs between `RwEngineOpen`
  and `RwEngineStart`, because librw builds its mode list in `Engine::open` and
  reads it in `Engine::start`. It picks the exclusive mode matching the desktop.
  If none matches, it says so and the game runs borderless. D3D9, D3D11 and GL3
  list exclusive modes. Vulkan reports one mode, the window.

The process is per-monitor DPI aware, set up by SDL in `iWindowSDL.cpp`.
Without it, Windows resizes the window when it crosses to a monitor with
different scaling, which reads as a user resize and grows the window each time.

## Not done

- **Changing the render size at run time.** The virtual screen is set once, in
  `RwEngineOpen`, and never updated. Changing it means rebuilding every camera
  raster listed above at once. A settings-menu version is separate work.
- **Menus, text boxes and cutscene overlays in widescreen.** They stay in the
  4:3 box in both `ui` modes.
- **High-resolution 2D art.** Needs assets.
