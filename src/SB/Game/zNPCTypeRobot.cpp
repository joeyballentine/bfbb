#include "zNPCTypeRobot.h"
#include "zNPCSupplement.h"
#include "zNPCSupport.h"
#include "zNPCSndLists.h"
#include "zNPCGoalRobo.h"
#include "zNPCTypes.h"
#include "zNPCGoalStd.h"
#include "zGlobals.h"
#include "zNPCGoals.h"

#include "xFactory.h"
#include "xMath.h"
#include "xAnim.h"
#include "xBehaviour.h"
#include "xMathInlines.h"
#include "xGroup.h"
#include "zScene.h"
#include "xLinkAsset.h"
#include "xutil.h"
#include "zNPCGoalStd.h"
#include "zNPCGoalCommon.h"

#include <string.h>

extern UVAModelInfo g_uvaShield;

extern S32 g_cnt_fodbzzt;

extern S32 g_cnt_sleepy;

extern S32 g_needuvincr_tube;

extern S32 g_needuvincr_bzzt;

extern S32 g_needuvincr_nightlight;

extern S32 g_needuvincr_slickshield;

extern S32 cnt_alerthokey__11zNPCFodBzzt;

extern S32 g_needMusician;

extern F32 tmr_nexthokey__11zNPCFodBzzt;

extern RwRaster* rast_discoLight__11zNPCFodBzzt;

zNPCSlick* g_slick_slipfx_owner;

U32 g_hash_roboanim[41] = { 0 };

char* g_strz_roboanim[41] = {
    "Unknown",        "Idle01",          "Fidget01",       "Move01",       "Notice01",
    "Taunt01",        "Respawn01",       "LassoGrab01",    "LassoHold01",  "StunBegin01",
    "StunLoop01",     "EndTag_Standard", "AlertIdle01",    "AlertMove01",  "Attack01",
    "Attack02",       "Attack03",        "AttackBegin01",  "AttackLoop01", "AttackEnd01",
    "HurtKnock01",    "HurtSmash01",     "HurtBash01",     "LassoYank01",  "PatPickup01",
    "PatCarry01",     "PatThrowBegin01", "PatThrowLoop01", "Sleep01",      "TeleportBegin01",
    "TeleportLoop01", "TeleportEnd01",   "Launch01",       "LaunchEnd01",  "DanceBegin01",
    "DanceLoop01",    "DanceEnd01",      "Death01",        "Death02",      "DodgeBBowl01",
    "DodgeBCruise01"
};

U32 g_hash_ttsanim[2] = { 0, 0 };

char* g_strz_ttsanim[2] = { "Unknown", "TarTar_Slosh01" };

U32 g_hash_cloudanim[3] = { 0, 0, 0 };

char* g_strz_cloudanim[3] = { "Unknown", "Cloud_Idle01", "Cloud_Attack01" };

U32 g_hash_nytlytanim[2] = { 0, 0 };

char* g_strz_nytlytanim[2] = { "Unknown", "Light_Idle01" };

U32 g_hash_flotanim[2] = { 0, 0 };

char* g_strz_flotanim[2] = { "Unknown", "Wiggle01" };

U32 g_hash_shieldanim[2] = { 0, 0 };

char* g_strz_shieldanim[2] = { "Unknown", "Shield_Idle01" };

void zNPCFodBzzt_DoTheHokeyPokey(F32 dt);
void ZNPC_Destroy_Robot(xFactoryInst* inst);
void ZNPC_AnimTable_RobotBase(xAnimTable*);
NPARMgmt* NPAR_PartySetup(en_nparptyp parType, void** userData, NPARXtraData* xtraData);

void PlayTheFiddle();
void zNPCSleepy_Timestep(F32 dt);
S32 DUMY_grul_returnToIdle(xGoal* goal, void*, en_trantype* trantype, F32, void*);
S32 FODR_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*);
S32 BOMB_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*);
S32 BZZT_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*);
S32 CHMP_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*);
S32 HAMR_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*);
S32 TART_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*);
S32 GLOV_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*);
S32 MOON_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*);
S32 SLEP_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*);
S32 ARFY_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*);
S32 PUPY_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*);
S32 CHUK_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*);
S32 TUBE_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*);
S32 SLCK_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*);
void ROBO_KillEffects();

void ZNPC_Robot_Startup()
{
    for (S32 i = 0; i < 41; i++)
    {
        g_hash_roboanim[i] = xStrHash(g_strz_roboanim[i]);
    }

    for (S32 i = 0; i < 2; i++)
    {
        g_hash_ttsanim[i] = xStrHash(g_strz_ttsanim[i]);
    }

    for (S32 i = 0; i < 3; i++)
    {
        g_hash_cloudanim[i] = xStrHash(g_strz_cloudanim[i]);
    }

    for (S32 i = 0; i < 2; i++)
    {
        g_hash_nytlytanim[i] = xStrHash(g_strz_nytlytanim[i]);
    }

    for (S32 i = 0; i < 2; i++)
    {
        g_hash_flotanim[i] = xStrHash(g_strz_flotanim[i]);
    }

    for (S32 i = 0; i < 2; i++)
    {
        g_hash_shieldanim[i] = xStrHash(g_strz_shieldanim[i]);
    }

    PlayTheFiddle();
}

void PlayTheFiddle()
{
}

void ZNPC_Robot_Shutdown()
{
}

void zNPCRobot_ScenePrepare()
{
    g_cnt_fodbzzt = 0;
    g_cnt_sleepy = 0;
    g_needuvincr_tube = 0;
    g_needuvincr_bzzt = 0;
    g_needuvincr_nightlight = 0;
    g_needuvincr_slickshield = 0;
    g_uvaShield.Clear();
}

void zNPCRobot_SceneFinish()
{
    ROBO_KillEffects();
    g_uvaShield.Hemorrage();
}

void zNPCRobot_SceneReset()
{
    zNPCFodBzzt_ResetDanceParty();
}

void zNPCRobot_ScenePostInit()
{
    ROBO_InitEffects();
    ROBO_PrepRoboCop();
}

void zNPCRobot_Timestep(xScene* sc, float dt)
{
    if (g_cnt_fodbzzt)
    {
        zNPCFodBzzt_DoTheHokeyPokey(dt);
    }
    if (g_cnt_sleepy)
    {
        zNPCSleepy_Timestep(dt);
    }

    g_needuvincr_tube = 1;
    g_needuvincr_bzzt = 1;
    g_needuvincr_nightlight = 1;
    g_needuvincr_slickshield = 1;
}

xFactoryInst* ZNPC_Create_Robot(S32 who, RyzMemGrow* grow, void*)
{
    zNPCRobot* robo;

    switch (who)
    {
    case NPC_TYPE_ROBOT:
    {
        robo = new (who, grow) zNPCRobot(who);
        break;
    }
    case NPC_TYPE_FODDER:
    {
        robo = new (who, grow) zNPCFodder(who);
        break;
    }
    case NPC_TYPE_FODBOMB:
    {
        robo = new (who, grow) zNPCFodBomb(who);
        break;
    }
    case NPC_TYPE_FODBZZT:
    {
        robo = new (who, grow) zNPCFodBzzt(who);
        break;
    }
    case NPC_TYPE_CHOMPER:
    {
        robo = new (who, grow) zNPCChomper(who);
        break;
    }
    case NPC_TYPE_CRITTER:
    {
        robo = new (who, grow) zNPCCritter(who);
        break;
    }
    case NPC_TYPE_HAMMER:
    case NPC_TYPE_HAMSPIN:
    {
        robo = new (who, grow) zNPCHammer(who);
        break;
    }
    case NPC_TYPE_TARTAR:
    {
        robo = new (who, grow) zNPCTarTar(who);
        break;
    }
    case NPC_TYPE_GLOVE:
    {
        robo = new (who, grow) zNPCGlove(who);
        break;
    }
    case NPC_TYPE_MONSOON:
    {
        robo = new (who, grow) zNPCMonsoon(who);
        break;
    }
    case NPC_TYPE_SLEEPY:
    {
        robo = new (who, grow) zNPCSleepy(who);
        break;
    }
    case NPC_TYPE_ARFDOG:
    {
        robo = new (who, grow) zNPCArfDog(who);
        break;
    }
    case NPC_TYPE_ARFARF:
    {
        robo = new (who, grow) zNPCArfArf(who);
        break;
    }
    case NPC_TYPE_CHUCK:
    {
        robo = new (who, grow) zNPCChuck(who);
        break;
    }
    case NPC_TYPE_TUBELET:
    {
        robo = new (who, grow) zNPCTubelet(who);
        break;
    }
    case NPC_TYPE_TUBESLAVE:
    {
        robo = new (who, grow) zNPCTubeSlave(who);
        break;
    }
    case NPC_TYPE_SLICK:
    {
        robo = new (who, grow) zNPCSlick(who);
        break;
    }
    default:
    {
        robo = new (who, grow) zNPCRobot(who);
        break;
    }
    }

    return robo;
}

void ZNPC_Destroy_Robot(xFactoryInst* inst)
{
    delete inst;
}

void ZNPC_AnimTable_RobotBase(xAnimTable* table)
{
    int ourAnims[17] = { 0x01, 0x03, 0x02, 0x04, 0x05, 0x07, 0x08, 0x17, 0x09,
                         0x0a, 0x14, 0x16, 0x15, 0x25, 0x26, 0x06, 0x00 };

    xAnimTableNewState(table, g_strz_roboanim[0x01], 0x110, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_roboanim[0x03], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_roboanim[0x02], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_roboanim[0x04], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_roboanim[0x05], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_roboanim[0x14], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_roboanim[0x16], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_roboanim[0x15], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_roboanim[0x25], 0x00, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_roboanim[0x26], 0x00, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_roboanim[0x06], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_roboanim[0x07], 0x020, 0x2000000, 1.0f, NULL, NULL, 0.0f, NULL,
                       NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_roboanim[0x08], 0x010, 0x2000000, 1.0f, NULL, NULL, 0.0f, NULL,
                       NULL, xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_roboanim[0x17], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_roboanim[0x09], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_roboanim[0x0a], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    NPCC_BuildStandardAnimTran(table, g_strz_roboanim, ourAnims, 1, 0.2f);

    xAnimTableNewTransition(table, g_strz_roboanim[0x07], g_strz_roboanim[0x08], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, g_strz_roboanim[0x09], g_strz_roboanim[0x0a], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, g_strz_roboanim[0x14], g_strz_roboanim[0x25], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, g_strz_roboanim[0x16], g_strz_roboanim[0x25], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(table, g_strz_roboanim[0x15], g_strz_roboanim[0x25], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);
}

xAnimTable* ZNPC_AnimTable_Fodder()
{
    int ourAnims[16] = { 0x01, 0x03, 0x02, 0x04, 0x05, 0x22, 0x23, 0x24,
                         0x0e, 0x14, 0x16, 0x15, 0x25, 0x26, 0x06, 0x00 };

    xAnimTable* pxVar1 = (xAnimTable*)xAnimTableNew("zNPCFodder", NULL, 0);

    xAnimTableNewState(pxVar1, g_strz_roboanim[0x01], 0x110, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x03], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x02], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x04], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x05], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x22], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x23], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x24], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x0e], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x14], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x16], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x15], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x25], 0x000, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x26], 0x000, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x06], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    NPCC_BuildStandardAnimTran(pxVar1, g_strz_roboanim, ourAnims, 1, 0.2f);

    xAnimTableNewTransition(pxVar1, g_strz_roboanim[0x14], g_strz_roboanim[0x25], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(pxVar1, g_strz_roboanim[0x16], g_strz_roboanim[0x25], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(pxVar1, g_strz_roboanim[0x15], g_strz_roboanim[0x25], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(pxVar1, g_strz_roboanim[0x22], g_strz_roboanim[0x23], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);

    return pxVar1;
}

xAnimTable* ZNPC_AnimTable_Hammer()
{
    int ourAnims[6] = {
        0x0e, 0x18, 0x19, 0x1a, 0x1b, 0x00,
    };

    xAnimTable* pxVar1 = xAnimTableNew("zNPCHammer", NULL, 0);
    ZNPC_AnimTable_RobotBase(pxVar1);

    xAnimTableNewState(pxVar1, g_strz_roboanim[0x0e], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x18], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x19], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x1a], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x1b], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    NPCC_BuildStandardAnimTran(pxVar1, g_strz_roboanim, ourAnims, 1, 0.2f);

    xAnimTableNewTransition(pxVar1, g_strz_roboanim[0x18], g_strz_roboanim[0x19], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(pxVar1, g_strz_roboanim[0x1a], g_strz_roboanim[0x1b], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);

    return pxVar1;
}

xAnimTable* ZNPC_AnimTable_TarTar()
{
    int ourAnims[4] = { 0x0c, 0x11, 0x0e, 0x00 };

    xAnimTable* pxVar1 = xAnimTableNew("zNPCTarTar", NULL, 0);
    ZNPC_AnimTable_RobotBase(pxVar1);

    xAnimTableNewState(pxVar1, g_strz_roboanim[0x0c], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x11], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x0e], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    NPCC_BuildStandardAnimTran(pxVar1, g_strz_roboanim, ourAnims, 1, 0.2f);

    xAnimTableNewTransition(pxVar1, g_strz_roboanim[0x11], g_strz_roboanim[0x0e], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(pxVar1, g_strz_roboanim[0x0e], g_strz_roboanim[0x0c], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(pxVar1, g_strz_roboanim[0x04], g_strz_roboanim[0x0c], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);

    return pxVar1;
}

xAnimTable* ZNPC_AnimTable_TTSauce()
{
    xAnimTable* pxVar1 = (xAnimTable*)xAnimTableNew("TarTarSauce", NULL, 0);

    xAnimTableNewState(pxVar1, g_strz_ttsanim[1], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    return pxVar1;
}

xAnimTable* ZNPC_AnimTable_GLove()
{
    int ourAnims[8] = {
        0x18, 0x19, 0x1a, 0x1b, 0x11, 0x12, 0x13, 0x00,
    };

    xAnimTable* pxVar1 = (xAnimTable*)xAnimTableNew("zNPCGlove", NULL, 0);
    ZNPC_AnimTable_RobotBase(pxVar1);

    xAnimTableNewState(pxVar1, g_strz_roboanim[0x11], 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x12], 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x13], 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x18], 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x19], 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x1a], 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x1b], 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    NPCC_BuildStandardAnimTran(pxVar1, g_strz_roboanim, ourAnims, 1, 0.2f);

    xAnimTableNewTransition(pxVar1, g_strz_roboanim[0x1a], g_strz_roboanim[0x1b], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(pxVar1, g_strz_roboanim[0x11], g_strz_roboanim[0x12], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(pxVar1, g_strz_roboanim[0x18], g_strz_roboanim[0x19], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);

    return pxVar1;
}

xAnimTable* ZNPC_AnimTable_Monsoon()
{
    int ourAnims[5] = { 0x0c, 0x0d, 0x11, 0x12, 0x00 };

    xAnimTable* pxVar1 = xAnimTableNew("zNPCMonsoon", NULL, 0);
    ZNPC_AnimTable_RobotBase(pxVar1);

    xAnimTableNewState(pxVar1, g_strz_roboanim[0x0c], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x0d], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x11], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x12], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    NPCC_BuildStandardAnimTran(pxVar1, g_strz_roboanim, ourAnims, 1, 0.2f);

    xAnimTableNewTransition(pxVar1, g_strz_roboanim[0x11], g_strz_roboanim[0x12], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);

    return pxVar1;
}

xAnimTable* ZNPC_AnimTable_ThunderCloud()
{
    int ourAnims[3] = { 0x01, 0x02, 0x00 };

    xAnimTable* pxVar1 = xAnimTableNew("ThunderCloud", NULL, 0);

    xAnimTableNewState(pxVar1, g_strz_roboanim[0x01], 0x010, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x02], 0x010, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    NPCC_BuildStandardAnimTran(pxVar1, g_strz_roboanim, ourAnims, 1, 0.2f);

    return pxVar1;
}

xAnimTable* ZNPC_AnimTable_NightLight()
{
    int ourAnims[2] = { 0x01, 0x00 };

    xAnimTable* pxVar1 = xAnimTableNew("NightLight", NULL, 0);

    xAnimTableNewState(pxVar1, g_strz_nytlytanim[1], 0x010, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    NPCC_BuildStandardAnimTran(pxVar1, g_strz_nytlytanim, ourAnims, 1, 0.2f);

    return pxVar1;
}

// Regalloc
xAnimTable* ZNPC_AnimTable_SleepyTime()
{
    int ourAnims[3] = { 0x0c, 0x0e, 0x00 };

    xAnimTable* pxVar1 = xAnimTableNew("zNPCSleepy", NULL, 0);
    ZNPC_AnimTable_RobotBase(pxVar1);

    xAnimTableNewState(pxVar1, g_strz_roboanim[0x0c], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x0e], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    NPCC_BuildStandardAnimTran(pxVar1, g_strz_roboanim, ourAnims, 1, 0.2f);

    return pxVar1;
}

xAnimTable* ZNPC_AnimTable_ArfDog()
{
    int ourAnims[17] = { 0x01, 0x03, 0x02, 0x04, 0x05, 0x20, 0x21, 0x0c, 0x0d,
                         0x12, 0x14, 0x16, 0x15, 0x25, 0x26, 0x06, 0x00 };

    xAnimTable* pxVar1 = (xAnimTable*)xAnimTableNew("zNPCArfDog", NULL, 0);

    xAnimTableNewState(pxVar1, g_strz_roboanim[0x01], 0x110, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x03], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x02], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x04], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x05], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x20], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x21], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x0c], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x0d], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x12], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x14], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x16], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x15], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x25], 0x000, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x26], 0x000, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x06], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    NPCC_BuildStandardAnimTran(pxVar1, g_strz_roboanim, ourAnims, 1, 0.2f);

    return pxVar1;
}

xAnimTable* ZNPC_AnimTable_ArfArf()
{
    int ourAnims[7] = { 0x0e, 0x0f, 0x10, 0x1d, 0x1e, 0x1f, 0x00 };

    xAnimTable* pxVar1 = xAnimTableNew("zNPCArfArf", NULL, 0);
    ZNPC_AnimTable_RobotBase(pxVar1);

    xAnimTableNewState(pxVar1, g_strz_roboanim[0x0e], 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x0f], 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x10], 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x1d], 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x1e], 0x00, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x1f], 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    NPCC_BuildStandardAnimTran(pxVar1, g_strz_roboanim, ourAnims, 1, 0.2f);

    xAnimTableNewTransition(pxVar1, g_strz_roboanim[0x1d], g_strz_roboanim[0x1e], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);

    return pxVar1;
}

xAnimTable* ZNPC_AnimTable_Chuck()
{
    int ourAnims[4] = { 0x0c, 0x0d, 0x0e, 0x00 };

    xAnimTable* pxVar1 = xAnimTableNew("zNPCChuck", NULL, 0);
    ZNPC_AnimTable_RobotBase(pxVar1);

    xAnimTableNewState(pxVar1, g_strz_roboanim[0x0c], 0x110, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x0d], 0x110, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x0e], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    NPCC_BuildStandardAnimTran(pxVar1, g_strz_roboanim, ourAnims, 1, 0.2f);

    xAnimTableNewTransition(pxVar1, g_strz_roboanim[0x0e], g_strz_roboanim[0x0c], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(pxVar1, g_strz_roboanim[0x04], g_strz_roboanim[0x0c], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);
    xAnimTableNewTransition(pxVar1, g_strz_roboanim[0x04], g_strz_roboanim[0x0c], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);

    return pxVar1;
}

// Regalloc
xAnimTable* ZNPC_AnimTable_Tubelet()
{
    int ourAnims[2] = { 0x0e, 0x00 };

    xAnimTable* pxVar1 = xAnimTableNew("zNPCTubelet", NULL, 0);
    ZNPC_AnimTable_RobotBase(pxVar1);

    xAnimTableNewState(pxVar1, g_strz_roboanim[0x0e], 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    NPCC_BuildStandardAnimTran(pxVar1, g_strz_roboanim, ourAnims, 1, 0.2f);

    return pxVar1;
}

xAnimTable* ZNPC_AnimTable_FloatDevice()
{
    xAnimTable* pxVar1 = xAnimTableNew("TubeletFloatDevice", NULL, 0);
    xAnimTableNewState(pxVar1, g_strz_flotanim[0x01], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    return pxVar1;
}

xAnimTable* ZNPC_AnimTable_SlickShield()
{
    xAnimTable* pxVar1 = xAnimTableNew("SlickBubbleShield", NULL, 0);
    xAnimTableNewState(pxVar1, g_strz_shieldanim[0x01], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    return pxVar1;
}

xAnimTable* ZNPC_AnimTable_Slick()
{
    int ourAnims[4] = { 0x11, 0x12, 0x13, 0x00 };

    xAnimTable* pxVar1 = xAnimTableNew("zNPCSlick", NULL, 0);

    ZNPC_AnimTable_RobotBase(pxVar1);

    xAnimTableNewState(pxVar1, g_strz_roboanim[0x11], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x12], 0x010, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(pxVar1, g_strz_roboanim[0x13], 0x020, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    NPCC_BuildStandardAnimTran(pxVar1, g_strz_roboanim, ourAnims, 1, 0.2f);

    xAnimTableNewTransition(pxVar1, g_strz_roboanim[0x11], g_strz_roboanim[0x12], NULL, NULL, 0x10,
                            0, 0.0f, 0.0f, 0, 0, 0.2f, NULL);

    return pxVar1;
}

U8 zNPCRobot::ColChkFlags() const
{
    S32 flags = 0x3E;

    if (npcset.reduceCollide)
    {
        flags &= ~0x6;
    }

    return flags;
}

U8 zNPCRobot::ColPenFlags() const
{
    S32 flags = 0x3E;

    if (npcset.reduceCollide)
    {
        flags &= ~0x6;
    }

    return flags;
}

U8 zNPCRobot::PhysicsFlags() const
{
    S32 flags = 0;

    if (flg_move & 0x6)
    {
        flags |= 3;
    }

    if (flg_move & 0x2)
    {
        flags |= 4;
    }

    return flags;
}

void zNPCRobot::Init(xEntAsset* asset)
{
    zNPCCommon::Init(asset);
    this->flg_move = 10;
    this->flg_vuln = -1;
    this->idx_neckBone = -1;
    this->flags1.flg_basenpc |= 8;
}

void zNPCRobot::Reset()
{
    NPCConfig* conf = cfg_npc;

    if (conf->dst_castShadow < 0.0f && !(flg_move & 4))
    {
        conf->dst_castShadow = 1.0f;
    }

    if (conf->rad_shadowCache < 0.0f)
    {
        conf->rad_shadowCache = GenShadCacheRad();
    }

    zNPCCommon::Reset();

    hitpoints = cfg_npc->pts_damage;

    if (PRIV_GetLassoData())
    {
        flg_vuln |= 0x1000000;
    }

    if (xNPCBasic::SelfType() != NPC_TYPE_TUBESLAVE)
    {
        zNPCGoalDead* goal;
        xPsyche* psy = psy_instinct;

        if (npc_duplodude)
        {
            goal = (zNPCGoalDead*)psy->FindGoal('NGRj');
            goal->DieQuietly();
            psy_instinct->GoalSet('NGRj', 1);
        }
        else
        {
            if ((U32)xEntIsEnabled(this) == 0)
            {
                goal = (zNPCGoalDead*)psy->FindGoal('NGRj');
                goal->DieQuietly();
                psy_instinct->GoalSet('NGRj', 1);
            }
            else
            {
                psy_instinct->GoalSet('NGN0', 1);
            }
        }
    }

    S32 rc = arena.NeedToCycle(this);

    if (rc == 2)
    {
        arena.Cycle(this, 1);
    }
    else
    {
        if (rc)
        {
            arena.Cycle(this, 0);
        }
    }
}

F32 zNPCRobot::GenShadCacheRad()
{
    F32 fac_use;
    F32 rad_cache;

    switch (xNPCBasic::SelfType())
    {
    case NPC_TYPE_HAMMER: // 0x4e545230:
    case NPC_TYPE_HAMSPIN: // 0x4e545231:
    case NPC_TYPE_TARTAR: // 0x4e545232:
    case NPC_TYPE_GLOVE: // 0x4e545233:
    case NPC_TYPE_MONSOON: // 0x4e545234:
    case NPC_TYPE_SLEEPY: // 0x4e545235:
    case NPC_TYPE_ARFARF: // 0x4e545237:
    case NPC_TYPE_TUBELET: // 0x4e545239:
    case NPC_TYPE_TUBESLAVE: // 0x4e54523a:
        fac_use = 2.4f;
        break;
    case NPC_TYPE_SLICK:
        fac_use = 2.5f;
        break;
    case NPC_TYPE_ARFDOG: // 0x4e545236:
    case NPC_TYPE_CHUCK: // 0x4e545238:
    case NPC_TYPE_FODDER: // 0x4e54523c:
    case NPC_TYPE_FODBOMB: // 0x4e54523d:
    case NPC_TYPE_FODBZZT: // 0x4e54523e:
    case NPC_TYPE_CHOMPER: // 0x4e54523f:
    case NPC_TYPE_CRITTER: // 0x4e545240:
        fac_use = 1.5f;
        break;
    default:
        fac_use = 2.0f;
        break;
    }
    rad_cache = zNPCCommon::BoundAsRadius(0);
    return (fac_use * rad_cache);
}

void zNPCRobot::ParseINI()
{
    zNPCCommon::ParseINI();
    cfg_npc->snd_traxShare = g_sndTrax_Robot;
    NPCS_SndTablePrepare(g_sndTrax_Robot);
}

void zNPCRobot::Process(xScene* xscn, F32 dt)
{
    psy_instinct->Timestep(dt, NULL);

    if (IsAlive())
    {
        DoAliveStuff(dt);
    }

    zNPCCommon::Process(xscn, dt);
}

void zNPCRobot::NewTime(xScene* xscn, F32 dt)
{
    if (idx_neckBone >= 0 && !IsDying())
    {
        TurnThemHeads();
    }
    zNPCCommon::NewTime(xscn, dt);
}

void zNPCRobot::SelfSetup()
{
    xBehaveMgr* bmgr = xBehaveMgr_GetSelf();
    psy_instinct = bmgr->Subscribe(this, 0);
    xPsyche* psy = psy_instinct;
    xGoal* goal = NULL;

    psy->BrainBegin();
    goal = psy->AddGoal('NGR4', NULL);
    goal->SetCallbacks(DUMY_grul_returnToIdle, NULL, NULL, NULL);
    AddBaseline(psy, NULL, NULL, NULL, NULL, NULL);
    AddStunThrow(psy, NULL, NULL, NULL, NULL);
    AddLassoing(psy, NULL, NULL, NULL, NULL, NULL);
    AddDamage(psy, NULL, NULL, NULL, NULL, NULL);
    AddSpawning(psy, NULL, NULL);
    AddScripting(psy, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    AddMiscTypical(psy, NULL, NULL, NULL);
    psy->BrainEnd();
    psy->SetSafety('NGN0');
}

U32 zNPCRobot::AnimPick(S32 gid, en_NPC_GOAL_SPOT gspot, xGoal* rawgoal)
{
    S32 idx = -1;
    U32 hashid = 0;

    switch (gid)
    {
    case NPC_GOAL_IDLE:
    case NPC_GOAL_WAITING:
    case NPC_GOAL_NOMANLAND:
    case NPC_GOAL_LIMBO:
    case NPC_GOAL_ALERT:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 1;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 1;
        }
        else
        {
            idx = 1;
        }
        break;
    case NPC_GOAL_PATROL:
        if (gspot == NPC_GSPOT_PATROLPAUSE)
        {
            idx = 2;
            break;
        }
    case NPC_GOAL_WANDER:
    case NPC_GOAL_EVADE:
    case NPC_GOAL_GOHOME:
    case NPC_GOAL_CHASE:
        idx = 3;
        break;
    case NPC_GOAL_FIDGET:
        idx = 2;
        break;
    case NPC_GOAL_NOTICE:
        idx = 4;
        break;
    case NPC_GOAL_TAUNT:
        idx = 5;
        break;
    case NPC_GOAL_LASSOBASE:
    case NPC_GOAL_LASSOGRAB:
        idx = 7;
        break;
    case NPC_GOAL_LASSOTHROW:
        idx = 0x17;
        break;
    case NPC_GOAL_EVILPAT:
    case NPC_GOAL_STUNNED:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 9;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 10;
        }
        break;
    case NPC_GOAL_PATCARRY:
    case NPC_GOAL_PATTHROW:
        idx = 10;
        break;
    case NPC_GOAL_WOUND:
        idx = 0x14;
        break;
    case NPC_GOAL_KNOCK:
    {
        S32 pick;

        if (xrand() & 0x800000)
        {
            pick = 0x25;
        }
        else
        {
            pick = 0x26;
        }

        idx = pick;
        break;
    }
    case NPC_GOAL_R56:
        idx = 0x15;
        break;
    case NPC_GOAL_BASHED:
        idx = 0x16;
        break;
    case NPC_GOAL_DAMAGE:
    case NPC_GOAL_R54:
        idx = 0x25;
        break;
    case NPC_GOAL_RESPAWN:
        idx = 6;
        break;
    case NPC_GOAL_AFTERLIFE:
        idx = 1;
        break;
    default:
        xUtil_idtag2string(gid, 0);
        idx = 1;
        break;
    }

    if (idx >= 0)
    {
        hashid = g_hash_roboanim[idx];
    }

    return hashid;
}

void zNPCRobot::DuploOwner(zNPCCommon* duper)
{
    zNPCCommon::DuploOwner(duper);

    xPsyche* psyche = this->psy_instinct;

    if (psyche)
    {
        zNPCGoalDead* dead = (zNPCGoalDead*)psyche->FindGoal('NGRj');
        dead->DieQuietly();
        psyche->GoalSet('NGRj', 1);
    }
}

void zNPCRobot::AddLassoing(xPsyche* psyche, xGoalProcessCallback cb1, xGoalProcessCallback cb2,
                            xGoalProcessCallback cb3, xGoalProcessCallback cb4,
                            xGoalProcessCallback cb5)
{
    if (!(flg_vuln & 0x1000000))
    {
        return;
    }

    xGoal* goal;
    goal = psyche->AddGoal(NPC_GOAL_LASSOBASE, NULL);
    goal->SetCallbacks(cb1, NULL, NULL, NULL);

    goal = psyche->AddGoal(NPC_GOAL_LASSOGRAB, NULL);
    goal->SetCallbacks(cb2, NULL, NULL, NULL);

    goal = psyche->AddGoal(NPC_GOAL_LASSOTHROW, NULL);
    goal->SetCallbacks(cb3, NULL, NULL, NULL);
}

void zNPCRobot::AddMiscTypical(xPsyche* psyche, xGoalProcessCallback cb1, xGoalProcessCallback cb2,
                               xGoalProcessCallback cb3)
{
    xGoal* goal;

    goal = psyche->AddGoal(NPC_GOAL_NOTICE, NULL);
    goal->SetCallbacks(cb1, NULL, NULL, NULL);

    goal = psyche->AddGoal(NPC_GOAL_TAUNT, NULL);
    goal->SetCallbacks(cb2, NULL, NULL, NULL);

    goal = psyche->AddGoal(NPC_GOAL_NOMANLAND, NULL);
    goal->SetCallbacks(cb3, NULL, NULL, NULL);
}

void zNPCRobot::AddStunThrow(xPsyche* psyche, xGoalProcessCallback cb1, xGoalProcessCallback cb2,
                             xGoalProcessCallback cb3, xGoalProcessCallback cb4)
{
    xGoal* goal;

    goal = psyche->AddGoal(NPC_GOAL_EVILPAT, NULL);
    goal->SetCallbacks(cb1, NULL, NULL, NULL);

    goal = psyche->AddGoal(NPC_GOAL_STUNNED, NULL);
    goal->SetCallbacks(cb2, NULL, NULL, NULL);

    if (cfg_npc->pts_damage < 2)
    {
        goal = psyche->AddGoal(NPC_GOAL_PATCARRY, NULL);
        goal->SetCallbacks(cb3, NULL, NULL, NULL);

        goal = psyche->AddGoal(NPC_GOAL_PATTHROW, NULL);
        goal->SetCallbacks(cb4, NULL, NULL, NULL);
    }
}

void zNPCRobot::AddDamage(xPsyche* psyche, xGoalProcessCallback cb1, xGoalProcessCallback cb2,
                          xGoalProcessCallback cb3, xGoalProcessCallback cb4,
                          xGoalProcessCallback cb5)
{
    xGoal* goal;

    goal = psyche->AddGoal(NPC_GOAL_DAMAGE, NULL);
    goal->SetCallbacks(cb1, NULL, NULL, NULL);

    goal = psyche->AddGoal(NPC_GOAL_KNOCK, NULL);
    goal->SetCallbacks(cb2, NULL, NULL, NULL);

    goal = psyche->AddGoal(NPC_GOAL_BASHED, NULL);
    goal->SetCallbacks(cb3, NULL, NULL, NULL);
}

void zNPCRobot::AddSpawning(xPsyche* psyche, xGoalProcessCallback cb1, xGoalProcessCallback cb2)
{
    xGoal* goal;

    goal = psyche->AddGoal(NPC_GOAL_AFTERLIFE, NULL);
    goal->SetCallbacks(cb1, NULL, NULL, NULL);

    goal = psyche->AddGoal(NPC_GOAL_RESPAWN, NULL);
    goal->SetCallbacks(cb2, NULL, NULL, NULL);
}

S32 zNPCRobot::LassoSetup()
{
    S32 param1 = -1;
    S32 param2 = -1;

    LassoModelIndex(&param1, &param2);
    if ((param1 >= 0) && (param2 >= 0))
    {
        LassoUseGuides(param1, param2);
    }
    return zNPCCommon::LassoSetup();
}

S32 zNPCRobot::IsDying()
{
    S32 dying = 0;
    xGoal* goal;

    if (psy_instinct == NULL)
    {
        return 0;
    }

    goal = psy_instinct->GetCurGoal();

    if (goal == NULL)
    {
        return 0;
    }

    switch (goal->GetID())
    {
    case NPC_GOAL_DAMAGE:
    case NPC_GOAL_KNOCK:
    case NPC_GOAL_R54:
    case NPC_GOAL_BASHED:
    case NPC_GOAL_R56:
        dying = 2;
        break;
    case NPC_GOAL_LIMBO:
    case NPC_GOAL_AFTERLIFE:
        dying = 1;
        break;
    }

    return dying;
}

S32 zNPCRobot::IsWounded()
{
    return IsDead() ? 0 : cfg_npc->pts_damage - hitpoints;
}

S32 zNPCRobot::SetCarryState(en_NPC_CARRY_STATE stat)
{
    S32 rc = 0;

    if (IsDead())
    {
        return 0;
    }

    xGoal* goal = psy_instinct->GetCurGoal();

    if (goal == NULL)
    {
        return 0;
    }

    switch (stat)
    {
    case zNPCCARRY_NONE:
        if (goal->GetID() == NPC_GOAL_PATTHROW)
        {
            psy_instinct->GoalSet(NPC_GOAL_DAMAGE, 0);
        }
        else
        {
            psy_instinct->GoalSet(NPC_GOAL_DAMAGE, 0);
        }
        break;
    case zNPCCARRY_PICKUP:
        if (goal->GetID() == NPC_GOAL_STUNNED)
        {
            psy_instinct->GoalSwap(NPC_GOAL_PATCARRY, 0);
            rc = 1;
        }
        break;
    case zNPCCARRY_THROW:
        if (goal->GetID() == NPC_GOAL_PATCARRY)
        {
            psy_instinct->GoalSwap(NPC_GOAL_PATTHROW, 0);
        }
        break;
    case zNPCCARRY_ATTEMPTPICKUP:
        if ((flg_vuln & 0x20000000) && goal->GetID() == NPC_GOAL_STUNNED)
        {
            rc = 1;
        }
        break;
    }

    return rc;
}

void zNPCRobot::LassoNotify(en_LASSO_EVENT event)
{
    if (!IsDead())
    {
        zNPCCommon::LassoNotify(event);
        switch (event)
        {
        case LASS_EVNT_GRABSTART:
            psy_instinct->GoalSet(0x4e47525d, 0); // NPC_GOAL_LASSOGRAB??
            break;
        }
    }
}

void zNPCRobot::InflictPain(S32 numHitPoints, S32 giveCreditToPlayer)
{
    if (numHitPoints < 0)
    {
        hitpoints = 0;
    }
    else
    {
        hitpoints -= numHitPoints;
    }

    hitpoints = (hitpoints > 0) ? hitpoints : 0;

    if (!hitpoints && giveCreditToPlayer)
    {
        zNPCCommon::GiveReward();
    }
}

void zNPCRobot::ShowerConfetti(xVec3* pos)
{
    xVec3 pos_use = *(pos ? pos : xEntGetCenter(this));

    zNPCRobot_TubeConfetti(&pos_use);
}

void zNPCFodder::Init(xEntAsset* asset)
{
    zNPCRobot::Init(asset);
    flg_move |= 0x10;
    flg_vuln &= 0x9effffff;
    idx_neckBone = 3;
}

void zNPCFodder::ParseINI()
{
    zNPCRobot::ParseINI();
    cfg_npc->snd_trax = g_sndTrax_Fodder;
    NPCS_SndTablePrepare(g_sndTrax_Fodder);
}

void zNPCFodder::SelfSetup()
{
    zNPCRobot::SelfSetup();

    xPsyche* psy = psy_instinct;
    xGoal* goal;

    goal = psy->FindGoal(NPC_GOAL_IDLE);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_FIDGET);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WAITING);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_PATROL);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WANDER);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_ALERT);
    goal->SetCallbacks(NULL, FODR_grul_alert, NULL, NULL);

    psy->BrainExtend();
    psy->AddGoal(NPC_GOAL_ALERTFODDER, NULL);
    psy->AddGoal(NPC_GOAL_ATTACKFODDER, NULL);
    psy->BrainEnd();

    psy->SetSafety(NPC_GOAL_IDLE);
}

U32 zNPCFodder::AnimPick(S32 gid, en_NPC_GOAL_SPOT gspot, xGoal* rawgoal)
{
    U32 hashid = 0;
    S32 idx = -1;

    switch (gid)
    {
    case NPC_GOAL_IDLE:
    case NPC_GOAL_ALERT:
        idx = 1;
        break;
    case NPC_GOAL_NOTICE:
        idx = 4;
        break;
    case NPC_GOAL_ALERTFODDER:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 1;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 1;
        }
        else if (gspot == NPC_GSPOT_STARTALT)
        {
            idx = 3;
        }
        else
        {
            idx = 1;
        }
        break;
    case NPC_GOAL_ATTACKFODDER:
        idx = 0xe;
        break;
    default:
        hashid = zNPCRobot::AnimPick(gid, gspot, rawgoal);
        break;
    }

    if (idx >= 0)
    {
        hashid = g_hash_roboanim[idx];
    }

    return hashid;
}

/*
void zNPCFodder::Stun(F32 stuntime)
{
    xVec3 dir_dmg;

    if (this->IsWounded())
    {
        return;
    }

    xVec3* pos = xEntGetPos(&globals.player.ent);
    xVec3* robot_pos = xEntGetPos(this);

    xVec3Sub(&dir_dmg, pos, robot_pos);
    F32 out = xVec3Normalize(&dir_dmg, robot_pos);

    this->Respawn(pos, NULL, NULL);
}
*/

void zNPCFodBzzt_ResetDanceParty()
{
    cnt_alerthokey__11zNPCFodBzzt = 0;
}

void zNPCFodder::Stun(F32 stuntime)
{
    if (!IsWounded())
    {
        xVec3 dir_dmg = { 0.0f, 0.0f, 0.0f };

        xVec3Sub(&dir_dmg, xEntGetPos(this), xEntGetPos(&globals.player.ent));
        xVec3Normalize(&dir_dmg, &dir_dmg);

        Damage(DMGTYP_SIDE, &globals.player.ent, &dir_dmg);
    }
}

void zNPCFodBomb::Init(xEntAsset* asset)
{
    zNPCRobot::Init(asset);
    flg_move |= 0x10;
    flg_vuln &= 0x9effffff;
    idx_neckBone = 3;
    rast_blink = 0;
}

void zNPCFodBomb::Setup()
{
    zNPCCommon::Setup();
    if (rast_blink == NULL)
    {
        rast_blink = NPCC_FindRWRaster("fx_fodbomb_blinker");
    }
}

void zNPCFodBomb::ParseINI()
{
    zNPCRobot::ParseINI();
    cfg_npc->snd_trax = g_sndTrax_FodBomb;
    NPCS_SndTablePrepare(g_sndTrax_FodBomb);
}

void zNPCFodBomb::SelfSetup()
{
    flg_vuln &= 0xfeffffff;

    zNPCRobot::SelfSetup();

    xPsyche* psy = psy_instinct;
    xGoal* goal;

    goal = psy->FindGoal(NPC_GOAL_IDLE);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_FIDGET);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WAITING);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_PATROL);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WANDER);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_ALERT);
    goal->SetCallbacks(NULL, BOMB_grul_alert, NULL, NULL);

    psy->BrainExtend();
    psy->AddGoal(NPC_GOAL_ALERTFODBOMB, NULL);
    psy->BrainEnd();

    psy->SetSafety(NPC_GOAL_IDLE);
}

void zNPCFodBomb::Stun(F32 stuntime)
{
    if (!IsWounded())
    {
        xVec3 dir_dmg = { 0.0f, 0.0f, 0.0f };

        xVec3Sub(&dir_dmg, xEntGetPos(this), xEntGetPos(&globals.player.ent));
        xVec3Normalize(&dir_dmg, &dir_dmg);

        Damage(DMGTYP_SIDE, &globals.player.ent, &dir_dmg);
    }
}

U32 zNPCFodBomb::AnimPick(S32 gid, en_NPC_GOAL_SPOT gspot, xGoal* rawgoal)
{
    U32 hashid = 0;
    S32 idx = -1;

    switch (gid)
    {
    case NPC_GOAL_ALERT:
    case NPC_GOAL_ALERTFODBOMB:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 1;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 1;
        }
        else if (gspot == NPC_GSPOT_STARTALT)
        {
            idx = 3;
        }
        else
        {
            idx = 1;
        }
        break;
    default:
        hashid = zNPCRobot::AnimPick(gid, gspot, rawgoal);
        break;
    }

    if (idx >= 0)
    {
        hashid = g_hash_roboanim[idx];
    }

    return hashid;
}

void zNPCFodBomb::BlinkerReset()
{
    blinker.Reset();
    flg_xtrarend &= ~1;
}

void zNPCFodBomb::BlinkerUpdate(F32 dt, F32 pct_timeRemain)
{
    blinker.Update(dt, 1.0f - pct_timeRemain, 0.5f, 0.1f);
    flg_xtrarend |= 1;
}

void zNPCFodBzzt::Init(xEntAsset* asset)
{
    zNPCRobot::Init(asset);

    flg_move &= 0xfffffffd;
    flg_move |= 4;

    flg_vuln &= 0x9effffff;

    idx_neckBone = -1;

    laser.Prepare();

    g_cnt_fodbzzt++;

    tmr_hokeypokey = -1.0f;
    g_needMusician = 0;
    tmr_nexthokey__11zNPCFodBzzt = -1.0f;
    rast_discoLight__11zNPCFodBzzt = NULL;
}

void zNPCFodBzzt::ParseINI()
{
    zNPCRobot::ParseINI();
    cfg_npc->snd_trax = g_sndTrax_FodBzzt;
    NPCS_SndTablePrepare(g_sndTrax_FodBzzt);
}

void zNPCFodBzzt::Reset()
{
    zNPCRobot::Reset();
}

void zNPCFodBzzt::SelfSetup()
{
    zNPCRobot::SelfSetup();

    xPsyche* psy = psy_instinct;
    xGoal* goal;

    goal = psy->FindGoal(NPC_GOAL_IDLE);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_FIDGET);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WAITING);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_PATROL);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WANDER);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_ALERT);
    goal->SetCallbacks(NULL, BZZT_grul_alert, NULL, NULL);

    psy->BrainExtend();
    psy->AddGoal(NPC_GOAL_ALERTFODBZZT, NULL);
    psy->AddGoal(NPC_GOAL_HOKEYPOKEY, NULL);
    psy->BrainEnd();

    psy->SetSafety(NPC_GOAL_IDLE);
}

void zNPCFodBzzt::Process(xScene* sc, F32 dt)
{
    zNPCRobot::Process(sc, dt);
    if (g_needuvincr_bzzt != 0)
    {
        g_needuvincr_bzzt = 0;
        laser.UVScrollUpdate(dt);
    }
}

void zNPCFodBzzt::Stun(F32 stuntime)
{
    if (!IsWounded())
    {
        xVec3 dir_dmg = { 0.0f, 0.0f, 0.0f };

        xVec3Sub(&dir_dmg, xEntGetPos(this), xEntGetPos(&globals.player.ent));
        xVec3Normalize(&dir_dmg, &dir_dmg);

        Damage(DMGTYP_SIDE, &globals.player.ent, &dir_dmg);
    }
}

U32 zNPCFodBzzt::AnimPick(S32 gid, en_NPC_GOAL_SPOT gspot, xGoal* rawgoal)
{
    U32 hashid = 0;
    S32 idx = -1;

    switch (gid)
    {
    case NPC_GOAL_ALERT:
    case NPC_GOAL_ALERTFODBZZT:
        idx = 0xe;
        break;
    case NPC_GOAL_HOKEYPOKEY:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 0x22;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 0x23;
        }
        else if (gspot == NPC_GSPOT_FINISH)
        {
            idx = 0x24;
        }
        else
        {
            idx = 0x23;
        }
        break;
    default:
        hashid = zNPCRobot::AnimPick(gid, gspot, rawgoal);
        break;
    }

    if (idx >= 0)
    {
        hashid = g_hash_roboanim[idx];
    }

    return hashid;
}

void zNPCFodBzzt::DiscoReset()
{
    tmr_discoLight = -1.0f;
}

F32 RANGEWRAP(F32* val, F32 lo, F32 hi)
{
    F32 range = xabs(hi - lo);
    F32 v = *val;

    while (v > hi)
    {
        v -= range;
    }
    while (v < lo)
    {
        v += range;
    }

    *val = v;
    return v;
}

void zNPCChomper::Init(xEntAsset* asset)
{
    zNPCRobot::Init(asset);
    flg_move |= 0x10;
    flg_vuln &= 0x9effffff;
    idx_neckBone = -1;
    NPAR_PartySetup(NPAR_TYP_DOGBREATH, NULL, NULL);
}

void zNPCChomper::ParseINI()
{
    zNPCRobot::ParseINI();
    cfg_npc->snd_trax = g_sndTrax_Chomper;
    NPCS_SndTablePrepare(g_sndTrax_Chomper);
}

void zNPCChomper::SelfSetup()
{
    flg_vuln &= 0xfeffffff;

    zNPCRobot::SelfSetup();

    xPsyche* psy = psy_instinct;
    xGoal* goal;

    goal = psy->FindGoal(NPC_GOAL_IDLE);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_FIDGET);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WAITING);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_PATROL);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WANDER);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_ALERT);
    goal->SetCallbacks(NULL, CHMP_grul_alert, NULL, NULL);

    psy->BrainExtend();
    psy->AddGoal(NPC_GOAL_ALERTCHOMPER, NULL);
    psy->AddGoal(NPC_GOAL_ATTACKCHOMPER, NULL);
    psy->BrainEnd();

    psy->SetSafety(NPC_GOAL_IDLE);
}

void zNPCChomper::Stun(F32 stuntime)
{
    if (!IsWounded())
    {
        xVec3 dir_dmg = { 0.0f, 0.0f, 0.0f };

        xVec3Sub(&dir_dmg, xEntGetPos(this), xEntGetPos(&globals.player.ent));
        xVec3Normalize(&dir_dmg, &dir_dmg);

        Damage(DMGTYP_SIDE, &globals.player.ent, &dir_dmg);
    }
}

void zNPCChomper::Process(xScene* xscn, F32 dt)
{
    if (IsAlive())
    {
        S32 gid = psy_instinct->GIDOfActive();
        if (gid != NPC_GOAL_ATTACKCHOMPER && gid != NPC_GOAL_RESPAWN)
        {
            BreathTrail();
        }
    }

    zNPCRobot::Process(xscn, dt);
}

U32 zNPCChomper::AnimPick(S32 gid, en_NPC_GOAL_SPOT gspot, xGoal* rawgoal)
{
    U32 hashid = 0;
    S32 idx = -1;

    switch (gid)
    {
    case NPC_GOAL_ALERT:
    case NPC_GOAL_ALERTCHOMPER:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 1;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 1;
        }
        else if (gspot == NPC_GSPOT_STARTALT)
        {
            idx = 3;
        }
        else
        {
            idx = 1;
        }
        break;
    case NPC_GOAL_ATTACKCHOMPER:
        idx = 0xe;
        break;
    default:
        hashid = zNPCRobot::AnimPick(gid, gspot, rawgoal);
        break;
    }

    if (idx >= 0)
    {
        hashid = g_hash_roboanim[idx];
    }

    return hashid;
}

void zNPCCritter::Init(xEntAsset* asset)
{
    zNPCRobot::Init(asset);
    flg_vuln = 0;
    idx_neckBone = -1;
}

void zNPCCritter::SelfSetup()
{
    zNPCRobot::SelfSetup();
    psy_instinct->SetSafety(NPC_GOAL_IDLE);
}

void zNPCHammer::Init(xEntAsset* asset)
{
    zNPCRobot::Init(asset);
    flg_vuln |= 0x60000000;
    idx_neckBone = -1;
}

void zNPCHammer::ParseINI()
{
    zNPCRobot::ParseINI();
    cfg_npc->snd_trax = g_sndTrax_Hammer;
    NPCS_SndTablePrepare(g_sndTrax_Hammer);
}

void zNPCHammer::SelfSetup()
{
    zNPCRobot::SelfSetup();

    xPsyche* psy = psy_instinct;
    xGoal* goal;

    goal = psy->FindGoal(NPC_GOAL_IDLE);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_FIDGET);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WAITING);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_PATROL);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WANDER);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_ALERT);
    goal->SetCallbacks(NULL, HAMR_grul_alert, NULL, NULL);

    psy->BrainExtend();
    psy->AddGoal(NPC_GOAL_ALERTHAMMER, NULL);
    psy->AddGoal(NPC_GOAL_ATTACKHAMMER, NULL);
    psy->BrainEnd();

    psy->SetSafety(NPC_GOAL_IDLE);
}

U32 zNPCHammer::AnimPick(S32 gid, en_NPC_GOAL_SPOT gspot, xGoal* rawgoal)
{
    U32 hashid = 0;
    S32 idx = -1;

    switch (gid)
    {
    case NPC_GOAL_ALERT:
    case NPC_GOAL_ALERTHAMMER:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 3;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 3;
        }
        else if (gspot == NPC_GSPOT_STARTALT)
        {
            idx = 3;
        }
        else
        {
            idx = 3;
        }
        break;
    case NPC_GOAL_ATTACKHAMMER:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 0xe;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 0xe;
        }
        else if (gspot == NPC_GSPOT_STARTALT)
        {
            idx = 0x11;
        }
        break;
    case NPC_GOAL_EVILPAT:
    case NPC_GOAL_STUNNED:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 9;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 10;
        }
        break;
    case NPC_GOAL_PATCARRY:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 0x18;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 0x19;
        }
        break;
    case NPC_GOAL_PATTHROW:
        idx = 0x1a;
        break;
    default:
        hashid = zNPCRobot::AnimPick(gid, gspot, rawgoal);
        break;
    }

    if (idx >= 0)
    {
        hashid = g_hash_roboanim[idx];
    }

    return hashid;
}

void zNPCTarTar::Init(xEntAsset* asset)
{
    zNPCRobot::Init(asset);
    flg_vuln &= 0x9fffffff;
    idx_neckBone = -1;
    NPAR_PartySetup(NPAR_TYP_TARTARGUNK, 0, 0);
}

void zNPCTarTar::ParseINI()
{
    zNPCRobot::ParseINI();
    cfg_npc->snd_trax = g_sndTrax_TarTar;
    NPCS_SndTablePrepare(g_sndTrax_TarTar);
}

void zNPCTarTar::SelfSetup()
{
    zNPCRobot::SelfSetup();

    xPsyche* psy = psy_instinct;
    xGoal* goal;

    goal = psy->FindGoal(NPC_GOAL_IDLE);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_FIDGET);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WAITING);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_PATROL);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WANDER);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_ALERT);
    goal->SetCallbacks(NULL, TART_grul_alert, NULL, NULL);

    psy->BrainExtend();
    psy->AddGoal(NPC_GOAL_ALERTTARTAR, NULL);
    psy->AddGoal(NPC_GOAL_ATTACKTARTAR, NULL);
    psy->AddGoal(NPC_GOAL_WOUND, NULL);
    psy->BrainEnd();

    psy->SetSafety(NPC_GOAL_IDLE);
}

U32 zNPCTarTar::AnimPick(S32 gid, en_NPC_GOAL_SPOT gspot, xGoal* rawgoal)
{
    U32 hashid = 0;
    S32 idx = -1;

    switch (gid)
    {
    case NPC_GOAL_ALERT:
    case NPC_GOAL_ALERTTARTAR:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 0xc;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 0xc;
        }
        else if (gspot == NPC_GSPOT_STARTALT)
        {
            idx = 3;
        }
        else
        {
            idx = 0xc;
        }
        break;
    case NPC_GOAL_ATTACKTARTAR:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 0x11;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 0xe;
        }
        break;
    default:
        hashid = zNPCRobot::AnimPick(gid, gspot, rawgoal);
        break;
    }

    if (idx >= 0)
    {
        hashid = g_hash_roboanim[idx];
    }

    return hashid;
}

void zNPCGlove::Init(xEntAsset* asset)
{
    zNPCRobot::Init(asset);
    flg_vuln = -1;
    idx_neckBone = -1;
    NPAR_PartySetup(NPAR_TYP_GLOVEDUST, 0, 0);
}

void zNPCGlove::ParseINI()
{
    NPCConfig* cfg = cfg_npc;
    zNPCRobot::ParseINI();
    cfg->snd_trax = g_sndTrax_Glove;
    NPCS_SndTablePrepare(g_sndTrax_Glove);
}

void zNPCGlove::SelfSetup()
{
    zNPCRobot::SelfSetup();

    xPsyche* psy = psy_instinct;
    xGoal* goal;

    goal = psy->FindGoal(NPC_GOAL_IDLE);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_FIDGET);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WAITING);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_PATROL);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WANDER);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_ALERT);
    goal->SetCallbacks(NULL, GLOV_grul_alert, NULL, NULL);

    psy->BrainExtend();
    psy->AddGoal(NPC_GOAL_ALERTGLOVE, NULL);
    psy->BrainEnd();

    psy->SetSafety(NPC_GOAL_IDLE);
}

U32 zNPCGlove::AnimPick(S32 gid, en_NPC_GOAL_SPOT gspot, xGoal* rawgoal)
{
    U32 hashid = 0;
    S32 idx = -1;

    switch (gid)
    {
    case NPC_GOAL_ALERTGLOVE:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 0x11;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 0x12;
        }
        else
        {
            idx = 0x12;
        }
        break;
    case NPC_GOAL_EVILPAT:
    case NPC_GOAL_STUNNED:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 9;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 10;
        }
        break;
    case NPC_GOAL_PATCARRY:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 0x18;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 0x19;
        }
        break;
    case NPC_GOAL_PATTHROW:
        idx = 0x1a;
        break;
    default:
        hashid = zNPCRobot::AnimPick(gid, gspot, rawgoal);
        break;
    }

    if (idx >= 0)
    {
        hashid = g_hash_roboanim[idx];
    }

    return hashid;
}

void zNPCMonsoon::Init(xEntAsset* asset)
{
    zNPCRobot::Init(asset);
    flg_move &= ~2;
    flg_move |= 4;
    flg_vuln &= 0x9fffffff;
    idx_neckBone = -1;
    NPAR_PartySetup(NPAR_TYP_MONSOONRAIN, NULL, NULL);
}

void zNPCMonsoon::Reset()
{
    zNPCRobot::Reset();
}

void zNPCMonsoon::ParseINI()
{
    NPCConfig* cfg = cfg_npc;
    zNPCRobot::ParseINI();
    cfg->snd_trax = g_sndTrax_Monsoon;
    NPCS_SndTablePrepare(g_sndTrax_Monsoon);
}

void zNPCMonsoon::SelfSetup()
{
    zNPCRobot::SelfSetup();

    xPsyche* psy = psy_instinct;
    xGoal* goal;

    goal = psy->FindGoal(NPC_GOAL_IDLE);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_FIDGET);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WAITING);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_PATROL);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WANDER);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_ALERT);
    goal->SetCallbacks(NULL, MOON_grul_alert, NULL, NULL);

    psy->BrainExtend();
    psy->AddGoal(NPC_GOAL_ALERTMONSOON, NULL);
    psy->AddGoal(NPC_GOAL_ATTACKMONSOON, NULL);
    psy->BrainEnd();

    psy->SetSafety(NPC_GOAL_IDLE);
}

U32 zNPCMonsoon::AnimPick(S32 gid, en_NPC_GOAL_SPOT gspot, xGoal* rawgoal)
{
    U32 hashid = 0;
    S32 idx = -1;

    switch (gid)
    {
    case NPC_GOAL_ALERT:
    case NPC_GOAL_ALERTMONSOON:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 0xc;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 0xc;
        }
        else if (gspot == NPC_GSPOT_STARTALT)
        {
            idx = 0xd;
        }
        else
        {
            idx = 0xc;
        }
        break;
    case NPC_GOAL_ATTACKMONSOON:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 0x11;
        }
        else
        {
            idx = 0x12;
        }
        break;
    default:
        hashid = zNPCRobot::AnimPick(gid, gspot, rawgoal);
        break;
    }

    if (idx >= 0)
    {
        hashid = g_hash_roboanim[idx];
    }

    return hashid;
}

void zNPCMonsoon::Process(xScene* xscn, F32 dt)
{
    zNPCRobot::Process(xscn, dt);

    if (psy_instinct != NULL && psy_instinct->GIDInStack(NPC_GOAL_ALERT))
    {
        FoulWeather(dt);
    }
}

void zNPCMonsoon::NewTime(xScene* sc, F32 dt)
{
    zNPCRobot::NewTime(sc, dt);
}

U8 zNPCMonsoon::FoulWeather(F32)
{
    return 0;
}

// Scheduling
void zNPCSleepy_Timestep(F32 dt)
{
    static S8 init;
    static F32 tmr_cycle;

    if (init == 0)
    {
        tmr_cycle = 0.0f;
        init = 1;
    }

    F32 dVar1 = NPCC_TmrCycle(&tmr_cycle, 0.016666667f, 2.63f);
    zNPCSleepy::hyt_NightLightCurrent = 4.0f;
    zNPCSleepy::hyt_NightLightCurrent += (0.35f * isin(PI * dVar1));
}

void zNPCSleepy::Init(xEntAsset* asset)
{
    zNPCRobot::Init(asset);

    flg_move |= 2;

    flg_vuln &= 0x8fffffff;

    idx_neckBone = -1;

    rast_detectcone = NULL;
    rast_killcone = NULL;

    // Weirdness related to this line.
    g_cnt_sleepy++;

    NPAR_PartySetup(NPAR_TYP_SLEEPYZEEZ, NULL, NULL);
}

void zNPCSleepy::Reset()
{
    zNPCRobot::Reset();

    ModelAtomicShow(0, NULL);
    ModelAtomicHide(1, NULL);
    ModelAtomicShow(4, NULL);

    if (rast_killcone == NULL)
    {
        rast_killcone = NPCC_FindRWRaster("fx_sleepy_beamodeath");
    }

    if (rast_detectcone == NULL)
    {
        rast_detectcone = NPCC_FindRWRaster("fx_sleepy_nightlight");
    }
}

void zNPCSleepy::ParseINI()
{
    static F32 rad_minimum;

    NPCConfig* cfg = cfg_npc;
    zNPCRobot::ParseINI();
    cfg->snd_trax = g_sndTrax_Sleepy;
    NPCS_SndTablePrepare(g_sndTrax_Sleepy);
    // Scheduling issue with init.
    if (init == 0)
    {
        rad_minimum = 5.0f;
        init = 1;
    }
    cfg_npc->rad_detect = MAX(rad_minimum, cfg_npc->rad_detect);
}

void zNPCSleepy::SelfSetup()
{
    zNPCRobot::SelfSetup();

    xPsyche* psy = psy_instinct;
    xGoal* goal;

    goal = psy->FindGoal(NPC_GOAL_IDLE);
    goal->SetCallbacks(SLEP_grul_goAlert, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_FIDGET);
    goal->SetCallbacks(SLEP_grul_goAlert, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WAITING);
    goal->SetCallbacks(SLEP_grul_goAlert, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_PATROL);
    goal->SetCallbacks(SLEP_grul_goAlert, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WANDER);
    goal->SetCallbacks(SLEP_grul_goAlert, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_ALERT);
    goal->SetCallbacks(NULL, SLEP_grul_alert, NULL, NULL);

    psy->BrainExtend();
    psy->AddGoal(NPC_GOAL_ALERTSLEEPY, NULL);
    psy->AddGoal(NPC_GOAL_WOUND, NULL);
    psy->BrainEnd();

    psy->SetSafety(NPC_GOAL_IDLE);
}

U32 zNPCSleepy::AnimPick(int gid, en_NPC_GOAL_SPOT gspot, xGoal* rawgoal)
{
    U32 uVar1 = 0;
    int iVar2 = -1;

    switch (gid)
    {
    case 'NGR4':
        iVar2 = 0xc;
        break;
    case 'NGR=':
        iVar2 = 0xe;
        break;
    default:
        uVar1 = zNPCRobot::AnimPick(gid, gspot, rawgoal);
        break;
    }

    if (iVar2 >= 0)
    {
        uVar1 = g_hash_roboanim[iVar2];
    }

    return uVar1;
}

void zNPCSleepy::NewTime(xScene* sc, F32 dt)
{
    if (!IsDead())
    {
        xModelInstance* nightlight = zNPCCommon::ModelAtomicFind(4, -1, NULL);
        if (nightlight == NULL)
        {
            return;
        }
        NightLightPos((xVec3*)(&nightlight->Mat->pos));
    }
    zNPCRobot::NewTime(sc, dt);
}

void zNPCSleepy::NightLightPos(xVec3* vec)
{
    xVec3Copy(vec, Pos());
    vec->y += hyt_NightLightCurrent;
}

S32 zNPCSleepy::RepelMissile(F32 dt)
{
    tmr_nextPatriot = MAX(-1.0f, tmr_nextPatriot - dt);
    if (haz_patriot != NULL && !(tmr_nextPatriot < 0.0f))
    {
        return 1;
    }
    haz_patriot = NULL;
    return 0;
}

void zNPCArfArf::Init(xEntAsset* asset)
{
    zNPCRobot::Init(asset);
    flg_move |= 2;
    flg_vuln &= ~0x60000000;
    idx_neckBone = -1;
}

void zNPCArfArf::Reset()
{
    zNPCRobot::Reset();
    for (int i = 0; i < sizeof(flg_puppy) / sizeof(S32); i++)
    {
        flg_puppy[i] = 1;
    }
}

void zNPCArfArf::ParseINI()
{
    NPCConfig* cfg = cfg_npc;
    zNPCRobot::ParseINI();
    cfg->snd_trax = g_sndTrax_ArfArf;
    NPCS_SndTablePrepare(g_sndTrax_ArfArf);
}

void zNPCArfArf::ParseLinks()
{
    zNPCCommon::ParseLinks();

    for (S32 i = 0; i < linkCount; i++)
    {
        xLinkAsset* lnk = &link[i];

        if (lnk->dstEvent == eEventConnectToChild)
        {
            xSceneID2Name(globals.sceneCur, id);
            xSceneID2Name(globals.sceneCur, lnk->dstAssetID);

            xBase* child = zSceneFindObject(lnk->dstAssetID);

            if (child != NULL &&
                (child->baseType == eBaseTypeNPC || child->baseType == eBaseTypeGroup))
            {
                ParseChild(child);
            }
        }
    }
}

void zNPCArfArf::ParseChild(xBase* child)
{
    if (child->baseType == eBaseTypeNPC)
    {
        if (((xNPCBasic*)child)->SelfType() == NPC_TYPE_ARFDOG)
        {
            S32 i;

            for (i = 0; i < 5; i++)
            {
                if (pup_kennel[i] == NULL)
                {
                    pup_kennel[i] = (zNPCArfDog*)child;
                    ((zNPCCommon*)child)->DuploOwner(this);
                    break;
                }
            }

            xSceneID2Name(globals.sceneCur, id);
            xSceneID2Name(globals.sceneCur, child->id);
        }
        else
        {
            ((xNPCBasic*)child)->SelfType();
        }
    }
    else if (child->baseType == eBaseTypeGroup)
    {
        xSceneID2Name(globals.sceneCur, id);

        U32 cnt = xGroupGetCount((xGroup*)child);

        for (S32 i = 0; i < (S32)cnt; i++)
        {
            xBase* item = xGroupGetItemPtr((xGroup*)child, i);

            if (item != NULL)
            {
                if (item->baseType == eBaseTypeNPC)
                {
                    ParseChild(item);
                }
                else if (item->baseType == eBaseTypeGroup)
                {
                    ParseChild(item);
                }
            }
        }
    }
}

void zNPCArfArf::SelfSetup()
{
    zNPCRobot::SelfSetup();

    xPsyche* psy = psy_instinct;
    xGoal* goal;

    goal = psy->FindGoal(NPC_GOAL_IDLE);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_FIDGET);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WAITING);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_PATROL);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WANDER);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_ALERT);
    goal->SetCallbacks(NULL, ARFY_grul_alert, NULL, NULL);

    psy->BrainExtend();
    psy->AddGoal(NPC_GOAL_ALERTARF, NULL);
    psy->AddGoal(NPC_GOAL_ATTACKARF, NULL);
    psy->AddGoal(NPC_GOAL_ATTACKARFMELEE, NULL);
    psy->AddGoal(NPC_GOAL_TELEPORT, NULL);
    psy->AddGoal(NPC_GOAL_WOUND, NULL);
    psy->BrainEnd();

    psy->SetSafety(NPC_GOAL_IDLE);
}

void zNPCArfArf::DuploNotice(en_SM_NOTICES notice, void* data)
{
    S32 i;

    switch (notice)
    {
    case SM_NOTE_NPCALIVE:
        for (i = 0; i < 5; i++)
        {
            if (pup_kennel[i] == (zNPCArfDog*)data)
            {
                flg_puppy[i] &= ~1;
                return;
            }
        }
        break;
    case SM_NOTE_NPCSTANDBY:
        for (i = 0; i < 5; i++)
        {
            if (pup_kennel[i] == (zNPCArfDog*)data)
            {
                flg_puppy[i] |= 1;
                return;
            }
        }
        break;
    }
}

zNPCArfDog* zNPCArfArf::AdoptADoggie()
{
    zNPCArfDog* pup = NULL;

    for (S32 i = 0; i < 5; i++)
    {
        if (pup_kennel[i] != NULL && (flg_puppy[i] & 1))
        {
            pup = pup_kennel[i];
            break;
        }
    }

    return pup;
}

void zNPCArfDog::Init(xEntAsset* asset)
{
    zNPCRobot::Init(asset);
    flg_move |= 2;
    flg_vuln &= 0x9effffff;
    idx_neckBone = -1;
    rast_blink = 0;
}

void zNPCArfDog::Setup()
{
    zNPCCommon::Setup();
    if (rast_blink == NULL)
    {
        rast_blink = NPCC_FindRWRaster("fx_fodbomb_blinker");
    }
}

void zNPCArfDog::Reset()
{
    zNPCRobot::Reset();
    BlinkReset();
}

void zNPCArfDog::ParseINI()
{
    NPCConfig* cfg = cfg_npc;
    zNPCRobot::ParseINI();
    cfg->snd_trax = g_sndTrax_ArfDog;
    NPCS_SndTablePrepare(g_sndTrax_ArfDog);
}

void zNPCArfDog::SelfSetup()
{
    zNPCRobot::SelfSetup();

    xPsyche* psy = psy_instinct;
    xGoal* goal;

    goal = psy->FindGoal(NPC_GOAL_IDLE);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_FIDGET);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WAITING);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_PATROL);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WANDER);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_ALERT);
    goal->SetCallbacks(NULL, PUPY_grul_alert, NULL, NULL);

    psy->BrainExtend();
    psy->AddGoal(NPC_GOAL_ALERTPUPPY, NULL);
    psy->AddGoal(NPC_GOAL_DOGLAUNCH, NULL);
    psy->AddGoal(NPC_GOAL_DOGBARK, NULL);
    psy->AddGoal(NPC_GOAL_DOGDASH, NULL);
    psy->AddGoal(NPC_GOAL_DOGPOUNCE, NULL);
    psy->BrainEnd();

    psy->SetSafety(NPC_GOAL_IDLE);
}

U32 zNPCArfDog::AnimPick(S32 gid, en_NPC_GOAL_SPOT gspot, xGoal* rawgoal)
{
    U32 hashid = 0;
    S32 idx = -1;

    switch (gid)
    {
    case NPC_GOAL_ALERT:
    case NPC_GOAL_ALERTPUPPY:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 0xc;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 0xc;
        }
        else if (gspot == NPC_GSPOT_STARTALT)
        {
            idx = 0xd;
        }
        else
        {
            idx = 0xc;
        }
        break;
    case NPC_GOAL_DOGLAUNCH:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 0x20;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 0x20;
        }
        else if (gspot == NPC_GSPOT_STARTALT)
        {
            idx = 0x20;
        }
        else if (gspot == NPC_GSPOT_FINISH)
        {
            idx = 0x21;
        }
        else
        {
            idx = 0x20;
        }
        break;
    case NPC_GOAL_DOGBARK:
        idx = 4;
        break;
    case NPC_GOAL_DOGDASH:
        idx = 0xd;
        break;
    case NPC_GOAL_DOGPOUNCE:
        idx = 0x12;
        break;
    default:
        hashid = zNPCRobot::AnimPick(gid, gspot, rawgoal);
        break;
    }

    if (idx >= 0)
    {
        hashid = g_hash_roboanim[idx];
    }

    return hashid;
}

void zNPCArfDog::Stun(F32 stuntime)
{
    if (!IsWounded())
    {
        xVec3 dir_dmg = { 0.0f, 0.0f, 0.0f };

        xVec3Sub(&dir_dmg, xEntGetPos(this), xEntGetPos(&globals.player.ent));
        xVec3Normalize(&dir_dmg, &dir_dmg);

        Damage(DMGTYP_SIDE, &globals.player.ent, &dir_dmg);

        SndPlayRandom(NPC_STYP_OUCH);
    }
}

void zNPCArfDog::BlinkReset()
{
    blinkHead.Reset();
    blinkTail.Reset();
    flg_xtrarend &= ~1;
}

void zNPCArfDog::BlinkUpdate(F32 dt, F32 ratio)
{
    blinkHead.Update(dt, ratio, 0.03f, 0.03f);
    blinkTail.Update(dt, ratio, 0.03f, 0.03f);
    flg_xtrarend |= 1;
}

void zNPCChuck::Init(xEntAsset* asset)
{
    zNPCRobot::Init(asset);
    flg_move &= 0xfffffffd;
    flg_move |= 4;
    flg_vuln &= 0x9fffffff;
    idx_neckBone = -1;
    NPAR_PartySetup(NPAR_TYP_CHUCKSPLASH, 0, 0);
}

void zNPCChuck::Reset()
{
    zNPCRobot::Reset();
    xModelInstance* minst = ModelAtomicHide(1, NULL);
    if (minst != NULL)
    {
        minst->Flags &= 0xfffe;
    }
}

void zNPCChuck::ParseINI()
{
    NPCConfig* cfg = cfg_npc;
    zNPCRobot::ParseINI();
    cfg->snd_trax = g_sndTrax_Chuck;
    NPCS_SndTablePrepare(g_sndTrax_Chuck);
}

void zNPCChuck::SelfSetup()
{
    zNPCRobot::SelfSetup();

    xPsyche* psy = psy_instinct;
    xGoal* goal;

    goal = psy->FindGoal(NPC_GOAL_IDLE);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_FIDGET);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WAITING);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_PATROL);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WANDER);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_ALERT);
    goal->SetCallbacks(NULL, CHUK_grul_alert, NULL, NULL);

    psy->BrainExtend();
    psy->AddGoal(NPC_GOAL_ALERTCHUCK, NULL);
    psy->AddGoal(NPC_GOAL_ATTACKCHUCK, NULL);
    psy->BrainEnd();

    ((zNPCGoalIdle*)psy->FindGoal(NPC_GOAL_IDLE))->flg_idle |= 1;
    ((zNPCGoalWander*)psy->FindGoal(NPC_GOAL_WANDER))->flg_wand |= 1;
    ((zNPCGoalWaiting*)psy->FindGoal(NPC_GOAL_WAITING))->flg_waiting |= 1;

    psy->SetSafety(NPC_GOAL_IDLE);
}

U32 zNPCChuck::AnimPick(S32 gid, en_NPC_GOAL_SPOT gspot, xGoal* rawgoal)
{
    U32 hashid = 0;
    S32 idx = -1;

    switch (gid)
    {
    case NPC_GOAL_ALERTCHUCK:
        if (gspot == NPC_GSPOT_START)
        {
            idx = 0xc;
        }
        else if (gspot == NPC_GSPOT_RESUME)
        {
            idx = 0xc;
        }
        else if (gspot == NPC_GSPOT_STARTALT)
        {
            idx = 0xd;
        }
        else
        {
            idx = 0xc;
        }
        break;
    case NPC_GOAL_ATTACKCHUCK:
        idx = 0xe;
        break;
    case NPC_GOAL_CHASE:
        idx = 0xd;
        break;
    default:
        hashid = zNPCRobot::AnimPick(gid, gspot, rawgoal);
        break;
    }

    if (idx >= 0)
    {
        hashid = g_hash_roboanim[idx];
    }

    return hashid;
}

void zNPCTubelet::Init(xEntAsset* asset)
{
    zNPCRobot::Init(asset);

    flg_move |= 2;
    flg_vuln &= 0x9FFFFFFF;
    idx_neckBone = -1;

    psynote.npc = this;

    NPAR_PartySetup(NPAR_TYP_TUBESPIRAL, NULL, NULL);
    NPAR_PartySetup(NPAR_TYP_TUBECONFETTI, NULL, NULL);

    xModelInstance* iVar1 = zNPCCommon::ModelAtomicFind(1, -1, NULL);

    iVar1->Flags &= 0xffdf;
    iVar1->Flags |= 8;
}

void zNPCTubelet::Reset()
{
    tubestat = TUBE_STAT_BORN;
    zNPCRobot::Reset();
    NPCConfig* cfg = cfg_npc;
    hitpoints = cfg->pts_damage;
    ModelAtomicHide(0, NULL);
    ModelAtomicHide(1, NULL);
    ModelAtomicHide(4, NULL);
}

void zNPCTubelet::ParseINI()
{
    NPCConfig* cfg = cfg_npc;
    zNPCRobot::ParseINI();
    cfg->snd_trax = g_sndTrax_Tubelet;
    NPCS_SndTablePrepare(g_sndTrax_Tubelet);
}

void zNPCTubelet::ParseLinks()
{
    zNPCCommon::ParseLinks();

    for (S32 i = 0; i < linkCount; i++)
    {
        xLinkAsset* lnk = &link[i];

        if (lnk->dstEvent == eEventConnectToChild)
        {
            xSceneID2Name(globals.sceneCur, id);
            xSceneID2Name(globals.sceneCur, lnk->dstAssetID);

            xBase* child = zSceneFindObject(lnk->dstAssetID);

            if (child->baseType == eBaseTypeNPC || child->baseType == eBaseTypeGroup)
            {
                ParseChild(child);
            }
        }
    }
}

void zNPCTubelet::ParseChild(xBase* child)
{
    if (child->baseType == eBaseTypeNPC)
    {
        if (((xNPCBasic*)child)->SelfType() == NPC_TYPE_TUBESLAVE)
        {
            S32 spot = ROBO_TUBE_PETE;

            if (tub_paul == NULL)
            {
                tub_paul = (zNPCTubeSlave*)child;
                spot = ROBO_TUBE_PAUL;
            }
            else if (tub_mary == NULL)
            {
                tub_mary = (zNPCTubeSlave*)child;
                spot = ROBO_TUBE_MARY;
            }

            if (spot != ROBO_TUBE_PETE)
            {
                xSceneID2Name(globals.sceneCur, id);
                xSceneID2Name(globals.sceneCur, child->id);
                ((zNPCTubeSlave*)child)->SetMaster(this, (en_tubespot)spot);
            }
        }
        else
        {
            ((xNPCBasic*)child)->SelfType();
        }
    }
    else if (child->baseType == eBaseTypeGroup)
    {
        xSceneID2Name(globals.sceneCur, id);

        U32 cnt = xGroupGetCount((xGroup*)child);

        for (S32 i = 0; i < (S32)cnt; i++)
        {
            xBase* item = xGroupGetItemPtr((xGroup*)child, i);

            if (item != NULL)
            {
                if (item->baseType == eBaseTypeNPC)
                {
                    ParseChild(item);
                }
                else if (item->baseType == eBaseTypeGroup)
                {
                    ParseChild(item);
                }
            }
        }
    }
}

void zNPCTubelet::SelfSetup()
{
    zNPCRobot::SelfSetup();

    xPsyche* psy = psy_instinct;
    xGoal* goal;

    goal = psy->FindGoal(NPC_GOAL_IDLE);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_FIDGET);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WAITING);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_PATROL);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WANDER);
    goal->SetCallbacks(ROBO_grul_goAlertMelee, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_ALERT);
    goal->SetCallbacks(NULL, TUBE_grul_alert, NULL, NULL);

    psy->SetNotify(&psynote);

    psy->BrainExtend();
    goal = psy->AddGoal(NPC_GOAL_ALERTTUBELET, NULL);
    ((zNPCGoalCommon*)goal)->flg_npcgauto |= 8;
    goal->AddFlags(0x10000);
    psy->AddGoal(NPC_GOAL_DEFLATE, NULL);
    psy->BrainEnd();

    psy->SetSafety(NPC_GOAL_IDLE);
}

U32 zNPCTubelet::AnimPick(S32 gid, en_NPC_GOAL_SPOT gspot, xGoal* rawgoal)
{
    U32 hashid = 0;
    S32 idx = -1;

    switch (gid)
    {
    case NPC_GOAL_ALERTTUBELET:
    case NPC_GOAL_DEFLATE:
        idx = 3;
        break;
    default:
        hashid = zNPCRobot::AnimPick(gid, gspot, rawgoal);
        break;
    }

    if (idx >= 0)
    {
        hashid = g_hash_roboanim[idx];
    }

    return hashid;
}

S32 zNPCTubelet::Respawn(const xVec3* pos, zMovePoint* mvptFirst, zMovePoint* mvptSpawnRef)
{
    S32 rc = zNPCCommon::Respawn(pos, mvptFirst, mvptSpawnRef);
    if (rc != 0)
    {
        PrepTheBand();
    }
    return rc;
}

void zNPCTubelet::PrepTheBand()
{
    tubestat = TUBE_STAT_BORN;
    hitpoints = cfg_npc->pts_damage;
    ModelAtomicShow(0, NULL);
    ModelAtomicHide(1, NULL);
    ModelAtomicHide(4, NULL);
    tub_paul->WeGotAGig();
    tub_mary->WeGotAGig();
}

void zNPCTubelet::LassoNotify(en_LASSO_EVENT event)
{
    zNPCLassoInfo* lassinfo = lassdata;

    if (IsDead())
    {
        return;
    }

    zNPCRobot::LassoNotify(event);

    switch (event)
    {
    case LASS_EVNT_ENDED:
    case LASS_EVNT_ABORT:
        break;
    case LASS_EVNT_BEGIN:
        tubestat = TUBE_STAT_LASSO;
        return;
    }

    if ((lassinfo->stage == LASS_STAT_PENDING || lassinfo->stage == LASS_STAT_DONE) &&
        tubestat == TUBE_STAT_LASSO && hitpoints >= 1)
    {
        PrepTheBand();

        xPsyche* psy = psy_instinct;

        if (psy->GIDInStack(NPC_GOAL_ALERT))
        {
            tubestat = TUBE_STAT_ATTACK;
        }
        else if (psy->GIDInStack(NPC_GOAL_IDLE))
        {
            tubestat = TUBE_STAT_DUCKLING;
        }
        else
        {
            tubestat = TUBE_STAT_DUCKLING;
        }
    }
}

void zNPCTubelet::Unbonk()
{
    ModelAtomicShow(0, NULL);
    ModelAtomicHide(1, NULL);
    ModelAtomicHide(4, NULL);
    hitpoints = cfg_npc->pts_damage;
    // Epilogue weirdness
    pflags &= 0xdf;
    bonkSpinRate = -1.0;
}

S32 zNPCTubelet::Chk_IsBonked()
{
    S32 rc;

    if (tubestat == TUBE_STAT_DYING)
    {
        rc = 1;
    }
    else if (tubestat == TUBE_STAT_DEAD)
    {
        rc = 1;
    }
    else
    {
        S32 cnt_dead;
        zNPCTubeSlave* mary;

        bonkSpinRate = 0.0f;

        cnt_dead = (hitpoints == 0);

        if (tub_paul == NULL)
        {
            cnt_dead++;
        }
        else if (tub_paul->hitpoints == 0)
        {
            cnt_dead++;
        }

        mary = tub_mary;

        if (mary == NULL)
        {
            cnt_dead++;
        }
        else if (mary->hitpoints == 0)
        {
            cnt_dead++;
        }

        if (cnt_dead == 3)
        {
            if (mary != NULL)
            {
                mary->hitpoints = 0;
            }

            if (tub_paul != NULL)
            {
                tub_paul->hitpoints = 0;
            }

            hitpoints = 0;
            tubestat = TUBE_STAT_DYING;
        }
        else if (hitpoints > 0)
        {
            Unbonk();
        }

        rc = (hitpoints == 0);
    }

    return rc;
}

void zNPCTubelet::PainInTheBand()
{
    tmr_restoreHealth = 2.5f;
}

void TubeNotice::Notice(en_psynote note, xGoal* goal, void* data)
{
    zNPCTubelet* tube = (zNPCTubelet*)npc;

    switch (note)
    {
    case PSY_NOTE_HASRESUMED:
    case PSY_NOTE_HASENTERED:
        break;
    default:
        return;
    }

    switch (goal->GetID())
    {
    case NPC_GOAL_ALERTTUBELET:
        if (tube->tubestat != TUBE_STAT_LASSO)
        {
            tube->tubestat = TUBE_STAT_ATTACK;
        }
        return;
    case NPC_GOAL_DEFLATE:
    case NPC_GOAL_DAMAGE:
    case NPC_GOAL_KNOCK:
        tube->tubestat = TUBE_STAT_DYING;
        return;
    case NPC_GOAL_LASSOBASE:
    case NPC_GOAL_LASSOGRAB:
    case NPC_GOAL_LASSOTHROW:
        tube->tubestat = TUBE_STAT_LASSO;
        return;
    case NPC_GOAL_AFTERLIFE:
        tube->tubestat = TUBE_STAT_DEAD;
        return;
    case NPC_GOAL_RESPAWN:
        tube->tubestat = TUBE_STAT_BORN;
        return;
    }

    if (tube->tubestat != TUBE_STAT_LASSO)
    {
        tube->tubestat = TUBE_STAT_DUCKLING;
    }
}

S32 zNPCTubelet::IsDying()
{
    return (tubestat == TUBE_STAT_DEAD) ? 1 : 0;
}

void zNPCTubeSlave::Init(xEntAsset* asset)
{
    zNPCRobot::Init(asset);
    flg_move &= 0xfffffffd;
    flg_move |= 4;

    flg_vuln &= 0x8fffffff;
    flg_vuln &= 0xfeffffff;

    idx_neckBone = -1;
    tubespot = ROBO_TUBE_PAUL;
    tub_pete = NULL;

    laser.Prepare();

    xModelInstance* mdl = ModelAtomicFind(1, -1, NULL);
    mdl->Flags &= 0xffdf;
    mdl->Flags |= 8;
}

void zNPCTubeSlave::Reset()
{
    zNPCRobot::Reset();
    flags |= 0x40;
    zNPCTubeSlave::WeGotAGig();
}

void zNPCTubeSlave::WeGotAGig()
{
    PartyOn();
    psy_instinct->GoalSet(0x4e47524f, 1);
}

void zNPCTubeSlave::PartyOn()
{
    ModelAtomicShow(0, NULL);
    ModelAtomicHide(1, NULL);
    ModelAtomicHide(4, NULL);
    hitpoints = cfg_npc->pts_damage;
}

void zNPCTubeSlave::ParseINI()
{
    NPCConfig* cfg = cfg_npc;
    zNPCRobot::ParseINI();
    cfg->snd_trax = g_sndTrax_Tubelet;
    NPCS_SndTablePrepare(g_sndTrax_Tubelet);
}

void zNPCTubeSlave::SelfSetup()
{
    flg_vuln &= 0xfeffffff;

    xBehaveMgr* bmgr = xBehaveMgr_GetSelf();
    psy_instinct = bmgr->Subscribe(this, 0);

    xPsyche* psy = psy_instinct;

    psy->BrainBegin();
    psy->AddGoal(NPC_GOAL_TAUNT, NULL);
    psy->AddGoal(NPC_GOAL_FIDGET, NULL);
    psy->AddGoal(NPC_GOAL_LIMBO, NULL);
    psy->AddGoal(NPC_GOAL_TUBEPAL, NULL);
    psy->AddGoal(NPC_GOAL_TUBEDUCKLING, NULL);
    psy->AddGoal(NPC_GOAL_TUBEATTACK, NULL);
    psy->AddGoal(NPC_GOAL_TUBELASSO, NULL);
    psy->AddGoal(NPC_GOAL_TUBEBIRTH, NULL);
    psy->AddGoal(NPC_GOAL_TUBEBONKED, NULL);
    psy->AddGoal(NPC_GOAL_TUBEDYING, NULL);
    psy->AddGoal(NPC_GOAL_TUBEDEAD, NULL);
    psy->BrainEnd();
    psy->SetSafety(NPC_GOAL_TUBEPAL);
}

U32 zNPCTubeSlave::AnimPick(S32 gid, en_NPC_GOAL_SPOT gspot, xGoal* rawgoal)
{
    U32 hashid = 0;
    S32 idx = -1;

    switch (gid)
    {
    case NPC_GOAL_TUBEPAL:
        idx = 1;
        break;
    case NPC_GOAL_TUBEDUCKLING:
        idx = 3;
        break;
    case NPC_GOAL_TUBELASSO:
        idx = 0xe;
        break;
    case NPC_GOAL_TUBEBIRTH:
        idx = 1;
        break;
    case NPC_GOAL_TUBEDEAD:
        idx = 0x25;
        break;
    case NPC_GOAL_TUBEATTACK:
        idx = 3;
        break;
    default:
        hashid = zNPCRobot::AnimPick(gid, gspot, rawgoal);
        break;
    }

    if (idx >= 0)
    {
        hashid = g_hash_roboanim[idx];
    }

    return hashid;
}

void zNPCTubeSlave::Process(xScene* xscn, F32 dt)
{
    zNPCRobot::Process(xscn, dt);
}

void zNPCTubeSlave::RenderExtra()
{
    DoLaserRendering();
    zNPCCommon::RenderExtra();
}

S32 zNPCTubeSlave::RoboHandleMail(NPCMsg* mail)
{
    S32 handled = 0;
    xPsyche* psy = psy_instinct;

    switch (mail->msgid)
    {
    case NPC_MID_DAMAGE:
        if (hitpoints != 0)
        {
            handled = 1;
            if (!psy->GIDInStack(NPC_GOAL_TUBEBONKED))
            {
                psy->GoalPush(NPC_GOAL_TUBEBONKED, 0);
            }
        }
        break;
    }

    return handled;
}

S32 zNPCTubeSlave::IsDying()
{
    return tub_pete->IsDying();
}

void zNPCTubeSlave::PosStacked(xVec3* pos_stacked)
{
    F32 hyt;

    if (tubespot == ROBO_TUBE_MARY)
    {
        hyt = 3.0f;
    }
    else
    {
        hyt = 1.5f;
    }

    xVec3Copy(pos_stacked, xEntGetPos(tub_pete));
    pos_stacked->y += hyt;
}

void zNPCTubeSlave::DoLaserRendering()
{
    xGoal* goal;
    if (tubespot == ROBO_TUBE_PAUL)
    {
        goal = psy_instinct->GetCurGoal();
        if (goal != NULL && (goal->GetID() == NPC_GOAL_TUBEATTACK))
        {
            ((zNPCGoalTubeAttack*)(goal))->LaserRender();
        }
    }
}

void zNPCSlick::Init(xEntAsset* asset)
{
    zNPCRobot::Init(asset);

    flg_move &= 0xfffffffd;
    flg_move |= 4;

    flg_vuln &= 0x9fffffff;
    flg_vuln |= 0x1000000;

    idx_neckBone = -1;

    if (!g_uvaShield.Valid())
    {
        xModelInstance* mdl = zNPCCommon::ModelAtomicFind(1, -1, NULL);
        if ((mdl != NULL) && (mdl->Data != NULL))
        {
            g_uvaShield.Init(mdl->Data, 0);
            g_uvaShield.UVVelSet(0.0f, -0.7f);
        }
    }
    NPAR_PartySetup(NPAR_TYP_OILBUB, NULL, NULL);
}

void zNPCSlick::Reset()
{
    zNPCRobot::Reset();
    tmr_repairShield = -1.0f;
    tmr_invuln = -1.0f;
    alf_shieldDesired = 100.0f / 255.0f;
    alf_shieldCurrent = 0.0f;
    rad_shield = 0.3f;
}

void zNPCSlick::ParseINI()
{
    NPCConfig* cfg = cfg_npc;
    zNPCRobot::ParseINI();
    cfg->snd_trax = g_sndTrax_Slick;
    NPCS_SndTablePrepare(g_sndTrax_Slick);
}

void zNPCSlick::SelfSetup()
{
    zNPCRobot::SelfSetup();

    xPsyche* psy = psy_instinct;
    xGoal* goal;

    goal = psy->FindGoal(NPC_GOAL_IDLE);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_FIDGET);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WAITING);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_PATROL);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_WANDER);
    goal->SetCallbacks(ROBO_grul_goAlertLobber, NULL, NULL, NULL);
    goal = psy->FindGoal(NPC_GOAL_ALERT);
    goal->SetCallbacks(NULL, SLCK_grul_alert, NULL, NULL);

    psy->BrainExtend();
    psy->AddGoal(NPC_GOAL_ALERTSLICK, NULL);
    psy->AddGoal(NPC_GOAL_ATTACKSLICK, NULL);
    psy->AddGoal(NPC_GOAL_WOUND, NULL);
    psy->BrainEnd();

    psy->SetSafety(NPC_GOAL_IDLE);
}

void zNPCSlick::ShieldFX(F32 dt)
{
    if (g_needuvincr_slickshield)
    {
        g_uvaShield.Update(dt, NULL);
        g_needuvincr_slickshield = 0;
    }
}

void zNPCSlick::RopePopsShield()
{
    ShieldGeneratorDamaged();
    alf_shieldCurrent = 0.0f;
    tmr_repairShield = 5.0f;
}

bool zNPCSlick::IsShield() const
{
    return alf_shieldDesired == 100.0f / 255.0f;
}

void zNPCSlick::ShieldHide()
{
    alf_shieldDesired = 0.0f;
}

void zNPCSlick::ShieldShow()
{
    alf_shieldDesired = 100.0f / 255.0f;
}

zNPCSlick* zNPCSlick::YouOwnSlipFX()
{
    return g_slick_slipfx_owner = this;
}

S32 DUMY_grul_returnToIdle(xGoal* goal, void*, en_trantype* trantype, F32, void*)
{
    S32 gid = 0;

    if (goal->GetPsyche()->TimerGet(XPSY_TYMR_CURGOAL) > 10.0f)
    {
        *trantype = GOAL_TRAN_SET;
        gid = NPC_GOAL_IDLE;
    }

    return gid;
}

S32 FODR_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*)
{
    *trantype = GOAL_TRAN_PUSH;
    return NPC_GOAL_ALERTFODDER;
}

S32 BOMB_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*)
{
    *trantype = GOAL_TRAN_PUSH;
    return NPC_GOAL_ALERTFODBOMB;
}

S32 BZZT_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*)
{
    *trantype = GOAL_TRAN_PUSH;
    return NPC_GOAL_ALERTFODBZZT;
}

S32 CHMP_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*)
{
    *trantype = GOAL_TRAN_PUSH;
    return NPC_GOAL_ALERTCHOMPER;
}

S32 HAMR_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*)
{
    *trantype = GOAL_TRAN_PUSH;
    return NPC_GOAL_ALERTHAMMER;
}

S32 TART_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*)
{
    *trantype = GOAL_TRAN_PUSH;
    return NPC_GOAL_ALERTTARTAR;
}

S32 GLOV_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*)
{
    *trantype = GOAL_TRAN_PUSH;
    return NPC_GOAL_ALERTGLOVE;
}

S32 MOON_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*)
{
    *trantype = GOAL_TRAN_PUSH;
    return NPC_GOAL_ALERTMONSOON;
}

S32 SLEP_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*)
{
    *trantype = GOAL_TRAN_PUSH;
    return NPC_GOAL_ALERTSLEEPY;
}

S32 ARFY_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*)
{
    *trantype = GOAL_TRAN_PUSH;
    return NPC_GOAL_ALERTARF;
}

S32 PUPY_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*)
{
    *trantype = GOAL_TRAN_PUSH;
    return NPC_GOAL_ALERTPUPPY;
}

S32 CHUK_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*)
{
    *trantype = GOAL_TRAN_PUSH;
    return NPC_GOAL_ALERTCHUCK;
}

S32 TUBE_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*)
{
    *trantype = GOAL_TRAN_PUSH;
    return NPC_GOAL_ALERTTUBELET;
}

S32 SLCK_grul_alert(xGoal* goal, void*, en_trantype* trantype, float, void*)
{
    *trantype = GOAL_TRAN_PUSH;
    return NPC_GOAL_ALERTSLICK;
}

void ROBO_KillEffects()
{
}

F32 NPCArena::PctFromHome(xVec3* pos)
{
    xVec3 dif;

    if (!IsReady())
    {
        return -1.0f;
    }

    xVec3Sub(&dif, pos, &pos_arena);
    dif.y = 0.0f;

    return xVec3Length2(&dif) / SQ(rad_arena);
}

F32 NPCArena::DstSqFromHome(xVec3* pos, xVec3* delt)
{
    xVec3 dif;

    if (!IsReady())
    {
        return -1.0f;
    }

    xVec3Sub(&dif, pos, &pos_arena);

    if (delt != NULL)
    {
        *delt = dif;
    }

    dif.y = 0.0f;

    return xVec3Length2(&dif);
}

S32 NPCArena::NeedToCycle(zNPCCommon* npc)
{
    S32 rc;

    if (IsReady() && !IncludesNPC(npc, 0.0f, NULL) && !(flg_arena & 4))
    {
        return 2;
    }

    if (npc->nav_dest != nav_refer_dest || npc->nav_curr != nav_refer_curr)
    {
        return 1;
    }

    if (!IsReady() && !(flg_arena & 4))
    {
        rc = 2;
    }
    else
    {
        rc = 0;
    }

    return rc;
}

void NPCArena::SetHome(zNPCCommon* npc, zMovePoint* nav)
{
    flg_arena = 0;

    if (nav == NULL)
    {
        nav_arena = NULL;
    }
    // TODO: should be nav->RadiusArena() - see hand-off notes
    else if (nav->asset->arenaRadius > 1.0f)
    {
        nav_arena = nav;
    }
    else
    {
        nav_arena = NextBestNav(npc, nav);
    }

    nav_refer_dest = npc->nav_dest;
    nav_refer_curr = npc->nav_curr;

    SyncHomeFromNav();
}

void NPCArena::AdjustHome(zNPCCommon* npc, xVec3* pos, F32 rad)
{
    flg_arena |= 2;

    if (pos != NULL)
    {
        xVec3Copy(&pos_arena, pos);
    }

    if (rad > 0.0f)
    {
        rad_arena = rad;
    }

    rad_arena = MAX(1.0f, rad_arena);
}

void NPCArena::SyncHomeFromNav()
{
    flg_arena = 0;

    if (nav_arena != NULL)
    {
        xVec3Copy(&pos_arena, nav_arena->PosGet());
        // TODO: should be nav_arena->RadiusArena() - see hand-off notes
        rad_arena = nav_arena->asset->arenaRadius;
    }
    else
    {
        xVec3Copy(&pos_arena, &g_O3);
        rad_arena = -1.0f;
    }
}

void UVAModelInfo::Clear()
{
    memset(this, 0, sizeof(UVAModelInfo));
}

void UVAModelInfo::UVVelSet(float x, float y)
{
    offset_vel.x = x;
    offset_vel.y = y;
}

zNPCLassoInfo* zNPCRobot::PRIV_GetLassoData()
{
    return &raw_lassoinfo;
}

S32 zNPCRobot::IsAlive()
{
    return !IsDead();
}

void zNPCRobot::LassoModelIndex(S32* idxgrab, S32* idxhold)
{
    *idxgrab = 0xffffffff;
    *idxhold = 0xffffffff;
}

void zNPCSlick::LassoModelIndex(S32* idxgrab, S32* idxhold)
{
    *idxgrab = 2;
    *idxhold = 3;
}

S32 zNPCRobot::IsHealthy()
{
    return (hitpoints < 0) ? 0 : hitpoints;
}

xEntDrive* zNPCRobot::PRIV_GetDriverData()
{
    return &raw_drvdata;
}

zNPCLassoInfo* zNPCFodder::PRIV_GetLassoData()
{
    return NULL;
}

void zNPCFodder::LassoModelIndex(S32* idxgrab, S32* idxhold)
{
    *idxgrab = -1;
    *idxhold = -1;
}

S32 xEntIsEnabled(xEnt* ent)
{
    return xBaseIsEnabled(ent);
}

zNPCLassoInfo* zNPCFodBomb::PRIV_GetLassoData()
{
    return NULL;
}

zNPCLassoInfo* zNPCFodBzzt::PRIV_GetLassoData()
{
    return NULL;
}

zNPCLassoInfo* zNPCChomper::PRIV_GetLassoData()
{
    return NULL;
}

zNPCLassoInfo* zNPCCritter::PRIV_GetLassoData()
{
    return NULL;
}

zNPCLassoInfo* zNPCArfDog::PRIV_GetLassoData()
{
    return NULL;
}

U8 zNPCTubeSlave::PhysicsFlags() const
{
    return 3;
}

U8 zNPCTubeSlave::ColPenByFlags() const
{
    return 16;
}

U8 zNPCTubeSlave::ColChkByFlags() const
{
    return 16;
}

U8 zNPCTubeSlave::ColPenFlags() const
{
    return 0;
}

U8 zNPCTubeSlave::ColChkFlags() const
{
    return 0;
}

S32 zNPCTubeSlave::CanRope()
{
    return 0;
}

U8 zNPCRobot::ColPenByFlags() const
{
    return 60;
}

U8 zNPCRobot::ColChkByFlags() const
{
    return 60;
}

void zNPCArfDog::LassoModelIndex(S32* idxgrab, S32* idxhold)
{
    *idxgrab = -1;
    *idxhold = -1;
}

void zNPCChomper::LassoModelIndex(S32* idxgrab, S32* idxhold)
{
    *idxgrab = -1;
    *idxhold = -1;
}

void zNPCCritter::LassoModelIndex(S32* idxgrab, S32* idxhold)
{
    *idxgrab = -1;
    *idxhold = -1;
}

void zNPCFodBomb::LassoModelIndex(S32* idxgrab, S32* idxhold)
{
    *idxgrab = -1;
    *idxhold = -1;
}

void zNPCFodBzzt::LassoModelIndex(S32* idxgrab, S32* idxhold)
{
    *idxgrab = -1;
    *idxhold = -1;
}

void zNPCArfArf::LassoModelIndex(S32* idxgrab, S32* idxhold)
{
    *idxgrab = 2;
    *idxhold = 3;
}

void zNPCHammer::LassoModelIndex(S32* idxgrab, S32* idxhold)
{
    *idxgrab = 1;
    *idxhold = 2;
}

void zNPCSleepy::LassoModelIndex(S32* idxgrab, S32* idxhold)
{
    *idxgrab = 2;
    *idxhold = 3;
}

void zNPCTarTar::LassoModelIndex(S32* idxgrab, S32* idxhold)
{
    *idxgrab = 2;
    *idxhold = 3;
}

void zNPCMonsoon::LassoModelIndex(S32* idxgrab, S32* idxhold)
{
    *idxgrab = 1;
    *idxhold = 2;
}

void zNPCTubelet::LassoModelIndex(S32* idxgrab, S32* idxhold)
{
    *idxgrab = 2;
    *idxhold = 3;
}

void zNPCTubeSlave::LassoModelIndex(S32* idxgrab, S32* idxhold)
{
    *idxgrab = 2;
    *idxhold = 3;
}

void zNPCChuck::LassoModelIndex(S32* idxgrab, S32* idxhold)
{
    *idxgrab = 2;
    *idxhold = 3;
}

void zNPCGlove::LassoModelIndex(S32* idxgrab, S32* idxhold)
{
    *idxgrab = 1;
    *idxhold = 2;
}

TubeNotice::TubeNotice()
{
}

S32 zNPCRobot::IsDead()
{
    return IsDying() == 1;
}

void NPCLaser::TextureSet(RwRaster* rast)
{
    rast_laser = rast;
}

RwRaster* NPCLaser::TextureGet()
{
    return rast_laser;
}

void NPCLaser::RadiusSet(F32 rad_start, F32 rad_end)
{
    radius[0] = rad_start;
    radius[1] = rad_end;
}

void NPCLaser::UVScrollSet(F32 u, F32 v)
{
    uv_scroll[0] = u;
    uv_scroll[1] = v;
}

void NPCLaser::Prepare()
{
    rast_laser = NULL;
    uv_base[0] = 0.0f;
    uv_base[1] = 0.0f;
}

void NPCLaser::UVScrollUpdate(F32 dt)
{
    uv_base[0] += dt * uv_scroll[0];
    while (uv_base[0] > 1.0f)
    {
        uv_base[0] -= 1.0f;
    }
    while (uv_base[0] < -0.0f)
    {
        uv_base[0] += 1.0f;
    }

    uv_base[1] += dt * uv_scroll[1];
    while (uv_base[1] > 1.0f)
    {
        uv_base[1] -= 1.0f;
    }
    while (uv_base[1] < -0.0f)
    {
        uv_base[1] += 1.0f;
    }
}

void zNPCArfDog::RenderExtra()
{
    BlinkRender();
    zNPCCommon::RenderExtra();
}

void zNPCFodBomb::RenderExtra()
{
    BlinkerRender();
    zNPCCommon::RenderExtra();
}
