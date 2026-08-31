#include "iWindow.h"

// The SDL3 window, which is what a GL3 librw wants.
//
// It is not the Win32 window with a different API on it. The two backends
// disagree about who owns the window:
//
//   D3D9  the port creates the window and hands librw the HWND. librw makes a
//         device against it. iWindowWin32.cpp does all of its work inside
//         iWindowOpen and the renderer never touches the window again.
//
//   GL3   librw creates the window. rw::EngineOpenParams carries an
//         `SDL_Window**` out-parameter with the size and the title beside it,
//         and startSDL3 calls SDL_CreateWindow and SDL_GL_CreateContext
//         together during RwEngineStart -- it has to, because the pixel format
//         a window is created with is part of choosing a GL context, and
//         librw's own loop walks four context profiles until one comes up.
//
// So this file is mostly a place to keep the request until librw asks for it,
// plus the per-frame obligations -- the event pump, the frame pace and the
// close button -- which are the port's on either backend. iWindow.h's
// iWindowDeferred block describes the handover.
//
// SDL3 rather than GLFW, which librw also supports: SDL is already the port's
// direction of travel for controllers, so a GL3 build costs one dependency
// instead of two.

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

// Written by librw, through the slot handed out below. Null until
// RwEngineStart has returned.
static SDL_Window* sWindow;

static S32 sShouldClose;
static S32 sWidth;
static S32 sHeight;
static iWindowMode sMode = iWINDOW_WINDOWED;

// Copied rather than pointed at. iWindowParams::title belongs to the caller's
// stack frame in iSystem.cpp and librw reads it much later, inside
// RwEngineStart, by which time that frame is long gone.
static char sTitle[128];

static iWindowDeferred sDeferred;
static S32 sOpened;

S32 iWindowOpen(const iWindowParams* params)
{
    if (params == NULL || sOpened)
    {
        return FALSE;
    }

    // **SDL's video subsystem is brought up HERE, not left to librw.**
    //
    // librw's openSDL3 calls SDL_InitSubSystem(SDL_INIT_VIDEO) itself and
    // SDL_QuitSubSystem in closeSDL3, so it would work without this. Two things
    // are bought by doing it first anyway, and they are the same two the D3D9
    // adapter probe in rw/engine_start.cpp buys:
    //
    //   * a machine with no usable video driver says so here, in a sentence,
    //     rather than inside RwEngineStart with nothing on screen to explain it
    //     -- and unlike D3D9's, this failure is reported before the engine has
    //     been initialised at all.
    //
    //   * SDL refcounts subsystem init, so this reference outlives librw's.
    //     Its close then leaves the subsystem up instead of tearing SDL's
    //     process-wide video state down and building it again, which is the
    //     handover D3D9 was seen to fault inside.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        printf("bfbb: SDL could not open a video device: %s\n", SDL_GetError());
        fflush(stdout);
        return FALSE;
    }

    sMode = params->mode;

    // The size the window will be created at, which is the size asked for and
    // not necessarily what it ends up as -- the two fullscreen modes cover a
    // monitor. What it ends up as is read in iWindowDeferredCreated.
    sWidth = params->width;
    sHeight = params->height;

    strncpy(sTitle, params->title != NULL ? params->title : "BFBB", sizeof(sTitle) - 1);
    sTitle[sizeof(sTitle) - 1] = '\0';

    sDeferred.handleSlot = (void**)&sWindow;
    sDeferred.title = sTitle;
    sDeferred.width = sWidth;
    sDeferred.height = sHeight;

    sShouldClose = 0;
    sOpened = TRUE;
    return TRUE;
}

const iWindowDeferred* iWindowDeferredParams()
{
    return sOpened ? &sDeferred : NULL;
}

void iWindowDeferredCreated()
{
    if (sWindow == NULL)
    {
        return;
    }

    // Borderless, applied now because there was nothing to apply it to before.
    //
    // SDL3 spells it as fullscreen with no display mode set: the window covers
    // the display it is on and the desktop's mode is left alone, which is
    // exactly what iWindow.h describes borderless as. librw's startSDL3 has
    // already called SDL_SetWindowFullscreenMode(win, NULL) on the non-
    // exclusive path, so only the second half is left to do.
    //
    // Exclusive fullscreen is NOT here. It is a video mode, chosen before
    // RwEngineStart by SelectFullscreenVideoMode -- librw creates the window
    // fullscreen in the first place when the selected mode is an exclusive one,
    // and a window put into exclusive fullscreen afterwards would be a mode set
    // the renderer does not know happened.
    if (sMode == iWINDOW_BORDERLESS)
    {
        SDL_SetWindowFullscreenMode(sWindow, NULL);
        SDL_SetWindowFullscreen(sWindow, true);

        // Blocks until the window manager has actually done it. Without this
        // the size read below is the pre-fullscreen one, because SDL's
        // fullscreen transitions are asynchronous on every platform that has a
        // compositor.
        SDL_SyncWindow(sWindow);
    }

    // What the window actually got. Read rather than assumed, for the same
    // reason iWindowWin32.cpp reads its client rect: this is the number the
    // rest of the port pairs with, and for the two fullscreen modes it is the
    // monitor rather than anything that was asked for.
    int width = 0;
    int height = 0;
    if (SDL_GetWindowSize(sWindow, &width, &height) && width > 0 && height > 0)
    {
        sWidth = width;
        sHeight = height;
    }

    sShouldClose = 0;
}

void iWindowClose()
{
    // The window is NOT destroyed here, and that is not an omission: librw
    // created it, and its stopSDL3 destroys it and the GL context together
    // during RwEngineStop. By the time anything calls this, the pointer is
    // already dangling -- librw does not null the slot it wrote -- so the only
    // correct thing to do with it is to forget it.
    sWindow = NULL;
    sOpened = FALSE;

    // Matches the SDL_InitSubSystem in iWindowOpen. librw's own QuitSubSystem
    // has already run by now and dropped the refcount to one; this is the
    // release that actually closes the video subsystem.
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void iWindowPump()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_EVENT_QUIT:
            sShouldClose = 1;
            break;

        // NOT SDL_DestroyWindow, for the reason iWindowWin32.cpp gives at its
        // WM_CLOSE: the game owns its own shutdown, and destroying the window
        // here would pull the GL context out from under all of it.
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            sShouldClose = 1;
            break;

        case SDL_EVENT_WINDOW_RESIZED:
            sWidth = event.window.data1;
            sHeight = event.window.data2;
            break;

        default:
            break;
        }
    }
}

// 60 Hz, which is what the GameCube's video interface gave the game. Same
// number and the same deadline arithmetic as iWindowWin32.cpp; see the long
// comment at the iWindowPaceFrame call in rw/camera.cpp for why the port paces
// at all when it is already waiting on the display.
#define IWINDOW_FRAME_PERIOD_NS (1000000000ULL / 60)

void iWindowPaceFrame()
{
    static bool started = false;
    static Uint64 nextFrame = 0;

    Uint64 now = SDL_GetTicksNS();

    if (!started)
    {
        nextFrame = now;
        started = true;
    }

    nextFrame += IWINDOW_FRAME_PERIOD_NS;

    // A frame that overran its budget must not be paid for by not sleeping for
    // the next several -- that turns one slow frame into a burst of fast ones.
    // Drop the missed deadlines and pace from now instead.
    if (nextFrame <= now)
    {
        nextFrame = now;
        return;
    }

    // SDL_DelayPrecise rather than SDL_DelayNS: the plain one rounds up to the
    // scheduler's tick and routinely overshoots by more than a frame at this
    // rate, which costs a frame every time rather than pacing one. The precise
    // one sleeps for all but the last fraction and spins out the rest, which is
    // what iWindowWin32.cpp writes by hand.
    SDL_DelayPrecise(nextFrame - now);
}

S32 iWindowShouldClose()
{
    return sShouldClose;
}

iWindowMode iWindowGetMode()
{
    return sMode;
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

void* iWindowNativeHandle()
{
    return (void*)sWindow;
}

const char* iWindowBackendName()
{
    return "sdl3";
}
