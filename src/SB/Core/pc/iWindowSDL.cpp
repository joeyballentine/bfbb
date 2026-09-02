#include "iWindow.h"

// The window, on SDL, for both render backends.
//
// The backends still disagree about WHO OWNS IT, which is the whole reason
// iWindow.h has an iWindowDeferred block:
//
//   D3D9  the port creates the window here and hands librw the HWND out of it.
//         librw makes a device against that handle and never touches the window
//         again.
//
//   GL3   librw creates the window. rw::EngineOpenParams carries an
//         `SDL_Window**` out-parameter with the size and the title beside it,
//         and startSDL3 calls SDL_CreateWindow and SDL_GL_CreateContext
//         together during RwEngineStart -- it has to, because the pixel format
//         a window is created with is part of choosing a GL context, and
//         librw's own loop walks four context profiles until one comes up.
//
// So the GL3 arm below is mostly a place to keep the request until librw asks
// for it, and the D3D9 arm creates a window and reads an HWND back out of it.
// The per-frame obligations -- the event pump, the frame pace, the close
// button -- are shared, and were duplicated between two files before this.
//
// SDL rather than CreateWindowEx for D3D9: the port already links SDL for
// controllers and audio, so this costs no dependency, and it brings the DPI
// awareness, the screensaver suppression and the display enumeration that the
// Win32 window backend had to write by hand.

#include <SDL3/SDL.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <string.h>

// Under GL3, written by librw through the slot handed out below, and null until
// RwEngineStart has returned. Under D3D9, created in iWindowOpen.
static SDL_Window* sWindow;

static S32 sShouldClose;
static S32 sWidth;
static S32 sHeight;
static iWindowMode sMode = iWINDOW_WINDOWED;

// Copied rather than pointed at. iWindowParams::title belongs to the caller's
// stack frame in iSystem.cpp and librw reads it much later, inside
// RwEngineStart, by which time that frame is long gone.
static char sTitle[128];

static S32 sOpened;

#ifdef RW_GL3
static iWindowDeferred sDeferred;
#endif

#if defined(_WIN32) && !defined(RW_GL3)
// The window's own icons, which are NOT the same thing as the executable's.
//
// The shell finds the taskbar and alt-tab icon by reading the lowest-numbered
// icon resource out of the .exe, so that one works whether or not anything here
// is set. The title bar does not: it draws the small icon of the window, and a
// window that was never sent WM_SETICON gets its class's, which for an SDL
// window is the system default. That is the whole difference between "the
// taskbar icon is right and the title bar's is generic" and both being right.
//
// Resource 1 is the icon in res/bfbb.rc. LoadImage is asked for the two system
// metric sizes rather than being left to guess, because the .ico carries 16,
// 32, 48 and 64 pixel images and this is what picks the right one for each slot
// instead of scaling a large one down.
static void SetTitleBarIcon(HWND hwnd)
{
    if (hwnd == NULL)
    {
        return;
    }

    HINSTANCE instance = GetModuleHandleA(NULL);

    HICON big = (HICON)LoadImageA(instance, MAKEINTRESOURCEA(1), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                                  LR_DEFAULTCOLOR);
    HICON small_icon = (HICON)LoadImageA(instance, MAKEINTRESOURCEA(1), IMAGE_ICON,
                                         GetSystemMetrics(SM_CXSMICON),
                                         GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);

    if (big != NULL)
    {
        SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)big);
    }

    if (small_icon != NULL)
    {
        SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small_icon);
    }
}
#endif

S32 iWindowOpen(const iWindowParams* params)
{
    if (params == NULL || sOpened)
    {
        return FALSE;
    }

    // **SDL's video subsystem is brought up HERE, not left to librw.**
    //
    // librw's openSDL3 calls SDL_InitSubSystem(SDL_INIT_VIDEO) itself and
    // SDL_QuitSubSystem in closeSDL3, so the GL3 build would work without this.
    // Two things are bought by doing it first anyway, and they are the same two
    // the D3D9 adapter probe in rw/engine_start.cpp buys:
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
    //
    // It is also what makes the process DPI aware, which the Win32 backend used
    // to arrange by hand: SDL sets per-monitor v2 awareness when video comes up
    // and handles WM_DPICHANGED itself, so a window dragged between two
    // monitors at different scalings keeps its physical size instead of being
    // resized behind the game's back and resetting the device on every step.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        printf("bfbb: SDL could not open a video device: %s\n", SDL_GetError());
        fflush(stdout);
        return FALSE;
    }

    // Keep the display awake for as long as the game is up. It matters more
    // than a blanked screen would suggest: when the display sleeps, this
    // machine's adapters stop reporting D3DDEVTYPE_HAL, so RenderWare startup
    // fails outright -- a screensaver could take the game down.
    SDL_DisableScreenSaver();

    sMode = params->mode;

    // The size the window will be created at, which is the size asked for and
    // not necessarily what it ends up as -- the two fullscreen modes cover a
    // monitor. What it ends up as is read back below.
    sWidth = params->width;
    sHeight = params->height;

    strncpy(sTitle, params->title != NULL ? params->title : "BFBB", sizeof(sTitle) - 1);
    sTitle[sizeof(sTitle) - 1] = '\0';

    sShouldClose = 0;

#ifdef RW_GL3

    sDeferred.handleSlot = (void**)&sWindow;
    sDeferred.title = sTitle;
    sDeferred.width = sWidth;
    sDeferred.height = sHeight;

    sOpened = TRUE;
    return TRUE;

#else

    // Windowed opens at the render size, wherever the window manager puts it.
    // The other two cover a monitor with no frame -- the WS_POPUP the Win32
    // backend made -- and need that monitor's rectangle. The primary one is the
    // only sensible choice with nothing else to go on, since there is no window
    // yet to ask which display it is nearest.
    //
    // The rectangle is wanted as much as the size: a monitor left of the
    // primary has a negative origin.
    SDL_WindowFlags flags = 0;
    S32 x = 0;
    S32 y = 0;
    S32 w = params->width;
    S32 h = params->height;
    bool place = false;

    if (sMode == iWINDOW_WINDOWED)
    {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    else
    {
        SDL_Rect bounds;

        if (SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &bounds) && bounds.w > 0 && bounds.h > 0)
        {
            x = bounds.x;
            y = bounds.y;
            w = bounds.w;
            h = bounds.h;
            place = true;
        }

        flags |= SDL_WINDOW_BORDERLESS;
    }

    // Exclusive fullscreen is deliberately NOT SDL's fullscreen here. Under
    // D3D9 the mode set belongs to the device: SelectFullscreenVideoMode picks
    // a librw video mode between RwEngineOpen and RwEngineStart, and librw
    // creates the device with Windowed = FALSE against this handle. A window
    // SDL had already taken exclusive would be a second mode set the renderer
    // does not know happened. So both non-windowed modes get the same
    // frameless, monitor-sized window and the renderer decides which it is.
    sWindow = SDL_CreateWindow(sTitle, w, h, flags);

    if (sWindow == NULL)
    {
        printf("bfbb: SDL could not create a window: %s\n", SDL_GetError());
        fflush(stdout);
        SDL_EnableScreenSaver();
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return FALSE;
    }

    if (place)
    {
        SDL_SetWindowPosition(sWindow, x, y);
    }

    // What the window actually got, in pixels, which is what the back buffer is
    // sized in and what every other part of the port pairs with. Read rather
    // than assumed: for a frameless window covering a monitor it is the
    // monitor, and for a windowed one the window manager may have had its own
    // opinion.
    int got_w = 0;
    int got_h = 0;

    if (SDL_GetWindowSizeInPixels(sWindow, &got_w, &got_h) && got_w > 0 && got_h > 0)
    {
        sWidth = got_w;
        sHeight = got_h;
    }

#ifdef _WIN32
    SetTitleBarIcon((HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(sWindow),
                                                 SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL));
#endif

    sOpened = TRUE;
    return TRUE;

#endif
}

const iWindowDeferred* iWindowDeferredParams()
{
#ifdef RW_GL3
    return sOpened ? &sDeferred : NULL;
#else
    // Nothing to defer. D3D9 is handed a window that already exists, so by the
    // time RwEngineOpen runs there is an HWND and iWindowNativeHandle answers
    // with it.
    return NULL;
#endif
}

void iWindowDeferredCreated()
{
#ifdef RW_GL3
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

    // What the window actually got, in the units the back buffer is in.
    int width = 0;
    int height = 0;

    if (SDL_GetWindowSizeInPixels(sWindow, &width, &height) && width > 0 && height > 0)
    {
        sWidth = width;
        sHeight = height;
    }

    sShouldClose = 0;
#endif
}

void iWindowClose()
{
#ifndef RW_GL3
    if (sWindow != NULL)
    {
        SDL_DestroyWindow(sWindow);
    }
#endif

    // Under GL3 the window is NOT destroyed here, and that is not an omission:
    // librw created it, and its stopSDL3 destroys it and the GL context
    // together during RwEngineStop. By the time anything calls this, the
    // pointer is already dangling -- librw does not null the slot it wrote --
    // so the only correct thing to do with it is to forget it.
    sWindow = NULL;
    sOpened = FALSE;

    SDL_EnableScreenSaver();

    // Matches the SDL_InitSubSystem in iWindowOpen. Under GL3, librw's own
    // QuitSubSystem has already run by now and dropped the refcount to one;
    // this is the release that actually closes the video subsystem.
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

// **SDL's event queue is one queue per process, and this file is not its only
// reader.** iPadHostSDL.cpp's iPadHostPoll drains it too, for hot-plug, and
// both run every frame. A plain SDL_PollEvent on either side is therefore a
// catch-all that takes the other side's events as well, and whichever ran
// first won: iPadHostPoll ran first every frame, swallowed
// SDL_EVENT_WINDOW_CLOSE_REQUESTED, and the window could not be closed at all.
//
// So each side takes only the range it owns. The joystick and gamepad event
// types occupy 0x600 through 0x6FF and are the pad host's; everything else is
// this one's, including the sweep of what nothing in the port reads -- SDL's
// queue is unbounded and fills with mouse, keyboard and display events for as
// long as the game runs.
//
// SDL_PumpEvents is also what refreshes the keyboard state iPadKeyboardSDL.cpp
// reads, so this runs before input is polled rather than after.
void iWindowPump()
{
    SDL_PumpEvents();

    SDL_Event event;

    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                          SDL_EVENT_JOYSTICK_AXIS_MOTION - 1) > 0)
    {
        switch (event.type)
        {
        case SDL_EVENT_QUIT:
            sShouldClose = 1;
            break;

        // NOT SDL_DestroyWindow. The game owns its own shutdown -- it saves,
        // tears RenderWare down and frees its heap -- and destroying the window
        // here would pull the device or the GL context out from under all of
        // it. iSystem reads iWindowShouldClose and runs the game's exit path.
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            sShouldClose = 1;
            break;

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            sWidth = event.window.data1;
            sHeight = event.window.data2;
            break;

        default:
            break;
        }
    }

    // Touch, pen, drop, audio device, sensor, camera and render events, none of
    // which anything here reads. Dropped rather than left to pile up.
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FINGER_DOWN, SDL_EVENT_LAST) > 0)
    {
    }
}

// 60 Hz, which is what the GameCube's video interface gave the game, and what
// the framerate setting defaults to. See the long comment at the
// iWindowPaceFrame call in rw/camera.cpp for why the port paces at all when it
// is already waiting on the display.
static S32 sFrameRate = 60;
static S32 sVSync = 1;

// Held at file scope rather than inside iWindowPaceFrame so that a rate change
// can invalidate the deadline. See iWindowSetFrameRate.
static bool sPacerStarted = false;
static Uint64 sNextFrame = 0;

void iWindowSetFrameRate(S32 fps)
{
    sFrameRate = fps > 0 ? fps : 0;

    // The deadline the pacer is holding was measured against the old period.
    // Leaving it be would make the first frame after a change sleep out the
    // remainder of a period that no longer exists.
    sPacerStarted = false;
}

S32 iWindowGetFrameRate()
{
    return sFrameRate;
}

void iWindowSetVSync(S32 on)
{
    sVSync = on ? 1 : 0;
}

S32 iWindowGetVSync()
{
    return sVSync;
}

S32 iWindowGetDisplayRefreshRate()
{
    // The display the window is on, not the primary one -- on a two-monitor
    // machine those differ, and the one that paces the game is the one it is
    // being displayed on. Before the window exists there is no such display and
    // the primary one is the only honest answer.
    SDL_DisplayID display = 0;

    if (sWindow != NULL)
    {
        display = SDL_GetDisplayForWindow(sWindow);
    }

    if (display == 0)
    {
        display = SDL_GetPrimaryDisplay();
    }

    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display);

    if (mode == NULL || mode->refresh_rate <= 1.0f)
    {
        // SDL reports 0 for a display whose rate it does not know, and a rate
        // of one is not a number of hertz worth capping to -- that is the game
        // stopped.
        return 0;
    }

    return (S32)(mode->refresh_rate + 0.5f);
}

void iWindowPaceFrame()
{
    if (sFrameRate <= 0)
    {
        sPacerStarted = false;
        return;
    }

    const Uint64 framePeriodNs = 1000000000ULL / (Uint64)sFrameRate;

    Uint64 now = SDL_GetTicksNS();

    if (!sPacerStarted)
    {
        sNextFrame = now;
        sPacerStarted = true;
    }

    sNextFrame += framePeriodNs;

    // A frame that overran its budget must not be paid for by not sleeping for
    // the next several -- that turns one slow frame into a burst of fast ones.
    // Drop the missed deadlines and pace from now instead.
    if (sNextFrame <= now)
    {
        sNextFrame = now;
        return;
    }

    // SDL_DelayPrecise rather than SDL_DelayNS: the plain one rounds up to the
    // scheduler's tick and routinely overshoots by more than a frame at this
    // rate, which costs a frame every time rather than pacing one. The precise
    // one sleeps for all but the last fraction and spins out the rest.
    SDL_DelayPrecise(sNextFrame - now);
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

// Whatever the linked render backend's EngineOpenParams wants, as iWindow.h
// says: an SDL_Window* under GL3, which librw wrote into the slot itself, and
// the HWND behind that window under D3D9.
void* iWindowNativeHandle()
{
#ifdef RW_GL3
    return (void*)sWindow;
#elif defined(_WIN32)
    if (sWindow == NULL)
    {
        return NULL;
    }

    return SDL_GetPointerProperty(SDL_GetWindowProperties(sWindow),
                                  SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
#else
    return (void*)sWindow;
#endif
}

const char* iWindowBackendName()
{
    return "sdl3";
}
