// RenderWare C API: RwIm2D and RwIm3D.
//
// Immediate mode is how the game draws everything that is not a model: text,
// shadows, particles, the screen fades, the aura and streak effects. The calls
// themselves are thin -- librw's rw::im2d and rw::im3d take the same arguments
// in the same order -- but the VERTEX FORMAT is not settled, and that is the
// thing to read before touching this file.
//
// RwIm2DVertex is a typedef for rwGameCube2DVertex (rwplcore.h) and
// RwIm3DVertex for RxObjSpace3DVertex (rwcore.h). Both are GameCube layouts,
// and the game fills them through the RwIm2DVertexSet*/RwIm3DVertexSet* macros,
// which write named fields. librw's are per backend and neither matches:
//
//   RxObjSpace3DVertex   x y z  nx ny nz  r g b a (bytes)  u v
//   gl3::Im3DVertex      x y z  nx ny nz  r g b a (bytes)  u v     -- agrees
//
//   rwGameCube2DVertex   x y z        RwRGBA        u v            -- 24 bytes
//   gl3::Im2DVertex      x y z w      r g b a       u v            -- 32 bytes
//   d3d::Im2DVertex      x y z w      uint32 ARGB   u v            -- 28 bytes
//
// The 3D vertex happens to line up with GL3's. The 2D one does not line up with
// anything: both real backends carry a w (the camera z the rasteriser needs for
// perspective-correct interpolation) that the GameCube format has no room for,
// and RwIm2DVertexSetRecipCameraZ is a no-op macro on this header because the
// console did not need it either.
//
// So RwIm2DVertex has to become the backend's layout at the same time a backend
// is linked -- which means editing rwplcore.h's typedef and the twelve macros
// under it, and it means RwIm2DVertexSetRecipCameraZ has to stop being nothing.
// That is a decision for whoever links GL3 or D3D9, not a decision to guess at
// now, so this file refuses to compile against a real backend rather than hand
// one 24-byte vertices it will read 32 bytes out of. Same shape as the guard in
// RwEngineOpen.

#include <rwcore.h>

#include "rw.h"

#if defined(RW_GL3) || defined(RW_GLES2) || defined(RW_GLES3) || defined(RW_D3D8) ||               \
    defined(RW_D3D9) || defined(RW_WDGL) || defined(RW_PS2)
#error                                                                                             \
    "RwIm2DVertex is still the GameCube's 24-byte format. Give it the backend's Im2DVertex layout, and make RwIm2DVertexSetRecipCameraZ write the w, before rendering through a real device."
#endif

// ---------------------------------------------------------------------------
// Im2D

// The screen-space Z range the 2D rasteriser wants, which the game reads to put
// its overlays at the front or the back of the depth buffer (xFont.cpp:425,
// zGame.cpp:848, zEntPlayerOOBState.cpp:220). librw keeps them on the device --
// they are the depth range it hands the hardware -- and under
// LIBRW_PLATFORM=NULL that range is the null device's 0.0 to 1.0.
RwReal RwIm2DGetNearScreenZ(void)
{
    return rw::im2d::GetNearZ();
}

RwReal RwIm2DGetFarScreenZ(void)
{
    return rw::im2d::GetFarZ();
}

RwBool RwIm2DRenderPrimitive(RwPrimitiveType primType, RwIm2DVertex* vertices,
                             RwInt32 numVertices)
{
    if (vertices == NULL || numVertices <= 0)
    {
        return FALSE;
    }

    rw::im2d::RenderPrimitive((rw::PrimitiveType)primType, vertices, numVertices);
    return TRUE;
}

RwBool RwIm2DRenderIndexedPrimitive(RwPrimitiveType primType, RwIm2DVertex* vertices,
                                    RwInt32 numVertices, RwImVertexIndex* indices,
                                    RwInt32 numIndices)
{
    if (vertices == NULL || numVertices <= 0 || indices == NULL || numIndices <= 0)
    {
        return FALSE;
    }

    // RwImVertexIndex is an RwUInt16 and librw reads its indices as int16, so
    // the array goes through as it is.
    rw::im2d::RenderIndexedPrimitive((rw::PrimitiveType)primType, vertices, numVertices, indices,
                                     numIndices);
    return TRUE;
}

// ---------------------------------------------------------------------------
// Im3D
//
// The transform flags are passed unconverted: rwIM3D_VERTEXUV, ALLOPAQUE,
// NOCLIP, VERTEXXYZ and VERTEXRGBA are 1, 2, 4, 8, 16 on both sides, asserted
// in layout_camera.cpp. That assertion matters more than most, because half the
// call sites pass the bits as a literal -- 0x19 in zFX.cpp, 0x1b in zLasso.cpp
// -- so a renumbering would not even change the source.

void* RwIm3DTransform(RwIm3DVertex* pVerts, RwUInt32 numVerts, RwMatrix* ltm, RwUInt32 flags)
{
    if (pVerts == NULL || numVerts == 0 || rw::engine == NULL)
    {
        return NULL;
    }

    rw::im3d::Transform(pVerts, (rw::int32)numVerts,
                        const_cast<rw::Matrix*>(reinterpret_cast<const rw::Matrix*>(ltm)), flags);

    // The return value is a SUCCESS FLAG, and nothing may dereference it.
    //
    // RenderWare returns a pointer into the immediate-mode heap where it put the
    // transformed vertices; librw's Transform returns void and leaves the
    // transformed copy inside the device. There is no equivalent pointer to
    // hand back. All twenty call sites in src/SB use the result the same way --
    //
    //     if (RwIm3DTransform(vert, count, NULL, 0x19) != NULL) { ...render... }
    //
    // -- as "may I render now", so that is the question this answers, and it
    // answers it with a real pointer to real vertices rather than a made-up
    // non-null value. Returning NULL unconditionally would be the other kind of
    // wrong: every effect in the game would silently stop drawing.
    return pVerts;
}

RwBool RwIm3DRenderPrimitive(RwPrimitiveType primType)
{
    rw::im3d::RenderPrimitive((rw::PrimitiveType)primType);
    return TRUE;
}

RwBool RwIm3DEnd(void)
{
    rw::im3d::End();
    return TRUE;
}
