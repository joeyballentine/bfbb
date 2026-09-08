// The renderer's own picture of the frame, drawn in the corner of it. What it
// is for is in iDebugView.h.

#include <rwcore.h>

#include "rw.h"

#include "iDebugView.h"

#include <stdio.h>

// Pushed down by iSystem.cpp. Outside the backend arms because the setter is.
static S32 sMode = IDEBUGVIEW_OFF;

#if defined(RW_GL3)

namespace
{
#include "debug_depth_gl.inc"
}

// How much of the screen one inset takes, along its width.
static const F32 kInsetFraction = 1.0f / 3.0f;

// And how far its edge sits from the screen's, in the same units.
static const F32 kInsetMargin = 0.02f;

static rw::gl3::Shader* sShader;
static rw::int32 sDepthUniform = -1;
static S32 sFailed;

static void debugViewFail(const char* what)
{
    sFailed = 1;
    printf("bfbb: the debug view is off -- %s\n", what);
    fflush(stdout);
}

// One quad, textured with whatever is bound, through `shader`. Screen
// coordinates, the same space the glow's passes use.
static void drawQuad(F32 x, F32 y, F32 w, F32 h, rw::gl3::Shader* shader, U8 grey)
{
    rwGameCube2DVertex vx[4];
    F32 z = RwIm2DGetNearScreenZ();

    for (S32 i = 0; i < 4; i++)
    {
        F32 u = (i & 2) ? 1.0f : 0.0f;
        F32 v = (i & 1) ? 1.0f : 0.0f;

        // No half-pixel shift: RWHALFPIXEL is a D3D9 rule and this arm is GL3.
        vx[i].x = x + u * w;
        vx[i].y = y + v * h;
        vx[i].z = z;
        vx[i].u = u;
        vx[i].v = v;
        vx[i].emissiveColor.red = grey;
        vx[i].emissiveColor.green = grey;
        vx[i].emissiveColor.blue = grey;
        vx[i].emissiveColor.alpha = 0xff;
    }

    rw::gl3::im2dOverrideShader = shader;
    RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, &vx[0], 4);
    rw::gl3::im2dOverrideShader = NULL;
}

// The depth copy, on stage 1, in one of the two views. `x` and `y` are the
// inset's top-left corner.
static void drawDepthInset(RwCamera* cam, F32 x, F32 y, F32 w, F32 h, F32 mode)
{
    // A dark border, so the inset reads as a panel over whatever is behind it.
    const F32 kBorder = 2.0f;
    drawQuad(x - kBorder, y - kBorder, w + 2.0f * kBorder, h + 2.0f * kBorder, NULL, 0x00);

    if (!rw::gl3::bindVirtualScreenDepth(1))
    {
        debugViewFail("the depth buffer could not be copied into a texture");
        return;
    }

    F32 params[4];
    params[0] = RwCameraGetNearClipPlane(cam);
    params[1] = RwCameraGetFarClipPlane(cam);
    params[2] = mode;
    params[3] = 0.0f;

    if (sDepthUniform >= 0)
    {
        rw::gl3::setUniform(sDepthUniform, params);
    }

    drawQuad(x, y, w, h, sShader, 0xff);

    rw::gl3::unbindVirtualScreenDepth(1);
}

void iDebugViewRender(RwCamera* cam)
{
    if (sMode == IDEBUGVIEW_OFF || sFailed || cam == NULL)
    {
        return;
    }

    // The screen, not a render target of somebody else's. The glow's chain and
    // the shadow cameras end their updates through the same call, and neither
    // wants an inset painted into it.
    RwRaster* frame = RwCameraGetRaster(cam);
    if (frame == NULL || RwRasterGetType(frame) != rwRASTERTYPECAMERA)
    {
        return;
    }

    if (sShader == NULL)
    {
        const char* vs[] = { rw::gl3::shaderDecl, rw::gl3::header_vert_src,
                             rw::gl3::im2d_vert_src, NULL };
        const char* fs[] = { rw::gl3::shaderDecl, rw::gl3::header_frag_src,
                             debug_depth_frag_src, NULL };

        sShader = rw::gl3::Shader::create(vs, fs);
        if (sShader == NULL)
        {
            debugViewFail("the debug view's shader would not compile");
            return;
        }
    }

    F32 screenW = (F32)frame->width;
    F32 screenH = (F32)frame->height;

    // The inset keeps the screen's shape, because it IS the screen: a surface
    // in the wrong place in the inset is in the same place in the picture.
    F32 w = screenW * kInsetFraction;
    F32 h = w * (screenH / screenW);
    F32 margin = screenW * kInsetMargin;
    F32 y = screenH - h - margin;

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);

    if (sMode == IDEBUGVIEW_BOTH)
    {
        drawDepthInset(cam, margin, y, w, h, 0.0f);
        drawDepthInset(cam, screenW - w - margin, y, w, h, 1.0f);
    }
    else
    {
        F32 mode = (sMode == IDEBUGVIEW_BANDS) ? 1.0f : 0.0f;
        drawDepthInset(cam, screenW - w - margin, y, w, h, mode);
    }

    // Put back what the rest of the frame expects, the way the glow does.
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
}

#else

// Every other backend. Getting the depth buffer back as a texture is the whole
// of this, and only the GL3 device has a way to do it. Stubbed here rather than
// left out of the build so the call site in camera.cpp needs no backend #ifdef.
void iDebugViewRender(RwCamera*)
{
}

#endif

void iDebugViewSetMode(S32 mode)
{
    sMode = mode;
}

void iDebugViewRegisterShaderUniforms(void)
{
#if defined(RW_GL3)
    sDepthUniform = rw::gl3::registerUniform("u_debugDepth", rw::gl3::UNIFORM_VEC4);
#endif
}
