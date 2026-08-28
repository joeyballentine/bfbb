// The Xbox full-screen glow. The chain, the kernel and where each came from are
// in iGlow.h.
//
// D3D9 only, same as snapshot.cpp and distort.cpp: it needs the frame buffer as
// a texture, and the shim has that for one backend.

#include <rwcore.h>

#if defined(RW_D3D9)
#include <windows.h>
#include <d3d9.h>
#define WITH_D3D
#endif

#include "rw.h"

#if defined(RW_D3D9)
#include "src/d3d/rwd3dimpl.h"
#endif

#include "iGlow.h"

#include <stdio.h>
#include <stdlib.h>

#if defined(RW_D3D9)

// fxc names every blob g_ps20_main, so each gets its own namespace.
namespace bright_ps
{
#include "shaders/glow_bright_PS.h"
}
namespace blur_ps
{
#include "shaders/glow_blur_PS.h"
}

// --- the chain's sizes, from the Xbox's three render targets ----------------
//
// 0x370544, 0x370540 and 0x370530, built by the calls at va 0x1712d8, 0x1712ee
// and 0x171304. Halving one axis per pass is what makes a four-tap kernel cover
// as much as it does.
static const RwInt32 kHalfW = 320, kHalfH = 240;
static const RwInt32 kVertW = 320, kVertH = 120;
static const RwInt32 kQuarterW = 160, kQuarterH = 120;

// The blur weights, from the Xbox's two tables. They sum to one.
static const F32 kNearWeight = 1.0f / 3.0f;
static const F32 kFarWeight = 1.0f / 6.0f;

// And the tap distances, in texels of the texture being sampled.
static const F32 kNearTap = 1.0f;
static const F32 kFarTap = 3.0f;

static const bool sEnabled = getenv("BFBB_GLOW") != NULL;

struct GlowTarget
{
    RwCamera* camera;
    RwRaster* raster;
    RwInt32 width;
    RwInt32 height;
};

static RwRaster* sScreen;          // the frame, copied so it can be sampled
static void* sCapturedInto;
static GlowTarget sHalf, sVert, sQuarter;
static void* sBrightShader;
static void* sBlurShader;
static S32 sFailed;

static void glowFail(const char* what, long hr)
{
    sFailed = 1;
    printf("bfbb: the glow is off -- %s (0x%08lx)\n", what, (unsigned long)hr);
    fflush(stdout);
}

static inline void* rasterTexture(RwRaster* raster)
{
    return GETD3DRASTEREXT(reinterpret_cast<rw::Raster*>(raster))->texture;
}

// A camera that renders into a texture, which is what the Xbox's 0x170660
// builds. No Z buffer: every pass here overwrites the whole target, and the
// three calls that make these all pass zero for it.
static bool makeTarget(GlowTarget* t, RwInt32 w, RwInt32 h)
{
    t->width = w;
    t->height = h;

    t->raster = RwRasterCreate(w, h, 32, rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT8888);
    if (t->raster == NULL)
    {
        return false;
    }

    t->camera = RwCameraCreate();
    if (t->camera == NULL)
    {
        RwRasterDestroy(t->raster);
        t->raster = NULL;
        return false;
    }

    RwCameraSetFrame(t->camera, RwFrameCreate());
    RwCameraSetRaster(t->camera, t->raster);

    // Im2D takes its screen z from these, so they have to be sane even though
    // nothing here is in a world or projected.
    RwCameraSetNearClipPlane(t->camera, 0.05f);
    RwCameraSetFarClipPlane(t->camera, 400.0f);
    return true;
}

// One full-screen quad in the current camera, textured with `src`, through
// `shader`. Every pass in the chain is this; only the target, the source and
// the constants differ.
static void drawPass(RwRaster* src, void* shader, F32 w, F32 h, RwBlendFunction srcBlend,
                     RwBlendFunction dstBlend, U8 alpha = 0xff)
{
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, src);
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE,
                     (void*)(srcBlend == rwBLENDONE ? FALSE : TRUE));
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)srcBlend);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)dstBlend);

    rwGameCube2DVertex vx[4];
    F32 z = RwIm2DGetNearScreenZ();

    for (S32 i = 0; i < 4; i++)
    {
        F32 u = (i & 2) ? 1.0f : 0.0f;
        F32 v = (i & 1) ? 1.0f : 0.0f;

        // Half a pixel left and up. D3D9 aligns a vertex at screen x with the
        // CENTRE of pixel x, not its corner, so a screen-aligned quad drawn at
        // 0..w samples half a texel off -- and it compounds down a chain like
        // this one: measured at +0.5 texels per pass, which is half a screen
        // pixel at the first and two at the composite, four or five all told.
        //
        // The Xbox does this too, at the head of its blur helper (va 0x171336
        // pushes -0.5 twice). librw's im2d transform carries no half-pixel term
        // of its own, so it has to be here.
        vx[i].x = u * w - 0.5f;
        vx[i].y = v * h - 0.5f;
        vx[i].z = z;
        vx[i].u = u;
        vx[i].v = v;
        vx[i].emissiveColor.red = 0xff;
        vx[i].emissiveColor.green = 0xff;
        vx[i].emissiveColor.blue = 0xff;
        vx[i].emissiveColor.alpha = alpha;
    }

    rw::d3d::im2dOverridePS = shader;
    RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, &vx[0], 4);
    rw::d3d::im2dOverridePS = NULL;
}

// The four weights and the four offsets, for one axis. `srcW`/`srcH` are the
// size of the texture being sampled, because the Xbox's offsets are in ITS
// texels, not the target's.
static void setBlurConstants(bool horizontal, F32 srcW, F32 srcH)
{
    F32 weights[4] = { kNearWeight, kFarWeight, kNearWeight, kFarWeight };

    F32 du = horizontal ? (1.0f / srcW) : 0.0f;
    F32 dv = horizontal ? 0.0f : (1.0f / srcH);

    F32 offs01[4] = { kNearTap * du, kNearTap * dv, kFarTap * du, kFarTap * dv };
    F32 offs23[4] = { -kNearTap * du, -kNearTap * dv, -kFarTap * du, -kFarTap * dv };

    rw::d3d::d3ddevice->SetPixelShaderConstantF(1, weights, 1);
    rw::d3d::d3ddevice->SetPixelShaderConstantF(2, offs01, 1);
    rw::d3d::d3ddevice->SetPixelShaderConstantF(3, offs23, 1);
}

// Copy the frame so it can be sampled. Same as distort.cpp, and the same reason
// the caller has to close the scene around it.
static bool captureScreen()
{
    IDirect3DSurface9* src = rw::d3d::d3d9Globals.defaultRenderTarget;
    if (src == NULL)
    {
        return false;
    }

    D3DSURFACE_DESC desc;
    if (FAILED(src->GetDesc(&desc)))
    {
        return false;
    }

    if (sScreen != NULL &&
        ((RwInt32)desc.Width != sScreen->width || (RwInt32)desc.Height != sScreen->height))
    {
        RwRasterDestroy(sScreen);
        sScreen = NULL;
    }

    if (sScreen == NULL)
    {
        sScreen = RwRasterCreate(desc.Width, desc.Height, 32,
                                 rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT8888);
        if (sScreen == NULL)
        {
            glowFail("this backend would not make a render-target texture", 0);
            return false;
        }
    }

    IDirect3DTexture9* tex = (IDirect3DTexture9*)rasterTexture(sScreen);
    if (tex == NULL)
    {
        return false;
    }

    IDirect3DSurface9* dst = NULL;
    if (FAILED(tex->GetSurfaceLevel(0, &dst)) || dst == NULL)
    {
        return false;
    }

    HRESULT hr = rw::d3d::d3ddevice->StretchRect(src, NULL, dst, NULL, D3DTEXF_NONE);
    dst->Release();

    if (FAILED(hr))
    {
        glowFail("the frame could not be copied into a texture", hr);
        return false;
    }

    sCapturedInto = tex;
    return true;
}

void iGlowRender(RwCamera* cam, F32 strength)
{
    if (!sEnabled || sFailed || cam == NULL || rw::d3d::d3ddevice == NULL)
    {
        return;
    }

    // Five scenes set this to zero, so an early return here is the effect
    // behaving, not a shortcut.
    if (strength <= 0.0f)
    {
        return;
    }

    if (strength > 1.0f)
    {
        strength = 1.0f;
    }

    if (sBrightShader == NULL)
    {
        sBrightShader = rw::d3d::createPixelShader((void*)bright_ps::g_ps20_main);
        sBlurShader = rw::d3d::createPixelShader((void*)blur_ps::g_ps20_main);
        if (sBrightShader == NULL || sBlurShader == NULL)
        {
            glowFail("the glow shaders would not compile", 0);
            return;
        }
    }

    if (sHalf.camera == NULL)
    {
        if (!makeTarget(&sHalf, kHalfW, kHalfH) || !makeTarget(&sVert, kVertW, kVertH) ||
            !makeTarget(&sQuarter, kQuarterW, kQuarterH))
        {
            glowFail("the glow's render targets would not be made", 0);
            return;
        }
    }

    // The passes render into their own cameras, and the copy needs the scene
    // closed, so the frame's camera goes down for the duration. The Xbox does
    // the same, at va 0x171e5e and 0x1720f0.
    RwCameraEndUpdate(cam);

    if (!captureScreen() || rasterTexture(sScreen) != sCapturedInto)
    {
        RwCameraBeginUpdate(cam);
        return;
    }

    // 1. threshold and halve
    RwCameraBeginUpdate(sHalf.camera);
    drawPass(sScreen, sBrightShader, (F32)sHalf.width, (F32)sHalf.height, rwBLENDONE,
             rwBLENDZERO);
    RwCameraEndUpdate(sHalf.camera);

    // 2. blur down the vertical axis
    RwCameraBeginUpdate(sVert.camera);
    setBlurConstants(false, (F32)sHalf.width, (F32)sHalf.height);
    drawPass(sHalf.raster, sBlurShader, (F32)sVert.width, (F32)sVert.height, rwBLENDONE,
             rwBLENDZERO);
    RwCameraEndUpdate(sVert.camera);

    // 3. and the horizontal one
    RwCameraBeginUpdate(sQuarter.camera);
    setBlurConstants(true, (F32)sVert.width, (F32)sVert.height);
    drawPass(sVert.raster, sBlurShader, (F32)sQuarter.width, (F32)sQuarter.height,
             rwBLENDONE, rwBLENDZERO);
    RwCameraEndUpdate(sQuarter.camera);

    // 4. add it back over the frame, at the frame's size
    RwCameraBeginUpdate(cam);

    RwRaster* frame = RwCameraGetRaster(cam);
    F32 w = frame != NULL ? (F32)frame->width : 640.0f;
    F32 h = frame != NULL ? (F32)frame->height : 480.0f;

    // The strength rides in on the vertex alpha, which is where the Xbox puts
    // it: the composite blends SRCALPHA, so this scales the whole glow.
    drawPass(sQuarter.raster, NULL, w, h, rwBLENDSRCALPHA, rwBLENDONE,
             (U8)(strength * 255.0f));

    // Put back what the rest of the frame expects; iScrFxEnd sets some of this
    // again but not the cull mode or the texture.
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
}

#else

// Every other backend, for the reason distort.cpp gives.
void iGlowRender(RwCamera*, F32)
{
}

#endif
