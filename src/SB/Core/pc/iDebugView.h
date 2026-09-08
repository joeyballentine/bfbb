#ifndef IDEBUGVIEW_H
#define IDEBUGVIEW_H

#include <types.h>

struct RwCamera;

// A picture of what the renderer is doing, drawn in the corner of the finished
// frame.
//
// This exists to answer questions about the depth buffer that the finished
// picture cannot: whether a surface drawn in the wrong order is at the wrong
// depth, or at the right depth and failing the test for some other reason.
// The inset is the whole screen scaled down, so a wrong surface is in the same
// place in both pictures.
//
// OpenGL only. D3D9 cannot hand back a depth surface without the INTZ format
// trick, and D3D11 would need its own depth-as-shader-resource path; neither
// is written. On every other backend the setting parses and nothing is drawn.

enum iDebugViewMode
{
    IDEBUGVIEW_OFF,

    // Distance from the camera, near white to far black, linearised so that
    // the picture reads as distance rather than as the depth buffer's own
    // crowding towards the near plane.
    IDEBUGVIEW_DEPTH,

    // The same distance in thirty-two contour bands. A surface drawn at the
    // wrong depth lands in the wrong band, which is easier to see than a shade
    // of grey.
    IDEBUGVIEW_BANDS,

    // Both, side by side along the bottom.
    IDEBUGVIEW_BOTH
};

// Which view, if any. Pushed down from iSystem.cpp for the reason iGlow.h
// gives: this file compiles into bfbb_rw, which must not learn what config.ini
// is. Off unless something says otherwise.
void iDebugViewSetMode(S32 mode);

// Draw it. Called from RwCameraEndUpdate with the camera still open, so that
// the inset lands on top of everything the frame drew, the interface included.
// Does nothing for a camera that renders into a texture.
void iDebugViewRender(RwCamera* cam);

// Name the view's shader constant, before any shader exists to hold it. See
// iGlow.h; the timing is the same and so is the reason. Called from
// RwEngineOpen.
void iDebugViewRegisterShaderUniforms(void);

#endif
