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

#include "rw.h"

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

RwBool RwEngineOpen(RwEngineOpenParams* initParams)
{
    if (RwEngineInstance == NULL)
    {
        return FALSE;
    }

    // librw's EngineOpenParams is declared per backend and has no definition
    // under LIBRW_PLATFORM=NULL, and what it holds for the backends that do
    // define it -- a window handle and a size for GL3, a HWND for D3D9 -- has
    // no counterpart in RwEngineOpenParams, whose displayID is a pointer to the
    // GameCube's RwGameCubeDeviceConfig. There is nothing to translate, so
    // nothing is: the null device ignores the argument entirely.
    //
    // This is the one place the shim would be wrong rather than merely
    // incomplete once a renderer is linked, so it refuses to compile then.
#if defined(RW_GL3) || defined(RW_D3D9) || defined(RW_PS2)
#error "RwEngineOpen has no EngineOpenParams to give a real backend. Build one from the window."
#endif
    (void)initParams;

    if (!rw::Engine::open(NULL))
    {
        return FALSE;
    }

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

static bool haveRenderDevice()
{
    return rw::engine != NULL && rw::engine->device.system != rw::null::deviceSystem;
}

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

    // librw's VideoMode stops there. refRate and format are RenderWare's and
    // have no source on this side, so they are zeroed rather than guessed --
    // nothing in the game reads either one.
    modeinfo->refRate = 0;
    modeinfo->format = 0;

    return modeinfo;
}
