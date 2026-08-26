# The 112 RenderWare functions, and what is done

Regenerate this list with:

    llvm-nm <every game object> | awk '$1=="U"{print $2}' \
      | sed 's/^?*//;s/@.*//;s/^_*//' | grep -E '^(Rw|Rp|Rt|Rx)' | sort -u

That regex drops one symbol the game does need: `_rwObjectHasFrameSetFrame`
comes out of the `^_*` strip as a lowercase `rw`, which `^(Rw|Rp|Rt|Rx)` does
not match. It is called at eight sites -- zGame.cpp:1270, iModel.cpp:67,
xLightKit.cpp:76 and 142, xModel.cpp:495 and 525, xShadow.cpp:95, 693 and 1114,
zNPCTypePrawn.cpp:568 and 622 -- and it is what `RwCameraSetFrame` and
`RpLightSetFrame` are macros for, so it is needed by the camera and light
groups even though it is not in the 112. librw has the counterpart
(`ObjectWithFrame::setFrame`); nobody has written the shim for it yet.

## Done (65)

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

## Do this next

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

## Blocked on the object-layout decision (47)

RESOLVED -- the port mirrors librw's layouts (method 1), so these are no longer
blocked on a decision, only unwritten. Each needs its type mirrored in
include/rwsdk with matching static_asserts alongside layout.cpp, in the same
commit. What is left of the 112 looks like this. A group with no entries under
it is one that is finished.

**RpAtomic** (3)

  - `RpAtomicForAllIntersections`
  - `RpAtomicSetFrame`
  - `RpAtomicSetGeometry`

**RpClump** (6)

  - `RpClumpAddAtomic`
  - `RpClumpDestroy`
  - `RpClumpForAllAtomics`
  - `RpClumpGetNumAtomics`
  - `RpClumpRemoveAtomic`
  - `RpClumpStreamRead`

**RpCollisionWorld** (1)

  - `RpCollisionWorldForAllIntersections`

**RpGeometry** (8)

  - `RpGeometryCreate`
  - `RpGeometryForAllMaterials`
  - `RpGeometryLock`
  - `RpGeometryTriangleGetMaterial`
  - `RpGeometryTriangleGetVertexIndices`
  - `RpGeometryTriangleSetMaterial`
  - `RpGeometryTriangleSetVertexIndices`
  - `RpGeometryUnlock`

**RpLight** (0)


**RpMatFX** (7)

  - `RpMatFXAtomicEnableEffects`
  - `RpMatFXMaterialGetEffects`
  - `RpMatFXMaterialSetBumpMapCoefficient`
  - `RpMatFXMaterialSetEffects`
  - `RpMatFXMaterialSetEnvMapCoefficient`
  - `RpMatFXMaterialSetupBumpMap`
  - `RpMatFXMaterialSetupEnvMap`

**RpMaterial** (1)

  - `RpMaterialSetTexture`

**RpMorphTarget** (1)

  - `RpMorphTargetCalcBoundingSphere`

**RpPTank** (4)

  - `RpPTankAtomicCreate`
  - `RpPTankAtomicDestroy`
  - `RpPTankAtomicLock`
  - `RpPTankAtomicUnlock`

**RpSkin** (6)

  - `RpSkinAtomicSetType`
  - `RpSkinGeometryGetSkin`
  - `RpSkinGetNumBones`
  - `RpSkinGetSkinToBoneMatrices`
  - `RpSkinGetVertexBoneIndices`
  - `RpSkinGetVertexBoneWeights`

**RpWorld** (7)

  - `RpWorldAddCamera`
  - `RpWorldAddLight`
  - `RpWorldCreate`
  - `RpWorldDestroy`
  - `RpWorldRemoveCamera`
  - `RpWorldRemoveLight`
  - `RpWorldStreamRead`

**Rt** (3)

  - `RtIntersectionBBoxTriangle`
  - `RtIntersectionSphereTriangle`
  - `RtQuatSetupSlerpCache`

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

