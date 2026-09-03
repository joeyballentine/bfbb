#include "iWindow.h"

// The window for a build that does not draw.
//
// This is not a placeholder for a missing implementation -- it is what the
// port does when it is built with BFBB_RENDER_BACKEND=NULL, and it is the
// correct answer in that configuration. There is no device, so there is no
// window to own, nothing to pump and nothing to present.
//
// It exists because the interface is not optional. rw/camera.cpp calls
// iWindowPump, iWindowShouldClose, iWindowGetVSync and iWindowPaceFrame from
// RwCameraShowRaster, and rw/engine_start.cpp calls iWindowGetSize -- all of
// them unconditionally, because the per-frame host obligations hang off the
// present rather than off a main loop the port does not own. A configuration
// with no window still links that code, which is why leaving the backend out
// of the link broke rw_selftest.exe rather than only bfbb.exe.

static bool sOpened;
static S32 sWidth;
static S32 sHeight;
static iWindowMode sMode = iWINDOW_WINDOWED;
static S32 sFrameRate = 60;
static S32 sVSync = TRUE;

S32 iWindowOpen(const iWindowParams* params)
{
    if (params == NULL || sOpened)
    {
        return FALSE;
    }

    // Succeeds, because nothing can go wrong: the failure the other backends
    // report is a window the OS would not give, and none is asked for here.
    // The size and mode are recorded rather than acted on, so that the
    // questions below have the answers the caller asked for.
    sOpened = true;
    sWidth = params->width;
    sHeight = params->height;
    sMode = params->mode;

    return TRUE;
}

void iWindowClose()
{
    sOpened = false;
    sWidth = 0;
    sHeight = 0;
    sMode = iWINDOW_WINDOWED;
}

void iWindowPump()
{
}

// No display to pace to. iSystem.cpp's iVSync does the same deadline
// arithmetic for the loops that have no renderer, and a headless build is
// meant to run as fast as the machine allows -- the self-tests are the caller.
void iWindowPaceFrame()
{
}

void iWindowSetFrameRate(S32 fps)
{
    sFrameRate = fps;
}

S32 iWindowGetFrameRate()
{
    return sFrameRate;
}

// No monitor, so the rate cannot be determined. video.framerate = display
// falls back to the configured cap on this answer.
S32 iWindowGetDisplayRefreshRate()
{
    return 0;
}

void iWindowSetVSync(S32 on)
{
    sVSync = on;
}

S32 iWindowGetVSync()
{
    return sVSync;
}

// Nothing can ask, so the answer never changes. A headless run ends when the
// game ends or when the process is killed.
S32 iWindowShouldClose()
{
    return FALSE;
}

void iWindowGetSize(S32* width, S32* height)
{
    if (width != NULL)
    {
        *width = sWidth;
    }

    if (height != NULL)
    {
        *height = sHeight;
    }
}

iWindowMode iWindowGetMode()
{
    return sMode;
}

void* iWindowNativeHandle()
{
    return NULL;
}

// Nothing to defer: librw's NULL backend creates no window of its own, so
// there is no handover to wait for.
const iWindowDeferred* iWindowDeferredParams()
{
    return NULL;
}

void iWindowDeferredCreated()
{
}

const char* iWindowBackendName()
{
    return "null";
}
