// The Xbox full-screen glow. The chain, the kernel and where each came from are
// in iGlow.h.
//
// Every backend that can hand back the frame buffer as a texture and take a
// pixel shader of the port's own for a 2D primitive: D3D9, D3D11 and GL3. What
// differs between them is the shader language, how a constant is named and how
// the frame is copied. Everything else -- the chain, its sizes, the quad and
// the render states -- is written once below.

#include <rwcore.h>

#if defined(RW_D3D9) || defined(RW_D3D11)
#include <windows.h>
#define WITH_D3D
#endif
#ifdef RW_D3D9
#include <d3d9.h>
#endif
#ifdef RW_D3D11
#include <d3d11.h>
#endif

#include "rw.h"

#include "backend.h"

#if defined(RW_D3D9) || defined(RW_D3D11)
#include "src/d3d/rwd3dimpl.h"
#endif
// GL3 needs no header of its own here: rw.h includes src/gl/rwgl3.h and
// rwgl3shader.h itself, and neither has an include guard.

#include "iGlow.h"

#include <stdio.h>

// config.ini's xbox.glow, pushed down by iSystem.cpp. On unless something says
// otherwise, so that a target which never calls the setter -- the shim's own
// test does not -- still gets the Xbox behaviour. Outside the backend arms
// because the setter is.
static S32 sEnabled = TRUE;

#if defined(RW_D3D9) || defined(RW_D3D11) || defined(RW_GL3)

// The compiled shader, whichever backend compiled it. void* rather than a
// backend's own handle: the chain below only ever passes one back to the
// backend that made it, and a build can carry several.
typedef void* GlowShader;

#if defined(RW_D3D9) || defined(RW_D3D11)
// fxc gives every blob in a tree the same name, so each gets a namespace of its
// own, and the tree is named in the include because a build can carry both
// Direct3D backends and the two trees use the same file names.
#ifdef RW_D3D9
namespace bright_ps_sm2
{
#include "shaders/glow_bright_PS.h"
}
namespace blur_ps_sm2
{
#include "shaders/glow_blur_PS.h"
}
#endif
#ifdef RW_D3D11
namespace bright_ps_sm4
{
#include "shaders11/glow_bright_PS.h"
}
namespace blur_ps_sm4
{
#include "shaders11/glow_blur_PS.h"
}
#endif
#endif

#ifdef RW_GL3
// GLSL, wrapped one string literal per line by shadersgl/gen.py. Two files
// rather than two namespaces: each declares a variable of its own name, which
// fxc's blobs do not.
namespace
{
#include "shadersgl/glow_blur_gl.inc"
#include "shadersgl/glow_bright_gl.inc"
} // namespace
#endif

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

static RwRaster* sScreen; // the frame, copied so it can be sampled
static GlowTarget sHalf, sVert, sQuarter;
static GlowShader sBrightShader;
static GlowShader sBlurShader;
static S32 sFailed;

static void glowFail(const char* what, long hr)
{
    sFailed = 1;
    printf("bfbb: the glow is off -- %s (0x%08lx)\n", what, (unsigned long)hr);
    fflush(stdout);
}

// --- the backend's half -----------------------------------------------------
//
// Six things the chain below needs and cannot say in one language: whether
// there is a device, how big the picture is, how to copy it, how to build the
// two shaders, how to hang one on the next 2D primitive, and how to hand it
// three constants.

// One namespace per backend, and both compile when both are linked. The
// dispatchers under them are what the chain calls; which half runs is
// video.backend, resolved before the device opened.

#if defined(RW_D3D9) || defined(RW_D3D11)
namespace d3dglow
{

    // The D3D texture behind the copy at the moment it was written to. See
    // snapshot.cpp: a device reset empties every D3DPOOL_DEFAULT surface without
    // invalidating the Raster* in front of it.
    static void* sCapturedInto;

    static inline void* rasterTexture(RwRaster* raster)
    {
        rw::Raster* r = reinterpret_cast<rw::Raster*>(raster);
#ifdef RW_D3D11
        if (iBackendIsD3D11())
        {
            // D3D11 keeps the system-memory copy in `texture` and the GPU's
            // own in `tex11`; D3D9 has only the one.
            return GETD3DRASTEREXT(r)->tex11;
        }
#endif
        return GETD3DRASTEREXT(r)->texture;
    }

    static bool glowDeviceReady()
    {
        return rw::d3d::deviceOpen() != 0;
    }

    static void glowScreenExtent(RwInt32* w, RwInt32* h)
    {
        rw::d3d::getScreenExtent(w, h);
    }

    static bool glowCopyFrame(RwRaster* dst)
    {
        if (!rw::d3d::captureFrame(reinterpret_cast<rw::Raster*>(dst)))
        {
            return false;
        }

        sCapturedInto = rasterTexture(dst);
        return true;
    }

    // False when the copy was taken but the surface it went into is gone.
    static bool glowCaptureIsLive()
    {
        return rasterTexture(sScreen) == sCapturedInto;
    }

    static bool glowCreateShaders()
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            sBrightShader = rw::d3d::createPixelShader((void*)bright_ps_sm2::g_ps20_main);
            sBlurShader = rw::d3d::createPixelShader((void*)blur_ps_sm2::g_ps20_main);
        }
#endif
#ifdef RW_D3D11
        if (iBackendIsD3D11())
        {
            sBrightShader = rw::d3d::createPixelShader((void*)bright_ps_sm4::g_main);
            sBlurShader = rw::d3d::createPixelShader((void*)blur_ps_sm4::g_main);
        }
#endif
        return sBrightShader != NULL && sBlurShader != NULL;
    }

    static void glowBindShader(GlowShader shader)
    {
        rw::d3d::im2dOverridePS = shader;
    }

    static void glowUnbindShader()
    {
        rw::d3d::im2dOverridePS = NULL;
    }

    // c1, c2 and c3, because librw owns c0 for the fog colour.
    static void glowUploadBlurConstants(F32* weights, F32* offs01, F32* offs23)
    {
        rw::d3d::setPixelShaderConstantF(1, weights, 1);
        rw::d3d::setPixelShaderConstantF(2, offs01, 1);
        rw::d3d::setPixelShaderConstantF(3, offs23, 1);
    }
} // namespace d3dglow
#endif

#ifdef RW_GL3
namespace gl3glow
{

    // GL3 has no device-lost equivalent to guard against, for the reason
    // snapshot.cpp's GL3 arm gives, so there is no captured-into pointer here.
    static rw::int32 sWeightsUniform = -1;
    static rw::int32 sOffs01Uniform = -1;
    static rw::int32 sOffs23Uniform = -1;

    static bool glowDeviceReady()
    {
        return rw::gl3::virtualScreenFramebuffer() != 0;
    }

    static void glowScreenExtent(RwInt32* w, RwInt32* h)
    {
        *w = (RwInt32)rw::gl3::virtualScreenWidth;
        *h = (RwInt32)rw::gl3::virtualScreenHeight;
    }

    static bool glowCopyFrame(RwRaster* dst)
    {
        return rw::gl3::copyVirtualScreen(reinterpret_cast<rw::Raster*>(dst)) != 0;
    }

    static bool glowCaptureIsLive()
    {
        return true;
    }

    static bool glowCreateShaders()
    {
        // librw's own im2d vertex stage, which is the only one the quad's
        // coordinates and the xform uniform agree with, and its fragment header,
        // which is where DoAlphaTest and the state uniforms are declared.
        const char* vs[] = { rw::gl3::shaderDecl, rw::gl3::header_vert_src, rw::gl3::im2d_vert_src,
                             NULL };

        const char* brightFs[] = { rw::gl3::shaderDecl, rw::gl3::header_frag_src,
                                   glow_bright_frag_src, NULL };
        const char* blurFs[] = { rw::gl3::shaderDecl, rw::gl3::header_frag_src, glow_blur_frag_src,
                                 NULL };

        sBrightShader = (GlowShader)rw::gl3::Shader::create(vs, brightFs);
        sBlurShader = (GlowShader)rw::gl3::Shader::create(vs, blurFs);

        return sBrightShader != NULL && sBlurShader != NULL;
    }

    static void glowBindShader(GlowShader shader)
    {
        rw::gl3::im2dOverrideShader = (rw::gl3::Shader*)shader;
    }

    static void glowUnbindShader()
    {
        rw::gl3::im2dOverrideShader = NULL;
    }

    static void glowUploadBlurConstants(F32* weights, F32* offs01, F32* offs23)
    {
        // Nothing to upload before the uniforms exist, and a -1 here means
        // iGlowRegisterShaderUniforms was never called. Registering them now would
        // work and then print a line per uniform per flush for the rest of the run;
        // see that function.
        if (sWeightsUniform < 0)
        {
            return;
        }

        rw::gl3::setUniform(sWeightsUniform, weights);
        rw::gl3::setUniform(sOffs01Uniform, offs01);
        rw::gl3::setUniform(sOffs23Uniform, offs23);
    }
} // namespace gl3glow
#endif

// The six things, dispatched. A build with no real backend, or a run with
// video.backend = null, answers "no device" and the chain stops there.

static bool glowDeviceReady()
{
#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        return d3dglow::glowDeviceReady();
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        return gl3glow::glowDeviceReady();
    }
#endif
    return false;
}

static void glowScreenExtent(RwInt32* w, RwInt32* h)
{
#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        d3dglow::glowScreenExtent(w, h);
        return;
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        gl3glow::glowScreenExtent(w, h);
        return;
    }
#endif
    *w = 0;
    *h = 0;
}

static bool glowCopyFrame(RwRaster* dst)
{
#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        return d3dglow::glowCopyFrame(dst);
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        return gl3glow::glowCopyFrame(dst);
    }
#endif
    return false;
}

static bool glowCaptureIsLive()
{
#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        return d3dglow::glowCaptureIsLive();
    }
#endif
    return true;
}

static bool glowCreateShaders()
{
#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        return d3dglow::glowCreateShaders();
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        return gl3glow::glowCreateShaders();
    }
#endif
    return false;
}

static void glowBindShader(GlowShader shader)
{
#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        d3dglow::glowBindShader(shader);
        return;
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        gl3glow::glowBindShader(shader);
    }
#endif
}

static void glowUnbindShader()
{
#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        d3dglow::glowUnbindShader();
        return;
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        gl3glow::glowUnbindShader();
    }
#endif
}

static void glowUploadBlurConstants(F32* weights, F32* offs01, F32* offs23)
{
#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        d3dglow::glowUploadBlurConstants(weights, offs01, offs23);
        return;
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        gl3glow::glowUploadBlurConstants(weights, offs01, offs23);
    }
#endif
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
static void drawPass(RwRaster* src, GlowShader shader, F32 w, F32 h, RwBlendFunction srcBlend,
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

        // Half a pixel left and up, on the backends that need it. D3D9 aligns
        // a vertex at screen x with the CENTRE of pixel x, not its corner, so
        // a screen-aligned quad drawn at 0..w samples half a texel off -- and
        // it compounds down a chain like this one: measured at +0.5 texels per
        // pass, which is half a screen pixel at the first and two at the
        // composite, four or five all told.
        //
        // The Xbox does this too, at the head of its blur helper (va 0x171336
        // pushes -0.5 twice). librw's im2d transform carries no half-pixel term
        // of its own, so it has to be here.
        //
        // iBackendHalfPixel is what decides, and it is a runtime question now
        // that one executable carries several backends: D3D10 and up put the
        // pixel centre at 0.5 the way OpenGL does, and subtracting there would
        // move the whole chain up and left instead.
        const F32 half = iBackendHalfPixel();

        vx[i].x = u * w - half;
        vx[i].y = v * h - half;
        vx[i].z = z;
        vx[i].u = u;
        vx[i].v = v;
        vx[i].emissiveColor.red = 0xff;
        vx[i].emissiveColor.green = 0xff;
        vx[i].emissiveColor.blue = 0xff;
        vx[i].emissiveColor.alpha = alpha;
    }

    glowBindShader(shader);
    RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, &vx[0], 4);
    glowUnbindShader();
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

    glowUploadBlurConstants(weights, offs01, offs23);
}

// Copy the frame so it can be sampled. Same as distort.cpp, and the same reason
// the caller has to close the scene around it.
static bool captureScreen()
{
    RwInt32 w = 0;
    RwInt32 h = 0;
    glowScreenExtent(&w, &h);
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

    if (!glowCopyFrame(sScreen))
    {
        glowFail("the frame could not be copied into a texture", 0);
        return false;
    }

    return true;
}

void iGlowRender(RwCamera* cam, F32 strength)
{
    if (!sEnabled || sFailed || cam == NULL || !glowDeviceReady())
    {
        return;
    }

#ifdef RW_D3D9
    // The bright pass and both blurs are ps_2_0. There is no pixel shader to
    // run them in on the fixed-function path, and saying so once is better
    // than a setting that is on and does nothing.
    if (iBackendIsD3D9() && rw::d3d::getFixedFunction())
    {
        glowFail("the fixed-function pipeline has no pixel shader to run it in", 0);
        return;
    }
#endif

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
        if (!glowCreateShaders())
        {
            glowFail("the glow shaders would not compile", 0);
            return;
        }
    }

    // The passes render into their own cameras, and the copy needs the scene
    // closed, so the frame's camera goes down for the duration. The Xbox does
    // the same, at va 0x171e5e and 0x1720f0.
    RwCameraEndUpdate(cam);

    if (!captureScreen() || !glowCaptureIsLive())
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
    drawPass(sScreen, sBrightShader, (F32)sHalf.width, (F32)sHalf.height, rwBLENDONE, rwBLENDZERO);
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
    drawPass(sVert.raster, sBlurShader, (F32)sQuarter.width, (F32)sQuarter.height, rwBLENDONE,
             rwBLENDZERO);
    RwCameraEndUpdate(sQuarter.camera);

    // 4. add it back over the frame, at the frame's size
    RwCameraBeginUpdate(cam);

    RwRaster* frame = RwCameraGetRaster(cam);
    F32 w = frame != NULL ? (F32)frame->width : 640.0f;
    F32 h = frame != NULL ? (F32)frame->height : 480.0f;

    // The strength rides in on the vertex alpha, which is where the Xbox puts
    // it: the composite blends SRCALPHA, so this scales the whole glow.
    drawPass(sQuarter.raster, NULL, w, h, rwBLENDSRCALPHA, rwBLENDONE, (U8)(strength * 255.0f));

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

// LIBRW_PLATFORM=NULL, for the reason distort.cpp gives.
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

// Name the glow's two constants to librw's GL3 uniform registry, which is one
// list shared by every shader in the process.
//
// It has to happen before ANY shader is created, and that is the whole reason
// this is a separate call rather than part of building the shaders. A Shader
// records how many uniforms the registry held when it was made, and
// flushUniforms prints a line for every uniform past that number -- so a
// registration after librw has built its own pipelines would put a printf in
// every flush of every one of them, forever.
//
// Nothing here touches GL, so RwEngineOpen is early enough and there need not
// be a context yet.
void iGlowRegisterShaderUniforms(void)
{
#if defined(RW_GL3)
    gl3glow::sWeightsUniform = rw::gl3::registerUniform("u_glowWeights", rw::gl3::UNIFORM_VEC4);
    gl3glow::sOffs01Uniform = rw::gl3::registerUniform("u_glowOffs01", rw::gl3::UNIFORM_VEC4);
    gl3glow::sOffs23Uniform = rw::gl3::registerUniform("u_glowOffs23", rw::gl3::UNIFORM_VEC4);
#endif
}
