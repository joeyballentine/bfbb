// RenderWare C API: RpCollisionWorld.
//
// RpCollisionWorldForAllIntersections is the environment half of collision
// for a BSP level: iCollide.cpp, xCollide.cpp, xScene.cpp and the shadow code
// ask it for the triangles of env->world that a sphere, line or box touches. A
// JSP level's world is an empty RpWorldCreate one, so there it finds nothing,
// and the JSP's own tree answers instead (xClumpColl.cpp).
//
// It follows GameCube's RenderWare (ctworld.c in the retail DOL):
//
//   * The plane sectors are descended with their leftValue and rightValue,
//     left first.
//   * An atomic sector without collision data is skipped, for every query.
//   * A sphere's distance is divided by its radius. A line's is the parameter
//     along it, from 0 at the start to 1 at the end, and only triangles that
//     face the start are hit: iRayHitsEnv casts the line both ways for that
//     reason. A box's is 0.
//   * The normal is normalize(cross(v1 - v0, v2 - v0)), except a sphere's,
//     which RtIntersectionSphereTriangle gives.
//   * A callback returning NULL ends the query.
//
// GameCube walks each sector's collision tree, where this tests every polygon
// in the sector. Both reach the same triangles; only the order differs.

#include <rwcore.h>
#include <rpcollis.h>
#include <rpcollbsptree.h>
#include <rpworld.h>
#include <rtintsec.h>

#include "intersect.h"
#include "rw.h"
#include "sector.h"

namespace
{
    struct WorldQuery
    {
        RpIntersection* intersection;
        RpIntersectionCallBackWorldTriangle callBack;
        void* data;

        // Bounds the primitive, for descending the plane sectors and for
        // skipping triangles nowhere near it.
        RwBBox box;

        RwV3d lineDelta;
        RwReal recipRadius;
    };
} // namespace

static bool triangleOutsideBox(const RwV3d* a, const RwV3d* b, const RwV3d* c, const RwBBox& box)
{
    return (a->x < box.inf.x && b->x < box.inf.x && c->x < box.inf.x) ||
           (a->x > box.sup.x && b->x > box.sup.x && c->x > box.sup.x) ||
           (a->y < box.inf.y && b->y < box.inf.y && c->y < box.inf.y) ||
           (a->y > box.sup.y && b->y > box.sup.y && c->y > box.sup.y) ||
           (a->z < box.inf.z && b->z < box.inf.z && c->z < box.inf.z) ||
           (a->z > box.sup.z && b->z > box.sup.z && c->z > box.sup.z);
}

// Moller-Trumbore against the triangles that face the line's start, with
// RenderWare's tolerances: a hit up to 1e-5 of the way outside an edge or past
// an end of the line still counts.
static bool lineHitsTriangle(const RwV3d* start, const RwV3d* delta, const RwV3d* v0,
                             const RwV3d* v1, const RwV3d* v2, RwReal* distance)
{
    RwV3d edge1, edge2, pVec, tVec, qVec;
    RwV3dSubMacro(&edge1, v1, v0);
    RwV3dSubMacro(&edge2, v2, v0);
    RwV3dCrossProductMacro(&pVec, delta, &edge2);
    RwReal det = RwV3dDotProductMacro(&edge1, &pVec);
    if (!(det > 1e-8f))
    {
        return false;
    }

    RwReal lo = 1e-5f * -det;
    RwReal hi = det - lo;

    RwV3dSubMacro(&tVec, start, v0);
    RwReal u = RwV3dDotProductMacro(&tVec, &pVec);
    if (u < lo || u > hi)
    {
        return false;
    }

    RwV3dCrossProductMacro(&qVec, &tVec, &edge1);
    RwReal v = RwV3dDotProductMacro(delta, &qVec);
    if (v < lo || u + v > hi)
    {
        return false;
    }

    RwReal t = RwV3dDotProductMacro(&edge2, &qVec);
    if (t < lo || t > hi)
    {
        return false;
    }

    *distance = t / det;
    return true;
}

static bool querySector(WorldQuery* query, RpWorldSector* sector)
{
    RpIntersection* isx = query->intersection;

    for (RwInt32 i = 0; i < sector->numPolygons; i++)
    {
        const RpPolygon& polygon = sector->polygons[i];
        RwV3d* v0 = &sector->vertices[polygon.vertIndex[0]];
        RwV3d* v1 = &sector->vertices[polygon.vertIndex[1]];
        RwV3d* v2 = &sector->vertices[polygon.vertIndex[2]];

        RpCollisionTriangle tri;
        RwReal distance;
        switch (isx->type)
        {
        case rpINTERSECTSPHERE:
            if (triangleOutsideBox(v0, v1, v2, query->box) ||
                !RtIntersectionSphereTriangle(&isx->t.sphere, v0, v1, v2, &tri.normal, &distance))
            {
                continue;
            }
            distance *= query->recipRadius;
            break;

        case rpINTERSECTLINE:
            if (!lineHitsTriangle(&isx->t.line.start, &query->lineDelta, v0, v1, v2, &distance))
            {
                continue;
            }
            rwshim::triangleNormal(v0, v1, v2, &tri.normal);
            break;

        default: // rpINTERSECTBOX
            if (!RtIntersectionBBoxTriangle(&isx->t.box, v0, v1, v2))
            {
                continue;
            }
            distance = 0.0f;
            rwshim::triangleNormal(v0, v1, v2, &tri.normal);
            break;
        }

        tri.point = *v0;
        tri.index = i;
        tri.vertices[0] = v0;
        tri.vertices[1] = v1;
        tri.vertices[2] = v2;

        if (query->callBack(isx, sector, &tri, distance, query->data) == NULL)
        {
            return false;
        }
    }

    return true;
}

static bool querySectors(WorldQuery* query, RpSector* sector)
{
    if (sector->type == rwSECTORATOMIC)
    {
        rwshim::AtomicSector* atomicSector = (rwshim::AtomicSector*)sector;
        return !atomicSector->collides || querySector(query, &atomicSector->sector);
    }

    RpPlaneSector* plane = (RpPlaneSector*)sector;
    if (rwshim::axisValue(query->box.inf, plane->type) >= plane->leftValue)
    {
        return querySectors(query, plane->rightSubTree);
    }

    if (!querySectors(query, plane->leftSubTree))
    {
        return false;
    }

    return rwshim::axisValue(query->box.sup, plane->type) < plane->rightValue ||
           querySectors(query, plane->rightSubTree);
}

// Returns NULL for a query type RenderWare refuses: points, and atomics, which
// the game never asks for.
RpWorld* RpCollisionWorldForAllIntersections(RpWorld* world, RpIntersection* intersection,
                                             RpIntersectionCallBackWorldTriangle callBack,
                                             void* data)
{
    if (world == NULL || intersection == NULL || callBack == NULL)
    {
        return NULL;
    }

    WorldQuery query;
    query.intersection = intersection;
    query.callBack = callBack;
    query.data = data;
    query.lineDelta.x = query.lineDelta.y = query.lineDelta.z = 0.0f;
    query.recipRadius = 0.0f;

    switch (intersection->type)
    {
    case rpINTERSECTSPHERE:
    {
        const RwSphere& sphere = intersection->t.sphere;
        query.box.inf.x = sphere.center.x - sphere.radius;
        query.box.inf.y = sphere.center.y - sphere.radius;
        query.box.inf.z = sphere.center.z - sphere.radius;
        query.box.sup.x = sphere.center.x + sphere.radius;
        query.box.sup.y = sphere.center.y + sphere.radius;
        query.box.sup.z = sphere.center.z + sphere.radius;
        query.recipRadius = 1.0f / sphere.radius;
        break;
    }

    case rpINTERSECTLINE:
    {
        const RwLine& line = intersection->t.line;
        query.box.inf.x = line.start.x < line.end.x ? line.start.x : line.end.x;
        query.box.inf.y = line.start.y < line.end.y ? line.start.y : line.end.y;
        query.box.inf.z = line.start.z < line.end.z ? line.start.z : line.end.z;
        query.box.sup.x = line.start.x > line.end.x ? line.start.x : line.end.x;
        query.box.sup.y = line.start.y > line.end.y ? line.start.y : line.end.y;
        query.box.sup.z = line.start.z > line.end.z ? line.start.z : line.end.z;
        RwV3dSubMacro(&query.lineDelta, &line.end, &line.start);
        break;
    }

    case rpINTERSECTBOX:
        query.box = intersection->t.box;
        break;

    default:
        return NULL;
    }

    if (world->rootSector != NULL)
    {
        querySectors(&query, world->rootSector);
    }

    return world;
}

// ---------------------------------------------------------------------------
// RpCollision, the GEOMETRY plugin -- which is not the world-sector code above
// and is much smaller than it looks.
//
// These two are easy to conflate and the distinction decides how much work the
// port owes:
//
//   * RpCollisionWorldForAllIntersections (above) walks a WORLD's sector tree,
//     which world.cpp reads. Each sector's own collision tree is skipped
//     there; its polygons are tested directly.
//   * RpCollisionGeometryGetData hangs an RpCollisionData off a GEOMETRY -- a
//     model's own collision tree. Four call sites, and every one of them
//     already copes with not having one.
//
// **What the callers need is a slot that reads NULL, not a tree.** The macro in
// rpcollbsptree.h is
//
//     RWPLUGINOFFSET(RpCollisionData*, geom, _rpCollisionGeometryDataOffset)
//         ? *RWPLUGINOFFSET(...) : NULL
//
// and RWPLUGINOFFSET is `(type*)((RwUInt8*)base + offset)`, which is never NULL
// for a live geometry whatever the offset is. So the ternary ALWAYS takes its
// first branch and dereferences whatever sits at that offset. Leaving the
// offset at zero would read the geometry's own first word as an
// RpCollisionData* and hand the callers garbage that passes their null checks.
// A real four-byte plugin slot, constructed to NULL, is what makes the macro
// answer NULL honestly.
//
// With that in place every caller degrades to the path it already has:
// xCollide.cpp:2093 falls back to a brute-force scan over the model's
// triangles, and xShadow.cpp:1454 tests `colldata != NULL && colldata->tree !=
// NULL` before using it. Same results, O(triangles) instead of O(log
// triangles) -- the same trade RpAtomicForAllIntersections already makes, and
// iCollide.cpp's collide_rwtime is where it will show.
//
// Nothing here reads or writes a tree, because nothing produces one: BFBB's
// JSP levels carry Heavy Iron's OWN xClumpCollBSPTree, which xClumpColl.cpp
// walks directly and which never passes through RenderWare's plugin.

RwInt32 _rpCollisionGeometryDataOffset = 0;

static void* collisionGeometryConstructor(void* object, RwInt32 offset, RwInt32 size)
{
    (void)size;

    // The whole point: the slot starts, and stays, NULL.
    *RWPLUGINOFFSET(RpCollisionData*, object, offset) = NULL;
    return object;
}

static void* collisionGeometryDestructor(void* object, RwInt32 offset, RwInt32 size)
{
    (void)offset;
    (void)size;

    // Nothing to free -- the port never allocates an RpCollisionData. Retail
    // frees the tree it read out of the asset here.
    return object;
}

static void* collisionGeometryCopy(void* dstObject, void* srcObject, RwInt32 offset, RwInt32 size)
{
    (void)srcObject;
    (void)size;

    // NOT a copy of the source pointer. Two geometries sharing one
    // RpCollisionData would double-free it the moment either is destroyed, and
    // since the port's is always NULL there is nothing to share anyway.
    *RWPLUGINOFFSET(RpCollisionData*, dstObject, offset) = NULL;
    return dstObject;
}

RwBool RpCollisionPluginAttach(void)
{
    // Between RwEngineInit and RwEngineOpen, like every other plugin attach:
    // this grows the size of a geometry, and Engine::open freezes that.
    // iSystem.cpp's RWAttachPlugins already calls it in that window.
    // RenderWare's own id for this plugin, spelled out because rpcollis.h does
    // not carry the constant. Nothing streams the chunk -- the port never reads
    // or writes collision data -- so the value matters only in that it must not
    // collide with a plugin librw registers itself.
    const rw::uint32 kCollisionPluginID = 0x0253;

    rw::int32 offset = rw::Geometry::registerPlugin(sizeof(RpCollisionData*), kCollisionPluginID,
                                                    collisionGeometryConstructor,
                                                    collisionGeometryDestructor,
                                                    collisionGeometryCopy);
    if (offset < 0)
    {
        return FALSE;
    }

    _rpCollisionGeometryDataOffset = offset;
    return TRUE;
}
