#ifndef IWINDOW_H
#define IWINDOW_H

#include <types.h>

// PC-only. The console has no counterpart and cannot: a GameCube's display is
// always there, already owned by the hardware, and gc/iSystem.cpp opens
// RenderWare on it with a RwGameCubeDeviceConfig it fills in from VIInit. A
// host has to ask an OS for a window first, and which OS call that is depends
// on the RENDER BACKEND rather than on the operating system -- D3D9 needs a
// raw Win32 HWND, GL3 needs whatever GLFW or SDL hands back, because librw's
// EngineOpenParams is declared per backend.
//
// That is why this seam exists rather than a few #ifdefs inside iSystem.cpp.
// The port targets D3D9 today and is meant to keep GL3 reachable, so the shape
// of the thing that owns a window is a seam with one implementation now and
// room for another, exactly like iHost, iSndHost and iPadHost.
//
// iWindowNativeHandle is the deliberately loose part of the interface. What it
// returns is whatever the backend's EngineOpenParams wants -- an HWND under
// Win32, a GLFWwindow* under GLFW -- and only src/SB/Core/pc/rw/engine_start.cpp
// may interpret it. Nothing else in the port should call it.

// How the window presents itself. None of the three changes what the game
// RENDERS at -- that is iScreen, and the picture is scaled to whatever surface
// it lands on either way. What they change is the surface.
//
//   WINDOWED    an ordinary resizable window, opened at the render size.
//
//   BORDERLESS  a WS_POPUP covering the whole of one monitor. No mode change,
//               so alt-tab is instant and nothing else on the desktop is
//               disturbed. The renderer neither knows nor cares: this is a
//               window that happens to be screen-sized.
//
//   FULLSCREEN  D3D9 exclusive fullscreen. The device owns the display, which
//               is what makes the flip a real page flip rather than a copy
//               into the desktop compositor. It costs a mode set, a slower
//               alt-tab, and a device that can be LOST rather than merely
//               resized -- so it is the one that asks the most of the reset
//               path in librw.
//
// Exclusive fullscreen is the only one of the three the RENDERER has to be told
// about, because a D3D9 device is created windowed or not and cannot change its
// mind without a reset. iWindowGetMode is how rw/engine_start.cpp asks, between
// RwEngineOpen and RwEngineStart -- the window is open by then and the device
// does not exist yet, which is the one moment the answer can still be acted on.
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

// Names the implementation that was linked in, for the startup log.
const char* iWindowBackendName();

#endif
