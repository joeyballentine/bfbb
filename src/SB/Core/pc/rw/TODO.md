# The 112 RenderWare functions, and what is done

Regenerate this list with:

    llvm-nm <every game object> | awk '$1=="U"{print $2}' \
      | sed 's/^?*//;s/@.*//;s/^_*//' | grep -E '^(Rw|Rp|Rt|Rx)' | sort -u

## Done (19)

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

## Do this next: librw's engine startup

Nothing here can *run* until librw's engine is up. `rw::Frame::create()` goes
through the plugin system, and a test that links cleanly still faults without
it -- `rw::Engine::init()` alone is not enough; there is memory-function setup,
plugin registration, then open and start.

That maps onto the `RwEngine` group below plus RenderWare's `RwEngineInit` /
`RwEngineOpen` / `RwEngineStart`, and it gates every other group, so it is the
next thing to write rather than more object wrappers.

Verified working today: the shim compiles, and it LINKS against a 32-bit librw
built with clang. Getting there needs the CRT configuration to match librw's
own, which CMake will handle once librw is vendored; by hand it wants
`-D_DEBUG -D_DLL -D_MT`, `--dependent-lib=msvcrtd,ucrtd,vcruntimed` and
`/NODEFAULTLIB:libucrt.lib`.

## Blocked on the object-layout decision (101)

RESOLVED -- the port mirrors librw's layouts (method 1), so these are no longer
blocked on a decision, only unwritten. Each needs its type mirrored in
include/rwsdk with matching static_asserts in layout.cpp, in the same commit.
What is left of 101 look like.

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

  - `RwEngineGetCurrentVideoMode`
  - `RwEngineGetVideoModeInfo`
  - `RwEngineInstance`

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

