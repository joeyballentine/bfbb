# The 112 RenderWare functions, and what is done

Regenerate this list with:

    llvm-nm <every game object> | awk '$1=="U"{print $2}' \
      | sed 's/^?*//;s/@.*//;s/^_*//' | grep -E '^(Rw|Rp|Rt|Rx)' | sort -u

## Done (11)

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

## Blocked on the object-layout decision (101)

Every one of these takes or returns a RenderWare *object*, and ours and
librw's disagree on layout. Read README.md before starting any of them: both
answers to that question change what all 101 look like.

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

  - `RwFrameCreate`
  - `RwFrameDestroy`
  - `RwFrameGetLTM`
  - `RwFrameOrthoNormalize`
  - `RwFrameRotate`
  - `RwFrameTransform`
  - `RwFrameTranslate`
  - `RwFrameUpdateObjects`

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

