// The Xbox full-screen glow. The chain, the kernel and where each came from are
// in iGlow.h.
//
// D3D9 only, same as snapshot.cpp and distort.cpp: it needs the frame buffer as
// a texture, and the shim has that for one backend.

#include <rwcore.h>

#if defined(RW_D3D9) || defined(RW_D3D11)
#include <windows.h>
#define WITH_D3D
#endif
#if defined(RW_D3D9)
#include <d3d9.h>
#elif defined(RW_D3D11)
#include <d3d11.h>
#endif

#include "rw.h"

#if defined(RW_D3D9) || defined(RW_D3D11)
#include "src/d3d/rwd3dimpl.h"
#endif

#include "iGlow.h"

#include <stdio.h>

// config.ini's xbox.glow, pushed down by iSystem.cpp. On unless something says
// otherwise, so that a target which never calls the setter -- the shim's own
// test does not -- still gets the Xbox behaviour. Outside the backend arms
// because the setter is.
static S32 sEnabled = TRUE;

#if defined(RW_D3D9) || defined(RW_D3D11)

// fxc gives every blob the same name, so each gets its own namespace. PS_NAME
// is what that name is, and librw's rwd3d.h picks it per shader model.
namespace bright_ps
{
#include "glow_bright_PS.h"
}
namespace blur_ps
{
#include "glow_blur_PS.h"
}

// --- the chain's sizes, from the Xbox's three render targets ----------------
//
// 0x370544, 0x370540 and 0x370530, built by the calls at va 0x1712d8, 0x1712ee
// and 0x171304. Halving one axis per pass is what makes a four-tap kernel cover
// as much as it does.
//
// The Xbox's numbers are 320x240, 320x120 and 160x120, which are a half and a
// quarter of its 640x480 frame -- so they are computed from the frame here
// rather than written down. A chain nailed to those three sizes would still
// run at a larger render size, and the glow would tighten as the resolution
// went up: the blur's tap distances are in texels OF THE TEXTURE BEING SAMPLED,
// so what fixes the bloom's radius as a fraction of the screen is the
// downsample RATIO, not the target's size.

// The blur weights, from the Xbox's two tables. They sum to one.
static const F32 kNearWeight = 1.0f / 3.0f;
static const F32 kFarWeight = 1.0f / 6.0f;

// And the tap distances, in texels of the texture being sampled.
static const F32 kNearTap = 1.0f;
static const F32 kFarTap = 3.0f;

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

// The frame and the raster by hand, then the camera. RenderWare leaves both to
// the caller -- librw's Camera::destroy only detaches the frame -- which is the
// same order iCamera.cpp:69 takes them in. There is no Z raster here; makeTarget
// does not give these one.
static void destroyTarget(GlowTarget* t)
{
    if (t->camera != NULL)
    {
        RwFrame* frame = RwCameraGetFrame(t->camera);
        if (frame != NULL)
        {
            RwCameraSetFrame(t->camera, NULL);
            RwFrameDestroy(frame);
        }

        RwCameraSetRaster(t->camera, NULL);
        RwCameraDestroy(t->camera);
    }

    if (t->raster != NULL)
    {
        RwRasterDestroy(t->raster);
    }

    t->camera = NULL;
    t->raster = NULL;
    t->width = 0;
    t->height = 0;
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

    rw::d3d::setPixelShaderConstantF(1, weights, 1);
    rw::d3d::setPixelShaderConstantF(2, offs01, 1);
    rw::d3d::setPixelShaderConstantF(3, offs23, 1);
}

// Copy the frame so it can be sampled. Same as distort.cpp, and the same reason
// the caller has to close the scene around it.
static bool captureScreen()
{
    RwInt32 w = 0;
    RwInt32 h = 0;
    rw::d3d::getScreenExtent(&w, &h);
    if (w <= 0 || h <= 0)
    {
        return false;
    }

    if (sScreen != NULL && (w != sScreen->width || h != sScreen->height))
    {
        RwRasterDestroy(sScreen);
        sScreen = NULL;
    }

    if (sScreen == NULL)
    {
        sScreen = RwRasterCreate(w, h, 32, rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT8888);
        if (sScreen == NULL)
        {
            glowFail("this backend would not make a render-target texture", 0);
            return false;
        }
    }

    if (!rw::d3d::captureFrame(reinterpret_cast<rw::Raster*>(sScreen)))
    {
        glowFail("the frame could not be copied into a texture", 0);
        return false;
    }

    sCapturedInto = rasterTexture(sScreen);
    return true;
}

void iGlowRender(RwCamera* cam, F32 strength)
{
    if (!sEnabled || sFailed || cam == NULL || !rw::d3d::deviceOpen())
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
        sBrightShader = rw::d3d::createPixelShader((void*)bright_ps::PS_NAME);
        sBlurShader = rw::d3d::createPixelShader((void*)blur_ps::PS_NAME);
        if (sBrightShader == NULL || sBlurShader == NULL)
        {
            glowFail("the glow shaders would not compile", 0);
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

    // The chain, sized from the frame that was just captured -- see the note on
    // the ratios above. Made after the capture rather than before it because
    // this is where the frame's size is known, and remade if it ever changes,
    // for the reason snapshot.cpp rebuilds its raster: nothing in the port
    // resizes the render target today, and a stale chain would silently start
    // scaling instead of downsampling.
    {
        RwInt32 halfW = sScreen->width / 2;
        RwInt32 halfH = sScreen->height / 2;
        RwInt32 quarterW = halfW / 2;
        RwInt32 quarterH = halfH / 2;

        // A render size small enough to divide to nothing is not a case worth
        // handling, but a zero-sized raster is a failure several calls further
        // on, so the floor is here rather than there.
        if (quarterW < 1)
        {
            quarterW = 1;
        }
        if (quarterH < 1)
        {
            quarterH = 1;
        }
        if (halfW < 1)
        {
            halfW = 1;
        }
        if (halfH < 1)
        {
            halfH = 1;
        }

        if (sHalf.camera == NULL || sHalf.width != halfW || sHalf.height != halfH)
        {
            destroyTarget(&sHalf);
            destroyTarget(&sVert);
            destroyTarget(&sQuarter);

            if (!makeTarget(&sHalf, halfW, halfH) || !makeTarget(&sVert, halfW, quarterH) ||
                !makeTarget(&sQuarter, quarterW, quarterH))
            {
                glowFail("the glow's render targets would not be made", 0);
                RwCameraBeginUpdate(cam);
                return;
            }
        }
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

// Outside the backend arms: the setting exists whichever backend is linked, and
// a build where the glow cannot run should still answer the same way about
// whether it was asked for.
void iGlowSetEnabled(S32 enabled)
{
    sEnabled = enabled ? TRUE : FALSE;
}
