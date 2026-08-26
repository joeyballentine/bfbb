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
#include <rpworld.h>

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
