// Fillet the sharp folds of the natural surfaces.
//
// PN tessellation bows a face across a crease but cannot round the crease
// itself: the smoothed normal along a fold is perpendicular to it, so the
// fold's own edges stay straight and a boulder keeps its rim. This pass
// smooths the refined mesh within `radius` of any edge where two landscape
// faces meet at more than `angle` degrees, Taubin-style so the band does not
// shrink, with the effect fading to nothing at the band's edge.
//
// Pinned: vertices on a boundary edge, vertices touching a face that is not
// landscape. A vertex of a flat floor or a grass cap moves vertically only,
// and only down, at most `floorMove`; any other at most `maxMove`.
//
// Guards, each of which was found necessary on a level:
//
//   - No triangle may fold over. One that the smoothing turned over collides
//     the wrong way; its vertices are held where they were.
//   - The level is built of sheets laid over one another: a grass cap over
//     an open-topped rock wall, its rim hiding the wall's top edge. A rim
//     rounded downward can sink below that edge and the wall pokes through,
//     so every vertex stops short of any steep landscape face it would pass
//     through on its way. A curtain stands on every open edge of the steep
//     landscape, three units tall, so a rim cannot slide in over the wall's
//     top edge either.
//   - And the other way round: a wall vertex that was hidden under a cap must
//     stay under it once the cap has come down. It is sunk to just below the
//     cap's new height; the strip it drags down is inside the rock.
//
// A port of round_creases in mods/hipoly/displace.py on the pc-mod-hipoly
// branch.

#include "iHipolyFillet.h"

#include <math.h>

namespace
{
    typedef iHipolyV3 V3;

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
    inline F64 dmin(F64 a, F64 b) { return a < b ? a : b; }
    inline F64 dmax(F64 a, F64 b) { return a > b ? a : b; }

    const F64 kCoverNy = 0.75;      // a landscape face this upright is a cap or floor
    const F64 kCurtain = 3.0;       // units; how tall the curtain on an open wall edge is
    const F64 kProbe = 3.0;         // units; how far up a wall vertex looks for its cap

    // A grid cell key with room for negative coordinates.
    inline U64 cellKey(S64 x, S64 y, S64 z)
    {
        const S64 off = 1 << 20;
        return ((U64)(x + off) << 42) | ((U64)(y + off) << 21) | (U64)(z + off);
    }

    // The welded refined mesh: every result's positions as one array, and
    // one index per distinct position so a seam vertex owned by two atomics
    // is one vertex.
    struct Mesh
    {
        iHipolyArray<U32> offs;    // per result, into P
        iHipolyArray<V3> P;
        iHipolyArray<U32> T;       // nt * 3, into P
        iHipolyArray<U32> W;       // per P: welded id
        iHipolyArray<U32> WT;      // nt * 3, welded
        U32 nw;
        U32 nt;
    };

    void weldResults(iHipolyResult* res, U32 n, Mesh& m)
    {
        m.offs.resize(n + 1);
        m.offs[0] = 0;
        U32 nt = 0;
        for (U32 k = 0; k < n; k++)
        {
            m.offs[k + 1] = m.offs[k] + res[k].nv;
            nt += res[k].nt;
        }
        U32 nv = m.offs[n];
        m.P.resize(nv);
        m.T.resize(nt * 3);
        U32 t = 0;
        for (U32 k = 0; k < n; k++)
        {
            for (U32 v = 0; v < res[k].nv; v++)
            {
                m.P[m.offs[k] + v] = v3(res[k].pos[v * 3], res[k].pos[v * 3 + 1], res[k].pos[v * 3 + 2]);
            }
            for (U32 i = 0; i < res[k].nt; i++, t++)
            {
                m.T[t * 3] = m.offs[k] + res[k].tris[i * 4];
                m.T[t * 3 + 1] = m.offs[k] + res[k].tris[i * 4 + 1];
                m.T[t * 3 + 2] = m.offs[k] + res[k].tris[i * 4 + 2];
            }
        }
        m.nt = nt;
        V3 lo = nv ? m.P[0] : v3(0, 0, 0), hi = lo;
        for (U32 v = 1; v < nv; v++)
        {
            lo = v3(dmin(lo.x, m.P[v].x), dmin(lo.y, m.P[v].y), dmin(lo.z, m.P[v].z));
            hi = v3(dmax(hi.x, m.P[v].x), dmax(hi.y, m.P[v].y), dmax(hi.z, m.P[v].z));
        }
        F64 extent = nv ? len(sub(hi, lo)) : 1.0;
        F64 quantum = dmax(extent * 2e-6, 1e-9);
        // Quantised positions packed into one key and sorted: a hash table
        // over half a million vertices is more memory than a 32-bit process
        // wants to give at once, and sorted keys are a tenth of it.
        iHipolyArray<U64> key;
        iHipolyArray<U32> order;
        key.resize(nv);
        order.resize(nv);
        for (U32 v = 0; v < nv; v++)
        {
            key[v] = cellKey((S64)llround(m.P[v].x / quantum), (S64)llround(m.P[v].y / quantum),
                             (S64)llround(m.P[v].z / quantum));
            order[v] = v;
        }
        iHipolySortU64(order.p, nv, key.p);
        m.W.resize(nv);
        m.nw = 0;
        for (U32 i = 0; i < nv; i++)
        {
            if (i > 0 && key[order[i]] != key[order[i - 1]])
            {
                m.nw++;
            }
            m.W[order[i]] = m.nw;
        }
        if (nv)
        {
            m.nw++;
        }
        m.WT.resize(nt * 3);
        for (U32 i = 0; i < nt * 3; i++)
        {
            m.WT[i] = m.W[m.T[i]];
        }
    }

    // For each vertex, the parameter t in (0, 1] at which the segment
    // V0 -> V0 + mv first passes through a candidate triangle that the
    // vertex is not a corner of; 1.0 where it passes through none. The
    // triangles sit at positions `V`. Triangles spanning more than
    // `maxSpan` cells on an axis are not tested.
    void firstCrossing(const V3* V0, const V3* mv, U32 nv, const U32* WT, U32 nt, const U8* cand,
                       const V3* V, F64* tHit, F64 cell = 1.0, S64 maxSpan = 6)
    {
        for (U32 v = 0; v < nv; v++)
        {
            tHit[v] = 1.0;
        }
        // Cells each candidate triangle touches, sorted by cell.
        iHipolyArray<U64> keys;
        iHipolyArray<U32> tids;
        for (U32 t = 0; t < nt; t++)
        {
            if (!cand[t])
            {
                continue;
            }
            const V3* p[3] = { &V[WT[t * 3]], &V[WT[t * 3 + 1]], &V[WT[t * 3 + 2]] };
            S64 lo[3], hi[3];
            bool ok = true;
            for (U32 a = 0; a < 3; a++)
            {
                F64 mn = (&p[0]->x)[a], mx = mn;
                for (U32 c = 1; c < 3; c++)
                {
                    F64 x = (&p[c]->x)[a];
                    mn = dmin(mn, x);
                    mx = dmax(mx, x);
                }
                lo[a] = (S64)floor(mn / cell);
                hi[a] = (S64)floor(mx / cell);
                if (hi[a] - lo[a] + 1 > maxSpan)
                {
                    ok = false;
                }
            }
            if (!ok)
            {
                continue;
            }
            for (S64 x = lo[0]; x <= hi[0]; x++)
            {
                for (S64 y = lo[1]; y <= hi[1]; y++)
                {
                    for (S64 z = lo[2]; z <= hi[2]; z++)
                    {
                        keys.push(cellKey(x, y, z));
                        tids.push(t);
                    }
                }
            }
        }
        if (keys.n == 0)
        {
            return;
        }
        iHipolyArray<U32> order;
        order.resize(keys.n);
        for (U32 i = 0; i < keys.n; i++)
        {
            order[i] = i;
        }
        iHipolySortU64(order.p, order.n, keys.p);
        iHipolyArray<U64> skey;
        iHipolyArray<U32> stid;
        skey.resize(keys.n);
        stid.resize(keys.n);
        for (U32 i = 0; i < keys.n; i++)
        {
            skey[i] = keys[order[i]];
            stid[i] = tids[order[i]];
        }

        for (U32 v = 0; v < nv; v++)
        {
            V3 d = mv[v];
            if (len(d) <= 1e-9)
            {
                continue;
            }
            V3 o = V0[v];
            V3 e = add(o, d);
            S64 slo[3], shi[3];
            for (U32 a = 0; a < 3; a++)
            {
                slo[a] = (S64)floor(dmin((&o.x)[a], (&e.x)[a]) / cell);
                shi[a] = (S64)floor(dmax((&o.x)[a], (&e.x)[a]) / cell);
            }
            for (S64 x = slo[0]; x <= shi[0]; x++)
            {
                for (S64 y = slo[1]; y <= shi[1]; y++)
                {
                    for (S64 z = slo[2]; z <= shi[2]; z++)
                    {
                        U64 key = cellKey(x, y, z);
                        U32 a = 0, b = skey.n;
                        while (a < b)
                        {
                            U32 mid = (a + b) / 2;
                            if (skey[mid] < key)
                            {
                                a = mid + 1;
                            }
                            else
                            {
                                b = mid;
                            }
                        }
                        for (; a < skey.n && skey[a] == key; a++)
                        {
                            U32 t = stid[a];
                            U32 w0 = WT[t * 3], w1 = WT[t * 3 + 1], w2 = WT[t * 3 + 2];
                            if (w0 == v || w1 == v || w2 == v)
                            {
                                continue;
                            }
                            // Moller-Trumbore, segment o + t d, t in (0, 1].
                            V3 p0 = V[w0], e1 = sub(V[w1], p0), e2 = sub(V[w2], p0);
                            V3 h = cross(d, e2);
                            F64 det = dot(e1, h);
                            if (fabs(det) < 1e-12)
                            {
                                continue;
                            }
                            F64 inv = 1.0 / det;
                            V3 s = sub(o, p0);
                            F64 u = dot(s, h) * inv;
                            V3 q = cross(s, e1);
                            F64 vv = dot(d, q) * inv;
                            F64 tt = dot(e2, q) * inv;
                            if (u >= -1e-6 && vv >= -1e-6 && u + vv <= 1.0 + 1e-6 && tt > 1e-6 && tt <= 1.0)
                            {
                                tHit[v] = dmin(tHit[v], tt);
                            }
                        }
                    }
                }
            }
        }
    }

    // Hold the vertices of any triangle the move turned over, until none is.
    void unfold(const Mesh& m, const V3* fn, iHipolyArray<V3>& mv, const V3* V0, iHipolyArray<V3>& V)
    {
        for (U32 pass = 0; pass < 8; pass++)
        {
            for (U32 w = 0; w < m.nw; w++)
            {
                V[w] = add(V0[w], mv[w]);
            }
            bool any = false;
            for (U32 t = 0; t < m.nt; t++)
            {
                U32 a = m.WT[t * 3], b = m.WT[t * 3 + 1], c = m.WT[t * 3 + 2];
                V3 nn = cross(sub(V[b], V[a]), sub(V[c], V[a]));
                if (dot(nn, fn[t]) < 0.0)
                {
                    mv[a] = mv[b] = mv[c] = v3(0, 0, 0);
                    any = true;
                }
            }
            if (!any)
            {
                break;
            }
        }
    }
}

void iHipolyFillet(iHipolyResult* res, U32 n, const U8* const* natural, const U8* const* landable,
                   const iHipolyFilletParams& pr, iHipolyFilletStats* st)
{
    memset(st, 0, sizeof(*st));
    Mesh m;
    weldResults(res, n, m);
    if (m.nt == 0 || m.nw == 0)
    {
        return;
    }
    // Face normals, and per face: landscape, flat floor.
    iHipolyArray<V3> fn, un;
    fn.resize(m.nt);
    un.resize(m.nt);
    iHipolyArray<U8> nat, isFloor;
    nat.resize(m.nt);
    isFloor.resize(m.nt);
    {
        U32 t = 0;
        for (U32 k = 0; k < n; k++)
        {
            for (U32 i = 0; i < res[k].nt; i++, t++)
            {
                V3 a = m.P[m.T[t * 3]], b = m.P[m.T[t * 3 + 1]], c = m.P[m.T[t * 3 + 2]];
                fn[t] = cross(sub(b, a), sub(c, a));
                F64 l = len(fn[t]);
                un[t] = l > 1e-12 ? scale(fn[t], 1.0 / l) : v3(0, 0, 0);
                nat[t] = natural[k][i];
                isFloor[t] = landable[k][i] && un[t].y > kCoverNy;
            }
        }
    }

    // Edges with their two faces.
    iHipolyArray<U32> elo, ehi, ecnt, efa, efb;
    {
        iHipolyArray<U64> key;
        iHipolyArray<U32> order;
        key.resize(m.nt * 3);
        order.resize(m.nt * 3);
        for (U32 t = 0; t < m.nt; t++)
        {
            for (U32 c = 0; c < 3; c++)
            {
                U32 a = m.WT[t * 3 + c], b = m.WT[t * 3 + (c + 1) % 3];
                U32 lo = a < b ? a : b, hi = a < b ? b : a;
                key[t * 3 + c] = ((U64)lo << 32) | hi;
                order[t * 3 + c] = t * 3 + c;
            }
        }
        iHipolySortU64(order.p, order.n, key.p);
        for (U32 i = 0; i < order.n; i++)
        {
            U64 k = key[order[i]];
            U32 t = order[i] / 3;
            if (i == 0 || k != key[order[i - 1]])
            {
                elo.push((U32)(k >> 32));
                ehi.push((U32)(k & 0xffffffffu));
                ecnt.push(0);
                efa.push(t);
                efb.push(t);
            }
            U32 e = elo.n - 1;
            if (ecnt[e] == 1)
            {
                efb[e] = t;
            }
            ecnt[e]++;
        }
    }
    U32 ne = elo.n;

    // Seeds: the ends of every sharp fold between two landscape faces.
    F64 cosAngle = cos(pr.angleDeg * 3.14159265358979323846 / 180.0);
    iHipolyArray<U8> seed;
    seed.resizeZero(m.nw);
    iHipolyArray<U32> seedList;
    for (U32 e = 0; e < ne; e++)
    {
        if (ecnt[e] != 2)
        {
            continue;
        }
        U32 fa = efa[e], fb = efb[e];
        F64 c = dot(un[fa], un[fb]);
        c = c < -1.0 ? -1.0 : (c > 1.0 ? 1.0 : c);
        if (c < cosAngle && nat[fa] && nat[fb])
        {
            if (!seed[elo[e]]) { seed[elo[e]] = 1; seedList.push(elo[e]); }
            if (!seed[ehi[e]]) { seed[ehi[e]] = 1; seedList.push(ehi[e]); }
        }
    }
    st->seeds = seedList.n;
    if (seedList.n == 0)
    {
        return;
    }

    // Pins, floors, covers.
    iHipolyArray<U8> pinned, onFloor, onCover, wallv;
    pinned.resizeZero(m.nw);
    onFloor.resizeZero(m.nw);
    onCover.resizeZero(m.nw);
    wallv.resizeZero(m.nw);
    for (U32 e = 0; e < ne; e++)
    {
        if (ecnt[e] == 1)
        {
            pinned[elo[e]] = pinned[ehi[e]] = 1;
        }
    }
    iHipolyArray<U8> steep, cover;
    steep.resize(m.nt);
    cover.resize(m.nt);
    for (U32 t = 0; t < m.nt; t++)
    {
        steep[t] = nat[t] && un[t].y <= kCoverNy;
        cover[t] = nat[t] && un[t].y > kCoverNy;
        for (U32 c = 0; c < 3; c++)
        {
            U32 w = m.WT[t * 3 + c];
            if (!nat[t]) pinned[w] = 1;
            if (isFloor[t]) onFloor[w] = 1;
            if (cover[t]) onCover[w] = 1;
            if (steep[t]) wallv[w] = 1;
        }
    }
    for (U32 w = 0; w < m.nw; w++)
    {
        if (onFloor[w]) onCover[w] = 1;
    }

    // One position per welded vertex.
    iHipolyArray<V3> V0;
    V0.resize(m.nw);
    for (U32 v = 0; v < m.P.n; v++)
    {
        V0[m.W[v]] = m.P[v];
    }

    // The band: welded vertices within `radius` of a seed, with a weight
    // that fades to zero at the band's edge. Seeds in a grid of cells the
    // radius wide, so each vertex looks at 27 cells.
    iHipolyArray<F64> wgt;
    wgt.resizeZero(m.nw);
    {
        F64 cell = pr.radius > 0.0 ? pr.radius : 1.0;
        iHipolyArray<U64> keys;
        iHipolyArray<U32> order;
        keys.resize(seedList.n);
        order.resize(seedList.n);
        for (U32 i = 0; i < seedList.n; i++)
        {
            V3 p = V0[seedList[i]];
            keys[i] = cellKey((S64)floor(p.x / cell), (S64)floor(p.y / cell), (S64)floor(p.z / cell));
            order[i] = i;
        }
        iHipolySortU64(order.p, order.n, keys.p);
        iHipolyArray<U64> skey;
        iHipolyArray<U32> sidx;
        skey.resize(keys.n);
        sidx.resize(keys.n);
        for (U32 i = 0; i < keys.n; i++)
        {
            skey[i] = keys[order[i]];
            sidx[i] = seedList[order[i]];
        }
        U32 inBand = 0;
        for (U32 w = 0; w < m.nw; w++)
        {
            if (pinned[w])
            {
                continue;
            }
            V3 p = V0[w];
            S64 cx = (S64)floor(p.x / cell), cy = (S64)floor(p.y / cell), cz = (S64)floor(p.z / cell);
            F64 best = 1e300;
            for (S64 dx = -1; dx <= 1; dx++)
            {
                for (S64 dy = -1; dy <= 1; dy++)
                {
                    for (S64 dz = -1; dz <= 1; dz++)
                    {
                        U64 key = cellKey(cx + dx, cy + dy, cz + dz);
                        U32 a = 0, b = skey.n;
                        while (a < b)
                        {
                            U32 mid = (a + b) / 2;
                            if (skey[mid] < key) a = mid + 1; else b = mid;
                        }
                        for (; a < skey.n && skey[a] == key; a++)
                        {
                            best = dmin(best, len(sub(V0[sidx[a]], p)));
                        }
                    }
                }
            }
            if (best <= pr.radius)
            {
                F64 t = 1.0 - best / pr.radius;
                t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
                wgt[w] = t * t * (3.0 - 2.0 * t);
                inBand++;
            }
        }
        st->band = inBand;
    }

    // Taubin smoothing over the welded graph, uniform weights.
    iHipolyArray<F64> deg;
    deg.resizeZero(m.nw);
    for (U32 e = 0; e < ne; e++)
    {
        deg[elo[e]] += 1.0;
        deg[ehi[e]] += 1.0;
    }
    iHipolyArray<V3> V, s;
    V.resize(m.nw);
    s.resize(m.nw);
    for (U32 w = 0; w < m.nw; w++)
    {
        V[w] = V0[w];
    }
    for (U32 it = 0; it < pr.iters; it++)
    {
        F64 lam = (it % 2 == 0) ? 0.5 : -0.53;
        for (U32 w = 0; w < m.nw; w++)
        {
            s[w] = v3(0, 0, 0);
        }
        for (U32 e = 0; e < ne; e++)
        {
            s[elo[e]] = add(s[elo[e]], V[ehi[e]]);
            s[ehi[e]] = add(s[ehi[e]], V[elo[e]]);
        }
        for (U32 w = 0; w < m.nw; w++)
        {
            F64 d = deg[w] > 0.0 ? deg[w] : 1.0;
            V[w] = add(V[w], scale(sub(scale(s[w], 1.0 / d), V[w]), lam * wgt[w]));
        }
    }
    iHipolyArray<V3> mv;
    mv.resize(m.nw);
    iHipolyArray<F64> lim;
    lim.resize(m.nw);
    for (U32 w = 0; w < m.nw; w++)
    {
        mv[w] = sub(V[w], V0[w]);
        // A floor vertex only comes down. A cap's rim that also drew inward
        // uncovered the rock shoulder it used to overhang, and no guard on
        // the wall's faces catches that: the shoulder is below and outside
        // the rim, not in its way. The rim still rounds by drooping, and the
        // lip below it still curls in.
        if (onCover[w])
        {
            mv[w].x = 0.0;
            mv[w].z = 0.0;
        }
        lim[w] = onCover[w] ? pr.floorMove : pr.maxMove;
        F64 d = len(mv[w]);
        if (d > lim[w])
        {
            mv[w] = scale(mv[w], lim[w] / d);
        }
    }
    unfold(m, fn.p, mv, V0.p, V);

    // Stop every vertex short of any steep landscape face it would pass
    // through, with a curtain on every open edge of the steep landscape.
    {
        iHipolyArray<U32> be;
        for (U32 e = 0; e < ne; e++)
        {
            if (ecnt[e] == 1 && steep[efa[e]])
            {
                be.push(e);
            }
        }
        U32 nb = be.n;
        iHipolyArray<V3> Vc;
        iHipolyArray<U32> WTc;
        iHipolyArray<U8> candc;
        iHipolyArray<V3> mvc;
        Vc.resize(m.nw + 2 * nb);
        mvc.resize(m.nw + 2 * nb);
        for (U32 w = 0; w < m.nw; w++)
        {
            Vc[w] = add(V0[w], mv[w]);
            mvc[w] = mv[w];
        }
        for (U32 i = 0; i < nb; i++)
        {
            Vc[m.nw + i] = add(V0[elo[be[i]]], v3(0, kCurtain, 0));
            Vc[m.nw + nb + i] = add(V0[ehi[be[i]]], v3(0, kCurtain, 0));
            mvc[m.nw + i] = mvc[m.nw + nb + i] = v3(0, 0, 0);
        }
        WTc.resize((m.nt + 2 * nb) * 3);
        candc.resize(m.nt + 2 * nb);
        memcpy(WTc.p, m.WT.p, m.nt * 12);
        memcpy(candc.p, steep.p, m.nt);
        for (U32 i = 0; i < nb; i++)
        {
            U32 a = elo[be[i]], b = ehi[be[i]], ca = m.nw + i, cb = m.nw + nb + i;
            U32 t1 = m.nt + i, t2 = m.nt + nb + i;
            WTc[t1 * 3] = a; WTc[t1 * 3 + 1] = b; WTc[t1 * 3 + 2] = cb;
            WTc[t2 * 3] = a; WTc[t2 * 3 + 1] = cb; WTc[t2 * 3 + 2] = ca;
            candc[t1] = candc[t2] = 1;
        }
        // The segment is from the shipped position; the triangles sit where
        // the move puts them.
        iHipolyArray<V3> Vc0;
        Vc0.resize(Vc.n);
        for (U32 w = 0; w < Vc.n; w++)
        {
            Vc0[w] = sub(Vc[w], mvc[w]);
        }
        iHipolyArray<F64> hits;
        hits.resize(Vc.n);
        firstCrossing(Vc0.p, mvc.p, Vc.n, WTc.p, m.nt + 2 * nb, candc.p, Vc.p, hits.p);
        U32 blocked = 0;
        for (U32 w = 0; w < m.nw; w++)
        {
            if (hits[w] < 1.0)
            {
                mv[w] = scale(mv[w], 0.8 * hits[w]);
                blocked++;
            }
        }
        st->blocked = blocked;
        if (blocked)
        {
            unfold(m, fn.p, mv, V0.p, V);
        }
    }

    // A wall vertex hidden under a cap stays under it once the cap has come
    // down: sink it to just below the cap's new height.
    {
        iHipolyArray<V3> probe, V1;
        probe.resize(m.nw);
        V1.resize(m.nw);
        for (U32 w = 0; w < m.nw; w++)
        {
            probe[w] = wallv[w] ? v3(0, kProbe, 0) : v3(0, 0, 0);
            V1[w] = add(V0[w], mv[w]);
        }
        iHipolyArray<F64> t0, t1;
        t0.resize(m.nw);
        t1.resize(m.nw);
        firstCrossing(V0.p, probe.p, m.nw, m.WT.p, m.nt, cover.p, V0.p, t0.p);
        firstCrossing(V0.p, probe.p, m.nw, m.WT.p, m.nt, cover.p, V1.p, t1.p);
        iHipolyArray<U8> hidden;
        hidden.resizeZero(m.nw);
        U32 sunk = 0;
        for (U32 w = 0; w < m.nw; w++)
        {
            F64 gap0 = kProbe * t0[w];
            F64 gap1 = kProbe * t1[w] - mv[w].y;
            hidden[w] = wallv[w] && t0[w] < 1.0 && gap0 > 0.01;
            if (hidden[w] && t1[w] < 1.0 && gap1 < 0.1)
            {
                mv[w].y += gap1 - 0.1;
                sunk++;
            }
        }
        st->sunk = sunk;
        if (sunk)
        {
            unfold(m, fn.p, mv, V0.p, V);
        }
        // What is left showing: hidden wall vertices now with no cap above,
        // or with the cap below them.
        for (U32 w = 0; w < m.nw; w++)
        {
            V1[w] = add(V0[w], mv[w]);
        }
        firstCrossing(V0.p, probe.p, m.nw, m.WT.p, m.nt, cover.p, V1.p, t1.p);
        for (U32 w = 0; w < m.nw; w++)
        {
            if (!hidden[w])
            {
                continue;
            }
            if (t1[w] >= 1.0)
            {
                st->exposed++;
            }
            else if (kProbe * t1[w] - mv[w].y < 0.0)
            {
                st->poking++;
            }
        }
    }

    // Write back.
    U32 moved = 0;
    F64 maxMove = 0.0;
    for (U32 w = 0; w < m.nw; w++)
    {
        F64 d = len(mv[w]);
        if (d > 1e-6)
        {
            moved++;
        }
        maxMove = dmax(maxMove, dmin(d, lim[w]));
        V[w] = add(V0[w], mv[w]);
    }
    st->moved = moved;
    st->maxMove = maxMove;
    for (U32 k = 0; k < n; k++)
    {
        for (U32 v = 0; v < res[k].nv; v++)
        {
            V3 p = V[m.W[m.offs[k] + v]];
            res[k].pos[v * 3] = (F32)p.x;
            res[k].pos[v * 3 + 1] = (F32)p.y;
            res[k].pos[v * 3 + 2] = (F32)p.z;
        }
    }
}
