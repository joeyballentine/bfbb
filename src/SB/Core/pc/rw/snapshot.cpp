// The last frame the port presented, kept as a texture.
//
// The interface and the argument for it are in iSnapshot.h. This is what to
// copy, from where, and how to know the copy is still there, on each of the
// backends that can do it.
//
// It lives beside the shim rather than in the platform layer because the one
// thing it needs is the surface the game's picture lands on, and that is
// librw's, not the operating system's.
//
// Both backends copy the same thing -- the surface every camera drawing to the
// frame buffer actually lands on, which is the virtual screen. Reading that
// rather than whatever target happens to be bound is what keeps the capture
// right when the last thing to render before a present was a camera texture, a
// shadow buffer say, and so left ITS surface bound.

#include <rwcore.h>

// BEFORE librw's headers, for the reason engine_start.cpp gives: rwd3d.h
// declares EngineOpenParams from whether _D3D9_H_ is defined by the time it is
// read.
#if defined(RW_D3D9)
#include <windows.h>
#include <d3d9.h>
#define WITH_D3D
#endif

#include "rw.h"

#if defined(RW_D3D9)
// d3d9Globals.defaultRenderTarget, which is the surface every camera drawing to
// the frame buffer actually lands on: the virtual screen when there is one, the
// back buffer when there is not. Reading it here is what makes the capture
// right whatever the window is doing, and what keeps it right when the last
// thing to render before a present was a camera texture -- a shadow buffer, say
// -- and so left ITS surface bound as render target zero.
#include "src/d3d/rwd3dimpl.h"
#endif

#include "iSnapshot.h"

#include <stdio.h>

// The captured frame, and the texture handed out over it. Created on the first
// capture rather than at startup, because until the engine is open there is no
// device to create a render target on and nothing has asked for one.
static RwRaster* sRaster;
static RwTexture* sTexture;

// Whether sRaster holds a frame at all, as opposed to being freshly created.
// The first boot has no previous frame and must fall back.
static S32 sHaveFrame;

// Frozen: the loading screen is up and holding the picture, so presents made
// while it is up must not overwrite it with itself.
static S32 sLatched;

// This frame has the screen fade over it. See iSnapshot.h.
static S32 sObscured;

// Set once, when something has gone wrong in a way that will go wrong the same
// way every frame. Reported once and then the feature is simply off, because a
// message a frame is not a diagnostic.
static S32 sFailed;

// config.ini's xbox.snapshot, pushed down by iSystem.cpp. See glow.cpp for why
// it is pushed rather than read. Off, every call here answers the way the
// unsupported-backend arm below does, and zGame falls back to the background
// texture asset -- which is the GameCube and PS2 loading screen exactly.
static S32 sEnabled = TRUE;

#if defined(RW_D3D9) || defined(RW_GL3)

// The size to capture at, and the raster to capture into. Shared by both
// backends because neither the sizing rule nor the lifetime differs: the
// snapshot is the picture at whatever size the port renders it, and the quad it
// fills is in the transition camera's screen space and stretches to fit either
// way.
static S32 snapshotEnsureRaster(RwInt32 width, RwInt32 height);

#endif

#if defined(RW_D3D9)

// The D3D texture behind a raster at the moment it was written to.
//
// A device reset -- alt-tab, a resize, a driver restart -- takes every
// D3DPOOL_DEFAULT surface down with it. librw handles that for its own rasters:
// releaseVidmemRasters destroys the texture behind a CAMERATEXTURE and
// recreateVidmemRasters makes a new one, EMPTY. The Raster* is still valid and
// still the right size, so nothing about it says its contents are gone.
//
// The new texture is a different allocation, though, so remembering the pointer
// the picture was written into is enough to notice. This is the whole of the
// device-lost handling and it is worth having: without it a reset during a load
// leaves a black rectangle where the level used to be, with no way to tell that
// from a capture that never happened.
static void* sCapturedInto;

static inline void* rasterTexture(RwRaster* raster)
{
    // GETD3DRASTEREXT is a macro, so it is spelled unqualified and expands to
    // the qualified names itself.
    rw::Raster* r = reinterpret_cast<rw::Raster*>(raster);
    return GETD3DRASTEREXT(r)->texture;
}

// One report, and then off for good.
static void snapshotFail(const char* what, long hr)
{
    sFailed = 1;
    printf("bfbb: the loading-screen snapshot is off -- %s (0x%08lx)\n", what,
           (unsigned long)hr);
    fflush(stdout);
}

void iSnapshotCapture()
{
    // Read and cleared here rather than reset by the writer: xScrFxUpdateFade
    // does not run on every frame the port presents, and a stale FALSE would
    // be a captured fade while a stale TRUE would stop capturing for good.
    const S32 obscured = sObscured;
    sObscured = 0;

    if (!sEnabled || sFailed || sLatched || obscured)
    {
        return;
    }

    if (rw::d3d::d3ddevice == NULL)
    {
        return;
    }

    // The RESOLVED frame, not defaultRenderTarget: with multisampling on, the
    // surface the scene is drawn into holds several samples per pixel and
    // nothing can sample or stretch from it. resolveVirtualScreen collapses it
    // and hands back the single-sampled copy; it answers null only when there
    // is no virtual screen at all, and then the back buffer is what was drawn.
    IDirect3DSurface9* src = rw::d3d::resolveVirtualScreen();
    if (src == NULL)
    {
        src = rw::d3d::d3d9Globals.defaultRenderTarget;
    }
    if (src == NULL)
    {
        return;
    }

    D3DSURFACE_DESC desc;
    if (FAILED(src->GetDesc(&desc)))
    {
        return;
    }

    if (!snapshotEnsureRaster((RwInt32)desc.Width, (RwInt32)desc.Height))
    {
        return;
    }

    IDirect3DTexture9* tex = (IDirect3DTexture9*)rasterTexture(sRaster);
    if (tex == NULL)
    {
        // Between a device reset and librw recreating its video-memory rasters.
        // Not a failure; the next frame has one.
        return;
    }

    IDirect3DSurface9* dst = NULL;
    if (FAILED(tex->GetSurfaceLevel(0, &dst)) || dst == NULL)
    {
        return;
    }

    // D3DTEXF_NONE because this is a copy, not a scale: the surfaces agree on
    // size by construction above. A filter here would be a lie about what the
    // call is doing and is rejected outright by some drivers for equal extents.
    HRESULT hr = rw::d3d::d3ddevice->StretchRect(src, NULL, dst, NULL, D3DTEXF_NONE);
    dst->Release();

    if (FAILED(hr))
    {
        // The one case worth naming: StretchRect refuses a multisampled source,
        // and it refuses format pairs the driver will not convert between. Both
        // are properties of the device, so both will fail again next frame.
        snapshotFail("the frame could not be copied into a texture", hr);
        return;
    }

    sCapturedInto = tex;
    sHaveFrame = 1;
}

RwTexture* iSnapshotBackgroundTexture()
{
    if (!sEnabled || sFailed || !sLatched || !sHaveFrame || sTexture == NULL)
    {
        return NULL;
    }

    // The surface the picture went into is not the surface that would be
    // sampled: a device reset has been and gone, and what is there now is a
    // fresh empty render target. Fall back rather than draw black.
    if (rasterTexture(sRaster) != sCapturedInto)
    {
        sHaveFrame = 0;
        return NULL;
    }

    return sTexture;
}

#elif defined(RW_GL3)

// GL3 has no device-lost equivalent to guard against. A GL context can be lost
// -- GL_KHR_robustness spells out how -- but librw neither asks for that
// extension nor recreates anything on it, so there is no half-alive state to
// detect here the way there is on D3D9 after a Reset. The texture behind the
// raster is a name that stays valid for as long as the raster does.

// One report, and then off for good.
static void snapshotFail(const char* what)
{
    sFailed = 1;
    printf("bfbb: the loading-screen snapshot is off -- %s\n", what);
    fflush(stdout);
}

void iSnapshotCapture()
{
    // Read and cleared here rather than reset by the writer: xScrFxUpdateFade
    // does not run on every frame the port presents, and a stale FALSE would
    // be a captured fade while a stale TRUE would stop capturing for good.
    const S32 obscured = sObscured;
    sObscured = 0;

    if (!sEnabled || sFailed || sLatched || obscured)
    {
        return;
    }

    // The size the virtual screen was made at, which is the size everything
    // the game draws lands on. Zero before the engine has started, and zero
    // for good if the driver refused the framebuffer -- in which case there is
    // no picture to copy and the game falls back, which is the same answer the
    // unsupported backends give.
    RwInt32 width = (RwInt32)rw::gl3::virtualScreenWidth;
    RwInt32 height = (RwInt32)rw::gl3::virtualScreenHeight;

    if (width <= 0 || height <= 0 || rw::gl3::virtualScreenFramebuffer() == 0)
    {
        return;
    }

    if (!snapshotEnsureRaster(width, height))
    {
        return;
    }

    if (!rw::gl3::copyVirtualScreen(reinterpret_cast<rw::Raster*>(sRaster)))
    {
        // Every reason this returns false is a property of the device or of
        // the raster, so it will be the same reason next frame.
        snapshotFail("the frame could not be copied into a texture");
        return;
    }

    sHaveFrame = 1;
}

RwTexture* iSnapshotBackgroundTexture()
{
    if (!sEnabled || sFailed || !sLatched || !sHaveFrame || sTexture == NULL)
    {
        return NULL;
    }

    return sTexture;
}

#else

// Every other backend. The capture needs a way to copy the frame buffer into a
// texture, and LIBRW_PLATFORM=NULL renders nothing to copy. Saying so here,
// rather than leaving the file out of the build, keeps the call sites in
// camera.cpp and zGame.cpp free of backend #ifdefs -- and the game already
// handles the refusal: zGame falls back to the background texture asset, which
// is the GameCube and PS2 loading screen exactly.

void iSnapshotCapture()
{
}

RwTexture* iSnapshotBackgroundTexture()
{
    return NULL;
}

#endif

#if defined(RW_D3D9) || defined(RW_GL3)

// The capture target, made on the first capture rather than at startup: until
// the engine is open there is no device to create a render target on and
// nothing has asked for one.
//
// Rebuilt if the screen ever changes size under us. Nothing in the port does
// that today -- the virtual screen is fixed at the size the window opened at --
// but a stale raster would make the copy scale rather than copy, and a silently
// scaled snapshot is worse than a rebuilt one.
static S32 snapshotEnsureRaster(RwInt32 width, RwInt32 height)
{
    if (sRaster != NULL && (width != sRaster->width || height != sRaster->height))
    {
        RwTextureDestroy(sTexture);
        sTexture = NULL;
        sRaster = NULL;
        sHaveFrame = 0;
    }

    if (sRaster != NULL)
    {
        return TRUE;
    }

    sRaster = RwRasterCreate(width, height, 32,
                             rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT8888);
    if (sRaster == NULL)
    {
        sFailed = 1;
        printf("bfbb: the loading-screen snapshot is off -- this backend would "
               "not make a render-target texture\n");
        fflush(stdout);
        return FALSE;
    }

    // The texture is what the game is handed, and it owns the raster from here:
    // RwTextureDestroy above takes both down together.
    sTexture = RwTextureCreate(sRaster);
    if (sTexture == NULL)
    {
        RwRasterDestroy(sRaster);
        sRaster = NULL;
        sFailed = 1;
        printf("bfbb: the loading-screen snapshot is off -- a texture could not "
               "be made for it\n");
        fflush(stdout);
        return FALSE;
    }

    return TRUE;
}

#endif

void iSnapshotLatch()
{
    sLatched = 1;
}

void iSnapshotRelease()
{
    sLatched = 0;
}

void iSnapshotSetObscured(S32 obscured)
{
    sObscured = obscured ? 1 : 0;
}

void iSnapshotSetEnabled(S32 enabled)
{
    sEnabled = enabled ? TRUE : FALSE;
}
