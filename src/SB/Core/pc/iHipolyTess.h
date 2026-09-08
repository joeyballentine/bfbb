#ifndef IHIPOLYTESS_H
#define IHIPOLYTESS_H

// Curved PN-triangle tessellation of a low-polygon mesh. What it is for and
// what keeps the result watertight is at the top of iHipolyTess.cpp. Nothing
// here knows about RenderWare: iHipoly.cpp feeds it geometries and puts the
// results back.

#include <types.h>

#include <stdlib.h>
#include <string.h>

// A growable array. The port's game code keeps the STL out, and these files
// follow it; this is all of std::vector that the tessellator needs.
template <typename T> struct iHipolyArray
{
    T* p;
    U32 n;
    U32 cap;

    iHipolyArray() : p(NULL), n(0), cap(0) {}
    ~iHipolyArray() { free(p); }

    void reserve(U32 want)
    {
        if (want <= cap)
        {
            return;
        }
        U32 c = cap ? cap : 16;
        while (c < want)
        {
            c *= 2;
        }
        p = (T*)realloc(p, (size_t)c * sizeof(T));
        cap = c;
    }

    void resize(U32 want)
    {
        reserve(want);
        n = want;
    }

    // Grow to `want` with every new element zeroed.
    void resizeZero(U32 want)
    {
        U32 old = n;
        resize(want);
        if (want > old)
        {
            memset(p + old, 0, (size_t)(want - old) * sizeof(T));
        }
    }

    void push(const T& v)
    {
        reserve(n + 1);
        p[n++] = v;
    }

    void clear() { n = 0; }

    T& operator[](U32 i) { return p[i]; }
    const T& operator[](U32 i) const { return p[i]; }

    // Take another array's storage; the other is left empty.
    void take(iHipolyArray<T>& o)
    {
        free(p);
        p = o.p;
        n = o.n;
        cap = o.cap;
        o.p = NULL;
        o.n = o.cap = 0;
    }

private:
    iHipolyArray(const iHipolyArray&);
    iHipolyArray& operator=(const iHipolyArray&);
};

struct iHipolyV3
{
    F64 x, y, z;
};

// One geometry of a domain, as the caller holds it. Attributes are optional;
// a NULL pointer means the geometry has none of that kind.
struct iHipolyGeom
{
    U32 nv;
    U32 nt;
    const F32* pos;        // nv * 3
    const F32* normal;     // nv * 3, or NULL: the world ships none
    const U8* color;       // nv * 4 RGBA, or NULL
    U32 numUV;             // texture coordinate sets, 0 to 8
    const F32* uv[8];      // nv * 2 each
    const U8* skinIndex;   // nv * 4, or NULL
    const F32* skinWeight; // nv * 4
    const U32* tris;       // nt * 4: three vertex indices and a material
};

// Per-face controls. Each array has one entry per face over the whole domain,
// in geometry order, or is NULL to use the scalar beside it.
struct iHipolyParams
{
    F64 target;            // edge length to aim for
    S32 maxLevel;
    F64 minBulge;          // an edge bowing less than this fraction stays level 1
    F64 creaseDeg;         // faces meeting sharper than this do not share a normal
    const F64* creaseDegPerFace;
    F64 maxBulge;          // units an edge midpoint may move
    const F64* maxBulgePerFace;
    F64 relBulge;          // as a fraction of the edge's length
    const F64* relBulgePerFace;
    F64 hardDeg;           // an edge folded more than this stays straight; < 0 for none
    bool noiseGuard;       // pin vertices with both convex and concave edges
    U32 maxVerts;          // per geometry: a 16-bit index buffer
    U32 maxTris;           // per geometry: what a JSP collision record can address
};

struct iHipolyResult
{
    U32 nv;
    U32 nt;
    iHipolyArray<F32> pos;        // nv * 3
    iHipolyArray<F32> normal;     // nv * 3, empty when the input had none
    iHipolyArray<U8> color;       // nv * 4
    U32 numUV;
    iHipolyArray<F32> uv[8];      // nv * 2
    iHipolyArray<U8> skinIndex;   // nv * 4
    iHipolyArray<F32> skinWeight; // nv * 4
    iHipolyArray<U32> tris;       // nt * 4
    iHipolyArray<U32> parent;     // nt: the input face each child was cut from
    iHipolyArray<F32> cbary;      // nt * 9: each corner's barycentrics in its parent
};

struct iHipolyStats
{
    U32 openEdges;
    U32 tJunctions;
    U32 folds;
};

// Tessellate every geometry of the domain together, in one vertex space, so
// the seams between them stay closed. `out` holds one result per geometry.
void iHipolyRefine(const iHipolyGeom* geoms, U32 numGeoms, const iHipolyParams& params,
                   iHipolyResult* out, iHipolyStats* stats);

// Sort `n` indices by a 64-bit key each. Shared with the collision tree
// builder; a double sorts by this once its bits are made monotonic.
void iHipolySortU64(U32* idx, U32 n, const U64* key);

#endif
