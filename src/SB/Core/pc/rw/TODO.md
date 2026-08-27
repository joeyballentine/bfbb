# The RenderWare functions, and what is done

Regenerate this list with:

    SHIM=<a directory of compiled shim .o files>
    # what the PC build references. Core/gc is excluded: it is GameCube-only.
    find build/GQPE78/obj/SB -name "*.o" ! -path "*/Core/gc/*" -print0 | xargs -0 llvm-nm       | awk '$1=="U"{print $2}' | sed 's/^?*//;s/@.*//'       | grep -E '^_*(Rw|Rp|Rt|Rx|rw|rp|rt|rx)' | sort -u > need.txt
    # what the game defines itself, and what the shim defines
    find build/GQPE78/obj/SB -name "*.o" -print0 | xargs -0 llvm-nm       | awk '$2!="U" && NF>=3 {print $3}' | sort -u > gamedef.txt
    llvm-nm $SHIM/*.o | awk '$2!="U" && $2!="a" && NF>=3 {print $3}'       | sed 's/^_//' | sort -u > have.txt
    comm -23 need.txt have.txt | comm -23 - gamedef.txt

**That command has been wrong twice, and each time it hid real work.**

The first version ended `s/^_*//' | grep -E '^(Rw|Rp|Rt|Rx)'`, and the strip and
the anchor together dropped every symbol RenderWare spells with a leading
underscore -- `_rwObjectHasFrameSetFrame` came out of the strip as a lowercase
`rw` that `^(Rw|Rp|Rt|Rx)` did not match. Four functions went missing that way.
The leading underscore is part of the name here; these are PowerPC objects, so
nothing prefixes a `_` of its own.

The second version fixed that but still only *listed* undefined symbols, with
no `have.txt` to subtract. Whoever read it ticked functions off by hand, and
three that nothing had ever written -- `RpAtomicDestroy`, `RpAtomicStreamRead`
and `RpAtomicStreamWrite` -- sat under a heading that said `**RpAtomic** (0)`
for four commits. Comparing against a real list of what the shim DEFINES is
what makes this list a measurement instead of a memory.

Two details that version also got wrong, both of which cost a false negative:

  - **Data symbols count.** `awk '$2=="T"'` misses `RwEngineInstance` and
    `_rpPTankAtomicDataOffset`, which are `B` and `D`. Filter on *not* `U`.
  - **COFF prefixes one underscore of its own**, so the shim's side needs
    `sed 's/^_//'` and the game's side does not.

As measured: **124 RenderWare symbols referenced by the PC build, 1 defined by
the game itself, 119 defined here, 4 left** -- and the four are listed under
"Do this next", where three of them turn out not to be this directory's job.

The list is no longer "112 functions"; that number came from the first,
broken command and was never right.

## Done
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
-- the game references directly; the other six are what it took to get there.)

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
The game never calls them by name, but `RWAttachPlugins` in iSystem.cpp
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
a crash. None of them is a missing function; all four need writing before
the port draws a frame that looks right.

**PTank particles are drawn.** *(was: invisible)* The instancing step needed the
camera's right and up vectors, and `RwEngineInstance->curCamera` used to be
permanently null; once `RwCameraBeginUpdate` began publishing it, the step
became writable and `ptankRenderCB` in `ptank.cpp` now builds a camera-facing
quad per particle -- position, size, colour, 2D rotation, and either the
two-corner or four-corner texture coordinates -- and marks the geometry dirty
so librw re-instances it. Until then every triangle was degenerate and every
particle in the game was invisible, the bubbles most visibly. Covered by
`test_ptank` in `tests/selftest.cpp`, which checks the four corners against
exact coordinates for an identity camera.

Not covered: the billboards are built from `rw::engine->currentCamera`, so a
ptank drawn outside a `BeginUpdate`/`EndUpdate` pair keeps the previous frame's
vertices rather than getting new ones. Nothing in the game does that today.

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

The world, as far as librw has one. `RpWorld` is mirrored onto `rw::World`,
with its offsets asserted in `layout_world.cpp` -- but it is the one type in the
layer that needed a PLUGIN to be mirrored at all. librw's own source calls
`World` "a bit of a stub" and it is not exaggerating: the whole struct is an
`Object` and three linked lists, against RenderWare's fifteen members. Eleven
have no counterpart, and two of the eleven are read by game code that has to
keep compiling -- `boundingBox` by `iEnvGetBBox` (an inline in `iEnv.h`, so it
reaches every unit that includes `xEnv.h`) and `matList` by xFX.cpp:425/443 and
zEnv.cpp:106/115 -- so the `RpAtomic` answer of dropping them was not available.
`RpWorldPluginAttach` registers the eleven as a librw World plugin instead, so
they live in memory librw allocated at an offset librw handed out, and both
`layout_world.cpp` and the attach itself check that offset is the one the struct
declaration assumes:

`world.cpp`:

  - `RpWorldCreate`
  - `RpWorldDestroy`
  - `RpWorldAddCamera`
  - `RpWorldRemoveCamera`
  - `RpWorldAddLight`
  - `RpWorldRemoveLight`
  - `RpWorldPluginAttach` -- never called by name from the game, but iSystem.cpp's
    `RWAttachPlugins` already calls it first of all seven attaches, in exactly
    the window it needs. Nothing may create a world before it runs, and
    `RpWorldCreate` checks rather than assumes.

`tests/selftest.cpp` runs all of it against a live engine, including the pair
that could not be checked before there was a world to check them with:
`RwCameraGetWorld` answering with the RpWorld the camera was added to, and
`RwCameraBeginUpdate` putting that world in `RwEngineInstance->curWorld`. The
two light lists are read back through RenderWare's OWN names -- librw's
"local"/"global" split is RenderWare's `lightList`/`directionalLightList`, and
getting the pair the wrong way round would light the world off the wrong set
without any other symptom.

### What the world path does NOT do yet

**There is no world reader, so there is no level.** `RpWorldStreamRead` returns
NULL, and every BSP asset comes through it (zAssetTypes.cpp:220). What is
missing is not a shim but a subsystem: librw has no world sector code of any
kind -- no `RpWorldSector` counterpart, no plane sectors, no world chunk reader,
and `World::render`'s own comment is "this is very wrong, we really want world
sectors". Writing it is two separable jobs:

  1. the portable world chunk -- header, material list, and the plane/atomic
     sector tree with its vertices, normals, prelit colours, texture coordinates
     and polygons. `rw::MaterialList::streamRead` already covers part of it.
  2. the GameCube native path, which is what BFBB's own BSPs actually are. A
     native world's atomic sectors carry no portable geometry; the real data is
     a GameCube display list in a platform extension chunk, and librw has PS2,
     D3D and OpenGL pipelines but no GameCube one. iFX.cpp:107 reading
     `_rpDlWorldVtxFmtOffset` off the current world is the game's own evidence
     for which kind it is loading.

A partial reader was considered and rejected: filling in the material list and
leaving the geometry empty would report success and hand back a level with
nothing in it. NULL is honest and the game is already loud about it -- BSP_Read
prints "BSP_Read RpWorldStreamRead failed".

**`RpCollisionWorldForAllIntersections` is blocked on the same thing**, which is
why `collision_world.cpp` is a file with one refusal in it and a long comment.
The maths it needs already exists -- atomic.cpp's sphere, segment and box
triangle tests, written for `RpAtomicForAllIntersections` -- but there is
nothing to run it over, because `rootSector` is always NULL. Until both are
written, everything in the game walks through the level geometry and falls
through the floor. Model-to-model collision is unaffected: that is
`RpAtomicForAllIntersections`, and xClumpColl and the JSP path go through it.

**`RpWorldSector` is not mirrored, and deliberately so.** There is no librw type
to mirror it onto, so it keeps RenderWare's own layout untouched and nothing
allocates one. `layout_world.cpp` asserts `RpPolygon` instead, because
iCollide.cpp and xCollide.cpp read `sector->polygons[tri->index].matIndex` and
that stride is what the whole collision system's object ids come out of.

**Nothing maintains most of RpWorld's plugin tail.** `boundingBox`, `matList`
and `renderOrder` are real; `rootSector`, `numClumpsInWorld`, `currentClumpLink`,
`numTexCoordSets`, `worldOrigin`, `flags`, `renderCallBack` and `pipeline` stay
at the zeros the plugin constructor wrote, because nothing in the port writes
them and nothing in the game reads them. A world reader will change that.

**`RpWorldDestroy` detaches where RenderWare dangles.** Retail leaves whatever
is still in a world holding a link into freed memory; librw notices later, by
asserting when the light or clump is destroyed. So the shim removes every clump
and light first. Cameras are the one thing it cannot find -- neither library
keeps a list of them, only a `World*` on the camera -- and the game already
removes those by hand (iCamera.cpp:54, zGame.cpp:1476).
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

The three functions the corrected command turned up, and the three the
comparison against `have.txt` turned up. Six functions found by fixing the
measurement rather than by reading the code, which is the argument for the
command at the top of this file.

`frame.cpp`:

  - `_rwFrameSyncDirty` -- seven sites. A frame that moves does NOT recompute
    its LTM; `RwFrameUpdateObjects` marks the root dirty and puts it on a list,
    and this is what flushes it. Anything reading an LTM without going through
    `RwFrameGetLTM` has to call it first, which is why iCamera.cpp:52 and
    xModelBucket.cpp:166/622 do. librw's `Frame::syncDirty` is RenderWare's step
    for step, down to emptying the list at the end.

`value.cpp`:

  - `_rwInvSqrt` -- three sites, plus the `rwInvSqrtMacro` in xCollide.cpp:29.
    `1.0f / sqrtf(num)`, and **the zero case is load-bearing**: xCollide.cpp:1413
    scales a degenerate triangle's zero-length normal by this and then rejects
    the triangle with `isnan(xnorm.x)`. That only fires because a zero input
    returns +infinity and `0 * inf` is NaN. Guarding the division -- the obvious
    defensive edit -- silently turns every degenerate triangle into a hit with
    no surface direction. The selftest checks the infinity for that reason.

`geometry.cpp`:

  - `_rpMeshHeaderForAllMeshes` -- one site, xJSP.cpp:35, and it is the walk
    that turns a level's atomics into the flat strip-vertex array the JSP
    renderer draws from. RenderWare starts the meshes `firstMeshOffset` bytes
    past the header and librw puts them immediately after it, leaving that field
    as padding -- so this must NOT add it. The selftest stamps `firstMeshOffset`
    with a value that would walk off the end if anything ever "restores"
    RenderWare's arithmetic.

`atomic.cpp` -- all three exist for one caller, `FullAtomicDupe`
(xModelBucket.cpp:125), which duplicates an atomic by writing it to a memory
stream and reading it back N times. The shim had already written a
grow-on-demand memory stream *for that function* without anyone noticing the
other half was missing:

  - `RpAtomicDestroy`
  - `RpAtomicStreamWrite`
  - `RpAtomicStreamRead`

  The standalone atomic chunk is NOT the atomic chunk inside a clump, and librw
  only has the second: `Atomic::streamReadClump` and `streamWriteClump` name the
  frame and the geometry by INDEX into a clump's lists, so neither can be called
  without a clump. `RpAtomicStreamGetSize` IS decompiled
  (src/rwsdk/world/baclump.c:269) and settles the standalone layout -- a STRUCT,
  then a whole GEOMETRY chunk unconditionally, then the plugin extension.

  The extension is the part that would be easy to skip and expensive to skip:
  RpSkin and RpMatFX hang off an atomic, xModelBucket duplicates skinned models,
  and dropping it would hand back atomics that render unskinned -- which reads
  as a bug in the animation system rather than as a missing chunk.

  `RpAtomicDestroy` detaches from any clump and world first. librw asserts on
  both; RenderWare frees the atomic and leaves the list holding a dangling link.
  Same choice, and the same reasoning, as `RpWorldDestroy` in world.cpp.

### One bug this comparison found that was not a missing function

`RwGameCubeSetMinRetraceCount` was **defined, and would not have linked.** It is
the one RenderWare function in this directory with no declaration in
include/rwsdk -- zGame.cpp:78 declares it itself, inside an `extern "C" { }`
block, because on the console it comes out of a GameCube driver library. So the
definition in engine.cpp had C++ linkage and the call in zGame.cpp:697 would not
have resolved to it: `?RwGameCubeSetMinRetraceCount@@YAXE@Z` against
`_RwGameCubeSetMinRetraceCount`. It now says `extern "C"`.

Worth knowing because nothing in the source looks wrong, and the error names a
symbol that is visibly present in the object file.

## The render backend (D3D9, as of 2026-08-26)

The port targets **D3D9** and keeps GL3 reachable. Three things that were
blockers here are done:

  - **`RwEngineOpen` builds real `EngineOpenParams`.** rw::d3d's is
    `{ HWND window; }`, taken from the port's own window seam
    (`src/SB/Core/pc/iWindow.h`, `iWindowWin32.cpp`). RenderWare's `displayID`
    is a `RwGameCubeDeviceConfig*` and had nothing to translate, so initParams
    is ignored. The GL3 arm still `#error`s on purpose: GL3's params differ by
    whether librw was built against GLFW, SDL2 or SDL3, and there is nothing
    sensible to write before that choice is made. That arm is what a second
    backend costs, and it is small.
  - **`RwIm2DVertex` is the backend's layout.** rwplcore.h declares it per
    backend, `layout_im2d.cpp` asserts it against the backend's own struct, and
    `RwIm2DVertexSetRecipCameraZ` writes the `w` instead of being nothing.
  - **Rasters carry real pixels.** `RwTexDictionaryStreamRead` calls
    `Raster::convertTexToCurrentPlatform` on every texture it reads. librw HAS
    that conversion, with working xbox_to_d3d and xbox_to_gl3 paths, and calls
    it **from nowhere at all** -- it is the application's job, and RenderWare
    hid it on a console because the native format WAS the device's format.
    Without it every texture reads successfully and stays blank, which looks
    exactly like a missing backend.

Four things a real device needs that the null one never did. All four were
found by the selftest FAULTING, not by reading code, which is the argument for
running the shim rather than compiling it:

  - A **window** must exist before `RwEngineOpen`.
  - A camera needs a **frame** before `RwCameraBeginUpdate`: d3ddevice.cpp:1228
    opens with `Matrix::invert(&inv, cam->getFrame()->getLTM())`, so a frameless
    camera dereferences NULL inside the driver -- no error, no return value.
  - A camera needs a **frame buffer and z buffer raster**, the pair
    iCamera.cpp:27-28 attaches. This is also the first exercise of
    `RwRasterCreate`'s success path, which NULL could never reach.
  - A 2D vertex whose **`w` is zero draws nothing**: librw's im2d vertex shader
    multiplies the position by w before the hardware divides by it. Most of the
    game's 2D call sites never set a camera z, so `RwIm2DVertex`'s default
    constructor puts 1.0 there.

`tests/selftest.cpp` runs **513 checks against a live D3D9 device** and 507
against `LIBRW_PLATFORM=NULL`. Keeping the headless configuration working is
what lets this layer be tested on a machine with no display, and it is a
supported configuration rather than a leftover.

**Two of the 507 FAIL, and have since the colour write mask went in.** They are
`the colour write mask lets every channel through` and `and can leave alpha on
with colour off`, and the cause is that they read the state back with
`rw::GetRenderState` -- which asks the DEVICE, and the null device answers 0 to
every question, exactly as the comment at the top of `renderstate.cpp` says.
The fix is to check them through the captured `device.setRenderState` hook
instead, which is how the alpha compare below is checked and why that one passes
on both backends. Left alone here because it is a different change from the one
that found it. The count above is what the D3D9 build gates on; the headless
build is 507 checks and 2 failures until someone does that.

## Do this next

Four symbols the PC build references and this directory did not define. Only one
of them is still open, and saying which is which is the point of this section --
the previous version of this file listed all four kinds of gap together and they
need different people.

**`_rpCollisionGeometryDataOffset` -- the shim's job, and a subsystem.** The
plugin offset behind `RpCollisionGeometryGetData`, which xCollide.cpp:2062/2093
and xShadow.cpp:1250/1454 use unguarded in portable `Core/x` code. It is the
same missing piece as `RpAtomicForAllIntersections` being a linear scan and
`RpCollisionPluginAttach` being unwritten: RenderWare hangs a collision BSP tree
off a geometry as a plugin, and librw has no collision code at all. Writing it
means the plugin, its stream reader, and the tree walk -- at which point
`RpAtomicForAllIntersections` stops being O(triangles) as well.

**`RwGameCubeSetAlphaCompare` -- WRITTEN, and no longer a gap.** Both it and
`_rwDlRenderStateSetZCompLoc` are GameCube driver entry points called UNGUARDED
from portable code (xModelBucket.cpp:524/530/600 and 526/531/601), and this
section used to argue they were not the shim's job because there was nothing on
a host to forward to. Defining them in engine.cpp is what let all 198 units
compile at the width the port builds at -- `python tools/pcprogress.py --m32
--cc clang++`.

The z-compare one is CORRECT as a no-op, for the reason engine.cpp gives on it.
The alpha compare was NOT -- it is cutout transparency, foliage and fences and
grates and chain link -- and it now forwards to librw's `ALPHATESTFUNC` and
`ALPHATESTREF`, which the D3D9 backend carries through to `D3DRS_ALPHAFUNC` and
`D3DRS_ALPHAREF`. The fork needed no change for it. GX's two-comparison form
reduces exactly for both of the states xModelBucket produces, because ALWAYS is
the identity for AND; seven checks in `test_renderstate` pin the reduction down,
including the two the game actually asks for.

What was missing turned out to be narrower than "the alpha was ignored"
suggested, and it is worth knowing before anyone judges the visual change:
D3D9 turns `D3DRS_ALPHATESTENABLE` on by itself out of whether the texture or
material has alpha, and `initD3D` leaves the function at GREATEREQUAL **10**. So
alpha-keyed geometry was already being cut out, at a fixed 10 rather than at the
threshold in the model. A bucket asking for 128 got 10, which keeps the
half-transparent texels the console dropped -- soft haloes round leaves rather
than solid quads.

**`_rpAtomicResyncInterpolatedSphere` -- already handled, and listed here only
so the next person does not chase it.** It appears in the regenerated list
because the objects being scanned are GameCube builds; rpworld.h:360 guards the
macro that calls it behind `#ifndef PLATFORM_PC`, and the PC spelling of
`RpAtomicGetBoundingSphereMacro` does not need it. librw does not interpolate
morph targets, so the sphere is never stale.

## Still unwritten, though the symbol exists

Two functions that link, and return NULL. They are one job, not two, and it is
the same job as the world reader -- see "What the world path does NOT do yet".

  - `RpWorldStreamRead` (world.cpp) -- every BSP asset comes through it
    (zAssetTypes.cpp:220). librw has no world sector code of any kind.
  - `RpCollisionWorldForAllIntersections` (collision_world.cpp) -- the maths it
    needs already exists in intersect.cpp; there is nothing to run it over,
    because `rootSector` is always NULL.

**Neither is on the Xbox asset path.** The Xbox packs carry 0 BSP and 110 JSP
across all 121 files, and `iEnv.cpp:61` gives a JSP level an empty
`RpWorldCreate(&tmpbbox)` and takes its collision from
`xClumpColl_InstancePointers` instead. See PCPORT.md. So these block the
GameCube assets and not the ones the port is being built against.

The per-group tally that used to live here has been removed: every group in it
read (0) except these two, and it was a second place to keep the same list up
to date. The command at the top of this file is the tally now.
