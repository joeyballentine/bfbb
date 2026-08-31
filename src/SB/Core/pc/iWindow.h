#ifndef IWINDOW_H
#define IWINDOW_H

#include <types.h>

// PC-only. The console has no counterpart and cannot: a GameCube's display is
// always there, already owned by the hardware, and gc/iSystem.cpp opens
// RenderWare on it with a RwGameCubeDeviceConfig it fills in from VIInit. A
// host has to ask an OS for a window first, and which OS call that is depends
// on the RENDER BACKEND rather than on the operating system, because librw's
// EngineOpenParams is declared per backend: D3D9 needs a raw Win32 HWND, GL3
// needs an SDL_Window.
//
// That is why this seam exists rather than a few #ifdefs inside iSystem.cpp,
// and it has two implementations -- iWindowWin32.cpp and iWindowSDL.cpp --
// exactly like iHost, iSndHost and iPadHost.
//
// The two do not merely differ in which OS calls they make. They differ in WHO
// OWNS THE WINDOW: under D3D9 the port creates it and hands librw the handle,
// and under GL3 librw creates it during RwEngineStart and hands the port back
// what it made. The iWindowDeferred block near the bottom of this header is
// that handover, and it is the only part of this interface a D3D9 build has
// nothing to say about.
//
// iWindowNativeHandle is the deliberately loose part of the interface. What it
// returns is whatever the backend's EngineOpenParams wants -- an HWND under
// Win32, an SDL_Window* under SDL -- and only
// src/SB/Core/pc/rw/engine_start.cpp may interpret it. Nothing else in the port
// should call it.

// How the window presents itself. None of the three changes what the game
// RENDERS at -- that is iScreen, and the picture is scaled to whatever surface
// it lands on either way. What they change is the surface.
//
//   WINDOWED    an ordinary resizable window, opened at the render size.
//
//   BORDERLESS  a window covering the whole of one monitor with no frame: a
//               WS_POPUP under Win32, SDL's fullscreen-with-no-display-mode
//               under SDL. No mode change either way, so alt-tab is instant and
//               nothing else on the desktop is disturbed. The renderer neither
//               knows nor cares: this is a window that happens to be
//               screen-sized.
//
//   FULLSCREEN  exclusive fullscreen. The device owns the display, which is
//               what makes the flip a real page flip rather than a copy into
//               the desktop compositor. It costs a mode set, a slower alt-tab,
//               and a device that can be LOST rather than merely resized -- so
//               it is the one that asks the most of the reset path in librw.
//
// Exclusive fullscreen is the only one of the three the RENDERER has to be told
// about, on either backend, because both create the display-owning object
// during RwEngineStart and neither can change its mind afterwards without
// tearing it down. iWindowGetMode is how rw/engine_start.cpp asks, between
// RwEngineOpen and RwEngineStart -- the video mode list exists by then and the
// device does not, which is the one moment the answer can still be acted on.
enum iWindowMode
{
    iWINDOW_WINDOWED,
    iWINDOW_BORDERLESS,
    iWINDOW_FULLSCREEN
};

struct iWindowParams
{
    const char* title;

    // The size to open a WINDOWED window at. Ignored by the other two, which
    // take the size of the monitor they land on.
    S32 width;
    S32 height;

    iWindowMode mode;
};

// FALSE if the window could not be created; the caller must not proceed to
// RwEngineOpen. Retail has no equivalent failure -- the console's display
// cannot be absent -- so iSystem has to invent the handling.
S32 iWindowOpen(const iWindowParams* params);
void iWindowClose();

// Drains the OS event queue. Called once a frame. A host window that is never
// pumped stops repainting and the OS eventually reports it as not responding,
// which looks exactly like the game hanging.
void iWindowPump();

// Paces the frame to the rate the game was built for, and returns having slept
// out whatever was left of it. Called once a frame, next to the pump.
//
// This belongs to the window rather than to the renderer because the window is
// what owns the display. Waiting on vertical retrace is not enough on its own:
// it paces to the MONITOR, and a 240 Hz monitor gives four times the frames a
// GameCube title was built for. Every part of the game that counts frames
// rather than seconds then runs four times too fast, and retail's own guard at
// zGame.cpp:559 -- which substitutes 1/60 s for any frame it measures under ten
// microseconds -- is a reminder that its timing was written against a console
// that could not produce one.
void iWindowPaceFrame();

// TRUE once the user has asked to close the window.
//
// Read at the frame boundary, in RwCameraShowRaster, alongside iWindowPump --
// see the comment there for why the port's per-frame obligations hang off the
// present rather off the main loop, which belongs to retail.
//
// What SHOULD happen is the game's own shutdown path, so that save-on-exit and
// the RenderWare teardown still run. What happens today is exit(0): retail
// never exits -- a GameCube title ends on OSPanic -- so there is no such path
// to hook, and inventing one needs a seam in zMainLoop.
S32 iWindowShouldClose();

void iWindowGetSize(S32* width, S32* height);

// The mode the window was opened in. iWINDOW_WINDOWED before it is opened, so
// a caller that asks too early gets the answer that needs nothing done about
// it rather than one that would put the device into exclusive fullscreen.
iWindowMode iWindowGetMode();

// The backend's handle. See the note above: only the RenderWare shim may
// interpret this, and what it means depends on which backend was linked.
void* iWindowNativeHandle();

// ---------------------------------------------------------------------------
// GL3 only. Nothing but src/SB/Core/pc/rw/engine_start.cpp may call these two.
//
// Under GL3 the window's ownership is INVERTED. librw's D3D9 backend is handed
// a window that already exists and makes a device against it. Its GL3 backend
// makes the window ITSELF: rw::EngineOpenParams carries a window out-parameter
// alongside the size and the title, and startSDL3 (librw's gl3device.cpp) calls
// SDL_CreateWindow during RwEngineStart -- because a GL context and the window
// it draws into have to be created together, and only the backend knows which
// pixel format and context version it is about to ask for.
//
// So iWindowOpen under GL3 records the request and creates nothing.
// iWindowDeferredParams hands that request, plus the slot librw writes the
// window into, to RwEngineOpen. iWindowDeferredCreated is called once
// RwEngineStart has returned: it is the first moment there is a window to read
// a size off or to put into borderless, and everything else in this interface
// starts answering for real from there.
//
// Exclusive fullscreen is NOT applied there. It is chosen between RwEngineOpen
// and RwEngineStart, by picking a video mode, exactly as the D3D9 backend does
// -- see SelectFullscreenVideoMode in engine_start.cpp -- because both backends
// create the display-owning object during RwEngineStart and neither can change
// its mind afterwards without tearing it down.
struct iWindowDeferred
{
    // Where librw stores the window it creates. An SDL_Window** in disguise,
    // for the same reason iWindowNativeHandle returns void*: this header must
    // not depend on which windowing library was linked.
    void** handleSlot;

    const char* title;
    S32 width;
    S32 height;
};

// NULL if iWindowOpen has not run, or if the linked implementation owns its own
// window -- the D3D9 path, which has nothing to defer.
const iWindowDeferred* iWindowDeferredParams();

void iWindowDeferredCreated();

// Names the implementation that was linked in, for the startup log.
const char* iWindowBackendName();

#endif
