// RenderWare C API: RpAtomic.
//
// RpAtomic is mirrored onto rw::Atomic (see include/rwsdk/rpworld.h and
// layout_geometry.cpp), so the two setters are a cast and a call.
//
// RpAtomicForAllIntersections is not: librw has no collision code at all, so
// it is written out below. It is also the one function here that RenderWare
// implements differently rather than merely elsewhere -- see the comment above
// it.

#include <rwcore.h>
#include <rpcollis.h>
#include <rpworld.h>
#include <rtintsec.h>

#include "intersect.h"
#include "stream.h" // brings in librw's rw.h, which must not be included twice

static inline rw::Atomic* asAtomic(RpAtomic* a)
{
    return reinterpret_cast<rw::Atomic*>(a);
}

RpAtomic* RpAtomicSetFrame(RpAtomic* atomic, RwFrame* frame)
{
    if (atomic == NULL)
    {
        return NULL;
    }

    // Unhooks the atomic from whatever frame it was on first, which is what
    // makes RpAtomicSetFrame(a, NULL) the detach xPtankPool.cpp uses before it
    // destroys a frame. librw's setFrame also marks the world bounding sphere
    // dirty, as RenderWare's does.
    asAtomic(atomic)->setFrame(reinterpret_cast<rw::Frame*>(frame));
    return atomic;
}

RpAtomic* RpAtomicSetGeometry(RpAtomic* atomic, RpGeometry* geometry, RwUInt32 flags)
{
    if (atomic == NULL)
    {
        return NULL;
    }

    // Reference counted on both sides: this drops the reference on the old
    // geometry and takes one on the new. zFX.cpp depends on that -- it builds
    // a replacement geometry, hands it over, and never destroys the original.
    //
    // flags is rpATOMICSAMEBOUNDINGSPHERE or nothing, and librw spells it
    // Atomic::SAMEBOUNDINGSPHERE with the same value: keep the sphere the
    // atomic already has instead of copying morph target 0's.
    asAtomic(atomic)->setGeometry(reinterpret_cast<rw::Geometry*>(geometry), flags);
    return atomic;
}

// ---------------------------------------------------------------------------
// RpAtomicForAllIntersections
//
// librw has no counterpart, so this is written out. Two things about it are
// worth knowing before reading the maths.
//
// FIRST, the spaces. RenderWare takes the intersection primitive in WORLD
// space and hands the callback triangles in the atomic's OBJECT space. That is
// not a detail this port is free to choose: iCollide.cpp's callbacks ignore
// the RpIntersection they are passed and test against `cbisx_local`, an
// object-space copy the caller built itself before calling. So the primitive
// is transformed down here by the inverse of the frame's LTM, and the vertices
// handed back are the morph target's own, untransformed.
//
// SECOND, RenderWare does not walk every triangle. A model built with the
// collision plugin carries a BSP tree, and RpAtomicForAllIntersections
// descends it. The port has no tree -- RpCollisionPluginAttach is not written
// and librw would have nowhere to keep one -- so this is a linear scan. It
// reports the same set of triangles, in geometry order rather than tree order,
// and it costs O(triangles) per query where retail costs O(log triangles).
// iCollide.cpp already times these calls (collide_rwtime), which is where the
// difference will show up.
//
// Only the three intersection types RenderWare supports for an atomic are
// handled. rpINTERSECTPOINT and rpINTERSECTATOMIC are world-sector queries and
// have no meaning against a triangle list on either side.

// Only the maths that has no RenderWare name of its own stays here. The
// sphere and box tests moved to intersect.cpp as RtIntersectionSphereTriangle
// and RtIntersectionBBoxTriangle, and this file calls them, so that a triangle
// the game finds by walking an atomic is a triangle it also finds by walking
// that atomic's collision tree in xClumpColl.cpp.

RpAtomic* RpAtomicForAllIntersections(RpAtomic* atomic, RpIntersection* intersection,
                                      RpIntersectionCallBackGeometryTriangle callBack, void* data)
{
    if (atomic == NULL || intersection == NULL || callBack == NULL)
    {
        return atomic;
    }

    RpGeometry* geometry = atomic->geometry;
    if (geometry == NULL || geometry->numTriangles == 0 || geometry->numMorphTargets == 0)
    {
        return atomic;
    }

    // Morph target 0, always. RenderWare would use the interpolated one, but
    // librw does not interpolate morph targets and the port's RpAtomic has no
    // interpolator to read -- see the comment on the struct in rpworld.h.
    RwV3d* verts = geometry->morphTarget[0].verts;
    if (verts == NULL)
    {
        return atomic;
    }

    // World space to object space. RwMatrixInvert handles a scaled LTM; the
    // scale has to come out of a sphere's radius separately, which is exactly
    // what iCollide.cpp does when it builds its own object-space copy, so the
    // two agree on what the callback is testing against.
    // Zeroed before RwMatrixSetIdentity because that macro ORs into the flags
    // word rather than assigning it, so it has to start from something.
    RwMatrix inverse = {};
    float scale = 1.0f;
    RwFrame* frame = RpAtomicGetFrame(atomic);
    if (frame != NULL)
    {
        RwMatrix* ltm = RwFrameGetLTM(frame);
        RwMatrixInvert(&inverse, ltm);
        scale = RwV3dLength(&ltm->right);
        if (scale == 0.0f)
        {
            // A zero-scaled atomic has no volume to hit.
            return atomic;
        }
    }
    else
    {
        RwMatrixSetIdentity(&inverse);
    }

    // The primitive, in object space.
    RwSphere sphere = {};
    RwV3d lineStart = {};
    RwV3d lineEnd = {};
    RwBBox box = {};

    switch (intersection->type)
    {
    case rpINTERSECTSPHERE:
    {
        RwV3dTransformPoints(&sphere.center, &intersection->t.sphere.center, 1, &inverse);
        sphere.radius = intersection->t.sphere.radius / scale;
        break;
    }

    case rpINTERSECTLINE:
    {
        RwV3dTransformPoints(&lineStart, &intersection->t.line.start, 1, &inverse);
        RwV3dTransformPoints(&lineEnd, &intersection->t.line.end, 1, &inverse);
        break;
    }

    case rpINTERSECTBOX:
    {
        // An RwBBox is axis-aligned in the space it was given in, and the
        // inverse LTM is not axis-preserving in general, so there is no
        // object-space box that is exactly this world-space box. The eight
        // corners are transformed and re-bounded: that box is at least as
        // large as the true one, so the callback sees a superset and never
        // misses a triangle. Nothing in the game issues a box query against an
        // atomic today, so which way RenderWare rounded is not something this
        // port can check.
        RwV3d corners[8];
        const RwV3d& inf = intersection->t.box.inf;
        const RwV3d& sup = intersection->t.box.sup;
        for (int i = 0; i < 8; i++)
        {
            corners[i].x = (i & 1) ? sup.x : inf.x;
            corners[i].y = (i & 2) ? sup.y : inf.y;
            corners[i].z = (i & 4) ? sup.z : inf.z;
        }
        RwV3dTransformPoints(corners, corners, 8, &inverse);

        // RwBBoxCalculate is RenderWare's own name for this and is not written
        // yet; it is four lines here and would be a second unasserted claim
        // about RwBBox's field order there.
        box.inf = corners[0];
        box.sup = corners[0];
        for (int i = 1; i < 8; i++)
        {
            if (corners[i].x < box.inf.x)
                box.inf.x = corners[i].x;
            if (corners[i].y < box.inf.y)
                box.inf.y = corners[i].y;
            if (corners[i].z < box.inf.z)
                box.inf.z = corners[i].z;
            if (corners[i].x > box.sup.x)
                box.sup.x = corners[i].x;
            if (corners[i].y > box.sup.y)
                box.sup.y = corners[i].y;
            if (corners[i].z > box.sup.z)
                box.sup.z = corners[i].z;
        }
        break;
    }

    default:
        // rpINTERSECTPOINT and rpINTERSECTATOMIC are world-sector queries.
        // RenderWare does not support them against an atomic either.
        return atomic;
    }

    RpTriangle* triangles = geometry->triangles;
    for (RwInt32 i = 0; i < geometry->numTriangles; i++)
    {
        RwV3d* pa = &verts[triangles[i].vertIndex[0]];
        RwV3d* pb = &verts[triangles[i].vertIndex[1]];
        RwV3d* pc = &verts[triangles[i].vertIndex[2]];

        RpCollisionTriangle collTriangle;

        RwReal distance;
        switch (intersection->type)
        {
        case rpINTERSECTSPHERE:
            // Fills collTriangle.normal on the way, which is the same normal
            // the other two cases compute below.
            if (!RtIntersectionSphereTriangle(&sphere, pa, pb, pc, &collTriangle.normal, &distance))
            {
                continue;
            }
            break;

        case rpINTERSECTLINE:
            if (!rwshim::intersectSegmentTriangle(&lineStart, &lineEnd, pa, pb, pc, &distance))
            {
                continue;
            }
            rwshim::triangleNormal(pa, pb, pc, &collTriangle.normal);
            break;

        default: // rpINTERSECTBOX
            if (!RtIntersectionBBoxTriangle(&box, pa, pb, pc))
            {
                continue;
            }
            // A box query has no single point of contact to measure to, and
            // RenderWare's documentation does not say what it puts here.
            // Zero is what an overlap test can honestly report. No caller in
            // the game reaches this, so nothing depends on the guess.
            distance = 0.0f;
            rwshim::triangleNormal(pa, pb, pc, &collTriangle.normal);
            break;
        }

        collTriangle.index = i;
        collTriangle.vertices[0] = pa;
        collTriangle.vertices[1] = pb;
        collTriangle.vertices[2] = pc;

        // `point` has to be a point ON the triangle's plane, not just near it:
        // properSphereIsectTri in iCollide.cpp builds the plane equation as
        // dot(normal, point). Vertex 0 is on the plane by construction, which
        // is all that equation needs.
        collTriangle.point = *pa;

        if (callBack(intersection, &collTriangle, distance, data) == NULL)
        {
            // RenderWare stops early when the callback returns NULL, and
            // sphereHitsEnv3CB uses that to stop once it has filled the
            // caller's collision array.
            break;
        }
    }

    return atomic;
}

// Destroy, and the standalone-atomic stream pair.
//
// **None of these three was on the 112-function list, and their absence was
// not deliberate.** The list was regenerated from the undefined RenderWare
// symbols in the GameCube objects; these are called only from
// xModelBucket.cpp, and the earlier sweeps read past them. They are found by
// the corrected command in TODO.md, which now excludes symbols the game
// defines itself and includes data symbols rather than only text ones.
//
// All three exist for one function: FullAtomicDupe (xModelBucket.cpp:125),
// which duplicates an atomic by writing it to a memory stream and reading it
// back N times. That is also the caller stream.cpp's hand-written memory
// stream exists for -- librw's own truncates at its initial capacity where
// RenderWare's grows -- so the port had already paid for half of this without
// the other half being there.

RwBool RpAtomicDestroy(RpAtomic* atomic)
{
    if (atomic == NULL)
    {
        return FALSE;
    }

    // librw ASSERTS that an atomic is in no clump and no world when it is
    // destroyed; RenderWare frees it and leaves whichever list it was on
    // holding a link into freed memory. Detaching first is the same choice
    // RpWorldDestroy makes in world.cpp, and for the same reason: retail's
    // dangling link is a latent bug that happens not to fire on the console,
    // and reproducing it here would trade a silent corruption for a loud
    // assert without making anything more faithful.
    //
    // xModelBucket.cpp:629 destroys bucket atomics that were read out of a
    // stream and never added to a clump, so neither branch is reached today.
    rw::Atomic* a = asAtomic(atomic);

    if (a->clump != NULL)
    {
        a->clump->removeAtomic(a);
    }

    if (a->world != NULL)
    {
        a->world->removeAtomic(a);
    }

    // librw's destroy releases the geometry reference and detaches the frame,
    // which is what RpAtomicDestroy in src/rwsdk/world/baclump.c does through
    // RpAtomicSetGeometry(atomic, NULL, 0) and _rwObjectHasFrameReleaseFrame.
    // It does NOT destroy the frame -- FullAtomicDupe destroys that itself,
    // one line before it calls this.
    a->destroy();
    return TRUE;
}

// The standalone atomic chunk, which is NOT the atomic chunk inside a clump.
//
// librw only has the in-clump form (Atomic::streamReadClump and
// streamWriteClump): there, an atomic names its frame and its geometry by
// INDEX into the clump's frame and geometry lists, so neither function can be
// called without a clump to index into. A standalone rwID_ATOMIC has no lists
// to reference, and RenderWare's answer is to write the geometry inline.
//
// That is not a guess. RpAtomicStreamGetSize IS decompiled, in
// src/rwsdk/world/baclump.c:269, and it says exactly what a standalone atomic
// contains:
//
//     size  = rwCHUNKHEADERSIZE + sizeof(rpAtomicChunkInfo);
//     size += rwCHUNKHEADERSIZE + RpGeometryStreamGetSize(atomic->geometry);
//     size += rwCHUNKHEADERSIZE + _rwPluginRegistryGetSize(&atomicTKList, atomic);
//
// -- a STRUCT, then a whole GEOMETRY chunk unconditionally, then the plugin
// extension. So the layout below is retail's, and the only part taken from
// librw is the shape of the struct, which its version < 0x30400 branch shows
// is {frameIndex, flags, unused} at 12 bytes and {frameIndex, geomIndex,
// flags, unused} at 16.
//
// The plugin extension is the part that matters and the part that would be
// easy to skip. RpSkin and RpMatFX both hang off an atomic, and xModelBucket
// duplicates skinned models -- so an implementation that round-tripped the
// geometry and dropped the extension would hand back atomics that render
// unskinned, which looks like a bug in the animation system rather than like a
// missing chunk here.

RpAtomic* RpAtomicStreamWrite(RpAtomic* atomic, RwStream* stream)
{
    if (atomic == NULL || stream == NULL)
    {
        return NULL;
    }

    rw::Atomic* a = asAtomic(atomic);

    if (a->geometry == NULL)
    {
        // RenderWare's own size calculation dereferences the geometry, so a
        // geometryless atomic was never writable there either.
        return NULL;
    }

    const rw::int32 structSize = rw::version < 0x30400 ? 12 : 16;

    rw::uint32 size = 12 + structSize;
    size += 12 + a->geometry->streamGetSize();
    size += 12 + rw::Atomic::s_plglist.streamGetSize(a);

    rw::writeChunkHeader(stream, rw::ID_ATOMIC, size);
    rw::writeChunkHeader(stream, rw::ID_STRUCT, structSize);

    // frameIndex and geomIndex are both zero: there are no lists here for them
    // to index into, and the reader below ignores them for the same reason.
    // RenderWare leaves them as whatever the clump exporter would have put
    // there, and nothing reads them back out of a standalone atomic.
    rw::int32 buf[4] = { 0, 0, 0, 0 };
    buf[rw::version < 0x30400 ? 1 : 2] = a->object.object.flags;
    stream->write32(buf, structSize);

    a->geometry->streamWrite(stream);
    rw::Atomic::s_plglist.streamWrite(stream, a);

    return atomic;
}

RpAtomic* RpAtomicStreamRead(RwStream* stream)
{
    if (stream == NULL)
    {
        return NULL;
    }

    // The rwID_ATOMIC header is already eaten: xModelBucket.cpp calls
    // RwStreamFindChunk(stream, rwID_ATOMIC, NULL, NULL) first, which is the
    // same contract RpClumpStreamRead works to.
    rw::uint32 version;
    if (!rw::findChunk(stream, rw::ID_STRUCT, NULL, &version))
    {
        return NULL;
    }

    rw::int32 buf[4] = { 0, 0, 0, 0 };
    const rw::int32 structSize = version < 0x30400 ? 12 : 16;
    stream->read32(buf, structSize);

    rw::Atomic* a = rw::Atomic::create();
    if (a == NULL)
    {
        return NULL;
    }

    // No frame. RenderWare's standalone reader has no frame list to resolve
    // frameIndex against either, and FullAtomicDupe gives the atomic a fresh
    // RwFrameCreate() on the very next line.
    if (!rw::findChunk(stream, rw::ID_GEOMETRY, NULL, NULL))
    {
        a->destroy();
        return NULL;
    }

    rw::Geometry* geometry = rw::Geometry::streamRead(stream);
    if (geometry == NULL)
    {
        a->destroy();
        return NULL;
    }

    // setGeometry takes a reference of its own, so the one streamRead handed
    // back is given up here. Getting this wrong leaks a geometry per duplicate
    // and FullAtomicDupe runs once per bucket per model.
    a->setGeometry(geometry, 0);
    geometry->destroy();

    a->setFlags(buf[version < 0x30400 ? 1 : 2]);

    if (!rw::Atomic::s_plglist.streamRead(stream, a))
    {
        a->destroy();
        return NULL;
    }

    return reinterpret_cast<RpAtomic*>(a);
}

// RenderWare's default atomic render callback, by name.
//
// Four sites assign it to restore an atomic's rendering after having replaced
// it -- xEntBoulder.cpp:158, zCutsceneMgr.cpp:263, zEntCruiseBubble.cpp:934 and
// zNPCTypeVillager.cpp:2162 -- and RpAtomicSetRenderCallBack's macro in
// rpworld.h:305 uses it as the value that means "back to normal". So it has to
// be a real function with a real address, not a forward to librw's, because
// what those sites store is the POINTER.
//
// The signatures differ in their return only: librw's renderCB is
// void(Atomic*), RenderWare's is RpAtomic*(RpAtomic*). rpworld.h's mirrored
// RpAtomic keeps RenderWare's spelling deliberately -- game code assigns its own
// callbacks into that slot -- and the note there explains why the mismatch is
// safe: one pointer either way, cdecl on both sides, and librw calls it as
// `this->renderCB(this)` and discards nothing because there is nothing to
// discard. This function is the other end of that arrangement.
RpAtomic* AtomicDefaultRenderCallBack(RpAtomic* atomic)
{
    if (atomic == NULL)
    {
        return NULL;
    }

    rw::Atomic::defaultRenderCB(asAtomic(atomic));
    return atomic;
}

// Build the atomic's platform-specific instance data.
//
// RenderWare's pipelines instance lazily -- the first render of an atomic
// converts its geometry into whatever the hardware wants and caches it in
// repEntry. RpAtomicInstance forces that to happen NOW instead, which is what
// iEnv.cpp:17 does to every atomic of a level as it loads, so that the first
// frame after a load does not pay for all of it at once.
//
// librw's Atomic::instance is the same idea and the same laziness, so this is a
// forward. What it does NOT do is the thing RenderWare's does when the geometry
// is locked -- librw instances from whatever is there and leaves the lock flags
// alone -- which matters only to a caller that instances a geometry it is
// midway through editing. Nothing does.
RwBool RpAtomicInstance(RpAtomic* atomic)
{
    if (atomic == NULL)
    {
        return FALSE;
    }

    asAtomic(atomic)->instance();
    return TRUE;
}
