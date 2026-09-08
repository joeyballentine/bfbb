// experimental.hipoly_assets: the world and the models smoothed as they load.
//
// The shipped geometry goes through iHipolyTess as it comes off the stream,
// and what RenderWare keeps is the tessellation. Two things make the world
// more than a model:
//
//   - Its collision tree names a triangle by (atomic, position in the
//     atomic's expanded index buffer), so the tree is rebuilt over the new
//     triangles, each carrying the flags of the shipped triangle it was cut
//     from. A triangle the shipped tree never held -- a backdrop, a decal --
//     stays out of the rebuilt tree too.
//   - The level is loaded into the game heap whole, but the geometry built
//     here is not: RwEngineInit was given no allocator, so RenderWare and
//     librw allocate from the C library, and the heap is not asked to hold
//     a world thirty times its size.
//
// The tuning is the pc-mod-hipoly suite's, which was settled by looking at
// every level: it is compiled in, and the setting is only on or off.

#include <rwcore.h>
#include <rpworld.h>

#include "rw.h"

#include "iHipoly.h"
#include "iHipolyTess.h"
#include "iHipolyFillet.h"
#include "iConfig.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace
{
    // --- tuning ------------------------------------------------------------

    // The world: edges are cut to this length, up to this many segments.
    const F64 kWorldTarget = 1.0;
    const S32 kWorldMaxLevel = 6;
    const F64 kWorldCrease = 60.0;       // degrees of turn; sharper folds stay sharp
    const F64 kWorldMaxBulge = 0.3;      // units an edge midpoint may move
    const F64 kWorldRelBulge = 0.3;
    // And never more than the surface turns across the edge, times this: a
    // normal at an end can be trusted to about half the dihedral angle.
    const F64 kTurnBulge = 1.5;
    // Rock, sand, kelp and the like round past folds a building must keep.
    // The wider bulge is for walls and boulders; a floor the player walks on
    // keeps the tight one, or the sand humps.
    const F64 kNaturalCrease = 100.0;
    const F64 kNaturalMaxBulge = 1.0;
    const F64 kNaturalRelBulge = 0.75;
    const F64 kSteepNy = 0.7;
    // A level's world is loaded whole. Past the budget, coarsen the target
    // until it fits; the count goes roughly with the square of the density.
    const U32 kWorldBudget = 600000;

    // Models: a prop is a few units across. Characters -- the sponge and the
    // squid -- are recognised by their skin: modelled bumps and folds that
    // read as crumpling when rounded, so their sharp edges stay and their
    // bumpy surfaces are left alone.
    const F64 kModelTarget = 0.25;
    const S32 kModelMaxLevel = 4;
    const F64 kModelMaxBulge = 0.1;
    const F64 kCharacterHard = 40.0;

    const F64 kMinBulge = 0.01;
    const U32 kMaxVerts = 60000;
    const U32 kMaxTris = 21000;         // a collision record addresses 3 * 21845 vertices

    // Surfaces that are landscape rather than something built.
    const char* const kNatural[] = { "rock", "cliff", "stone", "mountain", "kelp", "grass",
                                     "sand", "ground", "coral", "plant", "tree", "dirt",
                                     "moss", "seaweed", NULL };
    // A slide track, a jump lip, a rocket ship, a planter barrel and the road
    // lines all carry a landscape word in their texture name and are not
    // landscape.
    const char* const kNaturalVeto[] = { "sign", "shadow", "crater", "wood", "building",
                                         "street", "path", "window", "tonguebrd", "slide",
                                         "rocket", "planter", "streel", NULL };

    // Collision record bits. 0x01 continues a leaf's run, 0x02 flips the
    // winding; both are decided here. 0x04 is what makes a triangle collide
    // at all, 0x10 says it is not a floor; those are inherited.
    const U8 kFlagChain = 0x01;
    const U8 kFlagFlip = 0x02;

    const U32 kLeafSize = 6;
    const U32 kMaxDepth = 30;

    // The fillet: folds sharper than this between landscape faces, rounded
    // this far out; a vertex may move this much, a floor or cap vertex this
    // much and only down.
    const F64 kFilletAngle = 35.0;
    const F64 kFilletRadius = 0.0;
    const U32 kFilletIters = 12;
    const F64 kFilletMove = 0.6;
    const F64 kFilletFloorMove = 0.25;

    // --- the settings ------------------------------------------------------

    // Read once. `factor` scales how far everything rounds -- the bulge caps
    // and the fillet -- so a stronger or weaker version is one number. The
    // defaults are the offline suite's: factor 1, creases of 60 and 100
    // degrees, no fillet.
    struct Settings
    {
        S32 enabled;
        F64 factor;
        F64 target;
        S32 maxLevel;
        F64 crease;
        F64 naturalCrease;
        F64 fillet;
        F64 modelTarget;
        U32 budget;
    };
    const F64 kFactor = 1.0;
    Settings sCfg = { -1, kFactor, kWorldTarget, kWorldMaxLevel, kWorldCrease, kNaturalCrease,
                      kFilletRadius, kModelTarget, kWorldBudget };

    const Settings& settings()
    {
        if (sCfg.enabled < 0)
        {
            sCfg.enabled = iConfigGetBool("experimental.hipoly_assets", FALSE) ? 1 : 0;
            sCfg.factor = iConfigGetFloat("experimental.hipoly_factor", (F32)kFactor);
            sCfg.target = iConfigGetFloat("experimental.hipoly_target", (F32)kWorldTarget);
            sCfg.maxLevel = iConfigGetInt("experimental.hipoly_max_level", kWorldMaxLevel);
            sCfg.crease = iConfigGetFloat("experimental.hipoly_crease", (F32)kWorldCrease);
            sCfg.naturalCrease = iConfigGetFloat("experimental.hipoly_natural_crease", (F32)kNaturalCrease);
            sCfg.fillet = iConfigGetFloat("experimental.hipoly_fillet", (F32)kFilletRadius);
            sCfg.modelTarget = iConfigGetFloat("experimental.hipoly_model_target", (F32)kModelTarget);
            sCfg.budget = (U32)iConfigGetInt("experimental.hipoly_budget", (S32)kWorldBudget);
            if (sCfg.maxLevel < 1) sCfg.maxLevel = 1;
            if (sCfg.maxLevel > 15) sCfg.maxLevel = 15;
            if (sCfg.target < 0.05) sCfg.target = 0.05;
            if (sCfg.modelTarget < 0.02) sCfg.modelTarget = 0.02;
            if (sCfg.factor < 0.0) sCfg.factor = 0.0;
        }
        return sCfg;
    }

    bool containsWord(const char* name, const char* const* words)
    {
        char low[64];
        U32 i;
        for (i = 0; i < sizeof(low) - 1 && name[i]; i++)
        {
            char c = name[i];
            low[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        }
        low[i] = 0;
        for (const char* const* w = words; *w; w++)
        {
            if (strstr(low, *w))
            {
                return true;
            }
        }
        return false;
    }

    bool isNatural(const rw::Material* m)
    {
        const char* name = m && m->texture ? m->texture->name : "";
        return !containsWord(name, kNaturalVeto) && containsWord(name, kNatural);
    }

    // --- a geometry as the tessellator sees it -----------------------------

    struct View
    {
        iHipolyGeom g;
        iHipolyArray<U32> tris;
        iHipolyArray<F32> pos;
        iHipolyArray<F32> normal;
        iHipolyArray<U8> color;
        iHipolyArray<F32> uv[8];
        iHipolyArray<U8> skinIndex;
        iHipolyArray<F32> skinWeight;
    };

    void view(const rw::Geometry* geo, View& v)
    {
        memset(&v.g, 0, sizeof(v.g));
        U32 nv = (U32)geo->numVertices, nt = (U32)geo->numTriangles;
        v.g.nv = nv;
        v.g.nt = nt;
        v.pos.resize(nv * 3);
        memcpy(v.pos.p, geo->morphTargets[0].vertices, nv * 12);
        v.g.pos = v.pos.p;
        if (geo->morphTargets[0].normals && (geo->flags & rw::Geometry::NORMALS))
        {
            v.normal.resize(nv * 3);
            memcpy(v.normal.p, geo->morphTargets[0].normals, nv * 12);
            v.g.normal = v.normal.p;
        }
        if (geo->colors && (geo->flags & rw::Geometry::PRELIT))
        {
            v.color.resize(nv * 4);
            memcpy(v.color.p, geo->colors, nv * 4);
            v.g.color = v.color.p;
        }
        v.g.numUV = (U32)geo->numTexCoordSets;
        for (U32 s = 0; s < v.g.numUV; s++)
        {
            v.uv[s].resize(nv * 2);
            memcpy(v.uv[s].p, geo->texCoords[s], nv * 8);
            v.g.uv[s] = v.uv[s].p;
        }
        rw::Skin* skin = rw::Skin::get(geo);
        if (skin && skin->indices && skin->weights)
        {
            v.skinIndex.resize(nv * 4);
            v.skinWeight.resize(nv * 4);
            memcpy(v.skinIndex.p, skin->indices, nv * 4);
            memcpy(v.skinWeight.p, skin->weights, nv * 16);
            v.g.skinIndex = v.skinIndex.p;
            v.g.skinWeight = v.skinWeight.p;
        }
        v.tris.resize(nt * 4);
        for (U32 t = 0; t < nt; t++)
        {
            const rw::Triangle& tri = geo->triangles[t];
            v.tris[t * 4] = tri.v[0];
            v.tris[t * 4 + 1] = tri.v[1];
            v.tris[t * 4 + 2] = tri.v[2];
            v.tris[t * 4 + 3] = tri.matId;
        }
        v.g.tris = v.tris.p;
    }

    // --- a result as a RenderWare geometry ---------------------------------

    rw::Geometry* makeGeometry(const rw::Geometry* old, const iHipolyResult& r)
    {
        // The flags carry the texture coordinate set count in bits 16-23 and
        // the attribute kinds in the low byte. The result is always a
        // triangle list, and never the platform's own layout.
        U32 flags = old->flags & ~(U32)(rw::Geometry::TRISTRIP | rw::Geometry::NATIVE |
                                        rw::Geometry::NATIVEINSTANCE | rw::Geometry::NORMALS);
        if (r.normal.n)
        {
            flags |= rw::Geometry::NORMALS;
        }
        if (r.nv == 0 || r.nt == 0)
        {
            // Some atomics ship with no triangles at all; they stay as they are.
            return NULL;
        }
        rw::Geometry* geo = rw::Geometry::create((rw::int32)r.nv, (rw::int32)r.nt, flags);
        if (geo == NULL)
        {
            return NULL;
        }
        memcpy(geo->morphTargets[0].vertices, r.pos.p, r.nv * 12);
        if (r.normal.n && geo->morphTargets[0].normals)
        {
            memcpy(geo->morphTargets[0].normals, r.normal.p, r.nv * 12);
        }
        if (r.color.n && geo->colors)
        {
            memcpy(geo->colors, r.color.p, r.nv * 4);
        }
        for (U32 s = 0; s < r.numUV && s < (U32)geo->numTexCoordSets; s++)
        {
            memcpy(geo->texCoords[s], r.uv[s].p, r.nv * 8);
        }
        for (U32 t = 0; t < r.nt; t++)
        {
            rw::Triangle& tri = geo->triangles[t];
            tri.v[0] = (rw::uint16)r.tris[t * 4];
            tri.v[1] = (rw::uint16)r.tris[t * 4 + 1];
            tri.v[2] = (rw::uint16)r.tris[t * 4 + 2];
            tri.matId = (rw::uint16)r.tris[t * 4 + 3];
        }
        for (rw::int32 m = 0; m < old->matList.numMaterials; m++)
        {
            geo->matList.appendMaterial(old->matList.materials[m]);
        }
        rw::Skin* oldSkin = rw::Skin::get(old);
        if (oldSkin && r.skinIndex.n)
        {
            // What copySkin would do, over the new vertex count. The used-bone
            // list is sized for every bone and then measured: an Xbox skin
            // arrives with no list at all, and findUsedBones writes into it.
            rw::Skin* skin = rwNewT(rw::Skin, 1, rw::MEMDUR_EVENT | rw::ID_SKIN);
            memset(skin, 0, sizeof(*skin));
            skin->init(oldSkin->numBones, oldSkin->numBones, (rw::int32)r.nv);
            if (oldSkin->numBones)
            {
                memcpy(skin->inverseMatrices, oldSkin->inverseMatrices, oldSkin->numBones * 64);
            }
            memcpy(skin->indices, r.skinIndex.p, r.nv * 4);
            memcpy(skin->weights, r.skinWeight.p, r.nv * 16);
            skin->findNumWeights((rw::int32)r.nv);
            skin->findUsedBones((rw::int32)r.nv);
            rw::Skin::set(geo, skin);
        }
        geo->calculateBoundingSphere();
        geo->buildMeshes();
        return geo;
    }

    // What every replaced atomic showed and shows, for the F8 swap. One
    // reference of ours on each geometry, so the one not on the atomic
    // survives.
    //
    // The atomic holds the smoothed geometry at all times. Game code keeps
    // per-vertex arrays sized to whatever geometry an atomic had when it
    // looked -- a hazard's UV animation, the goo, the ripple -- and writes
    // them back through the atomic later, so a geometry with another vertex
    // count under it overruns the heap. The shipped geometry is put in by the
    // atomic's render callback for the draw alone.
    struct Swap
    {
        rw::Atomic* atomic;
        rw::Geometry* shipped;
        rw::Geometry* smooth;
        rw::Atomic::RenderCB render;   // what the atomic drew with before
    };
    iHipolyArray<Swap> sSwaps;
    bool sShowSmooth = true;
    bool sHotkeyWasDown = false;

    Swap* findSwap(rw::Atomic* atomic)
    {
        for (U32 i = 0; i < sSwaps.n; i++)
        {
            if (sSwaps[i].atomic == atomic)
            {
                return &sSwaps[i];
            }
        }
        return NULL;
    }

    void swapRenderCB(rw::Atomic* atomic)
    {
        Swap* sw = findSwap(atomic);
        if (sw == NULL)
        {
            rw::Atomic::defaultRenderCB(atomic);
            return;
        }
        // Only while the atomic still holds our geometry: the goo gives an
        // atomic one of its own.
        rw::Geometry* held = atomic->geometry;
        bool sub = !sShowSmooth && held == sw->smooth;
        if (sub)
        {
            atomic->geometry = sw->shipped;
        }
        sw->render(atomic);
        if (sub)
        {
            atomic->geometry = held;
        }
    }

    void replaceGeometry(rw::Atomic* atomic, rw::Geometry* geo, U32 flags)
    {
        Swap sw;
        sw.atomic = atomic;
        sw.shipped = atomic->geometry;
        sw.smooth = geo;
        sw.render = atomic->renderCB;
        sw.shipped->addRef();
        // setGeometry takes its own reference; the one create() gave us is
        // the one the swap keeps.
        atomic->setGeometry(geo, flags);
        atomic->renderCB = swapRenderCB;
        sSwaps.push(sw);
    }

    // --- the collision tree ------------------------------------------------

    struct CollTri
    {
        F32 v[3][3];
        U16 atom;
        U16 vert;
        U8 flags;
        U8 plat;
        U16 mat;
    };

    struct CollBuild
    {
        const CollTri* tris;
        iHipolyArray<F64> lo;      // n * 3
        iHipolyArray<F64> hi;
        iHipolyArray<U64> key[3];  // n per axis: the centre, as a sortable integer
        iHipolyArray<U32> order;   // leaf triangles in emission order
        iHipolyArray<U32> nodes;   // 4 words each: leftInfo, rightInfo, leftValue, rightValue
    };

    U64 sortableDouble(F64 d)
    {
        U64 bits;
        memcpy(&bits, &d, 8);
        return (bits & 0x8000000000000000ULL) ? ~bits : (bits | 0x8000000000000000ULL);
    }

    // Emit a subtree over idx[0..n). Returns type 1 (a leaf, index the run
    // start) or 2 (a branch, index its slot). A branch is the median split
    // along the longest axis; the split values overlap, so a triangle
    // straddling the pair is reachable from whichever side holds it.
    U32 collRec(CollBuild& b, U32* idx, U32 n, U32 depth, U32* index)
    {
        if (n <= kLeafSize || depth >= kMaxDepth)
        {
            *index = b.order.n;
            for (U32 i = 0; i < n; i++)
            {
                b.order.push(idx[i]);
            }
            return 1;
        }
        F64 blo[3] = { 1e300, 1e300, 1e300 }, bhi[3] = { -1e300, -1e300, -1e300 };
        for (U32 i = 0; i < n; i++)
        {
            for (U32 a = 0; a < 3; a++)
            {
                F64 l = b.lo[idx[i] * 3 + a], h = b.hi[idx[i] * 3 + a];
                if (l < blo[a]) blo[a] = l;
                if (h > bhi[a]) bhi[a] = h;
            }
        }
        U32 axis = 0;
        for (U32 a = 1; a < 3; a++)
        {
            if (bhi[a] - blo[a] > bhi[axis] - blo[axis])
            {
                axis = a;
            }
        }
        if (bhi[axis] - blo[axis] < 1e-4)
        {
            *index = b.order.n;
            for (U32 i = 0; i < n; i++)
            {
                b.order.push(idx[i]);
            }
            return 1;
        }
        iHipolySortU64(idx, n, b.key[axis].p);
        U32 half = n / 2;
        F64 leftMax = -1e300, rightMin = 1e300;
        for (U32 i = 0; i < half; i++)
        {
            F64 h = b.hi[idx[i] * 3 + axis];
            if (h > leftMax) leftMax = h;
        }
        for (U32 i = half; i < n; i++)
        {
            F64 l = b.lo[idx[i] * 3 + axis];
            if (l < rightMin) rightMin = l;
        }
        U32 slot = b.nodes.n / 4;
        b.nodes.push(0); b.nodes.push(0); b.nodes.push(0); b.nodes.push(0);
        U32 li, ri;
        U32 lt = collRec(b, idx, half, depth + 1, &li);
        U32 rt = collRec(b, idx + half, n - half, depth + 1, &ri);
        F32 lv = (F32)leftMax, rv = (F32)rightMin;
        U32 lvb, rvb;
        memcpy(&lvb, &lv, 4);
        memcpy(&rvb, &rv, 4);
        b.nodes[slot * 4] = lt | (axis * 4) | (li << 12);
        b.nodes[slot * 4 + 1] = rt | (axis * 4) | (ri << 12);
        b.nodes[slot * 4 + 2] = lvb;
        b.nodes[slot * 4 + 3] = rvb;
        *index = slot;
        return 2;
    }

    // The 0xBEEF01 payload: "CCOL", the branch nodes, then the triangle
    // records in leaf order.
    void* collisionTree(const CollTri* tris, U32 n, U32* outSize)
    {
        CollBuild b;
        b.tris = tris;
        b.lo.resize(n * 3);
        b.hi.resize(n * 3);
        for (U32 a = 0; a < 3; a++)
        {
            b.key[a].resize(n);
        }
        for (U32 i = 0; i < n; i++)
        {
            for (U32 a = 0; a < 3; a++)
            {
                F64 x0 = tris[i].v[0][a], x1 = tris[i].v[1][a], x2 = tris[i].v[2][a];
                F64 l = x0 < x1 ? x0 : x1;
                l = l < x2 ? l : x2;
                F64 h = x0 > x1 ? x0 : x1;
                h = h > x2 ? h : x2;
                b.lo[i * 3 + a] = l;
                b.hi[i * 3 + a] = h;
                b.key[a][i] = sortableDouble((x0 + x1 + x2) / 3.0);
            }
        }
        iHipolyArray<U32> idx;
        idx.resize(n);
        for (U32 i = 0; i < n; i++)
        {
            idx[i] = i;
        }
        if (n)
        {
            U32 root;
            collRec(b, idx.p, n, 0, &root);
        }
        // Leaves are runs: every triangle but the last of each run continues
        // it. A run starts wherever a leaf points.
        iHipolyArray<U8> starts;
        starts.resizeZero(b.order.n + 1);
        U32 numNodes = b.nodes.n / 4;
        for (U32 k = 0; k < numNodes; k++)
        {
            for (U32 side = 0; side < 2; side++)
            {
                U32 info = b.nodes[k * 4 + side];
                if ((info & 3) == 1)
                {
                    starts[info >> 12] = 1;
                }
            }
        }
        if (numNodes == 0)
        {
            starts[0] = 1;
        }
        U32 size = 12 + numNodes * 16 + b.order.n * 8;
        U8* out = (U8*)RwMalloc(size);
        if (out == NULL)
        {
            return NULL;
        }
        memcpy(out, "CCOL", 4);
        memcpy(out + 4, &numNodes, 4);
        U32 numTris = b.order.n;
        memcpy(out + 8, &numTris, 4);
        if (numNodes)
        {
            memcpy(out + 12, b.nodes.p, numNodes * 16);
        }
        U8* rec = out + 12 + numNodes * 16;
        for (U32 i = 0; i < b.order.n; i++)
        {
            const CollTri& t = tris[b.order[i]];
            bool last = i + 1 == b.order.n || starts[i + 1];
            U8 flags = (U8)((t.flags & ~kFlagChain) | (last ? 0 : kFlagChain));
            memcpy(rec, &t.atom, 2);
            memcpy(rec + 2, &t.vert, 2);
            rec[4] = flags;
            rec[5] = t.plat;
            memcpy(rec + 6, &t.mat, 2);
            rec += 8;
        }
        *outSize = size;
        return out;
    }

    // Walk a finished tree the way xClumpColl does and count what is wrong:
    // an index past the end, a run that leaves the array, a record naming
    // a vertex its atomic does not have, a triangle no leaf reaches.
    U32 checkTree(const U8* buf, U32 size, const U32* expandedCount, U32 numAtoms)
    {
        U32 numNodes, numTris;
        memcpy(&numNodes, buf + 4, 4);
        memcpy(&numTris, buf + 8, 4);
        const U8* nodes = buf + 12;
        const U8* recs = nodes + numNodes * 16;
        U32 bad = 0;
        iHipolyArray<U8> seen;
        seen.resizeZero(numTris);
        // (type, index) pairs
        iHipolyArray<U32> stack;
        stack.push(numNodes ? 2 : 1);
        stack.push(0);
        U32 depth = 0, maxDepth = 0;
        while (stack.n)
        {
            U32 index = stack[stack.n - 1];
            U32 type = stack[stack.n - 2];
            stack.n -= 2;
            if (type == 1)
            {
                U32 i = index;
                if (i >= numTris)
                {
                    bad++;
                    continue;
                }
                for (;;)
                {
                    seen[i] = 1;
                    U8 flags = recs[i * 8 + 4];
                    if (!(flags & kFlagChain))
                    {
                        break;
                    }
                    i++;
                    if (i >= numTris)
                    {
                        bad++;
                        break;
                    }
                }
            }
            else
            {
                if (index >= numNodes)
                {
                    bad++;
                    continue;
                }
                U32 l, r;
                memcpy(&l, nodes + index * 16, 4);
                memcpy(&r, nodes + index * 16 + 4, 4);
                stack.push(l & 3); stack.push(l >> 12);
                stack.push(r & 3); stack.push(r >> 12);
                depth = stack.n / 2;
                if (depth > maxDepth) maxDepth = depth;
            }
        }
        U32 unreachable = 0, badVert = 0;
        for (U32 i = 0; i < numTris; i++)
        {
            if (!seen[i]) unreachable++;
            U16 atom, vert;
            memcpy(&atom, recs + i * 8, 2);
            memcpy(&vert, recs + i * 8 + 2, 2);
            if (atom >= numAtoms || (U32)vert + 2 >= expandedCount[numAtoms - 1 - atom])
            {
                badVert++;
            }
        }
        if (bad || unreachable || badVert)
        {
            U32 l0 = 0, r0 = 0;
            if (numNodes)
            {
                memcpy(&l0, nodes, 4);
                memcpy(&r0, nodes + 4, 4);
            }
            printf("bfbb: hipoly collision tree is WRONG: %u bad links, %u unreachable, %u bad vertex refs "
                   "(stack %u); %u nodes, %u tris, root %08x %08x\n",
                   bad, unreachable, badVert, maxDepth, numNodes, numTris, l0, r0);
        }
        return bad + unreachable + badVert;
    }

    // --- the shipped tree's flags, per struct triangle ---------------------

    struct ParentFlags
    {
        // Per triangle of one shipped geometry: flags, platData, matIndex;
        // flags -1 where the tree never held it.
        iHipolyArray<S32> flags;
        iHipolyArray<U8> plat;
        iHipolyArray<U16> mat;
    };

    // The expanded index buffer xJSP builds for an atomic: every mesh's
    // indices, one after the other.
    void expandedIndices(const rw::Geometry* geo, iHipolyArray<U32>& out)
    {
        out.clear();
        rw::MeshHeader* mh = geo->meshHeader;
        if (mh == NULL)
        {
            return;
        }
        rw::Mesh* mesh = mh->getMeshes();
        for (U32 m = 0; m < mh->numMeshes; m++)
        {
            for (U32 i = 0; i < mesh[m].numIndices; i++)
            {
                out.push(mesh[m].indices[i]);
            }
        }
    }

    inline rw::V3d cross3(rw::V3d a, rw::V3d b)
    {
        return rw::makeV3d(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
    }

    inline void sort3(U32& a, U32& b, U32& c)
    {
        U32 t;
        if (a > b) { t = a; a = b; b = t; }
        if (b > c) { t = b; b = c; c = t; }
        if (a > b) { t = a; a = b; b = t; }
    }

    // Which (atomic, struct triangle) carried which collision flags. The
    // flags come back with the run bit cleared and the flip bit MEANING what
    // it should for the rebuilt triangles: set when the shipped collision
    // normal (the strip's winding, flipped where the tree says so) opposes
    // the struct triangle's winding, which is the winding the children
    // inherit. Some levels ship walls and floors drawn one way and collided
    // the other; dropping the bit turned those inside out.
    void parentFlags(rw::Atomic** atoms, U32 numAtoms, const U8* coll, U32 collSize,
                     ParentFlags* out)
    {
        if (collSize < 12 || memcmp(coll, "CCOL", 4) != 0)
        {
            return;
        }
        U32 numNodes, numTris;
        memcpy(&numNodes, coll + 4, 4);
        memcpy(&numTris, coll + 8, 4);
        const U8* rec = coll + 12 + numNodes * 16;
        if (12 + numNodes * 16 + numTris * 8 > collSize)
        {
            return;
        }
        // Per atomic: the expanded buffer and a map from a vertex triple to
        // the struct triangle over it.
        iHipolyArray<U32>* expanded = new iHipolyArray<U32>[numAtoms];
        struct Map3
        {
            iHipolyArray<U64> key;   // (a << 42 | b << 21 | c) sorted, then value
            iHipolyArray<U32> value;
            iHipolyArray<U32> order;
        };
        Map3* maps = new Map3[numAtoms];
        for (U32 k = 0; k < numAtoms; k++)
        {
            const rw::Geometry* geo = atoms[k]->geometry;
            expandedIndices(geo, expanded[k]);
            U32 nt = (U32)geo->numTriangles;
            out[k].flags.resize(nt);
            out[k].plat.resizeZero(nt);
            out[k].mat.resizeZero(nt);
            maps[k].key.resize(nt);
            maps[k].order.resize(nt);
            for (U32 t = 0; t < nt; t++)
            {
                out[k].flags[t] = -1;
                U32 a = geo->triangles[t].v[0], b = geo->triangles[t].v[1], c = geo->triangles[t].v[2];
                sort3(a, b, c);
                maps[k].key[t] = ((U64)a << 42) | ((U64)b << 21) | (U64)c;
                maps[k].order[t] = t;
            }
            iHipolySortU64(maps[k].order.p, nt, maps[k].key.p);
        }
        for (U32 i = 0; i < numTris; i++, rec += 8)
        {
            U16 atom, vert, mat;
            memcpy(&atom, rec, 2);
            memcpy(&vert, rec + 2, 2);
            U8 flags = rec[4], plat = rec[5];
            memcpy(&mat, rec + 6, 2);
            if (atom >= numAtoms)
            {
                continue;
            }
            U32 k = numAtoms - 1 - atom;
            const iHipolyArray<U32>& ex = expanded[k];
            if ((U32)vert + 2 >= ex.n)
            {
                continue;
            }
            U32 a = ex[vert], b = ex[vert + 1], c = ex[vert + 2];
            if (flags & kFlagFlip)
            {
                U32 t = b; b = c; c = t;
            }
            U32 sa = a, sb = b, sc = c;
            sort3(sa, sb, sc);
            if (sa == sb || sb == sc)
            {
                continue;
            }
            U64 key = ((U64)sa << 42) | ((U64)sb << 21) | (U64)sc;
            // binary search the sorted keys
            const Map3& m = maps[k];
            U32 lo = 0, hi = m.order.n;
            while (lo < hi)
            {
                U32 mid = (lo + hi) / 2;
                if (m.key[m.order[mid]] < key)
                {
                    lo = mid + 1;
                }
                else
                {
                    hi = mid;
                }
            }
            if (lo >= m.order.n || m.key[m.order[lo]] != key)
            {
                continue;
            }
            const rw::Geometry* geo = atoms[k]->geometry;
            const rw::V3d* P = geo->morphTargets[0].vertices;
            U32 flip = 0;
            for (; lo < m.order.n && m.key[m.order[lo]] == key; lo++)
            {
                U32 t = m.order[lo];
                const rw::Triangle& st = geo->triangles[t];
                rw::V3d nColl = cross3(rw::sub(P[b], P[a]), rw::sub(P[c], P[a]));
                rw::V3d nStruct = cross3(rw::sub(P[st.v[1]], P[st.v[0]]), rw::sub(P[st.v[2]], P[st.v[0]]));
                flip = rw::dot(nColl, nStruct) < 0.0f ? kFlagFlip : 0;
                out[k].flags[t] = (S32)((flags & ~3u) | flip);
                out[k].plat[t] = plat;
                out[k].mat[t] = mat;
            }
        }
        delete[] maps;
        delete[] expanded;
    }

    // --- atomics in xJSP's order ------------------------------------------

    U32 listAtomics(rw::Clump* clump, iHipolyArray<rw::Atomic*>& out)
    {
        out.clear();
        FORLIST(link, clump->atomics)
        {
            out.push(rw::Atomic::fromClump(link));
        }
        return out.n;
    }

    // Whether every atomic is something the tessellator can take.
    bool portable(rw::Atomic** atoms, U32 n)
    {
        for (U32 k = 0; k < n; k++)
        {
            const rw::Geometry* geo = atoms[k]->geometry;
            if (geo == NULL || (geo->flags & rw::Geometry::NATIVE) || geo->numVertices > 65535)
            {
                return false;
            }
        }
        return true;
    }
}

// ---------------------------------------------------------------------------

S32 iHipolyEnabled()
{
    return settings().enabled;
}

void* iHipolyWorld(RpClump* rpclump, const void* coll, U32 collSize, U32* outSize)
{
    *outSize = 0;
    if (!iHipolyEnabled() || rpclump == NULL)
    {
        return NULL;
    }
    const Settings& cfg = settings();
    rw::Clump* clump = reinterpret_cast<rw::Clump*>(rpclump);
    clock_t t0 = clock();
    iHipolyArray<rw::Atomic*> atoms;
    U32 n = listAtomics(clump, atoms);
    if (n == 0 || !portable(atoms.p, n))
    {
        return NULL;
    }

    View* views = new View[n];
    iHipolyGeom* geoms = new iHipolyGeom[n];
    U32 ntTot = 0, before = 0;
    for (U32 k = 0; k < n; k++)
    {
        view(atoms[k]->geometry, views[k]);
        geoms[k] = views[k].g;
        ntTot += geoms[k].nt;
    }
    before = ntTot;

    // Per-face crease angle and bulge caps: landscape textures round past
    // the folds a building keeps, on their steep faces.
    iHipolyArray<F64> crease, bulge, rel;
    crease.resize(ntTot);
    bulge.resize(ntTot);
    rel.resize(ntTot);
    U32 naturalFaces = 0;
    {
        U32 f = 0;
        for (U32 k = 0; k < n; k++)
        {
            const rw::Geometry* geo = atoms[k]->geometry;
            const iHipolyGeom& g = geoms[k];
            for (U32 t = 0; t < g.nt; t++, f++)
            {
                U32 m = g.tris[t * 4 + 3];
                bool nat = m < (U32)geo->matList.numMaterials && isNatural(geo->matList.materials[m]);
                const F32* p0 = g.pos + g.tris[t * 4] * 3;
                const F32* p1 = g.pos + g.tris[t * 4 + 1] * 3;
                const F32* p2 = g.pos + g.tris[t * 4 + 2] * 3;
                F64 e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
                F64 e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
                F64 nx = e1[1] * e2[2] - e1[2] * e2[1];
                F64 ny = e1[2] * e2[0] - e1[0] * e2[2];
                F64 nz = e1[0] * e2[1] - e1[1] * e2[0];
                F64 l = sqrt(nx * nx + ny * ny + nz * nz);
                F64 uy = l > 1e-12 ? ny / l : 0.0;
                bool steep = (uy < 0 ? -uy : uy) < kSteepNy;
                crease[f] = nat ? cfg.naturalCrease : cfg.crease;
                bulge[f] = ((nat && steep) ? kNaturalMaxBulge : kWorldMaxBulge) * cfg.factor;
                rel[f] = ((nat && steep) ? kNaturalRelBulge : kWorldRelBulge) * cfg.factor;
                if (nat)
                {
                    naturalFaces++;
                }
            }
        }
    }

    iHipolyParams pr;
    memset(&pr, 0, sizeof(pr));
    pr.target = cfg.target;
    pr.maxLevel = cfg.maxLevel;
    pr.turnBulge = kTurnBulge * cfg.factor;
    pr.minBulge = kMinBulge;
    pr.creaseDegPerFace = crease.p;
    pr.maxBulgePerFace = bulge.p;
    pr.relBulgePerFace = rel.p;
    pr.hardDeg = -1.0;
    pr.noiseGuard = false;
    pr.pinOpenEdges = true;
    pr.maxVerts = kMaxVerts;
    pr.maxTris = kMaxTris;

    iHipolyResult* res = new iHipolyResult[n];
    iHipolyStats stats;
    U32 total = 0;
    for (;;)
    {
        iHipolyRefine(geoms, n, pr, res, &stats);
        total = 0;
        for (U32 k = 0; k < n; k++)
        {
            total += res[k].nt;
        }
        if (total <= cfg.budget || pr.target >= 8.0)
        {
            break;
        }
        F64 grow = sqrt((F64)total / cfg.budget);
        grow = grow < 1.15 ? 1.15 : (grow > 2.0 ? 2.0 : grow);
        pr.target *= grow;
    }

    // The shipped tree's flags, before the geometries go.
    ParentFlags* pf = new ParentFlags[n];
    parentFlags(atoms.p, n, (const U8*)coll, collSize, pf);

    // The fillet, over the tessellation: which triangles are landscape and
    // which the shipped tree lets the player stand on come from the parents.
    iHipolyFilletStats fs;
    memset(&fs, 0, sizeof(fs));
    if (cfg.fillet > 0.0 && cfg.factor > 0.0)
    {
        iHipolyArray<U8>* natural = new iHipolyArray<U8>[n];
        iHipolyArray<U8>* landable = new iHipolyArray<U8>[n];
        const U8** natp = new const U8*[n];
        const U8** landp = new const U8*[n];
        for (U32 k = 0; k < n; k++)
        {
            const rw::Geometry* geo = atoms[k]->geometry;
            natural[k].resize(res[k].nt);
            landable[k].resize(res[k].nt);
            for (U32 t = 0; t < res[k].nt; t++)
            {
                U32 m = res[k].tris[t * 4 + 3];
                natural[k][t] = m < (U32)geo->matList.numMaterials && isNatural(geo->matList.materials[m]);
                U32 parent = res[k].parent[t];
                S32 flags = parent < pf[k].flags.n ? pf[k].flags[parent] : -1;
                landable[k][t] = flags >= 0 && !(flags & 0x10);
            }
            natp[k] = natural[k].p;
            landp[k] = landable[k].p;
        }
        iHipolyFilletParams fp;
        fp.angleDeg = kFilletAngle;
        fp.radius = cfg.fillet * cfg.factor;
        fp.iters = kFilletIters;
        fp.maxMove = kFilletMove * cfg.factor;
        fp.floorMove = kFilletFloorMove * cfg.factor;
        iHipolyFillet(res, n, natp, landp, fp, &fs);
        delete[] landp;
        delete[] natp;
        delete[] landable;
        delete[] natural;
    }

    // The new geometries, and the collision triangles over them, addressed
    // the way xJSP's expanded index buffer will number them: material by
    // material, three consecutive entries per triangle.
    iHipolyArray<CollTri> ctris;
    for (U32 k = 0; k < n; k++)
    {
        rw::Geometry* geo = makeGeometry(atoms[k]->geometry, res[k]);
        if (geo == NULL)
        {
            continue;
        }
        U32 rank = 0;
        for (rw::int32 m = 0; m < geo->matList.numMaterials; m++)
        {
            for (U32 t = 0; t < res[k].nt; t++)
            {
                if (res[k].tris[t * 4 + 3] != (U32)m)
                {
                    continue;
                }
                U32 parent = res[k].parent[t];
                S32 flags = parent < pf[k].flags.n ? pf[k].flags[parent] : -1;
                if (flags >= 0 && rank * 3 + 2 <= 65535)
                {
                    CollTri c;
                    for (U32 cc = 0; cc < 3; cc++)
                    {
                        const F32* p = res[k].pos.p + res[k].tris[t * 4 + cc] * 3;
                        c.v[cc][0] = p[0];
                        c.v[cc][1] = p[1];
                        c.v[cc][2] = p[2];
                    }
                    c.atom = (U16)(n - 1 - k);
                    c.vert = (U16)(rank * 3);
                    c.flags = (U8)flags;
                    c.plat = pf[k].plat[parent];
                    c.mat = pf[k].mat[parent];
                    ctris.push(c);
                }
                rank++;
            }
        }
        replaceGeometry(atoms[k], geo, 0);
    }
    void* tree = collisionTree(ctris.p, ctris.n, outSize);
    {
        iHipolyArray<U32> expandedCount;
        expandedCount.resize(n);
        for (U32 k = 0; k < n; k++)
        {
            const rw::MeshHeader* mh = atoms[k]->geometry->meshHeader;
            expandedCount[k] = mh ? mh->totalIndices : 0;
        }
        checkTree((const U8*)tree, *outSize, expandedCount.p, n);
    }

    printf("bfbb: hipoly world: %u -> %u triangles in %u atomics (%u natural faces, target %.2f), "
           "%u collision triangles, %u open edges, %u T-junctions, %u folds; %.1fs\n",
           before, total, n, naturalFaces, pr.target, ctris.n, stats.openEdges, stats.tJunctions,
           stats.folds, (double)(clock() - t0) / CLOCKS_PER_SEC);
    if (fs.seeds)
    {
        printf("bfbb: hipoly fillet: %u crease vertices, %u in the band, %u moved up to %.2f "
               "(%u held short of another sheet, %u sunk under a cap; %u left exposed, %u poking)\n",
               fs.seeds, fs.band, fs.moved, fs.maxMove, fs.blocked, fs.sunk, fs.exposed, fs.poking);
    }
    fflush(stdout);

    delete[] pf;
    delete[] res;
    delete[] geoms;
    delete[] views;
    return tree;
}

namespace
{
    struct Attached
    {
        const void* tree;
        void* buffer;
    };
    Attached sAttached[16];
}

void iHipolyWorldAttach(const void* colltree, void* buffer)
{
    for (U32 i = 0; i < 16; i++)
    {
        if (sAttached[i].tree == NULL)
        {
            sAttached[i].tree = colltree;
            sAttached[i].buffer = buffer;
            return;
        }
    }
    // Nowhere to remember it; the buffer stays until exit.
}

void iHipolyWorldDetach(const void* colltree)
{
    for (U32 i = 0; i < 16; i++)
    {
        if (sAttached[i].tree == colltree)
        {
            RwFree(sAttached[i].buffer);
            sAttached[i].tree = NULL;
            sAttached[i].buffer = NULL;
            return;
        }
    }
}

void iHipolyModel(RpClump* rpclump)
{
    if (!iHipolyEnabled() || rpclump == NULL)
    {
        return;
    }
    rw::Clump* clump = reinterpret_cast<rw::Clump*>(rpclump);
    iHipolyArray<rw::Atomic*> atoms;
    U32 n = listAtomics(clump, atoms);
    if (n == 0 || !portable(atoms.p, n))
    {
        return;
    }
    // Collision proxies, locators, invisible walls and the HUD's sprites:
    // untextured, or too few vertices to be a shape.
    bool skinned = false;
    for (U32 k = 0; k < n; k++)
    {
        const rw::Geometry* geo = atoms[k]->geometry;
        if (!(geo->flags & rw::Geometry::TEXTURED) || geo->numVertices < 6)
        {
            return;
        }
        if (rw::Skin::get(geo) != NULL)
        {
            skinned = true;
        }
    }
    // One domain per clump: a character's atomics share their bind pose, and
    // welding across them keeps the seams between parts closed.
    View* views = new View[n];
    iHipolyGeom* geoms = new iHipolyGeom[n];
    for (U32 k = 0; k < n; k++)
    {
        view(atoms[k]->geometry, views[k]);
        geoms[k] = views[k].g;
    }
    iHipolyParams pr;
    memset(&pr, 0, sizeof(pr));
    const Settings& cfg = settings();
    pr.target = cfg.modelTarget;
    pr.maxLevel = kModelMaxLevel;
    pr.minBulge = kMinBulge;
    pr.creaseDeg = cfg.crease;
    pr.maxBulge = kModelMaxBulge * cfg.factor;
    pr.relBulge = kWorldRelBulge * cfg.factor;
    pr.turnBulge = kTurnBulge * cfg.factor;
    pr.hardDeg = skinned ? kCharacterHard : -1.0;
    pr.noiseGuard = skinned;
    pr.pinOpenEdges = false;
    pr.maxVerts = kMaxVerts;
    pr.maxTris = kMaxTris;
    iHipolyResult* res = new iHipolyResult[n];
    iHipolyStats stats;
    iHipolyRefine(geoms, n, pr, res, &stats);
    bool changed = false;
    for (U32 k = 0; k < n; k++)
    {
        if (res[k].nt != geoms[k].nt)
        {
            changed = true;
        }
    }
    if (changed)
    {
        for (U32 k = 0; k < n; k++)
        {
            rw::Geometry* geo = makeGeometry(atoms[k]->geometry, res[k]);
            if (geo)
            {
                // iModelStreamRead has already given every atomic of the model
                // one bounding sphere, with room to spare; keep it.
                replaceGeometry(atoms[k], geo, rw::Atomic::SAMEBOUNDINGSPHERE);
            }
        }
    }
    delete[] res;
    delete[] geoms;
    delete[] views;
}

void iHipolyHotkey(S32 down)
{
    bool pressed = down && !sHotkeyWasDown;
    sHotkeyWasDown = down != 0;
    if (!pressed || sSwaps.n == 0)
    {
        return;
    }
    sShowSmooth = !sShowSmooth;
    printf("bfbb: hipoly: showing the %s geometry (%u atomics)\n", sShowSmooth ? "smoothed" : "shipped", sSwaps.n);
    fflush(stdout);
}

void iHipolyForget(RpClump* rpclump)
{
    if (rpclump == NULL || sSwaps.n == 0)
    {
        return;
    }
    rw::Clump* clump = reinterpret_cast<rw::Clump*>(rpclump);
    U32 kept = 0;
    for (U32 i = 0; i < sSwaps.n; i++)
    {
        Swap& sw = sSwaps[i];
        bool mine = false;
        FORLIST(link, clump->atomics)
        {
            if (rw::Atomic::fromClump(link) == sw.atomic)
            {
                mine = true;
                break;
            }
        }
        if (mine)
        {
            if (sw.atomic->renderCB == swapRenderCB)
            {
                sw.atomic->renderCB = sw.render;
            }
            sw.shipped->destroy();
            sw.smooth->destroy();
        }
        else
        {
            sSwaps[kept++] = sw;
        }
    }
    sSwaps.n = kept;
}
