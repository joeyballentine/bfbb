#ifndef SB_CORE_PC_RW_INTERSECT_H
#define SB_CORE_PC_RW_INTERSECT_H

// The parts of intersect.cpp that are not RenderWare API but that more than one
// file in the shim needs.
//
// RpAtomicForAllIntersections in atomic.cpp and the public Rt intersection
// functions have to agree: a triangle the game finds by walking an atomic must
// be a triangle it finds by testing that atomic's collision tree in
// xClumpColl.cpp, because iCollide.cpp routes the same query down whichever
// path the model happens to have. So the tests live in one place and both
// callers use them.
//
// The sphere and box tests are reachable as RtIntersectionSphereTriangle and
// RtIntersectionBBoxTriangle and are not repeated here. The segment test is:
// RenderWare has no public line/triangle entry point, so there is no Rt name
// for atomic.cpp to call.

#include <rwcore.h>

namespace rwshim
{
    // Unit normal of the triangle abc, in the winding the caller gave. A
    // degenerate triangle gets a zero normal rather than being rejected, which
    // is what lets iCollide.cpp's callbacks discard the hit themselves -- they
    // test the plane distance for exactly zero.
    //
    // The formula is normalize(cross(b - a, c - a)), which is the one
    // xClumpColl.cpp writes out by hand beside its own calls; the two have to
    // match or a triangle's front face changes depending on which path found
    // it.
    void triangleNormal(const RwV3d* a, const RwV3d* b, const RwV3d* c, RwV3d* normal);

    // Moller-Trumbore, double sided, against a segment rather than a ray. On a
    // hit *t comes back already normalised to [0,1] along start->end, which is
    // the number rayHitsEnvCB in iCollide.cpp scales by its own max_t.
    bool intersectSegmentTriangle(const RwV3d* start, const RwV3d* end, const RwV3d* a,
                                  const RwV3d* b, const RwV3d* c, RwReal* t);
} // namespace rwshim

#endif
