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
#include "iPadGlyph.h"
#include "iSoundtrack.h"
#include "iTextPatch.h"
#include "iTime.h"
#include "iLoadScreen.h"

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

// How fast the game runs and whether the present waits for the display, from
// config.ini's video.framerate and video.vsync.
//
// Pushed into iWindow rather than read there for the reason above: the window
// compiles into bfbb_rw, which does not link the platform layer and so cannot
// ask iConfig anything.
//
// Called after the window is open, because `display` means the refresh rate of
// the monitor the game landed on, and before it opens there is no such monitor.
static void ApplyDisplayRateConfig()
{
    iWindowSetVSync(iConfigGetBool("video.vsync", TRUE));

    const char* rate = iConfigGetString("video.framerate", "60");
    S32 fps;

    if (iHostStrCaseCmp(rate, "display") == 0)
    {
        fps = iWindowGetDisplayRefreshRate();
        if (fps <= 0)
        {
            // No cap rather than 60: vsync is the pacer the setting was asking
            // for, and it is still doing its job. Capping to a number the
            // display did not give would be inventing one.
            printf("bfbb: config: video.framerate = display, but the monitor's "
                   "refresh rate could not be read; not capping\n");
            fps = 0;
        }
    }
    else if (iHostStrCaseCmp(rate, "off") == 0 || iHostStrCaseCmp(rate, "unlimited") == 0 ||
             iHostStrCaseCmp(rate, "none") == 0)
    {
        fps = 0;
    }
    else
    {
        // A value that is neither a number nor one of the words above is
        // reported by iConfigGetInt and answered with 60.
        fps = iConfigGetInt("video.framerate", 60);
        if (fps < 0)
        {
            printf("bfbb: config: video.framerate cannot be negative, using the "
                   "default: %s\n",
                   rate);
            fps = 60;
        }
    }

    iWindowSetFrameRate(fps);

    if (fps > 0)
    {
        printf("bfbb: %d fps cap, vsync %s\n", (int)fps, iWindowGetVSync() ? "on" : "off");
    }
    else
    {
        printf("bfbb: no fps cap, vsync %s\n", iWindowGetVSync() ? "on" : "off");
    }
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

    // After the engine, not beside the window request above: under GL3 the
    // window belongs to librw and does not exist until RwEngineStart has run,
    // so `framerate = display` asked before this point would read the primary
    // monitor's rate rather than the one the game actually landed on. Under
    // D3D9 the window exists either side of this and the answer is the same.
    ApplyDisplayRateConfig();

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

// The size of the shadow raster, from config.ini's video.shadow_resolution.
// 0 means auto: derive it from the render size instead of pinning a number.
// Read back through iShadowResolution rather than pushed, because xShadowInit
// builds the shadow camera from game code long after this runs -- which also
// means auto sees the size the window actually opened at, not the one that was
// asked for.
static S32 sShadowResPinned = 0;

// The console rendered a 256-pixel shadow into a 480-pixel picture. Auto keeps
// that ratio: half the render height, rounded UP to a power of two, so the
// shadow is never coarser than the console's relative to the picture it is
// drawn in. 480 lands back on exactly 256, which is the number retail used.
//
//   480 -> 256    720 -> 512    1080 -> 1024    1440 -> 1024    2160 -> 2048
//
// SetupShadow then holds the result to no more than the render size, so a tall
// narrow window cannot ask for a raster wider than the framebuffer.
static S32 ShadowResolutionAuto()
{
    S32 height = iScreenHeight();

    // Before iScreenSetSize there is no picture to scale against. Retail's
    // number is the honest answer, not a guess at one.
    if (height <= 0)
    {
        return 256;
    }

    S32 want = height / 2;
    S32 res = 64;

    while (res < want && res < 4096)
    {
        res <<= 1;
    }

    return res;
}

static S32 ShadowResolutionFromConfig()
{
    const char* value = iConfigGetString("video.shadow_resolution", "auto");

    if (iHostStrCaseCmp(value, "auto") == 0)
    {
        return 0;
    }

    // Anything else is a power of two, 64 to 4096, and is REJECTED rather than
    // clamped or rounded: D3D9 refuses a non-power-of-two render target on
    // hardware without that capability, and the refusal arrives as a shadow
    // camera that failed to build -- no character shadows at all, and nothing
    // anywhere naming the setting that caused it.
    char* end;
    long res = strtol(value, &end, 10);

    if (end != value && *end == '\0' && res >= 64 && res <= 4096 && (res & (res - 1)) == 0)
    {
        return (S32)res;
    }

    printf("bfbb: config: video.shadow_resolution is not auto or a power of two from 64 to "
           "4096, using the default: %s\n",
           value);
    return 0;
}

S32 iShadowResolution()
{
    return sShadowResPinned != 0 ? sShadowResPinned : ShadowResolutionAuto();
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
    sShadowResPinned = ShadowResolutionFromConfig();

    // The render size, before RenderWareInit opens the window at it. Pushed the
    // same way the three render features are, and for a stronger reason: iScreen
    // is read by game code, which must not learn what config.ini is.
    iScreenSetSize(iConfigGetInt("video.width", 640), iConfigGetInt("video.height", 480));
    iScreenSetMultiSample(iConfigGetInt("video.msaa", 4));
    iScreenSetAlphaToCoverage(iConfigGetBool("video.alpha_to_coverage", TRUE));
    iScreenSetPerPixelLighting(iConfigGetBool("video.per_pixel_lighting", TRUE));

    // How the interface sits on a screen that is not 4:3. Nothing to report
    // when it cannot matter, which is every 4:3 render size.
    const char* uiMode = iConfigGetString("video.ui", "pillarbox");
    if (iHostStrCaseCmp(uiMode, "native") == 0)
    {
        iScreenSetUIMode(iSCREENUI_NATIVE);
    }
    else
    {
        if (iHostStrCaseCmp(uiMode, "pillarbox") != 0)
        {
            printf("bfbb: config: video.ui is not pillarbox or native, using the default: %s\n",
                   uiMode);
        }
        iScreenSetUIMode(iSCREENUI_PILLARBOX);
    }

    // The draw distance, before iCameraCreate builds the first frustum. Pushed
    // into iCamera as well as into iDrawDist because the far clip is a value the
    // camera holds rather than one it asks for each frame; the wrapped distances
    // in zLOD and zEntSimpleObj read the switch itself, at scene setup.
    S32 drawDistance = iConfigGetBool("video.draw_distance", TRUE);
    iDrawDistSetUnlimited(drawDistance);
    iCameraSetNearFarClip(0.0f, iDrawDistFarClip());

    // Where the music may be replaced from. Pushed rather than read, like the
    // text patch: nothing under here knows what config.ini is. The folder is
    // not scanned until the first sound asks for its bytes, so naming one costs
    // nothing at startup.
    iSoundtrackSetFolder(iConfigGetString("audio.soundtrack", ""));

    // Which controller's buttons the prompts draw. Pushed the same way, and
    // told where to look separately: the sets ship beside the executable, not
    // under the assets folder, because they are the port's files rather than
    // the game's. A player who moved them can say so in the setting by naming a
    // folder that is already there.
    const char* icons = iConfigGetString("input.button_icons", "auto");
    char beside[512];
    if (iHostExeDir(beside, sizeof(beside)))
    {
        iPadGlyphSetRoot(beside);
    }
    iPadGlyphSetChoice(icons);
    iPadGlyphSetEnabled(iHostStrCaseCmp(icons, "off") != 0);

    // How long the loading screen is up for at the least. Pushed like the
    // rest; the floor is held in zSceneInit, which is game code.
    const char* loadTime = iConfigGetString("video.load_time", "1");
    F32 loadSeconds;
    if (iHostStrCaseCmp(loadTime, "off") == 0 || iHostStrCaseCmp(loadTime, "none") == 0)
    {
        loadSeconds = 0.0f;
    }
    else
    {
        loadSeconds = iConfigGetFloat("video.load_time", 1.0f);
        if (loadSeconds < 0.0f)
        {
            printf("bfbb: config: video.load_time cannot be negative, using the "
                   "default: %s\n",
                   loadTime);
            loadSeconds = 1.0f;
        }
    }
    iLoadScreenSetMinTime(loadSeconds);

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
    // The shadow size is said with where it came from, because auto and a
    // pinned number that happen to agree are worth telling apart when someone
    // asks why their shadows changed after they resized the window.
    char shadows[24];
    sprintf(shadows, "%d%s", (int)iShadowResolution(), sShadowResPinned != 0 ? "" : " (auto)");

    printf("bfbb: %s -- %s; draw distance %s; shadows %s; Xbox features: "
           "glow %s, distortion %s, snapshot %s, reverb %s\n",
           path != NULL ? path : "no config.ini, defaults", WindowModeName(sWindowMode),
           drawDistance ? "unlimited" : "console", shadows, glow ? "on" : "off",
           distortion ? "on" : "off", snapshot ? "on" : "off", reverb ? "on" : "off");
    printf("bfbb: text rewritten for a PC: %s\n", wording ? "on" : "off");
    if (loadSeconds > 0.0f)
    {
        printf("bfbb: loading screen up for at least %.2f s\n", (double)loadSeconds);
    }
    else
    {
        printf("bfbb: loading screen up for as long as the load takes\n");
    }
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
    // A wrong asset path used to reach that loop and sit there, so the game
    // looked like it had hung on a blank window when what it had actually done
    // was fail to find a single file.
    //
    // Before the window opens, so the dialog is not behind a fullscreen one.
    {
        const char* missing = iFileMissingAssetPath();
        if (missing != NULL)
        {
            const char* root = iFileAssetRoot();
            const char* env = getenv("BFBB_ASSETS");
            const char* config = iConfigPath();

            // Which of the two to go and edit. Someone who has never set
            // BFBB_ASSETS should be sent to their config.ini by name rather
            // than to an environment variable they have never heard of, and
            // someone who HAS set one should be told that it is what is in
            // force -- otherwise they edit the file and nothing changes.
            char where[768];
            if (env != NULL && env[0] != '\0')
            {
                snprintf(where, sizeof(where),
                         "BFBB_ASSETS is set to \"%s\", and overrides the settings file.",
                         root);
            }
            else if (root[0] != '\0')
            {
                snprintf(where, sizeof(where), "[assets] path in %s is \"%s\".",
                         config != NULL ? config : "config.ini", root);
            }
            else
            {
                snprintf(where, sizeof(where), "[assets] path in %s is empty; set it there.",
                         config != NULL ? config : "config.ini");
            }

            char message[1536];
            snprintf(message, sizeof(message),
                     "The game's files were not found.\n\n"
                     "Looked for:\n%s\n\n"
                     "%s\n\n"
                     "It has to name the folder that DIRECTLY contains boot.HIP, "
                     "font.HIP and fmv\\ -- not a folder above that one, and not a "
                     "disc image. No assets ship with the port; they come from your "
                     "own copy of the Xbox release.",
                     missing, where);

            printf("bfbb: FATAL -- the game's files were not found.\n");
            printf("bfbb:   looked for: %s\n", missing);
            printf("bfbb:   %s\n", where);
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

    // The reference every alpha test is measured against, which librw leaves
    // at 10 and the console leaves at 1. Only now: it is a render state, and
    // until RenderWareInit returns there is no device to set one on.
    // renderstate.cpp says why the value matters.
    //
    // Declared here rather than in a header for the same reason
    // rwSetColorWriteMask is at iDraw.cpp:80 -- one seam, one caller.
    {
        void rwSetConsoleAlphaTest(void);
        rwSetConsoleAlphaTest();
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
    iPadGlyphExit();
    iFileExit();
    iTimeExit();
    xMemExit();

    // Retail ends on OSPanic, which halts the console -- the only way a
    // GameCube title exits. A host process returns.
}
