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
// This file implements the four calls the game makes into that plugin, plus
// the attach. It does NOT implement instancing or rendering, and the reason is
// not effort: turning particle positions into billboard vertices needs the
// camera's right and up vectors, and there is no camera on this side yet --
// RwEngineInstance->curCamera is documented as permanently null in
// engine_start.cpp. So a ptank created here is correctly shaped, correctly
// locked and correctly unlocked, and draws nothing: its vertices are zeroed at
// create and never moved, which makes every triangle degenerate. That is
// visible as missing particles, not as garbage geometry, and TODO.md says so.
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

    // rpPTANKDFLAGSTRUCTURE is structure-of-arrays: each cluster is its own
    // contiguous block and strides by one particle's worth of that cluster
    // alone. Anything else -- rpPTANKDFLAGARRAY, or neither, which the game
    // never passes -- interleaves every cluster into one record per particle,
    // so every cluster strides by the whole record.
    ext->isAStructure = (dataFlags & rpPTANKDFLAGSTRUCTURE) ? TRUE : FALSE;

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
