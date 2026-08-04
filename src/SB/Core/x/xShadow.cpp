#include "xShadow.h"

#include "rpworld.h"

#include "xMath.h"
#include "xRay3.h"
#include "iCollide.h"
#include "iCamera.h"
#include "xNPCBasic.h"
#include "iModel.h"
#include "zBase.h"
#include "zEnt.h"
#include "zGlobals.h"

#include <types.h>

RwRGBAReal ShadowLightColor = { 1.0f, 1.0f, 1.0f, 1.0f };
RwV3d gCamPos = { 0.0f, 0.0f, 0.0f };

F32 ShadowStrength = 0.3f;
static F32 rscale = 1.0f;

RpLight* volatile ShadowLight;
static F32 SHADOW_BF_DOT;
static F32 SHADOW_BOTH;
static RxObjSpace3DVertex* Im3DBuffer;
static U32 Im3DBufferPos;
RwCamera* volatile ShadowCamera;
RwRaster* volatile ShadowCameraRaster;
static RwRaster* ShadowRenderRaster;
U32 gShadowFlags;
F32 gShadowObjectRadius;
static S32 shadow_ent_count;
static S32 sShadowCollJSP;
static RwRaster* gc_saveraster;
static xEnt* sEntSelf;
static xShadowMgr* sMgrList;
static S32 sMgrCount;
static S32 sMgrTotal;

void xShadowInit();
static void ShadowCameraDestroy(RwCamera* shadowCamera);
static S32 SetupShadow();
static RwRaster* ShadowRasterCreate(S32 res);
static RwCamera* ShadowCameraCreatePersp(S32 param);
U32 xShadowCameraCreate();
void xShadowRenderWorld(xVec3* center, F32 radius, F32 max_dist);
void xShadowRender(xVec3* center, F32 radius, F32 max_dist);
void xShadow_ListAdd(xEnt* ent);
void xShadowManager_Add(xEnt* ent);
static void GCSaveFrameBuffer();
static RwCamera* ShadowCameraSetSpherePersp(RwCamera* camera, RwV3d* center, F32 radius);
int Im2DRenderQuad(float x1, float y1, float x2, float y2, float z, float recipCamZ, float uvOffset);

static RwCamera* ShadowCameraUpdate(RwCamera* shadowCamera, void* model, void (*renderCB)(void*),
                                    xVec3* center, F32 radius, S32 shadowMode);
static void InvertRaster(RwCamera* shadowCamera);
static void GCRestoreFrameBuffer();

void xShadowInit()
{
    xShadowCameraCreate();
    gc_saveraster = RwRasterCreate(256, 256, 32, 0x504);
    shadow_ent_count = 0;
    ShadowLight = RpLightCreate(1);
    RpLightSetColor(ShadowLight, &ShadowLightColor);
    RwFrame* frame = RwFrameCreate();
    _rwObjectHasFrameSetFrame(ShadowLight, frame);
}

void xShadowRender(xVec3* center, F32 radius, F32 max_dist)
{
    xShadowRenderWorld(center, radius, max_dist);
}

void xShadowSetLight(xVec3* target_pos, xVec3* in_vec, F32 dst_cast)
{
    xVec3 zvec;
    xMat4x3 matrix;

    xVec3Normalize(&zvec, in_vec);
    xMat3x3LookVec(&matrix, &zvec);
    matrix.pos = *target_pos;

    RwFrame* camFrame = (RwFrame*)ShadowCamera->object.object.parent;
    RwMatrixTag* camMatrix = &camFrame->modelling;

    xMat4x3Copy((xMat4x3*)camMatrix, &matrix);
    RwFrameOrthoNormalize(camFrame);
    RwMatrixUpdate(camMatrix);
    RwFrameUpdateObjects(camFrame);
}

static S32 SetupShadow()
{
    S32 res = 256;

    // Continuously halve res until it is less than or
    // equal to either display width or height.
    // On GCN, this routine normally won't happen,
    // as we're already below both dimensions.
    for (; (res > 640) || (res > 480); res >>= 1);

    ShadowCamera = ShadowCameraCreatePersp(res);
    if (ShadowCamera == NULL)
    {
        return 0;
    }
    ShadowCameraRaster = ShadowRasterCreate(res);

    RwRaster* raster = ShadowCameraRaster;
    if (raster == NULL)
    {
        return 0;
    }

    ShadowCamera->frameBuffer = raster;
    return 1;
}

void xShadowSetWorld(RpWorld* world)
{
    RpWorldAddCamera(world, ShadowCamera);
    SHADOW_BOTH = 2.0f;
}

U32 xShadowCameraCreate()
{
    U32 setup = SetupShadow();
    return ((-setup | setup) >> 0x1f);
}

void xShadowCameraUpdate(void* model, void(*renderCB)(void*), xVec3* center, float radius, int shadowMode)
{
    ShadowCameraSetSpherePersp(ShadowCamera, (RwV3d*)center, radius);
    ShadowCameraUpdate(ShadowCamera, model, renderCB, center, radius, shadowMode);
    ShadowRenderRaster = ShadowCameraRaster;
}

static void modelRenderCB(void* param)
{
    xModelRender((xModelInstance*)param);
}

S32 xShadowReceiveShadowSetup(xEnt* ent)
{
    if
    (
    (ent->model != NULL) &&
    (xEntIsVisible(ent)) &&
    (ent->baseFlags & 0x10) &&
    (!iModelCull(ent->model->Data, ent->model->Mat))
    )
    {
        return 1;
    }
    return 0;
}

void xShadow_ListAdd(xEnt* ent)
{
    xShadowManager_Add(ent);
}

void xShadowRender(xEnt* ent, F32 max_dist)
{
    xVec3 center;
    F32 radius;

    zEntGetShadowParams(ent, &center, &radius, xEntShadow::RADIUS_RASTER);
    xShadowCameraUpdate(ent->model, modelRenderCB, &center, radius, 0);
    xShadowRender(&center, radius, max_dist);
}

static void xShadow_PickByRayCast(xShadowMgr* mgr)
{
    xEnt* ent_best = NULL;
    S32 idx_best = -1;
    xCollis colrec;
    xRay3 ray;

    memset(&colrec, 0, sizeof(colrec));

    ray.dir = g_NY3;
    ray.min_t = 0.0f;
    ray.max_t = 10.5f;
    ray.flags = 0xc00;

    S32 num = mgr->cache->entCount;
    for (S32 i = 0; i < num; i++)
    {
        xEnt* ep = mgr->cache->ent[i];

        colrec.flags = 0;
        colrec.flags |= 0x1f00;

        ray.origin.x = ep->model->Mat->pos.x;
        ray.origin.y = ep->model->Mat->pos.y;
        ray.origin.z = ep->model->Mat->pos.z;

        iRayHitsModel(&ray, ep->model, &colrec);

        if ((colrec.flags & 1) && (colrec.dist <= 21.7f))
        {
            ent_best = ep;
            idx_best = i;
        }
    }

    if (idx_best > 0)
    {
        mgr->cache->ent[idx_best] = mgr->cache->ent[0];
        mgr->cache->ent[0] = ent_best;
    }
}

static void xShadow_PickEntForNPC(xShadowMgr* mgr)
{
    if (mgr->cache->entCount >= 2)
    {
        if ((mgr->ent->baseType == eBaseTypeNPC) &&
            (((xNPCBasic*)mgr->ent)->flags1.flg_basenpc & 0x8))
        {
            xShadow_PickByRayCast(mgr);
        }
    }
}

void ShadowCameraDestroy(RwCamera* shadowCamera)
{
    if (shadowCamera == NULL)
    {
        return;
    }

    _rwFrameSyncDirty();
    RwFrame* parent = (RwFrame*)shadowCamera->object.object.parent;
    if (parent != NULL)
    {
        _rwObjectHasFrameSetFrame(shadowCamera, NULL);
        RwFrameDestroy(parent);
    }

    // Scheduling issue with RwRasterDestroy calls

    RwRaster* zBuffer = shadowCamera->zBuffer;
    if (zBuffer != NULL)
    {
        shadowCamera->zBuffer = NULL;
        RwRasterDestroy(zBuffer);
    }

    RwRaster* frameBuffer = shadowCamera->frameBuffer;
    if (frameBuffer != NULL)
    {
        shadowCamera->frameBuffer = NULL;
        RwRasterDestroy(frameBuffer);
    }

    RwCameraDestroy(shadowCamera);
}

static void InvertRaster(RwCamera* shadowCamera)
{
    RwIm2DVertex vx[4];
    RwRaster* raster = shadowCamera->frameBuffer;
    F32 w = raster->width;
    F32 h = raster->height;

    RwIm2DVertexSetScreenX(&vx[0], 0.0f);
    RwIm2DVertexSetScreenY(&vx[0], 0.0f);
    RwIm2DVertexSetScreenZ(&vx[0], RwIm2DGetNearScreenZ());
    RwIm2DVertexSetIntRGBA(&vx[0], 255, 255, 255, 255);
    RwIm2DVertexSetU(&vx[0], 0.0f, 1.0f);
    RwIm2DVertexSetV(&vx[0], 0.0f, 1.0f);

    RwIm2DVertexSetScreenX(&vx[1], 0.0f);
    RwIm2DVertexSetScreenY(&vx[1], h);
    RwIm2DVertexSetScreenZ(&vx[1], RwIm2DGetNearScreenZ());
    RwIm2DVertexSetIntRGBA(&vx[1], 255, 255, 255, 255);
    RwIm2DVertexSetU(&vx[1], 0.0f, 1.0f);
    RwIm2DVertexSetV(&vx[1], 1.0f, 1.0f);

    RwIm2DVertexSetScreenX(&vx[2], w);
    RwIm2DVertexSetScreenY(&vx[2], 0.0f);
    RwIm2DVertexSetScreenZ(&vx[2], RwIm2DGetNearScreenZ());
    RwIm2DVertexSetIntRGBA(&vx[2], 255, 255, 255, 255);
    RwIm2DVertexSetU(&vx[2], 1.0f, 1.0f);
    RwIm2DVertexSetV(&vx[2], 0.0f, 1.0f);

    RwIm2DVertexSetScreenX(&vx[3], w);
    RwIm2DVertexSetScreenY(&vx[3], h);
    RwIm2DVertexSetScreenZ(&vx[3], RwIm2DGetNearScreenZ());
    RwIm2DVertexSetIntRGBA(&vx[3], 255, 255, 255, 255);
    RwIm2DVertexSetU(&vx[3], 1.0f, 1.0f);
    RwIm2DVertexSetV(&vx[3], 1.0f, 1.0f);

    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)0);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)0);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)1);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDINVDESTCOLOR);

    RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, vx, 4);

    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)1);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
}

static RwCamera* ShadowCameraUpdate(RwCamera* shadowCamera, void* model, void (*renderCB)(void*),
                                    xVec3* center, F32 radius, S32 shadowMode)
{
    RwRGBA bgColor = { 255, 255, 255, 0 };
    RwCamera* camera = *(RwCamera**)RwEngineInstance;
    S32 fogstate;

    RwRenderStateGet(rwRENDERSTATEFOGENABLE, &fogstate);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)0);

    if (camera != NULL)
    {
        RwCameraEndUpdate(camera);
    }

    GCSaveFrameBuffer();

    shadowCamera->frameBuffer->width--;
    shadowCamera->frameBuffer->height--;
    RwCameraClear(shadowCamera, &bgColor, rwCAMERACLEARIMAGE);
    shadowCamera->frameBuffer->width++;
    shadowCamera->frameBuffer->height++;

    RwFrameOrthoNormalize((RwFrame*)shadowCamera->object.object.parent);

    if (RwCameraBeginUpdate(shadowCamera) != NULL)
    {
        iCameraFrustumPlanes(shadowCamera, globals.camera.frustplane);

        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)0);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)0);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)0);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)1);

        renderCB(model);

        if (shadowMode == 0)
        {
            InvertRaster(shadowCamera);
        }

        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)1);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)1);

        RwCameraEndUpdate(shadowCamera);
        RwGameCubeCameraTextureFlush(shadowCamera->frameBuffer, 0);
    }

    if (camera != NULL)
    {
        RwCameraBeginUpdate(camera);
        iCameraFrustumPlanes(camera, globals.camera.frustplane);
    }

    GCRestoreFrameBuffer();
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)fogstate);

    return shadowCamera;
}

static RwCamera* ShadowCameraSetSpherePersp(RwCamera* camera, RwV3d* center, F32 radius)
{
    RwFrame* camFrame = (RwFrame*)camera->object.object.parent;
    RwMatrixTag* camMatrix = &camFrame->modelling;
    RwV3d* camPos = &camMatrix->pos;

    F32 objDepth = 572.95807f * radius;
    F32 nearZ = objDepth - rscale * radius;
    F32 farZ = objDepth + rscale * radius;

    camera->nearPlane = nearZ;
    camera->farPlane = farZ;
    RwCameraSetNearClipPlane(camera, nearZ);
    RwCameraSetFarClipPlane(camera, farZ);

    *camPos = *center;
    RwV3dIncrementScaledMacro(camPos, &camMatrix->at, -objDepth);
    gCamPos = *camPos;

    RwMatrixUpdate(camMatrix);
    RwFrameUpdateObjects(camFrame);

    gShadowObjectRadius = radius;

    return camera;
}

static S32 CmpShadowMgr(const void* a, const void* b)
{
    xEnt* entA = ((const xShadowMgr*)a)->ent;
    xEnt* entB = ((const xShadowMgr*)b)->ent;

    U8 typeA = entA->baseType;
    S32 isPlayerA = 0;
    if ((typeA == eBaseTypePlayer) || (typeA == eBaseTypeBoulder))
    {
        isPlayerA = 1;
    }

    U8 typeB = entB->baseType;
    S32 isPlayerB = 0;
    if ((typeB == eBaseTypePlayer) || (typeB == eBaseTypeBoulder))
    {
        isPlayerB = 1;
    }

    if (isPlayerA && !isPlayerB)
    {
        return -1;
    }
    if (isPlayerB && !isPlayerA)
    {
        return 1;
    }

    xVec3* campos = &globals.camera.mat.pos;

    F32 dxa = campos->x - entA->model->Mat->pos.x;
    F32 dya = campos->y - entA->model->Mat->pos.y;
    F32 dza = campos->z - entA->model->Mat->pos.z;
    F32 distA = dxa * dxa + dya * dya + dza * dza;

    F32 dxb = campos->x - entB->model->Mat->pos.x;
    F32 dyb = campos->y - entB->model->Mat->pos.y;
    F32 dzb = campos->z - entB->model->Mat->pos.z;
    F32 distB = dxb * dxb + dyb * dyb + dzb * dzb;

    if (distA < distB)
    {
        return -1;
    }
    return distA > distB;
}

static RwRaster* ShadowRasterCreate(S32 res)
{
    return RwRasterCreate(res, res, 0, 5);
}

void GCSaveFrameBuffer()
{
    RwGameCubeCameraTextureFlush(gc_saveraster, 0);
}

static void GCRestoreFrameBuffer()
{
    RwCamera* cam = *(RwCamera**)RwEngineInstance;
    F32 recipCamZ = (1.0f / cam->farPlane);

    RwRenderStateSet(rwRENDERSTATESRCBLEND,      (void*)2);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND,     (void*)1);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE,   (void*)0);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,  (void*)0);
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)1);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, gc_saveraster);

    Im2DRenderQuad(0.0f, 0.0f, 256.0f, 256.0f, RwIm2DGetFarScreenZ(), recipCamZ, 0.001953125f);

    RwRenderStateSet(rwRENDERSTATEZTESTENABLE,  (void*)1);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)1);
    RwRenderStateSet(rwRENDERSTATESRCBLEND,     (void*)5);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND,    (void*)6);
}

static RwCamera* ShadowCameraCreatePersp(S32 param)
{
    RwCamera* cam = RwCameraCreate();
    if (cam != NULL)
    {
        _rwObjectHasFrameSetFrame(cam, RwFrameCreate());

        RwV2d viewWin;
        viewWin.x = 0.001745331f;
        viewWin.y = 0.001745331f;

        RwCameraSetViewWindow(cam, &viewWin);

        if (cam->object.object.parent != NULL)
        {
            RwRaster* raster = RwRasterCreate(param, param, 0, 1);
            if (raster != NULL)
            {
                cam->zBuffer = raster;
                return cam;
            }
        }
    }
    ShadowCameraDestroy(cam);
    return NULL;
}

void xShadowManager_Init(S32 numEnts)
{
    sMgrList = (xShadowMgr*)xMemAlloc(gActiveHeap, numEnts << 4, 0);
    sMgrTotal = numEnts;
    sMgrCount = 0; // Scheduling off
}

void xShadowManager_Reset()
{
    sMgrCount = 0;
}

void xShadowManager_Add(xEnt* ent)
{
    for (int i = 0; i < sMgrCount; i++)
    {
        if (sMgrList[i].ent == ent)
        {
            return;
        }
    }

    if (sMgrCount < sMgrTotal)
    {
        sMgrList[sMgrCount].ent = ent;
        sMgrList[sMgrCount].cache = 0;
        sMgrList[sMgrCount].priority = 1000;
        sMgrList[sMgrCount].cacheReady = 0;
        sMgrCount++;
    }
}

int Im2DRenderQuad(float x1, float y1, float x2, float y2, float z, float recipCamZ, float uvOffset)
{
    RwIm2DVertex v[4];

    RwIm2DVertexSetScreenX(&v[0], x1);
    RwIm2DVertexSetScreenY(&v[0], y1);
    RwIm2DVertexSetScreenZ(&v[0], z);
    RwIm2DVertexSetRecipCameraZ(&v[0], recipCamZ);
    RwIm2DVertexSetIntRGBA(&v[0], 255, 255, 255, 255);
    RwIm2DVertexSetU(&v[0], uvOffset, recipCamZ);
    RwIm2DVertexSetV(&v[0], uvOffset, recipCamZ);

    RwIm2DVertexSetScreenX(&v[1], x1);
    RwIm2DVertexSetScreenY(&v[1], y2);
    RwIm2DVertexSetScreenZ(&v[1], z);
    RwIm2DVertexSetRecipCameraZ(&v[1], recipCamZ);
    RwIm2DVertexSetIntRGBA(&v[1], 255, 255, 255, 255);
    RwIm2DVertexSetU(&v[1], uvOffset, recipCamZ);
    RwIm2DVertexSetV(&v[1], 1.0f + uvOffset, recipCamZ);

    RwIm2DVertexSetScreenX(&v[2], x2);
    RwIm2DVertexSetScreenY(&v[2], y1);
    RwIm2DVertexSetScreenZ(&v[2], z);
    RwIm2DVertexSetRecipCameraZ(&v[2], recipCamZ);
    RwIm2DVertexSetIntRGBA(&v[2], 255, 255, 255, 255);
    RwIm2DVertexSetU(&v[2], 1.0f + uvOffset, recipCamZ);
    RwIm2DVertexSetV(&v[2], uvOffset, recipCamZ);

    RwIm2DVertexSetScreenX(&v[3], x2);
    RwIm2DVertexSetScreenY(&v[3], y2);
    RwIm2DVertexSetScreenZ(&v[3], z);
    RwIm2DVertexSetRecipCameraZ(&v[3], recipCamZ);
    RwIm2DVertexSetIntRGBA(&v[3], 255, 255, 255, 255);
    RwIm2DVertexSetU(&v[3], 1.0f + uvOffset, recipCamZ);
    RwIm2DVertexSetV(&v[3], 1.0f + uvOffset, recipCamZ);

    RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, v, 4);
    return 1;
}

float SQ(float x)
{
    return x * x;
}

void xDrawSphere(const xVec3* center, F32 r, U32 flags)
{
}

void xDrawSetColor(U8 r, U8 g, U8 b, U8 a)
{
}

void xShadowManager_Remove(xEnt* ent)
{
    int a = 0;
    for (int i = 6; i < sMgrCount; i++)
    {
        sMgrList[i].cache = NULL;
        a++;
    }

    a = 0;
    int i = 0;
    while (a < sMgrCount)
    {
        if (ent == sMgrList[i].ent)
        {
            sMgrList[i] = sMgrList[sMgrCount - 1];
            sMgrCount--;
        }
        else
        {
            i++;
            a++;
        }
    }
}
