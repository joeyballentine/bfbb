#ifndef IHIPOLYTESS_H
#define IHIPOLYTESS_H

// Curved PN-triangle tessellation of a low-polygon mesh. What it is for and
// what keeps the result watertight is at the top of iHipolyTess.cpp. Nothing
// here knows about RenderWare: iHipoly.cpp feeds it geometries and puts the
// results back.

#include <types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A growable array. The port's game code keeps the STL out, and these files
// follow it; this is all of std::vector that the tessellator needs.
// **What a tessellation that cannot fit says, instead of stopping the game.**
//
// It is a 32-bit process and a level's worth of new triangles is tens of
// megabytes in one piece, so the block that fails is not the one that ran the
// address space out -- Sandy's treedome dies looking for 46MB with gigabytes
// free. Backing off is the only answer, and the caller is the only one that
// knows what to back off to, so this travels up to it.
//
// Every buffer between here and there is an iHipolyArray, whose destructor
// frees on the way out, so the memory the failed attempt was holding is gone
// before the next one starts.
struct iHipolyOutOfMemory
{
    U32 wanted;
};

// Deletes what it holds however the scope ends. Only where an array of objects
// is wanted rather than one of values -- everything else is iHipolyArray.
template <typename T> struct iHipolyOwn
{
    T* p;

    iHipolyOwn(U32 n) : p(new T[n])
    {
    }
    ~iHipolyOwn()
    {
        delete[] p;
    }

private:
    iHipolyOwn(const iHipolyOwn&);
    iHipolyOwn& operator=(const iHipolyOwn&);
};

template <typename T> struct iHipolyArray
{
    T* p;
    U32 n;
    U32 cap;

    iHipolyArray() : p(NULL), n(0), cap(0)
    {
    }
    ~iHipolyArray()
    {
        free(p);
    }

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
        T* q = (T*)realloc(p, (size_t)c * sizeof(T));
        if (q == NULL)
        {
            iHipolyOutOfMemory oom;

            oom.wanted = (U32)((size_t)c * sizeof(T));
            throw oom;
        }
        p = q;
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

    void clear()
    {
        n = 0;
    }

    T& operator[](U32 i)
    {
        return p[i];
    }
    const T& operator[](U32 i) const
    {
        return p[i];
    }

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

// -----------------------------------------------------------------------
// A hash map from three 64-bit keys to a 32-bit value. Open addressing;
// never shrinks; sized for what the caller says it will hold.

struct iHipolyKey3Map
{
    struct Slot
    {
        S64 a, b, c;
        U32 v;
        U32 used;
    };
    iHipolyArray<Slot> slots;
    U32 mask;
    U32 count;

    void init(U32 expected)
    {
        U32 size = 64;
        while (size < expected * 2)
        {
            size *= 2;
        }
        slots.resizeZero(size);
        mask = size - 1;
        count = 0;
    }

    static U64 mix(U64 h)
    {
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        return h;
    }

    static U64 hash(S64 a, S64 b, S64 c)
    {
        return mix((U64)a * 0x9E3779B97F4A7C15ULL ^ mix((U64)b) ^ (mix((U64)c) << 1));
    }

    // The value for the key, inserting `v` when it is new; `inserted`
    // says which.
    U32 findOrInsert(S64 a, S64 b, S64 c, U32 v, bool* inserted)
    {
        U32 i = (U32)hash(a, b, c) & mask;
        for (;;)
        {
            Slot& s = slots[i];
            if (!s.used)
            {
                s.used = 1;
                s.a = a;
                s.b = b;
                s.c = c;
                s.v = v;
                count++;
                *inserted = true;
                return v;
            }
            if (s.a == a && s.b == b && s.c == c)
            {
                *inserted = false;
                return s.v;
            }
            i = (i + 1) & mask;
        }
    }

    S32 find(S64 a, S64 b, S64 c) const
    {
        U32 i = (U32)hash(a, b, c) & mask;
        for (;;)
        {
            const Slot& s = slots[i];
            if (!s.used)
            {
                return -1;
            }
            if (s.a == a && s.b == b && s.c == c)
            {
                return (S32)s.v;
            }
            i = (i + 1) & mask;
        }
    }
};

// One geometry of a domain, as the caller holds it. Attributes are optional;
// a NULL pointer means the geometry has none of that kind.
struct iHipolyGeom
{
    U32 nv;
    U32 nt;
    const F32* pos; // nv * 3
    const F32* normal; // nv * 3, or NULL: the world ships none
    const U8* color; // nv * 4 RGBA, or NULL
    U32 numUV; // texture coordinate sets, 0 to 8
    const F32* uv[8]; // nv * 2 each
    const U8* skinIndex; // nv * 4, or NULL
    const F32* skinWeight; // nv * 4
    const U32* tris; // nt * 4: three vertex indices and a material
    const U8* frozen; // nt, or NULL: faces the last pass left uncut, to stay so
};

// Per-face controls. Each array has one entry per face over the whole domain,
// in geometry order, or is NULL to use the scalar beside it.
struct iHipolyParams
{
    F64 target; // edge length to aim for
    S32 maxLevel;
    F64 minBulge; // an edge whose midpoint bows less than this fraction of its length, times 8, stays level 1: the sine of twice the normals' tilt
    F64 creaseDeg; // faces meeting sharper than this do not share a normal
    const F64* creaseDegPerFace;
    F64 maxBulge; // units an edge midpoint may move
    const F64* maxBulgePerFace;
    F64 relBulge; // as a fraction of the edge's length
    const F64* relBulgePerFace;
    F64 turnBulge; // times what the surface turns across the edge; 1.5 trusts the normals
    F64 hardDeg; // an edge folded more than this stays straight; < 0 for none
    bool noiseGuard; // pin vertices with both convex and concave edges
    bool pinOpenEdges; // keep one-faced edges straight: the world's sheets lie along each other
    F64 inset; // pull the surface back along the normals by this fraction of its bow: 0 bulges out from the faces, 1 keeps the bows flat and cuts the corners in, 0.5 is half of each
    U32 maxVerts; // per geometry: a 16-bit index buffer
    U32 maxTris; // per geometry: what a JSP collision record can address
};

struct iHipolyResult
{
    U32 nv;
    U32 nt;
    iHipolyArray<F32> pos; // nv * 3
    iHipolyArray<F32> normal; // nv * 3, empty when the input had none
    iHipolyArray<U8> color; // nv * 4
    U32 numUV;
    iHipolyArray<F32> uv[8]; // nv * 2
    iHipolyArray<U8> skinIndex; // nv * 4
    iHipolyArray<F32> skinWeight; // nv * 4
    iHipolyArray<U32> tris; // nt * 4
    iHipolyArray<U32> parent; // nt: the input face each child was cut from
    iHipolyArray<U8> flat; // nt: 1 where the child is its parent, uncut
    iHipolyArray<F32> cbary; // nt * 9: each corner's barycentrics in its parent
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
