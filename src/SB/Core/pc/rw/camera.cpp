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
#include "iWindow.h"
#include "iSnapshot.h"

#include <stdio.h>
#include <stdlib.h>

#include <time.h>

// Set from the environment once, so the check costs a load per frame.
static const bool sReportFps = getenv("BFBB_FPS") != NULL;

static inline rw::Camera* asCamera(RwCamera* camera)
{
    return reinterpret_cast<rw::Camera*>(camera);
}

static inline const rw::Camera* asCamera(const RwCamera* camera)
{
    return reinterpret_cast<const rw::Camera*>(camera);
}

// ---------------------------------------------------------------------------
// The live-camera register
//
// Neither library keeps a list of the cameras in a world -- there is only a
// World* on each camera -- so destroying a world leaves every camera that was
// in it pointing at freed memory. On RenderWare that pointer is merely stale;
// the next RpWorldAddCamera overwrites it and nothing has looked at it in
// between. Under librw it is fatal twice over: RpWorldAddCamera refuses a
// camera that claims to be in a world, so the add is silently dropped and the
// camera keeps the dead pointer, and then beginUpdate copies it into
// engine->currentWorld where the d3d9 lighting path dereferences it without a
// check.
//
// That is what killed the cutscene shadow. xShadowSetWorld adds the shadow
// camera to each scene's world and NOTHING ever removes it, so the first scene
// change left it in a freed world for good and enumerateLights walked the
// remains.
//
// So the shim keeps the list librw does not. Every camera it hands out is
// registered here, and RpWorldDestroy sweeps it, which restores the invariant
// RenderWare gives for free: once a world is gone, no camera claims to be in
// it.

#define RW_MAX_CAMERAS 16

static RwCamera* sCameras[RW_MAX_CAMERAS];

static void rwRegisterCamera(RwCamera* camera)
{
    for (RwInt32 i = 0; i < RW_MAX_CAMERAS; i++)
    {
        if (sCameras[i] == NULL)
        {
            sCameras[i] = camera;
            return;
        }
    }

    // Worth saying rather than growing silently: the game creates four (the
    // view, the shadow, iEnv's pipe and iModel's instancing camera), so a full
    // table means either a leak or a new camera nobody accounted for, and a
    // camera missing from here is one RpWorldDestroy cannot detach.
    printf("bfbb: more than %d cameras live at once; one will not be detached "
           "when its world is destroyed\n",
           RW_MAX_CAMERAS);
    fflush(stdout);
}

static void rwUnregisterCamera(RwCamera* camera)
{
    for (RwInt32 i = 0; i < RW_MAX_CAMERAS; i++)
    {
        if (sCameras[i] == camera)
        {
            sCameras[i] = NULL;
            return;
        }
    }
}

// Called by RpWorldDestroy, which is in world.cpp. Declared there rather than in
// a shared header because it is the only thing the two files pass between them.
void rwDetachCamerasFromWorld(void* world)
{
    for (RwInt32 i = 0; i < RW_MAX_CAMERAS; i++)
    {
        RwCamera* camera = sCameras[i];
        if (camera != NULL && camera->world == world)
        {
            // The camera is not removed from the world's own bookkeeping --
            // there is none to remove it from -- so clearing the pointer is the
            // whole operation.
            camera->world = NULL;
        }
    }
}

// ---------------------------------------------------------------------------

RwCamera* RwCameraCreate(void)
{
    RwCamera* camera = reinterpret_cast<RwCamera*>(rw::Camera::create());
    if (camera != NULL)
    {
        rwRegisterCamera(camera);
    }
    return camera;
}

RwBool RwCameraDestroy(RwCamera* camera)
{
    if (camera == NULL)
    {
        return FALSE;
    }

    rwUnregisterCamera(camera);

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

    // The frame boundary, and so where the port pumps its window.
    //
    // A Win32 window whose message queue is never drained stops repainting and
    // the OS marks the process as not responding, which ghosts the window and
    // looks exactly like the game hanging -- and only once it has focus, which
    // is when Windows starts checking. iWindow.h says as much; nothing was
    // calling iWindowPump.
    //
    // It has to happen here because the loop that would normally own the pump
    // is retail's, in zMenuLoop and zGameLoop, and that code is not the port's
    // to change. Presenting is the one thing every frame does exactly once on
    // every platform, so it is the honest place to hang a per-frame host
    // obligation. D3D wants the window pumped around Present in any case.
    iWindowPump();

    // And where the close button is answered, for the same reason: the loop
    // that would normally notice belongs to retail.
    //
    // This is NOT the shutdown iWindow.h describes. That one runs the game's
    // own exit path so that save-on-exit and the RenderWare teardown happen,
    // and reaching it needs a seam in zMainLoop that does not exist yet -- the
    // console never exits, so retail has no such path to hook. Until then the
    // choice is between a window whose close button does nothing at all and a
    // process that stops when asked, and the second is the better of the two.
    if (iWindowShouldClose())
    {
        printf("bfbb: window closed -- exiting. Note that the game's own "
               "save-on-exit does not run yet.\n");
        fflush(stdout);
        exit(0);
    }

    // Keep a copy of the frame, for the loading screen to stand on.
    //
    // Before the flip, because after it the back buffer's contents are the
    // driver's business -- the swap effect is DISCARD -- and because this is the
    // last moment the frame that was just drawn is still the frame buffer.
    //
    // It is here rather than at the scene change that wants it because by then
    // it is far too late: zGameExit has already taken the level down, and the
    // last frame of it exists nowhere but in the surface about to be presented.
    // One blit of the frame buffer per frame. See iSnapshot.h.
    iSnapshotCapture();

    // **The flip waits for the display, whatever the caller asked for.**
    //
    // iCamera.cpp:124 passes rwRASTERFLIPDONTWAIT, and librw honours it as
    // D3DPRESENT_INTERVAL_IMMEDIATE, so the game ran as fast as the GPU would
    // let it. On the console that flag costs nothing -- the GameCube's video
    // interface paces the frame whether RenderWare waits on it or not, and a
    // frame there is never shorter than a field.
    //
    // On a host it is not free, because of one line in retail's own loop.
    // zGame.cpp:559 treats a frame shorter than 10 microseconds as impossible
    // and substitutes a sixtieth of a second for it. Unlocked, this game
    // produces such frames constantly, and every one of them advances the
    // simulation by 1/60 s instead of by the time that actually passed -- at a
    // couple of thousand frames a second that is tens of seconds of game time
    // per real second. Pickups spinning like drills is what it looks like;
    // xEntPickup turns PI * dt per frame and means half a revolution a second.
    //
    // Waiting on the display is what the console does and what the game's
    // timing was written against. It also self-limits on any monitor: at 144 Hz
    // the frame is 7 ms, which is three orders of magnitude clear of that
    // threshold, so the game runs fast and correct rather than fast and wrong.
    asCamera(camera)->showRaster(flags | rwRASTERFLIPWAITVSYNC);

    // **And cap the rate, because waiting on the display is not the same as
    // running at the console's speed.**
    //
    // Waiting stopped the game running at thousands of frames a second, but it
    // paces to the MONITOR, and a 240 Hz monitor gives 240 frames a second --
    // four times what a GameCube title was built for. That is not free even
    // with a correct dt: every part of this game that counts frames rather
    // than seconds runs four times too fast, and the parts that do use dt
    // accumulate four times as much floating-point error per second.
    //
    // So the frame is also paced to 60 Hz, which is what the console's video
    // interface gave it. iVSync does exactly this for the loops that have no
    // renderer -- see iSystem.cpp -- and this is the same deadline arithmetic
    // for the loop that does: advance a deadline by one period and sleep to
    // it, dropping missed deadlines rather than trying to catch up, so one
    // slow frame does not become a burst of fast ones.
    //
    // Vsync is kept as well as the cap rather than instead of it. The cap sets
    // the rate; waiting on the display is what stops a frame being torn in
    // half, and on a 60 Hz monitor the two agree anyway.
    iWindowPaceFrame();

    // BFBB_FPS: how fast the port is ACTUALLY presenting.
    //
    // The frame rate is not cosmetic here. zGame.cpp:559 substitutes a
    // sixtieth of a second for any frame it measures at under ten
    // microseconds, so a game running far above the display's rate does not
    // merely look smooth, it runs its simulation too fast. When something
    // moves at the wrong speed this is the number that says whether the frame
    // rate or the thing itself is wrong, and it is the only way to tell
    // whether waiting on the display took effect at all.
    if (sReportFps)
    {
        // clock() rather than iHost or std::chrono: bfbb_rw links neither the
        // platform layer nor the C++ standard library, and over a one-second
        // window millisecond resolution is three digits more than this needs.
        static clock_t windowStart = 0;
        static U32 frames = 0;

        clock_t now = clock();
        if (windowStart == 0)
        {
            windowStart = now;
        }

        frames++;

        double seconds = (double)(now - windowStart) / (double)CLOCKS_PER_SEC;
        if (seconds >= 1.0)
        {
            printf("bfbb: %.1f fps (%.2f ms/frame, %u presents in %.2f s)\n",
                   frames / seconds, (seconds * 1000.0) / frames, frames, seconds);
            fflush(stdout);
            windowStart = now;
            frames = 0;
        }
    }

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
