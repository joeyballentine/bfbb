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

#include "rw.h"

#include <math.h>

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

namespace
{
    struct Vec
    {
        float x, y, z;
    };

    inline Vec sub(const Vec& a, const Vec& b)
    {
        Vec r = { a.x - b.x, a.y - b.y, a.z - b.z };
        return r;
    }

    inline Vec cross(const Vec& a, const Vec& b)
    {
        Vec r = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
        return r;
    }

    inline float dot(const Vec& a, const Vec& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    inline Vec madd(const Vec& a, const Vec& b, float s)
    {
        Vec r = { a.x + b.x * s, a.y + b.y * s, a.z + b.z * s };
        return r;
    }

    inline const Vec& asVec(const RwV3d& v)
    {
        return reinterpret_cast<const Vec&>(v);
    }

    // Closest point on triangle abc to p, and the distance to it. Ericson's
    // Voronoi-region form: it is the version that stays correct when the
    // closest feature is an edge or a vertex, which is most of the time for a
    // character-sized sphere against a floor made of long thin triangles.
    float distancePointTriangle(const Vec& p, const Vec& a, const Vec& b, const Vec& c)
    {
        Vec ab = sub(b, a);
        Vec ac = sub(c, a);
        Vec ap = sub(p, a);

        float d1 = dot(ab, ap);
        float d2 = dot(ac, ap);

        Vec closest;
        if (d1 <= 0.0f && d2 <= 0.0f)
        {
            closest = a;
        }
        else
        {
            Vec bp = sub(p, b);
            float d3 = dot(ab, bp);
            float d4 = dot(ac, bp);

            Vec cp = sub(p, c);
            float d5 = dot(ab, cp);
            float d6 = dot(ac, cp);

            float vc = d1 * d4 - d3 * d2;
            float vb = d5 * d2 - d1 * d6;
            float va = d3 * d6 - d5 * d4;

            if (d3 >= 0.0f && d4 <= d3)
            {
                closest = b;
            }
            else if (d6 >= 0.0f && d5 <= d6)
            {
                closest = c;
            }
            else if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
            {
                closest = madd(a, ab, d1 / (d1 - d3));
            }
            else if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
            {
                closest = madd(a, ac, d2 / (d2 - d6));
            }
            else if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
            {
                closest = madd(b, sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6)));
            }
            else
            {
                float denom = 1.0f / (va + vb + vc);
                closest = madd(madd(a, ab, vb * denom), ac, vc * denom);
            }
        }

        Vec d = sub(p, closest);
        return sqrtf(dot(d, d));
    }

    // Moller-Trumbore, double sided, against a segment rather than a ray: t
    // comes back already normalised to [0,1] along start->end, which is the
    // number rayHitsEnvCB scales by its own max_t.
    bool intersectSegmentTriangle(const Vec& start, const Vec& end, const Vec& a, const Vec& b,
                                  const Vec& c, float* t)
    {
        Vec edge1 = sub(b, a);
        Vec edge2 = sub(c, a);
        Vec dir = sub(end, start);

        Vec pvec = cross(dir, edge2);
        float det = dot(edge1, pvec);
        if (det > -1e-12f && det < 1e-12f)
        {
            // The segment lies in the triangle's plane. RenderWare reports no
            // hit here too: a line in the plane of a triangle has no single
            // point of intersection to report a distance for.
            return false;
        }

        float invDet = 1.0f / det;

        Vec tvec = sub(start, a);
        float u = dot(tvec, pvec) * invDet;
        if (u < 0.0f || u > 1.0f)
        {
            return false;
        }

        Vec qvec = cross(tvec, edge1);
        float v = dot(dir, qvec) * invDet;
        if (v < 0.0f || u + v > 1.0f)
        {
            return false;
        }

        float hit = dot(edge2, qvec) * invDet;
        if (hit < 0.0f || hit > 1.0f)
        {
            return false;
        }

        *t = hit;
        return true;
    }

    inline bool axisSeparates(float p0, float p1, float p2, float boxRadius)
    {
        float mn = p0 < p1 ? (p0 < p2 ? p0 : p2) : (p1 < p2 ? p1 : p2);
        float mx = p0 > p1 ? (p0 > p2 ? p0 : p2) : (p1 > p2 ? p1 : p2);
        return mn > boxRadius || mx < -boxRadius;
    }

    // Akenine-Moller's separating-axis test: the box's three face normals, the
    // triangle's plane normal, and the nine edge-cross-edge axes.
    bool intersectBoxTriangle(const Vec& boxCenter, const Vec& boxHalf, const Vec& a, const Vec& b,
                              const Vec& c)
    {
        Vec v0 = sub(a, boxCenter);
        Vec v1 = sub(b, boxCenter);
        Vec v2 = sub(c, boxCenter);

        if (axisSeparates(v0.x, v1.x, v2.x, boxHalf.x) ||
            axisSeparates(v0.y, v1.y, v2.y, boxHalf.y) ||
            axisSeparates(v0.z, v1.z, v2.z, boxHalf.z))
        {
            return false;
        }

        Vec e[3] = { sub(v1, v0), sub(v2, v1), sub(v0, v2) };

        for (int i = 0; i < 3; i++)
        {
            float fx = fabsf(e[i].x);
            float fy = fabsf(e[i].y);
            float fz = fabsf(e[i].z);

            // axis = cross(x, e[i])
            if (axisSeparates(e[i].y * v0.z - e[i].z * v0.y, e[i].y * v1.z - e[i].z * v1.y,
                              e[i].y * v2.z - e[i].z * v2.y, boxHalf.y * fz + boxHalf.z * fy))
            {
                return false;
            }
            // axis = cross(y, e[i])
            if (axisSeparates(e[i].z * v0.x - e[i].x * v0.z, e[i].z * v1.x - e[i].x * v1.z,
                              e[i].z * v2.x - e[i].x * v2.z, boxHalf.x * fz + boxHalf.z * fx))
            {
                return false;
            }
            // axis = cross(z, e[i])
            if (axisSeparates(e[i].x * v0.y - e[i].y * v0.x, e[i].x * v1.y - e[i].y * v1.x,
                              e[i].x * v2.y - e[i].y * v2.x, boxHalf.x * fy + boxHalf.y * fx))
            {
                return false;
            }
        }

        // The triangle's own plane against the box.
        Vec n = cross(e[0], sub(v2, v0));
        float r = boxHalf.x * fabsf(n.x) + boxHalf.y * fabsf(n.y) + boxHalf.z * fabsf(n.z);
        float s = dot(n, v0);
        return fabsf(s) <= r;
    }
} // namespace

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
    Vec sphereCenter = { 0.0f, 0.0f, 0.0f };
    float sphereRadius = 0.0f;
    Vec lineStart = { 0.0f, 0.0f, 0.0f };
    Vec lineEnd = { 0.0f, 0.0f, 0.0f };
    Vec boxCenter = { 0.0f, 0.0f, 0.0f };
    Vec boxHalf = { 0.0f, 0.0f, 0.0f };

    switch (intersection->type)
    {
    case rpINTERSECTSPHERE:
    {
        RwV3dTransformPoints(reinterpret_cast<RwV3d*>(&sphereCenter),
                             &intersection->t.sphere.center, 1, &inverse);
        sphereRadius = intersection->t.sphere.radius / scale;
        break;
    }

    case rpINTERSECTLINE:
    {
        RwV3dTransformPoints(reinterpret_cast<RwV3d*>(&lineStart), &intersection->t.line.start, 1,
                             &inverse);
        RwV3dTransformPoints(reinterpret_cast<RwV3d*>(&lineEnd), &intersection->t.line.end, 1,
                             &inverse);
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

        Vec mn = asVec(corners[0]);
        Vec mx = mn;
        for (int i = 1; i < 8; i++)
        {
            const Vec& v = asVec(corners[i]);
            if (v.x < mn.x)
                mn.x = v.x;
            if (v.y < mn.y)
                mn.y = v.y;
            if (v.z < mn.z)
                mn.z = v.z;
            if (v.x > mx.x)
                mx.x = v.x;
            if (v.y > mx.y)
                mx.y = v.y;
            if (v.z > mx.z)
                mx.z = v.z;
        }

        boxCenter.x = 0.5f * (mn.x + mx.x);
        boxCenter.y = 0.5f * (mn.y + mx.y);
        boxCenter.z = 0.5f * (mn.z + mx.z);
        boxHalf.x = 0.5f * (mx.x - mn.x);
        boxHalf.y = 0.5f * (mx.y - mn.y);
        boxHalf.z = 0.5f * (mx.z - mn.z);
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

        const Vec& a = asVec(*pa);
        const Vec& b = asVec(*pb);
        const Vec& c = asVec(*pc);

        float distance;
        switch (intersection->type)
        {
        case rpINTERSECTSPHERE:
            // The distance from the sphere's centre to the triangle, which is
            // what a sphere query's `distance` means. iCollide.cpp's sphere
            // callbacks recompute it for themselves out of cbisx_local and
            // ignore this one, so what it really decides is whether there is a
            // hit to report at all.
            distance = distancePointTriangle(sphereCenter, a, b, c);
            if (distance > sphereRadius)
            {
                continue;
            }
            break;

        case rpINTERSECTLINE:
            if (!intersectSegmentTriangle(lineStart, lineEnd, a, b, c, &distance))
            {
                continue;
            }
            break;

        default: // rpINTERSECTBOX
            if (!intersectBoxTriangle(boxCenter, boxHalf, a, b, c))
            {
                continue;
            }
            // A box query has no single point of contact to measure to, and
            // RenderWare's documentation does not say what it puts here.
            // Zero is what an overlap test can honestly report. No caller in
            // the game reaches this, so nothing depends on the guess.
            distance = 0.0f;
            break;
        }

        RpCollisionTriangle collTriangle;
        collTriangle.index = i;
        collTriangle.vertices[0] = pa;
        collTriangle.vertices[1] = pb;
        collTriangle.vertices[2] = pc;

        // `point` has to be a point ON the triangle's plane, not just near it:
        // properSphereIsectTri in iCollide.cpp builds the plane equation as
        // dot(normal, point). Vertex 0 is on the plane by construction, which
        // is all that equation needs.
        collTriangle.point = *pa;

        Vec n = cross(sub(b, a), sub(c, a));
        float len = sqrtf(dot(n, n));
        if (len > 0.0f)
        {
            n.x /= len;
            n.y /= len;
            n.z /= len;
        }
        // A degenerate triangle keeps a zero normal rather than being skipped,
        // so the callback still sees it and decides for itself -- which is
        // what iCollide.cpp does, discarding the hit when the plane distance
        // comes out as exactly zero.
        collTriangle.normal.x = n.x;
        collTriangle.normal.y = n.y;
        collTriangle.normal.z = n.z;

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
