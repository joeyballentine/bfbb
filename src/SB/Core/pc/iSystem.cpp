#include "iSystem.h"
#include "iConfig.h"
#include "iDrawDist.h"
#include "iFile.h"
#include "iHost.h"
#include "iPad.h"
#include "iPadHost.h"
#include "iScreen.h"
#include "iTRC.h"
#include "iSnapshot.h"
#include "iTextPatch.h"
#include "iTime.h"

#include <types.h>

#include <stdio.h>

#include "iCamera.h"
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

// After the RenderWare headers: both name RwCamera in their signatures and
// neither includes rwcore.h itself, matching every other i* header here.
#include "iDistort.h"
#include "iGlow.h"

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

// How the window presents itself, from config.ini's video.mode.
//
// Held here rather than read in RenderWareInit so that an unrecognised value is
// reported once, alongside the other settings, rather than at the moment the
// window opens. Everything else in this file is decided before anything can use
// it, and this is no different.
static iWindowMode sWindowMode = iWINDOW_FULLSCREEN;

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
    // The window, where VIInit was. It opens at the render size because that is
    // the least surprising thing to do, not because anything requires it: the
    // port draws into a virtual screen which is scaled into the back buffer at
    // present time, so the two are independent from here on and the window may
    // be resized freely.
    iWindowParams windowParams;
    windowParams.title = "SpongeBob SquarePants: Battle for Bikini Bottom";
    windowParams.width = iScreenWidth();
    windowParams.height = iScreenHeight();
    windowParams.mode = sWindowMode;
    if (!iWindowOpen(&windowParams))
    {
        printf("bfbb:   the window could not be opened\n");
        return TRUE;
    }

    // WINDOWED ONLY: render at what the window actually gave.
    //
    // A windowed window is opened at the render size, so if Windows hands back
    // something else -- a size it would not make, a DPI it disagreed about --
    // then following it is better than scaling to it, and costs nothing.
    //
    // Borderless and exclusive fullscreen cover a monitor, and their client
    // size has nothing to do with what the game should render at. Reconciling
    // there would make `mode = fullscreen` quietly override the resolution
    // setting with the desktop's, which is the opposite of what the virtual
    // screen exists for.
    if (sWindowMode == iWINDOW_WINDOWED)
    {
        S32 clientWidth = 0;
        S32 clientHeight = 0;
        iWindowGetSize(&clientWidth, &clientHeight);

        if (clientWidth > 0 && clientHeight > 0 &&
            (clientWidth != iScreenWidth() || clientHeight != iScreenHeight()))
        {
            printf("bfbb: asked for a %dx%d window and got %dx%d; rendering at that\n",
                   (int)iScreenWidth(), (int)iScreenHeight(), (int)clientWidth,
                   (int)clientHeight);
            iScreenSetSize(clientWidth, clientHeight);
        }
    }

    printf("bfbb: rendering at %dx%d\n", (int)iScreenWidth(), (int)iScreenHeight());

    // NULL rather than psGetMemoryFunctions(): that is the console's hook for
    // handing RenderWare the game's own allocator, and the port has no
    // equivalent yet. librw falls back to malloc, so RenderWare's allocations
    // do not come out of the game's heap and xMemMgr's accounting cannot see
    // them. Worth fixing before anything measures memory.
    if (!RwEngineInit(NULL, 0, 0x60000))
    {
        printf("bfbb:   RwEngineInit failed\n");
        return TRUE;
    }
    RwResourcesSetArenaSize(0x60000);
    if (RWAttachPlugins())
    {
        printf("bfbb:   a RenderWare plugin failed to attach\n");
        return TRUE;
    }

    // displayID is a RwGameCubeDeviceConfig* on the console and has no host
    // counterpart. The shim builds librw's own EngineOpenParams from the window
    // opened above and ignores what is passed here; see rw/engine_start.cpp.
    RwEngineOpenParams params;
    params.displayID = NULL;
    if (!RwEngineOpen(&params))
    {
        printf("bfbb:   RwEngineOpen failed -- see the line above for why\n");
        RwEngineTerm();
        return TRUE;
    }
    RwEngineGetVideoModeInfo(&sVideoMode, RwEngineGetCurrentVideoMode());
    if (!RwEngineStart())
    {
        printf("bfbb:   RwEngineStart failed\n");
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
    else if (getenv("BFBB_TEX") != NULL)
    {
        // BFBB_TEX: a material named a texture the asset store does not have.
        //
        // Retail returns null here too and the material renders untextured, so
        // this is not necessarily wrong -- but a texture that is missing
        // because its RWTX was never loaded looks exactly like one that failed
        // to convert, and the two have nothing in common. Naming it separates
        // them.
        printf("bfbb: texture '%s' not in the asset store\n", name);
        fflush(stdout);
    }

    return asset;
}

// The settings that have to be decided before anything can use them.
//
// The three render features are PUSHED here rather than read where they are
// used: glow.cpp, distort.cpp and snapshot.cpp compile into bfbb_rw, which does
// not link the platform layer -- rw_selftest links bfbb_rw alone, and that is
// worth keeping true. So the RenderWare shim never learns what a setting is,
// and this is the one place that knows which switch drives which feature.
//
// The cave reverb is not here: iSnd.cpp is in this library and asks iConfig
// directly.
static iWindowMode WindowModeFromConfig()
{
    const char* name = iConfigGetString("video.mode", "fullscreen");

    if (iHostStrCaseCmp(name, "fullscreen") == 0)
    {
        return iWINDOW_FULLSCREEN;
    }
    if (iHostStrCaseCmp(name, "borderless") == 0)
    {
        return iWINDOW_BORDERLESS;
    }
    if (iHostStrCaseCmp(name, "windowed") == 0)
    {
        return iWINDOW_WINDOWED;
    }

    printf("bfbb: config: video.mode is not fullscreen, borderless or windowed, using "
           "the default: %s\n",
           name);
    return iWINDOW_FULLSCREEN;
}

static const char* WindowModeName(iWindowMode mode)
{
    switch (mode)
    {
    case iWINDOW_FULLSCREEN:
        return "fullscreen";
    case iWINDOW_BORDERLESS:
        return "borderless";
    default:
        return "windowed";
    }
}

static void ApplyConfig()
{
    sWindowMode = WindowModeFromConfig();

    // The render size, before RenderWareInit opens the window at it. Pushed the
    // same way the three render features are, and for a stronger reason: iScreen
    // is read by game code, which must not learn what config.ini is.
    iScreenSetSize(iConfigGetInt("video.width", 640), iConfigGetInt("video.height", 480));

    // How the interface sits on a screen that is not 4:3. Nothing to report
    // when it cannot matter, which is every 4:3 render size.
    const char* uiMode = iConfigGetString("video.ui", "native");
    if (iHostStrCaseCmp(uiMode, "pillarbox") == 0)
    {
        iScreenSetUIMode(iSCREENUI_PILLARBOX);
    }
    else
    {
        if (iHostStrCaseCmp(uiMode, "native") != 0)
        {
            printf("bfbb: config: video.ui is not native or pillarbox, using the default: %s\n",
                   uiMode);
        }
        iScreenSetUIMode(iSCREENUI_NATIVE);
    }

    // The draw distance, before iCameraCreate builds the first frustum. Pushed
    // into iCamera as well as into iDrawDist because the far clip is a value the
    // camera holds rather than one it asks for each frame; the wrapped distances
    // in zLOD and zEntSimpleObj read the switch itself, at scene setup.
    S32 drawDistance = iConfigGetBool("video.draw_distance", TRUE);
    iDrawDistSetUnlimited(drawDistance);
    iCameraSetNearFarClip(0.0f, iDrawDistFarClip());

    S32 glow = iConfigGetBool("xbox.glow", TRUE);
    S32 distortion = iConfigGetBool("xbox.distortion", TRUE);
    S32 snapshot = iConfigGetBool("xbox.snapshot", TRUE);
    S32 reverb = iConfigGetBool("xbox.reverb", TRUE);

    iGlowSetEnabled(glow);
    iDistortSetEnabled(distortion);
    iSnapshotSetEnabled(snapshot);

    // Pushed for the same reason, one library further out: zAssetTypes.cpp
    // calls the patcher from the game code, and game code must not learn what
    // config.ini is.
    S32 wording = iConfigGetBool("text.platform_wording", TRUE);
    iTextPatchSetEnabled(wording);

    // Said out loud, and always, because these change what the game looks and
    // sounds like. Someone reporting that the port looks wrong should not have
    // to be asked whether they have a config.ini -- the log already says.
    const char* path = iConfigPath();
    printf("bfbb: %s -- %s; draw distance %s; Xbox features: glow %s, distortion %s, "
           "snapshot %s, reverb %s\n",
           path != NULL ? path : "no config.ini, defaults", WindowModeName(sWindowMode),
           drawDistance ? "unlimited" : "console", glow ? "on" : "off",
           distortion ? "on" : "off", snapshot ? "on" : "off", reverb ? "on" : "off");
    printf("bfbb: text rewritten for a PC: %s\n", wording ? "on" : "off");
}

void iSystemInit(U32 options)
{
    // First, before iFileInit reads where the assets are and long before
    // RenderWareInit opens a window: everything below may want a setting.
    iConfigLoad();
    ApplyConfig();

    xDebugInit();
    xMemInit();
    iFileInit();

    // The assets, before anything asks for one and before the window opens.
    //
    // Before anything asks, because the first thing that does is
    // zMainLoadFontHIP, and it ends in `do { } while (xSTLoadStep('FONT') <
    // 1.0f);` -- retail's loop, with no exit and nobody to return a failure to.
    // A wrong BFBB_ASSETS used to reach that loop and sit there, so the game
    // looked like it had hung on a blank window when what it had actually done
    // was fail to find a single file.
    //
    // Before the window opens, so the dialog is not behind a fullscreen one.
    {
        const char* missing = iFileMissingAssetPath();
        if (missing != NULL)
        {
            const char* root = getenv("BFBB_ASSETS");

            char message[1024];
            snprintf(message, sizeof(message),
                     "The game's files were not found.\n\n"
                     "Looked for:\n%s\n\n"
                     "BFBB_ASSETS is %s%s%s\n\n"
                     "It has to name the folder that DIRECTLY contains boot.HIP, "
                     "font.HIP and fmv\\ -- not a folder above that one, and not a "
                     "disc image. No assets ship with the port; they come from your "
                     "own copy of the Xbox release.",
                     missing, root != NULL && root[0] != '\0' ? "\"" : "",
                     root != NULL && root[0] != '\0' ? root : "not set",
                     root != NULL && root[0] != '\0' ? "\"" : "");

            printf("bfbb: FATAL -- the game's files were not found.\n");
            printf("bfbb:   looked for: %s\n", missing);
            printf("bfbb:   BFBB_ASSETS: %s\n",
                   root != NULL && root[0] != '\0' ? root : "not set");
            printf("bfbb:   it must name the folder that DIRECTLY contains boot.HIP, "
                   "font.HIP and fmv/\n");
            fflush(stdout);

            iHostErrorBox("SpongeBob SquarePants: Battle for Bikini Bottom", message);
            exit(1);
        }
    }

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
