#include "xParCmd.h"

#include "xParGroup.h"
#include "xVec3Inlines.h"
#include "xMath.h"
#include "xMathInlines.h"

struct xCmdInfo
{
    U32 type;
    U32 size;
    xParCmdUpdateFunc func;
};

static xCmdInfo sCmdInfo[XPARCMD_TYPE_COUNT] = {};

void xParCmdInit()
{
    xParCmdRegister(XPARCMD_TYPE_MOVE, sizeof(xParCmdMove), xParCmdMove_Update);
    xParCmdRegister(XPARCMD_TYPE_MOVERANDOM, sizeof(xParCmdMove), xParCmdMoveRandom_Update);
    xParCmdRegister(XPARCMD_TYPE_ACCELERATE, sizeof(xParCmdAccelerate), xParCmdAccelerate_Update);
    xParCmdRegister(XPARCMD_TYPE_VELOCITYAPPLY, sizeof(xParCmdAsset), xParCmdVelocityApply_Update);
    xParCmdRegister(XPARCMD_TYPE_UNK5, sizeof(xParCmdUnk5), NULL);
    xParCmdRegister(XPARCMD_TYPE_KILLSLOW, sizeof(xParCmdKillSlow), xParCmdKillSlow_Update);
    xParCmdRegister(XPARCMD_TYPE_FOLLOW, sizeof(xParCmdFollow), xParCmdFollow_Update);
    xParCmdRegister(XPARCMD_TYPE_ORBITPOINT, sizeof(xParCmdOrbitPoint), xParCmdOrbitPoint_Update);
    xParCmdRegister(XPARCMD_TYPE_ORBITLINE, sizeof(xParCmdOrbitLine), xParCmdOrbitLine_Update);
    xParCmdRegister(XPARCMD_TYPE_MOVERANDOMPAR, sizeof(xParCmdMoveRandomPar),
                    xParCmdMoveRandomPar_Update);
    xParCmdRegister(XPARCMD_TYPE_SCALE3RDPOLYREG, sizeof(xParCmdScale3rdPolyReg),
                    xParCmdScale3rdPolyReg_Update);
    xParCmdRegister(XPARCMD_TYPE_TEX, sizeof(xParCmdTex), xParCmdTex_Update);
    xParCmdRegister(XPARCMD_TYPE_TEXANIM, sizeof(xParCmdTexAnim), xParCmdTexAnim_Update);
    xParCmdRegister(XPARCMD_TYPE_RANDOMVELOCITYPAR, sizeof(xParCmdRandomVelocityPar),
                    xParCmdRandomVelocityPar_Update);
    xParCmdRegister(XPARCMD_TYPE_AGE, sizeof(xParCmdAge), xParCmdAge_Update);
    xParCmdRegister(XPARCMD_TYPE_ALPHA3RDPOLYREG, sizeof(xParCmdAlpha3rdPolyReg),
                    xParCmdAlpha3rdPolyReg_Update);
    xParCmdRegister(XPARCMD_TYPE_APPLYWIND, sizeof(xParCmdApplyWind), xParCmdApplyWind_Update);
    xParCmdRegister(XPARCMD_TYPE_ROTPAR, sizeof(xParCmdRotPar), xParCmdRotPar_Update);
    xParCmdRegister(XPARCMD_TYPE_ROTATEAROUND, sizeof(xParCmdRotateAround),
                    xParCmdRotateAround_Update);
    xParCmdRegister(XPARCMD_TYPE_SMOKEALPHA, sizeof(xParCmdSmokeAlpha), xParCmdSmokeAlpha_Update);
    xParCmdRegister(XPARCMD_TYPE_SCALE, sizeof(xParCmdScale), xParCmdScale_Update);
    xParCmdRegister(XPARCMD_TYPE_COLLIDEFALL, sizeof(xParCmdCollideFall),
                    xParCmdCollideFall_Update);
    xParCmdRegister(XPARCMD_TYPE_COLLIDEFALLSTICKY, sizeof(xParCmdCollideFallSticky),
                    xParCmdCollideFallSticky_Update);
    xParCmdRegister(XPARCMD_TYPE_SHAPER, sizeof(xParCmdShaperData), xParCmd_Shaper_Update);
    xParCmdRegister(XPARCMD_TYPE_ALPHAINOUT, sizeof(xParCmdAlphaInOutData),
                    xParCmd_AlphaInOut_Update);
    xParCmdRegister(XPARCMD_TYPE_SIZEINOUT, sizeof(xParCmdSizeInOutData), xParCmd_SizeInOut_Update);
    xParCmdRegister(XPARCMD_TYPE_DAMPENSPEED, sizeof(xParCmdDampenData),
                    xParCmd_DampenSpeed_Update);
}

void xParCmdRegister(U32 parType, U32 size, xParCmdUpdateFunc func)
{
    sCmdInfo[parType].type = parType;
    sCmdInfo[parType].size = size;
    sCmdInfo[parType].func = func;
}

U32 xParCmdGetSize(U32 parType)
{
    for (S32 i = 0; i < XPARCMD_TYPE_COUNT; i++)
    {
        if (sCmdInfo[i].type == parType)
        {
            return sCmdInfo[i].size;
        }
    }

    return 0;
}

xParCmdUpdateFunc xParCmdGetUpdateFunc(U32 parType)
{
    for (S32 i = 0; i < XPARCMD_TYPE_COUNT; i++)
    {
        if (sCmdInfo[i].type == parType)
        {
            return sCmdInfo[i].func;
        }
    }

    return NULL;
}

void xParCmdKillSlow_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    xParCmdKillSlow* cmd = (xParCmdKillSlow*)c->tasset;

    // The test is against m_vel, a displacement per FRAME, whose square shrinks
    // as dt squared while this limit shrinks as dt. The extra 60*dt makes both
    // sides scale together and is one at a sixtieth of a second.
#ifdef PLATFORM_PC
    F32 speedLimit = cmd->speedLimitSqr * dt * (60.0f * dt);
#else
    F32 speedLimit = cmd->speedLimitSqr * dt;
#endif

    if (cmd->kill_less_than)
    {
        while (p)
        {
            if (xVec3Length2(&p->m_vel) < speedLimit)
            {
                p->m_lifetime = -1.0f;
            }

            p = p->m_next;
        }
    }
    else
    {
        while (p)
        {
            if (xVec3Length2(&p->m_vel) > speedLimit)
            {
                p->m_lifetime = -1.0f;
            }

            p = p->m_next;
        }
    }
}

void xParCmdAge_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    F32 age_rate = ((xParCmdAge*)c->tasset)->unknown * dt;

    while (p)
    {
        p->m_lifetime -= age_rate;
        p = p->m_next;
    }
}

void xParCmdFollow_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    xParCmdFollow* cmd = (xParCmdFollow*)c->tasset;

    // xPar::m_vel is a displacement per FRAME -- xParCmdVelocityApply_Update
    // adds it to the position with no dt -- so an acceleration added to it
    // needs dt twice, not once. The extra 60*dt is one at a sixtieth of a
    // second and leaves the console's pull unchanged.
#ifdef PLATFORM_PC
    F32 mdt = cmd->gravity * dt * (60.0f * dt);
#else
    F32 mdt = cmd->gravity * dt;
#endif

    while (p && p->m_next)
    {
        xVec3 var_38;

        xVec3Sub(&var_38, &p->m_next->m_pos, &p->m_pos);

        F32 f31 = xVec3Length2(&var_38);
        F32 f1 = xVec3LengthFast(var_38.x, var_38.y, var_38.z);

        F32 force = mdt / (f1 * (f31 + cmd->epsilon));

        p->m_vel.x += var_38.x * force;
        p->m_vel.y += var_38.y * force;
        p->m_vel.z += var_38.z * force;

        p = p->m_next;
    }
}

void xParCmdOrbitPoint_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    xParCmdOrbitPoint* cmd = (xParCmdOrbitPoint*)c->tasset;

    // An acceleration on a per-frame displacement. See xParCmdFollow_Update.
#ifdef PLATFORM_PC
    F32 mdt = cmd->gravity * dt * (60.0f * dt);
#else
    F32 mdt = cmd->gravity * dt;
#endif

    while (p)
    {
        xVec3 var_38;

        xVec3Sub(&var_38, &cmd->center, &p->m_pos);

        F32 f31 = xVec3Length2(&var_38);

        if (f31 < cmd->maxRadiusSqr)
        {
            F32 f1 = xVec3LengthFast(var_38.x, var_38.y, var_38.z);

            F32 force = mdt / (f1 + (f31 + cmd->epsilon));

            p->m_vel.x += var_38.x * force;
            p->m_vel.y += var_38.y * force;
            p->m_vel.z += var_38.z * force;
        }

        p = p->m_next;
    }
}

void xParCmdOrbitLine_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    xParCmdOrbitLine* cmd = (xParCmdOrbitLine*)c->tasset;

    // An acceleration on a per-frame displacement. See xParCmdFollow_Update.
#ifdef PLATFORM_PC
    F32 mdt = cmd->gravity * dt * (60.0f * dt);
#else
    F32 mdt = cmd->gravity * dt;
#endif

    while (p)
    {
        xVec3 var_34, var_40, var_4C, var_58;

        xVec3Sub(&var_34, &p->m_pos, &cmd->p);
        xVec3Cross(&var_4C, &var_34, &cmd->axis);
        xVec3Cross(&var_40, &cmd->axis, &var_4C);
        xVec3Sub(&var_58, &var_40, &var_34);

        F32 f31 = xVec3Length2(&var_58);

        if (f31 < cmd->maxRadiusSqr)
        {
            F32 f1 = xVec3LengthFast(var_58.x, var_58.y, var_58.z);

            F32 force = mdt / (f1 + (f31 + cmd->epsilon));

            p->m_vel.x += var_58.x * force;
            p->m_vel.y += var_58.y * force;
            p->m_vel.z += var_58.z * force;
        }

        p = p->m_next;
    }
}

void xParCmdAccelerate_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdAccelerate* cmd = (xParCmdAccelerate*)c->tasset;

    xVec3 var_28;
    var_28 = cmd->acc;

    // An acceleration on a per-frame displacement. See xParCmdFollow_Update.
#ifdef PLATFORM_PC
    xVec3SMulBy(&var_28, dt * (60.0f * dt));
#else
    xVec3SMulBy(&var_28, dt);
#endif

    p = ps->m_root;

    while (p)
    {
        p->m_vel.x += var_28.x;
        p->m_vel.y += var_28.y;
        p->m_vel.z += var_28.z;

        p = p->m_next;
    }
}

void xParCmdMove_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdMove* cmd = (xParCmdMove*)c->tasset;

    xVec3 var_28;
    var_28 = cmd->dir;

    xVec3SMulBy(&var_28, dt);

    p = ps->m_root;

    while (p)
    {
        xVec3Add(&p->m_pos, &p->m_pos, &var_28);

        p = p->m_next;
    }
}

void xParCmdMoveRandom_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdMove* cmd = (xParCmdMove*)c->tasset;

    xVec3 var_28;
    var_28 = cmd->dir;

    var_28.x *= 2.0f * xurand();
    var_28.y *= 2.0f * xurand();
    var_28.z *= 2.0f * xurand();

    xVec3Sub(&var_28, &var_28, &cmd->dir);
    xVec3SMulBy(&var_28, dt);

    p = ps->m_root;

    while (p)
    {
        xVec3Add(&p->m_pos, &p->m_pos, &var_28);

        p = p->m_next;
    }
}

void xParCmdMoveRandomPar_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    xParCmdMoveRandomPar* cmd = (xParCmdMoveRandomPar*)c->tasset;
#ifdef PLATFORM_PC
    // A random walk, not a velocity: the steps are independent, so the spread
    // after a second grows with the square root of the step count, not the
    // count. A step proportional to dt shrinks the wander to a quarter at four
    // times the frame rate. Dividing by the square root of the frame's share of
    // a sixtieth of a second holds the spread, and is one at 60 fps.
    if (dt <= 0.0f)
    {
        // Retail moves nothing on a zero frame either, and the scale below
        // would be a division by zero.
        return;
    }

    F32 rate_scale = 1.0f / xsqrt(60.0f * dt);
    F32 f31 = cmd->dim.x * (dt / 2.0f) * rate_scale;
    F32 f30 = cmd->dim.z * (dt / 2.0f) * rate_scale;
#else
    F32 f31 = cmd->dim.x * (dt / 2.0f);
    F32 f30 = cmd->dim.z * (dt / 2.0f);
#endif

    while (p)
    {
        p->m_pos.x += f31 * (xurand() - 0.5f);
        p->m_pos.z += f30 * (xurand() - 0.5f);

        p = p->m_next;
    }
}

void xParCmdScale3rdPolyReg_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
}

void xParCmdSmokeAlpha_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
}

void xParCmdScale_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
}

void xParCmdAlpha3rdPolyReg_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
}

void xParCmdRandomVelocityPar_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    xParCmdRandomVelocityPar* cmd = (xParCmdRandomVelocityPar*)c->tasset;
    F32 f31 = cmd->x * dt;
    F32 f30 = cmd->y * dt;
    F32 f29 = cmd->z * dt;

    while (p)
    {
        xMat3x3 var_88;

        F32 y = 2.0f * (f31 * xurand()) - f31;
        F32 x = 2.0f * (f30 * xurand()) - f30;
        F32 z = 2.0f * (f29 * xurand()) - f29;

        xMat3x3Euler(&var_88, x, y, z);
        xMat3x3LMulVec(&p->m_vel, &var_88, &p->m_vel);

        p = p->m_next;
    }
}

void xParCmdApplyWind_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdApplyWind* cmd = (xParCmdApplyWind*)c->tasset;

    // The wind direction is hardcoded to (1, _, 1); only its magnitude is data-driven.
    F32 wind_x = 1.0f;
    F32 wind_z = 1.0f;

    // An acceleration on a per-frame displacement. See xParCmdFollow_Update.
#ifdef PLATFORM_PC
    F32 mag = cmd->unknown * dt * (60.0f * dt);
#else
    F32 mag = cmd->unknown * dt;
#endif

    wind_x *= mag;
    wind_z *= mag;

    p = ps->m_root;

    while (p)
    {
        p->m_vel.x += wind_x;
        p->m_vel.z += wind_z;

        p = p->m_next;
    }
}

#ifdef PLATFORM_PC
namespace
{
    // A whole byte-angle of turn for a frame, from a rate given per console
    // frame. xFrameEmitCount rounds at random so the average holds; it only
    // counts up, and a rotation rate runs either way.
    U8 rot_step(F32 per_frame, F32 dt)
    {
        if (per_frame < 0.0f)
        {
            return (U8)(-(S32)xFrameEmitCount(-per_frame, dt));
        }

        return (U8)xFrameEmitCount(per_frame, dt);
    }
} // namespace
#endif

void xParCmdRotPar_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    xParCmdRotPar* cmd = (xParCmdRotPar*)c->tasset;
    F32 f30 = 255.0f * ((cmd->max.x - cmd->min.x) / 360.0f);
    F32 f29 = 255.0f * ((cmd->max.y - cmd->min.y) / 360.0f);
    F32 f28 = 255.0f * ((cmd->max.z - cmd->min.z) / 360.0f);
    F32 f27 = 255.0f * (cmd->min.x / 360.0f);
    F32 f26 = 255.0f * (cmd->min.y / 360.0f);
    F32 f25 = 255.0f * (cmd->min.z / 360.0f);

    while (p)
    {
#ifdef PLATFORM_PC
        // The step truncates to a whole byte-angle. A sixtieth of a second of
        // turn is a small number, so a shorter frame rounds it to zero and the
        // particle stops turning: a 255-a-second spin holds still above 255
        // fps and runs at half speed from 128.
        p->m_rotdeg[0] += rot_step((f30 * xurand() + f27) * (1.0f / 60.0f), dt);
        p->m_rotdeg[1] += rot_step((f29 * xurand() + f26) * (1.0f / 60.0f), dt);
        p->m_rotdeg[2] += rot_step((f28 * xurand() + f25) * (1.0f / 60.0f), dt);
#else
        p->m_rotdeg[0] += (U8)(dt * (f30 * xurand() + f27));
        p->m_rotdeg[1] += (U8)(dt * (f29 * xurand() + f26));
        p->m_rotdeg[2] += (U8)(dt * (f28 * xurand() + f25));
#endif

        p = p->m_next;
    }
}

void xParCmdVelocityApply_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;

    while (p)
    {
        xVec3Add(&p->m_pos, &p->m_pos, &p->m_vel);

        p = p->m_next;
    }
}

void xParCmdRotateAround_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    xParCmdRotateAround* cmd = (xParCmdRotateAround*)c->tasset;

    F32 yaw = PI * (dt * cmd->yaw) / 180.0f;
    F32 radius_growth = dt * cmd->radius_growth;

    while (p)
    {
        xVec3 at;
        xVec3Sub(&at, &cmd->pos, &p->m_pos);

        at.y = 0.0f;

        xMat3x3 lookmat;

        F32 radius = xMat3x3LookVec(&lookmat, &at);

        xVec3 angles;
        xMat3x3GetEuler(&lookmat, &angles);

        angles.x += yaw;

        xMat3x3 rotmat;
        xMat3x3Euler(&rotmat, angles.x, angles.y, angles.z);

        radius += radius_growth;

        xVec3 var_BC, var_C8;

        var_BC.x = 0.0f;
        var_BC.y = 0.0f;
        var_BC.z = radius;

        xMat3x3RMulVec(&var_C8, &rotmat, &var_BC);

        p->m_pos.x = var_C8.x + cmd->pos.x;
        p->m_pos.z = var_C8.z + cmd->pos.z;

        p = p->m_next;
    }
}

void xParCmdTex_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
}

void xParCmdTexAnim_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdTexAnim* cmd = (xParCmdTexAnim*)c->tasset;
    xParCmdTex* tex = ps->m_cmdTex;

    if (!tex)
    {
        return;
    }

#ifdef PLATFORM_PC
    // Every mode below advances the flipbook by exactly one cell per call, so
    // with no throttle the animation runs at the frame rate. A throttle of a
    // sixtieth of a second is what the console gave it.
    F32 throttle = cmd->throttle_time > 0.0f ? cmd->throttle_time : 1.0f / 60.0f;

    cmd->throttle_time_elapsed -= dt;

    // Retail leaves the field alone when there is no throttle, so an asset
    // authored without one need never have held a sane value.
    if (cmd->throttle_time_elapsed > throttle)
    {
        cmd->throttle_time_elapsed = throttle;
    }

    if (cmd->throttle_time_elapsed > 0.0f)
    {
        return;
    }

    cmd->throttle_time_elapsed = throttle;
#else
    if (cmd->throttle_time > 0.0f)
    {
        cmd->throttle_time_elapsed -= dt;

        if (cmd->throttle_time_elapsed > 0.0f)
        {
            return;
        }

        cmd->throttle_time_elapsed = cmd->throttle_time;
    }
#endif

    p = ps->m_root;

    if (cmd->anim_mode == 0)
    {
        while (p)
        {
            p->m_texIdx[0] = xrand() % tex->cols;
            p->m_texIdx[1] = xrand() % tex->rows;

            p = p->m_next;
        }
    }
    else if (cmd->anim_mode == 1)
    {
        if (tex->cols > 1)
        {
            while (p)
            {
                p->m_texIdx[0]++;

                if (p->m_texIdx[0] >= tex->cols)
                {
                    p->m_texIdx[0] = 0;

                    if (cmd->anim_wrap_mode == 1)
                    {
                        p->m_texIdx[1]++;

                        if (p->m_texIdx[1] >= tex->rows)
                        {
                            p->m_texIdx[1] = 0;
                        }
                    }
                    else if (cmd->anim_wrap_mode == 2)
                    {
                        if (p->m_texIdx[1] == 0)
                        {
                            p->m_texIdx[1] = tex->rows - 1;
                        }
                        else
                        {
                            p->m_texIdx[1]--;
                        }
                    }
                    else if (cmd->anim_wrap_mode == 3)
                    {
                        p->m_texIdx[1]++;

                        if (p->m_texIdx[1] >= tex->rows)
                        {
                            p->m_texIdx[1] = tex->rows - 1;
                            p->m_texIdx[0] = tex->cols - 1;
                        }
                    }
                }

                p = p->m_next;
            }
        }
    }
    else if (cmd->anim_mode == 2)
    {
        if (tex->cols > 1)
        {
            while (p)
            {
                if (p->m_texIdx[0] == 0)
                {
                    p->m_texIdx[0] = tex->cols - 1;

                    if (cmd->anim_wrap_mode == 1)
                    {
                        p->m_texIdx[1]++;

                        if (p->m_texIdx[1] >= tex->rows)
                        {
                            p->m_texIdx[1] = 0;
                        }
                    }
                    else if (cmd->anim_wrap_mode == 2)
                    {
                        if (p->m_texIdx[1] == 0)
                        {
                            p->m_texIdx[1] = tex->rows - 1;
                        }
                        else
                        {
                            p->m_texIdx[1]--;
                        }
                    }
                }
                else
                {
                    p->m_texIdx[0]--;
                }

                p = p->m_next;
            }
        }
    }
    else if (cmd->anim_mode == 3)
    {
        if (tex->rows > 1)
        {
            while (p)
            {
                if (p->m_texIdx[1] == 0)
                {
                    p->m_texIdx[1] = tex->rows - 1;

                    if (cmd->anim_wrap_mode == 1)
                    {
                        p->m_texIdx[0]++;

                        if (p->m_texIdx[0] >= tex->cols)
                        {
                            p->m_texIdx[0] = 0;
                        }
                    }
                    else if (cmd->anim_wrap_mode == 2)
                    {
                        if (p->m_texIdx[0] == 0)
                        {
                            p->m_texIdx[0] = tex->cols - 1;
                        }
                        else
                        {
                            p->m_texIdx[0]--;
                        }
                    }
                }
                else
                {
                    p->m_texIdx[1]--;
                }

                p = p->m_next;
            }
        }
    }
    else if (cmd->anim_mode == 4)
    {
        if (tex->rows > 1)
        {
            while (p)
            {
                p->m_texIdx[1]++;

                if (p->m_texIdx[1] >= tex->rows)
                {
                    p->m_texIdx[1] = 0;

                    if (cmd->anim_wrap_mode == 1)
                    {
                        p->m_texIdx[0]++;

                        if (p->m_texIdx[0] >= tex->cols)
                        {
                            p->m_texIdx[0] = 0;
                        }
                    }
                    else if (cmd->anim_wrap_mode == 2)
                    {
                        if (p->m_texIdx[0] == 0)
                        {
                            p->m_texIdx[0] = tex->cols - 1;
                        }
                        else
                        {
                            p->m_texIdx[0]--;
                        }
                    }
                }

                p = p->m_next;
            }
        }
    }
}

void xParCmdCollideFall_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xParCmdCollideFall& cmd = *(xParCmdCollideFall*)c->tasset;
    xPar* p = ps->m_root;

    while (p)
    {
        F32& loc = p->m_pos.y;
        F32& vel = p->m_vel.y;
        F32 dloc = cmd.y - loc;

        if (dloc < 0.0f)
        {
            // lol
        }
        else
        {
            loc = dloc * cmd.bounce + cmd.y;

            if (vel < 0.0f)
            {
                vel = -vel * cmd.bounce;
            }
        }

        p = p->m_next;
    }
}

void xParCmdCollideFallSticky_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xParCmdCollideFallSticky& cmd = *(xParCmdCollideFallSticky*)c->tasset;
#ifdef PLATFORM_PC
    // A particle resting on the plane is damped every frame it stays there, so
    // the fraction that survives has to be the frame's share of a sixtieth of a
    // second.
    // xpow of a negative base is a NaN, and a NaN velocity never comes back.
    F32 xzdamp = 1.0f - cmd.sticky;
    if (xzdamp > 0.0f)
    {
        xzdamp = xpow(xzdamp, 60.0f * dt);
    }
#else
    F32 xzdamp = 1.0f - cmd.sticky;
#endif
    xPar* p = ps->m_root;

    while (p)
    {
        F32& loc = p->m_pos.y;
        F32& vel = p->m_vel.y;
        F32 dloc = cmd.y - loc;

        if (dloc < 0.0f)
        {
            // lol
        }
        else
        {
            loc = dloc * cmd.bounce + cmd.y;

            if (vel < 0.0f)
            {
                vel = -vel * cmd.bounce;
            }

            p->m_vel.x *= xzdamp;
            p->m_vel.z *= xzdamp;
        }

        p = p->m_next;
    }
}

void xParCmd_DampenSpeed_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdDampenData* cmd = (xParCmdDampenData*)c->tasset;

    if (cmd->enabled)
    {
        p = ps->m_root;

        F32 damp = dt * cmd->dampSpeed;

        while (p)
        {
            xVec3AddScaled(&p->m_vel, &p->m_vel, damp);

            p = p->m_next;
        }
    }
}

void xParCmd_SizeInOut_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdSizeInOutData* cmd = (xParCmdSizeInOutData*)c->tasset;

    if (cmd->enabled)
    {
        p = ps->m_root;

        S32 i, seg;
        F32 slope_size[3];
        F32 frac;

        slope_size[0] = 3.0f * (cmd->custSize[1] - cmd->custSize[0]);
        slope_size[1] = 3.0f * (cmd->custSize[2] - cmd->custSize[1]);
        slope_size[2] = 3.0f * (cmd->custSize[3] - cmd->custSize[2]);

        while (p)
        {
            frac = 1.0f - p->m_lifetime / p->totalLifespan;
            frac = CLAMP(frac, 0.0f, 1.0f);

            if (frac < 0.33333334f)
            {
                seg = 0;
            }
            else if (frac < 0.6666667f)
            {
                seg = 1;
            }
            else
            {
                seg = 2;
            }

            for (i = seg; i > 0; i--)
            {
                frac -= 0.33333334f;
            }

            p->m_size = frac * slope_size[seg] + cmd->custSize[seg];

            p = p->m_next;
        }
    }
}

void xParCmd_AlphaInOut_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdAlphaInOutData* cmd = (xParCmdAlphaInOutData*)c->tasset;

    if (cmd->enabled)
    {
        p = ps->m_root;

        S32 i, seg;
        F32 slope_alfa[3];
        F32 frac;
        F32 alfa;

        slope_alfa[0] = 3.0f * (cmd->custAlpha[1] - cmd->custAlpha[0]);
        slope_alfa[1] = 3.0f * (cmd->custAlpha[2] - cmd->custAlpha[1]);
        slope_alfa[2] = 3.0f * (cmd->custAlpha[3] - cmd->custAlpha[2]);

        while (p)
        {
            frac = 1.0f - p->m_lifetime / p->totalLifespan;
            frac = CLAMP(frac, 0.0f, 1.0f);

            if (frac < 0.33333334f)
            {
                seg = 0;
            }
            else if (frac < 0.6666667f)
            {
                seg = 1;
            }
            else
            {
                seg = 2;
            }

            for (i = seg; i > 0; i--)
            {
                frac -= 0.33333334f;
            }

            alfa = frac * slope_alfa[seg] + cmd->custAlpha[seg];
            p->m_cfl[3] = CLAMP(alfa, 0.0f, 255.0f);
            p->m_c[3] = (U8)p->m_cfl[3];

            p = p->m_next;
        }
    }
}

void xParCmd_Shaper_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdShaperData* cmd = (xParCmdShaperData*)c->tasset;

    if (cmd->enabled)
    {
        F32 damp = dt * cmd->dampSpeed;

        // An acceleration on a per-frame displacement. See xParCmdFollow_Update.
        // damp above is not one -- it scales m_vel by a fraction of itself,
        // which compounds to the same factor a second whatever the frame rate.
#ifdef PLATFORM_PC
        F32 grav = dt * cmd->gravity * (60.0f * dt);
#else
        F32 grav = dt * cmd->gravity;
#endif
        S32 doalpha = 1;
        S32 dosize = 1;

        if (cmd->custAlpha[0] < 0.0f)
        {
            doalpha = 0;
        }

        if (cmd->custSize[0] < 0.0f)
        {
            dosize = 0;
        }

        S32 i, seg;
        F32 slope_alfa[3];
        F32 slope_size[3];
        F32 frac;
        F32 alfa;

        for (i = 0; i < 3; i++)
        {
            slope_size[i] = 3.0f * (cmd->custSize[i + 1] - cmd->custSize[i]);
            slope_alfa[i] = 3.0f * (cmd->custAlpha[i + 1] - cmd->custAlpha[i]);
        }

        p = ps->m_root;

        while (p)
        {
            xVec3AddScaled(&p->m_vel, &p->m_vel, damp);
            p->m_vel.y -= grav;

            if (p->totalLifespan < 1e-5f || (!dosize && !doalpha))
            {
                p = p->m_next;
                continue;
            }

            frac = 1.0f - p->m_lifetime / p->totalLifespan;
            frac = CLAMP(frac, 0.0f, 1.0f);

            if (frac < 0.33333334f)
            {
                seg = 0;
            }
            else if (frac < 0.6666667f)
            {
                seg = 1;
            }
            else
            {
                seg = 2;
            }

            for (i = seg; i > 0; i--)
            {
                frac -= 0.33333334f;
            }

            if (dosize)
            {
                p->m_size = frac * slope_size[seg] + cmd->custSize[seg];
            }

            if (doalpha)
            {
                alfa = frac * slope_alfa[seg] + cmd->custAlpha[seg];
                p->m_cfl[3] = CLAMP(alfa, 0.0f, 255.0f);
                p->m_c[3] = (U8)p->m_cfl[3];
            }

            p = p->m_next;
        }
    }
}

WEAK F32 xVec3LengthFast(F32 x, F32 y, F32 z)
{
    F32 len;
    xsqrtfast(len, SQR(x) + SQR(y) + SQR(z));
    return len;
}
