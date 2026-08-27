// RenderWare C API: RwCamera.
//
// RwCamera has librw's field order under RenderWare's field names (see
// include/rwsdk/rwcore.h and layout_camera.cpp), so an RwCamera* IS an
// rw::Camera* and most of this file is a cast and a call.
//
// The exception is RwCameraBeginUpdate/RwCameraEndUpdate, which have to keep
// RwEngineInstance in step with rw::engine. See the comment above them.

#include <rwcore.h>
#include <rpworld.h>

#include "rw.h"

static inline rw::Camera* asCamera(RwCamera* camera)
{
    return reinterpret_cast<rw::Camera*>(camera);
}

static inline const rw::Camera* asCamera(const RwCamera* camera)
{
    return reinterpret_cast<const rw::Camera*>(camera);
}

RwCamera* RwCameraCreate(void)
{
    return reinterpret_cast<RwCamera*>(rw::Camera::create());
}

RwBool RwCameraDestroy(RwCamera* camera)
{
    if (camera == NULL)
    {
        return FALSE;
    }

    // Not the camera's frame: RenderWare leaves that to the caller, and
    // iCamera.cpp:69 relies on it -- it takes the frame off the camera and
    // destroys it by hand before getting here. librw's destroy() detaches the
    // frame the same way and does not destroy it either.
    asCamera(camera)->destroy();
    return TRUE;
}

// ---------------------------------------------------------------------------
// Begin/end update
//
// This is the pair that has to write RwEngineInstance, and the reason is in
// engine_start.cpp: RwGlobals and rw::Engine agree on curCamera/curWorld being
// their first two fields and on nothing after, so the shim owns a SECOND
// RwGlobals rather than an alias onto librw's engine. Game code reads the
// fields directly --
//
//     xCutscene.cpp:716   ((RwCamera*)RwEngineInstance->curCamera)->object.object.parent
//     xFX.cpp:3044        RwCamera* cam = (RwCamera*)RwEngineInstance->curCamera;
//     iParMgr.cpp:236     RWSRCGLOBAL(curCamera)
//
// -- six files in all, plus eleven more through RwCameraGetCurrentCamera(),
// which is a macro for exactly the same read: iCamera, iModel, xFont, xFX,
// xModel, xScrFx, xSkyDome, zFX, zParPTank, zScene and zTextBox.
//
// So there is no call to hook and nothing would catch it: those files would
// read null and fault in a cutscene, an aura or a shadow, long after this
// commit. Both copies get assigned here or the port is broken.
//
// The values are MIRRORED from rw::engine rather than assigned independently,
// so that there is one source of truth and the two cannot drift. librw's
// beginUpdate chain is what sets them: Camera::create installs
// worldBeginUpdateCB, which sets engine->currentWorld from the camera's world
// and then calls defaultBeginUpdateCB, which sets engine->currentCamera,
// syncs the dirty frame list and enters the device.
//
// End nulls both, in both copies, because the game uses "is curCamera null" as
// "is an update in progress":
//
//     zGame.cpp:910          cam = RwEngineInstance->curCamera;
//     zNPCTypePrawn.cpp:1718 if (cam != NULL) RwCameraEndUpdate(cam);
//
// Left non-null, both of those would end an update that had already ended.
// Nulling librw's copy as well is safe: the only readers of
// engine->currentCamera and engine->currentWorld inside librw are the immediate
// mode and material-effect paths in the GL and D3D backends, and every one of
// them runs between a begin and an end.

RwCamera* RwCameraBeginUpdate(RwCamera* camera)
{
    if (camera == NULL || RwEngineInstance == NULL)
    {
        return NULL;
    }

    asCamera(camera)->beginUpdate();

    RwEngineInstance->curCamera = rw::engine->currentCamera;
    RwEngineInstance->curWorld = rw::engine->currentWorld;

    return camera;
}

RwCamera* RwCameraEndUpdate(RwCamera* camera)
{
    if (camera == NULL || RwEngineInstance == NULL)
    {
        return NULL;
    }

    asCamera(camera)->endUpdate();

    rw::engine->currentCamera = NULL;
    rw::engine->currentWorld = NULL;
    RwEngineInstance->curCamera = NULL;
    RwEngineInstance->curWorld = NULL;

    return camera;
}

// ---------------------------------------------------------------------------

RwCamera* RwCameraClear(RwCamera* camera, RwRGBA* colour, RwInt32 clearMode)
{
    if (camera == NULL)
    {
        return NULL;
    }

    // colour is allowed to be null -- iCamera.cpp:107 clears Z only and passes
    // NULL. librw's d3d9 device builds the clear colour before it looks at the
    // mode (d3ddevice.cpp:1335), so it faults on that call rather than ignoring
    // the pointer the way the console device does. Substitute an opaque black:
    // with rwCAMERACLEARIMAGE clear of the mode, D3D never uses the value.
    rw::RGBA black = { 0, 0, 0, 255 };
    rw::RGBA* col = colour != NULL ? reinterpret_cast<rw::RGBA*>(colour) : &black;

    asCamera(camera)->clear(col, (rw::uint32)clearMode);
    return camera;
}

RwCamera* RwCameraShowRaster(RwCamera* camera, void* pDev, RwUInt32 flags)
{
    if (camera == NULL || camera->frameBuffer == NULL)
    {
        // librw dereferences the frame buffer unconditionally, so a camera with
        // no raster is refused here rather than faulting inside the library.
        return NULL;
    }

    // pDev is RenderWare's device-specific display handle -- on the GameCube a
    // RwGameCubeDeviceConfig* naming which of the two framebuffers to flip to.
    // librw's showRaster takes only flags, because it has exactly one display.
    // Every one of the six call sites in the game passes NULL, so there is
    // nothing being dropped; if one ever passes something, this needs a backend
    // that can act on it rather than a cast.
    (void)pDev;

    asCamera(camera)->showRaster(flags);
    return camera;
}

RpWorld* RwCameraGetWorld(const RwCamera* camera)
{
    if (camera == NULL)
    {
        return NULL;
    }

    // librw keeps the world pointer in the camera itself where RenderWare keeps
    // it in a plugin extension, so this is a field read rather than a plugin
    // offset. The result is a handle: RpWorld is not mirrored yet, so nothing
    // may dereference it until whoever writes RpWorldCreate does that.
    return (RpWorld*)asCamera(camera)->world;
}

RwCamera* RwCameraSetProjection(RwCamera* camera, RwCameraProjection projection)
{
    if (camera == NULL)
    {
        return NULL;
    }

    asCamera(camera)->setProjection((rw::int32)projection);
    return camera;
}

RwCamera* RwCameraSetViewWindow(RwCamera* camera, const RwV2d* viewWindow)
{
    if (camera == NULL || viewWindow == NULL)
    {
        return NULL;
    }

    // RenderWare also caches 1/viewWindow in recipViewWindow here. librw has no
    // such field -- cameraSync recomputes the reciprocal each time it builds the
    // view matrix -- so the PC RwCamera does not have one either and there is
    // nothing to keep up to date. See the struct comment in rwcore.h.
    asCamera(camera)->setViewWindow(reinterpret_cast<const rw::V2d*>(viewWindow));
    return camera;
}

RwCamera* RwCameraSetNearClipPlane(RwCamera* camera, RwReal nearClip)
{
    if (camera == NULL)
    {
        return NULL;
    }

    // Recomputes zScale/zShift from the device's own depth range, which is what
    // RenderWare's does. With LIBRW_PLATFORM=NULL that range is 0..1 rather than
    // a real depth buffer's, so the two numbers are computed but meaningless
    // until a backend is linked. Nothing in the game reads them.
    asCamera(camera)->setNearPlane(nearClip);
    return camera;
}

RwCamera* RwCameraSetFarClipPlane(RwCamera* camera, RwReal farClip)
{
    if (camera == NULL)
    {
        return NULL;
    }

    asCamera(camera)->setFarPlane(farClip);
    return camera;
}

RwFrustumTestResult RwCameraFrustumTestSphere(const RwCamera* camera, const RwSphere* sphere)
{
    // The frustum planes this reads are only valid once the camera has been
    // synced, which librw does in cameraSync off the frame's update. That is the
    // same precondition RenderWare has.
    return (RwFrustumTestResult)asCamera(camera)->frustumTestSphere(
        reinterpret_cast<const rw::Sphere*>(sphere));
}
