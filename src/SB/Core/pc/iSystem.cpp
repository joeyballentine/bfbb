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

void iSystemInit(U32 options)
{
    xDebugInit();
    xMemInit();
    iFileInit();
    iTimeInit();
    xPadInit();
    xSndInit();
    TRCInit();

    // TODO(pcport): RenderWareInit() belongs here, between TRCInit and
    // xMathInit, and is phase 3 -- librw. Everything below this line runs
    // without a renderer; nothing above it needed one.
    printf("bfbb: platform layer up, input backend: %s\n", iPadHostName());

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
