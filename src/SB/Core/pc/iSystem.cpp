#include "iSystem.h"
#include "iFile.h"
#include "iHost.h"
#include "iPad.h"
#include "iPadHost.h"
#include "iTRC.h"
#include "iTime.h"

#include <types.h>

#include <stdio.h>

#include "xDebug.h"
#include "xMath.h"
#include "xMath3.h"
#include "xMemMgr.h"
#include "xPad.h"
#include "xSnd.h"
#include "xstransvc.h"
#include "xString.h"
#include "iWindow.h"

#include <rwcore.h>
#include <rpworld.h>
#include <rpcollis.h>
#include <rphanim.h>
#include <rpmatfx.h>
#include <rpptank.h>
#include <rpskin.h>
#include <rpusrdat.h>

#include "xShadow.h"
#include "xFX.h"

#include <windows.h>

#include <stdlib.h>
#include <string.h>

// Retail waits on the video interface's vertical retrace, which is what paces
// the whole game -- iVSync is the only thing standing between the update loop
// and running as fast as the CPU allows.
//
// The port has no renderer yet, so there is no retrace to wait for. Returning
// immediately would not be a placeholder, it would be a behaviour change: the
// game would run its simulation at thousands of hertz. Sleeping to the next
// 60 Hz boundary is what "wait for vertical retrace" means on a 60 Hz display,
// so that is what this does until the renderer can supply a real one.
#define IVSYNC_PERIOD_NS (1000000000 / 60)

static bool sVSyncStarted;
static U64 sNextVSync;

void iVSync()
{
    U64 now = iHostMonotonicNs();

    if (!sVSyncStarted)
    {
        sNextVSync = now;
        sVSyncStarted = true;
    }

    sNextVSync += IVSYNC_PERIOD_NS;

    // A frame that overran its budget must not try to catch up by not sleeping
    // for the next several -- that turns one slow frame into a burst. Drop the
    // missed deadlines and pace from now instead.
    if (sNextVSync < now)
    {
        sNextVSync = now;
        return;
    }

    iHostSleepUntilNs(sNextVSync);
}

// Retail installs this as the floating-point error handler's tail call, purely
// so the handler has something non-inlinable to call. Nothing depends on what
// it does, because it does nothing.
void null_func()
{
}

static void TRCInit()
{
    iTRCDisk::Init();

    // The GameCube's TRCInit also registers six hooks -- stop rumbling,
    // suspend and resume sound, kill sound, suspend and resume the movie --
    // that the disc-error screen calls before and after it takes over the
    // display. There is no disc to eject here and no error screen to show, so
    // there is nothing to suspend and no hooks to register.
}

// RenderWare startup, ported from gc/iSystem.cpp.
//
// The sequence is retail's, with the console-only steps dropped and one step
// added. Gone: GXSetMisc/OSInit/DVDInit/VIInit and the OS error handlers, which
// are the console booting itself; DolphinInitMemorySystem and
// DolphinInstallFileSystem, which hand RenderWare the GameCube's allocator and
// DVD reader. New is the window -- a host has to own one before a renderer can
// open on it, and it goes exactly where VIInit went.
//
// The plugin attaches keep retail's order and stay strictly between
// RwEngineInit and RwEngineOpen. That is not tidiness: every one of them grows
// an atomic, a geometry, a material or a frame, and Engine::open freezes those
// sizes. Attaching after open hands out plugin offsets past the end of every
// object allocated afterwards.

static RwVideoMode sVideoMode;

static RwTexture* TextureRead(const RwChar* name, const RwChar* maskName);

static U32 RWAttachPlugins()
{
    // Returns TRUE on FAILURE, which is retail's convention here and reads
    // backwards, so it is worth saying out loud.
    if (!RpWorldPluginAttach())
    {
        return TRUE;
    }
    if (!RpCollisionPluginAttach())
    {
        return TRUE;
    }
    if (!RpSkinPluginAttach())
    {
        return TRUE;
    }
    if (!RpHAnimPluginAttach())
    {
        return TRUE;
    }
    if (!RpMatFXPluginAttach())
    {
        return TRUE;
    }
    if (!RpUserDataPluginAttach())
    {
        return TRUE;
    }
    if (!RpPTankPluginAttach())
    {
        return TRUE;
    }
    return FALSE;
}

static S32 RenderWareInit()
{
    // The window, where VIInit was. 640x480 is retail's framebuffer; nothing in
    // the port is resolution-independent yet, and xScrFx sizes its full-screen
    // rectangles from the video mode this produces.
    iWindowParams windowParams;
    windowParams.title = "SpongeBob SquarePants: Battle for Bikini Bottom";
    windowParams.width = 640;
    windowParams.height = 480;
    windowParams.fullscreen = false;
    if (!iWindowOpen(&windowParams))
    {
        return TRUE;
    }

    // NULL rather than psGetMemoryFunctions(): that is the console's hook for
    // handing RenderWare the game's own allocator, and the port has no
    // equivalent yet. librw falls back to malloc, so RenderWare's allocations
    // do not come out of the game's heap and xMemMgr's accounting cannot see
    // them. Worth fixing before anything measures memory.
    if (!RwEngineInit(NULL, 0, 0x60000))
    {
        return TRUE;
    }
    RwResourcesSetArenaSize(0x60000);
    if (RWAttachPlugins())
    {
        return TRUE;
    }

    // displayID is a RwGameCubeDeviceConfig* on the console and has no host
    // counterpart. The shim builds librw's own EngineOpenParams from the window
    // opened above and ignores what is passed here; see rw/engine_start.cpp.
    RwEngineOpenParams params;
    params.displayID = NULL;
    if (!RwEngineOpen(&params))
    {
        RwEngineTerm();
        return TRUE;
    }
    RwEngineGetVideoModeInfo(&sVideoMode, RwEngineGetCurrentVideoMode());
    if (!RwEngineStart())
    {
        RwEngineClose();
        RwEngineTerm();
        return TRUE;
    }
    RwTextureSetReadCallBack(TextureRead);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
    xShadowInit();
    xFXInit();
    RwTextureSetMipmapping(TRUE);
    RwTextureSetAutoMipmapping(TRUE);

    printf("bfbb: RenderWare up, %dx%d\n", (int)sVideoMode.width, (int)sVideoMode.height);
    return FALSE;
}

// Resolves a texture named inside a model's material list out of the game's own
// asset store. Without it every textured model comes back untextured.
//
// Retail's version has a GameCube branch this one does not: it rejects a raster
// under 8 bits deep unless the GameCube raster extension reports format 14,
// which is that driver's compressed-texture check. There is no such extension
// here -- the port's rasters are whatever xbox_to_d3d produced -- so the depth
// test has nothing to ask and is dropped rather than guessed at.
static RwTexture* TextureRead(const RwChar* name, const RwChar* maskName)
{
    char buf[0x100];
    sprintf(buf, "%s.rw3", name);

    U32 assetSize;
    RwTexture* asset = (RwTexture*)xSTFindAsset(xStrHash(buf), &assetSize);

    if (asset != NULL)
    {
        strcpy(asset->name, name);
        strcpy(asset->mask, maskName);
    }

    return asset;
}

void iSystemInit(U32 options)
{
    xDebugInit();
    xMemInit();
    iFileInit();
    iTimeInit();
    xPadInit();
    xSndInit();
    TRCInit();

    printf("bfbb: platform layer up, input backend: %s\n", iPadHostName());

    if (RenderWareInit())
    {
        // Retail OSPanics on a failed RenderWare startup. There is no OSPanic
        // here and no console to hold the message, so it says so and stops --
        // continuing would fault in the first thing that touches the engine,
        // several hundred lines further on, with nothing to point at.
        printf("bfbb: FATAL -- RenderWare failed to start\n");
        exit(1);
    }

    xMathInit();
    xMath3Init();
}

void iSystemExit()
{
    xDebugExit();
    xMathExit();

    // TODO(pcport): RenderWareExit() belongs here, matching iSystemInit.

    xSndExit();
    xPadKill();
    iFileExit();
    iTimeExit();
    xMemExit();

    // Retail ends on OSPanic, which halts the console -- the only way a
    // GameCube title exits. A host process returns.
}
