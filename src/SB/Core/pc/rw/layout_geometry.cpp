// Layout assertions for the model path: geometries, materials, atomics, and
// everything reachable only from them.
//
// Same job as layout.cpp -- no code, just the claims the reinterpret_casts in
// geometry.cpp, atomic.cpp, skin.cpp, matfx.cpp and ptank.cpp rest on, checked
// by the compiler. Split into its own file so this group can be worked on
// independently; read layout.cpp's header comment for why any of this exists.
//
// The offsets are librw's, taken from a throwaway program that printed
// offsetof for every member rather than read off rwobjects.h, because that
// header is full of macros and static members that make eyeballing it
// unreliable. PLUGINBASE in particular looks like a data member and is not:
// it expands to statics and methods only, so it contributes nothing to any
// layout here. librw's plugin data lives BEHIND the struct, at offsets handed
// out by registerPlugin -- which is why every sizeof below is the plain struct
// size and why appending a field to one of these would land in the plugin
// block rather than past the end of the allocation.
//
// RpSkin, RpMatFX and RpPTankAtomicExtPrv are deliberately absent. The first
// two are opaque to the game -- rpskin.h never defines RpSkin, and every
// RpMatFX access goes through a function -- so there is no layout claim to
// check. The third is the port's own struct on both sides of the seam; it is
// not a mirror of anything librw has, because librw has no PTank at all.

#include <rwcore.h>
#include <rpmatfx.h>
#include <rpskin.h>
#include <rpworld.h>

#include "rw.h"

#include <stddef.h>

#define SAME_SIZE(ours, theirs)                                                                    \
    static_assert(sizeof(ours) == sizeof(theirs), #ours " and " #theirs " differ in size")

#define SAME_OFFSET(ours, ourfield, theirs, theirfield)                                            \
    static_assert(offsetof(ours, ourfield) == offsetof(theirs, theirfield),                        \
                  #ours "." #ourfield " is not where " #theirs "." #theirfield " is")

// --- the building blocks ---------------------------------------------------
//
// RwObjectHasFrame is asserted here rather than in layout.cpp because RpAtomic
// leads with one and every offset below it depends on the size being right.
// It needs no PC variant: librw's ObjectWithFrame already agrees field for
// field, and this is what stops that from being a comment nobody rechecks.

SAME_SIZE(RwObjectHasFrame, rw::ObjectWithFrame);
SAME_OFFSET(RwObjectHasFrame, object, rw::ObjectWithFrame, object);
SAME_OFFSET(RwObjectHasFrame, lFrame, rw::ObjectWithFrame, inFrame);
SAME_OFFSET(RwObjectHasFrame, sync, rw::ObjectWithFrame, syncCB);

SAME_SIZE(RwSphere, rw::Sphere);
SAME_OFFSET(RwSphere, center, rw::Sphere, center);
SAME_OFFSET(RwSphere, radius, rw::Sphere, radius);

SAME_SIZE(RwSurfaceProperties, rw::SurfaceProperties);
SAME_OFFSET(RwSurfaceProperties, ambient, rw::SurfaceProperties, ambient);
SAME_OFFSET(RwSurfaceProperties, specular, rw::SurfaceProperties, specular);
SAME_OFFSET(RwSurfaceProperties, diffuse, rw::SurfaceProperties, diffuse);

SAME_SIZE(RwTexCoords, rw::TexCoords);
SAME_OFFSET(RwTexCoords, u, rw::TexCoords, u);
SAME_OFFSET(RwTexCoords, v, rw::TexCoords, v);

// --- RpMaterial ------------------------------------------------------------

SAME_SIZE(RpMaterial, rw::Material);
SAME_OFFSET(RpMaterial, texture, rw::Material, texture);
SAME_OFFSET(RpMaterial, color, rw::Material, color);
SAME_OFFSET(RpMaterial, surfaceProps, rw::Material, surfaceProps);
SAME_OFFSET(RpMaterial, pipeline, rw::Material, pipeline);
SAME_OFFSET(RpMaterial, refCount, rw::Material, refCount);

// RenderWare's refCount is an RwInt16 with an RwInt16 of padding after it, so
// the offsets match whichever way it is spelled. The width is what matters:
// librw writes all four bytes, and a 16-bit read of them would happen to work
// on x86 until a material's reference count passed 32767 -- which the world's
// shared materials plausibly could.
static_assert(sizeof(((RpMaterial*)0)->refCount) == sizeof(((rw::Material*)0)->refCount),
              "RpMaterial.refCount is not as wide as rw::Material.refCount");

// --- RpMaterialList --------------------------------------------------------
//
// Already identical, hence no PC variant.

SAME_SIZE(RpMaterialList, rw::MaterialList);
SAME_OFFSET(RpMaterialList, materials, rw::MaterialList, materials);
SAME_OFFSET(RpMaterialList, numMaterials, rw::MaterialList, numMaterials);
SAME_OFFSET(RpMaterialList, space, rw::MaterialList, space);

// --- RpTriangle ------------------------------------------------------------
//
// Also already identical. RenderWare's matIndex is signed and librw's matId is
// not; both are sixteen bits in the same place, and _rpMaterialListGetMaterial
// treats a negative index as "no material" on both sides.

SAME_SIZE(RpTriangle, rw::Triangle);
SAME_OFFSET(RpTriangle, vertIndex, rw::Triangle, v);
SAME_OFFSET(RpTriangle, matIndex, rw::Triangle, matId);
static_assert(sizeof(((RpTriangle*)0)->vertIndex) == sizeof(((rw::Triangle*)0)->v),
              "a triangle does not have three 16-bit indices on both sides");

// --- RpMorphTarget ---------------------------------------------------------
//
// Identical too, which is worth saying out loud: zFX.cpp reads
// geom->morphTarget->verts and ->normals directly at a dozen sites.

SAME_SIZE(RpMorphTarget, rw::MorphTarget);
SAME_OFFSET(RpMorphTarget, parentGeom, rw::MorphTarget, parent);
SAME_OFFSET(RpMorphTarget, boundingSphere, rw::MorphTarget, boundingSphere);
SAME_OFFSET(RpMorphTarget, verts, rw::MorphTarget, vertices);
SAME_OFFSET(RpMorphTarget, normals, rw::MorphTarget, normals);

// --- RpMesh and RpMeshHeader -----------------------------------------------
//
// xJSP.cpp walks these by hand -- atomic->geometry->mesh->totalIndicesInMesh
// and mesh->indices[i] -- so they need checking even though neither needed a
// PC variant.
//
// firstMeshOffset is the one place the two disagree in meaning rather than in
// layout: RenderWare stores the byte offset from the header to the first mesh
// and librw hardcodes it, putting the meshes immediately after the header
// (its getMeshes() is `(Mesh*)(this+1)`) and leaving the field as padding.
// _rpMeshHeaderForAllMeshes on this side must therefore not add it.

SAME_SIZE(RpMesh, rw::Mesh);
SAME_OFFSET(RpMesh, indices, rw::Mesh, indices);
SAME_OFFSET(RpMesh, numIndices, rw::Mesh, numIndices);
SAME_OFFSET(RpMesh, material, rw::Mesh, material);

SAME_SIZE(RpMeshHeader, rw::MeshHeader);
SAME_OFFSET(RpMeshHeader, flags, rw::MeshHeader, flags);
SAME_OFFSET(RpMeshHeader, numMeshes, rw::MeshHeader, numMeshes);
SAME_OFFSET(RpMeshHeader, serialNum, rw::MeshHeader, serialNum);
SAME_OFFSET(RpMeshHeader, totalIndicesInMesh, rw::MeshHeader, totalIndices);
SAME_OFFSET(RpMeshHeader, firstMeshOffset, rw::MeshHeader, pad);

// --- RpGeometry ------------------------------------------------------------

SAME_SIZE(RpGeometry, rw::Geometry);
SAME_OFFSET(RpGeometry, object, rw::Geometry, object);
SAME_OFFSET(RpGeometry, flags, rw::Geometry, flags);
SAME_OFFSET(RpGeometry, lockedSinceLastInst, rw::Geometry, lockedSinceInst);
SAME_OFFSET(RpGeometry, numTriangles, rw::Geometry, numTriangles);
SAME_OFFSET(RpGeometry, numVertices, rw::Geometry, numVertices);
SAME_OFFSET(RpGeometry, numMorphTargets, rw::Geometry, numMorphTargets);
SAME_OFFSET(RpGeometry, numTexCoordSets, rw::Geometry, numTexCoordSets);
SAME_OFFSET(RpGeometry, triangles, rw::Geometry, triangles);
SAME_OFFSET(RpGeometry, preLitLum, rw::Geometry, colors);
SAME_OFFSET(RpGeometry, texCoords, rw::Geometry, texCoords);
SAME_OFFSET(RpGeometry, morphTarget, rw::Geometry, morphTargets);
SAME_OFFSET(RpGeometry, matList, rw::Geometry, matList);
SAME_OFFSET(RpGeometry, mesh, rw::Geometry, meshHeader);
SAME_OFFSET(RpGeometry, instData, rw::Geometry, instData);
SAME_OFFSET(RpGeometry, refCount, rw::Geometry, refCount);

static_assert(sizeof(((RpGeometry*)0)->texCoords) == sizeof(((rw::Geometry*)0)->texCoords),
              "a geometry does not have eight texture coordinate sets on both sides");

// RpGeometryCreate and RpGeometryLock pass RenderWare's format and lock words
// straight through, so the two sides have to agree on what the bits mean.
static_assert((int)rpGEOMETRYTRISTRIP == (int)rw::Geometry::TRISTRIP, "geometry TRISTRIP differs");
static_assert((int)rpGEOMETRYPOSITIONS == (int)rw::Geometry::POSITIONS,
              "geometry POSITIONS differs");
static_assert((int)rpGEOMETRYTEXTURED == (int)rw::Geometry::TEXTURED, "geometry TEXTURED differs");
static_assert((int)rpGEOMETRYPRELIT == (int)rw::Geometry::PRELIT, "geometry PRELIT differs");
static_assert((int)rpGEOMETRYNORMALS == (int)rw::Geometry::NORMALS, "geometry NORMALS differs");
static_assert((int)rpGEOMETRYLIGHT == (int)rw::Geometry::LIGHT, "geometry LIGHT differs");
static_assert((int)rpGEOMETRYMODULATEMATERIALCOLOR == (int)rw::Geometry::MODULATE,
              "geometry MODULATE differs");
static_assert((int)rpGEOMETRYTEXTURED2 == (int)rw::Geometry::TEXTURED2,
              "geometry TEXTURED2 differs");
static_assert((int)rpGEOMETRYNATIVE == (int)rw::Geometry::NATIVE, "geometry NATIVE differs");

// The lock flags are not spelled in any header the port has -- xCutscene.cpp
// passes a bare 2 and zEntCruiseBubble.cpp a bare 0x10 -- so these check the
// numbers the call sites actually use against what librw does with them.
static_assert((int)rw::Geometry::LOCKVERTICES == 2, "a bare 2 is no longer LOCKVERTICES");
static_assert((int)rw::Geometry::LOCKTEXCOORDS1 == 0x10, "a bare 0x10 is no longer LOCKTEXCOORDS1");
static_assert((int)rw::Geometry::LOCKPOLYGONS == 1, "LOCKPOLYGONS moved");

// iMorph.cpp's MorphCommon builds its lock word as `(useNormals ? 4 : 0) | 2`,
// so a bare 4 has to keep meaning LOCKNORMALS. Not to be confused with
// rpGEOMETRYNORMALS, which is a different constant that happens to share a
// value with LOCKTEXCOORDS1 above.
static_assert((int)rw::Geometry::LOCKNORMALS == 4, "a bare 4 is no longer LOCKNORMALS");

// --- RpAtomic --------------------------------------------------------------
//
// The mirror that loses fields; rpworld.h says which four and why. What is
// asserted here is that everything librw and RenderWare DO share sits at the
// same offset, and that the struct is librw's size -- because librw allocates
// it, and a bigger RpAtomic would write past the allocation into the plugin
// block that RpPTank and RpSkin store their per-atomic data in.

SAME_SIZE(RpAtomic, rw::Atomic);
SAME_OFFSET(RpAtomic, object, rw::Atomic, object);
SAME_OFFSET(RpAtomic, geometry, rw::Atomic, geometry);
SAME_OFFSET(RpAtomic, boundingSphere, rw::Atomic, boundingSphere);
SAME_OFFSET(RpAtomic, worldBoundingSphere, rw::Atomic, worldBoundingSphere);
SAME_OFFSET(RpAtomic, clump, rw::Atomic, clump);
SAME_OFFSET(RpAtomic, inClumpLink, rw::Atomic, inClump);
SAME_OFFSET(RpAtomic, pipeline, rw::Atomic, pipeline);
SAME_OFFSET(RpAtomic, renderCallBack, rw::Atomic, renderCB);
SAME_OFFSET(RpAtomic, world, rw::Atomic, world);
SAME_OFFSET(RpAtomic, originalSync, rw::Atomic, originalSync);

static_assert((int)rpATOMICCOLLISIONTEST == (int)rw::Atomic::COLLISIONTEST,
              "atomic COLLISIONTEST differs");
static_assert((int)rpATOMICRENDER == (int)rw::Atomic::RENDER, "atomic RENDER differs");

// --- RpSkin ----------------------------------------------------------------
//
// RpSkin itself is opaque, but the shapes its accessors hand back are not, and
// each of those three casts is a claim about librw's flat float and byte
// arrays. The one that would fail silently is the matrix stride: librw stores
// sixteen floats per bone in a float*, and RpSkinGetSkinToBoneMatrices returns
// that as an RwMatrix*.

static_assert(sizeof(RwMatrix) == 16 * sizeof(float),
              "RpSkinGetSkinToBoneMatrices strides wrong: librw stores 16 floats per bone");
static_assert(sizeof(RwMatrixWeights) == 4 * sizeof(float),
              "RpSkinGetVertexBoneWeights strides wrong: librw stores 4 floats per vertex");
static_assert(sizeof(RwUInt32) == 4 * sizeof(RwUInt8),
              "RpSkinGetVertexBoneIndices packs four of librw's index bytes per word");

// --- RpMatFX ---------------------------------------------------------------
//
// No struct is shared, but the effect identifiers are: RpMatFXMaterialSetEffects
// hands RenderWare's enumerator straight to librw, and RpMatFXMaterialGetEffects
// hands librw's back as RenderWare's.

static_assert((int)rpMATFXEFFECTNULL == (int)rw::MatFX::NOTHING, "matfx NULL differs");
static_assert((int)rpMATFXEFFECTBUMPMAP == (int)rw::MatFX::BUMPMAP, "matfx BUMPMAP differs");
static_assert((int)rpMATFXEFFECTENVMAP == (int)rw::MatFX::ENVMAP, "matfx ENVMAP differs");
static_assert((int)rpMATFXEFFECTBUMPENVMAP == (int)rw::MatFX::BUMPENVMAP,
              "matfx BUMPENVMAP differs");
static_assert((int)rpMATFXEFFECTDUAL == (int)rw::MatFX::DUAL, "matfx DUAL differs");
static_assert((int)rpMATFXEFFECTUVTRANSFORM == (int)rw::MatFX::UVTRANSFORM,
              "matfx UVTRANSFORM differs");
static_assert((int)rpMATFXEFFECTDUALUVTRANSFORM == (int)rw::MatFX::DUALUVTRANSFORM,
              "matfx DUALUVTRANSFORM differs");
