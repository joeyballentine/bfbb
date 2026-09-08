// Curved PN-triangle tessellation of a low-polygon mesh.
//
// Every triangle becomes a cubic Bezier patch (Vlachos et al., "Curved PN
// Triangles") whose edge control points come from the vertex normals, and is
// sampled on a barycentric grid. Original vertices never move; flat surfaces
// stay exactly flat; surfaces whose normals turn across a triangle bulge to
// follow them.
//
// What keeps the result watertight:
//
// - Edge curves are decided once per edge, in the mesh's welded vertex space,
//   and every triangle sharing the edge samples the same curve at the same
//   parameters from the same table. A crease -- an edge whose two faces
//   disagree about the normal at an endpoint -- takes the curve of whichever
//   face bends it more, so a cylinder's rim rounds with its wall while its cap
//   stays planar.
// - A triangle's level is the largest of its edge levels; the grid points on
//   an edge with a smaller level are snapped onto that edge's own samples, so
//   neighbours of different levels meet edge for edge. The snapping leaves
//   degenerate triangles, which are dropped.
//
// Meshes without normals (the world) get them from smoothing groups: faces
// meeting at less than the crease angle share a normal. Meshes with normals
// keep them as authored, for the shading and as the curve guide.
//
// This is a port of mods/hipoly/tessellate.py from the pc-mod-hipoly branch,
// which built the same thing into a copy of the assets offline. The numpy
// went; the decisions did not, and the comments say why each one is made.

#include "iHipolyTess.h"

#include <math.h>

namespace
{
    typedef iHipolyV3 V3;

    const F64 kPi = 3.14159265358979323846;

    // Vertices closer than this fraction of the mesh's extent are the same
    // vertex. Absolute would not do: a level is a thousand units across and
    // a character's forearm is thinner than a thousandth of one.
    const F64 kWeldFraction = 2e-6;

    inline V3 v3(F64 x, F64 y, F64 z)
    {
        V3 r = { x, y, z };
        return r;
    }
    inline V3 add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
    inline V3 sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
    inline V3 scale(V3 a, F64 s) { return v3(a.x * s, a.y * s, a.z * s); }
    inline F64 dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline V3 cross(V3 a, V3 b)
    {
        return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
    }
    inline F64 len(V3 a) { return sqrt(dot(a, a)); }
    inline V3 unit(V3 a)
    {
        F64 l = len(a);
        return l > 0.0 ? scale(a, 1.0 / l) : a;
    }
    inline F64 clampd(F64 v, F64 lo, F64 hi) { return v < lo ? lo : (v > hi ? hi : v); }
    inline F64 dmin(F64 a, F64 b) { return a < b ? a : b; }
    inline F64 dmax(F64 a, F64 b) { return a > b ? a : b; }
    inline U32 umin(U32 a, U32 b) { return a < b ? a : b; }
    inline U32 umax(U32 a, U32 b) { return a > b ? a : b; }

    // -----------------------------------------------------------------------
    // A hash map from three 64-bit keys to a 32-bit value. Open addressing;
    // never shrinks; sized for what the caller says it will hold.

    struct Key3Map
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

    // -----------------------------------------------------------------------
    // Sorting indices by a 64-bit key. Heap sort: in place, no recursion,
    // and the order of equal keys does not matter where it is used.

    void sortIndicesU64(U32* idx, U32 n, const U64* key);
}

void iHipolySortU64(U32* idx, U32 n, const U64* key)
{
    sortIndicesU64(idx, n, key);
}

namespace
{
    void sortIndicesU64(U32* idx, U32 n, const U64* key)
    {
        if (n < 2)
        {
            return;
        }
        for (S32 start = (S32)(n / 2) - 1; start >= 0; start--)
        {
            U32 root = (U32)start;
            for (;;)
            {
                U32 child = 2 * root + 1;
                if (child >= n)
                {
                    break;
                }
                if (child + 1 < n && key[idx[child]] < key[idx[child + 1]])
                {
                    child++;
                }
                if (key[idx[root]] < key[idx[child]])
                {
                    U32 t = idx[root];
                    idx[root] = idx[child];
                    idx[child] = t;
                    root = child;
                }
                else
                {
                    break;
                }
            }
        }
        for (U32 end = n - 1; end > 0; end--)
        {
            U32 t = idx[0];
            idx[0] = idx[end];
            idx[end] = t;
            U32 root = 0;
            for (;;)
            {
                U32 child = 2 * root + 1;
                if (child >= end)
                {
                    break;
                }
                if (child + 1 < end && key[idx[child]] < key[idx[child + 1]])
                {
                    child++;
                }
                if (key[idx[root]] < key[idx[child]])
                {
                    U32 u = idx[root];
                    idx[root] = idx[child];
                    idx[child] = u;
                    root = child;
                }
                else
                {
                    break;
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Union-find over the corners, for the smoothing groups.

    struct UnionFind
    {
        iHipolyArray<U32> parent;

        void init(U32 n)
        {
            parent.resize(n);
            for (U32 i = 0; i < n; i++)
            {
                parent[i] = i;
            }
        }

        U32 find(U32 i)
        {
            U32 r = i;
            while (parent[r] != r)
            {
                r = parent[r];
            }
            while (parent[i] != r)
            {
                U32 next = parent[i];
                parent[i] = r;
                i = next;
            }
            return r;
        }

        void join(U32 a, U32 b)
        {
            a = find(a);
            b = find(b);
            if (a != b)
            {
                parent[a] = b;
            }
        }
    };

    // -----------------------------------------------------------------------
    // The domain, concatenated into one vertex space.

    struct Domain
    {
        U32 numGeoms;
        iHipolyArray<U32> voff;    // numGeoms + 1: first vertex of each geometry
        iHipolyArray<V3> P;
        iHipolyArray<V3> N;        // authored normals, where known
        iHipolyArray<U8> hasN;     // per vertex
        iHipolyArray<U32> T;       // nt * 3, global vertex ids
        iHipolyArray<U32> fgeom;
        iHipolyArray<U32> fmat;
        iHipolyArray<U32> flocal;
        U32 nt;
        F64 extent;
        iHipolyArray<U32> W;       // welded id per vertex
        U32 nw;
        iHipolyArray<V3> un;       // unit face normals
        iHipolyArray<V3> CN;       // nt * 3 corner normals
    };

    F64 extentOf(const iHipolyArray<V3>& P)
    {
        if (P.n == 0)
        {
            return 1.0;
        }
        V3 lo = P[0], hi = P[0];
        for (U32 i = 1; i < P.n; i++)
        {
            lo = v3(dmin(lo.x, P[i].x), dmin(lo.y, P[i].y), dmin(lo.z, P[i].z));
            hi = v3(dmax(hi.x, P[i].x), dmax(hi.y, P[i].y), dmax(hi.z, P[i].z));
        }
        return len(sub(hi, lo));
    }

    void weld(Domain& d)
    {
        F64 quantum = dmax(d.extent * kWeldFraction, 1e-9);
        Key3Map map;
        map.init(d.P.n);
        d.W.resize(d.P.n);
        d.nw = 0;
        for (U32 i = 0; i < d.P.n; i++)
        {
            S64 a = (S64)llround(d.P[i].x / quantum);
            S64 b = (S64)llround(d.P[i].y / quantum);
            S64 c = (S64)llround(d.P[i].z / quantum);
            bool inserted;
            d.W[i] = map.findOrInsert(a, b, c, d.nw, &inserted);
            if (inserted)
            {
                d.nw++;
            }
        }
    }

    void faceNormals(Domain& d)
    {
        d.un.resize(d.nt);
        for (U32 f = 0; f < d.nt; f++)
        {
            V3 a = d.P[d.T[f * 3]], b = d.P[d.T[f * 3 + 1]], c = d.P[d.T[f * 3 + 2]];
            d.un[f] = unit(cross(sub(b, a), sub(c, a)));
        }
    }

    // -----------------------------------------------------------------------
    // Edges: face-edge (f, c) runs corner c to corner c + 1. Every edge knows
    // its face-edges, in face order.

    struct Edges
    {
        U32 ne;
        iHipolyArray<U32> eid;      // nt * 3
        iHipolyArray<U8> forward;   // nt * 3: corner c is the edge's lo end
        iHipolyArray<U32> start;    // ne + 1, into fe
        iHipolyArray<U32> fe;       // face-edge indices f * 3 + c, grouped by edge
        iHipolyArray<U32> lo;       // ne: welded ids
        iHipolyArray<U32> hi;

        U32 count(U32 e) const { return start[e + 1] - start[e]; }
    };

    void buildEdges(const Domain& d, Edges& E)
    {
        Key3Map map;
        map.init(d.nt * 3);
        E.eid.resize(d.nt * 3);
        E.forward.resize(d.nt * 3);
        E.lo.clear();
        E.hi.clear();
        E.ne = 0;
        for (U32 f = 0; f < d.nt; f++)
        {
            for (U32 c = 0; c < 3; c++)
            {
                U32 a = d.W[d.T[f * 3 + c]];
                U32 b = d.W[d.T[f * 3 + (c + 1) % 3]];
                U32 lo = umin(a, b), hi = umax(a, b);
                bool inserted;
                U32 e = map.findOrInsert(lo, hi, 0, E.ne, &inserted);
                if (inserted)
                {
                    E.ne++;
                    E.lo.push(lo);
                    E.hi.push(hi);
                }
                E.eid[f * 3 + c] = e;
                E.forward[f * 3 + c] = a <= b;
            }
        }
        iHipolyArray<U32> counts;
        counts.resizeZero(E.ne);
        for (U32 i = 0; i < d.nt * 3; i++)
        {
            counts[E.eid[i]]++;
        }
        E.start.resize(E.ne + 1);
        E.start[0] = 0;
        for (U32 e = 0; e < E.ne; e++)
        {
            E.start[e + 1] = E.start[e] + counts[e];
        }
        E.fe.resize(d.nt * 3);
        for (U32 e = 0; e < E.ne; e++)
        {
            counts[e] = E.start[e];
        }
        for (U32 i = 0; i < d.nt * 3; i++)
        {
            E.fe[counts[E.eid[i]]++] = i;
        }
    }

    // -----------------------------------------------------------------------
    // Corner normals. Corners with authored normals use them. The rest get
    // smoothing-group normals: connected components of corners joined across
    // edges whose faces meet at less than the crease angle, each group's
    // corner-angle-weighted mean of its faces' normals.

    void cornerNormals(Domain& d, const Edges& E, const iHipolyParams& pr)
    {
        d.CN.resize(d.nt * 3);
        iHipolyArray<U8> have;
        have.resize(d.nt);
        U32 todo = 0;
        for (U32 f = 0; f < d.nt; f++)
        {
            U32 known = 0;
            for (U32 c = 0; c < 3; c++)
            {
                U32 v = d.T[f * 3 + c];
                if (d.hasN[v])
                {
                    d.CN[f * 3 + c] = d.N[v];
                    known++;
                }
            }
            have[f] = known == 3;
            if (!have[f])
            {
                todo++;
            }
        }
        if (todo == 0)
        {
            for (U32 i = 0; i < d.nt * 3; i++)
            {
                d.CN[i] = unit(d.CN[i]);
            }
            return;
        }

        // Groups: corners joined across smooth two-faced edges.
        UnionFind uf;
        uf.init(d.nt * 3);
        for (U32 e = 0; e < E.ne; e++)
        {
            if (E.count(e) != 2)
            {
                continue;
            }
            U32 fe1 = E.fe[E.start[e]], fe2 = E.fe[E.start[e] + 1];
            U32 f = fe1 / 3, g = fe2 / 3;
            F64 cf = cos((pr.creaseDegPerFace ? pr.creaseDegPerFace[f] : pr.creaseDeg) * kPi / 180.0);
            F64 cg = cos((pr.creaseDegPerFace ? pr.creaseDegPerFace[g] : pr.creaseDeg) * kPi / 180.0);
            if (dot(d.un[f], d.un[g]) < dmax(cf, cg))
            {
                continue;
            }
            // The corner at the lo end of each face-edge, and at the hi end.
            U32 c1 = fe1 % 3, c2 = fe2 % 3;
            U32 lo1 = E.forward[fe1] ? c1 : (c1 + 1) % 3, hi1 = E.forward[fe1] ? (c1 + 1) % 3 : c1;
            U32 lo2 = E.forward[fe2] ? c2 : (c2 + 1) % 3, hi2 = E.forward[fe2] ? (c2 + 1) % 3 : c2;
            uf.join(f * 3 + lo1, g * 3 + lo2);
            uf.join(f * 3 + hi1, g * 3 + hi2);
        }
        iHipolyArray<V3> acc;
        acc.resizeZero(d.nt * 3);
        for (U32 f = 0; f < d.nt; f++)
        {
            for (U32 c = 0; c < 3; c++)
            {
                V3 p = d.P[d.T[f * 3 + c]];
                V3 a = sub(d.P[d.T[f * 3 + (c + 1) % 3]], p);
                V3 b = sub(d.P[d.T[f * 3 + (c + 2) % 3]], p);
                F64 cosang = dot(a, b) / dmax(len(a) * len(b), 1e-12);
                F64 ang = acos(clampd(cosang, -1.0, 1.0));
                U32 r = uf.find(f * 3 + c);
                acc[r] = add(acc[r], scale(d.un[f], ang));
            }
        }
        for (U32 f = 0; f < d.nt; f++)
        {
            if (have[f])
            {
                for (U32 c = 0; c < 3; c++)
                {
                    d.CN[f * 3 + c] = unit(d.CN[f * 3 + c]);
                }
                continue;
            }
            for (U32 c = 0; c < 3; c++)
            {
                V3 n = unit(acc[uf.find(f * 3 + c)]);
                // A group that comes out zero (an isolated degenerate) takes
                // its face's.
                d.CN[f * 3 + c] = len(n) < 0.5 ? d.un[f] : n;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Edge classes that stay straight.

    // Half the angle between the two faces at each edge; 0 where an edge has
    // one face or more than two.
    void halfDihedral(const Domain& d, const Edges& E, iHipolyArray<F64>& half)
    {
        half.resizeZero(E.ne);
        for (U32 e = 0; e < E.ne; e++)
        {
            if (E.count(e) != 2)
            {
                continue;
            }
            U32 f = E.fe[E.start[e]] / 3, g = E.fe[E.start[e] + 1] / 3;
            half[e] = 0.5 * acos(clampd(dot(d.un[f], d.un[g]), -1.0, 1.0));
        }
    }

    // Edges with some other welded vertex lying strictly inside them: the
    // other side's triangles merely lie along it, or a vertex sits in its
    // middle. Such an edge is held together by being straight; curve it and
    // the crack opens.
    U32 splitEdges(const Domain& d, const Edges& E, iHipolyArray<U8>& split)
    {
        split.resizeZero(E.ne);
        // One representative position per welded vertex.
        iHipolyArray<V3> Q;
        Q.resize(d.nw);
        for (U32 v = 0; v < d.P.n; v++)
        {
            Q[d.W[v]] = d.P[v];
        }
        F64 eps = d.extent * 1e-5;
        // A uniform grid over the welded vertices, cells about a typical
        // edge long, as sorted (cell, vertex) pairs looked up by binary
        // search.
        F64 total = 0.0;
        for (U32 e = 0; e < E.ne; e++)
        {
            total += len(sub(Q[E.hi[e]], Q[E.lo[e]]));
        }
        F64 cell = E.ne ? total / E.ne : 1.0;
        cell = clampd(cell, d.extent / 1024.0, d.extent / 16.0);
        if (cell <= 0.0)
        {
            cell = 1.0;
        }
        V3 lo = Q.n ? Q[0] : v3(0, 0, 0);
        for (U32 i = 1; i < Q.n; i++)
        {
            lo = v3(dmin(lo.x, Q[i].x), dmin(lo.y, Q[i].y), dmin(lo.z, Q[i].z));
        }
        iHipolyArray<U64> ckey;
        iHipolyArray<U32> cvert;
        ckey.resize(Q.n);
        for (U32 i = 0; i < Q.n; i++)
        {
            U64 ix = (U64)((Q[i].x - lo.x) / cell), iy = (U64)((Q[i].y - lo.y) / cell),
                iz = (U64)((Q[i].z - lo.z) / cell);
            ckey[i] = (ix << 42) | (iy << 21) | iz;
        }
        iHipolyArray<U32> order;
        order.resize(Q.n);
        for (U32 i = 0; i < Q.n; i++)
        {
            order[i] = i;
        }
        sortIndicesU64(order.p, order.n, ckey.p);
        iHipolyArray<U64> sortedKey;
        sortedKey.resize(Q.n);
        cvert.resize(Q.n);
        for (U32 i = 0; i < Q.n; i++)
        {
            sortedKey[i] = ckey[order[i]];
            cvert[i] = order[i];
        }

        U32 found = 0;
        for (U32 e = 0; e < E.ne; e++)
        {
            V3 A = Q[E.lo[e]], B = Q[E.hi[e]];
            V3 D = sub(B, A);
            F64 L2 = dot(D, D);
            if (L2 < 1e-18)
            {
                continue;
            }
            V3 blo = v3(dmin(A.x, B.x) - eps, dmin(A.y, B.y) - eps, dmin(A.z, B.z) - eps);
            V3 bhi = v3(dmax(A.x, B.x) + eps, dmax(A.y, B.y) + eps, dmax(A.z, B.z) + eps);
            S64 x0 = (S64)floor((blo.x - lo.x) / cell), x1 = (S64)floor((bhi.x - lo.x) / cell);
            S64 y0 = (S64)floor((blo.y - lo.y) / cell), y1 = (S64)floor((bhi.y - lo.y) / cell);
            S64 z0 = (S64)floor((blo.z - lo.z) / cell), z1 = (S64)floor((bhi.z - lo.z) / cell);
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (z0 < 0) z0 = 0;
            bool hit = false;
            for (S64 ix = x0; ix <= x1 && !hit; ix++)
            {
                for (S64 iy = y0; iy <= y1 && !hit; iy++)
                {
                    for (S64 iz = z0; iz <= z1 && !hit; iz++)
                    {
                        U64 key = ((U64)ix << 42) | ((U64)iy << 21) | (U64)iz;
                        // lower bound
                        U32 a = 0, b = Q.n;
                        while (a < b)
                        {
                            U32 m = (a + b) / 2;
                            if (sortedKey[m] < key)
                            {
                                a = m + 1;
                            }
                            else
                            {
                                b = m;
                            }
                        }
                        for (; a < Q.n && sortedKey[a] == key; a++)
                        {
                            U32 c = cvert[a];
                            if (c == E.lo[e] || c == E.hi[e])
                            {
                                continue;
                            }
                            F64 t = dot(sub(Q[c], A), D) / L2;
                            if (t <= 1e-3 || t >= 1.0 - 1e-3)
                            {
                                continue;
                            }
                            V3 foot = add(A, scale(D, t));
                            if (len(sub(Q[c], foot)) < eps)
                            {
                                hit = true;
                                break;
                            }
                        }
                    }
                }
            }
            if (hit)
            {
                split[e] = 1;
                found++;
            }
        }
        return found;
    }

    // Welded vertices with both convex and concave edges around them. A
    // surface that is convex at one edge and concave at the next is texture
    // in the geometry -- SpongeBob's holes -- not a shape to smooth.
    void noisyVertices(const Domain& d, const Edges& E, const iHipolyArray<F64>& half,
                       iHipolyArray<U8>& noisy)
    {
        iHipolyArray<U8> convex, concave;
        convex.resizeZero(d.nw);
        concave.resizeZero(d.nw);
        for (U32 e = 0; e < E.ne; e++)
        {
            if (E.count(e) != 2)
            {
                continue;
            }
            U32 fe1 = E.fe[E.start[e]], fe2 = E.fe[E.start[e] + 1];
            U32 f = fe1 / 3, cf = fe1 % 3, g = fe2 / 3, cg = fe2 % 3;
            V3 onF = d.P[d.T[f * 3 + cf]];
            V3 oppG = d.P[d.T[g * 3 + (cg + 2) % 3]];
            F64 side = dot(sub(oppG, onF), d.un[f]);
            bool bent = 2.0 * half[e] > 5.0 * kPi / 180.0;
            if (!bent)
            {
                continue;
            }
            U32 a = d.W[d.T[f * 3 + cf]], b = d.W[d.T[f * 3 + (cf + 1) % 3]];
            if (side < 0.0)
            {
                convex[a] = convex[b] = 1;
            }
            else if (side > 0.0)
            {
                concave[a] = concave[b] = 1;
            }
        }
        noisy.resize(d.nw);
        for (U32 v = 0; v < d.nw; v++)
        {
            noisy[v] = convex[v] && concave[v];
        }
    }

    // -----------------------------------------------------------------------
    // The edge table: every edge's curve and level.

    struct EdgeTable
    {
        iHipolyArray<S32> level;   // ne
        iHipolyArray<V3> ctrl;     // ne * 2: the inner Bezier points, lo to hi
        iHipolyArray<V3> ends;     // ne * 2: lo, hi
    };

    void edgeTable(const Domain& d, const Edges& E, const iHipolyParams& pr, EdgeTable& et,
                   iHipolyStats* stats)
    {
        U32 nfe = d.nt * 3;
        iHipolyArray<V3> Plo, Phi, Nlo, Nhi;
        iHipolyArray<F64> L, wlo, whi, cap;
        Plo.resize(nfe); Phi.resize(nfe); Nlo.resize(nfe); Nhi.resize(nfe);
        L.resize(nfe); wlo.resize(nfe); whi.resize(nfe); cap.resize(nfe);
        for (U32 f = 0; f < d.nt; f++)
        {
            for (U32 c = 0; c < 3; c++)
            {
                U32 i = f * 3 + c;
                U32 c1 = (c + 1) % 3;
                V3 Pa = d.P[d.T[f * 3 + c]], Pb = d.P[d.T[f * 3 + c1]];
                V3 Na = d.CN[f * 3 + c], Nb = d.CN[f * 3 + c1];
                if (E.forward[i])
                {
                    Plo[i] = Pa; Phi[i] = Pb; Nlo[i] = Na; Nhi[i] = Nb;
                }
                else
                {
                    Plo[i] = Pb; Phi[i] = Pa; Nlo[i] = Nb; Nhi[i] = Na;
                }
                V3 dd = sub(Phi[i], Plo[i]);
                L[i] = len(dd);
                wlo[i] = dot(dd, Nlo[i]);     // (P1 - P0) . N0
                whi[i] = -dot(dd, Nhi[i]);    // (P0 - P1) . N1
                // An edge's midpoint moves by |w_lo N_lo + w_hi N_hi| / 8.
                // Long, nearly flat triangles -- the sand -- would otherwise
                // bow by whole units, and a floor that humps is a floor the
                // player notices. Clamp each term so the midpoint never moves
                // more than max_bulge, and the same in proportion: a crease
                // that borrows its neighbour's curve can otherwise bow a thin
                // limb's ring edges by a whole segment.
                F64 mb = pr.maxBulgePerFace ? pr.maxBulgePerFace[f] : pr.maxBulge;
                F64 rb = pr.relBulgePerFace ? pr.relBulgePerFace[f] : pr.relBulge;
                cap[i] = dmin(4.0 * mb, rb * L[i]);
            }
        }
        // One curve serves both faces on an edge, so the tighter face's cap
        // holds: a wall's wide cap must not hump the floor it stands on.
        iHipolyArray<F64> ecap;
        ecap.resize(E.ne);
        for (U32 e = 0; e < E.ne; e++)
        {
            F64 m = 1e300;
            for (U32 k = E.start[e]; k < E.start[e + 1]; k++)
            {
                m = dmin(m, cap[E.fe[k]]);
            }
            ecap[e] = m;
        }
        for (U32 i = 0; i < nfe; i++)
        {
            cap[i] = dmin(cap[i], ecap[E.eid[i]]);
        }
        // And no more than the geometry itself turns across the edge.
        // Authored normals on a bumpy surface -- SpongeBob's sponge -- tilt
        // far more than the faces do, and following them crumples every
        // silhouette. A surface sampled at these vertices turns by the
        // dihedral angle, so the normal at an end can be trusted to about
        // half of it. Open edges stay straight.
        iHipolyArray<F64> half;
        halfDihedral(d, E, half);
        for (U32 i = 0; i < nfe; i++)
        {
            cap[i] = dmin(cap[i], 1.5 * L[i] * sin(half[E.eid[i]]));
        }
        // A sharp feature stays sharp: past hard_deg the fold is authored
        // (the bumps of a sponge, the rim of a shoe), and rounding it reads
        // as crumpling. Off for the world, whose folds are rocks.
        if (pr.hardDeg >= 0.0)
        {
            F64 hard = pr.hardDeg * kPi / 180.0;
            for (U32 i = 0; i < nfe; i++)
            {
                if (2.0 * half[E.eid[i]] > hard)
                {
                    cap[i] = 0.0;
                }
            }
        }
        // Vertices with both kinds of edge around them pin their edges
        // straight; a sphere, all convex, still rounds.
        if (pr.noiseGuard)
        {
            iHipolyArray<U8> noisy;
            noisyVertices(d, E, half, noisy);
            for (U32 i = 0; i < nfe; i++)
            {
                U32 e = E.eid[i];
                if (noisy[E.lo[e]] || noisy[E.hi[e]])
                {
                    cap[i] = 0.0;
                }
            }
        }
        // An edge that only coincides with its neighbour -- one face, with
        // the other side's triangles merely lying along it, or a vertex
        // sitting in its middle -- is held together by being straight.
        iHipolyArray<U8> split;
        U32 tj = splitEdges(d, E, split);
        U32 open = 0;
        for (U32 e = 0; e < E.ne; e++)
        {
            if (E.count(e) == 1)
            {
                open++;
            }
        }
        for (U32 i = 0; i < nfe; i++)
        {
            U32 e = E.eid[i];
            if (E.count(e) == 1 || split[e])
            {
                cap[i] = 0.0;
            }
        }
        if (stats)
        {
            stats->openEdges = open;
            stats->tJunctions = tj;
        }
        // And never further than the thinnest triangle on the edge can take:
        // a bow deeper than the first interior row of samples folds that row
        // over, and a long sliver has its rows a fraction of a unit apart.
        // The midpoint moves by at most w/4; the row sits at altitude/level,
        // so decide the level from the bow so far, then cap by that level.
        iHipolyArray<F64> ealt, ebulge0, elen0;
        ealt.resize(E.ne); ebulge0.resizeZero(E.ne); elen0.resizeZero(E.ne);
        for (U32 e = 0; e < E.ne; e++)
        {
            ealt[e] = 1e300;
        }
        for (U32 f = 0; f < d.nt; f++)
        {
            V3 a = d.P[d.T[f * 3]], b = d.P[d.T[f * 3 + 1]], c = d.P[d.T[f * 3 + 2]];
            F64 area2 = len(cross(sub(b, a), sub(c, a)));
            for (U32 cc = 0; cc < 3; cc++)
            {
                U32 i = f * 3 + cc, e = E.eid[i];
                F64 Ls = dmax(L[i], 1e-9);
                ealt[e] = dmin(ealt[e], area2 / Ls);
                F64 pb = (fabs(clampd(wlo[i], -cap[i], cap[i])) + fabs(clampd(whi[i], -cap[i], cap[i]))) / Ls;
                ebulge0[e] = dmax(ebulge0[e], pb);
                elen0[e] = dmax(elen0[e], L[i]);
            }
        }
        for (U32 i = 0; i < nfe; i++)
        {
            U32 e = E.eid[i];
            F64 lvl0 = 1.0;
            if (ebulge0[e] >= pr.minBulge)
            {
                lvl0 = clampd(ceil(elen0[e] / pr.target), 2.0, (F64)pr.maxLevel);
            }
            cap[i] = dmin(cap[i], 2.0 * ealt[e] / lvl0);
        }
        iHipolyArray<F64> bulge;
        bulge.resize(nfe);
        for (U32 i = 0; i < nfe; i++)
        {
            wlo[i] = clampd(wlo[i], -cap[i], cap[i]);
            whi[i] = clampd(whi[i], -cap[i], cap[i]);
            bulge[i] = (fabs(wlo[i]) + fabs(whi[i])) / dmax(L[i], 1e-9);
        }
        // The face-edge that bends each edge the most wins; the first in
        // face order on a tie.
        et.level.resize(E.ne);
        et.ctrl.resize(E.ne * 2);
        et.ends.resize(E.ne * 2);
        for (U32 e = 0; e < E.ne; e++)
        {
            U32 win = E.fe[E.start[e]];
            for (U32 k = E.start[e] + 1; k < E.start[e + 1]; k++)
            {
                U32 i = E.fe[k];
                if (bulge[i] > bulge[win] || (bulge[i] == bulge[win] && i < win))
                {
                    win = i;
                }
            }
            et.ctrl[e * 2] = scale(sub(add(scale(Plo[win], 2.0), Phi[win]), scale(Nlo[win], wlo[win])), 1.0 / 3.0);
            et.ctrl[e * 2 + 1] = scale(sub(add(scale(Phi[win], 2.0), Plo[win]), scale(Nhi[win], whi[win])), 1.0 / 3.0);
            et.ends[e * 2] = Plo[win];
            et.ends[e * 2 + 1] = Phi[win];
            et.level[e] = 1;
            if (bulge[win] >= pr.minBulge)
            {
                et.level[e] = (S32)clampd(ceil(L[win] / pr.target), 2.0, (F64)pr.maxLevel);
            }
        }
    }

    // -----------------------------------------------------------------------
    // The barycentric grid for a level: points (i, j, k) with i + j + k = L,
    // the triangles over them, and per point which edge it lies on.

    struct Grid
    {
        S32 L;
        U32 npts;
        iHipolyArray<S32> ijk;      // npts * 3
        iHipolyArray<U32> tris;     // ntris * 3
        iHipolyArray<S32> edge;     // npts: -1, or the face edge it lies on
        iHipolyArray<S32> par;      // npts: the parameter along it, in L-ths
        iHipolyArray<S32> corner;   // npts: -1, or the corner it is
        U32 ntris() const { return tris.n / 3; }
    };

    void makeGrid(S32 L, Grid& g)
    {
        g.L = L;
        g.ijk.clear();
        g.tris.clear();
        // index[(i, j)] for i + j <= L
        iHipolyArray<U32> index;
        index.resize((U32)(L + 1) * (L + 1));
        U32 n = 0;
        for (S32 i = L; i >= 0; i--)
        {
            for (S32 j = L - i; j >= 0; j--)
            {
                S32 k = L - i - j;
                index[i * (L + 1) + j] = n++;
                g.ijk.push(i); g.ijk.push(j); g.ijk.push(k);
            }
        }
        g.npts = n;
        for (S32 i = 0; i < L; i++)
        {
            for (S32 j = 0; j < L - i; j++)
            {
                S32 k = L - i - j;
                U32 a = index[(i + 1) * (L + 1) + j];
                U32 b = index[i * (L + 1) + j + 1];
                U32 c = index[i * (L + 1) + j];
                g.tris.push(a); g.tris.push(b); g.tris.push(c);
                if (k >= 2)
                {
                    U32 dd = index[(i + 1) * (L + 1) + j + 1];
                    g.tris.push(a); g.tris.push(dd); g.tris.push(b);
                }
            }
        }
        // Edge c runs corner c -> c+1: edge 0 (k == 0) from corner 0 to 1,
        // parameter j; edge 1 (i == 0) from 1 to 2, parameter k; edge 2
        // (j == 0) from 2 to 0, parameter i.
        g.edge.resize(n);
        g.par.resize(n);
        g.corner.resize(n);
        for (U32 m = 0; m < n; m++)
        {
            S32 i = g.ijk[m * 3], j = g.ijk[m * 3 + 1], k = g.ijk[m * 3 + 2];
            g.edge[m] = -1;
            g.par[m] = 0;
            g.corner[m] = -1;
            if (k == 0 && i > 0 && j > 0)
            {
                g.edge[m] = 0; g.par[m] = j;
            }
            else if (i == 0 && j > 0 && k > 0)
            {
                g.edge[m] = 1; g.par[m] = k;
            }
            else if (j == 0 && k > 0 && i > 0)
            {
                g.edge[m] = 2; g.par[m] = i;
            }
            if (i == L) g.corner[m] = 0;
            if (j == L) g.corner[m] = 1;
            if (k == L) g.corner[m] = 2;
        }
    }

    inline V3 bezier3(V3 p0, V3 p1, V3 p2, V3 p3, F64 t)
    {
        F64 s = 1.0 - t;
        return add(add(scale(p0, s * s * s), scale(p1, 3.0 * s * s * t)),
                   add(scale(p2, 3.0 * s * t * t), scale(p3, t * t * t)));
    }

    // -----------------------------------------------------------------------
    // Output under construction, per geometry. Vertices are appended per
    // face and welded exactly at the end.

    struct Build
    {
        bool hasN;
        bool hasColor;
        U32 numUV;
        bool hasSkin;
        iHipolyArray<F32> pos, normal, color, uv[8], skinW;
        iHipolyArray<U8> skinI;
        iHipolyArray<U32> tris;     // *4
        iHipolyArray<U32> parent;
        iHipolyArray<F32> cbary;    // *9
        U32 nv() const { return pos.n / 3; }
    };

    // Blend bone weights across a face; keep the four strongest.
    void skinInterp(const iHipolyGeom& g, const U32* corners, const F64* bary, U8* outI, F32* outW)
    {
        U8 bones[12];
        F64 w[12];
        U32 n = 0;
        for (U32 c = 0; c < 3; c++)
        {
            for (U32 j = 0; j < 4; j++)
            {
                U8 bi = g.skinIndex[corners[c] * 4 + j];
                F64 bw = bary[c] * g.skinWeight[corners[c] * 4 + j];
                U32 k;
                for (k = 0; k < n; k++)
                {
                    if (bones[k] == bi)
                    {
                        w[k] += bw;
                        break;
                    }
                }
                if (k == n)
                {
                    bones[n] = bi;
                    w[n] = bw;
                    n++;
                }
            }
        }
        for (U32 j = 0; j < 4; j++)
        {
            outI[j] = 0;
            outW[j] = 0.0f;
        }
        F64 sum = 0.0;
        U32 picked = 0;
        for (U32 slot = 0; slot < 4 && slot < n; slot++)
        {
            U32 best = 0;
            F64 bw = -1.0;
            for (U32 k = 0; k < n; k++)
            {
                if (w[k] > bw)
                {
                    bw = w[k];
                    best = k;
                }
            }
            outI[slot] = bones[best];
            outW[slot] = (F32)w[best];
            sum += w[best];
            w[best] = -2.0;
            picked++;
        }
        F64 inv = 1.0 / dmax(sum, 1e-12);
        for (U32 j = 0; j < picked; j++)
        {
            outW[j] = (F32)(outW[j] * inv);
        }
    }

    // Quadratic PN normal interpolation at snapped barycentrics. A point on
    // a lower-level neighbour's edge has to take its normal at the parameter
    // it was moved to, or the two faces disagree along the edge and every
    // such edge shades as a seam.
    V3 pnNormal(const V3* CN, const V3* Pc, F64 u, F64 v, F64 w)
    {
        V3 n110, n011, n101;
        {
            V3 dd = sub(Pc[1], Pc[0]);
            F64 vv = 2.0 * dot(dd, add(CN[0], CN[1])) / dmax(dot(dd, dd), 1e-12);
            n110 = sub(add(CN[0], CN[1]), scale(dd, vv));
        }
        {
            V3 dd = sub(Pc[2], Pc[1]);
            F64 vv = 2.0 * dot(dd, add(CN[1], CN[2])) / dmax(dot(dd, dd), 1e-12);
            n011 = sub(add(CN[1], CN[2]), scale(dd, vv));
        }
        {
            V3 dd = sub(Pc[0], Pc[2]);
            F64 vv = 2.0 * dot(dd, add(CN[2], CN[0])) / dmax(dot(dd, dd), 1e-12);
            n101 = sub(add(CN[2], CN[0]), scale(dd, vv));
        }
        V3 out = add(add(scale(CN[0], u * u), scale(CN[1], v * v)), scale(CN[2], w * w));
        out = add(out, add(add(scale(n110, u * v), scale(n011, v * w)), scale(n101, w * u)));
        return unit(out);
    }

    void emitVertex(Build& b, const iHipolyGeom& g, const U32* corners, const F64* bary, V3 pos,
                    const V3* CN, const V3* Pc)
    {
        b.pos.push((F32)pos.x); b.pos.push((F32)pos.y); b.pos.push((F32)pos.z);
        if (b.hasN)
        {
            V3 n = pnNormal(CN, Pc, bary[0], bary[1], bary[2]);
            b.normal.push((F32)n.x); b.normal.push((F32)n.y); b.normal.push((F32)n.z);
        }
        if (b.hasColor)
        {
            for (U32 k = 0; k < 4; k++)
            {
                F64 v = 0.0;
                for (U32 c = 0; c < 3; c++)
                {
                    v += bary[c] * g.color[corners[c] * 4 + k];
                }
                b.color.push((F32)v);
            }
        }
        for (U32 s = 0; s < b.numUV; s++)
        {
            for (U32 k = 0; k < 2; k++)
            {
                F64 v = 0.0;
                for (U32 c = 0; c < 3; c++)
                {
                    v += bary[c] * g.uv[s][corners[c] * 2 + k];
                }
                b.uv[s].push((F32)v);
            }
        }
        if (b.hasSkin)
        {
            U8 si[4];
            F32 sw[4];
            skinInterp(g, corners, bary, si, sw);
            for (U32 k = 0; k < 4; k++)
            {
                b.skinI.push(si[k]);
                b.skinW.push(sw[k]);
            }
        }
    }

    // A level-1 face: its three vertices as they are.
    void emitFlat(Build& b, const iHipolyGeom& g, const U32* corners, U32 mat, U32 local)
    {
        U32 n0 = b.nv();
        for (U32 c = 0; c < 3; c++)
        {
            U32 v = corners[c];
            b.pos.push(g.pos[v * 3]); b.pos.push(g.pos[v * 3 + 1]); b.pos.push(g.pos[v * 3 + 2]);
            if (b.hasN)
            {
                V3 n = unit(v3(g.normal[v * 3], g.normal[v * 3 + 1], g.normal[v * 3 + 2]));
                b.normal.push((F32)n.x); b.normal.push((F32)n.y); b.normal.push((F32)n.z);
            }
            if (b.hasColor)
            {
                for (U32 k = 0; k < 4; k++)
                {
                    b.color.push((F32)g.color[v * 4 + k]);
                }
            }
            for (U32 s = 0; s < b.numUV; s++)
            {
                b.uv[s].push(g.uv[s][v * 2]);
                b.uv[s].push(g.uv[s][v * 2 + 1]);
            }
            if (b.hasSkin)
            {
                for (U32 k = 0; k < 4; k++)
                {
                    b.skinI.push(g.skinIndex[v * 4 + k]);
                    b.skinW.push(g.skinWeight[v * 4 + k]);
                }
            }
        }
        b.tris.push(n0); b.tris.push(n0 + 1); b.tris.push(n0 + 2); b.tris.push(mat);
        b.parent.push(local);
        static const F32 eye[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        for (U32 k = 0; k < 9; k++)
        {
            b.cbary.push(eye[k]);
        }
    }

    // -----------------------------------------------------------------------
    // Finishing: weld exactly equal vertices, drop what the snapping left
    // degenerate.

    void finish(Build& b, const iHipolyGeom& g, iHipolyResult& r, U32* folds)
    {
        U32 nv = b.nv();
        U32 stride = 3 + (b.hasN ? 3 : 0) + (b.hasColor ? 1 : 0) + b.numUV * 2 + (b.hasSkin ? 5 : 0);
        // The colour is rounded to what will be stored before welding, so two
        // samples that only differ past the eighth bit are one vertex.
        iHipolyArray<U32> key;
        key.resize(nv * stride);
        for (U32 v = 0; v < nv; v++)
        {
            U32* k = key.p + v * stride;
            U32 o = 0;
            memcpy(k + o, b.pos.p + v * 3, 12); o += 3;
            if (b.hasN)
            {
                memcpy(k + o, b.normal.p + v * 3, 12); o += 3;
            }
            if (b.hasColor)
            {
                U32 packed = 0;
                for (U32 c = 0; c < 4; c++)
                {
                    F64 x = b.color[v * 4 + c];
                    S32 q = (S32)nearbyint(clampd(x, 0.0, 255.0));
                    packed |= (U32)q << (8 * c);
                }
                k[o++] = packed;
            }
            for (U32 s = 0; s < b.numUV; s++)
            {
                memcpy(k + o, b.uv[s].p + v * 2, 8); o += 2;
            }
            if (b.hasSkin)
            {
                memcpy(k + o, b.skinI.p + v * 4, 4); o += 1;
                memcpy(k + o, b.skinW.p + v * 4, 16); o += 4;
            }
        }
        Key3Map map;
        map.init(nv);
        iHipolyArray<U32> remap, first;
        remap.resize(nv);
        for (U32 v = 0; v < nv; v++)
        {
            const U32* k = key.p + v * stride;
            U64 h1 = 0xcbf29ce484222325ULL, h2 = 0x84222325cbf29ce4ULL;
            for (U32 i = 0; i < stride; i++)
            {
                h1 = (h1 ^ k[i]) * 0x100000001b3ULL;
                h2 = (h2 ^ (k[i] * 0x9E3779B1u)) * 0x100000001b3ULL;
            }
            // Two hashes and the index of a candidate; a collision on both
            // is checked against the full key by probing on.
            for (;;)
            {
                bool inserted;
                U32 cand = map.findOrInsert((S64)h1, (S64)h2, 0, first.n, &inserted);
                if (inserted)
                {
                    first.push(v);
                    remap[v] = first.n - 1;
                    break;
                }
                if (memcmp(key.p + first[cand] * stride, k, stride * 4) == 0)
                {
                    remap[v] = cand;
                    break;
                }
                // A genuine 128-bit collision: perturb and retry.
                h2 += 0x9E3779B97F4A7C15ULL;
            }
        }
        U32 nu = first.n;
        r.nv = nu;
        r.pos.resize(nu * 3);
        if (b.hasN)
        {
            r.normal.resize(nu * 3);
        }
        if (b.hasColor)
        {
            r.color.resize(nu * 4);
        }
        r.numUV = b.numUV;
        for (U32 s = 0; s < b.numUV; s++)
        {
            r.uv[s].resize(nu * 2);
        }
        if (b.hasSkin)
        {
            r.skinIndex.resize(nu * 4);
            r.skinWeight.resize(nu * 4);
        }
        for (U32 u = 0; u < nu; u++)
        {
            U32 v = first[u];
            memcpy(r.pos.p + u * 3, b.pos.p + v * 3, 12);
            if (b.hasN)
            {
                memcpy(r.normal.p + u * 3, b.normal.p + v * 3, 12);
            }
            if (b.hasColor)
            {
                for (U32 c = 0; c < 4; c++)
                {
                    r.color[u * 4 + c] = (U8)nearbyint(clampd(b.color[v * 4 + c], 0.0, 255.0));
                }
            }
            for (U32 s = 0; s < b.numUV; s++)
            {
                memcpy(r.uv[s].p + u * 2, b.uv[s].p + v * 2, 8);
            }
            if (b.hasSkin)
            {
                memcpy(r.skinIndex.p + u * 4, b.skinI.p + v * 4, 4);
                memcpy(r.skinWeight.p + u * 4, b.skinW.p + v * 4, 16);
            }
        }
        // Zero-area triangles that survived the index test (three distinct
        // vertices on a line) are dropped too.
        F64 ext = 1.0;
        {
            V3 lo = v3(1e300, 1e300, 1e300), hi = v3(-1e300, -1e300, -1e300);
            for (U32 u = 0; u < nu; u++)
            {
                V3 p = v3(r.pos[u * 3], r.pos[u * 3 + 1], r.pos[u * 3 + 2]);
                lo = v3(dmin(lo.x, p.x), dmin(lo.y, p.y), dmin(lo.z, p.z));
                hi = v3(dmax(hi.x, p.x), dmax(hi.y, p.y), dmax(hi.z, p.z));
            }
            ext = nu ? len(sub(hi, lo)) : 1.0;
        }
        F64 minArea = (ext * 1e-6) * (ext * 1e-6);
        U32 ntIn = b.tris.n / 4;
        r.tris.clear();
        r.parent.clear();
        r.cbary.clear();
        U32 folded = 0;
        for (U32 t = 0; t < ntIn; t++)
        {
            U32 a = remap[b.tris[t * 4]], bb = remap[b.tris[t * 4 + 1]], c = remap[b.tris[t * 4 + 2]];
            if (a == bb || bb == c || a == c)
            {
                continue;
            }
            V3 pa = v3(r.pos[a * 3], r.pos[a * 3 + 1], r.pos[a * 3 + 2]);
            V3 pb = v3(r.pos[bb * 3], r.pos[bb * 3 + 1], r.pos[bb * 3 + 2]);
            V3 pc = v3(r.pos[c * 3], r.pos[c * 3 + 1], r.pos[c * 3 + 2]);
            V3 cn = cross(sub(pb, pa), sub(pc, pa));
            if (len(cn) <= minArea)
            {
                continue;
            }
            // Children whose winding normal opposes their parent's: a
            // folded patch. Zero is the only acceptable number.
            U32 pf = b.parent[t];
            const U32* pt = g.tris + pf * 4;
            V3 q0 = v3(g.pos[pt[0] * 3], g.pos[pt[0] * 3 + 1], g.pos[pt[0] * 3 + 2]);
            V3 q1 = v3(g.pos[pt[1] * 3], g.pos[pt[1] * 3 + 1], g.pos[pt[1] * 3 + 2]);
            V3 q2 = v3(g.pos[pt[2] * 3], g.pos[pt[2] * 3 + 1], g.pos[pt[2] * 3 + 2]);
            if (dot(cn, cross(sub(q1, q0), sub(q2, q0))) < 0.0)
            {
                folded++;
            }
            r.tris.push(a); r.tris.push(bb); r.tris.push(c); r.tris.push(b.tris[t * 4 + 3]);
            r.parent.push(pf);
            for (U32 k = 0; k < 9; k++)
            {
                r.cbary.push(b.cbary[t * 9 + k]);
            }
        }
        r.nt = r.tris.n / 4;
        *folds += folded;
    }
}

// ---------------------------------------------------------------------------

void iHipolyRefine(const iHipolyGeom* geoms, U32 numGeoms, const iHipolyParams& pr,
                   iHipolyResult* out, iHipolyStats* stats)
{
    if (stats)
    {
        stats->openEdges = stats->tJunctions = stats->folds = 0;
    }
    Domain d;
    d.numGeoms = numGeoms;
    d.voff.resize(numGeoms + 1);
    d.voff[0] = 0;
    U32 ntTot = 0;
    for (U32 g = 0; g < numGeoms; g++)
    {
        d.voff[g + 1] = d.voff[g] + geoms[g].nv;
        ntTot += geoms[g].nt;
    }
    U32 nvTot = d.voff[numGeoms];
    d.P.resize(nvTot);
    d.N.resizeZero(nvTot);
    d.hasN.resizeZero(nvTot);
    d.T.resize(ntTot * 3);
    d.fgeom.resize(ntTot);
    d.fmat.resize(ntTot);
    d.flocal.resize(ntTot);
    d.nt = ntTot;
    {
        U32 f = 0;
        for (U32 g = 0; g < numGeoms; g++)
        {
            const iHipolyGeom& G = geoms[g];
            for (U32 v = 0; v < G.nv; v++)
            {
                U32 gv = d.voff[g] + v;
                d.P[gv] = v3(G.pos[v * 3], G.pos[v * 3 + 1], G.pos[v * 3 + 2]);
                if (G.normal)
                {
                    d.N[gv] = unit(v3(G.normal[v * 3], G.normal[v * 3 + 1], G.normal[v * 3 + 2]));
                    d.hasN[gv] = 1;
                }
            }
            for (U32 t = 0; t < G.nt; t++, f++)
            {
                d.T[f * 3] = d.voff[g] + G.tris[t * 4];
                d.T[f * 3 + 1] = d.voff[g] + G.tris[t * 4 + 1];
                d.T[f * 3 + 2] = d.voff[g] + G.tris[t * 4 + 2];
                d.fgeom[f] = g;
                d.fmat[f] = G.tris[t * 4 + 3];
                d.flocal[f] = t;
            }
        }
    }
    d.extent = extentOf(d.P);
    weld(d);
    faceNormals(d);
    Edges E;
    buildEdges(d, E);
    cornerNormals(d, E, pr);
    EdgeTable et;
    edgeTable(d, E, pr, et, stats);

    // Per-geometry budgets: lower the levels of any geometry that would
    // overflow a 16-bit index buffer, in vertices, or -- the tighter bound --
    // in triangles: a JSP collision record names a vertex by its position in
    // the atomic's expanded index buffer, in 16 bits, so an atomic may hold
    // at most 21845 triangles or its collision points at the wrong vertices.
    iHipolyArray<S32> flevel;
    flevel.resize(d.nt);
    for (;;)
    {
        iHipolyArray<F64> estV, estT;
        estV.resizeZero(numGeoms);
        estT.resizeZero(numGeoms);
        for (U32 f = 0; f < d.nt; f++)
        {
            S32 L = et.level[E.eid[f * 3]];
            L = L > et.level[E.eid[f * 3 + 1]] ? L : et.level[E.eid[f * 3 + 1]];
            L = L > et.level[E.eid[f * 3 + 2]] ? L : et.level[E.eid[f * 3 + 2]];
            flevel[f] = L;
            estV[d.fgeom[f]] += (F64)(L + 1) * (L + 2) / 2.0;
            estT[d.fgeom[f]] += (F64)L * L;
        }
        bool over = false;
        for (U32 g = 0; g < numGeoms; g++)
        {
            if (estV[g] > pr.maxVerts || estT[g] > pr.maxTris)
            {
                over = true;
                // Every edge of the geometry one level down, once each: an
                // edge is shared by two of its faces.
                iHipolyArray<U8> touched;
                touched.resizeZero(E.ne);
                for (U32 f = 0; f < d.nt; f++)
                {
                    if (d.fgeom[f] != g)
                    {
                        continue;
                    }
                    for (U32 c = 0; c < 3; c++)
                    {
                        U32 e = E.eid[f * 3 + c];
                        if (touched[e])
                        {
                            continue;
                        }
                        touched[e] = 1;
                        if (et.level[e] > 1)
                        {
                            et.level[e]--;
                        }
                    }
                }
            }
        }
        if (!over)
        {
            break;
        }
    }

    // Interior sample positions per edge.
    iHipolyArray<U32> eoff;
    eoff.resize(E.ne + 1);
    eoff[0] = 0;
    for (U32 e = 0; e < E.ne; e++)
    {
        eoff[e + 1] = eoff[e] + (U32)(et.level[e] - 1);
    }
    iHipolyArray<V3> epts;
    epts.resize(eoff[E.ne]);
    for (U32 e = 0; e < E.ne; e++)
    {
        S32 lv = et.level[e];
        for (S32 k = 1; k < lv; k++)
        {
            epts[eoff[e] + (U32)(k - 1)] = bezier3(et.ends[e * 2], et.ctrl[e * 2], et.ctrl[e * 2 + 1],
                                                    et.ends[e * 2 + 1], (F64)k / lv);
        }
    }

    Grid grids[16];
    for (S32 L = 1; L <= pr.maxLevel && L < 16; L++)
    {
        makeGrid(L, grids[L]);
    }

    Build* builds = new Build[numGeoms];
    for (U32 g = 0; g < numGeoms; g++)
    {
        Build& b = builds[g];
        b.hasN = geoms[g].normal != NULL;
        b.hasColor = geoms[g].color != NULL;
        b.numUV = geoms[g].numUV;
        b.hasSkin = geoms[g].skinIndex != NULL;
    }

    for (U32 f = 0; f < d.nt; f++)
    {
        U32 g = d.fgeom[f];
        Build& b = builds[g];
        const iHipolyGeom& G = geoms[g];
        U32 corners[3] = { d.T[f * 3] - d.voff[g], d.T[f * 3 + 1] - d.voff[g], d.T[f * 3 + 2] - d.voff[g] };
        S32 L = flevel[f];
        if (L <= 1)
        {
            emitFlat(b, G, corners, d.fmat[f], d.flocal[f]);
            continue;
        }
        const Grid& gr = grids[L];
        V3 Pc[3] = { d.P[d.T[f * 3]], d.P[d.T[f * 3 + 1]], d.P[d.T[f * 3 + 2]] };
        const V3* CN = d.CN.p + f * 3;
        // The ten control points, from the shared edge curves. Edge points
        // come from the table in the face's own orientation.
        V3 b300 = Pc[0], b030 = Pc[1], b003 = Pc[2];
        V3 b210, b120, b021, b012, b102, b201;
        {
            U32 e0 = E.eid[f * 3], e1 = E.eid[f * 3 + 1], e2 = E.eid[f * 3 + 2];
            bool f0 = E.forward[f * 3], f1 = E.forward[f * 3 + 1], f2 = E.forward[f * 3 + 2];
            b210 = f0 ? et.ctrl[e0 * 2] : et.ctrl[e0 * 2 + 1];
            b120 = f0 ? et.ctrl[e0 * 2 + 1] : et.ctrl[e0 * 2];
            b021 = f1 ? et.ctrl[e1 * 2] : et.ctrl[e1 * 2 + 1];
            b012 = f1 ? et.ctrl[e1 * 2 + 1] : et.ctrl[e1 * 2];
            b102 = f2 ? et.ctrl[e2 * 2] : et.ctrl[e2 * 2 + 1];
            b201 = f2 ? et.ctrl[e2 * 2 + 1] : et.ctrl[e2 * 2];
        }
        V3 Ec = scale(add(add(add(b210, b120), add(b021, b012)), add(b102, b201)), 1.0 / 6.0);
        V3 Vc = scale(add(add(Pc[0], Pc[1]), Pc[2]), 1.0 / 3.0);
        V3 b111 = add(Ec, scale(sub(Ec, Vc), 0.5));

        U32 n0 = b.nv();
        for (U32 m = 0; m < gr.npts; m++)
        {
            F64 bary[3];
            V3 pos;
            if (gr.corner[m] >= 0)
            {
                U32 c = (U32)gr.corner[m];
                bary[0] = bary[1] = bary[2] = 0.0;
                bary[c] = 1.0;
                pos = Pc[c];
            }
            else if (gr.edge[m] >= 0)
            {
                U32 c = (U32)gr.edge[m];
                U32 e = E.eid[f * 3 + c];
                S32 lv = et.level[e];
                bool fwd = E.forward[f * 3 + c];
                // Parameter along corner c -> c+1 is par/L; snap to the
                // edge's own level, then orient into lo->hi. Half-way cases
                // round to even, which is what makes the two faces sharing
                // the edge agree on the sample from either end.
                S32 s = (S32)nearbyint((F64)gr.par[m] * lv / L);
                S32 sLoHi = fwd ? s : lv - s;
                bool isEnd0 = sLoHi == 0, isEnd1 = sLoHi == lv;
                // A sample that snapped onto an endpoint takes THIS face's
                // own corner, not the table's: two shipped vertices within
                // the weld distance are one edge but two positions, and a
                // corner taken from the other one leaves a sliver a
                // thousandth wide beside the real corner, with a winding of
                // its own.
                bool at0 = fwd ? isEnd0 : isEnd1;
                bool at1 = fwd ? isEnd1 : isEnd0;
                if (at0)
                {
                    pos = Pc[c];
                }
                else if (at1)
                {
                    pos = Pc[(c + 1) % 3];
                }
                else
                {
                    pos = epts[eoff[e] + (U32)(sLoHi - 1)];
                }
                F64 t = (F64)sLoHi / lv;
                if (!fwd)
                {
                    t = 1.0 - t;
                }
                bary[0] = bary[1] = bary[2] = 0.0;
                bary[c] = 1.0 - t;
                bary[(c + 1) % 3] = t;
            }
            else
            {
                F64 u = (F64)gr.ijk[m * 3] / L, v = (F64)gr.ijk[m * 3 + 1] / L, w = (F64)gr.ijk[m * 3 + 2] / L;
                bary[0] = u; bary[1] = v; bary[2] = w;
                pos = scale(b300, u * u * u);
                pos = add(pos, scale(b030, v * v * v));
                pos = add(pos, scale(b003, w * w * w));
                pos = add(pos, scale(b210, 3.0 * u * u * v));
                pos = add(pos, scale(b120, 3.0 * u * v * v));
                pos = add(pos, scale(b021, 3.0 * v * v * w));
                pos = add(pos, scale(b012, 3.0 * v * w * w));
                pos = add(pos, scale(b102, 3.0 * u * w * w));
                pos = add(pos, scale(b201, 3.0 * u * u * w));
                pos = add(pos, scale(b111, 6.0 * u * v * w));
            }
            emitVertex(b, G, corners, bary, pos, CN, Pc);
            // Remember the barycentrics for the children's corners.
            b.cbary.push((F32)bary[0]); b.cbary.push((F32)bary[1]); b.cbary.push((F32)bary[2]);
        }
        // The barycentrics were pushed per point; the children take theirs
        // per corner, so rewrite that tail into per-triangle form.
        U32 baryAt = b.cbary.n - gr.npts * 3;
        iHipolyArray<F32> pt;
        pt.resize(gr.npts * 3);
        memcpy(pt.p, b.cbary.p + baryAt, gr.npts * 12);
        b.cbary.resize(baryAt);
        for (U32 t = 0; t < gr.ntris(); t++)
        {
            for (U32 c = 0; c < 3; c++)
            {
                U32 m = gr.tris[t * 3 + c];
                b.tris.push(n0 + m);
            }
            b.tris.push(d.fmat[f]);
            b.parent.push(d.flocal[f]);
            for (U32 c = 0; c < 3; c++)
            {
                for (U32 k = 0; k < 3; k++)
                {
                    b.cbary.push(pt[gr.tris[t * 3 + c] * 3 + k]);
                }
            }
        }
    }

    U32 folds = 0;
    for (U32 g = 0; g < numGeoms; g++)
    {
        finish(builds[g], geoms[g], out[g], &folds);
    }
    delete[] builds;
    if (stats)
    {
        stats->folds = folds;
    }
}
