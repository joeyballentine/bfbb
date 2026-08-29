# Adding to the renderer

## Where things stand

Nothing in this document is implemented. It is an audit of two questions asked
in the same sitting -- what modern graphical effects the port could realistically
gain as optional settings, and whether it could also gain a fixed-function mode
for old hardware -- and it records what reading the pipeline turned up for each.
The file:line references are to the tree as of the `treedome` branch.

The two halves pull in opposite directions on purpose. One raises the ceiling,
the other lowers the floor, and the last section of part two is about what it
costs to want both.

## What the pipeline already is

Three facts decide most of what follows, and all three are better than they
might have been.

**librw's D3D9 backend is shader-based, not fixed-function.** Every draw goes
through a real vertex and pixel shader pair --
`third_party/librw/src/d3d/shaders/default_VS.hlsl` and `default_PS.hlsl`, with
lighting, fog and material constants in `standardConstants.h`. There is no
fixed-function wall to climb over for anything in part one.

**Full-screen post infrastructure exists and is ours.**
`src/SB/Core/pc/rw/glow.cpp` captures the back buffer into a sampleable raster,
manages a downsample chain of render targets, and runs compiled shader blobs
over it, with `rw/shaders/make_shaders.cmd` to rebuild them from HLSL.
`distort.cpp` and `snapshot.cpp` do the same capture. Adding another post pass
is a copy of an existing file rather than new plumbing.

**Everything already renders offscreen.** `rw/engine_start.cpp:364` calls
`setVirtualScreen`, so the frame is drawn into a render target of the configured
size and stretched into the back buffer at present time. A post chain running at
render resolution is already the shape the engine is in. See
[RESOLUTION.md](RESOLUTION.md).

The current hardware floor is Shader Model 2.0: everything compiles to `vs_2_0`
and `ps_2_0` (`shaders/make_default.cmd`, `rw/shaders/make_shaders.cmd`).

---

# Part one: modern effects

## Already in the engine, not exposed

These are the best ratio in the document. The code is present and unreachable.

### Anisotropic filtering

`maxAniso` is plumbed all the way to the sampler state
(`d3d/d3ddevice.cpp:599-616`), but it is a per-texture value and nothing in the
port ever sets one, so every texture runs at 1. A global override at the
`setFilterMode` call site plus a `video.anisotropy` key is small.

On a game whose floors are all long oblique ground planes, this is the largest
single sharpness gain available at high render sizes -- larger than any post
effect. It depends on the textures having mipmaps, which is **unverified**;
`rw/raster.cpp` handles mip levels but nothing generates them. Check the assets
before building anything on this.

### MSAA

`d3d9Globals.msLevel` exists and feeds the present parameters
(`d3ddevice.cpp:1951`), but the virtual screen render target is created
`D3DMULTISAMPLE_NONE` (`d3ddevice.cpp:1333`), and since every draw lands there,
the back buffer's sample count is dead. Making it work needs a multisampled
target and a resolve. Contained, but it is a change to the virtual screen, so
read RESOLUTION.md's account of who owns that surface first.

Note that MSAA does not touch alpha-tested edges, which is most of this game's
silhouettes. See alpha-to-coverage below.

### Supersampling, which is already there

`video.width` and `height` render above the display and downsample at present.
That is SSAA, and it is the most effective antialiasing on the list, but nothing
says so. Either the `config.ini` comment should say it or a `render_scale`
should be split out so nobody has to compute it.

## One new shader, existing plumbing

### Alpha-to-coverage

This pairs directly with the existing `video.alpha_cutout` setting, which turns
the soft blended band at a cutout's edge into a hard cut. The hard cut is
correct at any resolution but it is aliased, and MSAA will not fix it because
the edge comes from an alpha test rather than from geometry. Alpha-to-coverage
is a single render state and it gives that silhouette proper antialiasing.
Highest gain per line in part one.

### Per-pixel fog

The fog factor is computed in the vertex shader and interpolated:
`default_VS.hlsl:66` writes it into `TexCoord0.z` and `default_PS.hlsl` lerps
with it. World sectors are large polygons, so the factor is subtly wrong across
them. Computing it in the pixel shader from interpolated `w` is a few lines, and
opens the door to exponential and height fog as options.

### Per-pixel lighting

librw lights per vertex -- ambient, directional, point and spot, all summed in
`default_VS.hlsl` and `skin_VS.hlsl`. The characters are low-poly, so the
shading is visibly faceted on curved surfaces. Moving the lighting into the
pixel shader is contained (interpolate normal and world position, add PS
variants) and would visibly improve every character in the game without
touching any art. Good ratio, and it composes with everything below.

### Better bloom

The glow chain is faithful to the Xbox: two passes, four taps, weights and tap
distances documented in `glow.cpp` and `iGlow.h`. It is thin at high render
sizes. An optional enhanced mode -- Karis-average bright pass, progressive
downsample and upsample chain -- reuses `glow.cpp`'s render target management
wholesale. Low risk because the hard part is already written.

### The cheap post passes

FXAA or SMAA 1x for when MSAA is not affordable, sharpening for when the render
size is below the display, and a tonemap with an optional 3D LUT slot. All pure
post, all fit in `ps_2_0`, all copies of the glow pass's structure.

## Needs a depth source

D3D9 cannot read the depth buffer. Everything in this section is gated behind
one decision: the INTZ format hack, which works on essentially all D3D9-era
parts from all three vendors, or an explicit depth prepass into an `R32F`
target, which costs an extra pass over world and skinned geometry but depends on
nothing. That choice should be made once, deliberately, before any of the
following is started.

### SSAO

Realistic once depth exists, with one important caveat. **The world's lighting
is baked vertex colour**, which already contains authored ambient occlusion.
Naive SSAO will darken every corner a second time and look muddy. It has to land
as a multiply on the ambient term only, tuned tight and small-radius, catching
the creases the bake missed. Reconstruct normals from depth rather than trusting
geometry normals -- whether world sectors carry them is unverified.

### Depth of field

Worth scoping to cutscenes only. Gameplay depth of field on a 3D platformer
fights the player's ability to judge a jump.

### Shadow maps

`x/xShadow.cpp` projects a raster blob; `xShadowSimple.cpp` is the cheaper one.
Real shadows for the player and NPCs would be the largest single visual upgrade
available, and also the largest job: a per-level light direction (the
directional lights are in `zLight.cpp`), a shadow target, a caster pass, and
receiver sampling in the world pixel shader. Multi-week. The cheaper middle
ground is to keep the projected shadow and make it soft and correctly fitted.

## Ruled out

**TAA.** Needs motion vectors, which needs per-object previous transforms, which
librw does not track, plus jitter infrastructure, on a game whose art is
alpha-test-heavy. Bad trade.

**SSR.** No roughness data anywhere, and the matfx environment map already fakes
what little reflection the art asks for.

**PBR relighting or a deferred path.** The game is baked prelit with no material
parameters. It would not look better, it would look wrong.

## If only one thing gets done

Anisotropic filtering and alpha-to-coverage. Together they are a couple of days
and they fix the two things that actually make the game read as low-resolution
at high render sizes. SSAO is genuinely reachable but it is the depth-source
decision plus a careful fight with the baked lighting, so it is a project rather
than a setting.

---

# Part two: a fixed-function mode

## What it would lower the bar to

From Shader Model 2.0 -- Radeon 9500 (2002), GeForce FX 5200 (2003) -- to
DX7-class hardware T&L: GeForce 256/2/4MX, Radeon 7x00, 1999-2000. A real
three-year span, and it lands the game on hardware contemporary with the
GameCube and Xbox it shipped on, which is most of the appeal.

## `d3d8.cpp` is not what it looks like

`third_party/librw/src/d3d/d3d8.cpp` reads like a fixed-function backend and is
not one. It is the `PLATFORM_D3D8` stream plugin -- the reader for D3D8-instanced
geometry in RW files. There is no `IDirect3DDevice8` anywhere in librw. Nobody
should start this work expecting a backend to already be sitting there.

## The mapping is mostly one to one

- `default_VS.hlsl` does transform, vertex lighting for all four light types, a
  fog factor and an optional UV transform. Fixed function does every one of
  those natively: `D3DRS_LIGHTING` with `D3DLIGHT9`, `D3DRS_FOGVERTEXMODE`,
  `D3DTSS_TEXTURETRANSFORMFLAGS`.
- `default_PS.hlsl` is `diffuse * texture` then a fog lerp. That is one texture
  stage set to `MODULATE(TEXTURE, DIFFUSE)`, and fixed-function fog.
- `im2d` is pre-transformed 2D. `D3DFVF_XYZRHW`.
- Alpha test, including the `alpha_cutout` work, is render state
  (`D3DRS_ALPHATESTENABLE`, `ALPHAREF`, `ALPHAFUNC`) rather than shader code, so
  it crosses over unchanged.
- matfx environment mapping maps to `D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR`
  plus a second blended stage. Fiddly rather than hard. It needs two texture
  stages, which is exactly what DX7-era parts have, so it just fits.

There is an irony here worth keeping. Fixed-function **table fog**
(`D3DRS_FOGTABLEMODE`) is evaluated per pixel. The shader path computes fog per
vertex and interpolates it. Until the per-pixel fog above is written, the
fixed-function mode would have better fog than the shader mode.

## Skinning is the one real problem

`skin_VS.hlsl` declares `float4x3 boneMatrices[64]` and blends four influences
per vertex; `d3d9skin.cpp:289` uploads `skin->numBones*3` constants. Fixed
function has indexed vertex blending, but it is capped at
`MaxVertexBlendMatrixIndex`, which on hardware of that era is typically four
matrices per draw. The characters have far more bones than that. Two ways out:

1. **Bone-partition each mesh** into batches whose vertices touch at most four
   bones, one draw per batch. Correct, stays on the GPU, and is a week plus a
   permanent extra path in the pipeline builder.
2. **Skin on the CPU** into a dynamic vertex buffer each frame, emitting a plain
   POSITION/NORMAL/COLOR/TEXCOORD stream with no blend data at all. Two or three
   days, and it is what games of that hardware generation actually did. On a
   GeForce 2 the CPU is frequently the better skinner anyway.

Option 2, unless something argues otherwise.

## What is lost

Glow, distortion and everything in part one -- all `ps_2_0`. The `xbox.glow` and
`xbox.distortion` settings would need to report themselves forced off rather
than silently doing nothing. The snapshot should survive; it is a surface copy,
not a shader.

The lighting will be close but not identical. Fixed function has its own
normalisation and attenuation, and the tree already carries a local fix in that
area (`94b867a3`, librw lighting an object in proportion to its scale) which
would have to be replicated by hand. This is a look-alike mode, not a match.

## Where it plugs in

librw is our own fork (`joeyballentine/librw`, branch `bfbb-port`), so editing it
is ordinary practice here rather than a vendor patch.

The seam is clean. Drivers install their `defaultPipeline` at `driverOpen`, and
the feature pipelines are already separate files -- `d3d9render.cpp`,
`d3d9skin.cpp`, `d3d9matfx.cpp`, `d3d9skinmatfx.cpp`. Fixed-function siblings
would be swapped in at open. The caps machinery for an `auto` mode also exists:
`d3ddevice.cpp:1876` already calls `GetDeviceCaps`, and `:1909` already falls
back to `D3DCREATE_SOFTWARE_VERTEXPROCESSING`.

Config shape: `video.pipeline = auto | shader | fixed`.

## Cost, and the argument beyond nostalgia

Roughly a weekend to get the world drawing with no characters in it, and one to
two weeks for the whole thing.

The better argument is not nostalgia. A fixed-function path is an independent
second implementation of the same lighting, fog, alpha and blend semantics.
Where the two disagree, one of them is wrong. The port has already shipped two
bugs that came from librw's D3D9 driver inferring alpha state the console left
to the game; a second path turns that class of bug into something findable by
comparison rather than by noticing it in play.

The real cost is not the build, it is that every effect in part one then has to
be gated or duplicated for as long as both modes exist.

---

## What is not verified here

Stated plainly so nobody builds on it by accident:

- **Whether the assets ship mipmaps.** Anisotropic and trilinear filtering both
  depend on it and neither does anything without it.
- **Whether world sector geometry carries normals.** It affects SSAO's normal
  source and any per-pixel lighting on the world.
- **That the vertex prelight contains baked AO.** It is the expectation from how
  the world is lit, not something measured. It decides how SSAO has to be
  applied, so measure it before tuning anything.
- **`MaxVertexBlendMatrixIndex` on any specific target.** Four is the typical
  hardware value, not a number read off a device here.
