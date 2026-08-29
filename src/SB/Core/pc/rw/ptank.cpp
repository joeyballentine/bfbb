// RenderWare C API: RpPTank, the particle tank plugin.
//
// librw has no PTank at all -- not a stub, not a header, nothing -- so unlike
// every other file in this directory none of this forwards. It is written out.
//
// What a PTank is: an RpAtomic carrying an array of particles instead of a
// model, plus a geometry big enough to hold four billboard vertices per
// particle. The game writes particle data through RpPTankAtomicLock, which
// hands back a base pointer and a stride into one "cluster" -- positions,
// colours, sizes, texture coordinates -- and the plugin turns those clusters
// into billboard vertices at render time.
//
// This file implements the four calls the game makes into that plugin, the
// attach, and the instancing -- turning the clusters into billboard vertices,
// down in ptankRenderCB.
//
// Instancing came later than the rest. It needs the camera's right and up
// vectors to know which way a billboard faces, and for a long time there was
// no camera to ask: RwEngineInstance->curCamera was permanently null. That
// changed when camera.cpp began publishing the camera for the length of a
// BeginUpdate/EndUpdate pair, which is what made this writable. Until then a
// ptank was correctly shaped, correctly locked and correctly unlocked, and
// drew nothing at all, because its vertices stayed at the zeros create left
// them and every triangle was degenerate. That was every particle in the
// game: the bubbles above all.
//
// Where the attach goes, as in skin.cpp and matfx.cpp: RpPTankPluginAttach is
// RenderWare's own entry point, RWAttachPlugins in iSystem.cpp calls it
// between RwEngineInit and RwEngineOpen, and Atomic::registerPlugin has to
// happen before Engine::open freezes the atomic size.

#include <rwcore.h>
#include <rpptank.h>
#include <rpworld.h>

#include "rw.h"

#include <string.h>
#include <math.h>

// The byte size of one particle's worth of each cluster, indexed by the
// RPPTANKSIZE* constants in rpptank.h. Declared extern there; this is its
// definition.
const RwInt32 datasize[] = {
    sizeof(RwV3d), // RPPTANKSIZEPOSITION
    sizeof(RwMatrix), // RPPTANKSIZEMATRIX
    sizeof(RwV3d), // RPPTANKSIZENORMAL
    sizeof(RwV2d), // RPPTANKSIZESIZE
    sizeof(RwRGBA), // RPPTANKSIZECOLOR
    4 * sizeof(RwRGBA), // RPPTANKSIZEVTXCOLOR -- one per billboard corner
    sizeof(RwReal), // RPPTANKSIZE2DROTATE
    2 * sizeof(RwTexCoords), // RPPTANKSIZEVTX2TEXCOORDS -- top-left, bottom-right
    4 * sizeof(RwTexCoords), // RPPTANKSIZEVTX4TEXCOORDS -- one per corner
};

// Where the per-atomic PTank pointer lives in an atomic's plugin block.
// xPtankPool.cpp declares this extern and reaches through it with
// RPATOMICPTANKPLUGINDATA. Negative until the plugin is attached.
RwInt32 _rpPTankAtomicDataOffset = -1;

// _rpPTankGlobalsOffset is deliberately NOT defined. RenderWare gives PTank a
// block on RwGlobals as well, and GLOBALPTANKPLUGINDATA() reads it; there is
// no such block on this side, because RwGlobals here is the shim's own static
// struct with no plugin mechanism at all (see engine_start.cpp). Leaving the
// symbol undefined makes any use of that macro a link error rather than a read
// of a wrong offset. Nothing in the game uses it.

namespace
{
    // The dataFlags bit that turns each cluster on. The two orders are
    // different on purpose and both are RenderWare's: RpPTankDataFlags counts
    // position, colour, size, matrix, normal, rotation, ... while the
    // RPPTANKSIZE* cluster indices count position, matrix, normal, size,
    // colour, ... This table is the only place the two meet.
    struct ClusterFlag
    {
        RwUInt32 flag;
        RwInt32 cluster;
    };

    const ClusterFlag sClusters[] = {
        { rpPTANKDFLAGPOSITION, RPPTANKSIZEPOSITION },
        { rpPTANKDFLAGMATRIX, RPPTANKSIZEMATRIX },
        { rpPTANKDFLAGNORMAL, RPPTANKSIZENORMAL },
        { rpPTANKDFLAGSIZE, RPPTANKSIZESIZE },
        { rpPTANKDFLAGCOLOR, RPPTANKSIZECOLOR },
        { rpPTANKDFLAGVTXCOLOR, RPPTANKSIZEVTXCOLOR },
        { rpPTANKDFLAG2DROTATE, RPPTANKSIZE2DROTATE },
        { rpPTANKDFLAGVTX2TEXCOORDS, RPPTANKSIZEVTX2TEXCOORDS },
        { rpPTANKDFLAGVTX4TEXCOORDS, RPPTANKSIZEVTX4TEXCOORDS },
    };

    const int kNumClusters = (int)(sizeof(sClusters) / sizeof(sClusters[0]));

    // The per-particle clusters, so that the constant-value flags
    // (rpPTANKDFLAGCNS*) and the layout flags (ARRAY, STRUCTURE, USECENTER)
    // are not mistaken for them.
    const RwUInt32 kPerParticleFlags = rpPTANKDFLAGPOSITION | rpPTANKDFLAGCOLOR | rpPTANKDFLAGSIZE |
                                       rpPTANKDFLAGMATRIX | rpPTANKDFLAGNORMAL |
                                       rpPTANKDFLAG2DROTATE | rpPTANKDFLAGVTXCOLOR |
                                       rpPTANKDFLAGVTX2TEXCOORDS | rpPTANKDFLAGVTX4TEXCOORDS;

    // Every cluster's data is 16-byte aligned, which is what a vector-unit
    // instancing loop would want and costs nothing to promise now.
    const RwUInt32 kAlign = 16;

    inline RwUInt8* alignUp(RwUInt8* p)
    {
        return (RwUInt8*)(((size_t)p + (kAlign - 1)) & ~(size_t)(kAlign - 1));
    }

    inline RwInt32 alignSize(RwInt32 n)
    {
        return (RwInt32)(((RwUInt32)n + (kAlign - 1)) & ~(RwUInt32)(kAlign - 1));
    }

    // Four vertices per particle indexed by RwUInt16, so this is where the
    // index type runs out. RenderWare's PTank has the same ceiling; nothing in
    // the game asks for more than a few hundred particles.
    const RwInt32 kMaxParticles = 0x10000 / 4;

    RpPTankAtomicExtPrv* extOf(RpAtomic* atomic)
    {
        if (atomic == NULL || _rpPTankAtomicDataOffset < 0)
        {
            return NULL;
        }
        return RPATOMICPTANKPLUGINDATA(atomic);
    }

    // --- the atomic plugin -------------------------------------------------

    void* createPTankAtomic(void* object, rw::int32 offset, rw::int32)
    {
        *PLUGINOFFSET(RpPTankAtomicExtPrv*, object, offset) = NULL;
        return object;
    }

    void* destroyPTankAtomic(void* object, rw::int32 offset, rw::int32)
    {
        // The buffers belong to RpPTankAtomicDestroy, which the game always
        // calls. This is the backstop for an atomic freed another way -- a
        // clone, or RpAtomicDestroy straight on a ptank.
        RpPTankAtomicExtPrv* ext = *PLUGINOFFSET(RpPTankAtomicExtPrv*, object, offset);
        if (ext != NULL)
        {
            RwFree(ext->rawdata);
            RwFree(ext);
            *PLUGINOFFSET(RpPTankAtomicExtPrv*, object, offset) = NULL;
        }
        return object;
    }

    void* copyPTankAtomic(void* dst, void* src, rw::int32 offset, rw::int32)
    {
        // A cloned ptank does NOT share the original's particle buffer, and
        // duplicating it here would need the clone's own geometry too. Nothing
        // in the game clones a ptank; the clone is left without one so that
        // RPATOMICPTANKPLUGINDATA on it reads null rather than a buffer two
        // atomics both think they own.
        (void)src;
        *PLUGINOFFSET(RpPTankAtomicExtPrv*, dst, offset) = NULL;
        return dst;
    }

    // The plugin ID only has to be distinct within librw's atomic plugin list:
    // nothing streams PTank data in or out, on either side. It follows the
    // toolkit IDs librw already knows (ID_UVANIMATION is 0x35) rather than
    // being invented, but it has NOT been checked against RenderWare's own
    // rwID_PTANKPLUGIN, and it must not be relied on for stream matching.
    const rw::uint32 kPTankPluginID = MAKEPLUGINID(rw::VEND_CRITERIONTK, 0x36);

    // --- the geometry a ptank draws through --------------------------------
    //
    // Four vertices and two triangles per particle: one camera-facing quad.
    // The index buffer is built once here because it never changes -- only the
    // vertex positions do, and those are what instancing would write.
    RpGeometry* createPTankGeometry(RwInt32 maxPCount)
    {
        RwUInt32 format = rpGEOMETRYPOSITIONS | rpGEOMETRYTEXTURED | rpGEOMETRYPRELIT |
                          rpGEOMETRYMODULATEMATERIALCOLOR;

        RpGeometry* geometry = RpGeometryCreate(maxPCount * 4, maxPCount * 2, format);
        if (geometry == NULL)
        {
            return NULL;
        }

        rw::Geometry* geo = reinterpret_cast<rw::Geometry*>(geometry);

        // RpGeometryCreate does not clear what it allocates, and an
        // uninstanced ptank must draw nothing rather than whatever was in the
        // heap. Zeroed vertices make every triangle degenerate.
        memset(geometry->morphTarget[0].verts, 0, maxPCount * 4 * sizeof(RwV3d));
        memset(geometry->preLitLum, 0, maxPCount * 4 * sizeof(RwRGBA));
        memset(geometry->texCoords[0], 0, maxPCount * 4 * sizeof(RwTexCoords));

        rw::Material* material = rw::Material::create();
        if (material == NULL)
        {
            geo->destroy();
            return NULL;
        }

        rw::int32 matIndex = geo->matList.appendMaterial(material);

        // appendMaterial took the reference the geometry keeps, so the one
        // Material::create handed out is ours to drop.
        material->destroy();

        if (matIndex < 0)
        {
            geo->destroy();
            return NULL;
        }

        for (RwInt32 i = 0; i < maxPCount; i++)
        {
            RwUInt16 v = (RwUInt16)(i * 4);
            RpTriangle* t = &geometry->triangles[i * 2];

            t[0].vertIndex[0] = v;
            t[0].vertIndex[1] = (RwUInt16)(v + 1);
            t[0].vertIndex[2] = (RwUInt16)(v + 2);
            t[0].matIndex = (RwInt16)matIndex;

            t[1].vertIndex[0] = v;
            t[1].vertIndex[1] = (RwUInt16)(v + 2);
            t[1].vertIndex[2] = (RwUInt16)(v + 3);
            t[1].matIndex = (RwInt16)matIndex;
        }

        // Builds the mesh from those triangles, which is what makes
        // geometry->mesh and matList agree.
        RpGeometryUnlock(geometry);
        return geometry;
    }
} // namespace

namespace
{
    // --- instancing: particles into billboard vertices ---------------------
    //
    // This is the half of the plugin that turns a locked particle buffer into
    // something the rasteriser can draw, and it runs from the atomic's render
    // callback rather than at unlock time. It has to: a billboard faces the
    // camera, so the vertices depend on which camera is rendering, and the
    // same ptank can be drawn by more than one.
    //
    // The camera comes from rw::engine->currentCamera, which is set for the
    // duration of a RwCameraBeginUpdate/EndUpdate pair (camera.cpp). An
    // earlier revision of this file gave that pointer being permanently null
    // as the reason there was no instancing at all; that stopped being true
    // when camera.cpp started publishing it, and this is the work that was
    // waiting on it.
    //
    // Corners are wound to match the index buffer createPTankGeometry built:
    // 0,1,2 and 0,2,3, round the quad rather than across it.
    void instancePTank(RpAtomic* atomic, RpPTankAtomicExtPrv* ext)
    {
        rw::Atomic* a = reinterpret_cast<rw::Atomic*>(atomic);
        rw::Geometry* geo = a->geometry;

        if (geo == NULL || geo->morphTargets == NULL)
        {
            return;
        }

        rw::Camera* cam = rw::engine->currentCamera;
        if (cam == NULL)
        {
            // Outside a camera update there is no facing to compute against.
            // Leaving the vertices alone redraws the last frame's billboards
            // rather than a frame of garbage.
            return;
        }

        RwInt32 count = ext->actPCount;
        if (count < 0)
        {
            count = 0;
        }
        if (count > ext->maxPCount)
        {
            count = ext->maxPCount;
        }

        rw::V3d* verts = geo->morphTargets[0].vertices;
        rw::RGBA* colors = geo->colors;
        rw::TexCoords* uvs = geo->texCoords[0];

        if (verts == NULL)
        {
            return;
        }

        const RpPTankData& pd = ext->publicData;
        const RwUInt32 flags = (RwUInt32)pd.format.dataFlags;

        const RwUInt8* posData = (const RwUInt8*)pd.clusters[RPPTANKSIZEPOSITION].data;
        if (posData == NULL)
        {
            // A tank with no positions has nothing to place.
            return;
        }

        // The camera axes, in world space. Particle positions are world space
        // too -- xPtankPool gives every ptank an identity frame -- so each quad
        // is built straight around its position with no transform in between.
        rw::Matrix* ltm = cam->getFrame()->getLTM();
        const rw::V3d camRight = ltm->right;
        const rw::V3d camUp = ltm->up;

        const RwInt32 posStride = pd.clusters[RPPTANKSIZEPOSITION].stride;
        const RwUInt8* sizeData = (const RwUInt8*)pd.clusters[RPPTANKSIZESIZE].data;
        const RwInt32 sizeStride = pd.clusters[RPPTANKSIZESIZE].stride;
        const RwUInt8* colData = (const RwUInt8*)pd.clusters[RPPTANKSIZECOLOR].data;
        const RwInt32 colStride = pd.clusters[RPPTANKSIZECOLOR].stride;
        const RwUInt8* vtxColData = (const RwUInt8*)pd.clusters[RPPTANKSIZEVTXCOLOR].data;
        const RwInt32 vtxColStride = pd.clusters[RPPTANKSIZEVTXCOLOR].stride;
        const RwUInt8* rotData = (const RwUInt8*)pd.clusters[RPPTANKSIZE2DROTATE].data;
        const RwInt32 rotStride = pd.clusters[RPPTANKSIZE2DROTATE].stride;
        const RwUInt8* uv2Data = (const RwUInt8*)pd.clusters[RPPTANKSIZEVTX2TEXCOORDS].data;
        const RwInt32 uv2Stride = pd.clusters[RPPTANKSIZEVTX2TEXCOORDS].stride;
        const RwUInt8* uv4Data = (const RwUInt8*)pd.clusters[RPPTANKSIZEVTX4TEXCOORDS].data;
        const RwInt32 uv4Stride = pd.clusters[RPPTANKSIZEVTX4TEXCOORDS].stride;

        // rpPTANKDFLAGUSECENTER shifts the quad off its position; without it
        // the position is the middle of the billboard.
        const RwReal centerX = (flags & rpPTANKDFLAGUSECENTER) ? pd.cCenter.x : 0.0f;
        const RwReal centerY = (flags & rpPTANKDFLAGUSECENTER) ? pd.cCenter.y : 0.0f;

        for (RwInt32 i = 0; i < count; i++)
        {
            const RwV3d* pos = (const RwV3d*)(posData + (size_t)posStride * i);

            RwReal halfW = pd.cSize.x * 0.5f;
            RwReal halfH = pd.cSize.y * 0.5f;
            if (sizeData != NULL)
            {
                const RwV2d* sz = (const RwV2d*)(sizeData + (size_t)sizeStride * i);
                halfW = sz->x * 0.5f;
                halfH = sz->y * 0.5f;
            }

            // The billboard's two in-plane axes, turned about the view
            // direction when the tank carries a 2D rotation. RenderWare's
            // setters take degrees, so this does too.
            rw::V3d axisX = camRight;
            rw::V3d axisY = camUp;

            RwReal rot = (flags & rpPTANKDFLAGCNS2DROTATE) ? pd.cRotate : 0.0f;
            if (rotData != NULL)
            {
                rot = *(const RwReal*)(rotData + (size_t)rotStride * i);
            }

            if (rot != 0.0f)
            {
                const float rad = (float)(rot * (3.14159265358979323846 / 180.0));
                const float cs = cosf(rad);
                const float sn = sinf(rad);

                axisX.x = camRight.x * cs + camUp.x * sn;
                axisX.y = camRight.y * cs + camUp.y * sn;
                axisX.z = camRight.z * cs + camUp.z * sn;

                axisY.x = camUp.x * cs - camRight.x * sn;
                axisY.y = camUp.y * cs - camRight.y * sn;
                axisY.z = camUp.z * cs - camRight.z * sn;
            }

            // Corner offsets in billboard space, wound top-left, top-right,
            // bottom-right, bottom-left.
            const RwReal cx[4] = { -halfW + centerX, halfW + centerX, halfW + centerX,
                                   -halfW + centerX };
            const RwReal cy[4] = { halfH + centerY, halfH + centerY, -halfH + centerY,
                                   -halfH + centerY };

            rw::V3d* v = &verts[i * 4];
            for (int k = 0; k < 4; k++)
            {
                v[k].x = pos->x + axisX.x * cx[k] + axisY.x * cy[k];
                v[k].y = pos->y + axisX.y * cx[k] + axisY.y * cy[k];
                v[k].z = pos->z + axisX.z * cx[k] + axisY.z * cy[k];
            }

            if (colors != NULL)
            {
                rw::RGBA* c4 = &colors[i * 4];
                if (vtxColData != NULL)
                {
                    const RwRGBA* src = (const RwRGBA*)(vtxColData + (size_t)vtxColStride * i);
                    for (int k = 0; k < 4; k++)
                    {
                        c4[k].red = src[k].red;
                        c4[k].green = src[k].green;
                        c4[k].blue = src[k].blue;
                        c4[k].alpha = src[k].alpha;
                    }
                }
                else
                {
                    RwRGBA col = pd.cColor;
                    if (colData != NULL)
                    {
                        col = *(const RwRGBA*)(colData + (size_t)colStride * i);
                    }
                    for (int k = 0; k < 4; k++)
                    {
                        c4[k].red = col.red;
                        c4[k].green = col.green;
                        c4[k].blue = col.blue;
                        c4[k].alpha = col.alpha;
                    }
                }
            }

            if (uvs != NULL)
            {
                rw::TexCoords* t4 = &uvs[i * 4];
                if (uv4Data != NULL)
                {
                    const RwTexCoords* src = (const RwTexCoords*)(uv4Data + (size_t)uv4Stride * i);
                    for (int k = 0; k < 4; k++)
                    {
                        t4[k].u = src[k].u;
                        t4[k].v = src[k].v;
                    }
                }
                else
                {
                    // Two corners, top-left and bottom-right, opened out to
                    // four the same way round as the positions above.
                    RwTexCoords tl = pd.cUV[0];
                    RwTexCoords br = pd.cUV[1];
                    if (uv2Data != NULL)
                    {
                        const RwTexCoords* src =
                            (const RwTexCoords*)(uv2Data + (size_t)uv2Stride * i);
                        tl = src[0];
                        br = src[1];
                    }

                    t4[0].u = tl.u;
                    t4[0].v = tl.v;
                    t4[1].u = br.u;
                    t4[1].v = tl.v;
                    t4[2].u = br.u;
                    t4[2].v = br.v;
                    t4[3].u = tl.u;
                    t4[3].v = br.v;
                }
            }
        }

        // Particles past the active count have to go back to being degenerate,
        // or the tail of a tank that shrank keeps drawing last frame's
        // billboards. The whole remainder is cleared rather than tracking what
        // was live last time, because RpPTankAtomicExtPrv is RenderWare's own
        // struct and the GameCube build shares this header -- a field added
        // here for bookkeeping would change a layout that is not ours to
        // change. The cost is bounded by maxPCount, which is 64 for the ptank
        // pool: a few kilobytes of memset against a draw call.
        if (count < ext->maxPCount)
        {
            const RwInt32 stale = ext->maxPCount - count;
            memset(&verts[count * 4], 0, (size_t)stale * 4 * sizeof(rw::V3d));
        }

        // The vertices changed, so the instanced streams have to be rebuilt.
        // lock() raises the dirty bits d3d9.cpp's instance path reads; it does
        // NOT throw the mesh away unless LOCKPOLYGONS is among them, and the
        // mesh here never changes.
        geo->lock(rw::Geometry::LOCKVERTICES | rw::Geometry::LOCKPRELIGHT |
                  rw::Geometry::LOCKTEXCOORDS);
    }

    // The callback the atomic carries. It puts the tank's blend state up,
    // instances, hands over to whatever librw would have done -- which is what
    // actually draws -- and puts the blend state back.
    //
    // **The blend state is the ptank's, not the caller's, and only this
    // callback knows it.** xPtankPool.cpp writes srcBlend, dstBlend and
    // vertexAlphaBlend into publicData and buckets whole tanks by those three
    // (grab_block, sort_buckets, compare_ptanks) so that one draw can serve
    // every particle that shares them -- and then xPTankPoolRender sets only
    // the cull mode and the depth states before calling this. Nothing else in
    // the game ever applies them, which is why the fields exist.
    //
    // What RenderWare does with them is not inferred. The GameCube plugin's
    // render callback is in the target:
    //
    //     _rpPTankGameCubeRenderCallBack   rwsdk/plugin/ptank/gcn/
    //                                      ptankgcncallbacks.s:0x80206F98
    //
    //         if(ext->publicData.vertexAlphaBlend){        // +0xAC
    //             RwRenderStateGet(SRCBLEND,  &saveSrc);
    //             RwRenderStateSet(SRCBLEND,  ext->publicData.srcBlend);   // +0xA4
    //             RwRenderStateGet(DESTBLEND, &saveDst);
    //             RwRenderStateSet(DESTBLEND, ext->publicData.dstBlend);   // +0xA8
    //             RwRenderStateSet(VERTEXALPHAENABLE, TRUE);
    //         }else
    //             RwRenderStateSet(VERTEXALPHAENABLE, FALSE);
    //
    //         ext->defaultRenderCB(atomic);                // +0x10
    //
    //         if(ext->publicData.vertexAlphaBlend){
    //             RwRenderStateSet(SRCBLEND,  saveSrc);
    //             RwRenderStateSet(DESTBLEND, saveDst);
    //         }
    //
    // -- the states are 10, 11 and 12 in the disassembly, which is SRCBLEND,
    // DESTBLEND and VERTEXALPHAENABLE, and the offsets are where those three
    // fields land in RpPTankAtomicExtPrv. This is that, in the same order,
    // with the same save and restore.
    //
    // Without it a pooled ptank drew with whatever blend the last thing to
    // touch the render state had left behind, which for particles that are
    // meant to be additive is an alpha blend and for particles that are meant
    // to be alpha-blended is whatever came before.
    RpAtomic* ptankRenderCB(RpAtomic* atomic)
    {
        RpPTankAtomicExtPrv* ext = extOf(atomic);
        if (ext == NULL)
        {
            return AtomicDefaultRenderCallBack(atomic);
        }

        instancePTank(atomic, ext);
        ext->instFlags = 0;

        RwBlendFunction savedSrc = rwBLENDSRCALPHA;
        RwBlendFunction savedDst = rwBLENDINVSRCALPHA;
        const RwBool blending = ext->publicData.vertexAlphaBlend;

        if (blending)
        {
            RwRenderStateGet(rwRENDERSTATESRCBLEND, &savedSrc);
            RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)ext->publicData.srcBlend);
            RwRenderStateGet(rwRENDERSTATEDESTBLEND, &savedDst);
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)ext->publicData.dstBlend);
            RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
        }
        else
        {
            RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
        }

        RpAtomic* result;
        if (ext->defaultRenderCB != NULL)
        {
            result = ext->defaultRenderCB(atomic);
        }
        else
        {
            result = AtomicDefaultRenderCallBack(atomic);
        }

        if (blending)
        {
            RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)savedSrc);
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)savedDst);
        }

        return result;
    }
} // namespace

RwBool RpPTankPluginAttach(void)
{
    _rpPTankAtomicDataOffset =
        rw::Atomic::registerPlugin(sizeof(RpPTankAtomicExtPrv*), kPTankPluginID, createPTankAtomic,
                                   destroyPTankAtomic, copyPTankAtomic);

    return _rpPTankAtomicDataOffset >= 0 ? TRUE : FALSE;
}

RpAtomic* RpPTankAtomicCreate(RwInt32 maxParticleNum, RwUInt32 dataFlags, RwUInt32 platFlags)
{
    if (maxParticleNum <= 0 || maxParticleNum > kMaxParticles || _rpPTankAtomicDataOffset < 0)
    {
        // Nothing to hold, more billboard vertices than a 16-bit index can
        // reach, or RpPTankPluginAttach was never called and there is nowhere
        // on the atomic to keep the tank.
        return NULL;
    }

    RpAtomic* atomic = reinterpret_cast<RpAtomic*>(rw::Atomic::create());
    if (atomic == NULL)
    {
        return NULL;
    }

    RpGeometry* geometry = createPTankGeometry(maxParticleNum);
    if (geometry == NULL)
    {
        reinterpret_cast<rw::Atomic*>(atomic)->destroy();
        return NULL;
    }

    // The atomic takes its own reference; ours goes away, so the geometry dies
    // with the atomic and not before.
    RpAtomicSetGeometry(atomic, geometry, 0);
    reinterpret_cast<rw::Geometry*>(geometry)->destroy();

    RpPTankAtomicExtPrv* ext = (RpPTankAtomicExtPrv*)RwMalloc(sizeof(RpPTankAtomicExtPrv));
    if (ext == NULL)
    {
        reinterpret_cast<rw::Atomic*>(atomic)->destroy();
        return NULL;
    }
    memset(ext, 0, sizeof(*ext));

    ext->maxPCount = maxParticleNum;
    ext->actPCount = 0;
    ext->platFlags = platFlags;
    ext->publicData.format.dataFlags = dataFlags;

    // The two layout flags name what one *element* of the data is, not how the
    // clusters sit in memory, and they are the opposite way round to how they
    // read:
    //
    //   rpPTANKDFLAGSTRUCTURE -- each particle is a structure. The clusters are
    //       interleaved into one record per particle, so every cluster strides
    //       by the whole record and they all share that stride.
    //   rpPTANKDFLAGARRAY -- each cluster is an array. Every cluster is its own
    //       contiguous block striding by one particle's worth of that cluster
    //       alone, which is the structure-of-arrays form isAStructure names.
    //
    // Both callers prove it. xPtankPool.cpp asks for rpPTANKDFLAGSTRUCTURE and
    // then reads ONE stride out of the position lock and advances position,
    // colour, size and UV by it (xPtankPool.h, lock_block/next) -- only correct
    // if every cluster strides identically, i.e. interleaved. zParPTank.cpp
    // asks for rpPTANKDFLAGARRAY and multiplies each lock's own stride by the
    // particle index, which is right either way.
    //
    // Reading these backwards laid the ptank pool out as structure-of-arrays,
    // so the pool's single 12-byte position stride walked the size, colour and
    // UV writes straight through each other. It cost the Tubelet its fire: the
    // bottom-right texture coordinate of every particle was clobbered, the UV
    // rect collapsed to zero height, and 57 sprites a frame sampled the
    // transparent top row of fx_tubelet_flame and blended to nothing.
    ext->isAStructure = (dataFlags & rpPTANKDFLAGARRAY) ? TRUE : FALSE;

    RwUInt32 active = dataFlags & kPerParticleFlags;

    RwInt32 recordSize = 0;
    RwInt32 numActive = 0;
    for (int i = 0; i < kNumClusters; i++)
    {
        if (active & sClusters[i].flag)
        {
            recordSize += datasize[sClusters[i].cluster];
            numActive++;
        }
    }

    ext->publicData.format.numClusters = numActive;
    ext->publicData.format.stride = recordSize;

    RwInt32 total;
    if (ext->isAStructure)
    {
        // Each block padded up so the next one starts aligned.
        total = 0;
        for (int i = 0; i < kNumClusters; i++)
        {
            if (active & sClusters[i].flag)
            {
                total += alignSize(datasize[sClusters[i].cluster] * maxParticleNum);
            }
        }
    }
    else
    {
        total = alignSize(recordSize) * maxParticleNum;
    }

    // kAlign - 1 slack so the base can be brought up to alignment; rawdata
    // keeps the pointer that has to be freed.
    RwUInt8* raw = NULL;
    RwUInt8* base = NULL;
    if (total > 0)
    {
        raw = (RwUInt8*)RwMalloc(total + kAlign);
        if (raw == NULL)
        {
            RwFree(ext);
            reinterpret_cast<rw::Atomic*>(atomic)->destroy();
            return NULL;
        }
        memset(raw, 0, total + kAlign);
        base = alignUp(raw);
    }

    ext->rawdata = raw;
    ext->publicData.data = base;

    // Clusters the format does not include keep a null pointer and a zero
    // stride, so a lock of one is refused rather than handing out the base of
    // somebody else's array.
    RwUInt8* p = base;
    RwInt32 recordOffset = 0;
    for (int i = 0; i < kNumClusters; i++)
    {
        RpPTankLockStruct& cluster = ext->publicData.clusters[sClusters[i].cluster];
        if (!(active & sClusters[i].flag))
        {
            cluster.data = NULL;
            cluster.stride = 0;
            continue;
        }

        RwInt32 size = datasize[sClusters[i].cluster];
        if (ext->isAStructure)
        {
            cluster.data = p;
            cluster.stride = size;
            p += alignSize(size * maxParticleNum);
        }
        else
        {
            cluster.data = base + recordOffset;
            cluster.stride = alignSize(recordSize);
            recordOffset += size;
        }
    }

    // The constant-value fields the game writes straight into publicData.
    // Left at the zeros memset put there, except for the identity matrix,
    // which is the only one whose zero is meaningless.
    RwMatrixSetIdentity(&ext->publicData.cMatrix);

    // librw's default atomic render callback, which draws the geometry through
    // the atomic's pipeline. The game calls this by hand
    // (`ptank->renderCallBack(ptank)`), and until instancing is written it
    // draws the degenerate triangles createPTankGeometry zeroed -- i.e.
    // nothing. RenderWare would put the PTank render callback here instead.
    ext->defaultRenderCB = atomic->renderCallBack;

    // And now the ptank's own, which builds billboards out of the particle
    // clusters and then calls the one saved above.
    atomic->renderCallBack = ptankRenderCB;

    RPATOMICPTANKPLUGINDATA(atomic) = ext;
    return atomic;
}

void RpPTankAtomicDestroy(RpAtomic* ptank)
{
    if (ptank == NULL)
    {
        return;
    }

    RpPTankAtomicExtPrv* ext = extOf(ptank);
    if (ext != NULL)
    {
        RwFree(ext->rawdata);
        RwFree(ext);
        RPATOMICPTANKPLUGINDATA(ptank) = NULL;
    }

    // Takes the geometry with it, because the atomic holds the only reference
    // left after RpPTankAtomicCreate dropped its own.
    reinterpret_cast<rw::Atomic*>(ptank)->destroy();
}

RwBool RpPTankAtomicLock(RpAtomic* atomic, RpPTankLockStruct* dst, RwUInt32 dataFlags,
                         RpPTankLockFlags lockFlag)
{
    RpPTankAtomicExtPrv* ext = extOf(atomic);
    if (ext == NULL || dst == NULL)
    {
        return FALSE;
    }

    RwUInt32 wanted = dataFlags & kPerParticleFlags;

    // Exactly one cluster per call. There is one RpPTankLockStruct to fill and
    // no way to tell from the signature how RenderWare would report several,
    // so a multi-cluster lock is refused rather than guessed at -- and every
    // call site in the game (xPtankPool.cpp, zParPTank.cpp) asks for one.
    if (wanted == 0 || (wanted & (wanted - 1)) != 0)
    {
        return FALSE;
    }

    RwInt32 cluster = -1;
    for (int i = 0; i < kNumClusters; i++)
    {
        if (sClusters[i].flag == wanted)
        {
            cluster = sClusters[i].cluster;
            break;
        }
    }

    if (cluster < 0 || ext->publicData.clusters[cluster].data == NULL)
    {
        // Not a cluster, or not one this ptank was created with.
        return FALSE;
    }

    *dst = ext->publicData.clusters[cluster];

    // Remembered so that unlock knows what changed. The read and write bits
    // are kept apart from the cluster bits because they live at the top of the
    // word (0x40000000 and 0x80000000) and the cluster bits at the bottom.
    ext->lockFlags |= (RwUInt32)lockFlag | wanted;
    return TRUE;
}

RpAtomic* RpPTankAtomicUnlock(RpAtomic* atomic)
{
    RpPTankAtomicExtPrv* ext = extOf(atomic);
    if (ext == NULL)
    {
        return atomic;
    }

    if (ext->lockFlags & rpPTANKLOCKWRITE)
    {
        // rpPTANKIFLAG* and rpPTANKDFLAG* use the same bit for the same
        // cluster -- position is 1 in both, colour 2, size 4, and so on -- so
        // the clusters that were locked for writing are exactly the instance
        // flags to raise. The game ORs rpPTANKIFLAGACTNUMCHG in itself after
        // unlocking, because changing the particle count is not something a
        // lock can tell it about.
        ext->instFlags |= ext->lockFlags & kPerParticleFlags;
    }

    ext->lockFlags = 0;
    return atomic;
}
