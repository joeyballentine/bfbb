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
// Gram-Schmidt, in RenderWare's axis order: at is taken as given, up is made
// perpendicular to it, and right follows from the cross product, which is what
// keeps a camera's forward direction the one the caller set.
RwFrame* RwFrameOrthoNormalize(RwFrame* frame)
{
    rw::Matrix* m = asMatrix(&frame->modelling);

    m->at = rw::normalize(m->at);

    // up minus its component along at
    m->up = rw::normalize(rw::sub(m->up, rw::scale(m->at, rw::dot(m->up, m->at))));

    m->right = rw::cross(m->up, m->at);

    // The cached "what kind of matrix is this" flags are now stale.
    m->update();

    asFrame(frame)->updateObjects();
    return frame;
}
