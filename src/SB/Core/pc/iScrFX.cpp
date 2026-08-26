#include "iScrFX.h"

#include "rwplcore.h"
#include <types.h>

static U32 sMotionBlurEnabled;
static RwRaster* g_rast_gctapdance;
static S32 g_alreadyTriedAlloc;
static _iMotionBlurData sMBD = {};

void iScrFxInit()
{
}

void iScrFxBegin()
{
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERNEAREST);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)rwFOGTYPENAFOGTYPE);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, NULL);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, NULL);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
}

void iScrFxEnd()
{
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)rwFOGTYPENAFOGTYPE);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
}

void iScrFxDrawBox(F32 x1, F32 y1, F32 x2, F32 y2, U8 red, U8 green, U8 blue, U8 alpha)
{
    U16 indices[4] = { 0, 1, 2, 3 };
    rwGameCube2DVertex v[4];
    F32 nearZ = RwIm2DGetNearScreenZ();

    RwIm2DVertexSetScreenX(&v[0], x1);
    RwIm2DVertexSetScreenY(&v[0], y1);
    RwIm2DVertexSetScreenZ(&v[0], nearZ);
    RwIm2DVertexSetScreenX(&v[1], x2);
    RwIm2DVertexSetScreenY(&v[1], y1);
    RwIm2DVertexSetScreenZ(&v[1], nearZ);
    RwIm2DVertexSetScreenX(&v[2], x1);
    RwIm2DVertexSetScreenY(&v[2], y2);
    RwIm2DVertexSetScreenZ(&v[2], nearZ);
    RwIm2DVertexSetScreenX(&v[3], x2);
    RwIm2DVertexSetScreenY(&v[3], y2);
    RwIm2DVertexSetScreenZ(&v[3], nearZ);

    for (S32 i = 0; i < 4; i++)
    {
        RwIm2DVertexSetRealRGBA(&v[i], red, green, blue, alpha);
    }

    RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRISTRIP, v, 4, indices, 4);
}

void iCameraMotionBlurActivate(U32 activate)
{
    sMotionBlurEnabled = activate;
}

// The instructions regarding the setting of sMotionBlurEnabled and sMBD.motionBlurAlpha are in the wrong order.
void iCameraSetBlurriness(F32 amount)
{
    if (amount <= 0.0f)
    {
        sMotionBlurEnabled = FALSE;
    }
    else
    {
        if (amount > 1.0f)
        {
            amount = 1.0f;
        }
        sMotionBlurEnabled = TRUE;

        sMBD.motionBlurAlpha = 254.0f * amount + 0.5f;
    }
}

// Instructions in the wrong order.
void iScrFxCameraCreated(RwCamera* pCamera)
{
    sMBD.motionBlurAlpha = 0x90;
    sMBD.motionBlurFrontBuffer = NULL;
    sMBD.index[0] = 0;
    sMBD.index[1] = 1;
    sMBD.index[2] = 2;
    sMBD.index[3] = 0;
    sMBD.index[4] = 2;
    sMBD.index[5] = 3;
    iScrFxMotionBlurOpen(pCamera);
}

void iScrFxCameraEndScene(RwCamera* pCamera)
{
    if (sMotionBlurEnabled && sMBD.motionBlurAlpha != 0)
    {
        iScrFxMotionBlurRender(pCamera, sMBD.motionBlurAlpha & 0xff);
    }
}

void iScrFxPostCameraEnd(RwCamera* pCamera)
{
    GCMB_SiphonFrameBuffer(pCamera);
}

static void iCameraOverlayRender(RwCamera* pCamera, RwRaster* ras, RwRGBA col)
{
    //RwRect rect; // from dwarf, not sure where it's used
    RwRaster* raster = FBMBlur_DebugIntervention(pCamera, ras);

    for (S32 i = 0; i < 4; i++)
    {
        sMBD.vertex[i].emissiveColor.red = col.red;
        sMBD.vertex[i].emissiveColor.green = col.green;
        sMBD.vertex[i].emissiveColor.blue = col.blue;
        sMBD.vertex[i].emissiveColor.alpha = col.alpha;
    }

    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    // The console brackets this draw with RwRasterLock(raster, 0,
    // rwRASTERLOCKREAD) / RwRasterUnlock(raster). That is the ONLY line in this
    // file the port cannot take verbatim, and it is dropped deliberately.
    //
    // Locking a raster for READ is RenderWare's "give me a CPU pointer to these
    // pixels". Nothing here reads the pointer -- the return value is discarded
    // and the very next statement draws WITH the raster as the texture. On the
    // GameCube that is a cache-coherency call against the embedded framebuffer
    // copy and costs nothing; on a host it is the opposite of what is wanted,
    // because a texture that is locked is not available to the device to sample
    // from. librw's D3D9 rasterLock takes the surface away from the GPU for the
    // duration, so honouring the bracket would leave this quad untextured, and
    // possibly stall the pipeline once a frame, to serve a pointer no one uses.
    //
    // The port's shim has neither RwRasterLock nor RwRasterUnlock (see the
    // report; they would forward to rw::Raster::lock/unlock), and this is the
    // only call site the PC build compiles, so nothing else is waiting on them.
    //
    // Worth recording that this whole function is unreachable on the console
    // too: iCameraOverlayRender is called only from iScrFxMotionBlurRender,
    // which returns early unless sMBD.motionBlurFrontBuffer is non-NULL, and
    // nothing ever assigns it one -- iScrFxMotionBlurOpen returns 0 without
    // allocating and GCMB_MakeFrameBufferCopy has an empty body in retail. The
    // GameCube motion blur was cut before ship and the state machine that
    // drives it (iCameraSetBlurriness, iCameraMotionBlurActivate) was left in.
    // So this is a divergence in code that does not run, on either platform.
    RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, sMBD.vertex, 4, sMBD.index, 6);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
}

inline RwRaster* FBMBlur_DebugIntervention(RwCamera* camera, RwRaster* ras)
{
    return ras;
}

S32 iScrFxMotionBlurOpen(RwCamera* camera)
{
    return 0;
}

S32 iScrFxCameraDestroyed(RwCamera* pCamera)
{
    GCMB_KillFrameBufferCopy();
    if (sMBD.motionBlurFrontBuffer != NULL)
    {
        RwRasterDestroy(sMBD.motionBlurFrontBuffer);
        sMBD.motionBlurFrontBuffer = NULL;
        return 1;
    }
    return 0;
}

void iScrFxMotionBlurRender(RwCamera* camera, U32 alpha)
{
    if (sMBD.motionBlurFrontBuffer != NULL)
    {
        RwRGBA col = { 0xff, 0xff, 0xff, (U8)alpha };
        iCameraOverlayRender(camera, (RwRaster*)sMBD.motionBlurFrontBuffer, col);
    }
}

void GCMB_MakeFrameBufferCopy(const RwCamera* camera)
{
}

void GCMB_KillFrameBufferCopy()
{
    if (g_rast_gctapdance != NULL)
    {
        RwRasterDestroy(g_rast_gctapdance);
    }
    g_rast_gctapdance = NULL;
    g_alreadyTriedAlloc = 0;
}

void GCMB_SiphonFrameBuffer(const RwCamera* camera)
{
    if ((g_rast_gctapdance == NULL) && (g_alreadyTriedAlloc == 0))
    {
        GCMB_MakeFrameBufferCopy(camera);
    }
    if (g_rast_gctapdance != NULL)
    {
        RwGameCubeCameraTextureFlush(g_rast_gctapdance, 0);
    }
}
