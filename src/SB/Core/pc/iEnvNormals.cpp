// librw's headers BEFORE the game's, and that ordering is load-bearing:
// include/types.h defines `null` as a macro and rwengine.h declares
// `namespace null`. The game header wins if it goes first and the error names
// neither cause. Same reason rw/engine_start.cpp opens the way it does.
#include <rw.h>

#include "iEnvNormals.h"

#include "iDayNight.h"

#include "iEnv.h"
#include "iScreen.h"
#include "xClumpColl.h"
#include "xMath3.h"
#include "xMathInlines.h"

#include <math.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

// Normals for a world that shipped without them. iEnvNormals.h says what this
// is for; this file is how.
//
// **The mesh already encodes the smoothing the artist wanted, and the trick is
// not to destroy it.** An exporter writing a hard crease does it by splitting
// the vertex: two vertices at one position, each carrying the normal of its own
// side. So faces are summed per vertex INDEX, which reproduces those splits
// exactly and for free -- a vertex only ever receives the faces that name it.
//
// Matching by position instead, and averaging everything that lands on one
// point, throws away every crease in the level. Measured against a level that
// ships normals, that version agreed with the artists on 37% of vertices within
// five degrees and was 37 degrees out on average.
//
// Position matching still has a job, but a narrower one: a JSP world is cut
// into hundreds of pieces for culling and the cuts run straight through flat
// ground, so the two halves of a cut surface never see each other's faces.
// Those are healed below -- but only between vertices that already agree, so
// healing a seam can never round off a crease.

// How far apart two normals at one position may be and still be treated as one
// surface that was cut in two.
//
// A cut through flat ground leaves halves that agree almost exactly; a real
// crease is what the exporter split on and is far wider than this. Sixty
// degrees sits in the gap with room on both sides.
static const F32 kSeamCos = 0.5f;

// A vertex whose faces cancel exactly, or are all degenerate. Straight up beats
// a zero, which is what breaks a shader that normalizes -- see DoShadowNdl.
static const F32 kDegenerate = 1e-20f;

// What a ground decal's baked vertex colour is replaced with, which is what a
// character carries: nothing.
//
// Enabling a light kit asks for rpGEOMETRYPRELIT to be dropped, and a geometry
// with no prelight stream is fed a constant black one, so every bit of a
// character's colour comes from the light and the whole day/night tint lands on
// it. Zeroing the colours in place reaches the same number without losing the
// stream, and the stream has to stay: its alpha is what fades the decal's rim
// into the sand.
//
// **Do not scale the surfaceProps to make this brighter.** The vertex shader
// clamps the colour to 1.0 before it multiplies by the material colour, so an
// ambient gain over about 2.4 saturates all three channels against an ambient
// of 0.41/0.36/0.34, and the light's hue goes with them. That is a decal that
// cannot turn blue at night however bright it is.
static const U8 kDecalPrelight = 0;

struct NormalHash
{
    U32* slots;  // vertex index + 1, 0 meaning empty
    S32* head;   // first vertex of the group in that slot
    S32* next;   // next vertex at the same position, -1 to end
    S32 mask;
};

// Bit-mixing over the three coordinates.
//
// The key is the position's exact bits and not a rounded value. Vertices meant
// to be the same point come from one authored number and are bit-identical;
// quantising introduces a grid whose cell boundaries split pairs that were
// already equal, which is a worse failure and a subtler one.
static U32 HashPos(const xVec3* p)
{
    const U32* w = (const U32*)p;

    // Zero and negative zero are the same point and do not share bits.
    U32 a = (w[0] << 1) == 0 ? 0 : w[0];
    U32 b = (w[1] << 1) == 0 ? 0 : w[1];
    U32 c = (w[2] << 1) == 0 ? 0 : w[2];

    U32 h = a * 0x8DA6B343u + b * 0xD8163841u + c * 0xCB1AB31Fu;

    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;

    return h;
}

static S32 SamePos(const xVec3* a, const xVec3* b)
{
    return a->x == b->x && a->y == b->y && a->z == b->z;
}

struct AtomicList
{
    RpAtomic** items;
    S32 count;
    S32 max;
};

static RpAtomic* CollectCB(RpAtomic* atomic, void* data)
{
    AtomicList* list = (AtomicList*)data;

    if (list->count < list->max)
    {
        list->items[list->count] = atomic;
        list->count++;
    }

    return atomic;
}

static RpAtomic* CountAtomicCB(RpAtomic* atomic, void* data)
{
    (*(S32*)data)++;
    return atomic;
}

// A geometry this pass can work with: it has vertices and a triangle list to
// take face normals from.
static S32 Usable(RpGeometry* geo)
{
    return geo != NULL && geo->numVertices > 0 && geo->numTriangles > 0 &&
           geo->morphTarget != NULL && geo->morphTarget[0].verts != NULL;
}

// Object space to world and back. The clump's atomics may each carry a frame.
//
// Rigid transforms only, which is what a level's static geometry has: the
// inverse of a rotation is its transpose, so going back is three dot products
// against the same axes rather than a matrix inversion.
static void ToWorld(xVec3* out, const xVec3* v, const RwMatrix* m)
{
    out->x = v->x * m->right.x + v->y * m->up.x + v->z * m->at.x + m->pos.x;
    out->y = v->x * m->right.y + v->y * m->up.y + v->z * m->at.y + m->pos.y;
    out->z = v->x * m->right.z + v->y * m->up.z + v->z * m->at.z + m->pos.z;
}

static void DirToObject(xVec3* out, const xVec3* v, const RwMatrix* m)
{
    out->x = v->x * m->right.x + v->y * m->right.y + v->z * m->right.z;
    out->y = v->x * m->up.x + v->y * m->up.y + v->z * m->up.z;
    out->z = v->x * m->at.x + v->y * m->at.y + v->z * m->at.z;
}

// The transpose of DirToObject: an object-space direction into world space.
// Correct for a rotation, which is all a JSP atomic's frame carries.
static void DirToWorld(xVec3* out, const xVec3* v, const RwMatrix* m)
{
    out->x = v->x * m->right.x + v->y * m->up.x + v->z * m->at.x;
    out->y = v->x * m->right.y + v->y * m->up.y + v->z * m->at.y;
    out->z = v->x * m->right.z + v->y * m->up.z + v->z * m->at.z;
}

static S32 Normalize(xVec3* out, const xVec3* v)
{
    F32 len2 = v->x * v->x + v->y * v->y + v->z * v->z;

    if (len2 < kDegenerate)
    {
        out->assign(0.0f, 1.0f, 0.0f);
        return FALSE;
    }

    F32 inv = 1.0f / xsqrt(len2);

    out->assign(v->x * inv, v->y * inv, v->z * inv);
    return TRUE;
}

// Everything one run needs, so generate and compare can share it.
struct NormalWork
{
    RpAtomic** atomics;
    S32 numAtomics;
    S32 totalVerts;
    S32* base;  // where each atomic's vertices start
    xVec3* pos; // world position, per vertex
    xVec3* acc; // summed face normals, per vertex
    xVec3* out; // the answer, world space and unnormalized, per vertex
    NormalHash hash;
};

// Where one atomic's normals come from, set up once per atomic.
//
// **Its own, wherever it has any.** 21 of the 55 levels shipped normals, and on
// those the artists' set is what the surface was authored with;
// iEnvNormalsCompare measures the generated ones at 62% to 78% inside 5 degrees
// of them, so reading the authored set is free accuracy. The generated block is
// the fallback for the other 34, and for anything hipoly rebuilt.
//
// Set up before the vertex loop rather than inside it: the geometry, the frame
// and the LTM are per atomic, and looking them up per vertex is the same answer
// forty thousand times.
struct NormalRead
{
    const xVec3* own; // object space, NULL to use the generated block
    const RwMatrix* ltm; // NULL for identity
    const xVec3* gen;
};

static void NormalReadInit(NormalRead* r, const NormalWork* w, S32 a)
{
    RpGeometry* geo = RpAtomicGetGeometry(w->atomics[a]);
    RwFrame* frame = RpAtomicGetFrame(w->atomics[a]);

    r->own = NULL;
    r->ltm = frame ? RwFrameGetLTM(frame) : NULL;
    r->gen = &w->out[w->base[a]];

    if (geo != NULL && (geo->flags & rpGEOMETRYNORMALS) && geo->morphTarget[0].normals != NULL)
    {
        r->own = (const xVec3*)geo->morphTarget[0].normals;
    }
}

static void NormalReadAt(xVec3* n, const NormalRead* r, S32 i)
{
    if (r->own == NULL)
    {
        Normalize(n, &r->gen[i]);
        return;
    }

    if (r->ltm != NULL)
    {
        xVec3 world;

        DirToWorld(&world, &r->own[i], r->ltm);
        Normalize(n, &world);
        return;
    }

    Normalize(n, &r->own[i]);
}


static void WorkFree(NormalWork* w)
{
    if (w->hash.slots) RwFree(w->hash.slots);
    if (w->hash.head) RwFree(w->hash.head);
    if (w->hash.next) RwFree(w->hash.next);
    if (w->pos) RwFree(w->pos);
    if (w->acc) RwFree(w->acc);
    if (w->out) RwFree(w->out);
    if (w->base) RwFree(w->base);
    if (w->atomics) RwFree(w->atomics);
    memset(w, 0, sizeof(*w));
}

// Sum the face normals onto each vertex, in world space.
static void Accumulate(NormalWork* w)
{
    for (S32 a = 0; a < w->numAtomics; a++)
    {
        RpGeometry* geo = RpAtomicGetGeometry(w->atomics[a]);

        if (!Usable(geo))
        {
            continue;
        }

        RwFrame* frame = RpAtomicGetFrame(w->atomics[a]);
        RwMatrix* ltm = frame ? RwFrameGetLTM(frame) : NULL;
        const xVec3* verts = (const xVec3*)geo->morphTarget[0].verts;
        xVec3* pos = &w->pos[w->base[a]];
        xVec3* acc = &w->acc[w->base[a]];

        for (S32 i = 0; i < geo->numVertices; i++)
        {
            if (ltm)
            {
                ToWorld(&pos[i], &verts[i], ltm);
            }
            else
            {
                pos[i] = verts[i];
            }

            acc[i].assign(0.0f, 0.0f, 0.0f);
        }

        for (S32 t = 0; t < geo->numTriangles; t++)
        {
            const RpTriangle* tri = &geo->triangles[t];
            S32 i0 = tri->vertIndex[0];
            S32 i1 = tri->vertIndex[1];
            S32 i2 = tri->vertIndex[2];

            xVec3 e1;
            xVec3 e2;
            xVec3 n;

            xVec3Sub(&e1, &pos[i1], &pos[i0]);
            xVec3Sub(&e2, &pos[i2], &pos[i0]);
            xVec3Cross(&n, &e1, &e2);

            // **Every face counts the same, and a degenerate one counts for
            // nothing.** Weighting by area was measured against a level that
            // ships normals and came out very slightly worse (mean dot 0.968
            // against 0.970), so the artists' tool did not do it either.
            //
            // The early return matters more than the weighting. A zero-area
            // face -- and a tristrip turned into a triangle list is full of
            // them -- has no direction at all, and Normalize answers a
            // direction question it cannot answer with straight up. Summing
            // that would tilt every vertex of every strip towards the sky.
            xVec3 unit;

            if (!Normalize(&unit, &n))
            {
                continue;
            }

            n = unit;

            xVec3AddTo(&acc[i0], &n);
            xVec3AddTo(&acc[i1], &n);
            xVec3AddTo(&acc[i2], &n);
        }
    }
}

// Group the vertices that share a world position, one singly-linked list per
// position hung off its hash slot.
static S32 GroupByPosition(NormalWork* w)
{
    S32 cap = 1;

    while (cap < w->totalVerts * 2)
    {
        cap *= 2;
    }

    w->hash.mask = cap - 1;
    w->hash.slots = (U32*)RwMalloc(cap * sizeof(U32));
    w->hash.head = (S32*)RwMalloc(cap * sizeof(S32));
    w->hash.next = (S32*)RwMalloc(w->totalVerts * sizeof(S32));

    if (w->hash.slots == NULL || w->hash.head == NULL || w->hash.next == NULL)
    {
        return FALSE;
    }

    memset(w->hash.slots, 0, cap * sizeof(U32));

    for (S32 v = 0; v < w->totalVerts; v++)
    {
        U32 i = HashPos(&w->pos[v]) & (U32)w->hash.mask;

        while (w->hash.slots[i] != 0 && !SamePos(&w->pos[w->hash.slots[i] - 1], &w->pos[v]))
        {
            i = (i + 1) & (U32)w->hash.mask;
        }

        if (w->hash.slots[i] == 0)
        {
            w->hash.slots[i] = (U32)v + 1;
            w->hash.head[i] = -1;
        }

        w->hash.next[v] = w->hash.head[i];
        w->hash.head[i] = v;
    }

    return TRUE;
}

// Heal the cuts. For each vertex, add in the faces of the vertices at the same
// position whose own normal already points the same way.
//
// The gate is what makes this safe. Without it this is the position-averaging
// version again, rounding off every crease the exporter went to the trouble of
// splitting; with it, two halves of a cut flat surface find each other and two
// sides of a corner do not.
static void HealSeams(NormalWork* w)
{
    S32 cap = w->hash.mask + 1;

    for (S32 i = 0; i < cap; i++)
    {
        if (w->hash.slots[i] == 0)
        {
            continue;
        }

        S32 first = w->hash.head[i];

        // One vertex at this position: nothing was cut, so nothing to heal.
        if (w->hash.next[first] < 0)
        {
            w->out[first] = w->acc[first];
            continue;
        }

        for (S32 v = first; v >= 0; v = w->hash.next[v])
        {
            xVec3 mine;
            xVec3 sum = w->acc[v];

            if (!Normalize(&mine, &w->acc[v]))
            {
                w->out[v] = w->acc[v];
                continue;
            }

            for (S32 u = first; u >= 0; u = w->hash.next[u])
            {
                xVec3 theirs;

                if (u == v || !Normalize(&theirs, &w->acc[u]))
                {
                    continue;
                }

                if (mine.x * theirs.x + mine.y * theirs.y + mine.z * theirs.z >= kSeamCos)
                {
                    xVec3AddTo(&sum, &w->acc[u]);
                }
            }

            w->out[v] = sum;
        }
    }
}

static S32 WorkBuild(NormalWork* w, RpClump* clump)
{
    memset(w, 0, sizeof(*w));

    S32 numAtomics = 0;
    RpClumpForAllAtomics(clump, CountAtomicCB, &numAtomics);

    if (numAtomics == 0)
    {
        return FALSE;
    }

    w->atomics = (RpAtomic**)RwMalloc(numAtomics * sizeof(RpAtomic*));
    w->base = (S32*)RwMalloc(numAtomics * sizeof(S32));

    if (w->atomics == NULL || w->base == NULL)
    {
        WorkFree(w);
        return FALSE;
    }

    AtomicList list;
    list.items = w->atomics;
    list.count = 0;
    list.max = numAtomics;
    RpClumpForAllAtomics(clump, CollectCB, &list);
    w->numAtomics = list.count;

    for (S32 a = 0; a < w->numAtomics; a++)
    {
        RpGeometry* geo = RpAtomicGetGeometry(w->atomics[a]);

        w->base[a] = w->totalVerts;

        if (Usable(geo))
        {
            w->totalVerts += geo->numVertices;
        }
    }

    if (w->totalVerts == 0)
    {
        WorkFree(w);
        return FALSE;
    }

    w->pos = (xVec3*)RwMalloc(w->totalVerts * sizeof(xVec3));
    w->acc = (xVec3*)RwMalloc(w->totalVerts * sizeof(xVec3));
    w->out = (xVec3*)RwMalloc(w->totalVerts * sizeof(xVec3));

    if (w->pos == NULL || w->acc == NULL || w->out == NULL)
    {
        WorkFree(w);
        return FALSE;
    }

    Accumulate(w);

    if (!GroupByPosition(w))
    {
        WorkFree(w);
        return FALSE;
    }

    HealSeams(w);
    return TRUE;
}

// Whether this world already has normals. All-or-nothing per level, so the
// first usable geometry answers for the clump.
static S32 HasNormals(NormalWork* w)
{
    for (S32 a = 0; a < w->numAtomics; a++)
    {
        RpGeometry* geo = RpAtomicGetGeometry(w->atomics[a]);

        if (Usable(geo))
        {
            return (geo->flags & rpGEOMETRYNORMALS) != 0;
        }
    }

    return TRUE;
}

// The generated normal for one vertex, in that atomic's object space.
static void ReadNormal(xVec3* out, const NormalWork* w, S32 v, const RwMatrix* ltm)
{
    xVec3 n;

    Normalize(&n, &w->out[v]);

    if (ltm)
    {
        DirToObject(out, &n, ltm);
    }
    else
    {
        *out = n;
    }
}


// Whether this geometry's prelight is ARTWORK rather than a record of light.
//
// Some of a level is painted, not lit. Lighting it fresh does not improve it,
// it deletes it, and no rig can put it back because none of it was ever a
// function of the surface's normal.
//
// Both shapes below are read off the prelight itself and not off a name, so
// they hold on the levels this was never measured against:
//
//   **Vertex alpha over MUCH of the piece.** The colour is being blended
//   rather than shown. bb01 fakes the shadow of every building with a piece of
//   ordinary street laid over the street -- ground_alpha2 and ground_day3,
//   the same textures as the road it sits on -- darkened in RGB and faded out
//   at the edges by the alpha. Drop the prelight and the decal becomes opaque
//   and brightly lit, so the shadow turns into a patch of rock. The texture
//   was always rock; the shadow was only ever the paint.
//
//   **A prelight that is uniformly black.** Nothing about the light is
//   recorded in it, so there is nothing to replace, and the geometry is
//   invisible for a reason of its own -- bb01's collision walls carry a fully
//   transparent texture and a black vertex colour. Lighting one can only
//   invent light the artist did not put there.
//
// **A FRACTION and not "any", which is what a seam is made of.** Asking whether
// any vertex is see-through keeps a piece of ordinary opaque ground because two
// dozen vertices along one edge fade into the piece next to it. hb01 has four
// of those, 2,389 vertices of out-of-bounds sand between them and 1.3% to 4.8%
// of each one non-opaque, and they held their noon paint while the ground
// around them was relit: measured over the whole level the kept half averages
// 130/110/101 against the lit half's 161/147/144, so they read as darker and
// redder, and the boundary is visible from anywhere on the map.
//
// The two populations separate cleanly. bb01's real decals are 28% to 100%
// non-opaque and the pieces that only fade at an edge are all under 20%, with
// nothing in between, so a quarter sits in the gap rather than on a slope.
static S32 PrelightIsArtwork(RpGeometry* geo)
{
    if (geo == NULL || geo->preLitLum == NULL || geo->numVertices == 0)
    {
        return FALSE;
    }

    S32 lit = FALSE;
    S32 blended = 0;

    for (S32 i = 0; i < geo->numVertices; i++)
    {
        RwRGBA* c = &geo->preLitLum[i];

        if (c->alpha != 255)
        {
            blended++;
        }

        if (c->red != 0 || c->green != 0 || c->blue != 0)
        {
            lit = TRUE;
        }
    }

    if (!lit)
    {
        return TRUE;
    }

    return blended * 4 >= geo->numVertices;
}

// Recover the rig this level was baked from.
//
// **The light kit is not what lit the world.** A kit lights the objects that
// move through a level; the world's colour was baked by a separate rig, and in
// bb01 the two disagree by most of a half turn -- the kit's key light comes
// from (-0.509, 0.595, 0.622) and the bake from (0.343, 0.678, -0.650). Same
// side vertically, opposite side horizontally. A shadow cast along the kit's
// key light therefore falls on the wrong side of everything the artists lit.
//
// The bake is the only record of that rig, and with normals it is a readable
// one. What is fitted here is the equation the renderer actually evaluates,
//
//     lum = ambient + sum over lights of colour * max(0, dot(n, s))
//
// which is NOT linear in the light direction, because of the clamp. Fitting
// the signed dot instead is linear and has a closed form, and it is what this
// did first; it is also measurably worse, because it lets a surface facing
// away pull the answer around with light it will never receive. On bb01 that
// costs 6% of the residual, on jf01 5%.
//
// So the directions come from a search rather than a solve. Every candidate
// direction is one column of a Gram matrix built in a single pass over the
// world, and the search then works entirely inside that matrix -- lights are
// added one at a time, each one chosen as whichever remaining direction leaves
// the least residual once all the amplitudes are refitted. Amplitudes are
// constrained non-negative: a light with negative colour reproduces a bake very
// slightly better and renders as a hole.
//
// **Most of a bake is not a function of the normal at all, and no rig can
// ever get it.** Measured as the spread of baked colour among vertices that
// face the same way, it is 54% of the level's colour variation on bb01, 57% on
// jf01, 65% on gy01 and 80% on hb01 -- occlusion, bounce, and shadow painted in
// by hand. That is the ceiling this fit works against, and it is why turning
// world lighting on loses artwork however good the fit gets.
//
// What is left is still worth having. Against a level lit flat at its mean
// colour, the fit explains 40% of the variation on bb01, 37% on jf01, 28% on
// gy01 and 13% on hb01, where the single signed-dot directional it replaced
// managed 21%, 27%, 17% and 8%. Each is within a sixth of the ceiling above.

// Directions the search chooses from, spread over the sphere. 48, 96 and 128
// all land within 0.001 rms of this on every level measured: the residual is
// dominated by light no normal can explain, not by how finely the sphere is
// sampled.
static const S32 kFitBasis = 64;

// Vertices the Gram matrix is built from. The pass is O(kFitBasis^2) per
// vertex, and a level is fitted at load, so it is strided rather than run over
// every vertex of a 40,000-vertex world. Sampling this way costs under 0.001
// rms against using all of them.
static const S32 kFitSample = 12000;

// Rounds of projected gradient on each candidate's normal equations. The
// systems are five unknowns at most, so this is cheap and well past converged.
static const S32 kFitIters = 200;


// Fibonacci sphere: the cheapest even spread that needs no tables.
static void FitBasis(xVec3* b)
{
    for (S32 i = 0; i < kFitBasis; i++)
    {
        double t = (i + 0.5) / kFitBasis;
        double y = 1.0 - 2.0 * t;
        double r = sqrt(1.0 - y * y);
        double th = 3.883222077450933 * (i + 0.5); // pi * (1 + sqrt 5)

        b[i].assign((F32)(cos(th) * r), (F32)y, (F32)(sin(th) * r));
    }
}

// min ||Ax - rhs|| over x >= 0, for the small dense systems the search builds.
// Projected gradient, stepped by a Gershgorin bound on the largest eigenvalue,
// which needs no eigen decomposition and cannot overshoot.
static void FitSolveNN(const double* A, const double* rhs, S32 n, double* x)
{
    double step = 0.0;

    for (S32 i = 0; i < n; i++)
    {
        double sum = 0.0;

        for (S32 j = 0; j < n; j++)
        {
            sum += A[i * n + j] < 0.0 ? -A[i * n + j] : A[i * n + j];
        }

        if (sum > step) step = sum;
    }

    for (S32 i = 0; i < n; i++) x[i] = 0.0;

    if (step <= 0.0)
    {
        return;
    }

    for (S32 it = 0; it < kFitIters; it++)
    {
        for (S32 i = 0; i < n; i++)
        {
            double g = -rhs[i];

            for (S32 j = 0; j < n; j++) g += A[i * n + j] * x[j];

            double v = x[i] - g / step;

            x[i] = v > 0.0 ? v : 0.0;
        }
    }
}

static void RecoverBakedLight(NormalWork* w, iEnvBakedRig* rig)
{
    const S32 nc = kFitBasis + 1; // column 0 is the ambient, then one per direction

    xVec3 basis[kFitBasis];
    double* G = (double*)RwMalloc(nc * nc * sizeof(double));
    double* R = (double*)RwMalloc(nc * 3 * sizeof(double));

    if (G == NULL || R == NULL)
    {
        RwFree(G);
        RwFree(R);
        return;
    }

    FitBasis(basis);

    for (S32 i = 0; i < nc * nc; i++) G[i] = 0.0;
    for (S32 i = 0; i < nc * 3; i++) R[i] = 0.0;

    double cc[3] = { 0.0, 0.0, 0.0 };
    double csum[3] = { 0.0, 0.0, 0.0 };
    S32 used = 0;
    S32 stride = w->totalVerts / kFitSample;

    if (stride < 1) stride = 1;

    double* row = (double*)RwMalloc(nc * sizeof(double));

    if (row == NULL)
    {
        RwFree(G);
        RwFree(R);
        return;
    }

    row[0] = 1.0;

    for (S32 a = 0; a < w->numAtomics; a++)
    {
        RpGeometry* geo = RpAtomicGetGeometry(w->atomics[a]);

        if (!Usable(geo) || geo->preLitLum == NULL || PrelightIsArtwork(geo))
        {
            continue;
        }

        NormalRead nr;

        NormalReadInit(&nr, w, a);

        for (S32 i = 0; i < geo->numVertices; i += stride)
        {
            xVec3 n;

            NormalReadAt(&n, &nr, i);

            for (S32 j = 0; j < kFitBasis; j++)
            {
                double d = n.x * basis[j].x + n.y * basis[j].y + n.z * basis[j].z;

                row[1 + j] = d > 0.0 ? d : 0.0;
            }

            RwRGBA* col = &geo->preLitLum[i];
            double lum[3] = { col->red / 255.0, col->green / 255.0, col->blue / 255.0 };

            // Upper triangle only; the search reads it symmetrically.
            for (S32 r = 0; r < nc; r++)
            {
                if (row[r] == 0.0) continue;

                for (S32 c = r; c < nc; c++) G[r * nc + c] += row[r] * row[c];
                for (S32 c = 0; c < 3; c++) R[r * 3 + c] += row[r] * lum[c];
            }

            for (S32 c = 0; c < 3; c++)
            {
                cc[c] += lum[c] * lum[c];
                csum[c] += lum[c];
            }

            used++;
        }
    }

    RwFree(row);

    if (used == 0)
    {
        RwFree(G);
        RwFree(R);
        return;
    }

    for (S32 r = 0; r < nc; r++)
        for (S32 c = r + 1; c < nc; c++) G[c * nc + r] = G[r * nc + c];

    // Add lights one at a time, each the direction that leaves the least
    // residual once every amplitude is refitted around it.
    S32 chosen[iENV_BAKED_LIGHTS];
    S32 taken[kFitBasis];
    S32 count = 0;
    double sol[iENV_BAKED_LIGHTS + 1][3];

    for (S32 j = 0; j < kFitBasis; j++) taken[j] = 0;
    for (S32 c = 0; c < 3; c++) sol[0][c] = 0.0;

    double have = cc[0] + cc[1] + cc[2];

    for (S32 round = 0; round < iENV_BAKED_LIGHTS; round++)
    {
        S32 bestj = -1;
        double bestErr = 0.0;
        double bestSol[iENV_BAKED_LIGHTS + 1][3];


        for (S32 j = 0; j < kFitBasis; j++)
        {
            if (taken[j])
            {
                continue;
            }

            S32 cols[iENV_BAKED_LIGHTS + 2];
            S32 n = count + 2;

            cols[0] = 0;
            for (S32 k = 0; k < count; k++) cols[1 + k] = 1 + chosen[k];
            cols[n - 1] = 1 + j;

            double A[(iENV_BAKED_LIGHTS + 2) * (iENV_BAKED_LIGHTS + 2)];
            double rhs[iENV_BAKED_LIGHTS + 2];
            double x[iENV_BAKED_LIGHTS + 2];
            double err = 0.0;
            double trial[iENV_BAKED_LIGHTS + 1][3];

            for (S32 r = 0; r < n; r++)
                for (S32 c = 0; c < n; c++) A[r * n + c] = G[cols[r] * nc + cols[c]];

            for (S32 ch = 0; ch < 3; ch++)
            {
                for (S32 r = 0; r < n; r++) rhs[r] = R[cols[r] * 3 + ch];

                FitSolveNN(A, rhs, n, x);

                // ||Ax - b||^2 in the normal-equation form, so no second pass
                // over the world is needed to score a candidate.
                double e = cc[ch];

                for (S32 r = 0; r < n; r++)
                {
                    e -= 2.0 * x[r] * rhs[r];

                    for (S32 c = 0; c < n; c++) e += x[r] * A[r * n + c] * x[c];
                }

                err += e;

                for (S32 r = 0; r < n; r++) trial[r][ch] = x[r];
            }

            if (bestj < 0 || err < bestErr)
            {
                bestj = j;
                bestErr = err;

                for (S32 r = 0; r < n; r++)
                    for (S32 ch = 0; ch < 3; ch++) bestSol[r][ch] = trial[r][ch];
            }

        }

        // A round that buys less than a thousandth of what is left is noise,
        // and a light whose amplitude came back zero is not a light.
        if (bestj < 0 || bestErr > have - have * 0.001)
        {
            break;
        }

        double amp = 0.0;

        for (S32 ch = 0; ch < 3; ch++) amp += bestSol[count + 1][ch];

        if (amp <= 0.0)
        {
            break;
        }

        taken[bestj] = 1;
        chosen[count] = bestj;
        count++;
        have = bestErr;

        for (S32 r = 0; r <= count; r++)
            for (S32 ch = 0; ch < 3; ch++) sol[r][ch] = bestSol[r][ch];
    }

    if (count == 0)
    {
        // Lit flat, or by ambient alone. Nothing to hand back, and the caller
        // keeps whatever it had.
        RwFree(G);
        RwFree(R);
        return;
    }

    // Brightest first, so dir[0] is the one a shadow should follow.
    for (S32 i = 0; i < count; i++)
    {
        for (S32 j = i + 1; j < count; j++)
        {
            double bi = sol[1 + i][0] + sol[1 + i][1] + sol[1 + i][2];
            double bj = sol[1 + j][0] + sol[1 + j][1] + sol[1 + j][2];

            if (bj > bi)
            {
                S32 t = chosen[i];
                chosen[i] = chosen[j];
                chosen[j] = t;

                for (S32 ch = 0; ch < 3; ch++)
                {
                    double v = sol[1 + i][ch];
                    sol[1 + i][ch] = sol[1 + j][ch];
                    sol[1 + j][ch] = v;
                }
            }
        }
    }

    rig->count = count;

    for (S32 ch = 0; ch < 3; ch++)
    {
        rig->ambient[ch] = (F32)sol[0][ch];
        rig->dirMean[ch] = 0.0f;
    }

    for (S32 i = 0; i < count; i++)
    {
        const xVec3* s = &basis[chosen[i]];

        // Negated on the way out: the fit points TOWARDS the light and
        // everything downstream of here means the direction it travels.
        rig->dir[i].assign(-s->x, -s->y, -s->z);

        // G[0][col] is the sum of max(0, n.s) over the sampled world, so the
        // mean the contrast setting needs is already in the matrix.
        double mean = G[0 * nc + (1 + chosen[i])] / used;

        for (S32 ch = 0; ch < 3; ch++)
        {
            rig->color[i][ch] = (F32)sol[1 + i][ch];
            rig->dirMean[ch] += (F32)(sol[1 + i][ch] * mean);
        }
    }

    rig->valid = TRUE;

    // How much of the level's colour the rig accounts for, against a level
    // painted flat at its own mean. `have` is the residual the greedy stopped
    // at, in the same sum-of-squares terms.
    double spread = 0.0;

    for (S32 c = 0; c < 3; c++)
    {
        spread += cc[c] - csum[c] * csum[c] / used;
    }

    printf("bfbb: world lit by %d light(s), ambient %.2f %.2f %.2f, explains %.0f%% of the "
           "paint, over %d of %d vertices\n",
           (int)count, rig->ambient[0], rig->ambient[1], rig->ambient[2],
           spread > 0.0 ? 100.0 * (1.0 - have / spread) : 0.0, (int)used, (int)w->totalVerts);

    for (S32 i = 0; i < count; i++)
    {
        printf("bfbb:   light %d from %.3f %.3f %.3f, colour %.2f %.2f %.2f\n", (int)i,
               -rig->dir[i].x, -rig->dir[i].y, -rig->dir[i].z,
               rig->color[i][0], rig->color[i][1],
               rig->color[i][2]);
    }

    RwFree(G);
    RwFree(R);
}

// Take the baked colour off the world, so a run-time light replaces it instead
// of adding to it.
//
// Before instancing, like everything else here: the flag is what decides
// whether the vertex buffer carries a colour at all, and clearing it afterwards
// would leave the colour in the buffer and only stop the shader being told.
//
// Not iModelHack_DisablePrelight, which does the same job for models. The world
// is drawn by Jsp_ClumpRender straight through RpAtomicRender, and that hack is
// read by iModelRender, which the world never reaches.
// The rig fitted from a clump before anything rebuilt it, and the clump it came
// from. One slot: worlds load one at a time, and a slot that is not claimed is
// simply overwritten.
static RpClump* sShippedClump;
static iEnvBakedRig sShippedRig;

void iEnvFitShippedRig(RpClump* clump)
{
    NormalWork w;

    sShippedClump = NULL;
    memset(&sShippedRig, 0, sizeof(sShippedRig));

    if (clump == NULL || !WorkBuild(&w, clump))
    {
        return;
    }

    RecoverBakedLight(&w, &sShippedRig);
    WorkFree(&w);

    if (sShippedRig.valid)
    {
        sShippedClump = clump;
    }
}

// The rig for this clump, if one was fitted before its geometry was rebuilt.
static S32 TakeShippedRig(RpClump* clump, iEnvBakedRig* out)
{
    if (clump == NULL || sShippedClump != clump || !sShippedRig.valid)
    {
        return FALSE;
    }

    *out = sShippedRig;
    sShippedClump = NULL;
    return TRUE;
}

// How high a light has to sit before it may cast a shadow, as the sine of its
// elevation. 30 degrees.
//
// **A grazing light fits a bake nearly as well as a high one and casts nothing
// like the same shadow.** hb01 is the case: its rig explains only 15% of the
// level's colour variation and its ambient is stronger than its key light, so
// the fit's brightest direction is barely determined and lands anywhere from 19
// to 38 degrees up depending on which vertices are sampled. Traced from 19
// degrees the shadow covers two thirds of the level.
//
// A shading term is forgiving about this and an occlusion test is not, which is
// why the floor lives here and not in the fit. The fit is left exactly as
// measured, so nothing about the level's shading changes.
static const F32 kSunFloor = 0.5f;

// Which of the rig's lights may cast, or -1 for none.
//
// The brightest one high enough to be a sun. Not simply the first: the rig is
// ordered by brightness, and on a level whose light is mostly ambient the
// brightest direction can be a grazing one. A level with nothing above the floor
// gets no traced shadows, which is the right answer for an interior.
static S32 SunLight(const iEnvBakedRig* rig)
{
    S32 best = -1;
    F32 bestLum = 0.0f;

    for (S32 k = 0; k < rig->count; k++)
    {
        // dir is where the light travels, so a light from above points down.
        if (-rig->dir[k].y < kSunFloor)
        {
            continue;
        }

        F32 lum = rig->color[k][0] + rig->color[k][1] + rig->color[k][2];

        if (best < 0 || lum > bestLum)
        {
            best = k;
            bestLum = lum;
        }
    }

    return best;
}

// The rig at a contrast, shared with zScene so the kit and the bake agree.
void iEnvRigAtContrast(const iEnvBakedRig* rig, F32 contrast, F32 ambient[3],
                       F32 color[iENV_BAKED_LIGHTS][3])
{
    // **A level can only lend so much of its ambient to the directionals.**
    //
    // The ambient pays for the boost, so above 1 + ambient/dirMean it would have
    // to go negative and the clamp below swallows the difference: the level ends
    // with no ambient at all AND under the brightness this was supposed to hold.
    // Every face none of the four lights reaches then renders pure black. bb01
    // can absorb 1.61 and hb01 2.55, so the shipped 2.5 emptied bb01's ambient
    // completely. Capped per level, by whichever channel has least to give.
    //
    // Below 1 the ambient is gaining rather than paying, so nothing is capped.
    F32 cap = contrast;

    for (S32 i = 0; i < 3; i++)
    {
        if (rig->dirMean[i] > 0.0001f)
        {
            F32 h = 1.0f + rig->ambient[i] / rig->dirMean[i];

            if (h < cap)
            {
                cap = h;
            }
        }
    }

    if (cap < 1.0f)
    {
        cap = 1.0f;
    }

    if (contrast > cap)
    {
        contrast = cap;
    }

    for (S32 i = 0; i < 3; i++)
    {
        F32 a = rig->ambient[i] + rig->dirMean[i] * (1.0f - contrast);

        ambient[i] = a > 0.0f ? a : 0.0f;
    }

    for (S32 k = 0; k < iENV_BAKED_LIGHTS; k++)
    {
        for (S32 i = 0; i < 3; i++)
        {
            color[k][i] = (k < rig->count) ? rig->color[k][i] * contrast : 0.0f;
        }

        // xLightKit_Prepare scales any light whose brightest channel exceeds 1
        // back down to 1, so the kit path saturates a light's COLOUR rather than
        // its result. Matched here, or the two paths would part company at any
        // contrast above about 1.3 and the shadows would arrive with a
        // brightness change stapled to them.
        F32 peak = color[k][0];

        if (color[k][1] > peak) peak = color[k][1];
        if (color[k][2] > peak) peak = color[k][2];

        if (peak > 1.0f)
        {
            for (S32 i = 0; i < 3; i++) color[k][i] /= peak;
        }
    }
}

// One shadow ray, through the tree the game already collides against.
// **The shade a level's placed models throw on the level, over a whole day.**
//
// The world is lit at run time and the sun moves, so a shadow cannot be traced
// once and kept. It cannot be traced per frame either: a house is thousands of
// triangles and the world is a hundred thousand vertices.
//
// So the day is traced at load, at kSunSteps points along the arc the sun
// actually takes, and the run time blends between the two steps it stands
// between. One byte per world vertex per step: on hb01 that is 112,633 vertices
// by twelve, about 1.3 MB.
//
// **Every ray for one step is parallel, and that is what makes it affordable.**
// A sun is a direction, not a place, so all the rays of a step point the same
// way -- and then a grid laid out square to that direction sorts every occluder
// triangle into the cell it covers, and a vertex tests only its own cell. That
// turns a search over every triangle into a lookup.

// How many points along the day. Twelve is one every thirty degrees, which the
// blend below carries between without a visible step.
enum
{
    kSunSteps = 12
};

struct EnvOccluder
{
    xVec3 centre;
    F32 radius;
    S32 first;
    S32 count;
};

struct EnvOccTri
{
    xVec3 v[3];
};

static EnvOccluder* sOcc;
static S32 sOccCount;
static S32 sOccMax;
static EnvOccTri* sOccTris;
static S32 sOccTriCount;
static S32 sOccTriMax;

// What the trace found: kSunSteps bytes per world vertex, a step at a time.
static U8* sSunShade;
static S32 sSunShadeVerts;

void iEnvOccluderClear()
{
    if (sOcc != NULL)
    {
        RwFree(sOcc);
    }
    if (sOccTris != NULL)
    {
        RwFree(sOccTris);
    }

    sOcc = NULL;
    sOccTris = NULL;
    sOccCount = 0;
    sOccMax = 0;
    sOccTriCount = 0;
    sOccTriMax = 0;
}

static S32 OccGrow(void** arr, S32* max, S32 want, S32 size)
{
    if (want <= *max)
    {
        return TRUE;
    }

    S32 grown = *max == 0 ? 1024 : *max;

    while (grown < want)
    {
        grown *= 2;
    }

    void* bigger = RwMalloc(grown * size);

    if (bigger == NULL)
    {
        return FALSE;
    }

    if (*arr != NULL)
    {
        memcpy(bigger, *arr, *max * size);
        RwFree(*arr);
    }

    *arr = bigger;
    *max = grown;
    return TRUE;
}

// A model the level placed, in world space.
//
// Held as triangles rather than asked of the game's collision, because
// collision is not the same set: a decorative rock often has none and an
// invisible wall is nothing but. What blocks light is what you can see.
void iEnvOccluderAdd(RpAtomic* model, const RwMatrix* mat)
{
    if (model == NULL || mat == NULL)
    {
        return;
    }

    RpGeometry* geo = RpAtomicGetGeometry(model);

    if (geo == NULL || geo->numTriangles <= 0 || geo->morphTarget == NULL ||
        geo->morphTarget[0].verts == NULL)
    {
        return;
    }

    const xVec3* src = (const xVec3*)geo->morphTarget[0].verts;
    S32 first = sOccTriCount;

    if (!OccGrow((void**)&sOccTris, &sOccTriMax, sOccTriCount + geo->numTriangles,
                 sizeof(EnvOccTri)) ||
        !OccGrow((void**)&sOcc, &sOccMax, sOccCount + 1, sizeof(EnvOccluder)))
    {
        return;
    }

    xVec3 lo;
    xVec3 hi;
    S32 any = FALSE;

    for (S32 t = 0; t < geo->numTriangles; t++)
    {
        EnvOccTri* out = &sOccTris[sOccTriCount];

        for (S32 c = 0; c < 3; c++)
        {
            ToWorld(&out->v[c], &src[geo->triangles[t].vertIndex[c]], mat);

            if (!any)
            {
                lo = out->v[c];
                hi = out->v[c];
                any = TRUE;
            }
            else
            {
                if (out->v[c].x < lo.x) lo.x = out->v[c].x;
                if (out->v[c].y < lo.y) lo.y = out->v[c].y;
                if (out->v[c].z < lo.z) lo.z = out->v[c].z;
                if (out->v[c].x > hi.x) hi.x = out->v[c].x;
                if (out->v[c].y > hi.y) hi.y = out->v[c].y;
                if (out->v[c].z > hi.z) hi.z = out->v[c].z;
            }
        }

        sOccTriCount++;
    }

    if (!any)
    {
        return;
    }

    EnvOccluder* occ = &sOcc[sOccCount++];

    occ->centre.assign(0.5f * (lo.x + hi.x), 0.5f * (lo.y + hi.y), 0.5f * (lo.z + hi.z));

    F32 dx = 0.5f * (hi.x - lo.x);
    F32 dy = 0.5f * (hi.y - lo.y);
    F32 dz = 0.5f * (hi.z - lo.z);

    occ->radius = xsqrt(dx * dx + dy * dy + dz * dz);
    occ->first = first;
    occ->count = sOccTriCount - first;
}

// Moller-Trumbore, counting both sides. A model's winding is no guide to which
// way it blocks light, and a one-sided test lets the sun through a wall's back.
static S32 RayHitsTri(const xVec3* from, const xVec3* dir, F32 reach, const EnvOccTri* tri)
{
    F32 e1x = tri->v[1].x - tri->v[0].x;
    F32 e1y = tri->v[1].y - tri->v[0].y;
    F32 e1z = tri->v[1].z - tri->v[0].z;
    F32 e2x = tri->v[2].x - tri->v[0].x;
    F32 e2y = tri->v[2].y - tri->v[0].y;
    F32 e2z = tri->v[2].z - tri->v[0].z;
    F32 px = dir->y * e2z - dir->z * e2y;
    F32 py = dir->z * e2x - dir->x * e2z;
    F32 pz = dir->x * e2y - dir->y * e2x;
    F32 det = e1x * px + e1y * py + e1z * pz;

    if (det > -1e-8f && det < 1e-8f)
    {
        return FALSE;
    }

    F32 inv = 1.0f / det;
    F32 tx = from->x - tri->v[0].x;
    F32 ty = from->y - tri->v[0].y;
    F32 tz = from->z - tri->v[0].z;
    F32 u = (tx * px + ty * py + tz * pz) * inv;

    if (u < 0.0f || u > 1.0f)
    {
        return FALSE;
    }

    F32 qx = ty * e1z - tz * e1y;
    F32 qy = tz * e1x - tx * e1z;
    F32 qz = tx * e1y - ty * e1x;
    F32 v = (dir->x * qx + dir->y * qy + dir->z * qz) * inv;

    if (v < 0.0f || u + v > 1.0f)
    {
        return FALSE;
    }

    F32 hit = (e2x * qx + e2y * qy + e2z * qz) * inv;

    return hit > 0.0f && hit < reach;
}

// The grid one step is traced through: the occluders sorted by where they sit
// across the sun's direction.
enum
{
    kSunGrid = 256,

    // A triangle wider than this many cells goes in the list every ray tests.
    // Bucketing a wall that spans the level into three thousand cells costs more
    // memory than testing it outright costs time.
    kSunSpread = 48
};

struct SunGrid
{
    xVec3 u;
    xVec3 v;
    F32 lo[2];
    F32 span[2];
    S32* start;   // kSunGrid*kSunGrid + 1
    S32* item;
    S32* wide;    // triangles too big to bucket
    S32 numWide;
};

static void SunGridFree(SunGrid* g)
{
    if (g->start) RwFree(g->start);
    if (g->item) RwFree(g->item);
    if (g->wide) RwFree(g->wide);
    memset(g, 0, sizeof(*g));
}

static void SunGridCell(const SunGrid* g, const xVec3* p, S32 out[2])
{
    F32 a[2];

    a[0] = p->x * g->u.x + p->y * g->u.y + p->z * g->u.z;
    a[1] = p->x * g->v.x + p->y * g->v.y + p->z * g->v.z;

    for (S32 k = 0; k < 2; k++)
    {
        F32 t = (a[k] - g->lo[k]) / g->span[k];
        S32 c = (S32)(t * (F32)kSunGrid);

        if (c < 0) c = 0;
        if (c >= kSunGrid) c = kSunGrid - 1;

        out[k] = c;
    }
}

static S32 SunGridBuild(SunGrid* g, const xVec3* toward)
{
    memset(g, 0, sizeof(*g));

    if (sOccTriCount == 0)
    {
        return FALSE;
    }

    // Any two directions square to the sun will do; the grid only has to be
    // flat against it.
    xVec3 pick;

    pick.assign(0.0f, 1.0f, 0.0f);

    if (toward->y > 0.9f || toward->y < -0.9f)
    {
        pick.assign(1.0f, 0.0f, 0.0f);
    }

    xVec3 u;
    xVec3 v;

    xVec3Cross(&u, &pick, toward);

    if (!Normalize(&u, &u))
    {
        return FALSE;
    }

    xVec3Cross(&v, toward, &u);

    if (!Normalize(&v, &v))
    {
        return FALSE;
    }

    g->u = u;
    g->v = v;

    F32 lo[2] = { 1e30f, 1e30f };
    F32 hi[2] = { -1e30f, -1e30f };

    for (S32 t = 0; t < sOccTriCount; t++)
    {
        for (S32 c = 0; c < 3; c++)
        {
            const xVec3* p = &sOccTris[t].v[c];
            F32 a[2];

            a[0] = p->x * u.x + p->y * u.y + p->z * u.z;
            a[1] = p->x * v.x + p->y * v.y + p->z * v.z;

            for (S32 k = 0; k < 2; k++)
            {
                if (a[k] < lo[k]) lo[k] = a[k];
                if (a[k] > hi[k]) hi[k] = a[k];
            }
        }
    }

    for (S32 k = 0; k < 2; k++)
    {
        g->lo[k] = lo[k];
        g->span[k] = hi[k] - lo[k];

        if (g->span[k] < 1e-4f)
        {
            g->span[k] = 1e-4f;
        }
    }

    S32 cells = kSunGrid * kSunGrid;

    g->start = (S32*)RwMalloc((cells + 1) * sizeof(S32));
    g->wide = (S32*)RwMalloc(sOccTriCount * sizeof(S32));

    if (g->start == NULL || g->wide == NULL)
    {
        SunGridFree(g);
        return FALSE;
    }

    memset(g->start, 0, (cells + 1) * sizeof(S32));

    // Count, then place. Two walks and one allocation of exactly the right size.
    for (S32 pass = 0; pass < 2; pass++)
    {
        if (pass == 1)
        {
            // **Counts into offsets, and the accumulation runs one way only.**
            //
            // Pass 0 put each cell's count at start[cell + 1], so adding each
            // entry to the one before it leaves start[c] holding where cell c
            // begins and start[cells] holding the total. An exclusive scan --
            // which is the other way to write this and what was here first --
            // leaves every offset one cell's count short, so the ranges overlap
            // and the last cell's fill runs off the end of item[].
            for (S32 c = 1; c <= cells; c++)
            {
                g->start[c] += g->start[c - 1];
            }

            S32 total = g->start[cells];

            g->item = (S32*)RwMalloc((total > 0 ? total : 1) * sizeof(S32));

            if (g->item == NULL)
            {
                SunGridFree(g);
                return FALSE;
            }

            g->numWide = 0;
        }

        for (S32 t = 0; t < sOccTriCount; t++)
        {
            S32 c0[2];
            S32 c1[2];

            SunGridCell(g, &sOccTris[t].v[0], c0);
            c1[0] = c0[0];
            c1[1] = c0[1];

            for (S32 k = 1; k < 3; k++)
            {
                S32 c[2];

                SunGridCell(g, &sOccTris[t].v[k], c);

                for (S32 d = 0; d < 2; d++)
                {
                    if (c[d] < c0[d]) c0[d] = c[d];
                    if (c[d] > c1[d]) c1[d] = c[d];
                }
            }

            if ((c1[0] - c0[0] + 1) * (c1[1] - c0[1] + 1) > kSunSpread)
            {
                if (pass == 1)
                {
                    g->wide[g->numWide] = t;
                }

                g->numWide++;
                continue;
            }

            for (S32 y = c0[1]; y <= c1[1]; y++)
            {
                for (S32 x = c0[0]; x <= c1[0]; x++)
                {
                    S32 cell = y * kSunGrid + x;

                    if (pass == 0)
                    {
                        g->start[cell + 1]++;
                    }
                    else
                    {
                        g->item[g->start[cell]++] = t;
                    }
                }
            }
        }

        if (pass == 1)
        {
            // start[] walked forward as it filled, so put it back.
            for (S32 c = cells; c > 0; c--)
            {
                g->start[c] = g->start[c - 1];
            }

            g->start[0] = 0;
        }
    }

    return TRUE;
}

static S32 SunGridOccluded(const SunGrid* g, const xVec3* from, const xVec3* toward, F32 reach)
{
    for (S32 i = 0; i < g->numWide; i++)
    {
        if (RayHitsTri(from, toward, reach, &sOccTris[g->wide[i]]))
        {
            return TRUE;
        }
    }

    S32 c[2];

    SunGridCell(g, from, c);

    S32 cell = c[1] * kSunGrid + c[0];

    for (S32 i = g->start[cell]; i < g->start[cell + 1]; i++)
    {
        if (RayHitsTri(from, toward, reach, &sOccTris[g->item[i]]))
        {
            return TRUE;
        }
    }

    return FALSE;
}

void iEnvSunShadeClear()
{
    if (sSunShade != NULL)
    {
        RwFree(sSunShade);
        sSunShade = NULL;
    }

    sSunShadeVerts = 0;
}

const U8* iEnvSunShade(S32* verts, S32* steps)
{
    if (verts != NULL)
    {
        *verts = sSunShadeVerts;
    }
    if (steps != NULL)
    {
        *steps = kSunSteps;
    }

    return sSunShade;
}

// **Put the day's traced shade into the world, at the time it is now.**
//
// The trace above left kSunSteps answers per vertex; this picks the two the
// clock stands between and blends them into the prelight, which is where the cel
// pixel shader reads it -- see ToonModelShade. On that path the vertex shader
// adds no lighting, so the prelight carries this and nothing else.
//
// **Not every frame.** The write itself is nothing, but it has to be locked, and
// a lock means the renderer copies every world colour into its vertex buffers
// again. The sun crosses a step in twenty-five seconds of a five-minute day, so
// a write every fiftieth of a step is smooth and costs about four a second.
static F32 sShadeWrittenAt = -1.0f;

static const F32 kShadeStale = 1.0f / (F32)(kSunSteps * 50);

void iEnvSunShadeApply(iEnv* env, F32 phase, S32 force)
{
    if (sSunShade == NULL || env == NULL || env->jsp == NULL || env->jsp->clump == NULL)
    {
        return;
    }

    if (!force && sShadeWrittenAt >= 0.0f)
    {
        F32 moved = phase - sShadeWrittenAt;

        if (moved < 0.0f)
        {
            moved = -moved;
        }

        // Across the end of the day the difference is nearly a whole turn.
        if (moved > 0.5f)
        {
            moved = 1.0f - moved;
        }

        if (moved < kShadeStale)
        {
            return;
        }
    }

    NormalWork w;

    if (!WorkBuild(&w, env->jsp->clump))
    {
        return;
    }

    if (w.totalVerts != sSunShadeVerts)
    {
        WorkFree(&w);
        return;
    }

    F32 at = phase * (F32)kSunSteps;
    S32 step = (S32)at;
    F32 into = at - (F32)step;

    step = step % kSunSteps;

    if (step < 0)
    {
        step += kSunSteps;
    }

    S32 next = (step + 1) % kSunSteps;

    for (S32 a = 0; a < w.numAtomics; a++)
    {
        RpGeometry* geo = RpAtomicGetGeometry(w.atomics[a]);

        // Only the pieces the run-time rig lights. A piece that kept its
        // painting had rpGEOMETRYLIGHT cleared and its prelight IS the artwork,
        // so writing shade into it would paint over a decal.
        if (geo == NULL || geo->preLitLum == NULL || !(geo->flags & rpGEOMETRYLIGHT))
        {
            continue;
        }

        RpGeometryLock(geo, 0x8);

        for (S32 i = 0; i < geo->numVertices; i++)
        {
            const U8* got = &sSunShade[(size_t)(w.base[a] + i) * kSunSteps];
            F32 blended = (F32)got[step] + ((F32)got[next] - (F32)got[step]) * into;
            U8 v = (U8)(blended < 0.0f ? 0.0f : (blended > 255.0f ? 255.0f : blended));

            geo->preLitLum[i].red = v;
            geo->preLitLum[i].green = v;
            geo->preLitLum[i].blue = v;

            // Opaque, always. A prelight alpha under 255 is what the pipeline
            // reads to turn alpha blending on, and the world is not see-through.
            geo->preLitLum[i].alpha = 255;
        }

        RpGeometryUnlock(geo);
    }

    sShadeWrittenAt = phase;
    WorkFree(&w);
}

void iEnvSunShadeBake(iEnv* env)
{
    iEnvSunShadeClear();

    if (env == NULL || env->jsp == NULL || env->jsp->clump == NULL || !env->baked.valid ||
        sOccTriCount == 0)
    {
        return;
    }

    NormalWork w;

    if (!WorkBuild(&w, env->jsp->clump))
    {
        return;
    }

    sSunShade = (U8*)RwMalloc((size_t)w.totalVerts * kSunSteps);

    if (sSunShade == NULL)
    {
        WorkFree(&w);
        return;
    }

    memset(sSunShade, 255, (size_t)w.totalVerts * kSunSteps);
    sSunShadeVerts = w.totalVerts;

    // How far a ray has to travel to leave the level, and how far off the
    // surface it starts so a vertex does not shadow itself.
    xVec3 lo = w.pos[0];
    xVec3 hi = w.pos[0];

    for (S32 i = 1; i < w.totalVerts; i++)
    {
        if (w.pos[i].x < lo.x) lo.x = w.pos[i].x;
        if (w.pos[i].y < lo.y) lo.y = w.pos[i].y;
        if (w.pos[i].z < lo.z) lo.z = w.pos[i].z;
        if (w.pos[i].x > hi.x) hi.x = w.pos[i].x;
        if (w.pos[i].y > hi.y) hi.y = w.pos[i].y;
        if (w.pos[i].z > hi.z) hi.z = w.pos[i].z;
    }

    F32 dx = hi.x - lo.x;
    F32 dy = hi.y - lo.y;
    F32 dz = hi.z - lo.z;
    F32 reach = xsqrt(dx * dx + dy * dy + dz * dz);

    if (reach < 1.0f)
    {
        reach = 1.0f;
    }

    const F32 kLift = 0.1f;
    S32 shadowed = 0;
    S32 traced = 0;
    clock_t began = clock();

    for (S32 s = 0; s < kSunSteps; s++)
    {
        xVec3 toward;

        iDayNightSunToward(&env->baked, (F32)s / (F32)kSunSteps, &toward);

        SunGrid grid;

        if (!SunGridBuild(&grid, &toward))
        {
            continue;
        }

        for (S32 a = 0; a < w.numAtomics; a++)
        {
            RpGeometry* geo = RpAtomicGetGeometry(w.atomics[a]);

            if (!Usable(geo))
            {
                continue;
            }

            NormalRead nr;

            NormalReadInit(&nr, &w, a);

            for (S32 i = 0; i < geo->numVertices; i++)
            {
                S32 v = w.base[a] + i;
                xVec3 n;

                NormalReadAt(&n, &nr, i);

                // A surface facing away from the sun is dark whatever stands in
                // front of it, so there is nothing to trace.
                if (n.x * toward.x + n.y * toward.y + n.z * toward.z <= 0.0f)
                {
                    continue;
                }

                xVec3 from;

                from.assign(w.pos[v].x + n.x * kLift, w.pos[v].y + n.y * kLift,
                            w.pos[v].z + n.z * kLift);

                traced++;

                if (SunGridOccluded(&grid, &from, &toward, reach))
                {
                    sSunShade[(size_t)v * kSunSteps + s] = 0;
                    shadowed++;
                }
            }
        }

        SunGridFree(&grid);
    }

    sShadeWrittenAt = -1.0f;

    printf("bfbb: model shade traced over %d step(s) of the day -- %d of %d vertex-steps facing "
           "the sun are behind a model, %d model(s) casting (%d triangles); %.1fs\n",
           (int)kSunSteps, (int)shadowed, (int)traced, (int)sOccCount, (int)sOccTriCount,
           (double)(clock() - began) / CLOCKS_PER_SEC);
    fflush(stdout);

    WorkFree(&w);
}

static S32 sHitAnything;

static RpCollisionTriangle* ShadowRayCB(RpIntersection*, RpWorldSector*, RpCollisionTriangle* tri,
                                        F32, void*)
{
    sHitAnything = TRUE;

    // NULL ends the walk. Any hit at all is the whole answer, so there is no
    // reason to keep looking for a nearer one.
    return NULL;
}

static S32 Occluded(xClumpCollBSPTree* tree, const xVec3* from, const xVec3* toward, F32 reach)
{
    RpIntersection isx;

    isx.type = rpINTERSECTLINE;
    isx.t.line.start.x = from->x;
    isx.t.line.start.y = from->y;
    isx.t.line.start.z = from->z;
    isx.t.line.end.x = from->x + toward->x * reach;
    isx.t.line.end.y = from->y + toward->y * reach;
    isx.t.line.end.z = from->z + toward->z * reach;

    sHitAnything = FALSE;
    xClumpColl_ForAllIntersections(tree, &isx, ShadowRayCB, NULL);

    return sHitAnything;
}

void iEnvBakeShadowedLight(iEnv* env)
{
    if (env == NULL || env->jsp == NULL || env->jsp->clump == NULL ||
        env->jsp->colltree == NULL || !env->baked.valid)
    {
        return;
    }

    NormalWork w;

    if (!WorkBuild(&w, env->jsp->clump))
    {
        return;
    }

    F32 ambient[3];
    F32 color[iENV_BAKED_LIGHTS][3];

    iEnvRigAtContrast(&env->baked, iScreenWorldLightContrast(), ambient, color);

    // Toward each light, and how far a ray has to travel to leave the level.
    // The reach is the world's own diagonal: shorter misses an occluder across
    // a wide level, and longer only costs tree walking.
    xVec3 toward[iENV_BAKED_LIGHTS];

    for (S32 k = 0; k < iENV_BAKED_LIGHTS; k++)
    {
        toward[k].assign(-env->baked.dir[k].x, -env->baked.dir[k].y, -env->baked.dir[k].z);
    }

    xVec3 lo = w.pos[0];
    xVec3 hi = w.pos[0];

    for (S32 i = 1; i < w.totalVerts; i++)
    {
        if (w.pos[i].x < lo.x) lo.x = w.pos[i].x;
        if (w.pos[i].y < lo.y) lo.y = w.pos[i].y;
        if (w.pos[i].z < lo.z) lo.z = w.pos[i].z;
        if (w.pos[i].x > hi.x) hi.x = w.pos[i].x;
        if (w.pos[i].y > hi.y) hi.y = w.pos[i].y;
        if (w.pos[i].z > hi.z) hi.z = w.pos[i].z;
    }

    F32 dx = hi.x - lo.x;
    F32 dy = hi.y - lo.y;
    F32 dz = hi.z - lo.z;
    F32 reach = sqrtf(dx * dx + dy * dy + dz * dz);

    if (reach < 1.0f)
    {
        reach = 1.0f;
    }

    // Off the surface before tracing, or a vertex shadows itself on the
    // triangles it belongs to. 0.1 of a unit is around 14 cm of BFBB.
    const F32 kLift = 0.1f;

    // **Exactly one light casts, and the rest never do.**
    //
    // Only one of the fit's lights is a sun. The others come back pointing
    // sideways and from below -- bb01 fits (-0.44, -0.08, 0.90) and
    // (-0.83, -0.23, 0.51) -- because what they stand in for is bounce, not a
    // second source. The ground occludes a light arriving from under it almost
    // everywhere, so tracing them darkened two thirds of the level for no
    // physical reason. It is also four times fewer rays.
    S32 sun = SunLight(&env->baked);

    if (sun < 0)
    {
        printf("bfbb: world shadows skipped -- no light in the fit is above %.0f degrees\n",
               (double)(asinf(kSunFloor) * 180.0f / 3.14159265f));
        WorkFree(&w);
        return;
    }

    S32 done = 0;
    S32 shadowed = 0;
    S32 traced = 0;
    S32 rays = 0;
    clock_t began = clock();

    for (S32 a = 0; a < w.numAtomics; a++)
    {
        RpAtomic* atomic = w.atomics[a];
        RpGeometry* geo = RpAtomicGetGeometry(atomic);

        if (geo == NULL || geo->preLitLum == NULL || PrelightIsArtwork(geo))
        {
            continue;
        }

        NormalRead nr;

        NormalReadInit(&nr, &w, a);

        for (S32 i = 0; i < geo->numVertices; i++)
        {
            S32 v = w.base[a] + i;
            xVec3 n;

            NormalReadAt(&n, &nr, i);

            xVec3 from;

            from.assign(w.pos[v].x + n.x * kLift, w.pos[v].y + n.y * kLift,
                        w.pos[v].z + n.z * kLift);

            F32 lit[3] = { ambient[0], ambient[1], ambient[2] };

            for (S32 k = 0; k < env->baked.count; k++)
            {
                F32 ndl = n.x * toward[k].x + n.y * toward[k].y + n.z * toward[k].z;

                // A surface facing away is already dark, so there is nothing to
                // trace and nothing to add.
                if (ndl <= 0.0f)
                {
                    continue;
                }

                if (k == sun)
                {
                    traced++;
                    rays++;

                    if (Occluded(env->jsp->colltree, &from, &toward[sun], reach))
                    {
                        shadowed++;
                        continue;
                    }
                }

                for (S32 c = 0; c < 3; c++) lit[c] += ndl * color[k][c];
            }

            RwRGBA* out = &geo->preLitLum[i];
            F32* src = lit;
            U8* dst = &out->red;

            for (S32 c = 0; c < 3; c++)
            {
                F32 f = src[c] < 0.0f ? 0.0f : (src[c] > 1.0f ? 1.0f : src[c]);

                dst[c] = (U8)(f * 255.0f + 0.5f);
            }

            out->alpha = 255;
        }

        // The colour above IS the lighting, so nothing may light it again.
        geo->flags &= ~rpGEOMETRYLIGHT;
        done++;
    }

    printf("bfbb: world shadows traced from light %d (%.0f degrees up) -- %d of %d vertices "
           "facing it are in its shadow, %d rays over %d pieces; %.1fs\n",
           (int)sun, (double)(asinf(-env->baked.dir[sun].y) * 180.0f / 3.14159265f),
           (int)shadowed, (int)traced, (int)rays, (int)done,
           (double)(clock() - began) / CLOCKS_PER_SEC);

    WorkFree(&w);
}

// What the level was painted at, 0 to 1, or 0 where nothing measured it.
static F32 sPaintLevel;

F32 iEnvPaintLevel()
{
    return sPaintLevel;
}

void iEnvDropPrelight(iEnv* env)
{
    if (env == NULL || env->jsp == NULL || env->jsp->clump == NULL)
    {
        return;
    }

    NormalWork w;

    if (!WorkBuild(&w, env->jsp->clump))
    {
        return;
    }

    S32 kept = 0;
    S32 keptVerts = 0;

    // **How bright the artists painted this level, which is the only record of
    // it.** The cel path asks the lights how bright a room is and they cannot
    // say: four lights pointing four ways sum past one whether or not any
    // surface sees more than one of them, so a house indoors resolves as bright
    // as open sunlight. hb01 measures 0.578 here and the inside of SpongeBob's
    // house 0.487 -- the room IS darker, and only the paint knows it.
    //
    // Measured over the pieces about to lose their paint, so it is the light the
    // paint recorded and not the artwork PrelightIsArtwork keeps.
    double paintSum = 0.0;
    S32 paintCount = 0;

    for (S32 a = 0; a < w.numAtomics; a++)
    {
        RpGeometry* geo = RpAtomicGetGeometry(w.atomics[a]);

        if (geo == NULL)
        {
            continue;
        }

        if (PrelightIsArtwork(geo))
        {
            // Left exactly as the console drew it. The world's geometry
            // arrives with LIGHT already set and only NORMALS missing, so
            // clearing LIGHT is what keeps a piece out of the run-time rig --
            // and it has to be cleared rather than merely skipped, because
            // the normals added above would otherwise switch it on.
            geo->flags &= ~rpGEOMETRYLIGHT;

            kept++;
            keptVerts += geo->numVertices;
            continue;
        }

        if (geo->preLitLum != NULL)
        {
            for (S32 i = 0; i < geo->numVertices; i++)
            {
                RwRGBA* c = &geo->preLitLum[i];

                paintSum += 0.299 * c->red + 0.587 * c->green + 0.114 * c->blue;
                paintCount++;
            }
        }

        // **Kept, when it is carrying the model shade.** The cel path reads a
        // prelight as a scale on its light term rather than adding it, which is
        // what iEnvSunShadeApply writes there. Dropped otherwise, because on
        // every other path a prelight is light and this one is not.
        if (iScreenWorldModelShade() <= 0.0f)
        {
            geo->flags &= ~rpGEOMETRYPRELIT;
        }
    }

    sPaintLevel = paintCount != 0 ? (F32)(paintSum / paintCount / 255.0) : 0.0f;

    if (kept != 0)
    {
        printf("bfbb: world painting kept on %d of %d pieces, %d vertices\n", (int)kept,
               (int)w.numAtomics, (int)keptVerts);
    }

    WorkFree(&w);
}

void iEnvGenerateNormals(iEnv* env)
{
    if (env == NULL || env->jsp == NULL || env->jsp->clump == NULL)
    {
        return;
    }

    NormalWork w;

    if (!WorkBuild(&w, env->jsp->clump))
    {
        return;
    }

    // The rig fitted before the geometry was rebuilt wins, because it was
    // fitted from the paint and the topology the artists authored together.
    // Without hipoly there is no rebuild and the two are the same work on the
    // same mesh, so this is simply the one that already ran.
    if (!TakeShippedRig(env->jsp->clump, &env->baked))
    {
        RecoverBakedLight(&w, &env->baked);
    }

    if (HasNormals(&w))
    {
        WorkFree(&w);
        return;
    }

    // One block for the whole clump, sliced out per atomic.
    //
    // Not Geometry::allocateData, which is the obvious call and the wrong one:
    // it allocates a FRESH block for the triangles, colours and texture
    // coordinates and overwrites the pointers, so the level's artwork is lost
    // and leaked in the same breath, and every triangle's material id is reset.
    // Only the morph target's normals pointer needs to move.
    RwV3d* block = (RwV3d*)RwMalloc(w.totalVerts * sizeof(RwV3d));

    if (block == NULL)
    {
        WorkFree(&w);
        return;
    }

    env->genNormals = block;

    for (S32 a = 0; a < w.numAtomics; a++)
    {
        RpGeometry* geo = RpAtomicGetGeometry(w.atomics[a]);

        if (!Usable(geo))
        {
            continue;
        }

        RwFrame* frame = RpAtomicGetFrame(w.atomics[a]);
        RwMatrix* ltm = frame ? RwFrameGetLTM(frame) : NULL;
        xVec3* dst = (xVec3*)&block[w.base[a]];

        for (S32 i = 0; i < geo->numVertices; i++)
        {
            ReadNormal(&dst[i], &w, w.base[a] + i, ltm);
        }

        geo->morphTarget[0].normals = (RwV3d*)dst;
        geo->flags |= rpGEOMETRYNORMALS;
    }

    printf("bfbb: world normals generated -- %d vertices across %d pieces\n", (int)w.totalVerts,
           (int)w.numAtomics);
    fflush(stdout);

    WorkFree(&w);
}

// A level's props ship the way its world does: baked vertex colour, and no
// normals. 405 of bb01's 426 model geometries carry PRELIT without NORMALS.
//
// **A geometry with no normals is not merely unlit, it is pinned.** librw fills
// a missing normal from a constant vertex stream holding (0,0,0), so every
// light gives a dot product of zero and the cel ramp is read at its dark end
// over the whole model. Nothing a light or a ramp does can move it, and what is
// left on screen is the paint. That is why a crater standing in relit sand
// keeps the tan it was baked with while the sand around it does not.
//
// So a prop is given what the world is given: generated normals, and its paint
// dropped where the paint is lighting rather than artwork. The kit that lights
// it afterwards is the one zScene builds from the same rig the world uses, so
// the two land in the same place by construction.
//
// Same constraint as the world's, for the same reason: before the atomic is
// instanced, because the vertex buffer is built from the flags the geometry
// carries at that moment.

// Generated normals belong to the clump and are freed with it. One block per
// clump, sliced per atomic, the way the world's is.
struct ModelNormals
{
    RpClump* clump;
    RwV3d* block;
};

static ModelNormals* sModelNormals;
static S32 sModelNormalCount;
static S32 sModelNormalMax;

static S32 KeepModelNormals(RpClump* clump, RwV3d* block)
{
    if (sModelNormalCount == sModelNormalMax)
    {
        S32 want = sModelNormalMax ? sModelNormalMax * 2 : 64;
        ModelNormals* grown = (ModelNormals*)RwMalloc(want * sizeof(ModelNormals));

        if (grown == NULL)
        {
            return FALSE;
        }

        if (sModelNormals != NULL)
        {
            memcpy(grown, sModelNormals, sModelNormalCount * sizeof(ModelNormals));
            RwFree(sModelNormals);
        }

        sModelNormals = grown;
        sModelNormalMax = want;
    }

    sModelNormals[sModelNormalCount].clump = clump;
    sModelNormals[sModelNormalCount].block = block;
    sModelNormalCount++;

    return TRUE;
}


void iEnvPrepareModel(RpClump* clump)
{
    if (clump == NULL)
    {
        return;
    }

    NormalWork w;

    if (!WorkBuild(&w, clump))
    {
        return;
    }

    // Almost every model ships normals -- 210 of the 214 geometries loaded in
    // bb01 -- so the block is allocated only if something here needs one.
    S32 want = 0;

    for (S32 a = 0; a < w.numAtomics; a++)
    {
        RpGeometry* geo = RpAtomicGetGeometry(w.atomics[a]);

        if (Usable(geo) && !(geo->flags & rpGEOMETRYNORMALS))
        {
            want = 1;
            break;
        }
    }

    RwV3d* block = NULL;

    if (want)
    {
        block = (RwV3d*)RwMalloc(w.totalVerts * sizeof(RwV3d));

        if (block != NULL && !KeepModelNormals(clump, block))
        {
            RwFree(block);
            block = NULL;
        }
    }

    S32 relit = 0;
    S32 kept = 0;

    for (S32 a = 0; a < w.numAtomics; a++)
    {
        RpGeometry* geo = RpAtomicGetGeometry(w.atomics[a]);

        if (!Usable(geo))
        {
            continue;
        }

        S32 gave = FALSE;

        if (!(geo->flags & rpGEOMETRYNORMALS) && block != NULL)
        {
            RwFrame* frame = RpAtomicGetFrame(w.atomics[a]);
            RwMatrix* ltm = frame ? RwFrameGetLTM(frame) : NULL;
            xVec3* dst = (xVec3*)&block[w.base[a]];

            for (S32 i = 0; i < geo->numVertices; i++)
            {
                ReadNormal(&dst[i], &w, w.base[a] + i, ltm);
            }

            geo->morphTarget[0].normals = (RwV3d*)dst;
            geo->flags |= rpGEOMETRYNORMALS;
            gave = TRUE;
        }

        (void)gave;

        if (geo->preLitLum == NULL || !(geo->flags & rpGEOMETRYPRELIT))
        {
            continue;
        }

        if (PrelightIsArtwork(geo))
        {
            kept++;
            continue;
        }

        // **Repainted black, not taken away: lit entirely by the light, still
        // there for alpha.**
        //
        // The vertex shader starts at the prelight and ADDS the lighting to it,
        // so any colour left in here is a floor under the light that dilutes
        // its hue. Zero is what a character carries, and is why a character
        // takes the day/night tint whole. Dropping rpGEOMETRYPRELIT reaches the
        // same colour but loses the alpha with the stream, so the flag stays
        // and the colours change instead. The surfaceProps are left as authored,
        // also as a character's are; kDecalPrelight says why scaling them fails.
        for (S32 i = 0; i < geo->numVertices; i++)
        {
            geo->preLitLum[i].red = kDecalPrelight;
            geo->preLitLum[i].green = kDecalPrelight;
            geo->preLitLum[i].blue = kDecalPrelight;
        }

        relit++;
    }

    WorkFree(&w);
}

// **A model the level says is never lit, that is really a piece of the ground.**
//
// A level's PIPT gives each model a set of pipe flags, and bits 0xC0 == 0x40
// means "self-coloured, do not light". The class is mostly right: the sky
// domes, the fountain water, the caustics, the shiny pickups and the floating
// numbers all carry it and all supply their own colour.
//
// The craters carry it too, and they are not that. crater_sand is a decal
// splatted round the foot of the rocket, baked to 220/182/167 against ground
// baked to 238/199/184, with an alpha-faded rim to blend into it. It is the
// world, authored as a model. On the console both sides drew their bake and
// matched. Here the world's bake is replaced by a run-time rig and the crater's
// is not, so it stops matching the only thing it was ever meant to match.
//
// The flag is enforced in xModelBucket right before the draw -- the bucket
// enables a NULL kit for a 0x40 model, throwing away whatever the caller set --
// so nothing done at the entity loop can reach it. It has to come off the model.
//
// Kept as a list of asset ids rather than a test on the geometry, because the
// rest of the 0x40 class looks the same from the inside: the fountain water and
// the caustics are also flat, also face up, and must stay unlit.
static const U32 kGroundDecals[] = {
    0x2273B988,  // hb01 crater_sand, the 19 craters, one of them the rocket's
    0xF151B4EF,  // hb01 crater_sand_LOD1, what LODT swaps it for past 200 units
};

static RpAtomic** sDecalAtomics;
static S32 sDecalCount;
static S32 sDecalMax;

static RpAtomic* MarkDecalCB(RpAtomic* atomic, void* data)
{
    (void)data;

    if (sDecalCount == sDecalMax)
    {
        S32 want = sDecalMax ? sDecalMax * 2 : 32;
        RpAtomic** grown = (RpAtomic**)RwMalloc(want * sizeof(RpAtomic*));

        if (grown == NULL)
        {
            return atomic;
        }

        if (sDecalAtomics != NULL)
        {
            memcpy(grown, sDecalAtomics, sDecalCount * sizeof(RpAtomic*));
            RwFree(sDecalAtomics);
        }

        sDecalAtomics = grown;
        sDecalMax = want;
    }

    sDecalAtomics[sDecalCount] = atomic;
    sDecalCount++;

    return atomic;
}

// **The colours have to go before the clump is instanced.**
//
// A vertex buffer is built from the geometry as it stands at instance time, so
// a colour written afterwards is one the buffer never sees. The asset id says
// which models these are and only zAssetTypes knows it, but the instancing
// happens two calls below that -- so the answer is left here on the way in and
// read by iModelStreamRead at the one moment it can still be acted on.
static S32 sPendingGroundDecal;

void iEnvPendingGroundDecal(S32 on)
{
    sPendingGroundDecal = on;
}

S32 iEnvTakePendingGroundDecal(void)
{
    return sPendingGroundDecal;
}

S32 iEnvGroundDecalAsset(U32 assetID)
{
    for (U32 i = 0; i < sizeof(kGroundDecals) / sizeof(kGroundDecals[0]); i++)
    {
        if (kGroundDecals[i] == assetID)
        {
            return TRUE;
        }
    }

    return FALSE;
}

void iEnvMarkGroundDecal(RpClump* clump)
{
    if (clump != NULL)
    {
        RpClumpForAllAtomics(clump, MarkDecalCB, NULL);
    }
}

S32 iEnvIsGroundDecal(void* atomic)
{
    for (S32 i = 0; i < sDecalCount; i++)
    {
        if (sDecalAtomics[i] == (RpAtomic*)atomic)
        {
            return TRUE;
        }
    }

    return FALSE;
}

void iEnvForgetModel(RpClump* clump)
{
    for (S32 i = 0; i < sModelNormalCount; i++)
    {
        if (sModelNormals[i].clump != clump)
        {
            continue;
        }

        RwFree(sModelNormals[i].block);
        sModelNormals[i] = sModelNormals[sModelNormalCount - 1];
        sModelNormalCount--;
        return;
    }
}

void iEnvNormalsCompare(iEnv* env)
{
    if (env == NULL || env->jsp == NULL || env->jsp->clump == NULL)
    {
        return;
    }

    NormalWork w;

    if (!WorkBuild(&w, env->jsp->clump))
    {
        return;
    }

    if (!HasNormals(&w))
    {
        WorkFree(&w);
        return;
    }

    S32 within[4] = { 0, 0, 0, 0 };
    S32 total = 0;
    S32 flipped = 0;
    F32 sumDot = 0.0f;

    for (S32 a = 0; a < w.numAtomics; a++)
    {
        RpGeometry* geo = RpAtomicGetGeometry(w.atomics[a]);

        if (!Usable(geo) || geo->morphTarget[0].normals == NULL)
        {
            continue;
        }

        RwFrame* frame = RpAtomicGetFrame(w.atomics[a]);
        RwMatrix* ltm = frame ? RwFrameGetLTM(frame) : NULL;
        const xVec3* shipped = (const xVec3*)geo->morphTarget[0].normals;

        for (S32 i = 0; i < geo->numVertices; i++)
        {
            xVec3 mine;

            ReadNormal(&mine, &w, w.base[a] + i, ltm);

            F32 d = mine.x * shipped[i].x + mine.y * shipped[i].y + mine.z * shipped[i].z;

            if (d > 1.0f) d = 1.0f;
            if (d < -1.0f) d = -1.0f;

            total++;
            sumDot += d;

            if (d < 0.0f) flipped++;
            if (d > 0.99619f) within[0]++;  // 5 degrees
            if (d > 0.98481f) within[1]++;  // 10 degrees
            if (d > 0.86603f) within[2]++;  // 30 degrees
            if (d > 0.5f) within[3]++;      // 60 degrees
        }
    }

    if (total > 0)
    {
        F32 pct = 100.0f / (F32)total;

        printf("bfbb: world normals check -- %d verts, mean dot %.4f, within 5deg %.1f%%, "
               "10deg %.1f%%, 30deg %.1f%%, 60deg %.1f%%, backwards %.1f%%\n",
               (int)total, sumDot / (F32)total, within[0] * pct, within[1] * pct,
               within[2] * pct, within[3] * pct, flipped * pct);
        fflush(stdout);
    }

    WorkFree(&w);
}

void iEnvFreeNormals(iEnv* env)
{
    if (env == NULL)
    {
        return;
    }

    if (env->genNormals != NULL)
    {
        RwFree(env->genNormals);
        env->genNormals = NULL;
    }
}
