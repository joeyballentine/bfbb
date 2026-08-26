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

struct iWindowParams
{
    const char* title;
    S32 width;
    S32 height;
    bool fullscreen;
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

// TRUE once the user has asked to close the window. iSystem turns this into
// the game's own shutdown path rather than exiting underneath it, so that
// save-on-exit and the RenderWare teardown still run.
S32 iWindowShouldClose();

void iWindowGetSize(S32* width, S32* height);

// The backend's handle. See the note above: only the RenderWare shim may
// interpret this, and what it means depends on which backend was linked.
void* iWindowNativeHandle();

// Names the implementation that was linked in, for the startup log.
const char* iWindowBackendName();

#endif
