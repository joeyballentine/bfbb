// The cruise bubble's screen distortion. The interface, and the account of
// where each part of it was recovered from, are in iDistort.h.
//
// Every backend that can hand back the frame buffer as a texture and take a
// pixel shader of the port's own for a 2D primitive: D3D9, D3D11 and GL3. What
// differs between them is gathered in one place below, the way glow.cpp gathers
// its own.

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

#if defined(RW_D3D9) || defined(RW_D3D11)
#include "src/d3d/rwd3dimpl.h"
#endif
// GL3 needs no header of its own here: rw.h includes src/gl/rwgl3.h and
// rwgl3shader.h itself, and neither has an include guard.

#include "backend.h"
#include "iDistort.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

// config.ini's xbox.distortion, pushed down by iSystem.cpp. See glow.cpp.
static S32 sEnabled = TRUE;

#if defined(RW_D3D9) || defined(RW_D3D11) || defined(RW_GL3)

// The compiled shader, whichever backend compiled it. void* for the reason
// glow.cpp gives: a build can carry several, and the handle only ever goes
// back to the backend that made it.
typedef void* DistortShader;

#if defined(RW_D3D9) || defined(RW_D3D11)

// The compiled pixel shader. Built the way librw builds its own, by the
// make_shaders.cmd in each shaders directory. fxc gives every blob in a tree
// the same name, so each gets a namespace of its own, and the tree is named in
// the include because a build can carry both Direct3D backends.
//
// What it computes is read off the Xbox's D3DPIXELSHADERDEF, not guessed; the
// decode is in iDistort.h.
#ifdef RW_D3D9
namespace distort_ps_sm2
{
#include "shaders/distort_PS.h"
} // namespace distort_ps_sm2
#endif
#ifdef RW_D3D11
namespace distort_ps_sm4
{
#include "shaders11/distort_PS.h"
} // namespace distort_ps_sm4
#endif
#endif

#ifdef RW_GL3

namespace
{
// The same shader in GLSL, wrapped one string literal per line by
// shadersgl/gen.py.
#include "shadersgl/distort_gl.inc"
} // namespace
#endif

// --- the numbers, and where they come from ----------------------------------
//
// Every one of these is read out of the Xbox build; none is chosen.

// The displacement at full strength, in pixels. `fmul [0x26d778]` against the
// strength, in the wobble generator at va 0x1708d0. Pixels rather than texture
// coordinates because that generator is handed the screen width and height --
// which is what va 0x279ad4 and 0x279ad8 hold, both set from the frame buffer
// size at va 0x6e44d.
static const F32 kDisplacePixels = 15.0f;

// And the screen those 15 pixels are 15 pixels of. Both consoles rendered at
// 640x480, so this is the frame buffer size at va 0x6e44d and the only reason
// it appears as its own constant is that the port need not render at it.
static const F32 kReferenceWidth = 640.0f;
static const F32 kReferenceHeight = 480.0f;

// Radians per millisecond. `fmul [0x26d774]` in the same function, applied to a
// tick count read through the call at va 0x1b5085.
//
// The millisecond is confirmed, not assumed. That call is a three-instruction
// thunk -- `mov eax, [0x24bc80] / mov eax, [eax] / ret` -- and 0x24bc80 is slot
// 32 of the XBE's kernel import table, holding 0x8000009c: **ordinal 156, which
// is KeTickCount**, the Xbox kernel's millisecond counter. The unsigned
// conversion beside it (the 4294967296.0f fixup) is what that counter needs and
// a frame counter would not.
//
// So a full turn every 3.14 seconds.
static const F32 kRadiansPerMs = 0.002f;

// ---------------------------------------------------------------------------

static RwRaster* sScreen; // the copy of the frame, as a texture
static DistortShader sPixelShader;
static S32 sFailed;

static void distortFail(const char* what, long hr)
{
    sFailed = 1;
    printf("bfbb: the cruise-bubble distortion is off -- %s (0x%08lx)\n", what, (unsigned long)hr);
    fflush(stdout);
}

// --- the backend's half -----------------------------------------------------
//
// The same six things glow.cpp names, minus the blur's constants and plus the
// second texture stage. See the note there.

// One namespace per backend, both compiled when both are linked; which half
// runs is video.backend, resolved before the device opened.

#if defined(RW_D3D9) || defined(RW_D3D11)
namespace d3ddistort
{

    static void* sCapturedInto; // see snapshot.cpp: a device reset empties these

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

    static bool distortDeviceReady()
    {
        return rw::d3d::deviceOpen() != 0;
    }

    static void distortScreenExtent(RwInt32* w, RwInt32* h)
    {
        rw::d3d::getScreenExtent(w, h);
    }

    static bool distortCopyFrame(RwRaster* dst)
    {
        if (!rw::d3d::captureFrame(reinterpret_cast<rw::Raster*>(dst)))
        {
            return false;
        }

        sCapturedInto = rasterTexture(dst);
        return true;
    }

    static bool distortCaptureIsLive()
    {
        return rasterTexture(sScreen) == sCapturedInto;
    }

    static bool distortCreateShader()
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            sPixelShader = rw::d3d::createPixelShader((void*)distort_ps_sm2::g_ps20_main);
        }
#endif
#ifdef RW_D3D11
        if (iBackendIsD3D11())
        {
            sPixelShader = rw::d3d::createPixelShader((void*)distort_ps_sm4::g_main);
        }
#endif
        return sPixelShader != NULL;
    }

    static void distortBindShader()
    {
        rw::d3d::im2dOverridePS = sPixelShader;
    }

    static void distortUnbindShader()
    {
        rw::d3d::im2dOverridePS = NULL;
    }

    static void distortSetSwirlMap(RwTexture* map)
    {
        rw::d3d::setTexture(1, reinterpret_cast<rw::Texture*>(map));
    }

    // c1, because librw owns c0 for the fog colour.
    static void distortUploadDisplacement(F32* displace)
    {
        rw::d3d::setPixelShaderConstantF(1, displace, 1);
    }
} // namespace d3ddistort
#endif

#ifdef RW_GL3
namespace gl3distort
{

    static rw::int32 sDisplaceUniform = -1;

    static bool distortDeviceReady()
    {
        return rw::gl3::virtualScreenFramebuffer() != 0;
    }

    static void distortScreenExtent(RwInt32* w, RwInt32* h)
    {
        *w = (RwInt32)rw::gl3::virtualScreenWidth;
        *h = (RwInt32)rw::gl3::virtualScreenHeight;
    }

    static bool distortCopyFrame(RwRaster* dst)
    {
        return rw::gl3::copyVirtualScreen(reinterpret_cast<rw::Raster*>(dst)) != 0;
    }

    // GL3 has no device-lost equivalent to guard against; snapshot.cpp's GL3 arm
    // says why.
    static bool distortCaptureIsLive()
    {
        return true;
    }

    static bool distortCreateShader()
    {
        const char* vs[] = { rw::gl3::shaderDecl, rw::gl3::header_vert_src, rw::gl3::im2d_vert_src,
                             NULL };
        const char* fs[] = { rw::gl3::shaderDecl, rw::gl3::header_frag_src, distort_frag_src,
                             NULL };

        sPixelShader = (DistortShader)rw::gl3::Shader::create(vs, fs);
        return sPixelShader != NULL;
    }

    static void distortBindShader()
    {
        rw::gl3::im2dOverrideShader = (rw::gl3::Shader*)sPixelShader;
    }

    static void distortUnbindShader()
    {
        rw::gl3::im2dOverrideShader = NULL;
    }

    static void distortSetSwirlMap(RwTexture* map)
    {
        rw::gl3::setTexture(1, reinterpret_cast<rw::Texture*>(map));
    }

    static void distortUploadDisplacement(F32* displace)
    {
        // See iDistortRegisterShaderUniforms for what a -1 means and why
        // registering it here instead would be worse.
        if (sDisplaceUniform < 0)
        {
            return;
        }

        rw::gl3::setUniform(sDisplaceUniform, displace);
    }
} // namespace gl3distort
#endif

// The backend's half, dispatched. A run with no device answers "not ready"
// and the chain stops there.

static bool distortDeviceReady()
{
#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        return d3ddistort::distortDeviceReady();
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        return gl3distort::distortDeviceReady();
    }
#endif
    return false;
}

static void distortScreenExtent(RwInt32* w, RwInt32* h)
{
#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        d3ddistort::distortScreenExtent(w, h);
        return;
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        gl3distort::distortScreenExtent(w, h);
        return;
    }
#endif
}

static bool distortCopyFrame(RwRaster* dst)
{
#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        return d3ddistort::distortCopyFrame(dst);
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        return gl3distort::distortCopyFrame(dst);
    }
#endif
    return false;
}

static bool distortCaptureIsLive()
{
#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        return d3ddistort::distortCaptureIsLive();
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        return gl3distort::distortCaptureIsLive();
    }
#endif
    return true;
}

static bool distortCreateShader()
{
#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        return d3ddistort::distortCreateShader();
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        return gl3distort::distortCreateShader();
    }
#endif
    return false;
}

static void distortBindShader()
{
#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        d3ddistort::distortBindShader();
        return;
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        gl3distort::distortBindShader();
        return;
    }
#endif
}

static void distortUnbindShader()
{
#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        d3ddistort::distortUnbindShader();
        return;
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        gl3distort::distortUnbindShader();
        return;
    }
#endif
}

static void distortSetSwirlMap(RwTexture* map)
{
#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        d3ddistort::distortSetSwirlMap(map);
        return;
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        gl3distort::distortSetSwirlMap(map);
        return;
    }
#endif
}

static void distortUploadDisplacement(F32* displace)
{
#if defined(RW_D3D9) || defined(RW_D3D11)
    if (iBackendIsD3D())
    {
        d3ddistort::distortUploadDisplacement(displace);
        return;
    }
#endif
#ifdef RW_GL3
    if (iBackendIsGL3())
    {
        gl3distort::distortUploadDisplacement(displace);
        return;
    }
#endif
}

// Copy what has just been drawn into a texture we can sample.
//
// The frame buffer of a live camera is a rwRASTERTYPECAMERA raster, and librw
// gives those no D3D texture at all -- they ARE the render target. So it has to
// be copied, which is what the Xbox does too.
//
// StretchRect cannot run with a scene open, so the caller ends the camera's
// update around this and begins it again. That is not a liberty: the Xbox
// brackets its copy with the same pair (va 0x170a28 and 0x170d80).
static bool captureScreen()
{
    RwInt32 w = 0;
    RwInt32 h = 0;
    distortScreenExtent(&w, &h);
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
        // The picture's own size, not the Xbox's 512x512. That size was a
        // console's texture budget; here it would be a downsample and back for
        // nothing, and the shader samples this 1:1 with the quad either way.
        sScreen = RwRasterCreate(w, h, 32, rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT8888);
        if (sScreen == NULL)
        {
            distortFail("this backend would not make a render-target texture", 0);
            return false;
        }
    }

    if (!distortCopyFrame(sScreen))
    {
        distortFail("the frame could not be copied into a texture", 0);
        return false;
    }

    return true;
}

void iDistortRender(RwCamera* cam, RwTexture* map, F32 amount, F32 width, F32 height)
{
    // The Xbox tests the strength against zero first and returns, before it
    // looks at anything else (va 0x170a00). Same here: off costs a compare.
    if (!sEnabled || sFailed || cam == NULL || amount <= 0.0f)
    {
        return;
    }

    if (map == NULL || map->raster == NULL || !distortDeviceReady())
    {
        return;
    }

#ifdef RW_D3D9
    // The warp is a ps_2_0 dependent read. There is no pixel shader to do it
    // in on the fixed-function path, and saying so once is better than a
    // setting that is on and does nothing.
    if (iBackendIsD3D9() && rw::d3d::getFixedFunction())
    {
        distortFail("the fixed-function pipeline has no pixel shader to run it in", 0);
        return;
    }
#endif

    if (width <= 0.0f || height <= 0.0f)
    {
        return;
    }

    if (sPixelShader == NULL)
    {
        if (!distortCreateShader())
        {
            distortFail("the distortion pixel shader would not compile", 0);
            return;
        }
    }

    // Close the scene, copy the frame, open it again.
    RwCameraEndUpdate(cam);
    bool captured = captureScreen();
    RwCameraBeginUpdate(cam);

    if (!captured || !distortCaptureIsLive())
    {
        return;
    }

    // The rotating displacement, in pixels, then in texture coordinates. Two
    // reads of the same clock a moment apart on the Xbox; one here, because
    // they were never meant to differ.
    //
    // Wrapped to one turn in double before it becomes a float. KeTickCount is
    // milliseconds since the console came up and clock() is milliseconds since
    // the process started, and either is past the point where a float can still
    // resolve a millisecond within a day of running -- the angle would go
    // visibly steppy long before the counter itself wrapped.
    double ms = (double)clock() * (1000.0 / CLOCKS_PER_SEC);
    double turn = 2.0 * 3.14159265358979323846;
    F32 phase = (F32)fmod(ms * (double)kRadiansPerMs, turn);

    F32 amplitude = amount * kDisplacePixels;

    // Divided by the size the 15 pixels were MEASURED against, not by the size
    // being rendered at. Those are the same number on the console and on the
    // Xbox, which is why the original divides by the screen -- but taken
    // literally at a larger render size it would shrink the warp, since the
    // same 15 pixels are a smaller fraction of a wider screen. Dividing by the
    // reference keeps the bubble's distortion the same size on screen at any
    // resolution, which is what it is: a fraction of the picture, not a count
    // of pixels.
    F32 displace[4];
    displace[0] = (amplitude * sinf(phase)) / kReferenceWidth;
    displace[1] = (amplitude * cosf(phase)) / kReferenceHeight;
    displace[2] = 0.0f;
    displace[3] = 0.0f;

    distortUploadDisplacement(displace);

    // Stage 0 through the render state, so librw's own cache stays right about
    // it; stage 1 through librw's setter for the same reason. Nothing in the
    // Im2D path touches stage 1, so the map survives the flush.
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, sScreen);
    distortSetSwirlMap(map);

    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);

    // SRC ONE / DEST ZERO: the pass replaces the picture rather than tinting
    // it, which is what the Xbox sets at va 0x170a9d.
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);

    rwGameCube2DVertex vx[4];
    F32 z = RwIm2DGetNearScreenZ();

    for (S32 i = 0; i < 4; i++)
    {
        F32 u = (i & 2) ? 1.0f : 0.0f;
        F32 v = (i & 1) ? 1.0f : 0.0f;

        // Half a pixel left and up, the D3D9 rule: a vertex at screen x lines
        // up with the CENTRE of pixel x, so without this the re-read samples
        // half a texel off and softens the picture it is supposed to be
        // passing through untouched. Measured in the glow, which runs four of
        // these in a row and shows it plainly; this pass is 1:1 so it costs
        // sharpness rather than position, but it is the same mistake.
        //
        // Only where the backend wants it. D3D10 and up put the pixel centre
        // at 0.5 the way OpenGL does, and subtracting there would introduce the
        // offset instead of removing it.
        const F32 half = iBackendHalfPixel();

        vx[i].x = u * width - half;
        vx[i].y = v * height - half;
        vx[i].z = z;
        vx[i].u = u;
        vx[i].v = v;
        vx[i].emissiveColor.red = 0xff;
        vx[i].emissiveColor.green = 0xff;
        vx[i].emissiveColor.blue = 0xff;
        vx[i].emissiveColor.alpha = 0xff;
    }

    distortBindShader();
    RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, &vx[0], 4);
    distortUnbindShader();

    // Put back what the rest of the frame expects. iScrFxEnd runs right after
    // this and sets some of it again, but not the two stages or the cull mode,
    // and leaving a texture bound to stage 1 would follow the next draw.
    distortSetSwirlMap(NULL);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
}

#else

// Every other backend. The copy needs a way to get the frame buffer into a
// texture, and LIBRW_PLATFORM=NULL renders nothing to copy. Stubbed here rather
// than left out of the build so the call site in xScrFx.cpp needs no backend
// #ifdef.

void iDistortRender(RwCamera*, RwTexture*, F32, F32, F32)
{
}

#endif

// Outside the backend arms, for the reason glow.cpp gives.
void iDistortSetEnabled(S32 enabled)
{
    sEnabled = enabled ? TRUE : FALSE;
}

// Name the displacement to librw's GL3 uniform registry. Nothing on the
// backends whose constants are numbered. glow.cpp's own registration says why
// this cannot wait until the shader is built.
void iDistortRegisterShaderUniforms(void)
{
#if defined(RW_GL3)
    gl3distort::sDisplaceUniform =
        rw::gl3::registerUniform("u_distortDisplace", rw::gl3::UNIFORM_VEC4);
#endif
}
