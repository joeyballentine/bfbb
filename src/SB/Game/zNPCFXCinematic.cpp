#include "xVec3.h"
#include "xMath3.h"
#include "xMathInlines.h"
#include "xParEmitter.h"

#include "zNPCFXCinematic.h"
#include "zParPTank.h"
#include "zNPCTypeBossSB2.h"
#include "zNPCSupport.h"
#include "xString.h"
#include "xDebug.h"
#include "xstransvc.h"
#include "zScene.h"
#include "xEnt.h"
#include "iModel.h"
#include "rpskin.h"

#include <types.h>
#include <stdio.h>
#include <PowerPC_EABI_Support\MSL_C\MSL_Common\cmath>

// These structs were used in deadstripped functions.
// This function is here to force the symbols to be linked.
//
// The target opens .rodata with 11 unreferenced all-zero
// templates (0x148 total) that offset every later .rodata
// relocation. Same idiom as zVar.cpp.
void __deadstripped_zNPCFXCinematic()
{
    const char _405[0x0C] = {};
    const char _406[0x0C] = {};
    const char _410[0x0C] = {};
    const char _441[0x0C] = {};

    const char _612[0x28] = {};
    const char _613[0x28] = {};
    const char _614[0x28] = {};
    const char _615[0x28] = {};
    const char _616[0x28] = {};
    const char _617[0x28] = {};
    const char _618[0x28] = {};
}

static NCINBeNosey* g_noz_ncin;

void get_bone_matrix(xMat4x3& mat, const NCINEntry* fxrec, const RwMatrixTag* animMat);
void clamp_bone_index(NCINEntry* fxrec, RpAtomic* model);

// belongs in xDebug.h next to the other xDebugAddTweak overloads
void xDebugAddTweak(const char*, xVec3*, const tweak_callback*, void*, U32);

xVec3* LERP(F32 t, xVec3* dst, const xVec3* a, const xVec3* b);
xVec3* SMOOTH(F32 t, xVec3* dst, const xVec3* a, const xVec3* b);

void EmitFreezeBreath(xVec3* pos, xVec3* vel, F32 dt, F32 elapsed, F32 total);
void NPAR_EmitTubeSpiralCin(const xVec3* pos, const xVec3* vel, F32 dt);

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

        xParEmitterAsset* a = fxrec->fxdata.pardata.emitter->tasset;

        if (a == NULL || a->emit_type != 15)
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
            float_accum = 0.1f + a->e_entbone.bone;
        }

        fxrec->pos_B[0] = 1e9f;
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

        xParEmitterAsset* a = fxrec->fxdata.pardata.emitter->tasset;

        if (a == NULL || a->emit_type != 15)
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
            float_accum = 0.1f + a->e_entbone.bone;
        }

        fxrec->pos_B[0] = 1e9f;
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

        xParEmitterAsset* a = fxrec->fxdata.pardata.emitter->tasset;

        if (a == NULL || a->emit_type != 15)
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
            float_accum = 0.1f + a->e_entbone.bone;
        }

        fxrec->pos_B[0] = 1e9f;
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

        xParEmitterAsset* a = fxrec->fxdata.pardata.emitter->tasset;

        if (a == NULL || a->emit_type != 15)
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
            float_accum = 0.1f + a->e_entbone.bone;
        }

        fxrec->pos_B[0] = 1e9f;
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

        xParEmitterAsset* a = fxrec->fxdata.pardata.emitter->tasset;

        if (a == NULL || a->emit_type != 15)
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
            float_accum = 0.1f + a->e_entbone.bone;
        }

        fxrec->pos_B[0] = 1e9f;
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

        xParEmitterAsset* a = fxrec->fxdata.pardata.emitter->tasset;

        if (a == NULL || a->emit_type != 15)
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
            float_accum = 0.1f + a->e_entbone.bone;
        }

        fxrec->pos_B[0] = 1e9f;
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

        xParEmitterAsset* a = fxrec->fxdata.pardata.emitter->tasset;

        if (a == NULL || a->emit_type != 15)
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
            float_accum = 0.1f + a->e_entbone.bone;
        }

        fxrec->pos_B[0] = 1e9f;
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

        xParEmitterAsset* a = fxrec->fxdata.pardata.emitter->tasset;

        if (a == NULL || a->emit_type != 15)
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
            float_accum = 0.1f + a->e_entbone.bone;
        }

        fxrec->pos_B[0] = 1e9f;
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

        xParEmitterAsset* a = fxrec->fxdata.pardata.emitter->tasset;

        if (a == NULL || a->emit_type != 15)
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
            float_accum = 0.1f + a->e_entbone.bone;
        }

        fxrec->pos_B[0] = 1e9f;
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

        xParEmitterAsset* a = fxrec->fxdata.pardata.emitter->tasset;

        if (a == NULL || a->emit_type != 15)
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
            float_accum = 0.1f + a->e_entbone.bone;
        }

        fxrec->pos_B[0] = 1e9f;
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

        xParEmitterAsset* a = fxrec->fxdata.pardata.emitter->tasset;

        if (a == NULL || a->emit_type != 15)
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
            float_accum = 0.1f + a->e_entbone.bone;
        }

        fxrec->pos_B[0] = 1e9f;
    }
}

void NCINBeNosey::CanRenderNow()
{
    zCutsceneMgr* csnmgr = (zCutsceneMgr*)this->use_csnmgr;
    NCINEntry* fxtab = this->use_fxtab;
    NCINEntry* nextrec;

    if (csnmgr == NULL)
    {
        return;
    }

    if (fxtab == NULL)
    {
        return;
    }

    nextrec = fxtab;

    while (nextrec->typ_ncinfx != NCIN_FXTYP_UNKNOWN)
    {
        NCINEntry* fxrec = nextrec;

        nextrec++;

        if (fxrec->cb_fxrend != NULL && (fxrec->flg_stat & 1))
        {
            fxrec->cb_fxrend(csnmgr, fxrec);
        }
    }
}

void NCINBeNosey::UpdatedAnimated(RpAtomic* model, RwMatrixTag* animMat, U32 animIndex,
                                  U32 dataIndex)
{
    zCutsceneMgr* csnmgr = (zCutsceneMgr*)this->use_csnmgr;
    NCINEntry* fxtab = this->use_fxtab;
    NCINEntry* nextrec;

    if (csnmgr == NULL)
    {
        return;
    }

    if (fxtab == NULL)
    {
        return;
    }

    nextrec = fxtab;

    while (nextrec->typ_ncinfx != NCIN_FXTYP_UNKNOWN)
    {
        NCINEntry* fxrec = nextrec;

        nextrec++;

        if (fxrec->cb_fxanim != NULL && (fxrec->flg_stat & 1))
        {
            fxrec->cb_fxanim(csnmgr, fxrec, model, animMat, animIndex, dataIndex);
        }
    }
}

void zNPCFXStartup()
{
    static NCINBeNosey nozey_npc_cinematics;

    g_noz_ncin = &nozey_npc_cinematics;
}

void zNPCFXShutdown()
{
}

// TODO: needs g_cutmap and the per-cutscene NCINEntry tables, which do not
// exist in this translation unit yet.
static NCINEntry* zNPCFXCutscenePickTable(const zCutsceneMgr* csnmgr)
{
    return NULL;
}

S32 zNPCFXCutscenePrep(const xScene*, F32, const zCutsceneMgr* csnmgr)
{
    NCINEntry* fxtab;
    NCINEntry* nextrec;
    char tweakBase[128];
    char tweakName[128];

    fxtab = zNPCFXCutscenePickTable(csnmgr);

    if (fxtab == NULL)
    {
        return 0;
    }

    if (csnmgr != NULL && csnmgr->csn != NULL)
    {
        csnmgr->csn->NoseyClear();
    }

    g_noz_ncin->Init(csnmgr, fxtab, 3);

    nextrec = fxtab;

    while (nextrec->typ_ncinfx != NCIN_FXTYP_UNKNOWN)
    {
        NCINEntry* fxrec = nextrec;

        nextrec++;

        fxrec->flg_stat = 0;
        fxrec->flg_stat = 2 | 8;
        memset(&fxrec->fxdata, 0, sizeof(NCINData));
    }

    nextrec = fxtab;

    while (nextrec->typ_ncinfx != NCIN_FXTYP_UNKNOWN)
    {
        NCINEntry* fxrec = nextrec;

        nextrec++;

        sprintf(tweakBase, "CinematicFX|%s", fxrec->twk_name);

        sprintf(tweakName, "%s|posA[0]", tweakBase);
        xDebugAddTweak(tweakName, &fxrec->pos_A[0], NULL, NULL, 2);

        sprintf(tweakName, "%s|posA[1]", tweakBase);
        xDebugAddTweak(tweakName, &fxrec->pos_A[1], NULL, NULL, 2);

        sprintf(tweakName, "%s|posB[0]", tweakBase);
        xDebugAddTweak(tweakName, &fxrec->pos_B[0], NULL, NULL, 2);

        sprintf(tweakName, "%s|posB[1]", tweakBase);
        xDebugAddTweak(tweakName, &fxrec->pos_B[1], NULL, NULL, 2);

        sprintf(tweakName, "%s|tym_beg", tweakBase);
        xDebugAddTweak(tweakName, &fxrec->tym_beg, 0.0f, 1e9f, NULL, NULL, 2);

        sprintf(tweakName, "%s|tym_end", tweakBase);
        xDebugAddTweak(tweakName, &fxrec->tym_end, 0.0f, 1e9f, NULL, NULL, 2);
    }

    return 1;
}

void NCINBeNosey::Init(const zCutsceneMgr* m, NCINEntry* e, S32 i)
{
    this->use_csnmgr = m;
    this->use_fxtab = e;
    this->flg_nosey = i;
}

void zNPCFXCutsceneDone(const xScene*, F32, const zCutsceneMgr* csnmgr)
{
    NCINEntry* fxtab;
    NCINEntry* nextrec;
    NCINEntry* fxrec;

    fxtab = zNPCFXCutscenePickTable(csnmgr);

    if (fxtab == NULL)
    {
        return;
    }

    if (csnmgr != NULL && csnmgr->csn != NULL)
    {
        csnmgr->csn->NoseyClear();
    }

    g_noz_ncin->Done();

    nextrec = fxtab;

    while (nextrec->typ_ncinfx != NCIN_FXTYP_UNKNOWN)
    {
        fxrec = nextrec;
        nextrec++;

        if (fxrec->flg_stat & 4)
        {
            continue;
        }

        if (!(fxrec->flg_stat & 1))
        {
            continue;
        }

        fxrec->cb_fxupd((zCutsceneMgr*)csnmgr, fxrec, 1);
        fxrec->flg_stat &= ~1;
    }
}

void NCINBeNosey::Done()
{
    this->use_csnmgr = 0;
    this->use_fxtab = 0;
    this->flg_nosey = 0;
}

void zNPCFXCutscene(const xScene*, F32, const zCutsceneMgr* csnmgr)
{
    NCINEntry* fxtab;
    xCutscene* csn;
    S32 need_animated;
    S32 need_render;
    NCINEntry* nextrec;
    NCINEntry* fxrec;
    S32 flags;

    csn = csnmgr->csn;

    fxtab = zNPCFXCutscenePickTable(csnmgr);

    if (fxtab == NULL)
    {
        return;
    }

    nextrec = fxtab;
    need_animated = 0;
    need_render = 0;

    while (nextrec->typ_ncinfx != NCIN_FXTYP_UNKNOWN)
    {
        fxrec = nextrec;
        nextrec++;

        if (csn->Time < fxrec->tym_beg)
        {
            continue;
        }

        if (fxrec->flg_stat & 4)
        {
            continue;
        }

        if (csn->Time > fxrec->tym_end && (fxrec->flg_stat & 1))
        {
            fxrec->cb_fxupd((zCutsceneMgr*)csnmgr, fxrec, 1);
            fxrec->flg_stat &= ~1;
            continue;
        }

        fxrec->flg_stat |= 1;
        fxrec->cb_fxupd((zCutsceneMgr*)csnmgr, fxrec, 0);

        if (!(fxrec->flg_stat & 2))
        {
            fxrec->flg_stat &= ~8;
        }

        fxrec->flg_stat &= ~2;

        if (fxrec->flg_stat & 4)
        {
            fxrec->flg_stat &= ~1;
        }

        if (fxrec->flg_stat & 1)
        {
            if (fxrec->cb_fxanim != NULL)
            {
                need_animated++;
            }

            if (fxrec->cb_fxrend != NULL)
            {
                need_render++;
            }
        }
    }

    flags = 0;

    if (need_animated != 0)
    {
        flags |= 2;
    }

    if (need_render != 0)
    {
        flags |= 1;
    }

    if (flags != 0)
    {
        g_noz_ncin->flg_nosey = flags;
        csn->NoseySet(g_noz_ncin);
    }
    else
    {
        csn->NoseyClear();
    }
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

void NCIN_BubWipe(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    xMat4x3* mat;
    xVec3* pos;
    xVec3* vel;
    xVec3* pp;
    xVec3* vp;
    S32 i;

    static const xVec3 scl_wall = { 3.0f, 3.0f, 3.0f };
    static const xVec3 vel_wall = { 1.0f, 0.5f, 0.5f };
    static xMat4x3 mat_fake;

    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
        return;
    }

    mat = &mat_fake;

    mat->pos = fxrec->pos_A[0];
    mat->at = fxrec->pos_A[1];
    mat->up = fxrec->pos_B[0];
    mat->right = fxrec->pos_B[1];

    pos = (xVec3*)xMemPushTemp(2 * 50 * sizeof(xVec3));
    vel = pos + 50;

    xVec3 vec_infront = mat->at * 1.2f;

    F32 sx = scl_wall.x;
    F32 sy = scl_wall.y;
    F32 sz = scl_wall.z;
    F32 vx = vel_wall.x;
    F32 vy = vel_wall.y;
    F32 vz = vel_wall.z;

    pp = pos;
    vp = vel;

    for (i = 0; i < 50; i++)
    {
        pp->x = mat->pos.x + (xurand() - 0.5f) + sx * (xurand() - 0.5f);
        pp->y = mat->pos.y + (xurand() - 0.5f) + sy * (xurand() - 0.5f);
        pp->z = mat->pos.z + (xurand() - 0.5f) + sz * (xurand() - 0.5f);

        *pp += vec_infront;

        vp->x = vx * (xurand() - 0.5f);
        vp->y = vy * (xurand() - 0.5f);
        vp->z = vz * (xurand() - 0.5f);

        pp++;
        vp++;
    }

    zParPTankSpawnBubbles(pos, vel, 50, 1.0f);
    xMemPopTemp(pos);
}

void NCIN_BubTrailBone_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                          U32 num_1, U32 num_2)
{
    S32 ifx = fxrec->pos_A[1].x;
    S32 idx_boneInterest = fxrec->pos_A[1].y;

    if (num_1 != ifx)
    {
        return;
    }

    xVec3 pos_emit = *(const xVec3*)&animMat->pos;

    if (idx_boneInterest > 0)
    {
        pos_emit += *(const xVec3*)&animMat[idx_boneInterest].pos;
    }

    zFX_SpawnBubbleTrail(&pos_emit, 1);
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

void NCIN_Zapper(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    _tagLightningAdd addInfo;

    if (killit != 0)
    {
        fxrec->flg_stat |= 4;

        switch (fxrec->typ_ncinfx)
        {
        case NCIN_FXTYP_JELLYLIGHT_01:
        case NCIN_FXTYP_MONCLOUD:
            if (fxrec->fxdata.lytdata.lyt_zap != NULL)
            {
                zLightningKill(fxrec->fxdata.lytdata.lyt_zap);
            }

            fxrec->fxdata.lytdata.lyt_zap = NULL;
            break;
        }
        return;
    }

    if (fxrec->flg_stat & 2)
    {
        memset(&addInfo, 0, sizeof(_tagLightningAdd));

        switch (fxrec->typ_ncinfx)
        {
        case NCIN_FXTYP_JELLYLIGHT_01:
            NPCC_MakeLightningInfo(NPC_LYT_JELLYFISH, &addInfo);
            break;
        case NCIN_FXTYP_MONCLOUD:
            NPCC_MakeLightningInfo(NPC_LYT_CLOUDZAP, &addInfo);
            break;
        }

        addInfo.start = &fxrec->pos_A[0];
        addInfo.end = &fxrec->pos_A[1];
        addInfo.time = 0.5f + (fxrec->tym_end - fxrec->tym_beg);

        fxrec->fxdata.lytdata.lyt_zap = zLightningAdd(&addInfo);
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

void NCIN_HammerStreak_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                          U32 animIndex, U32 dataIndex)
{
    S32 i;

    if (animIndex != 2)
    {
        return;
    }

    U32 idx_hamBone[4] = { 23, 26, 25, 22 };
    xMat4x3* mat_root = (xMat4x3*)animMat;
    xMat4x3* mat_hamBone[4];

    for (i = 0; i < 4; i++)
    {
        mat_hamBone[i] = (xMat4x3*)&animMat[idx_hamBone[i]];
    }

    xVec3 left = mat_hamBone[0]->pos + mat_root->pos;
    xVec3 right = mat_hamBone[1]->pos + mat_root->pos;
    xVec3 top = mat_hamBone[2]->pos + mat_root->pos;
    xVec3 bottom = mat_hamBone[3]->pos + mat_root->pos;

    NCINStrk* strkdat = &fxrec->fxdata.strkdata;

    xFXStreakUpdate(strkdat->sid_horz, &left, &right);
    xFXStreakUpdate(strkdat->sid_vert, &top, &bottom);
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

void NCIN_HazProjShoot(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
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
        en_npchaz use_haztyp = NPC_HAZ_TARTARPROJ;

        switch (fxrec->typ_ncinfx)
        {
        case NCIN_FXTYP_TARTARSHOOT:
            use_haztyp = NPC_HAZ_TARTARPROJ;
            break;
        case NCIN_FXTYP_BONESHOOT:
            use_haztyp = NPC_HAZ_ARFBONE;
            break;
        case NCIN_FXTYP_OILSHOOT:
            use_haztyp = NPC_HAZ_OILBUBBLE;
            break;
        }

        NPCHazard* haz = HAZ_Acquire();

        if (haz == NULL)
        {
            return;
        }

        if (!haz->ConfigHelper(use_haztyp))
        {
            return;
        }

        haz->SetNPCOwner(NULL);
        haz->custdata.collide.flg_collide = 0;

        xVec3 pos_beg = fxrec->pos_A[0];
        xVec3 pos_end = fxrec->pos_A[1];
        xVec3 dir_shoot = pos_beg - pos_end;

        F32 dst_shoot = dir_shoot.length();
        F32 tym = fxrec->tym_end - fxrec->tym_beg;

        if (tym < 1e-5f)
        {
            tym = 0.25f;
        }

        haz->custdata.tartar.pos_tgt = pos_end;
        haz->Start(&fxrec->pos_A[0], tym);
        fxrec->fxdata.hazdata.npchaz = haz;
    }

    if (fxrec->fxdata.hazdata.npchaz == NULL)
    {
        fxrec->flg_stat |= 4;
    }
    else if (fxrec->fxdata.hazdata.npchaz->typ_hazard != NPC_HAZ_TARTARSPILL)
    {
        fxrec->fxdata.hazdata.npchaz->flg_hazard &= ~0x60000;
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

void NCIN_HazTTSteam_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                        U32 animIndex, U32 dataIndex)
{
    S32 idx_boneSign;
    RwMatrixTag* mat_bone;
    NPCHazard* haz;

    U32 ifx = fxrec->pos_A[1].x;

    if (animIndex != ifx)
    {
        return;
    }

    xVec3 vec_offset = fxrec->pos_A[0];
    xVec3 pos;

    idx_boneSign = fxrec->pos_A[1].y;

    if (idx_boneSign > 0)
    {
        mat_bone = &animMat[idx_boneSign];

        pos = *(const xVec3*)&mat_bone->pos;
        pos += *(const xVec3*)&mat_bone->right * vec_offset.x;
        pos += *(const xVec3*)&mat_bone->up * vec_offset.y;
        pos += *(const xVec3*)&mat_bone->at * vec_offset.z;
        pos += *(const xVec3*)&animMat->pos;
    }
    else
    {
        pos = *(const xVec3*)&animMat->pos;
        pos += *(const xVec3*)&animMat->right * vec_offset.x;
        pos += *(const xVec3*)&animMat->up * vec_offset.y;
        pos += *(const xVec3*)&animMat->at * vec_offset.z;
    }

    haz = fxrec->fxdata.hazdata.npchaz;
    haz->pos_hazard = pos;
}

void NCIN_TTGunSmoke_AR(const zCutsceneMgr* csnmgr, NCINEntry* fxrec, RpAtomic*,
                        RwMatrixTag* animMat, U32 animIndex, U32 dataIndex)
{
    S32 idx_boneGun;
    xMat4x3* mat_bone;
    F32 spd_blow;
    xCutscene* csn;
    F32 rat_blow;

    U32 idx_roboAnim = fxrec->pos_A[1].x;

    if (animIndex != idx_roboAnim)
    {
        return;
    }

    idx_boneGun = fxrec->pos_A[1].y;

    xVec3 vec_offset = fxrec->pos_A[0];
    xVec3 pos_smoke;

    mat_bone = (xMat4x3*)&animMat[idx_boneGun];

    pos_smoke = mat_bone->pos;
    pos_smoke += mat_bone->right * vec_offset.x;
    pos_smoke += mat_bone->up * vec_offset.y;
    pos_smoke += mat_bone->at * vec_offset.z;
    pos_smoke += *(const xVec3*)&animMat->pos;

    F32 tym_blow[2] = { 22.000002f, 23.166667f };

    csn = csnmgr->csn;

    if (csn->Time < tym_blow[0])
    {
        spd_blow = 0.0f;
    }
    else if (csn->Time > tym_blow[1])
    {
        spd_blow = 0.0f;
    }
    else
    {
        rat_blow = (csn->Time - tym_blow[0]) / (tym_blow[1] - tym_blow[0]);
        spd_blow = LERP(ARCH3(1.0f - CLAMP(rat_blow, 0.0f, 1.0f)), 0.5f, 15.5f);
    }

    spd_blow += spd_blow * (0.25f * (xurand() - 0.5f));

    xVec3 dir_blow = fxrec->pos_B[0];
    xVec3 vel_smoke;

    vel_smoke = g_Y3 * 2.0f;
    vel_smoke += g_Y3 * (2.0f * (xurand() - 0.5f));
    vel_smoke += dir_blow * spd_blow;

    NPAR_EmitTarTarSmoke(&pos_smoke, &vel_smoke);
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

void NCIN_SleepyLamp_AR(const zCutsceneMgr* csnmgr, NCINEntry* fxrec, RpAtomic*,
                        RwMatrixTag* animMat, U32 animIndex, U32 dataIndex)
{
    if (animIndex == 2)
    {
        xVec3* pos_robo = &fxrec->fxdata.lampdata.pos_robo;

        *pos_robo = *(const xVec3*)&animMat->pos;
        pos_robo->y += -2.0f;
        return;
    }

    if (animIndex != 3)
    {
        return;
    }

    xVec3* pos_robo = &fxrec->fxdata.lampdata.pos_robo;

    NPCCone cone;
    xVec3 pos_lamp = *(const xVec3*)&animMat->pos;
    xVec3 rgb_peace = { 1.0f, 1.0f, 0.63f };
    xVec3 rgb_anger = { 0.5f, 0.0f, 0.0f };
    xVec3 rgb_current;
    RwRGBA rgba_top;
    RwRGBA rgba_bot;
    F32 pct;

    static const F32 tym_anger[4] = { 4.8333335f, 5.3333335f, 11.000001f, 11.666667f };

    F32 tym = csnmgr->csn->Time;

    if (tym < tym_anger[0])
    {
        rgb_current = rgb_peace;
    }
    else if (tym > tym_anger[3])
    {
        rgb_current = rgb_peace;
    }
    else if (tym > tym_anger[1] && tym < tym_anger[2])
    {
        rgb_current = rgb_anger;
    }
    else
    {
        if (tym < tym_anger[1])
        {
            pct = (tym - tym_anger[0]) / (tym_anger[1] - tym_anger[0]);
        }
        else
        {
            pct = 1.0f - (tym - tym_anger[2]) / (tym_anger[3] - tym_anger[2]);
        }

        SMOOTH(CLAMP(pct, 0.0f, 1.0f), &rgb_current, &rgb_peace, &rgb_anger);
    }

    U8 red = (U8)(255.0f * rgb_current.x);
    U8 green = (U8)(255.0f * rgb_current.y);
    U8 blue = (U8)(255.0f * rgb_current.z);

    rgba_top.red = red;
    rgba_top.green = green;
    rgba_top.blue = blue;
    rgba_top.alpha = 96;

    rgba_bot.red = red;
    rgba_bot.green = green;
    rgba_bot.blue = blue;
    rgba_bot.alpha = 16;

    memset(&cone, 0, sizeof(NPCCone));

    cone.RadiusSet(3.5f);
    cone.ColorSet(rgba_top, rgba_bot);
    cone.UVBaseSet(0.5f, 0.0f);
    cone.UVSliceSet(1.0f, 1.0f);
    cone.TextureSet(NULL);
    cone.RenderCone(&pos_lamp, pos_robo);
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

void NCIN_SleepyDRay_AR(const zCutsceneMgr* csnmgr, NCINEntry* fxrec, RpAtomic*,
                        RwMatrixTag* animMat, U32 animIndex, U32 dataIndex)
{
    RwRGBA rgba_top = { 255, 255, 255, 255 };
    RwRGBA rgba_bot = { 255, 255, 255, 255 };

    static const F32 uv_scroll_dray[2] = { 0.0f, -4.55f };
    static const F32 uv_slice_dray[2] = { 0.25f, 0.25f };

    xVec3 pos_trail;

    if (animIndex == 0)
    {
        const xVec3* pos_beam = &fxrec->pos_A[0];

        for (S32 i = 0; i < 6; i++)
        {
            LERP(i / 6.0f, &pos_trail, pos_beam, (const xVec3*)&animMat->pos);
            zFX_SpawnBubbleTrail(&pos_trail, 4);
        }

        zFX_SpawnBubbleTrail((const xVec3*)&animMat->pos, 4);
        return;
    }

    if (animIndex != 2)
    {
        return;
    }

    xVec3 pos_top = *(const xVec3*)&animMat[16].pos + *(const xVec3*)&animMat->pos;

    F32 tym = csnmgr->csn->Time - fxrec->tym_beg;
    F32 uv_u = tym * uv_scroll_dray[0];
    F32 uv_v = tym * uv_scroll_dray[1];

    NPCCone cone;

    memset(&cone, 0, sizeof(NPCCone));

    cone.RadiusSet(0.5f);
    cone.ColorSet(rgba_top, rgba_bot);
    cone.UVBaseSet(uv_u, uv_v);
    cone.UVSliceSet(uv_slice_dray[0], uv_slice_dray[1]);
    cone.TextureSet(fxrec->fxdata.draydata.rast);
    cone.RenderCone(&pos_top, &fxrec->pos_A[0]);
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

void NCIN_FireSpiral_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
        NPAR_FindParty(NPAR_TYP_TUBESPIRAL)->KillAll();
    }
}

void NCIN_FireSpiral_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                        U32 animIndex, U32 dataIndex)
{
    F32 tym;

    if (animIndex != 3)
    {
        return;
    }

    xVec3 pos_emit = *(const xVec3*)&animMat[26].pos;

    pos_emit += *(const xVec3*)&animMat->pos;

    xVec3 dir_emit = pos_emit - *(const xVec3*)&animMat->pos;

    dir_emit.y = 0.0f;
    dir_emit.normalize();

    xVec3 vel = dir_emit * 5.0f;

    tym = fxrec->tym_end - fxrec->tym_beg;

    NPAR_EmitTubeSpiralCin(&pos_emit, &vel, MAX(0.25f, MIN(tym, 1.0f)));
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

void NCIN_OilHazard(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    S32 i;
    S32 rc;
    F32 tym;

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

    xVec3 pos_emit = fxrec->pos_A[0];
    xVec3 vel_emit;

    for (i = 0; i < 16; i++)
    {
        vel_emit = g_Y3;
        vel_emit += g_Z3 * ((xrand() & 0x800000) ? 1.0f : -1.0f) *
                    (0.4f * (2.0f * (xurand() - 0.5f)) + 0.25f);
        vel_emit += g_X3 * ((xrand() & 0x800000) ? 1.0f : -1.0f) *
                    (0.4f * (2.0f * (xurand() - 0.5f)) + 0.25f);
        vel_emit.normalize();
        vel_emit *= 15.0f;

        NPAR_EmitOilSplash(&pos_emit, &vel_emit);
    }

    NPCHazard* haz = HAZ_Acquire();

    if (haz == NULL)
    {
        return;
    }

    rc = haz->ConfigHelper(NPC_HAZ_OILSLICK);

    if (rc == 0)
    {
        return;
    }

    haz->SetNPCOwner(NULL);

    xVec3Copy((xVec3*)&haz->mdl_hazard->Mat->up, &g_Y3);
    xVec3Copy((xVec3*)&haz->mdl_hazard->Mat->at, &g_Z3);
    xVec3Copy((xVec3*)&haz->mdl_hazard->Mat->right, &g_NX3);

    tym = fxrec->tym_end - fxrec->tym_beg;

    haz->Start(&fxrec->pos_A[0], (0.5f > tym) ? 0.5f : tym);
    fxrec->fxdata.hazdata.npchaz = haz;
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

void NCIN_FodProd_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                     U32 animIndex, U32 dataIndex)
{
    if (animIndex != 0)
    {
        return;
    }

    static const xVec3 vec_offset = { 0.2f, 0.0f, 0.5f };

    const xMat4x3* mat_bone = (const xMat4x3*)&animMat[5];
    NPCHazard* haz = fxrec->fxdata.hazdata.npchaz;

    if (haz == NULL)
    {
        return;
    }

    xVec3 pos_poke = mat_bone->pos;

    pos_poke += mat_bone->right * vec_offset.x;
    pos_poke += mat_bone->up * vec_offset.y;
    pos_poke += mat_bone->at * vec_offset.z;
    pos_poke += *(const xVec3*)&animMat->pos;

    haz->PosSet(&pos_poke);
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

void NCIN_FodProdBone_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                         U32 animIndex, U32 dataIndex)
{
    xMat4x3* mat_bone;
    NPCHazard* haz;

    U32 ifx = fxrec->pos_A[1].x;

    if (animIndex != ifx)
    {
        return;
    }

    static S32 idx_bonePoke = fxrec->pos_A[1].y;

    haz = fxrec->fxdata.hazdata.npchaz;
    mat_bone = (xMat4x3*)&animMat[idx_bonePoke];

    if (haz == NULL)
    {
        return;
    }

    static xVec3 vec_offset = fxrec->pos_A[0];

    xVec3 pos_poke = mat_bone->pos;

    pos_poke += mat_bone->right * vec_offset.x;
    pos_poke += mat_bone->up * vec_offset.y;
    pos_poke += mat_bone->at * vec_offset.z;
    pos_poke += *(const xVec3*)&animMat->pos;

    haz->PosSet(&pos_poke);
}

void NCIN_MidFish_Upd(const zCutsceneMgr* mgr, NCINEntry* e, S32 i)
{
    if (i != 0)
    {
        e->flg_stat |= 4;
    }
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
            xVec3 pos_emit = *(const xVec3*)&animMat[i].pos + *(const xVec3*)&animMat->pos;
            zFX_SpawnBubbleTrail(&pos_emit, 1);
        }
    }
    else if (animIndex == 2 || animIndex == 3)
    {
        for (idx = g_idx_handbone; *idx >= 0; idx++)
        {
            xVec3 pos_emit = *(const xVec3*)&animMat[*idx].pos + *(const xVec3*)&animMat->pos;
            zFX_SpawnBubbleTrail(&pos_emit, 1);
        }
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

void NCIN_HookRecoil_AR(const zCutsceneMgr* csnmgr, NCINEntry*, RpAtomic* model,
                        RwMatrixTag* animMat, U32 animIndex, U32 dataIndex)
{
    F32 tym = csnmgr->csn->Time;
    S32 idx_anim;

    if (tym < 7.3666672f)
    {
        idx_anim = 5;
    }
    else if (tym < 12.800001f)
    {
        idx_anim = 1;
    }
    else
    {
        idx_anim = 2;
    }

    if (animIndex != idx_anim)
    {
        return;
    }

    U32 num_bones = iModelNumBones(model);

    for (U32 i = 1; i < num_bones; i++)
    {
        xVec3 pos_emit = *(const xVec3*)&animMat[i].pos + *(const xVec3*)&animMat->pos;
        zFX_SpawnBubbleTrail(&pos_emit, 1);
    }
}

void NCIN_Lightnin2Bones_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    _tagLightningAdd addInfo;
    xVec3 pnt;

    if (killit != 0)
    {
        if (fxrec->fxdata.arcdata.lightning != NULL)
        {
            zLightningKill(fxrec->fxdata.arcdata.lightning);
        }

        fxrec->flg_stat |= 4;
        return;
    }

    if (!(fxrec->flg_stat & 2))
    {
        return;
    }

    xVec3Init(&pnt, 0.0f, 0.0f, 0.0f);

    addInfo.type = 3;
    addInfo.thickness = 1.0f;
    addInfo.flags = 0x10;
    addInfo.color.r = 200;
    addInfo.color.g = 200;
    addInfo.color.b = 200;
    addInfo.color.a = 255;
    addInfo.time = 1.0f;
    addInfo.start = &pnt;
    addInfo.end = &pnt;

    fxrec->fxdata.arcdata.lightning = zLightningAdd(&addInfo);
}

void NCIN_Lightnin2Bones_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                            U32 animIndex, U32 dataIndex)
{
    xVec3 begpos;
    xVec3 endpos;
    zLightning* light = fxrec->fxdata.arcdata.lightning;

    if (light == NULL)
    {
        return;
    }

    S32 ifx = fxrec->pos_A[1].x;

    if (animIndex == ifx)
    {
        S32 boneIndex = fxrec->pos_A[1].y;
        const xMat3x3* bone = (const xMat3x3*)&animMat[boneIndex];

        xMat3x3RMulVec(&begpos, bone, &fxrec->pos_A[0]);
        xVec3AddTo(&begpos, (const xVec3*)&animMat[boneIndex].pos);

        if (boneIndex != 0)
        {
            xVec3AddTo(&begpos, (const xVec3*)&animMat->pos);
        }

        zLightningModifyEndpoints(light, &begpos, &light->func.endPoint[1]);
    }

    S32 ifx2 = fxrec->pos_B[1].x;

    if (animIndex == ifx2)
    {
        S32 boneIndex = fxrec->pos_B[1].y;
        const xMat3x3* bone = (const xMat3x3*)&animMat[boneIndex];

        xMat3x3RMulVec(&endpos, bone, &fxrec->pos_B[0]);
        xVec3AddTo(&endpos, (const xVec3*)&animMat[boneIndex].pos);

        if (boneIndex != 0)
        {
            xVec3AddTo(&endpos, (const xVec3*)&animMat->pos);
        }

        zLightningModifyEndpoints(light, &light->func.endPoint[0], &endpos);
    }
}

void NCIN_LightninBone_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    _tagLightningAdd addInfo;
    xVec3 pnt;
    zLightning* light;

    if (killit != 0)
    {
        if (fxrec->fxdata.arcdata.lightning != NULL)
        {
            zLightningKill(fxrec->fxdata.arcdata.lightning);
        }

        fxrec->flg_stat |= 4;
        return;
    }

    if (fxrec->flg_stat & 2)
    {
        xVec3Init(&pnt, 0.0f, 0.0f, 0.0f);

        addInfo.type = 3;
        addInfo.thickness = 1.0f;
        addInfo.flags = 0x100;
        addInfo.color.r = 200;
        addInfo.color.g = 200;
        addInfo.color.b = 200;
        addInfo.color.a = 255;
        addInfo.time = 0.25f * xurand() + 0.05f;
        addInfo.start = &pnt;
        addInfo.end = &pnt;

        fxrec->fxdata.arcdata.lightning = zLightningAdd(&addInfo);

        fxrec->fxdata.arcdata.endPos.x = fxrec->pos_B[0].x * xurand() + fxrec->pos_B[1].x;
        fxrec->fxdata.arcdata.endPos.y = fxrec->pos_B[0].y * xurand() + fxrec->pos_B[1].y;
        fxrec->fxdata.arcdata.endPos.z = fxrec->pos_B[0].z * xurand() + fxrec->pos_B[1].z;
    }

    light = fxrec->fxdata.arcdata.lightning;

    if (light == NULL)
    {
        return;
    }

    if (light->flags & 0x40)
    {
        return;
    }

    fxrec->fxdata.arcdata.endPos.x = fxrec->pos_B[0].x * xurand() + fxrec->pos_B[1].x;
    fxrec->fxdata.arcdata.endPos.y = fxrec->pos_B[0].y * xurand() + fxrec->pos_B[1].y;
    fxrec->fxdata.arcdata.endPos.z = fxrec->pos_B[0].z * xurand() + fxrec->pos_B[1].z;

    light->time_total = 0.25f * xurand() + 0.05f;
    light->time_left = light->time_total;

    zLightningShow(light, 1);
}

void NCIN_LightninBone_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                          U32 animIndex, U32 dataIndex)
{
    xVec3 pnt1;
    xVec3 pnt2;
    S32 ifx = fxrec->pos_A[1].x;

    if (animIndex != ifx)
    {
        return;
    }

    S32 boneIndex = fxrec->pos_A[1].y;
    const xMat3x3* bone = (const xMat3x3*)&animMat[boneIndex];

    xMat3x3RMulVec(&pnt1, bone, &fxrec->pos_A[0]);
    xVec3AddTo(&pnt1, (const xVec3*)&animMat[boneIndex].pos);

    if (boneIndex != 0)
    {
        xVec3AddTo(&pnt1, (const xVec3*)&animMat->pos);
    }

    xMat3x3RMulVec(&pnt2, bone, &fxrec->fxdata.arcdata.endPos);
    xVec3AddTo(&pnt2, &pnt1);
    zLightningModifyEndpoints(fxrec->fxdata.arcdata.lightning, &pnt1, &pnt2);
}

void NCIN_B101Shockwave_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    zParEmitter* emitter = fxrec->fxdata.pardata.emitter;

    if (killit != 0)
    {
        if (emitter != NULL)
        {
            emitter->emit_flags &= ~1;
        }

        fxrec->flg_stat |= 4;
        return;
    }

    if (fxrec->flg_stat & 2)
    {
        emitter = zParEmitterFind("PAREMIT_SHOCKWAVE");

        if (emitter != NULL)
        {
            fxrec->fxdata.pardata.emitter = emitter;
            emitter->tasset->e_circle.radius = 1.0f;
            xVec3Copy(&emitter->tasset->pos, &fxrec->pos_A[0]);
        }
        else
        {
            fxrec->flg_stat |= 4;
            return;
        }
    }

    emitter->tasset->e_circle.radius +=
        20.0f * globals.update_dt / xsqrt(emitter->tasset->e_circle.radius);
    emitter->emit_flags |= 1;

    if (emitter->tasset->e_circle.radius > 10.0f)
    {
        fxrec->flg_stat |= 4;
        emitter->emit_flags &= ~1;
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

void NCIN_FreezeBreath_AR(const zCutsceneMgr* csnmgr, NCINEntry* fxrec, RpAtomic*,
                          RwMatrixTag* animMat, U32 animIndex, U32 dataIndex)
{
    xVec3 pnt1;
    xVec3 pnt2;

    S32 ifx = fxrec->pos_A[1].x;

    if (animIndex != ifx)
    {
        return;
    }

    S32 boneIndex = fxrec->pos_A[1].y;
    const xMat3x3* bone = (const xMat3x3*)&animMat[boneIndex];

    xMat3x3RMulVec(&pnt1, bone, &fxrec->pos_A[0]);
    xVec3AddTo(&pnt1, (const xVec3*)&animMat[boneIndex].pos);

    if (boneIndex != 0)
    {
        xVec3AddTo(&pnt1, (const xVec3*)&animMat->pos);
    }

    xMat3x3RMulVec(&pnt2, bone, &fxrec->pos_B[0]);

    EmitFreezeBreath(&pnt1, &pnt2, globals.update_dt,
                     csnmgr->csn->Time - fxrec->tym_beg, fxrec->tym_end - fxrec->tym_beg);
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

void NCIN_GooLever_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                      U32 animIndex, U32 dataIndex)
{
    S32 ifx = fxrec->pos_A[1].x;

    if (animIndex != ifx)
    {
        return;
    }

    S32 ify = fxrec->pos_A[1].y;
    F32 fudgeRate = 400.0f * -animMat[ify].at.y;

    if (fudgeRate < 0.0f)
    {
        fudgeRate = 0.0f;
    }

    if (fxrec->fxdata.pardata.emitter != NULL)
    {
        fxrec->fxdata.pardata.emitter->prop->rate.val[0] = fudgeRate;
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
        zShrapnelAsset* sasset = (zShrapnelAsset*)xSTFindAsset(xStrHash("boss_pa_shrapnel"), NULL);

        if (sasset == NULL)
        {
            fxrec->flg_stat |= 4;
        }
        else
        {
            fxrec->fxdata.shrapdata.shrap = sasset;
        }
    }
    else
    {
        fxrec->flg_stat |= 4;
    }
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

void NCIN_SpatGlow_Upd(const zCutsceneMgr* mgr, NCINEntry* e, S32 i)
{
    if (i != 0)
    {
        e->flg_stat |= 4;
    }
}

void NCIN_SpatGlow_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic* model, RwMatrixTag* animMat,
                      U32 animIndex, U32 dataIndex)
{
    S32 ifx = fxrec->pos_A[1].x;

    if (animIndex != ifx)
    {
        return;
    }

    S32 boneIndex = fxrec->pos_A[1].y;
    iColor_tag color;

    zShrapnel_CinematicInit(fxrec->fxdata.shrapdata.shrap, model, animMat, NULL, NULL);

    color.r = (U8)(S32)fxrec->pos_B[0].x;
    color.g = (U8)(S32)fxrec->pos_B[0].y;
    color.b = (U8)(S32)fxrec->pos_B[0].z;
    color.a = (U8)(S32)fxrec->pos_B[1].x;

    xFXAuraAdd(fxrec, (xVec3*)&animMat[boneIndex].pos, &color, fxrec->pos_B[1].y);
}

void NCIN_GloveShrapnel_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;
    }
    else if (fxrec->flg_stat & 2)
    {
        zShrapnelAsset* sasset = (zShrapnelAsset*)xSTFindAsset(xStrHash("g-love_shrapnel"), NULL);

        if (sasset == NULL)
        {
            fxrec->flg_stat |= 4;
        }
        else
        {
            fxrec->fxdata.shrapdata.shrap = sasset;
        }
    }
    else
    {
        fxrec->flg_stat |= 4;
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

void NCIN_EntityBonePar_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic* model,
                           RwMatrixTag* animMat, U32 animIndex, U32 dataIndex)
{
    F32 dt;
    xParEmitter* pe;
    xParEmitterAsset* a;
    xParEmitterPropsAsset* prop;
    xParSys* ps;
    xParGroup* g;
    S32 rate_has_elapsed;
    F32 rate;
    S32 count;
    F32 life;
    F32 size_birth;
    F32 size_death;
    xMat4x3 bone_mat;
    S32 i;
    xPar* p;
    S32 c;
    F32 fc1;
    F32 fc2;

    S32 ifx = fxrec->pos_A[1].x;

    if (ifx != (S32)animIndex)
    {
        return;
    }

    clamp_bone_index(fxrec, model);

    pe = fxrec->fxdata.pardata.emitter;
    dt = globals.update_dt;

    if (pe == NULL)
    {
        return;
    }

    ps = pe->parSys;
    a = pe->tasset;
    prop = pe->prop;

    if (ps == NULL)
    {
        return;
    }

    g = ps->group;

    if (g == NULL)
    {
        return;
    }

    if (a->emit_type != eParEmitterEntityBone)
    {
        return;
    }

    pe->rate_time = pe->rate_time + dt;
    rate_has_elapsed = pe->rate_time > prop->rate.freq;

    if (prop->rate.freq == 0.0f)
    {
        pe->rate_time = 0.0f;
    }

    while (pe->rate_time > prop->rate.freq)
    {
        pe->rate_time -= prop->rate.freq;
    }

    rate = xParInterpCompute(pe->rate_mode, &prop->rate, pe->rate_time, rate_has_elapsed,
                             pe->rate);
    pe->rate = rate;
    pe->rate_fraction = pe->rate_fraction + rate * dt;
    pe->rate_fraction_cull = pe->rate_fraction_cull + rate * dt;

    count = std::floorf(pe->rate_fraction);

    if (count <= 0)
    {
        return;
    }

    pe->rate_fraction = pe->rate_fraction - count;

    if (ps->tasset->maxPar != 0)
    {
        if (g->m_num_of_particles >= (S32)ps->tasset->maxPar)
        {
            return;
        }

        if (ps->group->m_num_of_particles + count >= ps->tasset->maxPar)
        {
            count = ps->tasset->maxPar - ps->group->m_num_of_particles;
        }
    }

    get_bone_matrix(bone_mat, fxrec, animMat);

    xVec3 oldloc = fxrec->pos_B[0];
    xVec3 loc;
    xVec3 vel;

    xParEmitterTransformEntBone(loc, vel, *a, bone_mat);

    fxrec->pos_B[0] = loc;
    vel *= dt;

    xVec3 emitvel;

    if (pe->emit_flags & 8)
    {
        emitvel = loc - oldloc;

        if (emitvel.length2() > 25.0f)
        {
            emitvel = 0.0f;
        }
    }
    else
    {
        emitvel = 0.0f;
    }

    for (i = 0; i < count; i++)
    {
        p = xParGroupAddPar(ps->group);

        if (p == NULL)
        {
            return;
        }

        life = xParInterpCompute(prop->life.interp, &prop->life, pe->rate_time, 1, 0.0f);
        size_birth = xParInterpCompute(prop->size_birth.interp, &prop->size_birth, pe->rate_time,
                                       1, 0.0f);
        size_death = xParInterpCompute(prop->size_death.interp, &prop->size_death, pe->rate_time,
                                       1, 0.0f);

        p->m_lifetime = life;
        p->totalLifespan = life;
        p->m_size = size_birth;
        p->m_sizeVel = (size_death - size_birth) / life;
        p->m_flag = 0;
        p->m_rotdeg[0] = pe->rot[0];
        p->m_rotdeg[1] = pe->rot[1];
        p->m_rotdeg[2] = pe->rot[2];

        for (c = 0; c < 4; c++)
        {
            fc1 = xParInterpCompute(prop->color_birth[c].interp, &prop->color_birth[c],
                                    pe->rate_time, 1, 0.0f);
            fc2 = xParInterpCompute(prop->color_death[c].interp, &prop->color_death[c],
                                    pe->rate_time, 1, 0.0f);

            p->m_cfl[c] = fc1;
            p->m_c[c] = (U8)(S32)fc1;
            p->m_cvel[c] = (fc2 - fc1) / life;
        }

        p->m_pos = loc;
        xParEmitterEmitSetTexIdxs(p, ps);
        p->m_vel = vel;
        xParEmitterEmitEntBone(p, a, dt, bone_mat);
        p->m_vel += emitvel;
    }
}

void get_bone_matrix(xMat4x3& mat, const NCINEntry* fxrec, const RwMatrixTag* animMat)
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

void NCIN_BubbleTrail_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic* model,
                         RwMatrixTag* animMat, U32 animIndex, U32 dataIndex)
{
    S32 ifx = fxrec->pos_A[1].x;

    if (ifx != (S32)animIndex)
    {
        return;
    }

    clamp_bone_index(fxrec, model);

    xVec3& offset = fxrec->pos_A[0];
    xVec3& root_vel = fxrec->pos_B[0];
    F32 rand_diameter = fxrec->pos_B[1].x;
    F32 rand_vel = fxrec->pos_B[1].y;
    F32 scale = fxrec->pos_B[1].z;
    NCINCustom& data = fxrec->fxdata.customdata;
    xVec3& oldloc = data.v[0];
    xVec3& oldvel = data.v[1];
    S32 total;
    xMat4x3 mat;
    xVec3 vel;
    xVec3 loc;

    data.f[0] = data.f[0] + fxrec->pos_A[1].z * globals.update_dt;

    total = data.f[0];

    if (total <= 0)
    {
        return;
    }

    data.f[0] = data.f[0] - total;

    get_bone_matrix(mat, fxrec, animMat);
    xMat3x3RMulVec(&vel, (const xMat3x3*)&mat, &root_vel);
    xMat4x3Toworld(&loc, &mat, &offset);

    if (oldloc.x >= 1e38f)
    {
        oldloc = loc;
        oldvel = vel;
    }

    xVec3 rloc = { 0.0f, 0.0f, 0.0f };
    xVec3 rvel = { 0.0f, 0.0f, 0.0f };

    rloc.x = rand_diameter;
    rloc.y = rand_diameter;
    rloc.z = rand_diameter;

    rvel.x = rand_vel;
    rvel.y = rand_vel;
    rvel.z = rand_vel;

    zFX_SpawnBubbleTrail(&oldloc, &loc, &oldvel, &vel, total, &rloc, &rvel, scale);

    oldloc = loc;
    oldvel = vel;
}

void NCIN_SBBNode_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    if (killit != 0)
    {
        fxrec->flg_stat |= 4;

        zNPCB_SB2* sb2 = zNPCB_SB2::singleton();

        if (sb2 != NULL)
        {
            sb2->bind_nodes();
        }

        if (fxrec->fxdata.matdata.mat != NULL)
        {
            xMemPopTemp(fxrec->fxdata.matdata.mat);
        }
        return;
    }

    if (fxrec->flg_stat & 2)
    {
        fxrec->fxdata.matdata.mat = NULL;
    }
}

void NCIN_SBBNode_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic* model,
                     RwMatrixTag* animMat, U32 animIndex, U32 dataIndex)
{
    zNPCB_SB2* sb2;
    S32 bones;
    RpSkin* skin;
    xMat3x3 imat;
    S32 i;

    S32 ifx = fxrec->pos_A[1].x;

    if (ifx != (S32)animIndex)
    {
        return;
    }

    sb2 = zNPCB_SB2::singleton();

    if (sb2 == NULL)
    {
        return;
    }

    bones = 0;

    if (model->geometry != NULL)
    {
        skin = RpSkinGeometryGetSkin(model->geometry);

        if (skin != NULL)
        {
            bones = RpSkinGetNumBones(skin);
        }
    }

    if (fxrec->flg_stat & 8)
    {
        fxrec->fxdata.matdata.mat = (RwMatrixTag*)xMemPushTemp((bones + 1) * sizeof(RwMatrixTag));
    }

    sb2->rebind_nodes(model, fxrec->fxdata.matdata.mat);

    xMat3x3Transpose(&imat, (const xMat3x3*)animMat);

    for (i = 1; i < bones; i++)
    {
        xMat3x3Mul((xMat3x3*)&fxrec->fxdata.matdata.mat[i], (const xMat3x3*)&animMat[i], &imat);
        fxrec->fxdata.matdata.mat[i].pos = animMat[i].pos;
    }

    *fxrec->fxdata.matdata.mat = *animMat;

    sb2->move_nodes();
    sb2->render_nodes();
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
