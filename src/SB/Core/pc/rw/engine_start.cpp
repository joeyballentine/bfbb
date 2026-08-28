// RenderWare C API: bringing librw up, and RwEngineInstance.
//
// Nothing else in this directory can run until this does. librw's objects are
// allocated out of plugin-extended blocks whose sizes are only known after the
// modules have registered, and Frame::updateObjects walks `rw::engine`'s dirty
// list -- so a Frame::create() against a dead engine either allocates zero
// bytes or dereferences a null engine. Both were observed before this file
// existed.
//
// librw's sequence is init -> open -> start, and it is not optional:
//
//   Engine::init()   installs the memory functions, opens the plugin list and
//                    registers the core modules (Frame, Image, Raster,
//                    Texture) plus every platform's driver plugins. After this
//                    the per-object plugin sizes are still growing.
//   Engine::open()   allocates `rw::engine` itself, points it at the render
//                    device, and allocates the per-platform Driver blocks.
//                    Object sizes are frozen here.
//   Engine::start()  runs the plugin constructors -- which is what finally
//                    calls frameOpen() to initialise engine->frameDirtyList --
//                    and registers the image file formats.
//
// That maps one-to-one onto RenderWare's own RwEngineInit/Open/Start, including
// the rule that plugin attaches (RpWorldPluginAttach and friends, librw's
// rw::registerHAnimPlugin() and friends) go strictly between init and open.
// The GameCube's RenderWareInit in iSystem.cpp already sequences it that way,
// so it needs no changes to work here.

#include <rwcore.h>

// BEFORE librw's headers, and that ordering is load-bearing: rwd3d.h declares
// EngineOpenParams as { HWND window; } only when _D3D9_H_ is already defined,
// and as { uint32 please_include_windows_h; } otherwise. Including these after
// rw.h gives the second one and a compile error that names the fix.
#if defined(RW_D3D9) || defined(RW_D3D8)
#include <windows.h>
#include <d3d9.h>
#endif

#include "rw.h"

#include "iWindow.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Memory
//
// The two libraries disagree about allocator signatures: RenderWare's take a
// size, librw's also take a duration hint (MEMDUR_FRAME, MEMDUR_GLOBAL, ...)
// used only by its debug allocation tracker. Since the game's allocator has no
// notion of duration there is nothing to map the hint onto, and dropping it
// loses nothing -- librw's own default memfuncs drop it too.
//
// The indirection exists so there is exactly ONE allocator: whatever the game
// passed to RwEngineInit is what librw allocates from, and it is also what the
// RwMalloc/RwFree macros reach through RWSRCGLOBAL(memoryFuncs).

static void* shimMalloc(size_t size, rw::uint32 hint)
{
    return RWSRCGLOBAL(memoryFuncs).rwmalloc(size);
}

static void* shimRealloc(void* mem, size_t size, rw::uint32 hint)
{
    return RWSRCGLOBAL(memoryFuncs).rwrealloc(mem, size);
}

static void shimFree(void* mem)
{
    RWSRCGLOBAL(memoryFuncs).rwfree(mem);
}

// RwEngineInit accepts a null memFuncs, and RenderWare then uses the C library.
// Spelling that out here rather than passing null through to librw keeps the
// single-allocator property: the RwMalloc macros and librw still agree.
static const RwMemoryFunctions sCLibraryMemoryFunctions = { malloc, free, realloc, calloc };

// ---------------------------------------------------------------------------
// Strings
//
// RWSRCGLOBAL(stringFuncs) is a full dispatch table in retail, and xFX.cpp
// calls through it directly (`RwEngineInstance->stringFuncs.vecStrcmp`). The
// table is filled in completely rather than only where there is a caller
// today, because a half-filled one is a null pointer waiting for whichever
// call site is ported next.
//
// The three search functions need wrappers: C++ overloads strchr/strrchr/strstr
// on constness, so neither overload has RenderWare's `RwChar*(const RwChar*)`
// signature and the table cannot take their address directly.

static RwChar* shimStrrchr(const RwChar* string, int findThis)
{
    return const_cast<RwChar*>(strrchr(string, findThis));
}

static RwChar* shimStrchr(const RwChar* string, int findThis)
{
    return const_cast<RwChar*>(strchr(string, findThis));
}

static RwChar* shimStrstr(const RwChar* string, const RwChar* findThis)
{
    return const_cast<RwChar*>(strstr(string, findThis));
}

// _strupr/_strlwr are Microsoft's, and the port also builds against POSIX (see
// iHostPosix.cpp), which has no portable spelling of either. Four lines each is
// cheaper than another compat header.
static RwChar* shimStrupr(RwChar* string)
{
    for (RwChar* p = string; *p != '\0'; p++)
    {
        if (*p >= 'a' && *p <= 'z')
        {
            *p = (RwChar)(*p - ('a' - 'A'));
        }
    }
    return string;
}

static RwChar* shimStrlwr(RwChar* string)
{
    for (RwChar* p = string; *p != '\0'; p++)
    {
        if (*p >= 'A' && *p <= 'Z')
        {
            *p = (RwChar)(*p + ('a' - 'A'));
        }
    }
    return string;
}

// stricmp comes from the compat layer (src/SB/Core/pc/compat/string.h), which
// is the one of these with no portable spelling.
static const RwStringFunctions sStringFunctions = {
    sprintf,     vsprintf,   strcpy,     strncpy, strcat,  strncat,
    shimStrrchr, shimStrchr, shimStrstr, strcmp,  strncmp, stricmp,
    strlen,      shimStrupr, shimStrlwr, strtok,  sscanf,
};

// ---------------------------------------------------------------------------
// RwEngineInstance
//
// RenderWare allocates this; the shim gives it static storage instead, because
// there is no plugin mechanism extending RwGlobals on this side -- librw's
// engine plugins extend `rw::engine`, which is a different object. The pointer
// is still nulled by RwEngineTerm so that a use-after-term faults where it
// happens rather than reading a stale but plausible struct.
//
// RwGlobals and rw::Engine agree on their first two fields and on nothing after
// them, so this is a second structure and not an alias. What that costs is
// listed field by field in RwEngineInit.

static RwGlobals sGlobals;

RwGlobals* RwEngineInstance;

RwBool RwEngineInit(const RwMemoryFunctions* memFuncs, RwUInt32 initFlags, RwUInt32 resArenaSize)
{
    if (RwEngineInstance != NULL)
    {
        return FALSE;
    }

    memset(&sGlobals, 0, sizeof(sGlobals));

    sGlobals.memoryFuncs = (memFuncs != NULL) ? *memFuncs : sCLibraryMemoryFunctions;
    sGlobals.stringFuncs = sStringFunctions;

    // RwGlobals::resArenaInitSize is where RenderWare records this, and the
    // GameCube passes 0x60000. librw has no resource arena at all -- it
    // allocates raster and geometry instance data on demand -- so the number is
    // recorded and nothing sizes anything from it.
    sGlobals.resArenaInitSize = resArenaSize;

    // Not filled in, each for a reason, and each left null so that a call
    // through it faults at the call site instead of reading a wrong value:
    //
    //   memoryAlloc/memoryFree  RenderWare's free lists. librw has none; every
    //                           object type allocates through rwMalloc.
    //   dOpenDevice, stdFunc    retail's driver dispatch tables. On this side
    //                           RwRenderStateSet and the rest are shim
    //                           functions, not entries dispatched through here.
    //   fileFuncs               librw owns file access through
    //                           rw::engine->filefuncs, which Engine::open sets
    //                           to the C library. Nothing above the seam reads
    //                           RwGlobals::fileFuncs.
    //   metrics                 librw keeps no per-frame counters.
    //
    // curCamera and curWorld are the exception that needs following up: they
    // are read directly by xCutscene.cpp and xFX.cpp, and librw keeps the same
    // two in rw::engine->currentCamera/currentWorld. Because game code reads
    // them as struct fields there is no call to hook, so whoever writes
    // RwCameraBeginUpdate/RwCameraEndUpdate and RpWorldRender has to assign
    // both copies. Until then they stay null.

    // The dirty-frame list is librw's -- rw::engine->frameDirtyList, which
    // frameOpen() initialises during RwEngineStart. This one is initialised as
    // an empty list rather than left as null links so that a traversal of it
    // terminates immediately, which is the truth: it never has anything in it.
    rwLinkListInitialize(&sGlobals.dirtyFrameList);

    // rwENGINEINITNOFREELISTS asks RenderWare not to use free lists. librw has
    // no free lists to disable, so both values of the flag describe what it
    // already does.
    (void)initFlags;

    rw::MemoryFunctions memoryFunctions;
    memoryFunctions.rwmalloc = shimMalloc;
    memoryFunctions.rwrealloc = shimRealloc;
    memoryFunctions.rwfree = shimFree;

    // Left null on purpose: librw fills these two in with its own wrappers that
    // abort on a failed allocation, which is the behaviour we want and cannot
    // write here without duplicating it.
    memoryFunctions.rwmustmalloc = NULL;
    memoryFunctions.rwmustrealloc = NULL;

    // RwEngineInstance has to be live before this call, not after it: librw
    // allocates during init, and shimMalloc reads the table through it.
    RwEngineInstance = &sGlobals;

    if (!rw::Engine::init(&memoryFunctions))
    {
        RwEngineInstance = NULL;
        return FALSE;
    }

    sGlobals.engineStatus = rwENGINESTATUSINITED;
    return TRUE;
}

#ifdef RW_D3D9
// The probe's IDirect3D9, kept alive for as long as the engine is open. See the
// comment at the probe in RwEngineOpen for why it is not released there.
static IDirect3D9* sProbeD3D9;
#endif

RwBool RwEngineOpen(RwEngineOpenParams* initParams)
{
    if (RwEngineInstance == NULL)
    {
        return FALSE;
    }

    // librw's EngineOpenParams is declared per backend and there is nothing in
    // RwEngineOpenParams to translate from: its displayID is a pointer to the
    // GameCube's RwGameCubeDeviceConfig, which describes a console's video
    // encoder. So the parameters are built from the port's own window instead,
    // and initParams is ignored -- iSystem.cpp opens the window before it
    // reaches here, which is the same order gc/iSystem.cpp uses when it calls
    // VIInit before RwEngineOpen.
    //
    // This is the one place in the shim that has to know which backend was
    // linked, because EngineOpenParams is the only librw type whose SHAPE
    // changes with it. Everything else the port touches is backend-neutral.
    (void)initParams;

#if defined(RW_D3D9) || defined(RW_D3D8)
    rw::EngineOpenParams params;
    params.window = (HWND)iWindowNativeHandle();

    if (params.window == NULL)
    {
        // A D3D device cannot be created without one, and librw would assert
        // rather than say so.
        return FALSE;
    }

    // **Check for a usable adapter BEFORE handing librw the window.**
    //
    // librw does not tell you when the device fails to open: Engine::open calls
    // device.system(DEVICEOPEN, ...) and DISCARDS the result (engine.cpp:276),
    // then sets Engine::state = Opened and returns 1. The fault lands later,
    // inside RwEngineStart, dereferencing a nil d3d9Globals.d3d9 with nothing
    // on screen to explain it -- and checking AFTER the fact does not work
    // either, because by then the device query faults on the same nil pointer.
    //
    // So the probe happens here, with its own IDirect3D9 that is released
    // again. It is a few lines and it turns a segfault into a sentence.
    //
    // Worth knowing while reading librw's output: it reports BOTH of openD3D's
    // failures with the same string, "Direct3DCreate9() failed". The second
    // one, at d3d/d3ddevice.cpp:1533, actually means Direct3DCreate9 SUCCEEDED
    // and no adapter reported D3DDEVTYPE_HAL support -- a very different
    // problem, and usually an environmental one rather than a missing D3D9.
    {
        IDirect3D9* probe = Direct3DCreate9(D3D_SDK_VERSION);

        if (probe == NULL)
        {
            printf("bfbb: Direct3DCreate9 failed -- this machine has no usable "
                   "Direct3D 9 runtime\n");
            fflush(stdout);
            return FALSE;
        }

        D3DCAPS9 caps;
        bool haveHardwareAdapter = false;

        for (UINT adapter = 0; adapter < probe->GetAdapterCount(); adapter++)
        {
            if (SUCCEEDED(probe->GetDeviceCaps(adapter, D3DDEVTYPE_HAL, &caps)))
            {
                haveHardwareAdapter = true;
                break;
            }
        }

        // NOT released here.
        //
        // Releasing the last reference tears down D3D9's process-wide state,
        // and openD3D creates a second IDirect3D9 a few instructions later --
        // a destroy/recreate cycle that this machine has been seen to fault
        // inside, at Direct3DCreate9Ex+0x25472 writing address 0x14, with the
        // probe itself having succeeded moments before. Holding the reference
        // keeps that state alive across the handover, which is also what an
        // ordinary D3D application does: it creates the object once.
        //
        // Released in RwEngineClose, beside the engine it belongs to.
        sProbeD3D9 = probe;

        if (!haveHardwareAdapter)
        {
            // Environmental far more often than not: an adapter whose display
            // is asleep or switched off can stop reporting HAL support, and a
            // remote session has no hardware adapter at all.
            printf("bfbb: Direct3D 9 is present but no adapter reports hardware "
                   "support (checked %u)\n",
                   (unsigned)probe->GetAdapterCount());
            fflush(stdout);
            return FALSE;
        }
    }

    // Fix the size the game renders at, before the device is made.
    //
    // The game's framebuffer is a fixed part of its design -- zGame.cpp:395
    // builds the main camera's raster at a constant size, and librw takes the
    // viewport from that raster while the back buffer follows the window. So a
    // window larger than the raster does not enlarge the picture, it leaves the
    // picture at its own size in the top-left corner with the rest of the window
    // around it. A virtual screen makes the picture the thing that scales.
    //
    // The size comes from the window rather than from a constant repeated here,
    // because iSystem opens the window at the size it intends the game to render
    // at, and that has to agree with the raster zGame builds. Read now, before
    // anything can resize it: this is the size the port booted with, and it is
    // deliberately NOT updated afterwards.
    {
        RwInt32 screenWidth = 0;
        RwInt32 screenHeight = 0;
        iWindowGetSize(&screenWidth, &screenHeight);
        if (screenWidth > 0 && screenHeight > 0)
        {
            rw::d3d::setVirtualScreen(screenWidth, screenHeight);
        }
    }
    if (!rw::Engine::open(&params))
    {
        printf("bfbb: librw refused to open the D3D9 device on this window\n");
        fflush(stdout);
        return FALSE;
    }

#elif defined(RW_GL3)

    // Not written, and not guessed at. GL3's EngineOpenParams differs again by
    // which gfx library librw was built against -- SDL2, SDL3 and GLFW each
    // give it a different shape -- so it needs an iWindowGlfw.cpp (or SDL) next
    // to iWindowWin32.cpp and a matching arm here. The port is built for D3D9
    // today; this arm is what the second backend costs, and it is small.
#error "GL3 needs a GLFW or SDL iWindow implementation and the matching EngineOpenParams here."

#else

    // LIBRW_PLATFORM=NULL. The null device ignores the argument entirely, and
    // there is no window -- which is what lets the shim's own tests run
    // headless.
    if (!rw::Engine::open(NULL))
    {
        return FALSE;
    }

#endif

    sGlobals.engineStatus = rwENGINESTATUSOPENED;
    return TRUE;
}

RwBool RwEngineStart(void)
{
    if (RwEngineInstance == NULL)
    {
        return FALSE;
    }

    if (!rw::Engine::start())
    {
        return FALSE;
    }

#ifdef RW_D3D9
    // Engine::start DISCARDS what the device said.
    //
    // engine.cpp:311 is `engine->device.system(DEVICEINIT, nil, 0);` with the
    // result thrown away, and DEVICEINIT is where the d3d9 backend actually
    // creates the device -- d3ddevice.cpp:1622. So start() reports success
    // whether or not there is a device, and everything afterwards runs against
    // a null one and dies somewhere with no bearing on the cause. That is what
    // an intermittent segfault inside RwFrameCreate turned out to be.
    //
    // Engine::open discards its DEVICEOPEN result the same way, but the device
    // does not exist yet at that point, so this is the first place worth
    // asking. The adapter probe in RwEngineOpen catches the case where no
    // adapter admits to hardware support; this catches the case where one does
    // and the device still fails to come up, which on a working machine is
    // usually a display that has gone to sleep.
    if (rw::d3d::d3ddevice == NULL)
    {
        printf("bfbb: Direct3D 9 reported a hardware adapter but the device did not "
               "come up\n");
        printf("bfbb:   (a display that is asleep or switched off does this)\n");
        fflush(stdout);
        return FALSE;
    }
#endif

    sGlobals.engineStatus = rwENGINESTATUSSTARTED;
    return TRUE;
}

// The three teardown calls return void in librw and RwBool in RenderWare, so
// the result is taken from the engine state afterwards. That is a real check
// and not a constant: librw refuses to stop an engine that is not started, to
// close one that is not open, and to term one that is not initialised, and in
// each of those cases the state does not move.

RwBool RwEngineStop(void)
{
    if (RwEngineInstance == NULL)
    {
        return FALSE;
    }

    rw::Engine::stop();

    if (rw::Engine::state != rw::Engine::Opened)
    {
        return FALSE;
    }

    sGlobals.engineStatus = rwENGINESTATUSOPENED;
    return TRUE;
}

RwBool RwEngineClose(void)
{
    if (RwEngineInstance == NULL)
    {
        return FALSE;
    }

    rw::Engine::close();

    if (rw::Engine::state != rw::Engine::Initialized)
    {
        return FALSE;
    }

#ifdef RW_D3D9
    // After librw has closed its own, so that the probe's reference is the last
    // one released rather than the one that pulls D3D9 down early.
    if (sProbeD3D9 != NULL)
    {
        sProbeD3D9->Release();
        sProbeD3D9 = NULL;
    }
#endif

    sGlobals.engineStatus = rwENGINESTATUSINITED;
    return TRUE;
}

RwBool RwEngineTerm(void)
{
    if (RwEngineInstance == NULL)
    {
        return FALSE;
    }

    rw::Engine::term();

    if (rw::Engine::state != rw::Engine::Dead)
    {
        return FALSE;
    }

    sGlobals.engineStatus = rwENGINESTATUSIDLE;
    RwEngineInstance = NULL;
    return TRUE;
}

// ---------------------------------------------------------------------------
// Video modes
//
// librw answers these through the render device, and the NULL backend is not a
// render device: its deviceSystem() returns 1 for every request it does not
// recognise, so it claims one video mode, claims mode 1 is current, and reports
// success from DEVICEGETVIDEOMODEINFO without writing anything to the struct it
// was handed. Forwarding blindly would hand xScrFx an uninitialised width and
// height off its own stack.
//
// So the null device is detected and reported as what it is -- no video modes.
// The functions below are the real forwarding path and start working the moment
// a GL3 or D3D9 librw is linked; until then there is no screen to have a size.

// types.h:146 defines `null` as a macro and librw has a NAMESPACE of that name,
// so `rw::null::deviceSystem` stops parsing as soon as anything pulls types.h
// in ahead of it -- which the windows.h at the top of this file now does. The
// error it gives ("expected unqualified-id") names neither the macro nor the
// header, so the push/pop is worth more than the two lines it costs.
#pragma push_macro("null")
#undef null

static bool haveRenderDevice()
{
    return rw::engine != NULL && rw::engine->device.system != rw::null::deviceSystem;
}

#pragma pop_macro("null")

RwInt32 RwEngineGetCurrentVideoMode(void)
{
    if (!haveRenderDevice())
    {
        // RenderWare numbers video modes from 0, so there is no index that
        // means "none". -1 is out of range for every caller that would then
        // pass it to RwEngineGetVideoModeInfo, which fails on it.
        return -1;
    }

    return rw::Engine::getCurrentVideoMode();
}

RwVideoMode* RwEngineGetVideoModeInfo(RwVideoMode* modeinfo, RwInt32 modeIndex)
{
    if (modeinfo == NULL || !haveRenderDevice())
    {
        return NULL;
    }

    rw::VideoMode mode;
    if (rw::Engine::getVideoModeInfo(&mode, modeIndex) == NULL)
    {
        return NULL;
    }

    modeinfo->width = mode.width;
    modeinfo->height = mode.height;
    modeinfo->depth = mode.depth;
    modeinfo->flags = (RwVideoModeFlag)mode.flags;

    // **The CURRENT mode's size is the SCREEN THE GAME DRAWS INTO.**
    //
    // On the console a video mode IS the framebuffer -- there is one, it is the
    // screen, and RenderWare's width and height are the pixels the game draws
    // into. librw's D3D9 backend enumerates the ADAPTER's display modes
    // instead, so the current one comes back as the desktop's resolution:
    // 3840x2160 on the machine this was found on, against a 640x480 window.
    //
    // That is not academic. xScrFx.cpp:86 draws its full-screen rectangle from
    // (0,0) to (width,height) in SCREEN coordinates, and xScrFx.cpp:185, 255
    // and 270 size their effects the same way. With the desktop's numbers the
    // screen fades, the letterbox bars and the death vignette are all sized for
    // a rectangle six times wider than the thing being drawn into, so they
    // cover a corner of it or miss entirely.
    //
    // Only the CURRENT mode is rewritten. RwEngineGetVideoModeInfo is also a
    // mode ENUMERATOR -- a caller walking indices wants each mode's real size,
    // and lying about all of them would break the enumeration to fix the one
    // reading the game actually makes.
    if (modeIndex == rw::Engine::getCurrentVideoMode())
    {
        RwInt32 screenWidth = 0;
        RwInt32 screenHeight = 0;

#ifdef RW_D3D9
        rw::d3d::getVirtualScreen(&screenWidth, &screenHeight);
#endif

        // The virtual screen, NOT the window. What the game asks this question
        // for is the size of the thing it is drawing into, and once the picture
        // is scaled at present time that stops being the window: a full-screen
        // rectangle sized to a maximised window would be several times the
        // surface it lands on. Reporting the window here is what left the fades
        // and the letterbox bars covering the whole window while the game
        // itself occupied a corner of it.
        if (screenWidth <= 0 || screenHeight <= 0)
        {
            iWindowGetSize(&screenWidth, &screenHeight);
        }

        if (screenWidth > 0 && screenHeight > 0)
        {
            modeinfo->width = screenWidth;
            modeinfo->height = screenHeight;
        }
    }

    // librw's VideoMode stops there. refRate and format are RenderWare's and
    // have no source on this side, so they are zeroed rather than guessed --
    // nothing in the game reads either one.
    modeinfo->refRate = 0;
    modeinfo->format = 0;

    return modeinfo;
}

// librw has no resource arena.
//
// RenderWare's is a fixed block that instanced geometry is packed into and
// evicted from -- RwResourcesAllocateResEntry hands out of it, and running out
// is why the console sizes it at 0x60000 before anything loads. librw
// allocates instanced data per object and frees it with the object, so there is
// no arena to size and nothing to run out of.
//
// TRUE rather than FALSE: the caller asked for an arena of at most this size
// and got one that cannot be exceeded, which is the outcome it wanted.
// iSystem.cpp does not check the result, but RenderWareInit's early returns
// treat FALSE from anything as a failed startup.
RwBool RwResourcesSetArenaSize(RwUInt32 size)
{
    (void)size;
    return TRUE;
}
