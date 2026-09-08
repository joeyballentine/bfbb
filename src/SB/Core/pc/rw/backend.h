#ifndef BFBB_RW_BACKEND_H
#define BFBB_RW_BACKEND_H

// Which of the linked render backends is running.
//
// RW_D3D9, RW_D3D11 and RW_GL3 say which backends the build CARRIES, and are
// still what guards each backend's own code -- an arm whose headers are not
// there cannot compile. These say which one opened the device, and are what the
// parts written against more than one dispatch on. So the shape is
//
//     #ifdef RW_D3D9
//         if (iBackendIsD3D9()) { ... }
//     #endif
//
// with the #ifdef answering "can this compile" and the call answering "is this
// the one running".
//
// iScreenGetBackend holds the setting. RenderWareInit resolves it out of
// iSCREENBACKEND_AUTO before the device opens, so from then on it names exactly
// one backend and never AUTO.

// AFTER librw's headers, in a unit that has any. This reaches types.h, which
// has `#define null 0` in it, and librw has a `namespace null`.
#include "iScreen.h"

// Turn iSCREENBACKEND_AUTO into exactly one backend, and tell librw by setting
// rw::platform. Idempotent, and safe to call before the engine exists.
//
// It has to run before the WINDOW is opened, not just before the device: D3D
// is handed a window that already exists and GL3 makes its own, so iWindowOpen
// already needs the answer. RenderWareInit calls it there; RwEngineOpen calls
// it again in case something else got in first.
//
// A backend this build does not carry is reported by name and the first one it
// does have is used instead -- a setting should not stop the game starting.
void iBackendResolve(void);

// A backend the build does not carry folds to a constant false, so a call site
// guarded by one of these costs nothing where it cannot apply.

inline bool iBackendIsD3D9()
{
#ifdef RW_D3D9
    return iScreenGetBackend() == iSCREENBACKEND_D3D9;
#else
    return false;
#endif
}

inline bool iBackendIsD3D11()
{
#ifdef RW_D3D11
    return iScreenGetBackend() == iSCREENBACKEND_D3D11;
#else
    return false;
#endif
}

// The two Direct3D backends together. They are one namespace in librw and most
// of the port's D3D code is written against both.
inline bool iBackendIsD3D()
{
    return iBackendIsD3D9() || iBackendIsD3D11();
}

inline bool iBackendIsGL3()
{
#ifdef RW_GL3
    return iScreenGetBackend() == iSCREENBACKEND_GL3;
#else
    return false;
#endif
}

// No device at all: nothing to draw into and nothing to ask about a card.
inline bool iBackendIsNull()
{
    return iScreenGetBackend() == iSCREENBACKEND_NULL;
}

// Half a pixel where the device wants screen-space coordinates shifted by one,
// zero where it does not. D3D9 is the only backend that does; D3D10 and up and
// OpenGL put the pixel centre in the middle the way the coordinates assume.
//
// librw calls the same thing rw::halfPixel and sets it when the device opens.
// This is the port's spelling of it, so that a file needing the rule does not
// have to include librw's headers to ask.
inline float iBackendHalfPixel()
{
    return iBackendIsD3D9() ? 0.5f : 0.0f;
}

#endif
