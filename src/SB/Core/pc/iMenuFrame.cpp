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

    // The frames this has rebuilt, each with the mesh the artist made and the
    // widening it was last rebuilt for.
    //
    // Kept rather than rebuilt once and forgotten because the widening follows
    // video.ui, and the settings screen changes that while the frame is up:
    // the next draw has to see a different margin and build again from the
    // original. A 4:3 or pillarboxed screen gets the original back. The
    // original is held by a reference of its own so that swapping it out does
    // not free it.
    //
    // Other passes replace a drawn atomic's geometry too -- a copy of whatever
    // this built can be what the atomic holds on the next draw -- so an entry
    // is found by its atomic and nothing else, and is dropped when the model
    // that owns the atomic is unloaded (iMenuFrameForget). That is what stops
    // a new atomic made where an old one was from inheriting its entry.
    struct Seen
    {
        RpAtomic* atomic;
        RpGeometry* original;
        RpGeometry* built;
        // The widening it was built for, in thousandths of object space; -1
        // before the first build.
        int widen;
        bool rope;
    };

    const int kMaxSeen = 8;
    Seen sSeen[kMaxSeen];
    int sSeenCount = 0;

    Seen* find_seen(RpAtomic* atomic)
    {
        for (int i = 0; i < sSeenCount; i++)
        {
            if (sSeen[i].atomic == atomic)
            {
                return &sSeen[i];
            }
        }
        return NULL;
    }

    Seen* add_seen(RpAtomic* atomic, RpGeometry* original)
    {
        Seen* slot = NULL;
        for (int i = 0; i < sSeenCount && slot == NULL; i++)
        {
            if (sSeen[i].atomic == NULL)
            {
                slot = &sSeen[i];
            }
        }
        if (slot == NULL)
        {
            if (sSeenCount == kMaxSeen)
            {
                return NULL;
            }
            slot = &sSeen[sSeenCount++];
        }

        // A reference of its own, so the rebuilds can always start again from
        // the artist's mesh; given back in iMenuFrameForget.
        original->refCount++;
        slot->atomic = atomic;
        slot->original = original;
        slot->built = NULL;
        slot->widen = -1;
        slot->rope = false;
        return slot;
    }

    RpAtomic* ForgetCB(RpAtomic* atomic, void*)
    {
        for (int i = 0; i < sSeenCount; i++)
        {
            Seen& e = sSeen[i];
            if (e.atomic == atomic)
            {
                RpGeometryDestroy(e.original);
                e.atomic = NULL;
                e.original = NULL;
                e.built = NULL;
            }
        }
        return atomic;
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

        // One quad, moved to start at x0, `sx` times as long, and otherwise
        // exactly as drawn.
        //
        // This is a TRANSLATION, and it has to be. The two end caps do not
        // share a vertex order -- the artist turned the right-hand one 180
        // degrees for the opposite corner, so its first vertex is the far top
        // one where the left cap's is the near bottom one. Writing positions
        // into fixed slots therefore mirrors one cap and leaves the other
        // alone. Moving every vertex by one offset cannot.
        void quad(const RwV3d* srcPos, const RwTexCoords* srcUV, float x0, float z,
                  float sx = 1.0f)
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
                verts[vert + i].x = x0 + (srcPos[i].x - minX) * sx;
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

namespace
{
    // The frame as the artist built it, by its signature -- and the reason this
    // needs no hardcoded asset id: eighty vertices, and the four middle quads of
    // a rail are one tile repeated, so they share a texture rectangle exactly.
    // A mesh of this size whose tiles do not repeat is some other model that
    // happens to have eighty vertices, and it is left alone.
    bool is_artist_frame(const RpGeometry* g)
    {
        if (g == NULL || g->numVertices != kSrcVerts || g->numTriangles != kSrcTris ||
            g->numMorphTargets == 0 || g->numTexCoordSets == 0 || g->matList.numMaterials == 0)
        {
            return false;
        }

        const RwTexCoords* su = g->texCoords[0];
        if (g->morphTarget[0].verts == NULL || su == NULL)
        {
            return false;
        }

        for (int i = 1; i < kRailTiles; i++)
        {
            for (int k = 0; k < 4; k++)
            {
                if (su[kTopTiles + i * 4 + k].u != su[kTopTiles + k].u ||
                    su[kTopTiles + i * 4 + k].v != su[kTopTiles + k].v)
                {
                    return false;
                }
            }
        }
        return true;
    }
} // namespace

int iMenuFrameWiden(RpAtomic* atomic, float rectWidth)
{
    if (atomic == NULL || rectWidth <= 0.0f)
    {
        return 0;
    }


    Seen* seen = find_seen(atomic);
    RpGeometry* src = seen != NULL ? seen->original : atomic->geometry;
    if (!is_artist_frame(src))
    {
        return 0;
    }

    const RwV3d* sp = src->morphTarget[0].verts;
    const RwTexCoords* su = src->texCoords[0];

    // One bamboo segment, measured off the mesh rather than assumed.
    const float period = sp[kTopTiles + kBR].x - sp[kTopTiles + kBL].x;
    if (period <= 0.0f)
    {
        return 0;
    }

    // How far each stile moves out, in the frame's own object space.
    //
    // The whole margin less a fixed clearance, so the stiles sit the same
    // distance from the screen edge at every width, and the HUD anchored to
    // that edge stays inside them.
    //
    // One unit of object space covers kScale * rectWidth of the box, not
    // rectWidth: the frame is drawn nearer the camera than the plane its rect
    // is measured on. Measured off 16:9 and 21:9 frames, with the stiles'
    // outer edges 0.02 of the box inside it at 4:3.
    const float kScale = 1.16f;
    const float kEdgeClear = 0.02f;
    const float margin = iScreenAnchorMarginXF();
    float shift = (margin - kEdgeClear) / (kScale * rectWidth);
    if (shift < 0.0f)
    {
        shift = 0.0f;
    }
    const int widen = (int)(shift * 1000.0f + 0.5f);

    // Zero extra segments is not a reason to stop while the corner lashings are
    // being fixed: a 4:3 or pillarboxed screen needs no extra bamboo, but it has
    // the same missing rope as every other screen. With that fix off there is
    // nothing left for the rebuild to do, and the mesh is better left alone.
    const bool ropeFix = iFixMenuRope() != 0;
    if (seen != NULL && seen->widen == widen && seen->rope == ropeFix)
    {
        return 0;
    }
    if (seen == NULL)
    {
        seen = add_seen(atomic, src);
        if (seen == NULL)
        {
            return 0;
        }
    }

    if (widen == 0 && !ropeFix)
    {
        if (atomic->geometry != seen->original)
        {
            RpAtomicSetGeometry(atomic, seen->original, 0);
        }
        seen->built = NULL;
        seen->widen = 0;
        seen->rope = false;
        return 1;
    }

    // The tiles between the caps: as many whole ones as the widened rail holds,
    // each stretched by the few percent left over, so the rail ends exactly
    // where the stiles now stand.
    const float span = kRailTiles * period + 2.0f * shift;
    int railTiles = (int)(span / period + 0.5f);
    if (railTiles < kRailTiles)
    {
        railTiles = kRailTiles;
    }
    const float step = span / railTiles;
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

    // Each rail is a cap, the tiles, and the other cap. With no widening the
    // tiles land on the grid the original ones were on.
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
                const float x = left + period + i * step;
                b.quad(&sp[tile], &su[tile], x, railZ, step / period);
            }

            const float right = left + period + span;
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

    seen->built = dst;
    seen->widen = widen;
    seen->rope = ropeFix;

    // No rpATOMICSAMEBOUNDINGSPHERE: the frame is wider than it was, and the
    // sphere it is culled against has to know.
    RpAtomicSetGeometry(atomic, dst, 0);

    // The atomic holds the new frame now. Dropping the reference it was made
    // with is what lets the next rebuild -- or the scene's teardown -- free it.
    dst->refCount--;

    printf("bfbb: menu frame rebuilt: %d segments a rail, %d quads%s\n", railTiles, quads,
           ropeFix ? ", corner lashings brought forward" : "");
    fflush(stdout);
    return 1;
}

void iMenuFrameForget(RpClump* clump)
{
    if (clump != NULL)
    {
        RpClumpForAllAtomics(clump, ForgetCB, NULL);
    }
}
