// RenderWare C API: RpAtomic.
//
// RpAtomic is mirrored onto rw::Atomic (see include/rwsdk/rpworld.h and
// layout_geometry.cpp), so the two setters are a cast and a call.
//
// RpAtomicForAllIntersections is not: librw has no collision code at all, so
// it is written out below. It is also the one function here that RenderWare
// implements differently rather than merely elsewhere -- see the comment above
// it.

#include <rwcore.h>
#include <rpcollis.h>
#include <rpworld.h>
#include <rtintsec.h>

#include "intersect.h"
#include "rw.h"

static inline rw::Atomic* asAtomic(RpAtomic* a)
{
    return reinterpret_cast<rw::Atomic*>(a);
}

RpAtomic* RpAtomicSetFrame(RpAtomic* atomic, RwFrame* frame)
{
    if (atomic == NULL)
    {
        return NULL;
    }

    // Unhooks the atomic from whatever frame it was on first, which is what
    // makes RpAtomicSetFrame(a, NULL) the detach xPtankPool.cpp uses before it
    // destroys a frame. librw's setFrame also marks the world bounding sphere
    // dirty, as RenderWare's does.
    asAtomic(atomic)->setFrame(reinterpret_cast<rw::Frame*>(frame));
    return atomic;
}

RpAtomic* RpAtomicSetGeometry(RpAtomic* atomic, RpGeometry* geometry, RwUInt32 flags)
{
    if (atomic == NULL)
    {
        return NULL;
    }

    // Reference counted on both sides: this drops the reference on the old
    // geometry and takes one on the new. zFX.cpp depends on that -- it builds
    // a replacement geometry, hands it over, and never destroys the original.
    //
    // flags is rpATOMICSAMEBOUNDINGSPHERE or nothing, and librw spells it
    // Atomic::SAMEBOUNDINGSPHERE with the same value: keep the sphere the
    // atomic already has instead of copying morph target 0's.
    asAtomic(atomic)->setGeometry(reinterpret_cast<rw::Geometry*>(geometry), flags);
    return atomic;
}

// ---------------------------------------------------------------------------
// RpAtomicForAllIntersections
//
// librw has no counterpart, so this is written out. Two things about it are
// worth knowing before reading the maths.
//
// FIRST, the spaces. RenderWare takes the intersection primitive in WORLD
// space and hands the callback triangles in the atomic's OBJECT space. That is
// not a detail this port is free to choose: iCollide.cpp's callbacks ignore
// the RpIntersection they are passed and test against `cbisx_local`, an
// object-space copy the caller built itself before calling. So the primitive
// is transformed down here by the inverse of the frame's LTM, and the vertices
// handed back are the morph target's own, untransformed.
//
// SECOND, RenderWare does not walk every triangle. A model built with the
// collision plugin carries a BSP tree, and RpAtomicForAllIntersections
// descends it. The port has no tree -- RpCollisionPluginAttach is not written
// and librw would have nowhere to keep one -- so this is a linear scan. It
// reports the same set of triangles, in geometry order rather than tree order,
// and it costs O(triangles) per query where retail costs O(log triangles).
// iCollide.cpp already times these calls (collide_rwtime), which is where the
// difference will show up.
//
// Only the three intersection types RenderWare supports for an atomic are
// handled. rpINTERSECTPOINT and rpINTERSECTATOMIC are world-sector queries and
// have no meaning against a triangle list on either side.

// Only the maths that has no RenderWare name of its own stays here. The
// sphere and box tests moved to intersect.cpp as RtIntersectionSphereTriangle
// and RtIntersectionBBoxTriangle, and this file calls them, so that a triangle
// the game finds by walking an atomic is a triangle it also finds by walking
// that atomic's collision tree in xClumpColl.cpp.

RpAtomic* RpAtomicForAllIntersections(RpAtomic* atomic, RpIntersection* intersection,
                                      RpIntersectionCallBackGeometryTriangle callBack, void* data)
{
    if (atomic == NULL || intersection == NULL || callBack == NULL)
    {
        return atomic;
    }

    RpGeometry* geometry = atomic->geometry;
    if (geometry == NULL || geometry->numTriangles == 0 || geometry->numMorphTargets == 0)
    {
        return atomic;
    }

    // Morph target 0, always. RenderWare would use the interpolated one, but
    // librw does not interpolate morph targets and the port's RpAtomic has no
    // interpolator to read -- see the comment on the struct in rpworld.h.
    RwV3d* verts = geometry->morphTarget[0].verts;
    if (verts == NULL)
    {
        return atomic;
    }

    // World space to object space. RwMatrixInvert handles a scaled LTM; the
    // scale has to come out of a sphere's radius separately, which is exactly
    // what iCollide.cpp does when it builds its own object-space copy, so the
    // two agree on what the callback is testing against.
    // Zeroed before RwMatrixSetIdentity because that macro ORs into the flags
    // word rather than assigning it, so it has to start from something.
    RwMatrix inverse = {};
    float scale = 1.0f;
    RwFrame* frame = RpAtomicGetFrame(atomic);
    if (frame != NULL)
    {
        RwMatrix* ltm = RwFrameGetLTM(frame);
        RwMatrixInvert(&inverse, ltm);
        scale = RwV3dLength(&ltm->right);
        if (scale == 0.0f)
        {
            // A zero-scaled atomic has no volume to hit.
            return atomic;
        }
    }
    else
    {
        RwMatrixSetIdentity(&inverse);
    }

    // The primitive, in object space.
    RwSphere sphere = {};
    RwV3d lineStart = {};
    RwV3d lineEnd = {};
    RwBBox box = {};

    switch (intersection->type)
    {
    case rpINTERSECTSPHERE:
    {
        RwV3dTransformPoints(&sphere.center, &intersection->t.sphere.center, 1, &inverse);
        sphere.radius = intersection->t.sphere.radius / scale;
        break;
    }

    case rpINTERSECTLINE:
    {
        RwV3dTransformPoints(&lineStart, &intersection->t.line.start, 1, &inverse);
        RwV3dTransformPoints(&lineEnd, &intersection->t.line.end, 1, &inverse);
        break;
    }

    case rpINTERSECTBOX:
    {
        // An RwBBox is axis-aligned in the space it was given in, and the
        // inverse LTM is not axis-preserving in general, so there is no
        // object-space box that is exactly this world-space box. The eight
        // corners are transformed and re-bounded: that box is at least as
        // large as the true one, so the callback sees a superset and never
        // misses a triangle. Nothing in the game issues a box query against an
        // atomic today, so which way RenderWare rounded is not something this
        // port can check.
        RwV3d corners[8];
        const RwV3d& inf = intersection->t.box.inf;
        const RwV3d& sup = intersection->t.box.sup;
        for (int i = 0; i < 8; i++)
        {
            corners[i].x = (i & 1) ? sup.x : inf.x;
            corners[i].y = (i & 2) ? sup.y : inf.y;
            corners[i].z = (i & 4) ? sup.z : inf.z;
        }
        RwV3dTransformPoints(corners, corners, 8, &inverse);

        // RwBBoxCalculate is RenderWare's own name for this and is not written
        // yet; it is four lines here and would be a second unasserted claim
        // about RwBBox's field order there.
        box.inf = corners[0];
        box.sup = corners[0];
        for (int i = 1; i < 8; i++)
        {
            if (corners[i].x < box.inf.x)
                box.inf.x = corners[i].x;
            if (corners[i].y < box.inf.y)
                box.inf.y = corners[i].y;
            if (corners[i].z < box.inf.z)
                box.inf.z = corners[i].z;
            if (corners[i].x > box.sup.x)
                box.sup.x = corners[i].x;
            if (corners[i].y > box.sup.y)
                box.sup.y = corners[i].y;
            if (corners[i].z > box.sup.z)
                box.sup.z = corners[i].z;
        }
        break;
    }

    default:
        // rpINTERSECTPOINT and rpINTERSECTATOMIC are world-sector queries.
        // RenderWare does not support them against an atomic either.
        return atomic;
    }

    RpTriangle* triangles = geometry->triangles;
    for (RwInt32 i = 0; i < geometry->numTriangles; i++)
    {
        RwV3d* pa = &verts[triangles[i].vertIndex[0]];
        RwV3d* pb = &verts[triangles[i].vertIndex[1]];
        RwV3d* pc = &verts[triangles[i].vertIndex[2]];

        RpCollisionTriangle collTriangle;

        RwReal distance;
        switch (intersection->type)
        {
        case rpINTERSECTSPHERE:
            // Fills collTriangle.normal on the way, which is the same normal
            // the other two cases compute below.
            if (!RtIntersectionSphereTriangle(&sphere, pa, pb, pc, &collTriangle.normal, &distance))
            {
                continue;
            }
            break;

        case rpINTERSECTLINE:
            if (!rwshim::intersectSegmentTriangle(&lineStart, &lineEnd, pa, pb, pc, &distance))
            {
                continue;
            }
            rwshim::triangleNormal(pa, pb, pc, &collTriangle.normal);
            break;

        default: // rpINTERSECTBOX
            if (!RtIntersectionBBoxTriangle(&box, pa, pb, pc))
            {
                continue;
            }
            // A box query has no single point of contact to measure to, and
            // RenderWare's documentation does not say what it puts here.
            // Zero is what an overlap test can honestly report. No caller in
            // the game reaches this, so nothing depends on the guess.
            distance = 0.0f;
            rwshim::triangleNormal(pa, pb, pc, &collTriangle.normal);
            break;
        }

        collTriangle.index = i;
        collTriangle.vertices[0] = pa;
        collTriangle.vertices[1] = pb;
        collTriangle.vertices[2] = pc;

        // `point` has to be a point ON the triangle's plane, not just near it:
        // properSphereIsectTri in iCollide.cpp builds the plane equation as
        // dot(normal, point). Vertex 0 is on the plane by construction, which
        // is all that equation needs.
        collTriangle.point = *pa;

        if (callBack(intersection, &collTriangle, distance, data) == NULL)
        {
            // RenderWare stops early when the callback returns NULL, and
            // sphereHitsEnv3CB uses that to stop once it has filled the
            // caller's collision array.
            break;
        }
    }

    return atomic;
}
