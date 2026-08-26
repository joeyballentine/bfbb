// RenderWare C API: RpCollisionWorld.
//
// One function, and it is NOT implemented. This file exists to say why in the
// place a reader will look for it, and to be where the implementation goes.
//
// RpCollisionWorldForAllIntersections is the environment half of collision:
// iCollide.cpp calls it ten times, for every sphere, ray and swept-sphere query
// the player and the NPCs make against the level. Its atomic counterpart,
// RpAtomicForAllIntersections, IS written -- atomic.cpp has the sphere, segment
// and box triangle tests, written out because librw has no collision code -- so
// the maths this needs already exists and is already tested.
//
// What does not exist is anything to run it over. RenderWare descends the
// world's collision BSP from RpWorld::rootSector and tests the triangles in
// each RpWorldSector it reaches. On this side rootSector is always NULL and
// there is never a sector, because the only thing that would build one is
// RpWorldStreamRead and that is not written either -- see the comment on it in
// world.cpp. librw has no world sector code at all: no RpWorldSector
// counterpart, no plane sectors, no world chunk reader.
//
// So the two are blocked on one missing piece, not two, and they have to be
// written together:
//
//   - a world reader that produces sectors (world.cpp says what that involves,
//     including that BFBB's own BSPs are GameCube-native and need a pipeline
//     librw does not have), and
//   - RpWorldSector's tree, which is why the struct in rpworld.h is left as
//     RenderWare's own layout rather than mirrored: there is no librw type to
//     mirror it onto, and inventing the plane-sector node here -- with nothing
//     to read it and no way to run it -- would put a guess in a public header
//     that a real reader would then have to match.
//
// Writing the traversal now against a tree nothing builds would compile, run,
// visit nothing, and report no collisions, which is exactly what a caller sees
// today. The difference is that today that is visibly unimplemented instead of
// looking finished.
//
// What a caller notices until then: the player and everything else walks
// through the level geometry and falls through the floor. Model-to-model
// collision still works -- that is RpAtomicForAllIntersections, and xClumpColl
// and the JSP path go through it.

#include <rwcore.h>
#include <rpcollis.h>
#include <rpcollbsptree.h>
#include <rpworld.h>

#include "rw.h"

// Returns NULL -- RenderWare's "this call failed" -- rather than the world it
// was handed, so that the one thing a caller could distinguish is not a claim
// of success. Every one of the ten call sites in iCollide.cpp discards the
// result, so the distinction is for whoever reads this next, not for the game.
RpWorld* RpCollisionWorldForAllIntersections(RpWorld* world, RpIntersection* intersection,
                                             RpIntersectionCallBackWorldTriangle callBack,
                                             void* data)
{
    return NULL;
}

// ---------------------------------------------------------------------------
// RpCollision, the GEOMETRY plugin -- which is not the world-sector code above
// and is much smaller than it looks.
//
// These two are easy to conflate and the distinction decides how much work the
// port owes:
//
//   * RpCollisionWorldForAllIntersections (above) walks a WORLD's sector tree.
//     librw has no world sectors and BFBB's Xbox assets carry no BSP, so
//     nothing reaches it. Still unwritten, still returns NULL.
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
