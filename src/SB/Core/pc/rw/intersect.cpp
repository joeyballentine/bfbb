// RenderWare C API: the Rt intersection toolkit, and RtQuatSetupSlerpCache.
//
// None of this is a cast and a call. librw has no collision code and no
// quaternion toolkit, so all three functions are written out. They are pure
// maths on value types -- RwSphere, RwBBox, RwV3d, RtQuat -- so no object
// layout is involved and there is nothing for layout_*.cpp to assert.
//
// The two intersection functions were written first as file-static helpers
// inside atomic.cpp, for RpAtomicForAllIntersections. They live here now and
// atomic.cpp calls them, because the game reaches the same triangles by two
// different routes: iCollide.cpp asks RpAtomicForAllIntersections for a model
// with no collision tree and xClumpColl.cpp walks the tree and calls these
// directly for a model that has one. If the two disagreed, whether SpongeBob
// stood on a floor would depend on how that floor was exported.

#include <rwcore.h>
#include <rtintsec.h>
#include <rtquat.h>
#include <rtslerp.h>

#include "intersect.h"

#include <math.h>

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

    inline const Vec& asVec(const RwV3d* v)
    {
        return *reinterpret_cast<const Vec*>(v);
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

    inline bool axisSeparates(float p0, float p1, float p2, float boxRadius)
    {
        float mn = p0 < p1 ? (p0 < p2 ? p0 : p2) : (p1 < p2 ? p1 : p2);
        float mx = p0 > p1 ? (p0 > p2 ? p0 : p2) : (p1 > p2 ? p1 : p2);
        return mn > boxRadius || mx < -boxRadius;
    }
} // namespace

void rwshim::triangleNormal(const RwV3d* a, const RwV3d* b, const RwV3d* c, RwV3d* normal)
{
    Vec n = cross(sub(asVec(b), asVec(a)), sub(asVec(c), asVec(a)));

    float len = sqrtf(dot(n, n));
    if (len > 0.0f)
    {
        n.x /= len;
        n.y /= len;
        n.z /= len;
    }

    normal->x = n.x;
    normal->y = n.y;
    normal->z = n.z;
}

bool rwshim::intersectSegmentTriangle(const RwV3d* start, const RwV3d* end, const RwV3d* a,
                                      const RwV3d* b, const RwV3d* c, RwReal* t)
{
    Vec edge1 = sub(asVec(b), asVec(a));
    Vec edge2 = sub(asVec(c), asVec(a));
    Vec dir = sub(asVec(end), asVec(start));

    Vec pvec = cross(dir, edge2);
    float det = dot(edge1, pvec);
    if (det > -1e-12f && det < 1e-12f)
    {
        // The segment lies in the triangle's plane. RenderWare reports no hit
        // here too: a line in the plane of a triangle has no single point of
        // intersection to report a distance for.
        return false;
    }

    float invDet = 1.0f / det;

    Vec tvec = sub(asVec(start), asVec(a));
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

// ---------------------------------------------------------------------------
// RtIntersectionSphereTriangle
//
// `distance` is the distance from the sphere's CENTRE to the nearest point of
// the triangle, so the test is exactly `distance <= radius`. RenderWare's own
// documentation does not pin the quantity down any further than "the distance
// to the triangle", and the game does not settle it either: the one caller,
// LeafNodeSpherePolyIntersect in xClumpColl.cpp, divides it by the radius and
// hands it to a world callback -- and every such callback in iCollide.cpp
// opens by overwriting the parameter (`dist = FLOAT_MAX`) and recomputing it
// from the triangle with properSphereIsectTri. So what this number really
// decides is whether there is a hit to report at all, and centre-to-nearest-
// point is the reading that makes the returned RwBool the sphere/triangle test
// itself.
//
// Both outputs are written whether or not there is a hit. Nothing reads them
// on a miss.
RwBool RtIntersectionSphereTriangle(RwSphere* sphere, RwV3d* v0, RwV3d* v1, RwV3d* v2,
                                    RwV3d* normal, RwReal* distance)
{
    if (sphere == NULL || v0 == NULL || v1 == NULL || v2 == NULL)
    {
        return FALSE;
    }

    if (normal != NULL)
    {
        rwshim::triangleNormal(v0, v1, v2, normal);
    }

    float d = distancePointTriangle(asVec(&sphere->center), asVec(v0), asVec(v1), asVec(v2));
    if (distance != NULL)
    {
        *distance = d;
    }

    return d <= sphere->radius ? TRUE : FALSE;
}

// ---------------------------------------------------------------------------
// RtIntersectionBBoxTriangle
//
// Akenine-Moller's separating-axis test: the box's three face normals, the
// triangle's plane normal, and the nine edge-cross-edge axes. Thirteen axes,
// and the triangle is outside the box only if one of them separates them.
//
// An RwBBox is `sup` then `inf` -- that order, which is the opposite of how it
// reads -- so this converts to centre and half-extent once rather than reach
// for the fields thirteen times.
RwBool RtIntersectionBBoxTriangle(RwBBox* bbox, RwV3d* v0, RwV3d* v1, RwV3d* v2)
{
    if (bbox == NULL || v0 == NULL || v1 == NULL || v2 == NULL)
    {
        return FALSE;
    }

    Vec boxCenter = { 0.5f * (bbox->inf.x + bbox->sup.x), 0.5f * (bbox->inf.y + bbox->sup.y),
                      0.5f * (bbox->inf.z + bbox->sup.z) };
    Vec boxHalf = { 0.5f * (bbox->sup.x - bbox->inf.x), 0.5f * (bbox->sup.y - bbox->inf.y),
                    0.5f * (bbox->sup.z - bbox->inf.z) };

    // An inside-out box (sup below inf) would come out with a negative half
    // extent and separate on every axis, which is the right answer for a box
    // that encloses nothing.

    Vec a = sub(asVec(v0), boxCenter);
    Vec b = sub(asVec(v1), boxCenter);
    Vec c = sub(asVec(v2), boxCenter);

    if (axisSeparates(a.x, b.x, c.x, boxHalf.x) || axisSeparates(a.y, b.y, c.y, boxHalf.y) ||
        axisSeparates(a.z, b.z, c.z, boxHalf.z))
    {
        return FALSE;
    }

    Vec e[3] = { sub(b, a), sub(c, b), sub(a, c) };

    for (int i = 0; i < 3; i++)
    {
        float fx = fabsf(e[i].x);
        float fy = fabsf(e[i].y);
        float fz = fabsf(e[i].z);

        // axis = cross(x, e[i])
        if (axisSeparates(e[i].y * a.z - e[i].z * a.y, e[i].y * b.z - e[i].z * b.y,
                          e[i].y * c.z - e[i].z * c.y, boxHalf.y * fz + boxHalf.z * fy))
        {
            return FALSE;
        }
        // axis = cross(y, e[i])
        if (axisSeparates(e[i].z * a.x - e[i].x * a.z, e[i].z * b.x - e[i].x * b.z,
                          e[i].z * c.x - e[i].x * c.z, boxHalf.x * fz + boxHalf.z * fx))
        {
            return FALSE;
        }
        // axis = cross(z, e[i])
        if (axisSeparates(e[i].x * a.y - e[i].y * a.x, e[i].x * b.y - e[i].y * b.x,
                          e[i].x * c.y - e[i].y * c.x, boxHalf.x * fy + boxHalf.y * fx))
        {
            return FALSE;
        }
    }

    // The triangle's own plane against the box.
    Vec n = cross(e[0], sub(c, a));
    float r = boxHalf.x * fabsf(n.x) + boxHalf.y * fabsf(n.y) + boxHalf.z * fabsf(n.z);
    float s = dot(n, a);
    return fabsf(s) <= r ? TRUE : FALSE;
}

// ---------------------------------------------------------------------------
// RtQuatSetupSlerpCache
//
// The other half of this function is RtQuatSlerpMacro in rtslerp.h, and the
// division of labour there is what dictates everything below. The macro
// computes
//
//     result = raFrom * sin((1 - t) * omega) + raTo * sin(t * omega)
//
// and never divides by anything, so the sin(omega) that belongs underneath a
// slerp has to be folded into the cached quaternions here -- which is what
// rtslerp.h means by calling them the "scaled" initial and final quaternions.
//
// Three things the macro leaves to this function:
//
//   - The shortest arc. q and -q are the same rotation, so a negative dot
//     product means the pair is being interpolated the long way round; negate
//     the destination and the same maths takes the short way. iAnimSKB.cpp
//     blends animation keyframes with this and would otherwise spin a bone
//     most of the way round the circle whenever two keys landed either side of
//     a sign flip.
//
//   - The nearly-parallel case. As omega goes to zero so does sin(omega), and
//     the scale factor 1/sin(omega) goes to infinity. `nearlyZeroOm` tells the
//     macro to skip the sines entirely, which leaves it computing
//     from * (1 - t) + to * t -- a plain lerp, which is what a slerp
//     degenerates to over a small angle anyway.
//
//   - Unnormalised quaternions. The cache holds `from` and `to` as given,
//     scaled but not normalised, so a caller that hands in unit quaternions
//     gets unit quaternions back and one that does not, does not. RenderWare
//     does not normalise here either.
//
// The threshold is on cos(omega) rather than on sin(omega), because that is
// the number already in hand and it is the one that stays well conditioned:
// 1 - cos(omega) is about omega^2/2, so a bound of 1e-6 puts the switch to
// lerp at about 0.08 degrees, where the two answers differ by less than a
// float can hold.
void RtQuatSetupSlerpCache(RtQuat* qpFrom, RtQuat* qpTo, RtQuatSlerpCache* sCache)
{
    if (qpFrom == NULL || qpTo == NULL || sCache == NULL)
    {
        return;
    }

    RwReal cosom = qpFrom->imag.x * qpTo->imag.x + qpFrom->imag.y * qpTo->imag.y +
                   qpFrom->imag.z * qpTo->imag.z + qpFrom->real * qpTo->real;

    sCache->raFrom = *qpFrom;

    if (cosom < 0.0f)
    {
        cosom = -cosom;
        sCache->raTo.imag.x = -qpTo->imag.x;
        sCache->raTo.imag.y = -qpTo->imag.y;
        sCache->raTo.imag.z = -qpTo->imag.z;
        sCache->raTo.real = -qpTo->real;
    }
    else
    {
        sCache->raTo = *qpTo;
    }

    if ((1.0f - cosom) > 1e-6f)
    {
        // acosf's argument can drift a hair outside [-1,1] when the inputs are
        // not quite unit length, and acosf(1.0000001f) is a NaN that would
        // reach every bone in the skeleton.
        if (cosom > 1.0f)
        {
            cosom = 1.0f;
        }

        RwReal omega = acosf(cosom);
        RwReal recipSinom = 1.0f / sinf(omega);

        sCache->omega = omega;
        sCache->nearlyZeroOm = FALSE;

        sCache->raFrom.imag.x *= recipSinom;
        sCache->raFrom.imag.y *= recipSinom;
        sCache->raFrom.imag.z *= recipSinom;
        sCache->raFrom.real *= recipSinom;

        sCache->raTo.imag.x *= recipSinom;
        sCache->raTo.imag.y *= recipSinom;
        sCache->raTo.imag.z *= recipSinom;
        sCache->raTo.real *= recipSinom;
    }
    else
    {
        // The macro reads `omega` only when nearlyZeroOm is FALSE, but it is
        // set anyway: a cache with a stale angle in it is the kind of thing
        // that shows up much later, in a debugger, in something unrelated.
        sCache->omega = 0.0f;
        sCache->nearlyZeroOm = TRUE;
    }
}
