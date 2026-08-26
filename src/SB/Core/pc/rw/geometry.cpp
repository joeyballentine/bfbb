// RenderWare C API: RpGeometry, RpMaterial and RpMorphTarget.
//
// All three are mirrored onto librw's (see include/rwsdk/rpworld.h and
// layout_geometry.cpp), so an RpGeometry* IS an rw::Geometry* and most of what
// is here is a cast and a call. RpMorphTarget, RpTriangle and RpMaterialList
// needed no reordering at all: librw already lays them out the way RenderWare
// does, which matters because zFX.cpp walks morphTarget->verts and
// triangle->vertIndex by hand.

#include <rwcore.h>
#include <rpworld.h>

#include "rw.h"

static inline rw::Geometry* asGeometry(const RpGeometry* g)
{
    return const_cast<rw::Geometry*>(reinterpret_cast<const rw::Geometry*>(g));
}

static inline rw::Material* asMaterial(const RpMaterial* m)
{
    return const_cast<rw::Material*>(reinterpret_cast<const rw::Material*>(m));
}

RpGeometry* RpGeometryCreate(RwInt32 numVert, RwInt32 numTriangles, RwUInt32 format)
{
    // The format word carries the flags in its low byte and the texture
    // coordinate set count in bits 16-23, and librw splits it exactly the way
    // RenderWare does -- including deriving a count of 1 or 2 from
    // rpGEOMETRYTEXTURED/TEXTURED2 when the field is zero. Geometry::create
    // also allocates the single morph target RenderWare's does, so there is no
    // RpGeometryAddMorphTargets call to make here.
    return reinterpret_cast<RpGeometry*>(rw::Geometry::create(numVert, numTriangles, format));
}

RpGeometry* RpGeometryLock(RpGeometry* geometry, RwInt32 lockMode)
{
    if (geometry == NULL)
    {
        return NULL;
    }

    // Both sides record which parts were locked so that the renderer knows to
    // re-instance them, and both throw the mesh away when the polygons are
    // locked, because the index buffer is about to stop describing them.
    asGeometry(geometry)->lock(lockMode);
    return geometry;
}

RpGeometry* RpGeometryUnlock(RpGeometry* geometry)
{
    if (geometry == NULL)
    {
        return NULL;
    }

    // Rebuilds the mesh if the lock threw it away, which is the half of
    // RenderWare's unlock that matters here. It does NOT recompute the morph
    // targets' bounding spheres: RenderWare leaves that to
    // RpMorphTargetCalcBoundingSphere, which the callers that move vertices
    // (xCutscene.cpp, zFX.cpp) call for themselves.
    asGeometry(geometry)->unlock();
    return geometry;
}

RpGeometry* RpGeometryForAllMaterials(RpGeometry* geometry, RpMaterialCallBack fpCallBack,
                                      void* pData)
{
    if (geometry == NULL || fpCallBack == NULL)
    {
        return geometry;
    }

    RpMaterialList& matList = geometry->matList;
    for (RwInt32 i = 0; i < matList.numMaterials; i++)
    {
        if (fpCallBack(matList.materials[i], pData) == NULL)
        {
            // RenderWare stops early when the callback returns NULL.
            break;
        }
    }

    return geometry;
}

const RpGeometry* RpGeometryTriangleGetVertexIndices(const RpGeometry* geometry,
                                                     const RpTriangle* triangle, RwUInt16* vert1,
                                                     RwUInt16* vert2, RwUInt16* vert3)
{
    if (vert1 != NULL)
    {
        *vert1 = triangle->vertIndex[0];
    }
    if (vert2 != NULL)
    {
        *vert2 = triangle->vertIndex[1];
    }
    if (vert3 != NULL)
    {
        *vert3 = triangle->vertIndex[2];
    }

    return geometry;
}

const RpGeometry* RpGeometryTriangleSetVertexIndices(const RpGeometry* geometry,
                                                     RpTriangle* triangle, RwUInt16 vert1,
                                                     RwUInt16 vert2, RwUInt16 vert3)
{
    triangle->vertIndex[0] = vert1;
    triangle->vertIndex[1] = vert2;
    triangle->vertIndex[2] = vert3;

    return geometry;
}

RpMaterial* RpGeometryTriangleGetMaterial(const RpGeometry* geometry, const RpTriangle* triangle)
{
    // A triangle with no material carries an index RenderWare treats as -1 and
    // librw writes as 0xFFFF; the two are the same sixteen bits, and this is
    // the read that has to agree with both. Anything out of range is refused
    // rather than indexed, because a geometry streamed in with a material
    // count of zero still has triangles.
    RwInt32 index = triangle->matIndex;
    if (index < 0 || index >= geometry->matList.numMaterials)
    {
        return NULL;
    }

    return geometry->matList.materials[index];
}

RpGeometry* RpGeometryTriangleSetMaterial(RpGeometry* geometry, RpTriangle* triangle,
                                          RpMaterial* material)
{
    if (geometry == NULL || triangle == NULL)
    {
        return geometry;
    }

    if (material == NULL)
    {
        triangle->matIndex = -1;
        return geometry;
    }

    // RenderWare adds the material to the geometry's list if it is not already
    // in it, and that append is what takes the reference the geometry then
    // owns. librw's appendMaterial does both, and returns the index either
    // way, so finding it first is what keeps a material from being appended
    // twice when several triangles share it.
    rw::Geometry* geo = asGeometry(geometry);
    rw::Material* mat = asMaterial(material);

    RwInt32 index = geo->matList.findIndex(mat);
    if (index < 0)
    {
        index = geo->matList.appendMaterial(mat);
        if (index < 0)
        {
            return NULL;
        }
    }

    triangle->matIndex = (RwInt16)index;
    return geometry;
}

RpMaterial* RpMaterialSetTexture(RpMaterial* material, RwTexture* texture)
{
    if (material == NULL)
    {
        return NULL;
    }

    // Reference counted on both sides: setTexture drops the reference on the
    // texture that was there and takes one on the new one. zParPTank.cpp
    // depends on that -- it hands a texture it found in a dictionary to a
    // material and then lets the dictionary go.
    asMaterial(material)->setTexture(reinterpret_cast<rw::Texture*>(texture));
    return material;
}

const RpMorphTarget* RpMorphTargetCalcBoundingSphere(const RpMorphTarget* morphTarget,
                                                     RwSphere* boundingSphere)
{
    if (morphTarget == NULL || boundingSphere == NULL)
    {
        return morphTarget;
    }

    // Computed into the caller's sphere and NOT stored back into the morph
    // target, which is RenderWare's split: RpMorphTargetSetBoundingSphere is
    // the one that stores.
    //
    // librw seeds the bounding box from +/-1000000 rather than from the first
    // vertex, so a model whose vertices all sit outside that cube would come
    // back with a sphere covering the origin as well. Nothing in this game is
    // anywhere near it -- the largest level is a few thousand units across --
    // and the alternative is duplicating librw's loop to change one constant.
    rw::Sphere sphere =
        const_cast<rw::MorphTarget*>(reinterpret_cast<const rw::MorphTarget*>(morphTarget))
            ->calculateBoundingSphere();

    boundingSphere->center.x = sphere.center.x;
    boundingSphere->center.y = sphere.center.y;
    boundingSphere->center.z = sphere.center.z;
    boundingSphere->radius = sphere.radius;

    return morphTarget;
}

// Walk the meshes behind a mesh header.
//
// One call site, xJSP.cpp:35, and it is the one that turns a level's atomics
// into the flat strip-vertex array the JSP renderer draws from -- so getting
// the walk wrong would not crash, it would build the level out of the wrong
// vertices.
//
// The subtlety is where the meshes start, and it is recorded at the RpMesh
// asserts in layout_geometry.cpp: RenderWare stores a byte offset from the end
// of the header to the first mesh, librw hardcodes the meshes as immediately
// following the header (`Mesh *getMeshes(void) { return (Mesh*)(this+1); }`)
// and leaves that field as padding. So this must NOT add firstMeshOffset the
// way RenderWare's own implementation does -- on a librw mesh header the field
// holds whatever the reader left there, and adding it would walk off into the
// index data. Going through librw's accessor rather than writing
// `(RpMesh*)(meshHeader + 1)` is what keeps that true if librw ever moves them.
//
// The early-out on a NULL callback return is RenderWare's documented
// RpMeshCallBack contract and is what its other ForAll* functions do, but it is
// inferred rather than checked: src/rwsdk/world/bamesh.c is not decompiled.
// Nothing depends on it today -- xJSP's AddMeshCB returns its mesh every time.
RpMeshHeader* _rpMeshHeaderForAllMeshes(RpMeshHeader* meshHeader, RpMeshCallBack fpCallBack,
                                        void* pData)
{
    if (meshHeader == NULL || fpCallBack == NULL)
    {
        return meshHeader;
    }

    rw::MeshHeader* header = reinterpret_cast<rw::MeshHeader*>(meshHeader);
    RpMesh* mesh = reinterpret_cast<RpMesh*>(header->getMeshes());

    for (RwUInt32 i = 0; i < header->numMeshes; i++)
    {
        if (fpCallBack(mesh, meshHeader, pData) == NULL)
        {
            break;
        }

        mesh++;
    }

    return meshHeader;
}
