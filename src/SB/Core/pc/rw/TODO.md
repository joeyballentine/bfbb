# The 112 RenderWare functions, and what is done

Regenerate this list with:

    llvm-nm <every game object> | awk '$1=="U"{print $2}' \
      | sed 's/^?*//;s/@.*//' | grep -E '^_*(Rw|Rp|Rt|Rx|rw|rp|rt|rx)' | sort -u

**That command used to end `s/^_*//' | grep -E '^(Rw|Rp|Rt|Rx)'`, and the strip
and the anchor together dropped every symbol RenderWare spells with a leading
underscore** -- `_rwObjectHasFrameSetFrame` came out of the strip as a
lowercase `rw` that `^(Rw|Rp|Rt|Rx)` did not match. The leading underscore is
part of the name here; these are PowerPC objects, so nothing prefixes a `_` of
its own. Keeping the underscore and letting the grep skip over it is what makes
the list right.

Four functions were hidden that way. One of them is now written
(`_rwObjectHasFrameSetFrame`, in `frame.cpp`); the other three are listed under
"Do this next" below. Everything else the corrected command turns up is either
GameCube-only (`_rwDl*`, `_rwDolphin*`, `_RwGameCubeRasterExtOffset`,
`_rxPipelineDestroy`, `_rpMaterialListGetMaterial` -- all reached only from
`src/SB/Core/gc`), already provided (`_rpPTankAtomicDataOffset` and
`_rpPTankGlobalsOffset`, which `ptank.cpp` defines), or defined by the game
itself (`_rpCollBSPTreeForAllCapsuleLeafNodeIntersections`, in xCollide.cpp and
xShadow.cpp).

## Done (105)

`value.cpp` -- value types only, so no object layout is involved and the
reinterpret_casts are backed by static_asserts that fail the build if librw
rearranges anything:

  - `RwMatrixInvert`
  - `RwMatrixScale`
  - `RwMatrixTranslate`
  - `RwMatrixUpdate`
  - `RwV3dLength`
  - `RwV3dNormalize`
  - `RwV3dTransformPoints`

`engine.cpp` -- byte order, the error slot, and the two GameCube driver entry
points that have no host counterpart:

  - `RwErrorGet`
  - `RwGameCubeCameraTextureFlush`
  - `RwGameCubeSetMinRetraceCount`
  - `RwMemNative32`

`frame.cpp` -- the first group written against the mirrored layout, and the
proof that the approach works. RwFrame now has librw's field ORDER under
RenderWare's field NAMES, so an `RwFrame*` IS an `rw::Frame*` and each of these
is a cast and a call. `RwFrameOrthoNormalize` is written out rather than
forwarded, because librw has no counterpart:

  - `RwFrameCreate`
  - `RwFrameDestroy`
  - `RwFrameGetLTM`
  - `RwFrameOrthoNormalize`
  - `RwFrameRotate`
  - `RwFrameTransform`
  - `RwFrameTranslate`
  - `RwFrameUpdateObjects`

`engine_start.cpp` -- the startup sequence, which gated everything else.
RenderWare's init/open/start maps one-to-one onto librw's, including the rule
that plugin attaches go strictly between init and open, so the GameCube's
`RenderWareInit` sequences it correctly already:

  - `RwEngineClose`
  - `RwEngineGetCurrentVideoMode`
  - `RwEngineGetVideoModeInfo`
  - `RwEngineInit`
  - `RwEngineInstance`
  - `RwEngineOpen`
  - `RwEngineStart`
  - `RwEngineStop`
  - `RwEngineTerm`

(Nine names, three of which -- `RwEngineInstance` and the two video-mode calls
-- are on the 112-function list; the other six are what it took to get there.)

Verified by running, not by compiling: `tests/selftest.cpp` creates a frame
through the C API, moves it, reads `->modelling.pos` back out of the RenderWare
struct and gets 1 2 3. Before this file existed that same program faulted.

The asset-reading path -- everything between a HIP block and a texture the
renderer can bind. RwRaster, RwTexture, RwTexDictionary and RwImage are
mirrored the way RwFrame is, with their offsets asserted in
`layout_stream.cpp`:

`stream.cpp` -- RwStream is the one type here that is NOT mirrored, and the
reason is structural: RenderWare's stream is a POD tagged union, librw's is an
abstract class with a vtable. rwsdk leaves the type incomplete on PC and
`stream.h` defines it as something deriving from `rw::Stream`. The memory
stream is written out rather than forwarded, because librw's truncates at its
initial capacity where RenderWare's grows, and FullAtomicDupe depends on the
growth:

  - `RwStreamOpen` -- `rwSTREAMMEMORY` and `rwSTREAMFILENAME` only, see below
  - `RwStreamClose`
  - `RwStreamFindChunk`
  - `RwStreamReadChunkHeaderInfo`

`texture.cpp`:

  - `RwTexDictionaryStreamRead`
  - `RwTexDictionaryDestroy`
  - `RwTexDictionaryForAllTextures`
  - `RwTexDictionaryRemoveTexture`
  - `RwTextureCreate`
  - `RwTextureDestroy`

`raster.cpp`:

  - `RwRasterCreate`
  - `RwRasterDestroy`

`image.cpp` -- `RwImageSetFromRaster` is written out, because librw's
`Raster::toImage` returns a new image where RenderWare fills the caller's:

  - `RwImageCreate`
  - `RwImageDestroy`
  - `RwImageAllocatePixels`
  - `RwImageSetFromRaster`

### Left unimplemented on purpose

  - `RwStreamOpen(rwSTREAMFILE, ...)` -- takes a `FILE*` the caller keeps
    owning, and librw's StreamFile closes whatever handle it holds.
  - `RwStreamOpen(rwSTREAMCUSTOM, ...)` -- the caller supplies a skip callback
    that only moves forward, so `Stream::seek` cannot be honoured.
  - `RwImageSetFromRaster` into a 16- or 24-bit image -- librw converts to 32,
    8 and 4 bits and no further, and the conversion belongs in a driver rather
    than in the shim.

Nothing in the game reaches any of the three. They return NULL rather than
pretending to work.

`tests/selftest.cpp` runs all of this against a live engine, including the one
thing librw's own memory stream cannot do -- growing from an empty RwMemory --
and round-tripping a chunk back out through `RwStreamFindChunk`. Three things
in the group are checked only for their refusals, because they reach a render
backend that `LIBRW_PLATFORM=NULL` does not have: `RwRasterCreate` (librw's
null driver asserts in `rasterCreate`), the success path of
`RwImageSetFromRaster` (driver `rasterToImage`), and the success path of
`RwTexDictionaryStreamRead` (the textures inside a TXD are native rasters).
Whoever links a GL3 or D3D9 librw should come back and finish those three.

The camera, light, render state and immediate mode group. `RwCamera` and
`RpLight` are mirrored the way `RwFrame` is, with their offsets asserted in
`layout_camera.cpp` -- which also asserts every enumeration these cast straight
through, because a renumbering in librw would otherwise be silent.

`camera.cpp` -- `RwCameraBeginUpdate` and `RwCameraEndUpdate` are what close out
the `curCamera`/`curWorld` item that used to be at the top of "Do this next".
They mirror both fields out of `rw::engine` on begin and null both on end,
because the game uses "curCamera is not null" as "an update is in progress"
(zGame.cpp:910, zNPCTypePrawn.cpp:1718). `RwCameraSetViewWindow` does not
maintain a `recipViewWindow`: librw has no such field and the mirrored struct
therefore does not either:

  - `RwCameraCreate`
  - `RwCameraDestroy`
  - `RwCameraBeginUpdate`
  - `RwCameraEndUpdate`
  - `RwCameraClear`
  - `RwCameraShowRaster`
  - `RwCameraGetWorld`
  - `RwCameraSetProjection`
  - `RwCameraSetViewWindow`
  - `RwCameraSetNearClipPlane`
  - `RwCameraSetFarClipPlane`
  - `RwCameraFrustumTestSphere`

`light.cpp` -- `RpLightSetRadius` is written out, because librw has no setter
for it:

  - `RpLightCreate`
  - `RpLightDestroy`
  - `RpLightSetColor`
  - `RpLightSetRadius`
  - `RpLightSetConeAngle`

`renderstate.cpp` -- the one group here that is not a cast and a call. The game
reads render state back (nine `RwRenderStateGet` calls in `xfont::
set_render_state` alone, plus `RxRenderStateVectorLoadDriverState` in
xShadowSimple) and forwarding a Get to librw would not answer it: librw asks the
render device, and the device under `LIBRW_PLATFORM=NULL` answers 0 to
everything. So the shim keeps its own copy of the state vector, forwards every
state librw models on the way in, and answers a Get out of the copy:

  - `RwRenderStateGet`
  - `RwRenderStateSet`
  - `RxRenderStateVectorLoadDriverState`

`im.cpp`:

  - `RwIm2DGetNearScreenZ`
  - `RwIm2DGetFarScreenZ`
  - `RwIm2DRenderPrimitive`
  - `RwIm2DRenderIndexedPrimitive`
  - `RwIm3DTransform`
  - `RwIm3DRenderPrimitive`
  - `RwIm3DEnd`

`tests/selftest.cpp` runs all twenty-seven against a live engine, including the
`curCamera`/`curWorld` pair, the frustum planes at the indices iCamera.cpp reads
them at, and the fog colour swizzle. The render state and immediate mode calls
are checked by standing in for the device's own entry points -- `rw::engine->
device.setRenderState` and friends are replaced for the length of a call -- so
what reaches librw is checked rather than assumed, which the null device makes
impossible any other way.

### Recorded but not rendered, or not reachable without a backend

  - `rwRENDERSTATESHADEMODE` -- librw has no shade mode; its own header says
    "? shademode". Fifteen call sites. The value is recorded so that
    xFont.cpp:626/649 can still save and restore it, and `RwRenderStateSet`
    returns FALSE to say it did not reach a renderer. Everything the game draws
    flat will come out gouraud, which for single-colour untextured geometry is
    the same picture -- the outlined text in xFont.cpp:3230 is not.
  - `rwRENDERSTATETEXTUREPERSPECTIVE`, `rwRENDERSTATEBORDERCOLOR`,
    `rwRENDERSTATEFOGTYPE` -- same treatment, no call sites that matter.
  - `rwRENDERSTATEFOGDENSITY` -- refused outright rather than recorded. The one
    caller passes a POINTER to a float (iCamera.cpp:380), so the encoding is a
    console driver detail this side cannot check, and librw has no fog density
    state to forward to.
  - `rwRENDERSTATECULLMODE` -- set and forwarded, but a Get for it asks librw,
    because `RxRenderStateVector` has no field to shadow it in. That is
    RenderWare's own omission and xShadowSimple does not restore cull mode
    either. Under `LIBRW_PLATFORM=NULL` the Get answers 0. No caller in src/SB.
  - `RwCameraClear` and `RwCameraShowRaster` -- exercised only for their
    refusals. Both reach the device (`clearCamera`, `Raster::show`), and the
    null device's clear is an empty function, so a success would prove nothing
    about what ends up on screen.
  - `RwIm2DVertex` is **still the GameCube's 24-byte layout** and does not match
    either real backend's -- both carry a `w` for the camera z that
    `rwGameCube2DVertex` has no room for, and `RwIm2DVertexSetRecipCameraZ` is a
    no-op macro on this header. `im.cpp` refuses to compile against `RW_GL3`,
    `RW_D3D9` and the rest rather than hand a backend vertices it will read past
    the end of. Whoever links one has to give the typedef in `rwplcore.h` the
    backend's layout and make the twelve `RwIm2DVertexSet*` macros write it.
    `RwIm3DVertex` (`RxObjSpace3DVertex`) happens to match GL3's `Im3DVertex`
    field for field, so only the 2D one is outstanding.
  - `RwIm3DTransform` returns the caller's own vertex array as its success
    answer. RenderWare returns a pointer into its immediate-mode heap and librw
    returns nothing, so there is no equivalent to hand back; all twenty call
    sites use the result only as "may I render now", and returning NULL would
    silently stop every effect in the game from drawing. Nothing may
    dereference it.
The model path -- geometries, materials, atomics, and the three plugins that
hang off them. RpMaterial, RpGeometry and RpAtomic are mirrored the way RwFrame
is, with their offsets asserted in `layout_geometry.cpp`; RpTriangle,
RpMaterialList, RpMorphTarget, RpMesh and RpMeshHeader needed no reordering and
are asserted anyway. RpSkin and RpMatFX are mirrored not at all, because
RenderWare keeps both opaque to the game.

`geometry.cpp`:

  - `RpGeometryCreate`
  - `RpGeometryLock`
  - `RpGeometryUnlock`
  - `RpGeometryForAllMaterials`
  - `RpGeometryTriangleGetVertexIndices`
  - `RpGeometryTriangleSetVertexIndices`
  - `RpGeometryTriangleGetMaterial`
  - `RpGeometryTriangleSetMaterial`
  - `RpMaterialSetTexture`
  - `RpMorphTargetCalcBoundingSphere`

`atomic.cpp` -- `RpAtomicForAllIntersections` is written out rather than
forwarded, because librw has no collision code at all. It is a linear scan
where RenderWare descends a collision BSP tree, so it reports the same
triangles in geometry order instead of tree order and costs O(triangles) per
query. iCollide.cpp already times these calls in `collide_rwtime`:

  - `RpAtomicSetFrame`
  - `RpAtomicSetGeometry`
  - `RpAtomicForAllIntersections` -- sphere, line and box

`skin.cpp` -- the one group that needed no struct mirrored, because rpskin.h
never defines RpSkin. What the accessors hand back is not opaque though, and
the strides are asserted:

  - `RpSkinPluginAttach`
  - `RpSkinGeometryGetSkin`
  - `RpSkinGetNumBones`
  - `RpSkinGetSkinToBoneMatrices`
  - `RpSkinGetVertexBoneWeights`
  - `RpSkinGetVertexBoneIndices`
  - `RpSkinAtomicSetType`

`matfx.cpp`:

  - `RpMatFXPluginAttach`
  - `RpMatFXAtomicEnableEffects`
  - `RpMatFXMaterialSetEffects`
  - `RpMatFXMaterialGetEffects`
  - `RpMatFXMaterialSetupEnvMap`
  - `RpMatFXMaterialSetupBumpMap`
  - `RpMatFXMaterialSetEnvMapCoefficient`
  - `RpMatFXMaterialSetBumpMapCoefficient`

`ptank.cpp` -- written out end to end, because librw has no PTank at all:

  - `RpPTankPluginAttach`
  - `RpPTankAtomicCreate`
  - `RpPTankAtomicDestroy`
  - `RpPTankAtomicLock`
  - `RpPTankAtomicUnlock`

The three `Rp*PluginAttach` calls are the answer to where librw's
`registerSkinPlugin`/`registerMatFXPlugin` and the PTank atomic plugin belong.
They are not on the 112-function list, but `RWAttachPlugins` in iSystem.cpp
already calls them between `RwEngineInit` and `RwEngineOpen`, which is exactly
the window each one needs: all three grow the size of an atomic, a geometry or
a material, and `Engine::open` freezes those sizes. Putting them in
`RwEngineInit` instead would attach plugins the game never asked for.

`tests/selftest.cpp` runs all of it against a live engine: the intersection
walk against a known quad with the atomic's frame moved, so that the world-in
object-out contract is checked rather than assumed; the skin accessors against
a skin built by hand; the matfx effect block read back through librw; and both
PTank memory layouts, structure-of-arrays and interleaved.

### What the model path does NOT do yet

Four things, each of which will show up as a visible difference rather than as
a crash. None of them is on the 112-function list; all four need writing before
the port draws a frame that looks right.

**PTank particles are invisible.** `RpPTankAtomicCreate` builds the atomic, the
geometry and the particle buffers, and lock/unlock are complete -- but nothing
turns particle positions into billboard vertices. That instancing step needs
the camera's right and up vectors and there is no camera yet (see
`RwEngineInstance->curCamera` below), so the ptank's vertices stay at the zeros
`RpPTankAtomicCreate` wrote and every triangle is degenerate. Nothing draws;
nothing draws *wrongly*. Whoever writes `RwCameraBeginUpdate` should come back
for this. It is NOT tested, because it does not exist.

**Skinned models get the plain skin pipeline.** `RpSkinAtomicSetType` passes the
type through, but librw's `Skin::setPipeline` casts it to void and installs the
one skin pipeline it has per platform. The two calls asking for
`rpSKINTYPEMATFX` -- xFX.cpp and zEntCruiseBubble.cpp, both wanting an
environment-mapped skinned model -- will render without the effect.

**RpAtomic has lost four of RenderWare's fields**, because librw's atomic is 84
bytes to RenderWare's 112 and there is nowhere to put them that is not inside
librw's plugin block: `repEntry`, `interpolator`, `renderFrame` and
`llWorldSectorsInAtomic`. rpworld.h says which and why. Reaching for one is a
compile error on PC, which is the point; three sites clear
`interpolator.flags` today (zAssetTypes.cpp, zCutsceneMgr.cpp, and the
GameCube-only iModel.cpp) and need looking at when the PC build reaches them.
`RpAtomicGetBoundingSphere` already has a PC spelling that does not need the
interpolator, because with no morph interpolation the sphere is never stale.

**`RpAtomicForAllIntersections` is a linear scan.** RenderWare descends the
collision BSP tree the model was built with; there is no tree here, and
`RpCollisionPluginAttach` is unwritten. Same triangles, geometry order instead
of tree order, O(triangles) instead of O(log triangles). iCollide.cpp's
`collide_rwtime` is where that will show.

Not tested, and said plainly: nothing in this group reaches a render backend
EXCEPT the pipelines that `RpSkinAtomicSetType` and `RpMatFXAtomicEnableEffects`
install, which are librw's dummy pipelines under `LIBRW_PLATFORM=NULL`. The
selftest checks that the atomic ends up pointing at them, not that they draw
anything -- they cannot.

The clump group, the Rt maths, and the one function the list was hiding.
`RpClump` is mirrored the way `RwFrame` is, with its offsets asserted in
`layout_clump.cpp`. The Rt functions need no type mirrored at all: they are
pure maths on value types.

`clump.cpp` -- five of the six forward to librw. `RpClumpAddAtomic` does NOT,
and the reason is that RenderWare's own implementation is in this repository:
`src/rwsdk/world/baclump.c` is decompiled matching code, and its
`RpClumpAddAtomic` inserts at the HEAD (`rwLinkListAddLLLink`) where librw's
`Clump::addAtomic` appends. xJSP.cpp:171-177 moves every atomic of one clump
into another by walking an array backwards, which preserves their order only if
each add goes to the front -- and xJSP indexes its baked strip vectors by
position in that list, so appending would attach the vertex data of a merged
JSP to the wrong pieces of the level. The shim does the two list writes itself:

  - `RpClumpStreamRead`
  - `RpClumpDestroy`
  - `RpClumpAddAtomic`
  - `RpClumpRemoveAtomic`
  - `RpClumpForAllAtomics`
  - `RpClumpGetNumAtomics`

`intersect.cpp` -- the two intersection tests were file-static helpers in
`atomic.cpp`, written for `RpAtomicForAllIntersections`. They are the public
functions now and `atomic.cpp` calls them, because the game reaches the same
triangles two ways: `iCollide.cpp` asks `RpAtomicForAllIntersections` for a
model with no collision tree, and `xClumpColl.cpp` walks the tree and calls
these directly for a model that has one. `RtQuatSetupSlerpCache` is the other
half of `RtQuatSlerpMacro` in rtslerp.h, which never divides by anything -- so
the `1/sin(omega)` a slerp needs is folded into the cached quaternions here,
which is what that header means by the "scaled" initial and final quaternions:

  - `RtIntersectionSphereTriangle`
  - `RtIntersectionBBoxTriangle`
  - `RtQuatSetupSlerpCache`

`frame.cpp` -- and the function the regeneration command was hiding. Eleven
call sites, and `RwCameraSetFrame` and `RpLightSetFrame` are macros for it, so
the camera and light groups were never complete without it:

  - `_rwObjectHasFrameSetFrame`

`tests/selftest.cpp` runs all ten against a live engine: the clump's atomic
list including the head-insert and the xJSP move, a clump written out through
librw and read back in through `RpClumpStreamRead` with the atomics matched up
by where their frames sit, the sphere test against the cases a plane-distance
test would get wrong, the box test against a box inside the triangle's bounds
and past its diagonal, and a slerp checked at its midpoint, its endpoints, its
shortest-arc flip and its two degenerate cases.

### What the clump group does NOT do

**`RpClumpStreamRead`'s atomic ORDER is the one thing here that could not be
checked against retail.** Every other function in this group has decompiled
RenderWare source in `src/rwsdk/world/baclump.c` to compare against; the stream
reader does not. librw appends each atomic as it reads it, so
`RpClumpForAllAtomics` afterwards walks them in the order the file lists them,
and the selftest confirms a write/read round trip through librw is order
preserving. That is also the only order consistent with RenderWare's own
exporter, given that `RpClumpAddAtomic` prepends -- a reader built on
`RpClumpAddAtomic` would not round-trip its own files. It is still an inference.
`iModel.cpp` returns the FIRST atomic of a multi-atomic model as the model, so
if a model ever comes out inside-out, this is the line to doubt first.

**`RpClump` has lost `callback`.** librw's clump is 44 bytes and there is
nowhere to append to that is not inside its plugin block. Nothing in the game
sets or reads it -- RenderWare only calls it from `RpClumpClone`, which the
port does not have -- so reaching for it is a compile error on PC and that is
the whole of the loss. rpworld.h says so at the struct.

**librw asserts a clump is not still in a world when it is destroyed**, where
RenderWare frees it and leaves the world holding a dangling link. Nothing calls
`RpWorldAddClump`, so neither path is reachable today.

**A box query reports a distance of zero.** `RpAtomicForAllIntersections` has
no single point of contact to measure to for a box, and RenderWare's
documentation does not say what it puts there. No caller in the game issues one.

## Do this next

**Three more functions the corrected regeneration command turned up.** All
three are called from units the PC build compiles, and all three are missing,
so the link will fail on them the moment there is a link:

  - `_rwFrameSyncDirty` -- seven sites (iCamera.cpp:52, xLightKit.cpp:138,
    xModel.cpp:521, xModelBucket.cpp:166 and 622, xShadow.cpp:689,
    zGame.cpp:1461). librw has the counterpart in `Frame::syncDirty`.
  - `_rwInvSqrt` -- three sites (xClumpColl.cpp:701 and 789, xCollide.cpp:1413,
    plus the `rwInvSqrtMacro` in xCollide.cpp:29). No librw counterpart; it is
    a reciprocal square root and the console's is a Newton step off the
    PowerPC estimate instruction, which a host has no reason to reproduce.
  - `_rpMeshHeaderForAllMeshes` -- one site (xJSP.cpp:35). A walk over the mesh
    array behind an `RpMeshHeader`, whose stride is already asserted in
    `layout_geometry.cpp`.

**There is no video mode.** `LIBRW_PLATFORM=NULL` has no render device, and
librw's null device answers every request with 1 -- including reporting success
from `DEVICEGETVIDEOMODEINFO` without writing to the struct it was handed.
`RwEngineGetVideoModeInfo` detects that device and returns NULL rather than
handing xScrFx an uninitialised width and height off its own stack. The real
forwarding path is written and starts working the moment a GL3 or D3D9 librw is
linked. `RwEngineOpen` is the other half of that: it passes no
`EngineOpenParams` because the null device ignores it and RenderWare's
`displayID` (a GameCube `RwGameCubeDeviceConfig*`) has nothing to translate
into. It refuses to compile against a real backend rather than pass null to one.

## Blocked on the object-layout decision (8)

RESOLVED -- the port mirrors librw's layouts (method 1), so these are no longer
blocked on a decision, only unwritten. Each needs its type mirrored in
include/rwsdk with matching static_asserts alongside layout.cpp, in the same
commit. What is left of the 112 looks like this. A group with no entries under
it is one that is finished.

**RpAtomic** (0)

**RpClump** (0)

**RpCollisionWorld** (1)

  - `RpCollisionWorldForAllIntersections`

**RpGeometry** (0)

**RpLight** (0)


**RpMatFX** (0)

**RpMaterial** (0)

**RpMorphTarget** (0)

**RpPTank** (0)

**RpSkin** (0)

**RpWorld** (7)

  - `RpWorldAddCamera`
  - `RpWorldAddLight`
  - `RpWorldCreate`
  - `RpWorldDestroy`
  - `RpWorldRemoveCamera`
  - `RpWorldRemoveLight`
  - `RpWorldStreamRead`

**Rt** (0)

**RwCamera** (0)


**RwEngine** (0)

**RwFrame** (0)


**RwIm2D** (0)


**RwIm3D** (0)


**RwImage** (0)


**RwRaster** (0)


**RwRenderState** (0)


**RwStream** (0)


**RwTexDictionary** (0)


**RwTexture** (0)


**Rx** (0)

