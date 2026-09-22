# The renderer

How the PC port draws, what the game's assets give it to work with, which
optional effects exist, the fixed-function Direct3D 9 mode, and what is not
built. [RESOLUTION.md](RESOLUTION.md) covers render size, widescreen and the
window.

## Backends

One executable carries several render backends. `[video] backend` picks one at
startup: `auto`, `d3d9`, `d3d11`, `gl3` or `vulkan`.

| Backend | Window | Shaders | Notes |
|---|---|---|---|
| Direct3D 9 | SDL window, created by the port | HLSL, SM 2.0 (SM 3.0 for toon) | The only backend with a fixed-function path. |
| Direct3D 11 | SDL window, created by the port | HLSL, SM 4.0 (feature level 10_0) | Same pipelines as D3D9. |
| Vulkan 1.3 | SDL window, created by the port | HLSL compiled to SPIR-V | Same pipelines as D3D9. Not in the default build. |
| OpenGL 3.3 | SDL window, created by librw | GLSL | Falls back through 2.1, GLES 3.1 and GLES 2.0. The backend off Windows. |

- `BFBB_RENDER_BACKENDS` (CMake) lists what the build carries. The default is
  `D3D9;D3D11;GL3` on Windows and `GL3` elsewhere. `VULKAN` has to be added.
  `NULL` alone is the headless build the self-tests run in.
- `auto` takes the first backend the build has, in the order D3D9, D3D11, GL3,
  Vulkan. A named backend the build lacks falls back to the first one it has,
  with a message. `iBackendResolve` in `src/SB/Core/pc/rw/backend.cpp` does
  this before the window opens.
- librw is our fork: `third_party/librw`, `joeyballentine/librw`, branch
  `bfbb-port`. Changing it is normal practice here.
- D3D11 and Vulkan are implementations of librw's `rw::d3d` device. All three
  answer to `PLATFORM_D3D9`, read the same native data and register the same
  pipelines. `rw::d3d::useD3D11` and `rw::d3d::useVulkan` pick the device.
- Port code tests the running backend with `iBackendIsD3D9`, `iBackendIsD3D11`,
  `iBackendIsVulkan`, `iBackendIsGL3` (`rw/backend.h`). `iBackendIsD3D` covers
  both Direct3Ds and not Vulkan. The `RW_*` defines only say what compiles.

## Shaders

**D3D9, D3D11 and Vulkan share one HLSL source per shader**, in
`third_party/librw/src/d3d/shaders/`. `make_shaders.cmd` there compiles each one
three times:

- SM 2.0 or 3.0 into `shaders/`, for D3D9 (fxc).
- SM 4.0 with `SM4` defined into `shaders11/`, for D3D11 (fxc).
- SPIR-V from the SM 4.0 source into `shadersvk/`, for Vulkan (dxc, then
  `spirv_h.py`).

`shaders/rwshader.h` holds everything that differs between SM 2/3 and SM 4:
texture and sampler declarations, the alpha test (a `clip` against `c7` at SM 4),
integer constants, and blend indices. Constant registers are the same in both.
The compiled blobs are checked in, so a build needs no shader compiler.

The port's own pass shaders (glow, distort) follow the same scheme in
`src/SB/Core/pc/rw/shaders/make_shaders.cmd`, which includes librw's
`rwshader.h`.

**GL3 has its own GLSL**, in `third_party/librw/src/gl/shaders/` and
`src/SB/Core/pc/rw/shadersgl/`. The `.inc` files are generated from the
`.vert` and `.frag` sources.

**The hardware floor on D3D9 is Shader Model 2.0.** Everything is `vs_2_0` and
`ps_2_0` except the two toon pixel shaders, `default_toon_PS` and
`default_tex_toon_PS`, which are `ps_3_0` because they need `ddx`/`ddy`.

Lighting is per vertex unless `video.per_pixel_lighting` is on:
`default_VS.hlsl` and `skin_VS.hlsl` sum ambient, directional, point and spot
lights. Fog is per vertex on every backend: `default_VS.hlsl` writes the factor
into `TexCoord0.z` and `default_PS.hlsl` lerps by it; GL3's `default.vert`
writes `v_fog`.

## Everything renders offscreen

Every backend draws the frame into a render target of the render size, the
virtual screen, and scales it into the window at present with black bars.

- D3D9: `setVirtualScreen` and `blitVirtualScreen` in `d3d/d3ddevice.cpp`.
- D3D11: the same names in `d3d/d3d11device.cpp`, presented through
  `blit_VS`/`blit_PS`.
- Vulkan: the scene image, `acquireScene` in `d3d/vkdevice.cpp`.
- GL3: a framebuffer object, `setVirtualScreen` and `blitVirtualScreen` in
  `gl/gl3device.cpp`.

The `OpenDevice*` functions in `src/SB/Core/pc/rw/engine_start.cpp` set the size
and sample count before `Engine::open`. MSAA lives on this surface: a
multisampled target, resolved at present.

Other offscreen targets:

- **Camera textures.** A `Raster::CAMERATEXTURE` camera draws into its own
  colour surface. D3D11 and Vulkan pair it with its Z raster only when the two
  are the same size. A camera with no Z raster is legal on every backend; the
  glow chain's cameras have none.
- **The character shadow.** `xShadow.cpp` renders the caster into a camera
  texture and projects it onto receivers. Its size is
  `video.shadow_resolution`: `auto` is half the render height rounded up to a
  power of two (480 lines gives retail's 256), or a power of two from 64 to
  4096. `SetupShadow` halves it until it fits inside the render size.
- **Full-screen cameras** (`Raster::CAMERA`) have no surface. They draw into the
  virtual screen, and on D3D9 their Z raster has to match its size.
  [RESOLUTION.md](RESOLUTION.md) has the list and the reason.
- **GL3 depth as a texture.** `rw::gl3::bindVirtualScreenDepth` copies the
  virtual screen's depth into a texture and binds it to a stage. Nothing calls
  it.

## Screen passes

`src/SB/Core/pc/rw/glow.cpp`, `distort.cpp` and `snapshot.cpp`. Each copies the
virtual screen into a texture and draws 2D quads over it with its own pixel
shader. One file per pass, with an arm per backend chosen by `iBackendIs*`. All
four backends have all three.

| Pass | Setting | What it is |
|---|---|---|
| Glow | `xbox.glow` | The Xbox bloom: bright pass, two four-tap blurs, additive composite. The chain is sized from the captured frame; `iGlow.h` has the numbers. |
| Distort | `xbox.distortion` | The Cruise Bubble screen warp. |
| Snapshot | `xbox.snapshot` | Keeps the last presented frame for the loading screen. |

A new pass is a copy of one of these files. Under the D3D9 fixed-function path,
glow and distort report themselves off; the snapshot is a surface copy and
still works.

## What the assets are

Measured across all 55 level `.HOP` files, bounded to each asset's extent from
the file's own `AHDR` dictionary.

**The world is baked vertex colour, all of it.** 1,313,413 world vertices
across two `JSP ` assets per level, 100% carrying prelit colour. Clearing it
leaves the world flat-shaded.

**Only 36.4% of world vertices carry normals, all-or-nothing per level: 34 of
55 levels have none.** Levels with normals include b301-b303, hb01-hb06,
hb08-hb10, rb01-rb03, gy01, gy03, jf02, sm01, bb02 and bc03. jf01, gl01-gl03,
kf01, kf02, kf04, kf05, hb00, hb07, db01-db06 and the rest have none.
`iEnvGenerateNormals` (`src/SB/Core/pc/iEnvNormals.cpp`) generates them at load
under `experimental.world_lighting`. `iEnvNormalsCompare` checks the generator
against the 21 levels that ship normals.

**Every light kit is directional-only.** 34 distinct `LKIT` assets, each 4 to 8
directionals plus at most one ambient. No point or spot light ships. The only
point light the game makes is `zDiscoFloor.cpp`'s, at run time.

**No placed dynamic lights ship.** Zero `LITE` assets, so `zLight.cpp`'s
flicker, strobe, dim and cauldron code has no data.

**17 of 55 levels ship a `bspLightKit`** in their ENV asset, a light rig
authored for the world. `zSceneSetup` loads it into `xEnv::lightKit`. Retail
never enables it. `experimental.world_lighting = on` does: `zWorldLightKit` in
`zScene.cpp` uses it where the level has one.

To repeat the measurement: scan asset extents, not from the `JSP\0` marker. A
HOP holds ~167 `MODL` assets beside its two JSPs, and the JSP's clump comes
before its `JSP\0` header, which follows the `0xBEEF01` collision block.
Scanning marker-to-EOF measures the models. `AHDR` fields are big-endian:
`assetID, type(4cc), offset, size, plus, flags`, then an `ADBG` sub-chunk with
the name.

## Shipped options

### MSAA

`video.msaa = 1 | 2 | 4 | 8`, all four backends. The virtual screen is
multisampled and resolved at present. A count the device refuses falls back to
off. D3D9 and D3D11 turn multisampling off for 2D quads, whose edges are placed
in pixels.

### Keyed alpha cutout

Not a setting. A texture classified as keyed when its raster is read (almost
no alpha values between 0 and 255) is alpha-tested instead of blended when the draw writes depth,
has no vertex alpha and is not a 2D quad. The reference moves to the middle of
the alpha ramp when the game left it at 1. This is `updateAlphaStates` in each
backend (`d3ddevice.cpp`, `d3d11state.cpp`, `vkpipeline.cpp`,
`gl3device.cpp`).

It stops a magnified cutout edge from blending while writing depth, which showed
the sky through walls. Alpha-to-coverage and `video.alpha_to_coverage` are
gone. Cutout edges are hard at every MSAA count.

### Supersampling

A render size above the display size is downsampled at present. There is no
separate `render_scale`; `video.width` and `video.height` are the render size.

### Per-pixel lighting

`video.per_pixel_lighting`, off by default. All four backends (`default_pp_VS`,
`default_pp_PS`, `skin_pp_VS` in HLSL; `lighting.frag` on GL3).

- Moves ambient and directional lighting into the pixel shader for the default,
  UV-transform and skin pipelines, and for matfx meshes that carry no effect.
  Environment-mapped draws stay per vertex.
- A draw lit by a point or spot light falls back to per vertex. In practice that
  is the disco floor.
- Fits `ps_2_0`: eight unrolled directionals plus ambient. Unused slots are
  uploaded as zero, so one shader covers every light count.
- D3D9 uploads a second set of constants to the pixel stage, starting at `c8`.
  GL3 needs none: a uniform declared in both stages is one uniform in the linked
  program.

### Toon

`experimental.toon` and the `toon_*` keys under it. All four backends; D3D9
needs `ps_3_0`.

- Lighting looked up in a ramp texture of bands (`iToon.cpp` generates it; rows
  for characters, metal, world and props), with saturation pushed.
- An inverted-hull outline (`outline_VS`, `skin_outline_VS`, `outline_PS`;
  GL3 `outline.frag`).
- Forces the per-pixel path for the draws it shades.
- `world_outline` draws the level a second time for its outline.

### World lighting

`experimental.world_lighting = off | on | bake`. Replaces the world's baked
colour with lights computed at run time.

- Generates the missing normals, then drops the prelight
  (`iEnvDropPrelight`) where there is a rig to replace it.
- `on` uses the level's `bspLightKit` where it has one, and the fitted rig
  otherwise. `bake` always uses the fit: an ambient and four directionals
  fitted per channel to the baked colour.
- Objects are lit by the same rig as the world (`zObjectLightKit`).
- `world_light_contrast` spreads the fitted rig. `world_light_shadows` traces
  each vertex against the collision tree at load and makes the lighting static.
  `day_night_cycle` rotates the sun. `world_model_shade` needs toon.

## The fixed-function mode

`video.pipeline = auto | shader | fixed`, D3D9 only. The other backends ignore
the setting. The path is `third_party/librw/src/d3d/d3d9ff.cpp`. It draws the
world, static models, characters, im2d and im3d.

### What it targets

DX7-class hardware T&L: GeForce 256/2/4MX, Radeon 7x00. The shader path needs
Shader Model 2.0: Radeon 9500, GeForce FX 5200.

### Where it plugs in

- `rw::d3d::setFixedFunctionEnabled` is set in `OpenDeviceD3D9`
  (`engine_start.cpp`) before `Engine::open` and never changed. `auto` takes
  the fixed path when `D3DCAPS9` reports less than `vs_2_0` or `ps_2_0`, because
  librw asserts on a shader it asked for and did not get.
- `driverOpen`, `skinOpen` and `matfxOpen` read the flag to pick each pipeline's
  render callback and to skip compiling shaders. Nothing switches per draw.
- `beginUpdate` sets `D3DTS_VIEW`, `D3DTS_PROJECTION` and the fog range on the
  device (`ffBeginUpdate`). `flushCache` skips the fog constants. `FOGENABLE`
  reaches the device. im2d draws through a `POSITIONT` declaration.
- `glow.cpp` and `distort.cpp` report themselves off.

### How it maps

- Vertex lighting, fog and the UV transform: `D3DRS_LIGHTING` with
  `D3DLIGHT9`, `D3DRS_FOGVERTEXMODE`, `D3DTSS_TEXTURETRANSFORMFLAGS`.
- `diffuse * texture`: one texture stage, `MODULATE(TEXTURE, DIFFUSE)`.
- The material colour goes into `D3DRS_TEXTUREFACTOR` in stage 1, because the
  shaders multiply by it after clamping the lighting and a `D3DMATERIAL9`
  cannot express that.
- Alpha test is render state and carries over unchanged.
- Table fog is per pixel. The shader path's fog is per vertex, so here the
  fixed path is the better picture.
- **Skinning is on the CPU.** Fixed-function vertex blending caps at about four
  matrices per draw and the characters have more bones. `skinRenderCB_Fix`
  blends into a dynamic vertex buffer each frame from the geometry's portable
  arrays (`skin->indices`, `skin->weights`, morph target 0) and emits
  POSITION/NORMAL/COLOR/TEXCOORD. The index buffer is the instance header's,
  unchanged.

`third_party/librw/src/d3d/d3d8.cpp` is not a fixed-function backend. It is the
`PLATFORM_D3D8` stream plugin. `lightingCB_Fix`, `setMaterial_fix` and the
formerly commented-out `defaultRenderCB_Fix` are what the fixed path reused.

### What it loses

Glow, distortion, per-pixel lighting and toon. The lighting is close, not
identical: fixed function normalises with `D3DRS_NORMALIZENORMALS` and
attenuates point and spot lights as `1/(a + bd + cd^2)`. The tree's fix for
librw lighting an object in proportion to its scale (`94b867a3`) is not
replicated. The header of `d3d9ff.cpp` lists the differences.

### What is still missing

- **Environment mapping.** matfx and skin+matfx fall back to the plain render,
  so a shiny material draws its base texture only. It needs a second stage with
  `D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR`.
- **Vertex alpha on lit geometry.** Fixed function takes vertex alpha from the
  DIFFUSE source, so a mesh with vertex alpha reads its diffuse from the
  vertices, which tints its dynamic light by the baked one.
- **No run on hardware without shaders.** Every check so far is
  `pipeline = fixed` on a modern card. Two texture stages,
  `D3DRS_TEXTUREFACTOR`, `D3DTTFF_COUNT2` and a stride-0 constant vertex stream
  are assumed present.
- **No side-by-side comparison with the shader path.** A second implementation
  of the same lighting, fog, alpha and blend rules finds bugs where the two
  disagree. That needs someone to look at the same scene in both.

Every effect added to the shader path has to be gated or duplicated for as long
as both paths exist.

## Not built

### Anisotropic filtering

The sampler side is plumbed on every backend: `setFilterMode` in
`d3ddevice.cpp`, `filterMode` in `d3d11state.cpp`, `setFilterMode` in
`gl3device.cpp` (`EXT_texture_filter_anisotropic`), and the Vulkan sampler
cache. All read `Texture::getMaxAnisotropy`, which is 1 because nothing calls
`setMaxAnisotropy`. A global override plus a `video.anisotropy` key is small.

It depends on mip levels, which are not confirmed; see below. The game's floors
are long oblique planes, so this is the largest sharpness gain available at
high render sizes.

### Per-pixel fog

The fog factor is interpolated from the vertices, and world sectors are large
polygons. Computing it in the pixel shader from interpolated `w` is a few lines
per backend and allows exponential and height fog.

### Better bloom

The glow matches the Xbox: two passes, four taps. It is thin at high render
sizes. An optional mode with a Karis-average bright pass and a progressive
downsample/upsample chain reuses `glow.cpp`'s target management.

### Cheap post passes

FXAA or SMAA 1x for when MSAA is too costly, sharpening for render sizes below
the display, a tonemap with an optional 3D LUT. All pure post, all fit
`ps_2_0`, all shaped like the glow pass.

### A depth source

D3D9 cannot sample its depth buffer. The options are the INTZ format or a
depth prepass into an `R32F` target. Branch `pc-shadowmap` (unmerged) packs
depth into an RGBA8 colour target instead, which needs neither and works on
D3D9 and GL3 alike. GL3 already has `bindVirtualScreenDepth`. D3D11 and Vulkan
can sample depth directly. Decide this once before building anything below.

### SSAO

Needs the depth source. The world's lighting is baked vertex colour and is
expected to contain authored occlusion already, so naive SSAO darkens corners
twice. It has to multiply the ambient term only, small radius. Reconstruct
normals from depth: 34 levels ship no geometry normals.

### Depth of field

Cutscenes only. In gameplay it fights the player's judgement of a jump.

### Shadow maps

Branch `pc-shadowmap`, unmerged: an orthographic light camera renders the player
as packed depth, working on GL3; nothing samples it yet. librw on treedome
already has GL3's receiver side (`rw::gl3::setShadowMap`, the `u_shadowParams`
uniforms in `header.frag`), with no caller. Treedome ships the retail projected
shadow only.

## Ruled out

- **TAA.** Needs motion vectors, which need per-object previous transforms that
  librw does not track, plus jitter, on alpha-test-heavy art.
- **SSR.** No roughness data. The matfx environment map covers the reflections
  the art asks for.
- **PBR or a deferred path.** The game is prelit with no material parameters.

## Not verified

- **Mip levels.** `iSystem.cpp` asks for auto-mipmapping, but whether any
  backend builds mip levels for the game's textures has not been checked. hb01's
  Xbox textures ship none. Anisotropic and trilinear filtering do nothing
  without them.
- **That the prelight contains baked occlusion.** The prelight is measured and
  universal. That it holds occlusion and not only colour is inferred from how
  the world looks. It decides how SSAO has to be applied.
- **How the 17 `bspLightKit`s compare with the bake.** `world_lighting = on`
  renders them. No comparison against `off` is recorded here.
