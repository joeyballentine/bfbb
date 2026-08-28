// The cruise bubble's screen distortion. The interface, and the account of
// where each part of it was recovered from, are in iDistort.h.
//
// D3D9 only, for the same reason snapshot.cpp is: the one thing this needs is a
// way to get the frame buffer into a texture, and the shim has that for one
// backend.

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

#include "iDistort.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

// config.ini's xbox.distortion, pushed down by iSystem.cpp. See glow.cpp.
static S32 sEnabled = TRUE;

#if defined(RW_D3D9)

namespace
{
// The compiled pixel shader. Built the way librw builds its own --
// `fxc /T ps_2_0 /Fh` -- see shaders/make_distort.cmd. The blob is named
// g_ps20_main by fxc, which is also what librw's shader headers are called, so
// it is kept in here rather than at file scope.
//
// What it computes is read off the Xbox's D3DPIXELSHADERDEF, not guessed; the
// decode is in iDistort.h.
#include "shaders/distort_PS.h"
}

// --- the numbers, and where they come from ----------------------------------
//
// Every one of these is read out of the Xbox build; none is chosen.

// The displacement at full strength, in pixels. `fmul [0x26d778]` against the
// strength, in the wobble generator at va 0x1708d0. Pixels rather than texture
// coordinates because that generator is handed the screen width and height --
// which is what va 0x279ad4 and 0x279ad8 hold, both set from the frame buffer
// size at va 0x6e44d.
static const F32 kDisplacePixels = 15.0f;

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

static RwRaster* sScreen;      // the copy of the frame, as a texture
static void* sPixelShader;
static S32 sFailed;
static void* sCapturedInto;    // see snapshot.cpp: a device reset empties these

static void distortFail(const char* what, long hr)
{
    sFailed = 1;
    printf("bfbb: the cruise-bubble distortion is off -- %s (0x%08lx)\n", what,
           (unsigned long)hr);
    fflush(stdout);
}

static inline void* rasterTexture(RwRaster* raster)
{
    rw::Raster* r = reinterpret_cast<rw::Raster*>(raster);
    return GETD3DRASTEREXT(r)->texture;
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
        // The picture's own size, not the Xbox's 512x512. That size was a
        // console's texture budget; here it would be a downsample and back for
        // nothing, and the shader samples this 1:1 with the quad either way.
        sScreen = RwRasterCreate(desc.Width, desc.Height, 32,
                                 rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT8888);
        if (sScreen == NULL)
        {
            distortFail("this backend would not make a render-target texture", 0);
            return false;
        }
    }

    IDirect3DTexture9* tex = (IDirect3DTexture9*)rasterTexture(sScreen);
    if (tex == NULL)
    {
        return false;   // between a device reset and librw recreating it
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
        distortFail("the frame could not be copied into a texture", hr);
        return false;
    }

    sCapturedInto = tex;
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

    if (map == NULL || map->raster == NULL || rw::d3d::d3ddevice == NULL)
    {
        return;
    }

    if (width <= 0.0f || height <= 0.0f)
    {
        return;
    }

    if (sPixelShader == NULL)
    {
        sPixelShader = rw::d3d::createPixelShader((void*)g_ps20_main);
        if (sPixelShader == NULL)
        {
            distortFail("the distortion pixel shader would not compile", 0);
            return;
        }
    }

    // Close the scene, copy the frame, open it again.
    RwCameraEndUpdate(cam);
    bool captured = captureScreen();
    RwCameraBeginUpdate(cam);

    if (!captured || rasterTexture(sScreen) != sCapturedInto)
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

    F32 displace[4];
    displace[0] = (amplitude * sinf(phase)) / width;
    displace[1] = (amplitude * cosf(phase)) / height;
    displace[2] = 0.0f;
    displace[3] = 0.0f;

    // c1, because librw owns c0 for the fog colour.
    rw::d3d::d3ddevice->SetPixelShaderConstantF(1, displace, 1);

    // Stage 0 through the render state, so librw's own cache stays right about
    // it; stage 1 through librw's setter for the same reason. Nothing in the
    // Im2D path touches stage 1, so the map survives the flush.
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, sScreen);
    rw::d3d::setTexture(1, reinterpret_cast<rw::Texture*>(map));

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
        vx[i].x = u * width - 0.5f;
        vx[i].y = v * height - 0.5f;
        vx[i].z = z;
        vx[i].u = u;
        vx[i].v = v;
        vx[i].emissiveColor.red = 0xff;
        vx[i].emissiveColor.green = 0xff;
        vx[i].emissiveColor.blue = 0xff;
        vx[i].emissiveColor.alpha = 0xff;
    }

    rw::d3d::im2dOverridePS = sPixelShader;
    RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, &vx[0], 4);
    rw::d3d::im2dOverridePS = NULL;

    // Put back what the rest of the frame expects. iScrFxEnd runs right after
    // this and sets some of it again, but not the two stages or the cull mode,
    // and leaving a texture bound to stage 1 would follow the next draw.
    rw::d3d::setTexture(1, NULL);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
}

#else

// Every other backend. The copy needs a way to get the frame buffer into a
// texture and the shim has one for D3D9 alone; NULL renders nothing to copy and
// the GL3 arm does not exist yet. Stubbed here rather than left out of the
// build so the call site in xScrFx.cpp needs no backend #ifdef.

void iDistortRender(RwCamera*, RwTexture*, F32, F32, F32)
{
}

#endif

// Outside the backend arms, for the reason glow.cpp gives.
void iDistortSetEnabled(S32 enabled)
{
    sEnabled = enabled ? TRUE : FALSE;
}
