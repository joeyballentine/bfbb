# The 112 RenderWare functions, and what is done

Regenerate this list with:

    llvm-nm <every game object> | awk '$1=="U"{print $2}' \
      | sed 's/^?*//;s/@.*//;s/^_*//' | grep -E '^(Rw|Rp|Rt|Rx)' | sort -u

## Done (22)

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

## Do this next

Two things the startup could not finish, both of which need another group
written first:

**`RwEngineInstance->curCamera` and `->curWorld` are always null.** librw keeps
the same two fields in `rw::engine`, and RwGlobals agrees with `rw::Engine` on
those first two offsets and on nothing after them, so the shim has a second
RwGlobals rather than an alias. xCutscene.cpp and xFX.cpp read the fields
directly, which leaves no call to hook -- so whoever writes `RwCameraBeginUpdate`
/ `RwCameraEndUpdate` and `RpWorldRender` has to assign both copies.

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

## Blocked on the object-layout decision (90)

RESOLVED -- the port mirrors librw's layouts (method 1), so these are no longer
blocked on a decision, only unwritten. Each needs its type mirrored in
include/rwsdk with matching static_asserts in layout.cpp, in the same commit.
What is left of the 112 looks like this. A group with no entries under it is
one that is finished.

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

**RpLight** (5)

  - `RpLightCreate`
  - `RpLightDestroy`
  - `RpLightSetColor`
  - `RpLightSetConeAngle`
  - `RpLightSetRadius`

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

**RwCamera** (12)

  - `RwCameraBeginUpdate`
  - `RwCameraClear`
  - `RwCameraCreate`
  - `RwCameraDestroy`
  - `RwCameraEndUpdate`
  - `RwCameraFrustumTestSphere`
  - `RwCameraGetWorld`
  - `RwCameraSetFarClipPlane`
  - `RwCameraSetNearClipPlane`
  - `RwCameraSetProjection`
  - `RwCameraSetViewWindow`
  - `RwCameraShowRaster`

**RwEngine** (3)

**RwFrame** (8)


**RwIm2D** (4)

  - `RwIm2DGetFarScreenZ`
  - `RwIm2DGetNearScreenZ`
  - `RwIm2DRenderIndexedPrimitive`
  - `RwIm2DRenderPrimitive`

**RwIm3D** (3)

  - `RwIm3DEnd`
  - `RwIm3DRenderPrimitive`
  - `RwIm3DTransform`

**RwImage** (4)

  - `RwImageAllocatePixels`
  - `RwImageCreate`
  - `RwImageDestroy`
  - `RwImageSetFromRaster`

**RwRaster** (2)

  - `RwRasterCreate`
  - `RwRasterDestroy`

**RwRenderState** (2)

  - `RwRenderStateGet`
  - `RwRenderStateSet`

**RwStream** (4)

  - `RwStreamClose`
  - `RwStreamFindChunk`
  - `RwStreamOpen`
  - `RwStreamReadChunkHeaderInfo`

**RwTexDictionary** (4)

  - `RwTexDictionaryDestroy`
  - `RwTexDictionaryForAllTextures`
  - `RwTexDictionaryRemoveTexture`
  - `RwTexDictionaryStreamRead`

**RwTexture** (2)

  - `RwTextureCreate`
  - `RwTextureDestroy`

**Rx** (1)

  - `RxRenderStateVectorLoadDriverState`

