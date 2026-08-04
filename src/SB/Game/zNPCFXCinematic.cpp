#include "xVec3.h"
#include "xMath3.h"
#include "xParEmitter.h"

#include "zNPCFXCinematic.h"
#include "zParPTank.h"
#include "zNPCTypeBossSB2.h"
#include "zNPCSupport.h"
#include "xString.h"
#include "xstransvc.h"
#include "zScene.h"
#include "xEnt.h"
#include "iModel.h"

#include <types.h>

static NCINBeNosey* g_noz_ncin;

void NCIN_Par_BPLANK_JET_1_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
        return;
    }

    if (fxrec->flg_stat & 2)
    {
        fxrec->fxdata.pardata.emitter = zParEmitterFind("PAREMIT_BPLANK_JET_1");

        xParEmitterAsset* tasset = fxrec->fxdata.pardata.emitter->tasset;

        if (tasset == NULL || tasset->emit_type != 15)
        {
            fxrec->fxdata.pardata.emitter = NULL;
        }

        if (fxrec->fxdata.pardata.emitter == NULL)
        {
            fxrec->flg_stat |= 4;
            return;
        }

        F32& float_accum = fxrec->pos_A[1].y;

        if (float_accum < 0.0f)
        {
            float_accum = 20.0f + tasset->e_entbone.bone;
        }

        fxrec->pos_B[0] = 5.0f;
    }
}

void NCIN_Par_BPLANK_JET_2_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
        return;
    }

    if (fxrec->flg_stat & 2)
    {
        fxrec->fxdata.pardata.emitter = zParEmitterFind("PAREMIT_BPLANK_JET_2");

        xParEmitterAsset* tasset = fxrec->fxdata.pardata.emitter->tasset;

        if (tasset == NULL || tasset->emit_type != 15)
        {
            fxrec->fxdata.pardata.emitter = NULL;
        }

        if (fxrec->fxdata.pardata.emitter == NULL)
        {
            fxrec->flg_stat |= 4;
            return;
        }

        F32& float_accum = fxrec->pos_A[1].y;

        if (float_accum < 0.0f)
        {
            float_accum = 20.0f + tasset->e_entbone.bone;
        }

        fxrec->pos_B[0] = 5.0f;
    }
}

void NCIN_Par_BPLANK_SBB_FLAMES_1_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
        return;
    }

    if (fxrec->flg_stat & 2)
    {
        fxrec->fxdata.pardata.emitter = zParEmitterFind("PAREMIT_SBB_FLAMES_1");

        xParEmitterAsset* tasset = fxrec->fxdata.pardata.emitter->tasset;

        if (tasset == NULL || tasset->emit_type != 15)
        {
            fxrec->fxdata.pardata.emitter = NULL;
        }

        if (fxrec->fxdata.pardata.emitter == NULL)
        {
            fxrec->flg_stat |= 4;
            return;
        }

        F32& float_accum = fxrec->pos_A[1].y;

        if (float_accum < 0.0f)
        {
            float_accum = 20.0f + tasset->e_entbone.bone;
        }

        fxrec->pos_B[0] = 5.0f;
    }
}

void NCIN_Par_BPLANK_SBB_FLAMES_2_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
        return;
    }

    if (fxrec->flg_stat & 2)
    {
        fxrec->fxdata.pardata.emitter = zParEmitterFind("PAREMIT_SBB_FLAMES_2");

        xParEmitterAsset* tasset = fxrec->fxdata.pardata.emitter->tasset;

        if (tasset == NULL || tasset->emit_type != 15)
        {
            fxrec->fxdata.pardata.emitter = NULL;
        }

        if (fxrec->fxdata.pardata.emitter == NULL)
        {
            fxrec->flg_stat |= 4;
            return;
        }

        F32& float_accum = fxrec->pos_A[1].y;

        if (float_accum < 0.0f)
        {
            float_accum = 20.0f + tasset->e_entbone.bone;
        }

        fxrec->pos_B[0] = 5.0f;
    }
}

void NCIN_Par_BPLANK_SBB_JET_1_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
        return;
    }

    if (fxrec->flg_stat & 2)
    {
        fxrec->fxdata.pardata.emitter = zParEmitterFind("PAREMIT_SBB_JET_1");

        xParEmitterAsset* tasset = fxrec->fxdata.pardata.emitter->tasset;

        if (tasset == NULL || tasset->emit_type != 15)
        {
            fxrec->fxdata.pardata.emitter = NULL;
        }

        if (fxrec->fxdata.pardata.emitter == NULL)
        {
            fxrec->flg_stat |= 4;
            return;
        }

        F32& float_accum = fxrec->pos_A[1].y;

        if (float_accum < 0.0f)
        {
            float_accum = 20.0f + tasset->e_entbone.bone;
        }

        fxrec->pos_B[0] = 5.0f;
    }
}

void NCIN_Par_BPLANK_SBB_JET_2_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
        return;
    }

    if (fxrec->flg_stat & 2)
    {
        fxrec->fxdata.pardata.emitter = zParEmitterFind("PAREMIT_SBB_JET_2");

        xParEmitterAsset* tasset = fxrec->fxdata.pardata.emitter->tasset;

        if (tasset == NULL || tasset->emit_type != 15)
        {
            fxrec->fxdata.pardata.emitter = NULL;
        }

        if (fxrec->fxdata.pardata.emitter == NULL)
        {
            fxrec->flg_stat |= 4;
            return;
        }

        F32& float_accum = fxrec->pos_A[1].y;

        if (float_accum < 0.0f)
        {
            float_accum = 20.0f + tasset->e_entbone.bone;
        }

        fxrec->pos_B[0] = 5.0f;
    }
}

void NCIN_Par_BPLANK_SBB_SMOKE_1_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
        return;
    }

    if (fxrec->flg_stat & 2)
    {
        fxrec->fxdata.pardata.emitter = zParEmitterFind("PAREMIT_SBB_SMOKE_1");

        xParEmitterAsset* tasset = fxrec->fxdata.pardata.emitter->tasset;

        if (tasset == NULL || tasset->emit_type != 15)
        {
            fxrec->fxdata.pardata.emitter = NULL;
        }

        if (fxrec->fxdata.pardata.emitter == NULL)
        {
            fxrec->flg_stat |= 4;
            return;
        }

        F32& float_accum = fxrec->pos_A[1].y;

        if (float_accum < 0.0f)
        {
            float_accum = 20.0f + tasset->e_entbone.bone;
        }

        fxrec->pos_B[0] = 5.0f;
    }
}

void NCIN_Par_BPLANK_SBB_SMOKE_2_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
        return;
    }

    if (fxrec->flg_stat & 2)
    {
        fxrec->fxdata.pardata.emitter = zParEmitterFind("PAREMIT_SBB_SMOKE_2");

        xParEmitterAsset* tasset = fxrec->fxdata.pardata.emitter->tasset;

        if (tasset == NULL || tasset->emit_type != 15)
        {
            fxrec->fxdata.pardata.emitter = NULL;
        }

        if (fxrec->fxdata.pardata.emitter == NULL)
        {
            fxrec->flg_stat |= 4;
            return;
        }

        F32& float_accum = fxrec->pos_A[1].y;

        if (float_accum < 0.0f)
        {
            float_accum = 20.0f + tasset->e_entbone.bone;
        }

        fxrec->pos_B[0] = 5.0f;
    }
}

void NCIN_Par_CIN_BIGDUP_SMOKE_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
        return;
    }

    if (fxrec->flg_stat & 2)
    {
        fxrec->fxdata.pardata.emitter = zParEmitterFind("PAREMIT_CIN_BIGUP_SMOKE");

        xParEmitterAsset* tasset = fxrec->fxdata.pardata.emitter->tasset;

        if (tasset == NULL || tasset->emit_type != 15)
        {
            fxrec->fxdata.pardata.emitter = NULL;
        }

        if (fxrec->fxdata.pardata.emitter == NULL)
        {
            fxrec->flg_stat |= 4;
            return;
        }

        F32& float_accum = fxrec->pos_A[1].y;

        if (float_accum < 0.0f)
        {
            float_accum = 20.0f + tasset->e_entbone.bone;
        }

        fxrec->pos_B[0] = 5.0f;
    }
}

void NCIN_Par_CIN_BIGDUP_SPAWN_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
        return;
    }

    if (fxrec->flg_stat & 2)
    {
        fxrec->fxdata.pardata.emitter = zParEmitterFind("PAREMIT_CIN_BIGUP_SPAWN");

        xParEmitterAsset* tasset = fxrec->fxdata.pardata.emitter->tasset;

        if (tasset == NULL || tasset->emit_type != 15)
        {
            fxrec->fxdata.pardata.emitter = NULL;
        }

        if (fxrec->fxdata.pardata.emitter == NULL)
        {
            fxrec->flg_stat |= 4;
            return;
        }

        F32& float_accum = fxrec->pos_A[1].y;

        if (float_accum < 0.0f)
        {
            float_accum = 20.0f + tasset->e_entbone.bone;
        }

        fxrec->pos_B[0] = 5.0f;
    }
}

void NCIN_Par_CIN_PLATFORM_JETS_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
        return;
    }

    if (fxrec->flg_stat & 2)
    {
        fxrec->fxdata.pardata.emitter = zParEmitterFind("PAREMIT_CIN_PLATFORM_JETS");

        xParEmitterAsset* tasset = fxrec->fxdata.pardata.emitter->tasset;

        if (tasset == NULL || tasset->emit_type != 15)
        {
            fxrec->fxdata.pardata.emitter = NULL;
        }

        if (fxrec->fxdata.pardata.emitter == NULL)
        {
            fxrec->flg_stat |= 4;
            return;
        }

        F32& float_accum = fxrec->pos_A[1].y;

        if (float_accum < 0.0f)
        {
            float_accum = 20.0f + tasset->e_entbone.bone;
        }

        fxrec->pos_B[0] = 5.0f;
    }
}

void NCINBeNosey::CanRenderNow()
{
    zCutsceneMgr* csnmgr = (zCutsceneMgr*)this->use_csnmgr;
    NCINEntry* fxtab = this->use_fxtab;

    if (csnmgr == NULL || fxtab == NULL)
    {
        return;
    }

    while (fxtab->typ_ncinfx != NCIN_FXTYP_UNKNOWN)
    {
        NCINEntry* fxrec = fxtab;

        fxtab++;

        if (fxrec->cb_fxrend != NULL && (fxrec->flg_stat & 1))
        {
            fxrec->cb_fxrend(csnmgr, fxrec);
        }
    }
}

void zNPCFXShutdown()
{
}

void NCINBeNosey::Init(const zCutsceneMgr* m, NCINEntry* e, S32 i) // TODO: investigate missing member
{
    this->use_fxtab = e;
    this->use_csnmgr = m;
}

void NCINBeNosey::Done()
{
    this->use_fxtab = 0;
    this->use_csnmgr = 0;
}

void NCIN_Generic_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
    }
}

// TODO: NEEDS REWRITEN / CORRECTED
void NCIN_BubSlam(const zCutsceneMgr*, NCINEntry* fxrec, S32 param)
{
    if (param != 0)
    {
        fxrec->flg_stat |= 4;
        return;
    }

    if (!(fxrec->flg_stat & 2))
    {
        return;
    }

    switch (fxrec->typ_ncinfx)
    {
    case 3:
        zFX_SpawnBubbleSlam(&fxrec->pos_A[0], 64, PI, 2.0f, 2.0f);
        break;
    case 2:
        zFX_SpawnBubbleSlam(&fxrec->pos_A[0], 128, PI, 5.0f, 5.0f);
        break;
    default:
        break;
    }
}

void NCIN_BubTrailBone_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                          U32 num_1, U32 num_2)
{
    S32 ifx = fxrec->pos_A[1].x;
    S32 ify = fxrec->pos_A[1].y;

    if (num_1 != ifx)
    {
        return;
    }

    xVec3 pos = *(const xVec3*)&animMat->pos;

    if (ify > 0)
    {
        pos += *(const xVec3*)&animMat[ify].pos;
    }

    zFX_SpawnBubbleTrail(&pos, 1);
}

void NCIN_BubHit(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
    }
    else
    {
        if (fxrec->flg_stat & 2)
        {
            zFX_SpawnBubbleHit(&fxrec->pos_A[0], 16);
        }
        zFX_SpawnBubbleHit(&fxrec->pos_A[0], 3);
    }
}

// TODO: NEEDS REWRITEN / CORRECTED
void NCIN_Zapper(const zCutsceneMgr*, NCINEntry* fxrec, S32 param)
{
    if (param != 0)
    {
        fxrec->flg_stat |= 4;

        if (fxrec->idx_anim == 29 || fxrec->idx_anim == 8)
        {
            if (fxrec->fxdata.lytdata.lyt_zap != NULL)
            {
                zLightningKill(fxrec->fxdata.lytdata.lyt_zap);
            }

            fxrec->fxdata.lytdata.lyt_zap = NULL;
        }
        return;
    }

    if (fxrec->flg_stat & 2)
    {
        memset(&fxrec->fxdata.arcdata.lightning, 0, sizeof(fxrec->fxdata.arcdata.lightning));
    }

    if (fxrec->fxdata.lytdata.lyt_zap == NULL)
    {
        fxrec->flg_stat |= 4;
    }
}

void NCIN_HammerShock(const zCutsceneMgr*, NCINEntry* fxrec, S32 param)
{
    if (param != 0)
    {
        fxrec->flg_stat |= 4;
        return;
    }
    else if (fxrec->flg_stat & 2)
    {
        zFXHammer(&fxrec->pos_A[0]);
        fxrec->flg_stat |= 4;
    }
}

static void NCIN_HammerStreak_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;

        xFXStreakStop(fxrec->fxdata.strkdata.sid_horz);
        xFXStreakStop(fxrec->fxdata.strkdata.sid_vert);

        fxrec->fxdata.strkdata.sid_horz = 57005;
        fxrec->fxdata.strkdata.sid_vert = 57005;
    }
    else if (fxrec->flg_stat & 2)
    {
        en_npcstreak styp_h = NPC_STRK_HAMMERSMASH_HORZ;
        en_npcstreak styp_v = NPC_STRK_HAMMERSMASH_VERT;

        fxrec->fxdata.strkdata.sid_horz = NPCC_StreakCreate(styp_h);
        fxrec->fxdata.strkdata.sid_vert = NPCC_StreakCreate(styp_v);
    }
}

void NCIN_HammerStreak_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag*, U32 num_1,
                          U32 num_2)
{
    if (num_2 != 2)
    {
        return;
    }

    U32 sid_vert = fxrec->fxdata.strkdata.sid_vert;
    U32 sid_horz = fxrec->fxdata.strkdata.sid_horz;
}

void NCIN_WaterSplash(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
    }
    else if (fxrec->flg_stat & 2)
    {
        NPCC_MakeASplash(&fxrec->pos_A[0], -1.0f);
        fxrec->flg_stat |= 4;
    }
}

void NCIN_HazProjShoot(const zCutsceneMgr* mgr, NCINEntry* fxrec, S32 param)
{
    if (param != 0)
    {
        fxrec->flg_stat |= 4;

        if (fxrec->fxdata.hazdata.npchaz->flg_hazard)
        {
            fxrec->fxdata.hazdata.npchaz->MarkForRecycle();
        }
        return;
    }

    if (fxrec->flg_stat & 2)
    {
        S32 type = fxrec->fxdata.hazdata.npchaz->typ_hazard;
        S32 haztype = 10;

        if (type == 11)
        {
            haztype = 18;
        }
        else if (type >= 10 && type < 13)
        {
            haztype = (type == 12) ? 16 : 10;
        }

        NPCHazard* haz = HAZ_Acquire();

        if (!haz)
        {
            return;
        }

        if (!haz->ConfigHelper((en_npchaz)haztype))
        {
            return;
        }

        haz->SetNPCOwner(NULL);
        fxrec->fxdata.hazdata.npchaz = haz;
        haz->flg_hazard &= ~128;

        xVec3 delta = fxrec->pos_B[0] - fxrec->pos_A[0];
        F32 len = delta.length();

        F32 height = fxrec->tym_beg - fxrec->tym_end;

        if (height < 0.01f)
        {
            height = 1.0f;
        }

        haz->pos_hazard = delta;
        haz->Start(&fxrec->pos_A[0], height);
    }

    if (fxrec->fxdata.hazdata.npchaz)
    {
        if (fxrec->fxdata.hazdata.npchaz->typ_hazard != 11)
        {
            fxrec->fxdata.hazdata.npchaz->flg_hazard &= ~0xF000;
        }
        else
        {
            fxrec->flg_stat |= 4;
        }
    }
}

void NCIN_HazTTSteam_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;

        if (fxrec->fxdata.hazdata.npchaz != NULL)
        {
            fxrec->fxdata.hazdata.npchaz->MarkForRecycle();
        }
        return;
    }

    if (fxrec->flg_stat & 2)
    {
        NPCHazard* haz = HAZ_Acquire();

        if (haz == NULL)
        {
            return;
        }

        if (!haz->ConfigHelper(NPC_HAZ_TARTARSTINK))
        {
            return;
        }

        haz->SetNPCOwner(NULL);

        F32 tym = fxrec->tym_end - fxrec->tym_beg;

        if (tym < 1e-5f)
        {
            tym = 0.25f;
        }

        haz->Start(&g_Y3, tym);
        fxrec->fxdata.hazdata.npchaz = haz;
    }

    if (fxrec->fxdata.hazdata.npchaz == NULL)
    {
        fxrec->flg_stat |= 4;
    }
}

void NCIN_HazTTSteam_AR(const zCutsceneMgr* cutsceneMgr, NCINEntry* fxrec, RpAtomic* atomic,
                        RwMatrixTag* matrix, U32 num_1, U32 num_2)
{
}

void NCIN_ArfDogBoom(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;

        if (fxrec->fxdata.hazdata.npchaz != NULL)
        {
            fxrec->fxdata.hazdata.npchaz->MarkForRecycle();
        }
        return;
    }

    if (!(fxrec->flg_stat & 2))
    {
        return;
    }

    NPCHazard* haz = HAZ_Acquire();

    if (haz == NULL)
    {
        return;
    }

    if (!haz->ConfigHelper(NPC_HAZ_PUPPYNUKE))
    {
        return;
    }

    haz->SetNPCOwner(NULL);

    F32 tym = fxrec->tym_end - fxrec->tym_beg;

    tym = (0.5f > tym) ? 0.5f : tym;

    haz->custdata.typical.rad_max = 1.24f;
    haz->Start(&fxrec->pos_A[0], tym);
    fxrec->fxdata.hazdata.npchaz = haz;
}

void NCIN_ShieldPop(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
    }
    else if (fxrec->flg_stat & 2)
    {
        NPCC_BurstBubble(NPC_BURST_SHIELD, &fxrec->pos_A[0]);
        fxrec->flg_stat |= 4;
    }
}

void NCIN_MidFish_Upd(const zCutsceneMgr* mgr, NCINEntry* e, S32 i)
{
    if (i != 0)
    {
        e->flg_stat |= 4;
    }
}

void NCIN_BombTrail_Upd(const zCutsceneMgr* mgr, NCINEntry* e, S32 i)
{
    if (i != 0)
    {
        e->flg_stat |= 4;
    }
}

void NCIN_BombTrail_AR(const zCutsceneMgr* mgr, NCINEntry* e, RpAtomic* a, RwMatrixTag* t, U32 i1, U32 i2)
{
    if (i1 == 0x4)
    {
        zFX_SpawnBubbleTrail((const xVec3*)&t->pos, 0x4);
    }
}

void NCIN_BoneTrail_Upd(const zCutsceneMgr* mgr, NCINEntry* e, S32 i)
{
    if (i != 0)
    {
        e->flg_stat |= 4;
    }
}

void NCIN_BoneTrail_AR(const zCutsceneMgr* mgr, NCINEntry* e, RpAtomic* a, RwMatrixTag* t, U32 i1, U32 i2)
{
    if (i1 == 0x7)
    {
        zFX_SpawnBubbleTrail((const xVec3*)&t->pos, 0x4);
    }
}

void NCIN_HookRecoil_Upd(const zCutsceneMgr* mgr, NCINEntry* e, S32 i)
{
    if (i != 0)
    {
        e->flg_stat |= 4;
    }
}

void NCIN_SpatGlow_Upd(const zCutsceneMgr* mgr, NCINEntry* e, S32 i)
{
    if (i != 0)
    {
        e->flg_stat |= 4;
    }
}

static void get_bone_matrix(xMat4x3& mat, const NCINEntry* fxrec, const RwMatrixTag* animMat)
{
    S32 idx = fxrec->pos_A[1].y;

    mat = *(const xMat4x3*)&animMat[idx];

    if (idx != 0)
    {
        mat.pos += *(const xVec3*)&animMat->pos;
    }
}

void clamp_bone_index(NCINEntry*, RpAtomic*)
{
}

void xCutscene::NoseyClear()
{
    this->NoseySet(0);
}

void xCutscene::NoseySet(XCSNNosey* nosey)
{
    this->cb_nosey = nosey;
}

void NPCCone::TextureSet(RwRaster* raster)
{
    rast_cone = raster;
}

void NPCCone::UVSliceSet(F32 u, F32 v)
{
    this->uv_tip[2] = u;
    this->uv_slice[1] = v;
}

void NPCCone::UVBaseSet(F32 u, F32 v)
{
    this->uv_tip[0] = u;
    this->uv_tip[1] = v;
}

void NPCCone::ColorSet(RwRGBA top, RwRGBA bot)
{
    this->rgba_top = top;
    this->rgba_bot = bot;
}

void NPCCone::RadiusSet(F32 conefloat)
{
    rad_cone = conefloat;
}

void NPARMgmt::KillAll()
{
    this->cnt_active = 0;
}

void NCIN_BubbleTrail_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
        return;
    }

    if (!(fxrec->flg_stat & 2))
    {
        return;
    }

    fxrec->fxdata.customdata.v[0].x = 1e38f;
    fxrec->fxdata.customdata.f[0] = 0.0f;
}

void NCIN_FireSpiral_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
        NPAR_FindParty(NPAR_TYP_TUBESPIRAL)->KillAll();
    }
}

void NCIN_FreezeBreath_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        StopFreezeBreath();
        fxrec->flg_stat |= 4;
    }
    else if (fxrec->flg_stat & 2)
    {
        StartFreezeBreath();
    }
}

void NCIN_PeteBonk(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
    }
    else if (fxrec->flg_stat & 2)
    {
        zNPCRobot_TubeConfetti(&fxrec->pos_A[0]);
        fxrec->flg_stat |= 4;
    }
}

void NCIN_SleepyLamp_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
    }
    else if (fxrec->flg_stat & 2)
    {
        fxrec->fxdata.lampdata.rast = NPCC_FindRWRaster("fx_sleepy_nightlight");
    }
}

void NCIN_SBBNode_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
}

void NCIN_SleepyDRay_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
    }
    else if (fxrec->flg_stat & 2)
    {
        fxrec->fxdata.draydata.rast = NPCC_FindRWRaster("fx_sleepy_beamodeath");
        zFX_SpawnBubbleSlam(&fxrec->pos_A[0], 64, 2.0f * PI, 1.5f, 3.0f);
    }
}

void NCIN_GloveShrapnel_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
    }
    else if (fxrec->flg_stat & 2)
    {
        zShrapnelAsset* shrap = (zShrapnelAsset*)xSTFindAsset(xStrHash("g-love_shrapnel"), NULL);

        if (shrap == NULL)
        {
            fxrec->flg_stat |= 4;
        }
        else
        {
            fxrec->fxdata.shrapdata.shrap = shrap;
        }
    }
    else
    {
        fxrec->flg_stat |= 4;
    }
}

void NCIN_PatBossShrapnel_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
    }
    else if (fxrec->flg_stat & 2)
    {
        zShrapnelAsset* shrap = (zShrapnelAsset*)xSTFindAsset(xStrHash("boss_pa_shrapnel"), NULL);

        if (shrap == NULL)
        {
            fxrec->flg_stat |= 4;
        }
        else
        {
            fxrec->fxdata.shrapdata.shrap = shrap;
        }
    }
    else
    {
        fxrec->flg_stat |= 4;
    }
}

void NCIN_GooLever_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    zParEmitter* emitter = fxrec->fxdata.pardata.emitter;

    if (killit != 0)
    {
        if (emitter != NULL)
        {
            emitter->prop->rate.val[0] = 0.0f;
        }

        fxrec->flg_stat |= 4;
    }
    else if (fxrec->flg_stat & 2)
    {
        emitter = zParEmitterFind("FUDGE_EMIT");

        if (emitter != NULL)
        {
            fxrec->fxdata.pardata.emitter = emitter;
            emitter->prop->rate.val[0] = 0.0f;
        }
        else
        {
            fxrec->flg_stat |= 4;
        }
    }
}

void NCIN_MaryBoom(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;

        if (fxrec->fxdata.hazdata.npchaz != NULL)
        {
            fxrec->fxdata.hazdata.npchaz->MarkForRecycle();
        }
        return;
    }

    if (!(fxrec->flg_stat & 2))
    {
        return;
    }

    NPCHazard* haz = HAZ_Acquire();

    if (haz == NULL)
    {
        return;
    }

    if (!haz->ConfigHelper(NPC_HAZ_TUBELETBLAST))
    {
        return;
    }

    haz->SetNPCOwner(NULL);

    F32 tym = fxrec->tym_end - fxrec->tym_beg;

    haz->Start(&fxrec->pos_A[0], (0.5f > tym) ? 0.5f : tym);
    fxrec->fxdata.hazdata.npchaz = haz;
}

void NCIN_B201HideBelt_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    S32 i;

    if (killit != 0)
    {
        for (i = 0; i < 4; i++)
        {
            xEntShow(fxrec->fxdata.entdata.ent[i]);
        }

        fxrec->flg_stat |= 4;
    }
    else if (fxrec->flg_stat & 2)
    {
        fxrec->fxdata.entdata.ent[0] = (xEnt*)zSceneFindObject(xStrHash("CONVEYOR BELT 03"));
        fxrec->fxdata.entdata.ent[1] = (xEnt*)zSceneFindObject(xStrHash("ROLLER_MECH 07"));
        fxrec->fxdata.entdata.ent[2] = (xEnt*)zSceneFindObject(xStrHash("ROLLER_MECH 08"));
        fxrec->fxdata.entdata.ent[3] = (xEnt*)zSceneFindObject(xStrHash("ROLLER_MECH 09"));

        for (i = 0; i < 4; i++)
        {
            xEntHide(fxrec->fxdata.entdata.ent[i]);
        }
    }
}

void NCIN_B101Shockwave_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
}

void NCIN_LightninBone_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
}

void NCIN_Lightnin2Bones_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
}

void NCIN_FodProd_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;

        if (fxrec->fxdata.hazdata.npchaz != NULL)
        {
            fxrec->fxdata.hazdata.npchaz->MarkForRecycle();
        }
        return;
    }

    if (!(fxrec->flg_stat & 2))
    {
        return;
    }

    NPCHazard* haz = HAZ_Acquire();

    if (haz == NULL)
    {
        return;
    }

    if (!haz->ConfigHelper(NPC_HAZ_CATTLEPROD))
    {
        return;
    }

    haz->SetNPCOwner(NULL);

    F32 tym = fxrec->tym_end - fxrec->tym_beg;

    tym = (0.5f > tym) ? 0.5f : tym;

    if (tym < 2.0f)
    {
        haz->custdata.typical.rad_min = 0.01f;
    }
    else
    {
        haz->custdata.typical.rad_min = 0.15f;
    }

    haz->custdata.typical.rad_max = 0.15f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->Start(&fxrec->pos_A[0], tym);
    fxrec->fxdata.hazdata.npchaz = haz;
}

void NCIN_FodProdBone_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;

        if (fxrec->fxdata.hazdata.npchaz != NULL)
        {
            fxrec->fxdata.hazdata.npchaz->MarkForRecycle();
        }
        return;
    }

    if (!(fxrec->flg_stat & 2))
    {
        return;
    }

    NPCHazard* haz = HAZ_Acquire();

    if (haz == NULL)
    {
        return;
    }

    if (!haz->ConfigHelper(NPC_HAZ_CATTLEPROD))
    {
        return;
    }

    haz->SetNPCOwner(NULL);

    F32 tym = fxrec->tym_end - fxrec->tym_beg;

    tym = (0.5f > tym) ? 0.5f : tym;

    if (tym < 2.0f)
    {
        haz->custdata.typical.rad_min = 0.01f;
    }
    else
    {
        haz->custdata.typical.rad_min = 0.15f;
    }

    haz->custdata.typical.rad_max = 0.15f;
    haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    haz->Start(&fxrec->pos_A[0], tym);
    fxrec->fxdata.hazdata.npchaz = haz;
}

void NCIN_OilHazard(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
}

void NCIN_BubWipe(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
}

void NCIN_PatBossShrapnel_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic* model,
                             RwMatrixTag* animMat, U32 animIndex, U32 dataIndex)
{
    S32 ifx = fxrec->pos_A[1].x;

    if (animIndex == ifx)
    {
        zShrapnel_CinematicInit(fxrec->fxdata.shrapdata.shrap, model, animMat, NULL, NULL);
    }
}

void NCIN_GooLever_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                      U32 animIndex, U32 dataIndex)
{
    S32 ifx = fxrec->pos_A[1].x;

    if (animIndex != ifx)
    {
        return;
    }

    S32 ify = fxrec->pos_A[1].y;
    F32 amt = 400.0f * -animMat[ify].at.y;

    if (amt < 0.0f)
    {
        amt = 0.0f;
    }

    if (fxrec->fxdata.pardata.emitter != NULL)
    {
        fxrec->fxdata.pardata.emitter->prop->rate.val[0] = amt;
    }
}

void NCIN_GloveShrapnel_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic* model,
                           RwMatrixTag* animMat, U32 animIndex, U32 dataIndex)
{
    S32 ifx = fxrec->pos_A[1].x;

    if (animIndex == ifx)
    {
        zShrapnel_CinematicInit(fxrec->fxdata.shrapdata.shrap, model, animMat, NULL, NULL);
        zShrapnel_CinematicInit(fxrec->fxdata.shrapdata.shrap, model, animMat, NULL, NULL);
        zShrapnel_CinematicInit(fxrec->fxdata.shrapdata.shrap, model, animMat, NULL, NULL);
    }
}

void NCIN_FreezeBreath_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag*, U32 a,
                          U32 b)
{
}

void NCIN_LightninBone_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                          U32 animIndex, U32 dataIndex)
{
    xVec3 begpos;
    xVec3 endpos;
    S32 ifx = fxrec->pos_A[1].x;

    if (animIndex != ifx)
    {
        return;
    }

    S32 ify = fxrec->pos_A[1].y;
    const xMat3x3* bone = (const xMat3x3*)&animMat[ify];

    xMat3x3RMulVec(&begpos, bone, &fxrec->pos_A[0]);
    xVec3AddTo(&begpos, (const xVec3*)&animMat[ify].pos);

    if (ify != 0)
    {
        xVec3AddTo(&begpos, (const xVec3*)&animMat->pos);
    }

    xMat3x3RMulVec(&endpos, bone, &fxrec->fxdata.arcdata.endPos);
    xVec3AddTo(&endpos, &begpos);
    zLightningModifyEndpoints(fxrec->fxdata.arcdata.lightning, &begpos, &endpos);
}

void NCIN_HookRecoil_AR(const zCutsceneMgr* csnmgr, NCINEntry*, RpAtomic* model,
                        RwMatrixTag* animMat, U32 animIndex, U32 dataIndex)
{
    F32 tym = csnmgr->csn->Time;
    S32 idx;

    if (tym < 7.3666672f)
    {
        idx = 5;
    }
    else if (tym < 12.8f)
    {
        idx = 1;
    }
    else
    {
        idx = 2;
    }

    if (animIndex != idx)
    {
        return;
    }

    U32 nbones = iModelNumBones(model);

    for (U32 i = 1; i < nbones; i++)
    {
        xVec3 pos = *(const xVec3*)&animMat[i].pos + *(const xVec3*)&animMat->pos;
        zFX_SpawnBubbleTrail(&pos, 1);
    }
}

void NCIN_FodProd_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                     U32 animIndex, U32 dataIndex)
{
    if (animIndex != 0)
    {
        return;
    }

    static const xVec3 vec_offset = { 0.2f, 0.0f, 0.5f };

    const xMat4x3* bone = (const xMat4x3*)&animMat[5];
    NPCHazard* haz = fxrec->fxdata.hazdata.npchaz;

    if (haz == NULL)
    {
        return;
    }

    xVec3 pos = bone->pos;

    pos += bone->right * vec_offset.x;
    pos += bone->up * vec_offset.y;
    pos += bone->at * vec_offset.z;
    pos += *(const xVec3*)&animMat->pos;

    haz->PosSet(&pos);
}

void NCIN_SpatGlow_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic* model, RwMatrixTag* animMat,
                      U32 animIndex, U32 dataIndex)
{
    iColor_tag color;
    S32 ifx = fxrec->pos_A[1].x;

    if (animIndex != ifx)
    {
        return;
    }

    S32 ify = fxrec->pos_A[1].y;

    zShrapnel_CinematicInit(fxrec->fxdata.shrapdata.shrap, model, animMat, NULL, NULL);

    color.r = (U8)(S32)fxrec->pos_B[0].x;
    color.g = (U8)(S32)fxrec->pos_B[0].y;
    color.b = (U8)(S32)fxrec->pos_B[0].z;
    color.a = (U8)(S32)fxrec->pos_B[1].x;

    xFXAuraAdd(fxrec, (xVec3*)&animMat[ify].pos, &color, fxrec->pos_B[1].y);
}

void NCIN_MidFish_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                     U32 animIndex, U32 dataIndex)
{
    static S32 g_idx_handbone[] = { 11, 16, 27, 36, 41, -1 };

    S32* idx;

    if (animIndex == 0)
    {
        for (S32 i = 0; i < 24; i += 2)
        {
            xVec3 pos = *(const xVec3*)&animMat[i].pos + *(const xVec3*)&animMat->pos;
            zFX_SpawnBubbleTrail(&pos, 1);
        }
    }
    else if (animIndex == 2 || animIndex == 3)
    {
        for (idx = g_idx_handbone; *idx >= 0; idx++)
        {
            xVec3 pos = *(const xVec3*)&animMat[*idx].pos + *(const xVec3*)&animMat->pos;
            zFX_SpawnBubbleTrail(&pos, 1);
        }
    }
}

void NCIN_FireSpiral_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag*, U32 a,
                        U32 b)
{
}

void NCIN_SBBNode_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag*, U32 a, U32 b)
{
}

void NCIN_Lightnin2Bones_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                            U32 animIndex, U32 dataIndex)
{
    xVec3 begpos;
    xVec3 endpos;
    zLightning* lyt = fxrec->fxdata.arcdata.lightning;

    if (lyt == NULL)
    {
        return;
    }

    S32 ifx = fxrec->pos_A[1].x;

    if (animIndex == ifx)
    {
        S32 ify = fxrec->pos_A[1].y;
        const xMat3x3* bone = (const xMat3x3*)&animMat[ify];

        xMat3x3RMulVec(&begpos, bone, &fxrec->pos_A[0]);
        xVec3AddTo(&begpos, (const xVec3*)&animMat[ify].pos);

        if (ify != 0)
        {
            xVec3AddTo(&begpos, (const xVec3*)&animMat->pos);
        }

        zLightningModifyEndpoints(lyt, &begpos, &lyt->func.endPoint[1]);
    }

    S32 ifx2 = fxrec->pos_B[1].x;

    if (animIndex == ifx2)
    {
        S32 ify2 = fxrec->pos_B[1].y;
        const xMat3x3* bone = (const xMat3x3*)&animMat[ify2];

        xMat3x3RMulVec(&endpos, bone, &fxrec->pos_B[0]);
        xVec3AddTo(&endpos, (const xVec3*)&animMat[ify2].pos);

        if (ify2 != 0)
        {
            xVec3AddTo(&endpos, (const xVec3*)&animMat->pos);
        }

        zLightningModifyEndpoints(lyt, &lyt->func.endPoint[0], &endpos);
    }
}

void NCIN_FodProdBone_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag*, U32 a,
                         U32 b)
{
}

void NCIN_SleepyDRay_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag*, U32 a,
                        U32 b)
{
}

void NCIN_BubbleTrail_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag*, U32 a,
                         U32 b)
{
}

void NCIN_TTGunSmoke_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag*, U32 a,
                        U32 b)
{
}

void NCIN_SleepyLamp_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag*, U32 a,
                        U32 b)
{
}

void NCIN_EntityBonePar_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag*, U32 a,
                           U32 b)
{
}

void zNPCFXStartup()
{
    static NCINBeNosey nozey_npc_cinematics;

    g_noz_ncin = &nozey_npc_cinematics;
}

void zNPCFXCutsceneDone(const xScene*, F32, const zCutsceneMgr* csnmgr)
{
}

void zNPCFXCutscene(const xScene*, F32, const zCutsceneMgr* csnmgr)
{
}

S32 zNPCFXCutscenePrep(const xScene*, F32, const zCutsceneMgr* csnmgr)
{
    return 0;
}

void NCINBeNosey::UpdatedAnimated(RpAtomic* model, RwMatrixTag* animMat, U32 animIndex,
                                  U32 dataIndex)
{
    zCutsceneMgr* csnmgr = (zCutsceneMgr*)this->use_csnmgr;
    NCINEntry* fxtab = this->use_fxtab;

    if (csnmgr == NULL || fxtab == NULL)
    {
        return;
    }

    while (fxtab->typ_ncinfx != NCIN_FXTYP_UNKNOWN)
    {
        NCINEntry* fxrec = fxtab;

        fxtab++;

        if (fxrec->cb_fxanim != NULL && (fxrec->flg_stat & 1))
        {
            fxrec->cb_fxanim(csnmgr, fxrec, model, animMat, animIndex, dataIndex);
        }
    }
}
