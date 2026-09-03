// The menu's bamboo frame, rebuilt wider. The argument for doing it here, and
// not by stretching a rect, is in iMenuFrame.h.

#include "iMenuFrame.h"

#include "iFixes.h"
#include "iScreen.h"

#include <rwcore.h>
#include <rpworld.h>

#include <stdio.h>

namespace
{
    // The frame as the artist built it: twenty quads, in the order the exporter
    // wrote them. Every one of these is checked before anything is rebuilt, so
    // a different mesh arriving here is left alone rather than mangled.
    const int kSrcVerts = 80;
    const int kSrcTris = 40;

    const int kTopCapL = 0;   // one quad
    const int kTopTiles = 4;  // four
    const int kTopCapR = 20;  // one
    const int kBotCapL = 24;
    const int kBotTiles = 28;
    const int kBotCapR = 44;
    const int kStileL = 48;  // four
    const int kStileR = 64;  // four

    const int kRailTiles = 4;
    const int kStileTiles = 4;

    // Which atomics have already been through this.
    //
    // The count-based test is not enough on its own: on a 4:3 or pillarboxed
    // screen the rebuild adds no segments, so the mesh it produces has exactly
    // the eighty vertices the signature looks for and would be rebuilt again on
    // the next frame, and every frame after, leaking a geometry each time. The
    // frame appears on two menu screens, so a couple of slots is plenty.
    const int kMaxSeen = 8;
    RpAtomic* sSeen[kMaxSeen];
    int sSeenCount = 0;

    bool already_done(RpAtomic* atomic)
    {
        for (int i = 0; i < sSeenCount; i++)
        {
            if (sSeen[i] == atomic)
            {
                return true;
            }
        }

        return false;
    }

    // A quad's four vertices, as the exporter laid them out: bottom-left,
    // bottom-right, top-left, top-right.
    const int kBL = 0;
    const int kBR = 1;
    const int kTL = 2;
    const int kTR = 3;

    struct Builder
    {
        RpGeometry* geo;
        RwV3d* verts;
        RwTexCoords* uvs;
        RpTriangle* tris;
        int vert;
        int tri;

        // One quad, moved to start at x0 and otherwise exactly as drawn.
        //
        // This is a TRANSLATION, and it has to be. The two end caps do not
        // share a vertex order -- the artist turned the right-hand one 180
        // degrees for the opposite corner, so its first vertex is the far top
        // one where the left cap's is the near bottom one. Writing positions
        // into fixed slots therefore mirrors one cap and leaves the other
        // alone. Moving every vertex by one offset cannot.
        void quad(const RwV3d* srcPos, const RwTexCoords* srcUV, float x0, float z)
        {
            float minX = srcPos[0].x;
            for (int i = 1; i < 4; i++)
            {
                if (srcPos[i].x < minX)
                {
                    minX = srcPos[i].x;
                }
            }

            for (int i = 0; i < 4; i++)
            {
                verts[vert + i].x = x0 + (srcPos[i].x - minX);
                verts[vert + i].y = srcPos[i].y;
                verts[vert + i].z = z;
                uvs[vert + i] = srcUV[i];
            }

            RpGeometryTriangleSetVertexIndices(geo, &tris[tri], (RwUInt16)(vert + kBL),
                                               (RwUInt16)(vert + kBR), (RwUInt16)(vert + kTL));
            RpGeometryTriangleSetVertexIndices(geo, &tris[tri + 1], (RwUInt16)(vert + kBR),
                                               (RwUInt16)(vert + kTR), (RwUInt16)(vert + kTL));

            vert += 4;
            tri += 2;
        }

        // A quad carried over unchanged but for a shift along x -- the stiles,
        // which move outward without changing shape.
        void shifted(const RwV3d* srcPos, const RwTexCoords* srcUV, float dx, float z)
        {
            for (int i = 0; i < 4; i++)
            {
                verts[vert + i].x = srcPos[i].x + dx;
                verts[vert + i].y = srcPos[i].y;
                verts[vert + i].z = z;
                uvs[vert + i] = srcUV[i];
            }

            RpGeometryTriangleSetVertexIndices(geo, &tris[tri], (RwUInt16)(vert + kBL),
                                               (RwUInt16)(vert + kBR), (RwUInt16)(vert + kTL));
            RpGeometryTriangleSetVertexIndices(geo, &tris[tri + 1], (RwUInt16)(vert + kBR),
                                               (RwUInt16)(vert + kTR), (RwUInt16)(vert + kTL));

            vert += 4;
            tri += 2;
        }
    };
}

int iMenuFrameWiden(RpAtomic* atomic, float rectWidth)
{
    if (atomic == NULL || rectWidth <= 0.0f)
    {
        return 0;
    }

    if (already_done(atomic))
    {
        return 0;
    }

    RpGeometry* src = atomic->geometry;
    if (src == NULL || src->numVertices != kSrcVerts || src->numTriangles != kSrcTris ||
        src->numMorphTargets == 0 || src->numTexCoordSets == 0)
    {
        return 0;
    }

    const RwV3d* sp = src->morphTarget[0].verts;
    const RwTexCoords* su = src->texCoords[0];
    if (sp == NULL || su == NULL || src->matList.numMaterials == 0)
    {
        return 0;
    }

    // The signature, and the reason this needs no hardcoded asset id: in this
    // frame the four middle quads of a rail are one tile repeated, so they
    // share a texture rectangle exactly. A mesh of this size whose tiles do not
    // repeat is some other model that happens to have eighty vertices, and it
    // is left alone.
    for (int i = 1; i < kRailTiles; i++)
    {
        for (int k = 0; k < 4; k++)
        {
            if (su[kTopTiles + i * 4 + k].u != su[kTopTiles + k].u ||
                su[kTopTiles + i * 4 + k].v != su[kTopTiles + k].v)
            {
                return 0;
            }
        }
    }

    // One bamboo segment, measured off the mesh rather than assumed.
    const float period = sp[kTopTiles + kBR].x - sp[kTopTiles + kBL].x;
    if (period <= 0.0f)
    {
        return 0;
    }

    // How much of the frame's own object space one screen margin is worth. The
    // model spans rectWidth of the screen for every 1.0 of object space, so the
    // margin divides straight through.
    const float margin = iScreenAnchorMarginXF();
    int extra = (int)(margin / (period * rectWidth) + 0.5f);
    if (extra < 0)
    {
        extra = 0;
    }

    // Zero extra segments is not a reason to stop while the corner lashings are
    // being fixed: a 4:3 or pillarboxed screen needs no extra bamboo, but it has
    // the same missing rope as every other screen. With that fix off there is
    // nothing left for the rebuild to do, and the mesh is better left alone.
    const bool ropeFix = iFixMenuRope() != 0;
    if (extra == 0 && !ropeFix)
    {
        return 0;
    }

    const int railTiles = kRailTiles + 2 * extra;
    const int quads = 2 * (2 + railTiles) + 2 * kStileTiles;

    RpGeometry* dst = RpGeometryCreate(quads * 4, quads * 2,
                                       src->flags | ((RwUInt32)src->numTexCoordSets << 16));
    if (dst == NULL)
    {
        return 0;
    }

    // Everything: positions, texture coordinates and the polygons themselves.
    // librw spells it Geometry::LOCKALL and throws the mesh away when the
    // polygons are locked, which is exactly what wants to happen here -- the
    // index buffer is about to stop describing this geometry.
    RpGeometryLock(dst, 0x0fff);

    Builder b;
    b.geo = dst;
    b.verts = dst->morphTarget[0].verts;
    b.uvs = dst->texCoords[0];
    b.tris = dst->triangles;
    b.vert = 0;
    b.tri = 0;

    const float shift = extra * period;

    // The two depths the frame is drawn on. Retail puts the stiles on the nearer
    // one AND draws them second, so they beat the rails twice over: at each
    // corner the stile covers the rail's end cap, and the end cap is where the
    // rope lashing is painted. The fix hands each plane to the other group and
    // lays the stiles down first, so the lashing lands on top, which is what the
    // texture was drawn for. These are the art's own numbers rather than an
    // invented offset, and the shear at this scale moves a quad by well under a
    // pixel for the difference between them.
    const float nearZ = sp[kStileL + kBL].z;
    const float farZ = sp[kTopCapL + kBL].z;

    const float stileZ = ropeFix ? farZ : nearZ;
    const float railZ = ropeFix ? nearZ : farZ;

    // Each stile moves outward without changing shape.
    const auto stiles = [&]() {
        for (int i = 0; i < kStileTiles; i++)
        {
            b.shifted(&sp[kStileL + i * 4], &su[kStileL + i * 4], -shift, stileZ);
        }

        for (int i = 0; i < kStileTiles; i++)
        {
            b.shifted(&sp[kStileR + i * 4], &su[kStileR + i * 4], shift, stileZ);
        }
    };

    // Each rail is a cap, the tiles, and the other cap. The tiles are laid on
    // the same grid the original ones were, so a widened rail is
    // indistinguishable from the one the artist drew except for being longer.
    const auto rails = [&]() {
        const int caps[2][3] = { { kTopCapL, kTopTiles, kTopCapR },
                                 { kBotCapL, kBotTiles, kBotCapR } };

        for (int rail = 0; rail < 2; rail++)
        {
            const int capL = caps[rail][0];
            const int tile = caps[rail][1];
            const int capR = caps[rail][2];

            const float left = sp[capL + kBL].x - shift;

            b.quad(&sp[capL], &su[capL], left, railZ);

            for (int i = 0; i < railTiles; i++)
            {
                const float x = left + (i + 1) * period;
                b.quad(&sp[tile], &su[tile], x, railZ);
            }

            const float right = left + (railTiles + 1) * period;
            b.quad(&sp[capR], &su[capR], right, railZ);
        }
    };

    if (ropeFix)
    {
        stiles();
        rails();
    }
    else
    {
        rails();
        stiles();
    }

    // Colours, if the format asked for them: white, because the frame is lit by
    // its texture alone and an uninitialised buffer is not.
    if (dst->preLitLum != NULL)
    {
        for (int i = 0; i < quads * 4; i++)
        {
            dst->preLitLum[i].red = 0xFF;
            dst->preLitLum[i].green = 0xFF;
            dst->preLitLum[i].blue = 0xFF;
            dst->preLitLum[i].alpha = 0xFF;
        }
    }

    RpMaterial* material = src->matList.materials[0];
    for (int i = 0; i < quads * 2; i++)
    {
        RpGeometryTriangleSetMaterial(dst, &dst->triangles[i], material);
    }

    RpGeometryUnlock(dst);

    if (sSeenCount < kMaxSeen)
    {
        sSeen[sSeenCount++] = atomic;
    }

    // No rpATOMICSAMEBOUNDINGSPHERE: the frame is wider than it was, and the
    // sphere it is culled against has to know.
    RpAtomicSetGeometry(atomic, dst, 0);

    printf("bfbb: menu frame rebuilt: %d extra segment(s) each side, %d quads%s\n", extra, quads,
           ropeFix ? ", corner lashings brought forward" : "");
    fflush(stdout);
    return 1;
}
