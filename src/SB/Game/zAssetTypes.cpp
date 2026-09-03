#include "zAssetTypes.h"

#include "xAnim.h"
#include "xCM.h"
#include "xCurveAsset.h"
#include "xCutscene.h"
#include "xstransvc.h"
#include "xDebug.h"
#include "xEnv.h"
#include "xJSP.h"
#include "xMorph.h"

#include <types.h>
#include <stdio.h>
#include <rwcore.h>
#include <rpworld.h>

#ifdef PLATFORM_PC
#include "iTextPatch.h"
#include "xTextAsset.h"
#endif

static void* Curve_Read(void* param_1, U32 param_2, void* indata, U32 insize, U32* outsize);
#ifdef PLATFORM_PC
static void* TEXT_Read(void* param_1, U32 param_2, void* indata, U32 insize, U32* outsize);
#endif
static void* ATBL_Read(void* param_1, U32 param_2, void* indata, U32 insize, U32* outsize);
static void ATBL_Init();
static void* RWTX_Read(void* param_1, U32 param_2, void* indata, U32 insize, U32* outsize);
static void* Model_Read(void* param_1, U32 param_2, void* indata, U32 insize, U32* outsize);
static void* BSP_Read(void* param_1, U32 param_2, void* indata, U32 insize, U32* outsize);
static void* JSP_Read(void* param_1, U32 param_2, void* indata, U32 insize, U32* outsize);
static void* SndInfoRead(void*, unsigned int, void*, unsigned int, unsigned int*);
static void Model_Unload(void*, U32);
static void BSP_Unload(void*, U32);
static void JSP_Unload(void*, U32);
static void Anim_Unload(void*, U32);
static void TextureRW3_Unload(void*, U32);
static void LightKit_Unload(void*, U32);
#ifdef BFBB_PTR64
static void* LightKit_Read(void*, U32, void*, U32, U32*);
static void* CutsceneTOC_Read(void*, U32, void*, U32, U32*);
static void* Anim_Read(void*, U32, void*, U32, U32*);
static void* Credits_Read(void*, U32, void*, U32, U32*);
#define LKIT_READ LightKit_Read
#define CTOC_READ CutsceneTOC_Read
#define ANIM_READ Anim_Read
#define CRDT_READ Credits_Read
#else
#define LKIT_READ NULL
#define CTOC_READ NULL
#define ANIM_READ NULL
#define CRDT_READ NULL
#endif
static void MovePoint_Unload(void*, U32);

// The GameCube build's dummy is a xJSPHeaderGC: the retail object reserves 32
// bytes for it and JSP_Read below reports its size as 32. Declaring it as the
// 24-byte base left stripVecCount/stripVecList off the end of the object.
static xJSPHeaderGC sDummyEmptyJSP;
static xJSPHeader* sTempJSP;

static u32 s_sbFootSoundA;
static u32 s_sbFootSoundB;
static u32 s_scFootSoundA;
static u32 s_scFootSoundB;
static u32 s_patFootSoundA;
static u32 s_patFootSoundB;

static st_PACKER_ASSETTYPE assetTypeHandlers[78] = {
    { 'BSP ', 0, 0, BSP_Read, NULL, NULL, NULL, NULL, BSP_Unload, NULL },
    { 'JSP ', 0, 0, JSP_Read, NULL, NULL, NULL, NULL, JSP_Unload, NULL },
    { 'TXD ' },
    { 'MODL', 0, 0, Model_Read, NULL, NULL, NULL, NULL, Model_Unload, NULL },
    { 'ANIM', 0, 0, ANIM_READ, NULL, NULL, NULL, NULL, Anim_Unload, NULL },
    { 'RWTX', 0, 0, RWTX_Read, NULL, NULL, NULL, NULL, TextureRW3_Unload, NULL },
    { 'LKIT', 0, 0, LKIT_READ, NULL, NULL, NULL, NULL, LightKit_Unload, NULL },
    { 'CAM ' },
    { 'PLYR' },
    { 'NPC ' },
    { 'ITEM' },
    { 'PKUP' },
    { 'TRIG' },
    { 'SDF ' },
    { 'TEX ' },
    { 'TXT ' },
    { 'ENV ' },
    { 'ATBL', 0, 0, ATBL_Read, NULL, NULL, NULL, NULL, NULL, NULL },
    { 'MINF' },
    { 'PICK' },
    { 'PLAT' },
    { 'PEND' },
    { 'MRKR' },
    { 'MVPT', 0, 0, NULL, NULL, NULL, NULL, NULL, MovePoint_Unload, NULL },
    { 'TIMR' },
    { 'CNTR' },
    { 'PORT' },
    { 'SND ' },
    { 'SNDS' },
    { 'GRUP' },
    { 'MPHT' },
    { 'SFX ' },
    { 'SNDI', 0, 0, SndInfoRead, NULL, NULL, NULL, NULL, NULL, NULL },
    { 'HANG' },
    { 'SIMP' },
    { 'BUTN' },
    { 'SURF' },
    { 'DSTR' },
    { 'BOUL' },
    { 'MAPR' },
    { 'GUST' },
    { 'VOLU' },
    { 'UI  ' },
    { 'UIFT' },
#ifdef PLATFORM_PC
    // PORT: the one place every TEXT asset in the game passes through, whatever
    // archive it came from. The port runs on the Xbox assets and their text
    // talks about an Xbox; TEXT_Read below rewrites that as the asset loads.
    // Retail leaves this type with no handlers at all, which is what the
    // GameCube build still compiles.
    { 'TEXT', 0, 0, TEXT_Read, NULL, NULL, NULL, NULL, NULL, NULL },
#else
    { 'TEXT' },
#endif
    { 'COND' },
    { 'DPAT' },
    { 'PRJT' },
    { 'LOBM' },
    { 'FOG ' },
    { 'LITE' },
    { 'PARE' },
    { 'PARS' },
    { 'CSN ' },
    { 'CTOC', 0, 0, CTOC_READ, NULL, NULL, NULL, NULL, NULL, NULL },
    { 'CSNM' },
    { 'EGEN' },
    { 'ALST' },
    { 'RAW ' },
    { 'LODT' },
    { 'SHDW' },
    { 'DYNA' },
    { 'VIL ' },
    { 'VILP' },
    { 'COLL' },
    { 'PARP' },
    { 'PIPT' },
    { 'DSCO' },
    { 'JAW ' },
    { 'SHRP' },
    { 'FLY ' },
    { 'TRCK' },
    { 'CRV ', 0, 0, Curve_Read, NULL, NULL, NULL, NULL, NULL, NULL },
    { 'ZLIN' },
    { 'DUPC' },
    { 'SLID' },
    { 'CRDT', 0, 0, CRDT_READ, NULL, NULL, NULL, NULL, NULL, NULL },
};

void zAssetStartup()
{
    xSTStartup(assetTypeHandlers);
    ATBL_Init();
}

void zAssetShutdown()
{
    xSTShutdown();
}

static HackModelRadius hackRadiusTable[3] = { { 0xFA77E6FAU, 20.0f },
                                              { 0x5BD0EDACU, 1000.0f },
                                              { 0xED21A1C6U, 50.0f } };

static void* Model_Read(void* param_1, U32 param_2, void* indata, U32 insize, U32* outsize)
{
    RpAtomic* model = (RpAtomic*)iModelFileNew(indata, insize);

    *outsize = 0x70;

    for (int i = 0; i < 3; i++)
    {
        if (param_2 != hackRadiusTable[i].assetid)
        {
            continue;
        }
        for (RpAtomic* tmpModel = model; tmpModel != NULL;
             tmpModel = (RpAtomic*)iModelFile_RWMultiAtomic(tmpModel))
        {
            tmpModel->boundingSphere.radius = hackRadiusTable[i].radius;

            tmpModel->boundingSphere.center.x = 0.0f;
            tmpModel->boundingSphere.center.y = 0.0f;
            tmpModel->boundingSphere.center.z = 0.0f;

#ifndef PLATFORM_PC
            tmpModel->interpolator.flags &= ~2;
#else
    // PORT: rpINTERPOLATORDIRTYSPHERE (bit 1) tells RenderWare not to recompute
    // the bounding sphere from the morph targets, because the one just written
    // above is the authoritative one. librw has no such flag and never
    // recomputes -- its Atomic::boundingSphere is plain state, and
    // SAMEBOUNDINGSPHERE is a parameter to setGeometry rather than something it
    // stores. So the behaviour this line asks for is already librw's default,
    // and there is no field to clear: RpAtomic mirrors rw::Atomic exactly, and
    // librw does the allocating, so a member with nowhere to live would be a
    // write past the end of the object.
#endif
        }
        break;
    }

    return model;
}
static void* Curve_Read(void* param_1, U32 param_2, void* indata, U32 insize, U32* outsize)
{
    *outsize = insize;

    void* __dest = RWSRCGLOBAL(memoryFuncs.rwmalloc(insize));
    memcpy(__dest, indata, insize);

    // The baked point array is packed directly after the header, so points
    // has to be fixed up to point just past the struct we copied in.
    ((xCurveAsset*)__dest)->points = (F32*)((xCurveAsset*)__dest + 1);

    return __dest;
}

#ifdef PLATFORM_PC
// PORT: rewrites the console out of a TEXT asset's string as it loads. See
// src/SB/Core/pc/iTextPatch.h; off in config.ini this is a no-op and the asset
// arrives exactly as the disc shipped it.
//
// The asset is transformed IN PLACE and handed straight back. Every other
// readXForm here returns a new object, and the packer supports both -- but a
// TEXT asset's payload is a string, the rewrite only ever shortens it, and
// returning the same bytes means there is nothing to free and so no unload
// handler to keep in step with this one.
static void* TEXT_Read(void*, U32 assetID, void* indata, U32 insize, U32* outsize)
{
    *outsize = insize;

    // The asset is an xTextAsset header and then the string. One that cannot
    // hold both is not a text asset, and is left for the callers to find empty
    // the same way retail would.
    //
    // Qualified, and spelled out rather than going through xTextAssetGetText:
    // zTalkBox.h declares a second xTextAsset of its own inside an anonymous
    // namespace, and it reaches this file through the include chain, so the
    // bare name is ambiguous here and the macro's cast is too.
    if (indata != NULL && insize > sizeof(::xTextAsset))
    {
        char* text = (char*)((::xTextAsset*)indata + 1);
        iTextPatchAsset(assetID, text, insize - sizeof(::xTextAsset));
    }

    return indata;
}
#endif

static void Model_Unload(void* userdata, U32)
{
    if (userdata != NULL)
    {
        iModelUnload((RpAtomic*)userdata);
    }
}

static void* BSP_Read(void* param_1, U32 param_2, void* indata, U32 insize, U32* outsize)
{
    RwMemory rwmem = { (U8*)indata, insize };

    RwStream* stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &rwmem);
    if (stream == 0)
    {
        printf("BSP_Read RwStreamOpen failed\n");
    }

    if (RwStreamFindChunk(stream, 0xb, 0, 0) == 0)
    {
        RwChunkHeaderInfo chunkHeaderInfo;
        RwStreamReadChunkHeaderInfo(stream, &chunkHeaderInfo);
        *outsize = 0;
        return 0;
    }

    RpWorld* bsp = RpWorldStreamRead(stream);
    if (bsp == 0)
    {
        printf("BSP_Read RpWorldStreamRead failed\n");
    }
    RwStreamClose(stream, 0);
    *outsize = 4;

    return bsp;
}

static void BSP_Unload(void*, U32)
{
    xEnvFree(globals.sceneCur->env);
}

static char* jsp_shadow_hack_textures[] = {
    "beach_towel",  "wood_board_Nails_singleV2", "wood_board_Nails_singleV3",
    "glass_broken", "ground_path_alpha",
};

static char** jsp_shadow_hack_end_textures = &jsp_shadow_hack_textures[5];

struct AnimTableList animTable[33] = {
    { "ZNPC_AnimTable_Test", ZNPC_AnimTable_Test, 0 },
    { "ZNPC_AnimTable_Dutchman", ZNPC_AnimTable_Dutchman, 0 },
    { "ZNPC_AnimTable_Duplotron", ZNPC_AnimTable_Duplotron, 0 },
    { "ZNPC_AnimTable_Common", ZNPC_AnimTable_Common, 0 },
    { "ZNPC_AnimTable_BossPlankton", ZNPC_AnimTable_BossPlankton, 0 },
    { "ZNPC_AnimTable_BossSandy", ZNPC_AnimTable_BossSandy, 0 },
    { "ZNPC_AnimTable_SleepyTime", ZNPC_AnimTable_SleepyTime, 0 },
    { "ZNPC_AnimTable_BossSandyHead", ZNPC_AnimTable_BossSandyHead, 0 },
    { "ZNPC_AnimTable_Hammer", ZNPC_AnimTable_Hammer, 0 },
    { "ZNPC_AnimTable_TTSauce", ZNPC_AnimTable_TTSauce, 0 },
    { "ZNPC_AnimTable_KingJelly", ZNPC_AnimTable_KingJelly, 0 },
    { "ZNPC_AnimTable_Slick", ZNPC_AnimTable_Slick, 0 },
    { "ZNPC_AnimTable_TarTar", ZNPC_AnimTable_TarTar, 0 },
    { "ZNPC_AnimTable_Villager", ZNPC_AnimTable_Villager, 0 },
    { "ZNPC_AnimTable_BalloonBoy", ZNPC_AnimTable_BalloonBoy, 0 },
    { "ZNPC_AnimTable_Fodder", ZNPC_AnimTable_Fodder, 0 },
    { "ZNPC_AnimTable_Prawn", ZNPC_AnimTable_Prawn, 0 },
    { "ZNPC_AnimTable_Neptune", ZNPC_AnimTable_Neptune, 0 },
    { "ZNPC_AnimTable_BossSB1", ZNPC_AnimTable_BossSB1, 0 },
    { "ZNPC_AnimTable_BossSBobbyArm", ZNPC_AnimTable_BossSBobbyArm, 0 },
    { "ZNPC_AnimTable_Monsoon", ZNPC_AnimTable_Monsoon, 0 },
    { "ZNPC_AnimTable_ArfDog", ZNPC_AnimTable_ArfDog, 0 },
    { "ZNPC_AnimTable_ArfArf", ZNPC_AnimTable_ArfArf, 0 },
    { "ZNPC_AnimTable_BossSB2", ZNPC_AnimTable_BossSB2, 0 },
    { "ZNPC_AnimTable_Tiki", ZNPC_AnimTable_Tiki, 0 },
    { "ZNPC_AnimTable_Tubelet", ZNPC_AnimTable_Tubelet, 0 },
    { "ZNPC_AnimTable_Ambient", ZNPC_AnimTable_Ambient, 0 },
    { "ZNPC_AnimTable_GLove", ZNPC_AnimTable_GLove, 0 },
    { "ZNPC_AnimTable_LassoGuide", ZNPC_AnimTable_LassoGuide, 0 },
    { "ZNPC_AnimTable_Chuck", ZNPC_AnimTable_Chuck, 0 },
    { "ZNPC_AnimTable_Jelly", ZNPC_AnimTable_Jelly, 0 },
    { "ZNPC_AnimTable_SuperFriend", ZNPC_AnimTable_SuperFriend, 0 },
    {
        "ZNPC_AnimTable_BossPatrick",
        ZNPC_AnimTable_BossPatrick,
        0,
    }
};

struct jsp_shadow_hack_atomic_context
{
    xJSPHeader* jsp;
    S32 index;
    S32 last_material;
};

inline bool jsp_shadow_hack_match(RpAtomic* atomic)
{
    RpGeometry* geom = RpAtomicGetGeometry(atomic);
    S32 numMaterials = geom->matList.numMaterials;

    char** hack = &jsp_shadow_hack_textures[0];
    char** hack_end = jsp_shadow_hack_end_textures;
    for (; hack != hack_end; ++hack)
    {
        char* name = *hack;
        for (S32 i = 0; i < numMaterials; ++i)
        {
            RwTexture* texture = geom->matList.materials[i]->texture;
            if (texture == NULL)
            {
                continue;
            }
            char* texname = texture->name;
            if (texname == NULL)
            {
                continue;
            }
            if (stricmp(texname, name) == 0)
            {
                return true;
            }
        }
    }
    return false;
}

static RpAtomic* jsp_shadow_hack_atomic_cb(RpAtomic* atomic, void* data)
{
    jsp_shadow_hack_atomic_context& context = *(jsp_shadow_hack_atomic_context*)data;
    S32 index = context.index;
    context.index++;

    if (!jsp_shadow_hack_match(atomic))
    {
        return atomic;
    }

    xClumpCollBSPTree* colltree = context.jsp->colltree;
    if (context.jsp->jspNodeList[index].originalMatIndex == context.last_material)
    {
        return atomic;
    }

    S32 material_index = context.jsp->jspNodeList[index].originalMatIndex;
    context.last_material = material_index;

    xClumpCollBSPTriangle* tri = colltree->triangles;
    xClumpCollBSPTriangle* end_tri = colltree->triangles + colltree->numTriangles;

    for (; tri != end_tri; ++tri)
    {
        if (tri->matIndex != material_index)
        {
            continue;
        }
        tri->flags |= 0x20;
    }
    return atomic;
}

static void jsp_shadow_hack(xJSPHeader* header)
{
    if (header == NULL || header->clump == 0 || header->colltree == NULL)
    {
        return;
    }

    jsp_shadow_hack_atomic_context context = { header, 0, -1 };
    RpClumpForAllAtomics(header->clump, jsp_shadow_hack_atomic_cb, &context);
}

static xAnimTable* (*tableFuncList[])() = {
    zEntPlayer_AnimTable,
    ZNPC_AnimTable_Common,
    zPatrick_AnimTable,
    zSandy_AnimTable,
    ZNPC_AnimTable_Villager,
    zSpongeBobTongue_AnimTable,
    ZNPC_AnimTable_LassoGuide,
    ZNPC_AnimTable_Hammer,
    ZNPC_AnimTable_TarTar,
    ZNPC_AnimTable_GLove,
    ZNPC_AnimTable_Monsoon,
    ZNPC_AnimTable_SleepyTime,
    ZNPC_AnimTable_ArfDog,
    ZNPC_AnimTable_ArfArf,
    ZNPC_AnimTable_Chuck,
    ZNPC_AnimTable_Tubelet,
    ZNPC_AnimTable_Slick,
    ZNPC_AnimTable_Ambient,
    ZNPC_AnimTable_Tiki,
    ZNPC_AnimTable_Fodder,
    ZNPC_AnimTable_Duplotron,
    ZNPC_AnimTable_Jelly,
    ZNPC_AnimTable_Test,
    ZNPC_AnimTable_Neptune,
    ZNPC_AnimTable_KingJelly,
    ZNPC_AnimTable_Dutchman,
    ZNPC_AnimTable_Prawn,
    ZNPC_AnimTable_BossSandy,
    ZNPC_AnimTable_BossPatrick,
    ZNPC_AnimTable_BossSB1,
    ZNPC_AnimTable_BossSB2,
    ZNPC_AnimTable_BossSBobbyArm,
    ZNPC_AnimTable_BossPlankton,
    zEntPlayer_BoulderVehicleAnimTable,
    ZNPC_AnimTable_BossSandyHead,
    ZNPC_AnimTable_BalloonBoy,
    xEnt_AnimTable_AutoEventSmall,
    ZNPC_AnimTable_SlickShield,
    ZNPC_AnimTable_SuperFriend,
    ZNPC_AnimTable_ThunderCloud,
    XHUD_AnimTable_Idle,
    ZNPC_AnimTable_NightLight,
    ZNPC_AnimTable_HazardStd,
    ZNPC_AnimTable_FloatDevice,
    cruise_bubble::anim_table, // Cruise Bubble anim table based on PS2 DWARF data
    ZNPC_AnimTable_BossSandyScoreboard,
    zEntPlayer_TreeDomeSBAnimTable,
    NULL,
};

static void* JSP_Read(void* param_1, U32 param_2, void* indata, U32 insize, U32* outsize)
{
    xJSPHeader* retjsp = &sDummyEmptyJSP;
    *outsize = 32;
    xJSP_MultiStreamRead(indata, insize, &sTempJSP);
    if (sTempJSP->jspNodeList != NULL)
    {
        retjsp = sTempJSP;
        sTempJSP = 0;
        *outsize = 4;
    }
    jsp_shadow_hack(retjsp);
    return retjsp;
}

static void JSP_Unload(void* userdata, U32 b)
{
    if ((xJSPHeader*)userdata != &sDummyEmptyJSP)
    {
        xJSP_Destroy((xJSPHeader*)userdata);
    }
}

static RwTexture* TexCB(RwTexture* texture, void* data)
{
    if (*(RwTexture**)data == NULL)
    {
        *(RwTexture**)(data) = texture;
    }
    return texture;
}

static void* RWTX_Read(void*, U32, void* indata, U32 insize, U32* outsize)
{
    RwTexDictionary* txd = NULL;
    RwMemory rwmem;
    RwStream* stream = NULL;
    RwTexture* tex = NULL;
    RwError error;

    if (insize != 0)
    {
        rwmem.start = (U8*)indata;
        rwmem.length = insize;
        stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &rwmem);
        if (stream != NULL)
        {
            if (!RwStreamFindChunk(stream, rwID_TEXDICTIONARY, NULL, NULL))
            {
                RwErrorGet(&error);
                RwStreamFindChunk(stream, rwID_TEXDICTIONARY, NULL, NULL);
                RwStreamClose(stream, NULL);
            }
            else
            {
                txd = RwTexDictionaryStreamRead(stream);
                RwStreamClose(stream, NULL);
                if (txd != NULL)
                {
                    RwTexDictionaryForAllTextures(txd, TexCB, &tex);
                    if (tex == NULL)
                    {
                        RwTexDictionaryDestroy(txd);
                    }
                    else
                    {
                        RwTexDictionaryRemoveTexture(tex);
                        RwTexDictionaryDestroy(txd);
                        RwTextureAddRef(tex);
                        RwTextureSetFilterMode(tex, rwFILTERLINEARMIPLINEAR);
                        *outsize = sizeof(RwTexture);
                        return tex;
                    }
                }
            }
        }
    }

    *outsize = insize;
    return NULL;
}

static void TextureRW3_Unload(void* a, U32 b)
{
    if (a != NULL)
    {
        ((RwTexture*)(a))->refCount = 1;
        RwTextureDestroy((RwTexture*)a);
    }
}

static void ATBL_Init()
{
    for (int i = 0; i < 0x21; i++)
    {
        animTable[i].id = xStrHash(animTable[i].name);
    }
}

void FootstepHackSceneEnter()
{
    s_sbFootSoundA = xStrHash("SB_run1L");
    s_sbFootSoundB = xStrHash("SB_run1R");
    s_scFootSoundA = xStrHash("SC_run_kelpL");
    s_scFootSoundB = xStrHash("SC_run_kelpL");
    s_patFootSoundA = xStrHash("Pat_run_rock_dryL");
    s_patFootSoundB = xStrHash("Pat_run_rock_dryR");
}

static U32 dummyEffectCB(U32, xAnimActiveEffect*, xAnimSingle*, void*)
{
    return 0;
}

static U32 soundEffectCB(U32 cbenum, xAnimActiveEffect* acteffect, xAnimSingle* single,
                         void* object)
{
    U32 sndhandle = 0;
    U32 vil_SID = 0;
    S32 vil_result;

    if (cbenum == 1)
    {
        xEnt* ent_tmp = (xEnt*)object;
        zAnimFxSound* snd = (zAnimFxSound*)(acteffect->Effect + 1);
        if (ent_tmp == NULL)
        {
            vil_result = 0;
        }
        else if (ent_tmp->baseType == eBaseTypeNPC)
        {
            vil_result = ((zNPCCommon*)ent_tmp)->SndPlayFromAFX(snd, &vil_SID);
        }
        else
        {
            vil_result = 0;
        }

        if (vil_result > 0)
        {
            sndhandle = vil_SID;
        }
        else if (vil_result < 0)
        {
            sndhandle = 0;
        }
        else
        {
            U32 id = snd->ID;
            U32 newId;
            F32 volFactor;

            static U32 footSelector = 0;
            footSelector++;
            if (id == 0x42B584CB || id == 0x56E1F71E || id == 0x331BDF8F)
            {
                newId = 0;
                volFactor = 1.0f;

                switch (id)
                {
                case 0x42B584CB:
                {
                    newId = footSelector % 2 ? s_sbFootSoundA : s_sbFootSoundB;
                    volFactor = 0.6f;
                    break;
                }
                case 0x56E1F71E:
                {
                    newId = footSelector % 2 ? s_scFootSoundA : s_scFootSoundB;
                    volFactor = 0.4f;
                    break;
                }
                case 0x331BDF8F:
                {
                    newId = footSelector % 2 ? s_patFootSoundA : s_patFootSoundB;
                    volFactor = 0.6f;
                    break;
                }
                }
                volFactor *= 0.65f;
                sndhandle = xSndPlay(newId, snd->vol * 0.77f * volFactor, snd->pitch, snd->priority,
                                     snd->flags, 0, SND_CAT_GAME, 0.0f);
            }
            else
            {
                sndhandle = xSndPlay3D(snd->ID, snd->vol * 0.77f, snd->pitch, snd->priority,
                                       snd->flags, (xEnt*)object, snd->radius, SND_CAT_GAME, 0.0f);
            }
        }
    }
    if (cbenum == 3)
    {
        xSndStop(acteffect->Handle);
    }

    return sndhandle;
}

static U32 (*effectFuncList[])(U32, xAnimActiveEffect*, xAnimSingle*, void*) = { dummyEffectCB,
                                                                                 soundEffectCB };

static void* FindAssetCB(U32 ID, char*)
{
    U32 size;
    return xSTFindAsset(ID, &size);
}

static xAnimTable* Anim_ATBL_getTable(xAnimTable* (*constructor)());

#ifdef BFBB_PTR64
// The same transform as ATBL_Read below, for a host where a pointer is 8 bytes.
//
// ATBL_Read works in place: the asset arrives holding asset ids, indices and
// byte offsets, and it overwrites each of them with the pointer it resolves to.
// That only works while a pointer is exactly as wide as the slot the file
// reserved for it. Here it is not, so this walks the same bytes at their real
// on-disk stride and keeps the pointers in arrays of its own. The asset is not
// written to at all.
//
// The layout, in bytes from the start of the asset:
//
//     0                 xAnimAssetTable, 5 x U32
//     20                NumRaw   x U32   asset id of each raw animation
//     20 + 4 * NumRaw   NumFiles x 32    file records, below
//     ...               NumStates x 28   xAnimAssetState
//
// and one file record is
//
//     0   U32  FileFlags      16  U32  RawData, a byte offset into the asset
//     4   F32  Duration       20  S32  Physics
//     8   F32  TimeOffset     24  S32  StartPose
//     12  U16  NumAnims[2]    28  S32  EndPose
//
// where RawData points at NumAnims[0] * NumAnims[1] U32 indices into the raw
// array. xAnimAssetState and xAnimAssetEffect hold no pointers, so those two
// are the same size here as on disk and are still read through their structs.
#define ATBL_DISK_FILE_SIZE 32

static void* ATBL_Read64(void* indata, U32* outsize)
{
    U32 i;
    U32 j;
    U32 debugNum = 0;
    U32 tmpsize;

    xAnimTable* table;
    xAnimState* astate;
    xAnimTransition* atran;

    U8* asset = (U8*)indata;
    xAnimAssetTable* zaTbl = (xAnimAssetTable*)indata;
    U32* rawID = (U32*)(zaTbl + 1);
    U8* fileRec = (U8*)(rawID + zaTbl->NumRaw);
    xAnimAssetState* zaState =
        (xAnimAssetState*)(fileRec + zaTbl->NumFiles * ATBL_DISK_FILE_SIZE);

    void** zaRaw = (void**)xMemPushTemp(zaTbl->NumRaw * sizeof(void*));
    xAnimFile** fList = (xAnimFile**)xMemPushTemp(zaTbl->NumFiles * sizeof(xAnimFile*));

    for (i = 0; i < zaTbl->NumRaw; ++i)
    {
        zaRaw[i] = xSTFindAsset(rawID[i], &tmpsize);
    }

    for (i = 0; i < zaTbl->NumRaw; ++i)
    {
        if (zaRaw[i] == NULL)
        {
            for (j = 0; j < zaTbl->NumRaw; ++j)
            {
                if (zaRaw[j] != NULL)
                {
                    zaRaw[i] = zaRaw[j];
                    break;
                }
            }
        }
    }

    for (i = 0; i < zaTbl->NumRaw; ++i)
    {
        if (*(U32*)zaRaw[i] == 'QSPM')
        {
            xMorphSeqSetup(zaRaw[i], FindAssetCB);
        }
    }

    for (i = 0; i < zaTbl->NumFiles; ++i)
    {
        U8* rec = fileRec + i * ATBL_DISK_FILE_SIZE;
        U32 fileFlags = *(U32*)(rec + 0);
        F32 duration = *(F32*)(rec + 4);
        F32 timeOffset = *(F32*)(rec + 8);
        U32 numX = *(U16*)(rec + 12);
        U32 numY = *(U16*)(rec + 14);
        U32* rawIndex = (U32*)(asset + *(U32*)(rec + 16));
        U32 numAnims = numX * numY;

        void** rawData = (void**)xMemPushTemp(numAnims * sizeof(void*));
        for (U32 k = 0; k < numAnims; ++k)
        {
            rawData[k] = zaRaw[rawIndex[k]];
        }

        fList[i] = xAnimFileNewBilinear(rawData, "", fileFlags, NULL, numX, numY);
        if (timeOffset >= 0.0f)
        {
            xAnimFileSetTime(fList[i], duration, timeOffset);
        }

        xMemPopTemp(rawData);
    }

    xAnimTable* (*constructor)() = NULL;
    if (zaTbl->ConstructFunc < sizeof(tableFuncList) / sizeof(xAnimTable * (*)()))
    {
        constructor = tableFuncList[zaTbl->ConstructFunc];
    }
    else
    {
        for (S32 k = 0; k < sizeof(animTable) / sizeof(AnimTableList); ++k)
        {
            if (zaTbl->ConstructFunc == animTable[k].id)
            {
                constructor = animTable[k].constructor;
                break;
            }
        }
    }

    gxAnimUseGrowAlloc = true;

    table = Anim_ATBL_getTable(constructor);

    char tmpstr[32];
    for (i = 0; i < zaTbl->NumStates; ++i)
    {
        astate = xAnimTableAddFileID(table, fList[zaState[i].FileIndex], zaState[i].StateID,
                                     zaState[i].SubStateID, zaState[i].SubStateCount);

        if (astate == NULL)
        {
            sprintf(tmpstr, "Debug%02d", debugNum++);
            astate = xAnimTableNewState(table, tmpstr, 0x20, 0x80000000, 1.0f, NULL, NULL, 0.0f,
                                        NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
            atran = xAnimTableNewTransition(table, tmpstr, NULL, NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0,
                                            0, 0.2f, NULL);
            atran->Dest = table->StateList;
            xAnimTableAddFileID(table, fList[zaState[i].FileIndex], astate->ID, 0, 0);
        }
        astate->Speed = zaState[i].Speed;
    }

    xAnimFile* foundFile = NULL;
    for (astate = table->StateList; astate != NULL; astate = astate->Next)
    {
        if (foundFile == NULL && astate->Data != NULL)
        {
            foundFile = astate->Data;
        }
    }
    for (astate = table->StateList; astate != NULL; astate = astate->Next)
    {
        if (astate->Data == NULL)
        {
            astate->Data = foundFile;
            astate->UserFlags |= 0x40000000;
        }
    }

    for (i = 0; i < zaTbl->NumStates; ++i)
    {
        if (zaState[i].EffectCount != 0)
        {
            xAnimState* state = xAnimTableGetStateID(table, zaState[i].StateID);
            xAnimAssetEffect* zaEffect = (xAnimAssetEffect*)(asset + zaState[i].EffectOffset);

            if (state != NULL)
            {
                for (j = 0; j < zaState[i].EffectCount; ++j)
                {
                    xAnimEffect* effect =
                        xAnimStateNewEffect(state, zaEffect->Flags, zaEffect->StartTime,
                                            zaEffect->EndTime, effectFuncList[zaEffect->EffectType],
                                            zaEffect->UserDataSize);
                    memcpy(effect + 1, zaEffect + 1, zaEffect->UserDataSize);

                    zaEffect = (xAnimAssetEffect*)((U8*)zaEffect + zaEffect->UserDataSize) + 1;
                }
            }
        }
    }

    gxAnimUseGrowAlloc = false;

    xMemPopTemp(fList);
    xMemPopTemp(zaRaw);

    *outsize = sizeof(xAnimTable);
    return table;
}
#endif

static void* ATBL_Read(void*, U32, void* indata, U32 param_4, U32* outsize)
{
#ifdef BFBB_PTR64
    return ATBL_Read64(indata, outsize);
#else
    U32 i;
    U32 j;
    U32 debugNum = 0;
    U32 tmpsize;

    xAnimTable* table;
    xAnimState* astate;
    xAnimTransition* atran;
    U8* zaBytes;

    xAnimAssetTable* zaTbl = (xAnimAssetTable*)indata;
    void** zaRaw = (void**)(zaTbl + 1);
    xAnimAssetFile* zaFile = (xAnimAssetFile*)(zaRaw + zaTbl->NumRaw);
    xAnimAssetState* zaState =
        (xAnimAssetState*)((U32)(UPtr)zaFile + zaTbl->NumFiles * sizeof(xAnimAssetFile));

    for (i = 0; i < zaTbl->NumRaw; ++i)
    {
        zaRaw[i] = xSTFindAsset(*(U32*)&zaRaw[i], &tmpsize);
    }

    for (i = 0; i < zaTbl->NumRaw; ++i)
    {
        if (zaRaw[i] == NULL)
        {
            for (j = 0; j < zaTbl->NumRaw; ++j)
            {
                if (zaRaw[j] != NULL)
                {
                    zaRaw[i] = zaRaw[j];
                    break;
                }
            }
        }
    }

    for (i = 0; i < zaTbl->NumRaw; ++i)
    {
        if (*(U32*)zaRaw[i] == 'QSPM')
        {
            xMorphSeqSetup(zaRaw[i], FindAssetCB);
        }
    }

    for (i = 0; i < zaTbl->NumFiles; ++i)
    {
        zaFile[i].RawData = (void**)(UPtr)((U32)(UPtr)zaFile[i].RawData + (U32)(UPtr)zaTbl);
        for (S32 k = 0; k < zaFile[i].NumAnims[0] * zaFile[i].NumAnims[1]; ++k)
        {
            zaFile[i].RawData[k] = zaRaw[(U32)(UPtr)zaFile[i].RawData[k]];
        }
    }

    xAnimFile** fList = (xAnimFile**)zaFile;
    for (i = 0; i < zaTbl->NumFiles; ++i)
    {
        fList[i] = xAnimFileNewBilinear(zaFile[i].RawData, "", zaFile[i].FileFlags, NULL,
                                        zaFile[i].NumAnims[0], zaFile[i].NumAnims[1]);
        if (zaFile[i].TimeOffset >= 0.0f)
        {
            xAnimFileSetTime(fList[i], zaFile[i].Duration, zaFile[i].TimeOffset);
        }
    }

    xAnimTable* (*constructor)() = NULL;
    if (zaTbl->ConstructFunc < sizeof(tableFuncList) / sizeof(xAnimTable * (*)()))
    {
        constructor = tableFuncList[zaTbl->ConstructFunc];
    }
    else
    {
        for (S32 i = 0; i < sizeof(animTable) / sizeof(AnimTableList); ++i)
        {
            if (zaTbl->ConstructFunc == animTable[i].id)
            {
                constructor = animTable[i].constructor;
                break;
            }
        }
    }

    gxAnimUseGrowAlloc = true;

    table = Anim_ATBL_getTable(constructor);

    char tmpstr[32];
    for (i = 0; i < zaTbl->NumStates; ++i)
    {
        astate = xAnimTableAddFileID(table, fList[zaState[i].FileIndex], zaState[i].StateID,
                                     zaState[i].SubStateID, zaState[i].SubStateCount);

        if (astate == NULL)
        {
            sprintf(tmpstr, "Debug%02d", debugNum++);
            astate = xAnimTableNewState(table, tmpstr, 0x20, 0x80000000, 1.0f, NULL, NULL, 0.0f,
                                        NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
            atran = xAnimTableNewTransition(table, tmpstr, NULL, NULL, NULL, 0x10, 0, 0.0f, 0.0f, 0,
                                            0, 0.2f, NULL);
            atran->Dest = table->StateList;
            xAnimTableAddFileID(table, fList[zaState[i].FileIndex], astate->ID, 0, 0);
        }
        astate->Speed = zaState[i].Speed;
    }

    xAnimFile* foundFile = NULL;
    for (astate = table->StateList; astate != NULL; astate = astate->Next)
    {
        if (foundFile == NULL && astate->Data != NULL)
        {
            foundFile = astate->Data;
        }
    }
    for (astate = table->StateList; astate != NULL; astate = astate->Next)
    {
        if (astate->Data == NULL)
        {
            astate->Data = foundFile;
            astate->UserFlags |= 0x40000000;
        }
    }

    for (i = 0; i < zaTbl->NumStates; ++i)
    {
        if (zaState[i].EffectCount != 0)
        {
            xAnimState* state = xAnimTableGetStateID(table, zaState[i].StateID);
            xAnimAssetEffect* zaEffect = (xAnimAssetEffect*)(UPtr)((U32)(UPtr)zaTbl + zaState[i].EffectOffset);

            if (state != NULL)
            {
                for (j = 0; j < zaState[i].EffectCount; ++j)
                {
                    xAnimEffect* effect =
                        xAnimStateNewEffect(state, zaEffect->Flags, zaEffect->StartTime,
                                            zaEffect->EndTime, effectFuncList[zaEffect->EffectType],
                                            zaEffect->UserDataSize);
                    memcpy(effect + 1, zaEffect + 1, zaEffect->UserDataSize);

                    zaEffect = (xAnimAssetEffect*)(UPtr)(U32(UPtr(zaEffect)) + zaEffect->UserDataSize) + 1;
                }
            }
        }
    }

    gxAnimUseGrowAlloc = false;
    *outsize = sizeof(xAnimTable);
    return table;
#endif
}

static void Anim_Unload(void*, U32)
{
}

static void LightKit_Unload(void* userdata, U32 b)
{
    xLightKit_Destroy((xLightKit*)userdata);
}

#ifdef BFBB_PTR64
// An LKIT asset is an xLightKit and then its lights, and both structs hold a
// pointer, so both are wider here than on disc. The asset is copied into an
// allocation laid out for this build's structs rather than read in place.
//
//     0   U32  tagID          12  u32  lightList, not written on disc
//     4   U32  groupID        16  ...  lightCount x 96 light records
//     8   U32  lightCount
//
// and one light record is
//
//     0   U32  type              84  F32  radius
//     4   RwRGBAReal color       88  F32  angle
//     20  F32  matrix[16]        92  u32  platLight, filled in by Prepare
//
// The lights follow the header directly, which is what xLightKit_Prepare
// expects and also how zDiscoFloor and zNPCTypeBossSB2 build theirs by hand.
#define LKIT_DISK_HEADER_SIZE 16
#define LKIT_DISK_LIGHT_SIZE 96

static void* LightKit_Read(void*, U32, void* indata, U32 insize, U32* outsize)
{
    const U8* asset = (const U8*)indata;

    if (indata == NULL || insize < LKIT_DISK_HEADER_SIZE)
    {
        *outsize = 0;
        return indata;
    }

    U32 lightCount = ((const U32*)asset)[2];
    if (insize < LKIT_DISK_HEADER_SIZE + lightCount * LKIT_DISK_LIGHT_SIZE)
    {
        lightCount = (insize - LKIT_DISK_HEADER_SIZE) / LKIT_DISK_LIGHT_SIZE;
    }

    U32 size = sizeof(xLightKit) + lightCount * sizeof(xLightKitLight);
    xLightKit* lkit = (xLightKit*)xMemPushTemp(size);
    xLightKitLight* lights = (xLightKitLight*)(lkit + 1);

    lkit->tagID = ((const U32*)asset)[0];
    lkit->groupID = ((const U32*)asset)[1];
    lkit->lightCount = lightCount;
    lkit->lightList = lights;

    for (U32 i = 0; i < lightCount; i++)
    {
        const U8* rec = asset + LKIT_DISK_HEADER_SIZE + i * LKIT_DISK_LIGHT_SIZE;

        lights[i].type = *(const U32*)rec;
        memcpy(&lights[i].color, rec + 4, sizeof(RwRGBAReal));
        memcpy(lights[i].matrix, rec + 20, sizeof(lights[i].matrix));
        lights[i].radius = *(const F32*)(rec + 84);
        lights[i].angle = *(const F32*)(rec + 88);
        lights[i].platLight = NULL;
    }

    *outsize = size;
    return lkit;
}

// A CTOC asset is a count and then one record per cutscene:
//
//     0   U32  count
//     4   ...  count records, each cnfo->HeaderSize bytes
//
// and one record is an xCutsceneInfo, NumData xCutsceneData, NumTime + 1 chunk
// offsets, VisSize visibility words and BreakCount xCutsceneBreak.
// xCutsceneInfo and everything after the data array are words and characters,
// so only xCutsceneData changes width: its last member is a union of the file
// offset on disc and the pointer xCutscene_Create writes over it. The records
// are copied into an allocation with room for that, and HeaderSize is adjusted
// so the walk over the copy still steps one record at a time.
#define CTOC_DISK_DATA_SIZE 16

static void* CutsceneTOC_Read(void*, U32, void* indata, U32 insize, U32* outsize)
{
    const U8* asset = (const U8*)indata;

    if (indata == NULL || insize < sizeof(U32))
    {
        *outsize = 0;
        return indata;
    }

    U32 count = *(const U32*)asset;
    U32 grow = sizeof(xCutsceneData) - CTOC_DISK_DATA_SIZE;
    U32 size = sizeof(U32);
    U32 i;

    const xCutsceneInfo* cnfo = (const xCutsceneInfo*)(asset + sizeof(U32));
    for (i = 0; i < count; i++)
    {
        if (cnfo->HeaderSize < sizeof(xCutsceneInfo) + cnfo->NumData * CTOC_DISK_DATA_SIZE)
        {
            *outsize = insize;
            return indata;
        }

        size += cnfo->HeaderSize + cnfo->NumData * grow;
        cnfo = (const xCutsceneInfo*)((const U8*)cnfo + cnfo->HeaderSize);
    }

    U8* out = (U8*)xMemPushTemp(size);
    *(U32*)out = count;

    U8* dest = out + sizeof(U32);
    cnfo = (const xCutsceneInfo*)(asset + sizeof(U32));

    for (i = 0; i < count; i++)
    {
        const U8* src = (const U8*)cnfo;
        U32 numData = cnfo->NumData;
        U32 tail = cnfo->HeaderSize - sizeof(xCutsceneInfo) - numData * CTOC_DISK_DATA_SIZE;

        memcpy(dest, src, sizeof(xCutsceneInfo));
        ((xCutsceneInfo*)dest)->HeaderSize = cnfo->HeaderSize + numData * grow;

        xCutsceneData* data = (xCutsceneData*)(dest + sizeof(xCutsceneInfo));
        const U8* diskData = src + sizeof(xCutsceneInfo);

        for (U32 j = 0; j < numData; j++)
        {
            const U32* rec = (const U32*)(diskData + j * CTOC_DISK_DATA_SIZE);

            data[j].DataType = rec[0];
            data[j].AssetID = rec[1];
            data[j].ChunkSize = rec[2];
            data[j].DataPtr = NULL;
            data[j].FileOffset = rec[3];
        }

        memcpy(data + numData, diskData + numData * CTOC_DISK_DATA_SIZE, tail);

        dest += sizeof(xCutsceneInfo) + numData * sizeof(xCutsceneData) + tail;
        cnfo = (const xCutsceneInfo*)(src + cnfo->HeaderSize);
    }

    *outsize = size;
    return out;
}

// Most ANIM assets are raw animation data and pass straight through. A morph
// sequence is the exception: it is
//
//     0   xMorphSeqFile         TimeCount x 48   frame records, below
//     16  TimeCount x F32       ModelCount x 2   asset ids, one word each
//                               ...              the names behind them
//
// and one frame record is
//
//     0   u32  Model, an index into the asset ids
//     4   F32  RecipTime            16  u32  Targets[4], indices or -1
//     8   F32  Scale                32  S16  WeightStart[4]
//     12  U16  Flags, NumVerts      40  S16  WeightEnd[4]
//
// xMorphSeqSetup replaces Model, the targets and the asset ids in place with
// the pointers they resolve to, so all three are wider here than on disc.
#define MPSQ_MAGIC 'QSPM'
#define MPSQ_DISK_FRAME_SIZE 48
#define MPSQ_DISK_SLOT_SIZE 4

static void* Anim_Read(void*, U32, void* indata, U32 insize, U32* outsize)
{
    const U8* asset = (const U8*)indata;

    *outsize = insize;

    if (indata == NULL || insize < sizeof(xMorphSeqFile) || *(const U32*)asset != MPSQ_MAGIC)
    {
        return indata;
    }

    const xMorphSeqFile* hdr = (const xMorphSeqFile*)indata;
    U32 timeCount = hdr->TimeCount;
    U32 slotCount = hdr->ModelCount * 2;

    U32 fixed = sizeof(xMorphSeqFile) + timeCount * sizeof(F32);
    U32 diskBody = timeCount * MPSQ_DISK_FRAME_SIZE + slotCount * MPSQ_DISK_SLOT_SIZE;

    if (insize < fixed + diskBody)
    {
        return indata;
    }

    U32 nameBytes = insize - fixed - diskBody;
    U32 size = fixed + timeCount * sizeof(xMorphFrame) +
               slotCount * sizeof(xMorphAssetSlot) + nameBytes;

    U8* out = (U8*)xMemPushTemp(size);
    memcpy(out, asset, fixed);

    const U8* diskFrame = asset + fixed;
    xMorphFrame* frames = (xMorphFrame*)(out + fixed);

    for (U32 i = 0; i < timeCount; i++)
    {
        const U8* rec = diskFrame + i * MPSQ_DISK_FRAME_SIZE;
        const U32* words = (const U32*)rec;

        frames[i].Model = (RpAtomic*)(UPtr)words[0];
        frames[i].RecipTime = *(const F32*)(rec + 4);
        frames[i].Scale = *(const F32*)(rec + 8);
        frames[i].Flags = *(const U16*)(rec + 12);
        frames[i].NumVerts = *(const U16*)(rec + 14);

        for (U32 j = 0; j < 4; j++)
        {
            frames[i].Targets[j] = (S16*)(UPtr)words[4 + j];
            frames[i].WeightStart[j] = *(const S16*)(rec + 32 + j * 2);
            frames[i].WeightEnd[j] = *(const S16*)(rec + 40 + j * 2);
        }
    }

    const U32* diskSlot = (const U32*)(diskFrame + timeCount * MPSQ_DISK_FRAME_SIZE);
    xMorphAssetSlot* slots = (xMorphAssetSlot*)(frames + timeCount);

    for (U32 i = 0; i < slotCount; i++)
    {
        slots[i] = (xMorphAssetSlot)(UPtr)diskSlot[i];
    }

    memcpy(slots + slotCount, diskSlot + slotCount, nameBytes);

    *outsize = size;
    return out;
}

// A credits asset is an xCMheader and then credits blocks, each an xCMcredits,
// its presets, and the hunks that reference them:
//
//     0   xCMheader, total_size bytes to the end of the last block
//     24  xCMcredits, credits_size bytes to the end of its hunks
//         num_presets x xCMpreset
//         hunks, each hunk_size bytes: a 24-byte record and its text
//
// and one hunk record is
//
//     0   U32  hunk_size        8   F32  t0, t1
//     4   U32  preset           16  u32  text1, text2, offsets into the asset
//
// xCMprep turns those two offsets into pointers in place, so a hunk is wider
// here than on disc and the three sizes that walk the asset move with it. The
// copy is written with the pointers already resolved and state left at 0,
// which is the state that means "resolved" -- xCMprep never runs.
//
// xCMtexture is aliased over an xCMpreset's first textbox and caches an
// RwTexture* at offset 24. That still lands inside the 32-byte textbox at
// either width, so presets are copied as they are.
#define CRDT_MAGIC 0xBEEEEEEF
#define CRDT_DISK_HUNK_SIZE 24

static void* Credits_Read(void*, U32, void* indata, U32 insize, U32* outsize)
{
    const U8* asset = (const U8*)indata;

    *outsize = insize;

    if (indata == NULL || insize < sizeof(xCMheader))
    {
        return indata;
    }

    const xCMheader* hdr = (const xCMheader*)indata;
    U32 total = hdr->total_size;

    if (hdr->magic != CRDT_MAGIC || total > insize || total < sizeof(xCMheader))
    {
        return indata;
    }

    U32 grow = sizeof(xCMhunk) - CRDT_DISK_HUNK_SIZE;
    U32 hunks = 0;
    const U8* cp = asset + sizeof(xCMheader);

    while ((U32)(cp - asset) < total)
    {
        const xCMcredits* credits = (const xCMcredits*)cp;
        const U8* hp = cp + sizeof(xCMcredits) + credits->num_presets * sizeof(xCMpreset);

        while ((U32)(hp - cp) < credits->credits_size)
        {
            U32 hunkSize = *(const U32*)hp;

            if (hunkSize < CRDT_DISK_HUNK_SIZE || hp + hunkSize > asset + total)
            {
                return indata;
            }

            // Both texts have to live in the hunk that names them, or their
            // offsets cannot be moved with it.
            for (U32 j = 0; j < 2; j++)
            {
                U32 off = ((const U32*)hp)[4 + j];

                if (off != 0 && (off < (U32)(hp - asset) + CRDT_DISK_HUNK_SIZE ||
                                 off >= (U32)(hp - asset) + hunkSize))
                {
                    return indata;
                }
            }

            hunks++;
            hp += hunkSize;
        }

        cp = hp;
    }

    U32 size = total + hunks * grow;
    U8* out = (U8*)xMemPushTemp(size);

    memcpy(out, asset, sizeof(xCMheader));
    ((xCMheader*)out)->total_size = size;
    ((xCMheader*)out)->state = 0;

    cp = asset + sizeof(xCMheader);
    U8* dest = out + sizeof(xCMheader);

    while ((U32)(cp - asset) < total)
    {
        const xCMcredits* credits = (const xCMcredits*)cp;
        U32 presetBytes = credits->num_presets * sizeof(xCMpreset);
        U32 blockHunks = 0;

        memcpy(dest, cp, sizeof(xCMcredits) + presetBytes);

        const U8* hp = cp + sizeof(xCMcredits) + presetBytes;
        U8* destHunk = dest + sizeof(xCMcredits) + presetBytes;

        while ((U32)(hp - cp) < credits->credits_size)
        {
            U32 hunkSize = *(const U32*)hp;
            U32 textBytes = hunkSize - CRDT_DISK_HUNK_SIZE;
            xCMhunk* hunk = (xCMhunk*)destHunk;
            const U32* rec = (const U32*)hp;

            hunk->hunk_size = hunkSize + grow;
            hunk->preset = rec[1];
            hunk->t0 = *(const F32*)(hp + 8);
            hunk->t1 = *(const F32*)(hp + 12);

            memcpy(hunk + 1, hp + CRDT_DISK_HUNK_SIZE, textBytes);

            hunk->text1 = rec[4] != 0 ? (char*)(hunk + 1) + (rec[4] - ((U32)(hp - asset) +
                                                                      CRDT_DISK_HUNK_SIZE))
                                      : NULL;
            hunk->text2 = rec[5] != 0 ? (char*)(hunk + 1) + (rec[5] - ((U32)(hp - asset) +
                                                                      CRDT_DISK_HUNK_SIZE))
                                      : NULL;

            blockHunks++;
            hp += hunkSize;
            destHunk += hunkSize + grow;
        }

        ((xCMcredits*)dest)->credits_size = credits->credits_size + blockHunks * grow;

        dest = destHunk;
        cp = hp;
    }

    *outsize = size;
    return out;
}

#endif

static xAnimTable* Anim_ATBL_getTable(xAnimTable* (*constructor)())
{
    return constructor();
}

static void MovePoint_Unload(void* userdata, U32 b)
{
    xMovePointSplineDestroy((xMovePoint*)userdata);
}

static void* SndInfoRead(void* param_1, U32 param_2, void* indata, U32 insize, U32* outsize)
{
    void* __dest = RWSRCGLOBAL(memoryFuncs.rwmalloc(insize));

    if (__dest == NULL)
    {
        return __dest;
    }

    memcpy(__dest, indata, insize);

    if (iSndLoadSounds(__dest) == 0)
    {
        RWSRCGLOBAL(memoryFuncs.rwfree(__dest));
        return NULL;
    }
    else
    {
        *outsize = insize;
    }

    return __dest;
}
