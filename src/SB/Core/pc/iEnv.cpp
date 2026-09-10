#include "iEnv.h"
#include "iToon.h"

#include "iEnvNormals.h"
#include "iModel.h"

#include "iCamera.h"
#include "iScreen.h"
#include "xMemMgr.h"

static S32 sBeginDrawFX;
static RpWorld* volatile sPipeWorld;
static RwCamera* volatile sPipeCamera;
static iEnv* lastEnv;

static RpAtomic* SetPipelineCB(RpAtomic* atomic, void* data)
{
    if (RwCameraBeginUpdate(sPipeCamera))
    {
        RpAtomicInstance(atomic);
        RwCameraEndUpdate(sPipeCamera);
    }

    if (data)
    {
        RpAtomicSetPipeline(atomic, (RxPipeline*)data);
    }

    return atomic;
}

static void iEnvSetBSP(iEnv* env, S32 envDataType, RpWorld* bsp)
{
    if (envDataType == 0)
    {
        env->world = bsp;
    }
    else if (envDataType == 1)
    {
        env->collision = bsp;
    }
    else if (envDataType == 2)
    {
        env->fx = bsp;
    }
    else if (envDataType == 3)
    {
        env->camera = bsp;
    }
}

void iEnvLoad(iEnv* env, const void* data, U32, S32 dataType)
{
    RpWorld* bsp = (RpWorld*)data;
    xJSPHeader* jsp = (xJSPHeader*)data;

    if (jsp->idtag[0] == 'J' && jsp->idtag[1] == 'S' && jsp->idtag[2] == 'P' &&
        jsp->idtag[3] == '\0')
    {
        if (dataType == 0)
        {
            RwBBox tmpbbox = { 1000.0f, 1000.0f, 1000.0f, -1000.0f, -1000.0f, -1000.0f };

            env->world = RpWorldCreate(&tmpbbox);

            sPipeCamera = iCameraCreate(iScreenWidth(), iScreenHeight(), 0);
            sPipeWorld = env->world;

            RpWorldAddCamera(sPipeWorld, sPipeCamera);

            env->jsp = jsp;

            // All three before the instancing below, and that is the whole
            // constraint: SetPipelineCB builds each atomic's vertex buffer from
            // the flags the geometry has at that moment, so a normal added
            // afterwards is one the buffer has no room for, and a prelight
            // dropped afterwards is one the buffer still carries.
            //
            // Compare before generate, and that order matters too. Both read
            // the same flag to decide whether the level has normals, so
            // generating first would leave the check comparing the generated
            // normals against themselves and reporting a perfect score.
            iEnvNormalsCompare(env);
            iEnvGenerateNormals(env);

            // Only once there is a rig to put in its place. A fit that
            // failed -- a world with no prelight to read, or too little of it
            // -- would otherwise leave the level with no colour and no light.
            if (iScreenWorldLighting() != IWORLDLIGHT_OFF && env->baked.valid)
            {
                if (iScreenWorldLightShadows())
                {
                    // Keeps the prelight and puts the finished lighting in it,
                    // so prelightDropped stays FALSE and zScene leaves the
                    // world alone. See iScreenWorldLightShadows.
                    iEnvBakeShadowedLight(env);
                }
                else
                {
                    iEnvDropPrelight(env);
                    env->prelightDropped = TRUE;
                }
            }

            RpClumpForAllAtomics(env->jsp->clump, SetPipelineCB, NULL);
            xClumpColl_InstancePointers(env->jsp->colltree, env->jsp->clump);

            RpWorldRemoveCamera(sPipeWorld, sPipeCamera);

            iCameraDestroy(sPipeCamera);

            sPipeWorld = NULL;
            sPipeCamera = NULL;
        }
    }
    else
    {
        if (dataType == 0)
        {
            env->jsp = NULL;
        }

        iEnvSetBSP(env, dataType, bsp);
    }

    if (dataType == 0)
    {
        env->memlvl = xMemGetBase();
    }
}

void iEnvFree(iEnv* env)
{
    _rwFrameSyncDirty();

    iEnvFreeNormals(env);

    RpWorldDestroy(env->world);
    env->world = NULL;

    if (env->fx)
    {
        RpWorldDestroy(env->fx);
        env->fx = NULL;
    }

    if (env->collision)
    {
        RpWorldDestroy(env->collision);
        env->collision = NULL;
    }
}

void iEnvDefaultLighting(iEnv*)
{
}

void iEnvLightingBasics(iEnv*, xEnvAsset*)
{
}

// This is named JspPS2_ClumpRender on PS2
static void Jsp_ClumpRender(RpClump* clump, xJSPNodeInfo* nodeInfo)
{
    S32 backcullon = 1;
    S32 zbufferon = 1;
    RwLLLink* cur = rwLinkListGetFirstLLLink(&clump->atomicList);
    RwLLLink* end = rwLinkListGetTerminator(&clump->atomicList);

    while (cur != end)
    {
        RpAtomic* apAtom = rwLLLinkGetData(cur, RpAtomic, inClumpLink);

        if (RpAtomicGetFlags(apAtom) & rpATOMICRENDER)
        {
            RwFrame* frame = RpAtomicGetFrame(apAtom);

            if (!iModelCull(apAtom, &frame->ltm))
            {
                if (backcullon)
                {
                    if (nodeInfo->nodeFlags & 0x4)
                    {
                        backcullon = 0;
                        RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
                    }
                }
                else
                {
                    if (!(nodeInfo->nodeFlags & 0x4))
                    {
                        backcullon = 1;
                        RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
                    }
                }

                if (zbufferon)
                {
                    if (nodeInfo->nodeFlags & 0x2)
                    {
                        zbufferon = 0;
                        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
                    }
                }
                else
                {
                    if (!(nodeInfo->nodeFlags & 0x2))
                    {
                        zbufferon = 1;
                        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
                    }
                }

                RpAtomicRender(apAtom);
            }
        }

        cur = rwLLLinkGetNext(cur);
        nodeInfo++;
    }
}

void iEnvRender(iEnv* env)
{
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

    // The world is painted, not drawn. Its own strip -- gentler shadow, one
    // more step -- because hard bands across a wall read as a mistake where the
    // same bands on a face read as a style. Put back afterwards so that
    // whatever draws next is not shaded like scenery.
    iToonSetRampRow(ITOON_RAMP_WORLD);

    // The shade its own models throw on it, which only the world takes: a house
    // is not shaded by the answer traced for the ground under it.
    iToonSetModelShade(iScreenWorldModelShade());

    // And how bright the level itself is, measured off its paint, which is how a
    // room indoors reads as one. kPaintFullyLit is what an exterior paints: hb01
    // is 0.578 and the inside of SpongeBob's house 0.487.
    {
        const F32 kPaintFullyLit = 0.58f;
        F32 painted = iEnvPaintLevel();
        F32 scale = 1.0f;

        if (painted > 0.0f && painted < kPaintFullyLit)
        {
            scale = powf(painted / kPaintFullyLit, iScreenToonRoomLevel());
        }

        iToonSetRoomLevel(scale);
    }

    // **The level itself, inked.** A hull is geometry, so this draws the whole
    // level a second time; that is the whole of what it costs and why it is a
    // setting.
    //
    // Nothing has named an atomic, so the ink is refused on anything
    // see-through -- the water, the plants on their cards, the goo. The split
    // height is pushed out of reach as well, or the two-tone region the player
    // left behind would fire on a wall.
    S32 worldInk = iScreenToon() && iScreenWorldOutline();

    if (worldInk)
    {
        iToonOutlineAtomic(NULL);
        iToonOutlineOrient(NULL);
        iToonOutlineSplit(0.0f, ITOON_OUTLINE_PLAIN);
        iToonSetOutline(ITOON_OUTLINE_PLAIN);
        iToonOutlineMinWidth(NULL);
        iToonOutlineMaxWidth(NULL);
    }

    if (env->jsp)
    {
        Jsp_ClumpRender(env->jsp->clump, env->jsp->jspNodeList);
    }
    else
    {
        RpWorldRender(env->world);
    }

    if (worldInk)
    {
        iToonSetOutline(ITOON_OUTLINE_NONE);
    }

    iToonSetModelShade(0.0f);
    iToonSetRoomLevel(1.0f);
    iToonSetRampRow(ITOON_RAMP_CHARACTER);

    lastEnv = env;
}

void iEnvEndRenderFX(iEnv*)
{
    iEnv* env = lastEnv;

    if (env->fx && globalCamera && sBeginDrawFX)
    {
        RpWorldRemoveCamera(env->fx, globalCamera);
        RpWorldAddCamera(env->world, globalCamera);

        sBeginDrawFX = 0;
    }
}
