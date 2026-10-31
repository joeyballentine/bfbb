// RenderWare C API: RwFrame.
//
// The port gives RwFrame librw's field order under RenderWare's field names
// (see include/rwsdk/rwcore.h and layout.cpp), so an RwFrame* IS an rw::Frame*
// and every function here is a cast and a call. Nothing is copied or converted,
// which is what makes this safe when the game writes ->modelling directly.

#include <rwcore.h>

#include "rw.h"

#include <math.h>

static inline rw::Frame* asFrame(RwFrame* f)
{
    return reinterpret_cast<rw::Frame*>(f);
}

static inline rw::V3d* asV3d(const RwV3d* v)
{
    return const_cast<rw::V3d*>(reinterpret_cast<const rw::V3d*>(v));
}

static inline rw::Matrix* asMatrix(const RwMatrix* m)
{
    return const_cast<rw::Matrix*>(reinterpret_cast<const rw::Matrix*>(m));
}

RwFrame* RwFrameCreate(void)
{
    return reinterpret_cast<RwFrame*>(rw::Frame::create());
}

RwBool RwFrameDestroy(RwFrame* frame)
{
    if (frame == NULL)
    {
        return FALSE;
    }

    asFrame(frame)->destroy();
    return TRUE;
}

RwMatrix* RwFrameGetLTM(RwFrame* frame)
{
    // librw's getLTM resynchronises the hierarchy before returning, which is
    // what RenderWare's does too -- the LTM is only valid once every parent
    // above it has been recomputed.
    return reinterpret_cast<RwMatrix*>(asFrame(frame)->getLTM());
}

RwFrame* RwFrameRotate(RwFrame* frame, const RwV3d* axis, RwReal angle, RwOpCombineType combine)
{
    asFrame(frame)->rotate(asV3d(axis), angle, (rw::CombineOp)combine);
    return frame;
}

RwFrame* RwFrameTranslate(RwFrame* frame, const RwV3d* v, RwOpCombineType combine)
{
    asFrame(frame)->translate(asV3d(v), (rw::CombineOp)combine);
    return frame;
}

RwFrame* RwFrameTransform(RwFrame* frame, const RwMatrix* m, RwOpCombineType combine)
{
    asFrame(frame)->transform(asMatrix(m), (rw::CombineOp)combine);
    return frame;
}

RwFrame* RwFrameUpdateObjects(RwFrame* frame)
{
    asFrame(frame)->updateObjects();
    return frame;
}

// The other half of RwFrame: attaching a camera, a light or an atomic TO one.
//
// This is not on the 112-function list and its absence from that list is a bug
// in how the list was generated (see TODO.md), not a statement that nothing
// needs it. RwCameraSetFrame and RpLightSetFrame are macros for it, and eleven
// sites call it by name -- so the camera and light groups were never actually
// complete without this function.
//
// The void* is RenderWare's own signature, and it works because every object
// that can hang off a frame leads with an RwObjectHasFrame: RwCamera, RpLight
// and RpAtomic all do, on both sides of the mirror. That is asserted for
// RwObjectHasFrame itself in layout_geometry.cpp; what makes the cast below
// safe is that librw's Camera, Light and Atomic each lead with an
// ObjectWithFrame too, so offset zero means the same thing in both.
//
// librw's ObjectWithFrame::setFrame is what RenderWare's does, step for step,
// down to inserting at the HEAD of the frame's object list and running
// RwFrameUpdateObjects afterwards -- which is the call that makes the newly
// attached object pick up the frame's LTM before anything reads it.
void _rwObjectHasFrameSetFrame(void* object, RwFrame* frame)
{
    if (object == NULL)
    {
        return;
    }

    // A NULL frame is the detach, and every caller that passes one means it:
    // xLightKit.cpp:142 parks a light, xShadow.cpp:693 and zNPCTypePrawn.cpp:622
    // park a camera before its frame is destroyed underneath it.
    reinterpret_cast<rw::ObjectWithFrame*>(object)->setFrame(asFrame(frame));
}

// librw has no counterpart for this one, so it is written out rather than
// forwarded. RenderWare re-orthonormalizes a frame's modelling matrix to undo
// the drift that accumulates when rotations are concatenated over thousands of
// frames -- without it a rotating object slowly shears.
//
// RenderWare's MatrixOrthoNormalize (bamatrix.c), step for step. Each axis is
// normalized, one is kept, a second is rebuilt as the cross of the kept one
// and the third, and the third as the cross of those two. A zero axis is the
// one rebuilt, because _rwInvSqrt(0) is 0 rather than infinity: a camera
// asset with a zero `at` (BFBB Deluxe's Moon, b107) gets right x up on the
// GameCube, where a plain normalize gives NaN and a black screen.
static float InvLength(const rw::V3d& v)
{
    float sq = rw::dot(v, v);
    return sq > 0.0f ? 1.0f / sqrtf(sq) : 0.0f;
}

RwFrame* RwFrameOrthoNormalize(RwFrame* frame)
{
    rw::Matrix* m = asMatrix(&frame->modelling);

    float r = InvLength(m->right);
    float u = InvLength(m->up);
    float a = InvLength(m->at);
    m->right = rw::scale(m->right, r);
    m->up = rw::scale(m->up, u);
    m->at = rw::scale(m->at, a);

    rw::V3d* keep;
    rw::V3d* other;
    rw::V3d* rebuilt;
    if (r <= 0.0f)
    {
        keep = &m->up, other = &m->at, rebuilt = &m->right;
    }
    else if (u <= 0.0f)
    {
        keep = &m->at, other = &m->right, rebuilt = &m->up;
    }
    else if (a <= 0.0f)
    {
        keep = &m->right, other = &m->up, rebuilt = &m->at;
    }
    else
    {
        // Keep an axis of the pair that is closest to perpendicular.
        float ua = fabsf(rw::dot(m->up, m->at));
        float ra = fabsf(rw::dot(m->right, m->at));
        float ru = fabsf(rw::dot(m->right, m->up));
        if (ua < ra && ua < ru)
        {
            keep = &m->up, other = &m->at, rebuilt = &m->right;
        }
        else if (ua >= ra && ra < ru)
        {
            keep = &m->at, other = &m->right, rebuilt = &m->up;
        }
        else
        {
            keep = &m->right, other = &m->up, rebuilt = &m->at;
        }
    }

    *rebuilt = rw::cross(*keep, *other);
    *rebuilt = rw::scale(*rebuilt, InvLength(*rebuilt));
    *other = rw::cross(*rebuilt, *keep);
    *other = rw::scale(*other, InvLength(*other));

    // The cached "what kind of matrix is this" flags are now stale.
    m->update();

    asFrame(frame)->updateObjects();
    return frame;
}

// Flush the dirty frame list.
//
// This is the third function the old regeneration command hid (see TODO.md),
// and like _rwObjectHasFrameSetFrame it is not optional: seven sites call it,
// and every one of them calls it for the same reason. A frame that is moved
// does not recompute its LTM there and then -- RwFrameUpdateObjects only marks
// the frame's root dirty and puts it on a list -- so anything that wants to
// READ an LTM without going through RwFrameGetLTM has to flush that list
// first. iCamera.cpp:52 does it before reading the camera's LTM into the view
// matrix; xModelBucket.cpp:166 and 622 do it before instancing a bucket's
// worth of atomics, which is the hot path this deferral exists for.
//
// librw's Frame::syncDirty is RenderWare's, step for step: it walks
// engine->frameDirtyList, syncs each root's LTM if the hierarchy needs it,
// calls the sync callback on every object attached to every frame in the
// subtree, clears the dirty bits, and empties the list. The one difference is
// where the list lives -- RWSRCGLOBAL(dirtyFrameList) against rw::engine->
// frameDirtyList -- and nothing on this side of the seam can tell.
RwBool _rwFrameSyncDirty(void)
{
    rw::Frame::syncDirty();
    return TRUE;
}

// The frame HIERARCHY calls, which iModel.cpp needs to build and tear down a
// model's skeleton.

RwFrame* RwFrameGetRoot(const RwFrame* frame)
{
    if (frame == NULL)
    {
        return NULL;
    }

    // Mirrored, so this is a field read either way -- but going through the
    // struct rather than librw keeps it honest if the mirroring ever changes.
    return const_cast<RwFrame*>(frame)->root;
}

// Detach everything hanging off this frame and every frame below it.
//
// librw asserts that a frame's object list is empty before it destroys the
// frame (frame.cpp:79), and RenderWare does not: it frees the frames and leaves
// each attached object pointing at memory that is now free. iModelUnload
// depends on that -- it destroys the hierarchy FIRST and the clump second, so
// every atomic in the clump is still attached when the frames go. On the
// console the atomics are then destroyed before anything reuses the memory and
// nobody notices; under librw the assertion fires and the process aborts.
//
// Clearing the links instead of removing the check reproduces what the game can
// actually observe -- an object whose frame is gone -- without leaving a
// dangling pointer for RpClumpDestroy to walk a moment later. It is the strictly
// safer half of RenderWare's behaviour, not a different one.
static void DetachObjects(rw::Frame* frame)
{
    for (rw::Frame* child = frame->child; child != NULL; child = child->next)
    {
        DetachObjects(child);
    }

    // setFrame(nil) unlinks the object, so the list head moves under us; take
    // the first entry each time rather than iterating.
    while (!frame->objectList.isEmpty())
    {
        rw::ObjectWithFrame* obj = rw::ObjectWithFrame::fromFrame(frame->objectList.link.next);
        obj->setFrame(NULL);
    }
}

RwBool RwFrameDestroyHierarchy(RwFrame* frame)
{
    if (frame == NULL)
    {
        return FALSE;
    }

    DetachObjects(asFrame(frame));

    // Destroys the frame AND every frame below it. iModel.cpp:iModelUnload
    // calls this on a model's root, which is what frees a skeleton -- the
    // per-frame destroy would leave every child leaked and still linked.
    asFrame(frame)->destroyHierarchy();
    return TRUE;
}

RwFrame* RwFrameForAllChildren(RwFrame* frame, RwFrameCallBack callBack, void* data)
{
    if (frame == NULL || callBack == NULL)
    {
        return frame;
    }

    // librw's forAllChildren visits the IMMEDIATE children only, which is
    // RenderWare's contract too -- iModel.cpp recurses itself when it wants the
    // whole subtree (GetChildFrameHierarchy).
    asFrame(frame)->forAllChildren(reinterpret_cast<rw::Frame::Callback>(callBack), data);
    return frame;
}
