#include "zParPTank.h"

#ifdef __MWERKS__
#include <PowerPC_EABI_Support\MSL_C\MSL_Common\cmath>
#else
#include <cmath>
#endif
#include <rpptank.h>
#include <types.h>

#include "iColor.h"

#include "xClimate.h"
#include "xMath.h"
#include "xMathInlines.h"
#include "xMemMgr.h"
#include "xParEmitter.h"
#include "xPtankPool.h"
#include "xstransvc.h"
#include "xString.h"
#include "xVec3.h"

#include "zGame.h"
#include "zGlobals.h"

// NOTE (Square)
// There's something slightly off in this file. Functions appear to be equivalent but it's hard to
// analyze due to the scheduling differences. Right now, attempting to load or start a new game
// with this file linked will result in a crash, so I'm not marking it as Equivalent yet.

struct BubbleData
{
    xVec3 vel;
    float life;
};

extern RwCamera* sGameScreenTransCam;

const U32 gPTankDisable = 0;
static zParPTank sPTank[7];
static U32 sNumPTanks;
static zParPTank* sSparklePTank;
static zParPTank* sBubblePTank;
static zParPTank* sMenuBubblePTank;
static zParPTank* sSnowPTank;
static zParPTank* sSteamPTank;
static F32 sSparkleAnimTime;
static BubbleData* sBubbleData;
static BubbleData* sMenuBubbleData;

#ifdef PLATFORM_PC
// The loading wipe's own tank, and whether this frame's caller is drawing it
// itself. It is a tank of its own rather than a share of the bubble tank
// because the wipe draws its bubbles OVER the still, and the still covers the
// level: anything in the tank it borrowed came over the still too, so a level
// with ambient bubbles in it showed them through the picture of the level you
// had just left. See zParPTankRenderBubbles.
static zParPTank* sWipePTank;
static BubbleData* sWipeBubbleData;
static S32 sDeferBubbles;
#endif

namespace
{
    // total size: 0x30
    struct snow_particle_data
    {
        xVec3 loc;
        F32 size;
        xVec3 vel;
        F32 life;
        F32 u;
        F32 pad[3];
    };

    static ptank_pool__pos_color_size_uv2 snow_pool;
    static snow_particle_data* snow_particles;
} // namespace

static float sSteamAnimTime;

const RwV2d sparkle_size = { 0.3f, 0.3f };

static void zParPTankSparkleCreate(zParPTank* zp, U32 max_particles, zParPTankUpdateCallback update)
{
    zp->num_particles = 0;
    zp->max_particles = max_particles;
    zp->flags = 0;
    zp->update = update;

    RwTexture* tex = (RwTexture*)xSTFindAsset(xStrHash("partex0"), NULL);
    if (!tex)
    {
        return;
    }

    RwTextureSetFilterMode(tex, rwFILTERLINEAR);
    RwRGBA defaultColor = { 0xFF, 0xFF, 0xFF, 0xFF };

    zp->ptank = RpPTankAtomicCreate(
        zp->max_particles, rpPTANKDFLAGPOSITION | rpPTANKDFLAGVTX2TEXCOORDS | rpPTANKDFLAGARRAY, 0);

    RwFrame* frame = RwFrameCreate();
    RwMatrixSetIdentity(&frame->modelling);
    RpAtomicSetFrame(zp->ptank, frame);

    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.cColor = defaultColor;

    if (zp->ptank->geometry->matList.materials[0])
    {
        zp->ptank->geometry->matList.materials[0]->color =
            RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.cColor;
    }
    RPATOMICPTANKPLUGINDATA(zp->ptank)->instFlags |= rpPTANKIFLAGCNSCOLOR;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.cSize = sparkle_size;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->instFlags |= rpPTANKIFLAGCNSSIZE;

    RpMaterialSetTexture(zp->ptank->geometry->matList.materials[0], tex);

    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.srcBlend = 5;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.dstBlend = 2;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->instFlags |= rpPTANKIFLAGALPHABLENDING;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.vertexAlphaBlend = 1;

    sSparkleAnimTime = 0.0f;
}

// Equivalent: float loading optimization
static void zParPTankSparkleUpdate(zParPTank* zp, float dt)
{
    sSparkleAnimTime += dt;
    if (!(sSparkleAnimTime >= 1.0f / 30.0f))
    {
        return;
    }

    RpPTankLockStruct plock;
    RpPTankLockStruct uvlock;
    RpPTankAtomicLock(zp->ptank, &plock, rpPTANKDFLAGPOSITION, rpPTANKLOCKWRITE);
    RpPTankAtomicLock(zp->ptank, &uvlock, rpPTANKDFLAGVTX2TEXCOORDS, rpPTANKLOCKWRITE);

    U32 plock_base = (U32)plock.data;
    U32 uvlock_base = (U32)uvlock.data;

    // RwTexCoords* uv = (RwTexCoords*)uvlock.data;
    for (S32 i = 0; i < zp->num_particles; i++)
    {
        RwTexCoords* uv = (RwTexCoords*)uvlock.data;
        uv[0].u += 0.125f;
        uv[1].u += 0.125f;

        if (uv[0].u >= 1.0f)
        {
            *(RwV3d*)(plock_base + i * plock.stride) =
                *(RwV3d*)(plock_base + (zp->num_particles - 1) * plock.stride);

            RwTexCoords* end_uv =
                (RwTexCoords*)(uvlock_base + (zp->num_particles - 1) * uvlock.stride);
            uv[0] = end_uv[0];
            uv[1] = end_uv[1];

            i--;
            zp->num_particles--;
            uvlock.data -= uvlock.stride;
        }
        uvlock.data += uvlock.stride;
    }

    RpPTankAtomicUnlock(zp->ptank);

    RPATOMICPTANKPLUGINDATA(zp->ptank)->instFlags |= rpPTANKIFLAGACTNUMCHG;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->actPCount = zp->num_particles;
    sSparkleAnimTime -= 1.0f / 30.f;
}

// Equivalent: regswaps
void zParPTankSpawnSparkles(xVec3* pos, U32 count)
{
    if (zGameIsPaused())
    {
        return;
    }
    zParPTank* zp = sSparklePTank;

    if (count > zp->max_particles - zp->num_particles)
    {
        count = zp->max_particles - zp->num_particles;
    }

    if (count == 0)
    {
        return;
    }

    RpPTankLockStruct posLock;
    RpPTankLockStruct vtx2TexCoordsLock;

    RpPTankAtomicLock(zp->ptank, &posLock, rpPTANKLFLAGPOSITION, rpPTANKLOCKWRITE);
    if (posLock.data == NULL)
    {
        return;
    }

    RpPTankAtomicLock(zp->ptank, &vtx2TexCoordsLock, rpPTANKDFLAGVTX2TEXCOORDS, rpPTANKLOCKWRITE);
    if (vtx2TexCoordsLock.data == NULL)
    {
        return;
    }

    U32 poslock_base = (U32)posLock.data;
    U32 uvlock_base = (U32)vtx2TexCoordsLock.data;
    xVec3* ref_pos = pos;
    RwCamera* camera = RwCameraGetCurrentCamera();
    if (gGameState == eGameState_Play && camera)
    {
        ref_pos = (xVec3*)&RwFrameGetMatrix(RwCameraGetFrame(camera))->pos;
    }

    xVec3* posit = pos;
    for (U32 i = 0; i < count; posit++, i++)
    {
        if (!sGameScreenTransCam && ref_pos && xVec3Dist2(posit, ref_pos) > 900.0f)
        {
            continue;
        }

        *(xVec3*)(poslock_base + zp->num_particles * posLock.stride) = *posit;
        RwTexCoords* uv =
            (RwTexCoords*)(uvlock_base + zp->num_particles * vtx2TexCoordsLock.stride);
        uv[0].u = 0.0f;
        uv[0].v = 0.5f;
        uv[1].u = 0.125f + uv[0].u;
        uv[1].v = 0.125f + uv[0].v;
        zp->num_particles += 1;
    }

    RpPTankAtomicUnlock(zp->ptank);
    RPATOMICPTANKPLUGINDATA(zp->ptank)->instFlags |= rpPTANKIFLAGACTNUMCHG;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->actPCount = zp->num_particles;
}

const RwRGBA bubble_color = { 0x80, 0x80, 0x80, 0xFF };

// Equivalent, float scheduling
static void zParPTankBubbleCreate(zParPTank* zp, U32 max_particles, zParPTankUpdateCallback update)
{
    zp->num_particles = 0;
    zp->max_particles = max_particles;
    zp->flags = 0;
    zp->update = update;

    RwTexture* tex = (RwTexture*)xSTFindAsset(xStrHash("partex0"), NULL);
    if (!tex)
    {
        return;
    }

    RwTextureSetFilterMode(tex, rwFILTERLINEAR);

    zp->ptank = RpPTankAtomicCreate(zp->max_particles,
                                    rpPTANKDFLAGPOSITION | rpPTANKDFLAGCOLOR | rpPTANKDFLAGSIZE |
                                        rpPTANKDFLAGVTX2TEXCOORDS | rpPTANKDFLAGARRAY,
                                    0);

    RwFrame* frame = RwFrameCreate();
    RwMatrixSetIdentity(&frame->modelling);
    RpAtomicSetFrame(zp->ptank, frame);

    RpMaterialSetTexture(zp->ptank->geometry->matList.materials[0], tex);

    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.srcBlend = 5;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.dstBlend = 2;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->instFlags |= rpPTANKIFLAGALPHABLENDING;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.vertexAlphaBlend = 1;

    sBubbleData = (BubbleData*)xMemPushTemp(zp->max_particles * sizeof(BubbleData));
}

#ifdef PLATFORM_PC
// zParPTankBubbleCreate with its own particle data. Everything else about the
// tank is the same -- same texture, same additive blend, same update -- because
// they are the same bubbles; only who draws them, and when, differs.
static void zParPTankWipeBubbleCreate(zParPTank* zp, U32 max_particles,
                                      zParPTankUpdateCallback update)
{
    zp->num_particles = 0;
    zp->max_particles = max_particles;
    zp->flags = 0;
    zp->update = update;

    RwTexture* tex = (RwTexture*)xSTFindAsset(xStrHash("partex0"), NULL);
    if (!tex)
    {
        return;
    }

    RwTextureSetFilterMode(tex, rwFILTERLINEAR);

    zp->ptank = RpPTankAtomicCreate(zp->max_particles,
                                    rpPTANKDFLAGPOSITION | rpPTANKDFLAGCOLOR | rpPTANKDFLAGSIZE |
                                        rpPTANKDFLAGVTX2TEXCOORDS | rpPTANKDFLAGARRAY,
                                    0);

    RwFrame* frame = RwFrameCreate();
    RwMatrixSetIdentity(&frame->modelling);
    RpAtomicSetFrame(zp->ptank, frame);

    RpMaterialSetTexture(zp->ptank->geometry->matList.materials[0], tex);

    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.srcBlend = 5;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.dstBlend = 2;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->instFlags |= rpPTANKIFLAGALPHABLENDING;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.vertexAlphaBlend = 1;

    sWipeBubbleData = (BubbleData*)xMemPushTemp(zp->max_particles * sizeof(BubbleData));
}
#endif

static void zParPTankMenuBubbleCreate(zParPTank* zp, U32 max_particles,
                                      zParPTankUpdateCallback update)
{
    zp->num_particles = 0;
    zp->max_particles = max_particles;
    zp->flags = 0;
    zp->update = update;

    RwTexture* tex = (RwTexture*)xSTFindAsset(xStrHash("partex0"), NULL);
    if (!tex)
    {
        return;
    }

    RwTextureSetFilterMode(tex, rwFILTERLINEAR);

    zp->ptank = RpPTankAtomicCreate(zp->max_particles,
                                    rpPTANKDFLAGPOSITION | rpPTANKDFLAGCOLOR | rpPTANKDFLAGSIZE |
                                        rpPTANKDFLAGVTX2TEXCOORDS | rpPTANKDFLAGARRAY,
                                    0);

    RwFrame* frame = RwFrameCreate();
    RwMatrixSetIdentity(&frame->modelling);
    RpAtomicSetFrame(zp->ptank, frame);

    RpMaterialSetTexture(zp->ptank->geometry->matList.materials[0], tex);

    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.srcBlend = 5;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.dstBlend = 2;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->instFlags |= rpPTANKIFLAGALPHABLENDING;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.vertexAlphaBlend = 1;

    sMenuBubbleData = (BubbleData*)xMemPushTemp(zp->max_particles * sizeof(BubbleData));
}

// Equivalent
static void zParPTankBubbleUpdate(zParPTank* zp, float dt)
{
    RpPTankLockStruct plock;
    RpPTankLockStruct clock;
    RpPTankLockStruct slock;
    RpPTankLockStruct uvlock;
    RpPTankAtomicLock(zp->ptank, &plock, rpPTANKDFLAGPOSITION, rpPTANKLOCKWRITE);
    RpPTankAtomicLock(zp->ptank, &clock, rpPTANKDFLAGCOLOR, rpPTANKLOCKWRITE);
    RpPTankAtomicLock(zp->ptank, &slock, rpPTANKDFLAGSIZE, rpPTANKLOCKWRITE);
    RpPTankAtomicLock(zp->ptank, &uvlock, rpPTANKDFLAGVTX2TEXCOORDS, rpPTANKLOCKWRITE);

    U32 plock_base = (U32)plock.data;
    U32 clock_base = (U32)clock.data;
    U32 slock_base = (U32)slock.data;
    U32 uvlock_base = (U32)uvlock.data;

    F32 damp = xpow(0.95f, 60.0f * dt);

#ifdef PLATFORM_PC
    // A bubble between 1.2 and 0.5 seconds of life left has a 4% chance of
    // popping every frame, which is a rate per frame. Over the 0.7 seconds the
    // window is open, four times the frames pop four times as often and almost
    // nothing survives to fade out. This is the chance of surviving a frame of
    // dt seconds at the same number of pops a second.
    F32 pop_keep = 1.0f - xFrameEmitChance(0.04f, dt);
#endif

#ifdef PLATFORM_PC
    BubbleData* base_xp = zp == sBubblePTank   ? sBubbleData
                          : zp == sWipePTank ? sWipeBubbleData
                                             : sMenuBubbleData;
#else
    BubbleData* base_xp = zp == sBubblePTank ? sBubbleData : sMenuBubbleData;
#endif
    BubbleData* xp = base_xp;

    for (S32 i = 0; i < zp->num_particles; i++)
    {
        xVec3* pos = (xVec3*)plock.data;
        RwTexCoords* uv = (RwTexCoords*)uvlock.data;

        xp->life -= dt;

        pos->x += xp->vel.x * dt;
        pos->y += xp->vel.y * dt;
        pos->z += xp->vel.z * dt;

        xp->vel.x *= damp;
        xp->vel.y += 3.0f * dt;
        xp->vel.y *= damp;
        xp->vel.z *= damp;

        if (xp->life > 0.21875f)
        {
            uv[0].u = -(0.125f * (U32)((xp->life * 8.0f) / 1.75f) - 1.0f);
            uv[1].u = 0.125f + uv[0].u;
        }

        RwRGBA* color = (RwRGBA*)clock.data;

        F32 life = xp->life > 0.0f ? xp->life : 0.0f;
        if (life > 1.5749999f)
        {
            color->alpha = 255.0f * ((1.75f - life) / 0.175f);
        }
        else if (life < 0.7f)
        {
            color->alpha = 255.0f * (life / 0.7f);
        }
        else
        {
            color->alpha = 0xFF;
        }

#ifdef PLATFORM_PC
        if ((xp->life < 1.2f && xp->life > 0.5f && xurand() > pop_keep) != 0 || xp->life < 0.0f)
#else
        if ((xp->life < 1.2f && xp->life > 0.5f && xurand() > 0.96f) != 0 || xp->life < 0.0f)
#endif
        {
            *pos = *(xVec3*)(plock_base + (zp->num_particles - 1) * plock.stride);
            *color = *(RwRGBA*)(clock_base + (zp->num_particles - 1) * clock.stride);
            *(RwV2d*)slock.data = *(RwV2d*)(slock_base + (zp->num_particles - 1) * slock.stride);

            RwTexCoords* end_uv =
                (RwTexCoords*)(uvlock_base + (zp->num_particles - 1) * uvlock.stride);
            uv[0] = end_uv[0];
            uv[1] = end_uv[1];

            xp->vel = base_xp[zp->num_particles - 1].vel;
            xp->life = base_xp[zp->num_particles - 1].life;

            i--;
            xp--;
            zp->num_particles--;
            plock.data -= plock.stride;
            clock.data -= clock.stride;
            slock.data -= slock.stride;
            uvlock.data -= uvlock.stride;
        }

        xp++;
        plock.data += plock.stride;
        clock.data += clock.stride;
        slock.data += slock.stride;
        uvlock.data += uvlock.stride;
    }

    RpPTankAtomicUnlock(zp->ptank);
    RPATOMICPTANKPLUGINDATA(zp->ptank)->instFlags |= rpPTANKIFLAGACTNUMCHG;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->actPCount = zp->num_particles;
}

// regswaps
#ifdef PLATFORM_PC
// Whether a wall may be spawned with no player in the world. See the guard
// below.
static S32 sSpawnWithoutPlayer;

void zParPTankSpawnWithoutPlayer(S32 allow)
{
    sSpawnWithoutPlayer = allow;
}
#endif

static void zParPTankSpawnBubbles(xVec3* pos, xVec3* vel, U32 count, float scale, zParPTank* zp)
{
#ifdef PLATFORM_PC
    // Every bubble in the game is spawned off something the player did, so the
    // guard below stands in for "the game is live" -- except for the one caller
    // that is not: zGameScreenTransitionUpdate spawns a wall every frame of the
    // loading screen, and there is no player during a load. zGameExit tore it
    // down and zGameSetupPlayer does not run until zSceneInit has returned.
    //
    // So the loading screen's wall is dropped. Not always: globals.player.ent
    // .model is left dangling rather than cleared, so for the two or three
    // frames before the loader writes over that memory it still reads as a
    // model and about a hundred bubbles get through. That is the whole of the
    // wall on the console too, and it is why the screen looks nearly empty.
    //
    // zGame turns this on for the length of the loading screen. Nothing in
    // here touches the player -- the guard is the only mention of it -- so
    // there is nothing else to stand in for.
    if (!sSpawnWithoutPlayer)
#endif
    if (globals.player.ent.model == 0 || globals.player.ent.model->Mat == 0)
    {
        return;
    }

    if (count > zp->max_particles - zp->num_particles)
    {
        count = zp->max_particles - zp->num_particles;
    }

    if (count == 0)
    {
        return;
    }

#ifdef PLATFORM_PC
    BubbleData* base_xp = zp == sBubblePTank   ? sBubbleData
                          : zp == sWipePTank ? sWipeBubbleData
                                             : sMenuBubbleData;
#else
    BubbleData* base_xp = zp == sBubblePTank ? sBubbleData : sMenuBubbleData;
#endif

    RpPTankLockStruct plock;
    RpPTankLockStruct clock;
    RpPTankLockStruct slock;
    RpPTankLockStruct uvlock;
    RpPTankAtomicLock(zp->ptank, &plock, rpPTANKDFLAGPOSITION, rpPTANKLOCKWRITE);
    if (!plock.data)
    {
        return;
    }

    RpPTankAtomicLock(zp->ptank, &clock, rpPTANKDFLAGCOLOR, rpPTANKLOCKWRITE);
    if (!clock.data)
    {
        return;
    }

    RpPTankAtomicLock(zp->ptank, &slock, rpPTANKDFLAGSIZE, rpPTANKLOCKWRITE);
    if (!slock.data)
    {
        return;
    }

    RpPTankAtomicLock(zp->ptank, &uvlock, rpPTANKDFLAGVTX2TEXCOORDS, rpPTANKLOCKWRITE);
    if (!uvlock.data)
    {
        return;
    }

    U32 plock_base = (U32)plock.data;
    U32 clock_base = (U32)clock.data;
    U32 slock_base = (U32)slock.data;
    U32 uvlock_base = (U32)uvlock.data;
    xVec3* ref_pos = pos;
    RwCamera* camera = RwCameraGetCurrentCamera();
    if (gGameState == eGameState_Play && camera)
    {
        ref_pos = (xVec3*)&RwFrameGetMatrix(RwCameraGetFrame(camera))->pos;
    }

    xVec3* posit = pos;
    xVec3* velit = vel;
    U32 i = 0;
    for (; i < count; posit++, velit++, i++)
    {
        if (!sGameScreenTransCam && ref_pos && xVec3Dist2(posit, ref_pos) > 5625.0f)
        {
            continue;
        }

        *(xVec3*)(plock_base + zp->num_particles * plock.stride) = *posit;
        base_xp[zp->num_particles].vel = *velit;
        base_xp[zp->num_particles].life = 1.75f;

        RwTexCoords* uv = (RwTexCoords*)(uvlock_base + zp->num_particles * uvlock.stride);
        uv[0].u = 0.0f;
        uv[0].v = 0.625f;
        uv[1].u = 0.125f + uv[0].u;
        uv[1].v = 0.75f;

        RwV2d* size = (RwV2d*)(slock_base + zp->num_particles * slock.stride);

        size->x = size->y = scale * (xurand() * 0.15f + 0.1f);

        *(RwRGBA*)(clock_base + zp->num_particles * clock.stride) = bubble_color;
        zp->num_particles++;
    }

    RpPTankAtomicUnlock(zp->ptank);
    RPATOMICPTANKPLUGINDATA(zp->ptank)->instFlags |= rpPTANKIFLAGACTNUMCHG;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->actPCount = zp->num_particles;
}

void zParPTankSpawnBubbles(xVec3* pos, xVec3* vel, U32 count, float scale)
{
    if (zGameIsPaused())
    {
        return;
    }

    zParPTankSpawnBubbles(pos, vel, count, scale, sBubblePTank);
}

S32 zParPTankBubblesAvailable()
{
    return sBubblePTank->max_particles - sBubblePTank->num_particles;
}

#ifdef PLATFORM_PC
void zParPTankSpawnWipeBubbles(xVec3* pos, xVec3* vel, U32 count, F32 scale)
{
    zParPTankSpawnBubbles(pos, vel, count, scale, sWipePTank);
}
#endif

void zParPTankSpawnMenuBubbles(xVec3* pos, xVec3* vel, U32 count)
{
    zParPTankSpawnBubbles(pos, vel, count, 1.0f, sMenuBubblePTank);
}

// Equivalent: Scheduling
static void zParPTankSnowCreate(zParPTank* zp, U32 max_particles, zParPTankUpdateCallback update)
{
    zp->flags = 0;
    zp->update = update;
    zp->ptank = NULL;
    zp->num_particles = 0;
    zp->max_particles = max_particles;

    snow_pool.rs.texture = (RwTexture*)xSTFindAsset(xStrHash("partex0"), NULL);
    snow_pool.rs.src_blend = 5;
    snow_pool.rs.dst_blend = 2;
    snow_pool.rs.flags = 0;
    snow_particles =
        (snow_particle_data*)xMemAllocSize(zp->max_particles * sizeof(snow_particle_data));
}

// Equivalent: float scheduling
static void zParPTankSnowUpdate(zParPTank* zp, float dt)
{
    snow_particle_data* end = snow_particles + zp->num_particles;
    F32 fadein_life = 0.9f * snow_life;
    F32 fadeout_life = 0.4f * snow_life;
    F32 ilife = snow_life;
    F32 ifadein = 2550.0f * (1.0f / ilife);
    F32 ifadeout = 637.5f * (1.0f / ilife);
    snow_pool.reset();
    snow_particle_data* it = snow_particles;
    while (it != end)
    {
        it->life -= dt;
        if (it->life <= 0.0f)
        {
            end--;
            *it = *end;
            continue;
        }

        it->loc += it->vel * dt;
        xVec4* _loc = (xVec4*)&it->loc;
        F32 par_dist;
        snow_pool.next();
        if (!snow_pool.valid())
        {
            end = it;
            break;
        }

        *(U32*)snow_pool.color = 0xFFFFFFFF;

        if (it->life > fadein_life)
        {
            snow_pool.color->a = ifadein * (snow_life - it->life) + 0.5f;
        }
        else if (it->life < fadeout_life)
        {
            snow_pool.color->a = it->life * ifadeout + 0.5f;
        }
        else
        {
            snow_pool.color->a = 0xFF;
        }

        *snow_pool.pos = it->loc;
        snow_pool.size->assign(it->size);
        snow_pool.uv[0].assign(it->u, 0.875f);
        snow_pool.uv[1].assign(0.125f + snow_pool.uv[0].x, 0.125f + snow_pool.uv[0].y);

        it++;
    }

    snow_pool.flush();
    zp->num_particles = end - snow_particles;
}

void zParPTankSpawnSnow(xVec3* pos, xVec3* vel, U32 count)
{
    if (zGameIsPaused())
    {
        return;
    }

    zParPTank* zp = sSnowPTank;
    U32 old_size = zp->num_particles;
    zp->num_particles = old_size + count;

    if (zp->num_particles > zp->max_particles)
    {
        zp->num_particles = zp->max_particles;
    }

    snow_particle_data* it = snow_particles + old_size;
    snow_particle_data* end = it + (zp->num_particles - old_size);

    for (; it != end; it++)
    {
        it->loc = *pos;
        it->vel = *vel;
        it->life = snow_life;

        it->size = 0.15f * xurand() + 0.15f;
        it->u = 0.125f * ((xrand() >> 13) & 0x7);

        pos++;
        vel++;
    }
}

const RwV2d steam_size = { 0.4f, 0.4f };

static void zParPTankSteamCreate(zParPTank* zp, U32 max_particles, zParPTankUpdateCallback update)
{
    zp->num_particles = 0;
    zp->max_particles = max_particles;
    zp->flags = 0;
    zp->update = update;

    RwTexture* tex = (RwTexture*)xSTFindAsset(xStrHash("particle_cloud"), NULL);
    if (!tex)
    {
        return;
    }

    RwTextureSetFilterMode(tex, rwFILTERLINEAR);
    RwRGBA defaultColor = { 0xFF, 0xFF, 0xFF, 0xFF };

    zp->ptank = RpPTankAtomicCreate(
        zp->max_particles, rpPTANKDFLAGPOSITION | rpPTANKDFLAGVTX2TEXCOORDS | rpPTANKDFLAGARRAY, 0);

    RwFrame* frame = RwFrameCreate();
    RwMatrixSetIdentity(&frame->modelling);
    RpAtomicSetFrame(zp->ptank, frame);

    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.cColor = defaultColor;

    if (zp->ptank->geometry->matList.materials[0])
    {
        zp->ptank->geometry->matList.materials[0]->color =
            RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.cColor;
    }
    RPATOMICPTANKPLUGINDATA(zp->ptank)->instFlags |= rpPTANKIFLAGCNSCOLOR;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.cSize = steam_size;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->instFlags |= rpPTANKIFLAGCNSSIZE;

    RpMaterialSetTexture(zp->ptank->geometry->matList.materials[0], tex);

    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.srcBlend = 5;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.dstBlend = 2;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->instFlags |= rpPTANKIFLAGALPHABLENDING;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.vertexAlphaBlend = 1;

    sSteamAnimTime = 0.0f;
}

// Equivalent: float load optimization
static void zParPTankSteamUpdate(zParPTank* zp, float dt)
{
    RPATOMICPTANKPLUGINDATA(zp->ptank)->publicData.cSize = steam_size;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->instFlags |= rpPTANKIFLAGCNSSIZE;

#ifdef PLATFORM_PC
    // Unlike the sparkle clock this one is never taken back, so past the first
    // thirtieth of a second the gate is always open and the UV walk below steps
    // every frame -- 60 Hz on console. Carry a sixtieth so it keeps stepping at
    // that rate: each step advances u by an eighth and the particle dies at 1.0,
    // so a steam puff otherwise lasts eight host frames instead of eight
    // sixtieths of a second.
    sSteamAnimTime += dt;

    if (sSteamAnimTime < 1.0f / 60.0f)
    {
        return;
    }

    sSteamAnimTime -= 1.0f / 60.0f;

    if (sSteamAnimTime > 1.0f / 60.0f)
    {
        sSteamAnimTime = 1.0f / 60.0f;
    }
#else
    sSteamAnimTime += dt;

    if (!(sSteamAnimTime >= 1.0f / 30.0f))
    {
        return;
    }
#endif

    RpPTankLockStruct plock;
    RpPTankLockStruct uvlock;

    RpPTankAtomicLock(zp->ptank, &plock, rpPTANKDFLAGPOSITION, rpPTANKLOCKWRITE);
    RpPTankAtomicLock(zp->ptank, &uvlock, rpPTANKDFLAGVTX2TEXCOORDS, rpPTANKLOCKWRITE);

    U32 plock_base = (U32)plock.data;
    U32 uvlock_base = (U32)uvlock.data;

    for (U32 i = 0; i < zp->num_particles; i++)
    {
        RwTexCoords* uv = (RwTexCoords*)uvlock.data;
        uv[0].u += 0.125f;
        uv[1].u += 0.125f;

        if (uv[0].u >= 1.0f)
        {
            *(RwV3d*)(plock_base + i * plock.stride) =
                *(RwV3d*)(plock_base + (zp->num_particles - 1) * plock.stride);

            RwTexCoords* end_uv =
                (RwTexCoords*)(uvlock_base + (zp->num_particles - 1) * uvlock.stride);
            uv[0] = end_uv[0];
            uv[1] = end_uv[1];

            i--;
            zp->num_particles--;
            uvlock.data -= uvlock.stride;
        }

        // plock.data += plock.stride;
        uvlock.data += uvlock.stride;
    }

    RpPTankAtomicUnlock(zp->ptank);
    RPATOMICPTANKPLUGINDATA(zp->ptank)->instFlags |= rpPTANKIFLAGACTNUMCHG;
    RPATOMICPTANKPLUGINDATA(zp->ptank)->actPCount = zp->num_particles;
}

zParPTank* zParPTankAdd()
{
    return &sPTank[sNumPTanks++];
}

// Equivalent: Scheduling
void zParPTankInit()
{
    sNumPTanks = 0;
    sSparklePTank = zParPTankAdd();
    zParPTankSparkleCreate(sSparklePTank, 0x80, zParPTankSparkleUpdate);

    sMenuBubblePTank = zParPTankAdd();
    zParPTankMenuBubbleCreate(sMenuBubblePTank, 0x10, zParPTankBubbleUpdate);
    sMenuBubblePTank->flags |= 0x2;

    sBubblePTank = zParPTankAdd();
    zParPTankBubbleCreate(sBubblePTank, 0x300, zParPTankBubbleUpdate);

#ifdef PLATFORM_PC
    // The loading wipe's. Same particles, its own tank; see sWipePTank.
    sWipePTank = zParPTankAdd();
    zParPTankWipeBubbleCreate(sWipePTank, 0x400, zParPTankBubbleUpdate);
#endif

    sSteamPTank = zParPTankAdd();
    zParPTankSteamCreate(sSteamPTank, 0x80, zParPTankSteamUpdate);
}

// Equivalent: Scheduling
void zParPTankSceneEnter()
{
    sSnowPTank = zParPTankAdd();
    zParPTankSnowCreate(sSnowPTank, 0x400, zParPTankSnowUpdate);
}

void zParPTankSceneExit()
{
}

//Equivalent: Scheduling
void zParPTankExit()
{
    zParPTank* zp = sPTank;
    for (S32 i = 0; i < sNumPTanks; i++, zp++)
    {
        if (zp->ptank == NULL)
        {
            continue;
        }

        RwFrame* tmpframe = *(RwFrame**)&zp->ptank->object.object.parent;
        RpAtomicSetFrame(zp->ptank, NULL);
        RwFrameDestroy(tmpframe);
        RpPTankAtomicDestroy(zp->ptank);
    }

    if (sBubbleData)
    {
        xMemPopTemp(sBubbleData);
    }
    sBubbleData = NULL;

    if (sMenuBubbleData)
    {
        xMemPopTemp(sMenuBubbleData);
    }
    sMenuBubbleData = NULL;

#ifdef PLATFORM_PC
    if (sWipeBubbleData)
    {
        xMemPopTemp(sWipeBubbleData);
    }
    sWipeBubbleData = NULL;
#endif
}

void zParPTankUpdate(float dt)
{
    S32 paused = zGameIsPaused();

    zParPTank* zp = sPTank;
    for (S32 i = 0; i < sNumPTanks; i++, zp++)
    {
        if ((!paused || zp->flags & 0x2) && zp->update)
        {
            zp->update(zp, dt);
        }
    }
}

#ifdef PLATFORM_PC
// video.load_transition = fancy. The wipe draws the still over the whole
// rendered frame and then wants the bubbles ON TOP of it, the way the loading
// screen draws them over its background -- otherwise the only bubbles anyone
// can see are the ones the wipe has already uncovered, and the wipe line looks
// glued to the top of them.
//
// So while the wipe is up the bubble tank comes out of the pass below and
// zGameLoop renders it after the still instead. Rendering it in both places
// would draw every bubble under the line twice and put a brightness step along
// the line, which is the thing being fixed.

// Where the camera was when the bubbles were last carried, and whether that
// is a camera from THIS wipe. See zParPTankBubblesFollowCamera.
static xMat4x3 sFollowCam;
static S32 sFollowValid;

void zParPTankDeferBubbles(S32 defer)
{
    // Going up is the start of a wipe, which is the one moment the stored
    // camera belongs to a different one. Keyed off this rather than off a flag
    // of its own because both mean the same thing: the wipe is on screen.
    if (defer && !sDeferBubbles)
    {
        sFollowValid = 0;
    }

    sDeferBubbles = defer;
}

// Carry the wall along with the camera.
//
// The bubbles are world particles, spawned a fixed distance in front of
// wherever the camera was looking. That is right while the camera is the
// player's, and wrong the moment it is not: a level that opens on a cutscene
// flies the camera away and leaves the wall behind in empty world space, so the
// wipe reveals a level with no bubbles in it.
//
// Each particle is taken into the previous frame's camera space and put back
// out through this frame's, which pins the wall to the camera through both
// movement and rotation. Velocities go with them, so a bubble keeps rising
// relative to the view rather than swinging as the camera turns.
void zParPTankBubblesFollowCamera()
{
    zParPTank* zp = sWipePTank;

    if (zp == NULL || zp->ptank == NULL)
    {
        return;
    }

    const xMat4x3* cam = &globals.camera.mat;

    // The first frame of a wipe has nowhere to carry the bubbles from.
    if (!sFollowValid)
    {
        xMat4x3Copy(&sFollowCam, cam);
        sFollowValid = 1;
        return;
    }

    if (zp->num_particles != 0)
    {
        RpPTankLockStruct plock;
        RpPTankAtomicLock(zp->ptank, &plock, rpPTANKDFLAGPOSITION, rpPTANKLOCKWRITE);

        if (plock.data != NULL)
        {
            BubbleData* xp = sWipeBubbleData;

            for (S32 i = 0; i < zp->num_particles; i++, xp++)
            {
                xVec3* pos = (xVec3*)plock.data;
                xVec3 local;

                xMat4x3Tolocal(&local, &sFollowCam, pos);
                xMat4x3Toworld(pos, cam, &local);

                xMat3x3Tolocal(&local, &sFollowCam, &xp->vel);
                xMat3x3RMulVec(&xp->vel, cam, &local);

                plock.data += plock.stride;
            }

            RpPTankAtomicUnlock(zp->ptank);
        }
    }

    xMat4x3Copy(&sFollowCam, cam);
}

static void zParPTankRenderOne(zParPTank* zp)
{
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)1);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)1);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, 0);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)5);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)2);

    if (zp->num_particles)
    {
        zp->ptank->renderCallBack(zp->ptank);
    }
}

void zParPTankRenderBubbles()
{
    if (sWipePTank != NULL && sWipePTank->ptank != NULL)
    {
        zParPTankRenderOne(sWipePTank);
    }
}
#endif

void zParPTankRender()
{
    zParPTank* zp = sPTank;
    for (S32 i = 0; i < sNumPTanks; i++, zp++)
    {
#ifdef PLATFORM_PC
        if (sDeferBubbles && zp == sWipePTank)
        {
            continue;
        }
#endif

        if ((!zGameIsPaused() || zp == sMenuBubblePTank) && zp->ptank)
        {
            RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)1);
            RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)1);
            RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, 0);
            RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)5);
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)2);

            if (zp->num_particles)
            {
                zp->ptank->renderCallBack(zp->ptank);
            }
        }
    }
}

S32 zParPTankConvertEmitRate(xParEmitter* pe, float dt)
{
    xParEmitterPropsAsset* prop = pe->prop;
    pe->rate_time += dt;

    S32 rate_has_elapsed = (pe->rate_time > prop->rate.freq);
    if (prop->rate.freq == 0.0f)
    {
        pe->rate_time = 0.0f;
    }

    while (pe->rate_time > prop->rate.freq)
    {
        pe->rate_time -= prop->rate.freq;
    }

    F32 rate =
        xParInterpCompute(pe->rate_mode, &prop->rate, pe->rate_time, rate_has_elapsed, pe->rate);

    pe->rate = rate;
    pe->rate_fraction += rate * dt;
    pe->rate_fraction_cull += rate * dt;

    S32 count = std::floorf(pe->rate_fraction);
    if (count > 0)
    {
        pe->rate_fraction -= count;
    }

    return count;
}
