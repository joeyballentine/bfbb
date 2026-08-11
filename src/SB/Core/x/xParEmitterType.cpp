#include "xParEmitterType.h"

#include <types.h>
#include <xMath.h>
#include <xMathInlines.h>
#include <xParEmitter.h>
#include <iModel.h>

// These structs were used in deadstripped functions.
// This function is here to force the symbols to be linked.
//
// The target object opens .rodata with eleven all-zero anonymous templates
// that nothing in the object references, which puts every later .rodata
// relocation 0x148 ahead of ours. Same shape and the same anonymous indices
// as zVar.cpp, which uses this idiom in a Matching unit.
void __deadstripped_xParEmitterType()
{
    const char _427[0x0C] = {};
    const char _428[0x0C] = {};
    const char _432[0x0C] = {};
    const char _463[0x0C] = {};

    const char _617[0x28] = {};
    const char _618[0x28] = {};
    const char _619[0x28] = {};
    const char _620[0x28] = {};
    const char _621[0x28] = {};
    const char _622[0x28] = {};
    const char _623[0x28] = {};
}

void xParEmitterEmitPoint(xPar* p, xParEmitterAsset* a, F32 dt)
{
    p->m_vel = a->vel;
    xVec3SMulBy(&p->m_vel, dt);
    xParEmitterAngleVariation(p, a);
}

void xParEmitterAngleVariation(xPar* p, xParEmitterAsset* a)
{
    F32 vary = a->vel_angle_variation;

    if (vary != 0.0f)
    {
        xMat3x3 mat = { 0 };
        F32 ang[3] = { vary * (xurand() - 0.5f), vary * (xurand() - 0.5f),
                       vary * (xurand() - 0.5f) };

        xMat3x3Euler(&mat, ang[0], ang[1], ang[2]);
        xMat3x3LMulVec(&p->m_vel, &mat, &p->m_vel);
    }
}

void xParEmitterEmitCircleEdge(xPar* p, xParEmitterAsset* a, F32 dt)
{
    F32 sinr;
    F32 cosr;
    F32 rot;
    F32 dz;
    F32 dx;
    F32 deflect;

    p->m_vel = a->vel;
    xVec3SMulBy(&p->m_vel, dt);

    rot = 6.2831855f * xurand();
    cosr = icos(rot);
    sinr = isin(rot);

    {
        xVec2 rmat[2] = { { 1.0f, 0.0f }, { 0.0f, 1.0f } };

        xVec2Init(&rmat[0], cosr, sinr);
        xVec2Init(&rmat[1], -sinr, cosr);

        xVec2 dir = { 0.0f, 1.0f };

        dx = xVec2Dot(&rmat[0], &dir);
        dx *= a->e_circle.radius;
        dz = xVec2Dot(&rmat[1], &dir);
        dz *= a->e_circle.radius;
    }

    p->m_pos.x += dx;
    p->m_pos.z += dz;

    xParEmitterAngleVariation(p, a);

    if (a->e_circle.deflection != 0.0f)
    {
        deflect = a->e_circle.deflection * (a->e_circle.radius * dt);
        p->m_vel.x += dx * deflect;
        p->m_vel.z += dz * deflect;
    }
}

void xParEmitterEmitCircle(xPar* p, xParEmitterAsset* a, F32 dt)
{
    F32 cosr;
    F32 sinr;
    F32 rot;
    F32 dx;
    F32 dz;
    F32 deflect;

    p->m_vel = a->vel;
    xVec3SMulBy(&p->m_vel, dt);

    rot = 6.2831855f * xurand();
    cosr = icos(rot);
    sinr = isin(rot);

    {
        xVec2 rmat[2] = { { 1.0f, 0.0f }, { 0.0f, 1.0f } };

        xVec2Init(&rmat[0], cosr, sinr);
        xVec2Init(&rmat[1], -sinr, cosr);

        xVec2 dir = { 0.0f, 1.0f };
        F32 rad = xurand();

        dir.y = -((rad * rad) - 1.0f) * a->e_circle.radius;

        dx = xVec2Dot(&rmat[0], &dir);
        dz = xVec2Dot(&rmat[1], &dir);
    }

    p->m_pos.x += dx;
    p->m_pos.z += dz;

    xParEmitterAngleVariation(p, a);

    if (a->e_circle.deflection != 0.0f)
    {
        deflect = a->e_circle.deflection * (a->e_circle.radius * dt);
        p->m_vel.x += dx * deflect;
        p->m_vel.z += dz * deflect;
    }
}

void xParEmitterEmitRect(xPar* p, xParEmitterAsset* a, F32 dt)
{
    F32 x_length;
    F32 z_length;

    p->m_vel = a->vel;
    xVec3SMulBy(&p->m_vel, dt);
    x_length = a->e_rect.x_len;
    p->m_pos.x += (2.0f * (x_length * xurand())) - x_length;
    z_length = a->e_rect.z_len;
    p->m_pos.z += (2.0f * (z_length * xurand())) - z_length;
    xParEmitterAngleVariation(p, a);
}

void xParEmitterEmitRectEdge(xPar* p, xParEmitterAsset* a, F32 dt)
{
    F32 r;

    p->m_vel = a->vel;
    xVec3SMulBy(&p->m_vel, dt);

    r = xurand();
    if ((2.0f * (r * a->e_rect.x_len)) > a->e_rect.x_len)
    {
        p->m_pos.x += a->e_rect.x_len;
    }
    else
    {
        p->m_pos.x -= a->e_rect.x_len;
    }

    r = xurand();
    if ((2.0f * (r * a->e_rect.z_len)) >= a->e_rect.z_len)
    {
        p->m_pos.z += a->e_rect.z_len;
    }
    else
    {
        p->m_pos.z -= a->e_rect.z_len;
    }

    xParEmitterAngleVariation(p, a);
}

void xParEmitterEmitLine(xPar* p, xParEmitterAsset* a, F32 dt)
{
    xVec3 dir;
    F32 len;
    F32 dist;
    F32 yaw;
    F32 pitch;
    F32 rad;

    p->m_vel = a->vel;
    xVec3SMulBy(&p->m_vel, dt);

    xVec3Sub(&dir, &a->e_line.pos2, &a->e_line.pos1);
    len = xVec3Normalize(&dir, &dir);
    dist = len * xurand();

    p->m_pos.x = dir.x * dist + a->e_line.pos1.x;
    p->m_pos.y = dir.y * dist + a->e_line.pos1.y;
    p->m_pos.z = dir.z * dist + a->e_line.pos1.z;

    if (a->e_line.radius > 0.0f)
    {
        yaw = 6.2831855f * xurand();
        pitch = 6.2831855f * xurand();

        F32 sy = isin(yaw);
        F32 cy = icos(yaw);
        F32 sp = isin(pitch);
        F32 cp = icos(pitch);

        xVec3 off = { -sy, cy * sp, cy * cp };

        rad = xurand();

        p->m_pos += off * (a->e_line.radius * -((rad * (rad * rad)) - 1.0f));
    }

    xParEmitterAngleVariation(p, a);
}

void xParEmitterEmitSphere(xPar* p, xParEmitterAsset* a, F32 dt)
{
    F32 yaw;
    F32 pitch;

    p->m_vel = a->vel * dt;

    yaw = 6.2831855f * xurand();
    pitch = 6.2831855f * xurand();

    {
        F32 sy = isin(yaw);
        F32 cy = icos(yaw);
        F32 sp = isin(pitch);
        F32 cp = icos(pitch);

        xVec3 dir = { -sy, cy * sp, cy * cp };

        p->m_pos += dir * (a->e_sphere.radius * xurand());
    }

    xParEmitterAngleVariation(p, a);
}

void xParEmitterEmitSphereEdge(xPar* p, xParEmitterAsset* a, F32 dt, S32 subtype)
{
    xVec3 dir = { 0.0f, 0.0f, 1.0f };
    xVec3 off = { 0.0f, 0.0f, 0.0f };
    xMat3x3 mat = { 0 };
    F32 pitch;
    F32 roll;

    p->m_vel = a->vel;
    xVec3SMulBy(&p->m_vel, dt);

    switch (subtype)
    {
    case 7:
        roll = 6.2831855f * xurand();
        pitch = 6.2831855f * xurand();
        xMat3x3Euler(&mat, 6.2831855f * xurand(), pitch, roll);
        break;
    case 10:
        pitch = (3.1415927f * xurand()) + 3.1415927f;
        xMat3x3Euler(&mat, 6.2831855f * xurand(), pitch, 0.0f);
        break;
    case 11:
        pitch = 3.1415927f * xurand();
        xMat3x3Euler(&mat, 6.2831855f * xurand(), pitch, 0.0f);
        break;
    }

    dir = mat.at;

    xVec3SMul(&p->m_vel, &dir, xVec3Length(&p->m_vel));
    xVec3SMul(&off, &dir, a->e_sphere.radius);
    xVec3AddTo(&p->m_pos, &off);

    xParEmitterAngleVariation(p, a);
}

void xParEmitterEmitVolume(xPar* p, xParEmitterAsset* a, F32 dt, xVolume* vol)
{
    xBound* b;
    xVec3 size;

    p->m_vel = a->vel;
    xVec3SMulBy(&p->m_vel, dt);

    if (vol != NULL)
    {
        b = vol->GetBound();
        if (b->type == XBOUND_TYPE_BOX)
        {
            xVec3Sub(&size, &b->box.box.upper, &b->box.box.lower);
            size.x *= xurand();
            size.y *= xurand();
            size.z *= xurand();
            xVec3Add(&p->m_pos, &size, &b->box.box.lower);
        }
    }

    xParEmitterAngleVariation(p, a);
}

void xParEmitterEmitEntity(xPar* p, xParEmitterAsset* a, F32 dt, xEnt* ent)
{
    U32 total;
    U32 which;
    U32 count;
    xModelInstance* model;
    xVec3 loc;

    total = 0;

    model = ent->model;
    while (model != NULL)
    {
        if (!(model->Flags & 0x8000))
        {
            total += model->Data->geometry->numVertices;
        }
        model = model->Next;
    }

    which = (xrand() >> 13) % total;

    model = ent->model;
    while (model != NULL)
    {
        if (!(model->Flags & 0x8000))
        {
            count = model->Data->geometry->numVertices;
            if (which < count)
            {
                iModelVertEval(model->Data, which, 1, model->Mat, NULL, &loc);
                break;
            }
            which -= count;
        }
        model = model->Next;
    }

    p->m_pos = loc;
    p->m_vel = a->vel * dt;

    xParEmitterAngleVariation(p, a);
}

void xParEmitterEmitOffsetPoint(xParEmitter* pe, xPar* p, xParEmitterAsset* a, xEnt* ent)
{
    xModelInstance* model;
    RpAtomic* data;

    if (ent != NULL)
    {
        model = ent->model;
        data = model->Data;

        if (data->geometry->morphTarget->verts != NULL)
        {
            iModelTagEval(data, &pe->tag, model->Mat, &p->m_pos);
        }
    }
    else
    {
        p->m_pos += a->e_offsetp.offset;
    }

    xParEmitterAngleVariation(p, a);
}

void xParEmitterEmitVCylEdge(xPar* p, xParEmitterAsset* a, F32 dt)
{
    F32 ang;
    F32 deflect;

    p->m_vel *= dt;

    ang = 6.2831855f * xurand();

    {
        xVec2 dir = { isin(ang), icos(ang) };
        xVec2 off = dir * a->e_vcyl.radius;

        p->m_pos.x += off.x;
        p->m_pos.y += a->e_vcyl.height * xurand();
        p->m_pos.z += off.y;

        deflect = a->e_vcyl.deflection * dt;

        p->m_vel.x += off.x * deflect;
        p->m_vel.z += off.y * deflect;
    }

    xParEmitterAngleVariation(p, a);
}

namespace
{
    void ocircle_emit(xPar& p, xParEmitterAsset& a, F32 dt, F32 radius);
}

void xParEmitterEmitOCircleEdge(xPar* p, xParEmitterAsset* a, F32 dt)
{
    ocircle_emit(*p, *a, dt, a->e_circle.radius);
}

namespace
{
    void ocircle_emit(xPar& p, xParEmitterAsset& a, F32 dt, F32 radius)
    {
        xMat3x3 mat;

        xMat3x3LookVec(&mat, &a.e_circle.dir);

        F32 ang = 6.2831855f * xurand();
        F32 cosa = icos(ang);
        F32 sina = isin(ang);

        xVec3 dir = { cosa, sina, 0.0f };

        dir *= radius;

        xMat3x3RMulVec(&dir, &mat, &dir);

        p.m_pos += dir;
        p.m_vel = a.e_circle.dir * a.vel.y;

        xParEmitterAngleVariation(&p, &a);

        if (a.e_circle.deflection != 0.0f)
        {
            p.m_vel += dir * (radius * a.e_circle.deflection);
        }

        p.m_vel *= dt;
    }
}

void xParEmitterEmitOCircle(xPar* p, xParEmitterAsset* a, F32 dt)
{
    F32 rad = xurand();
    F32 scale = -((rad * rad) - 1.0f);

    ocircle_emit(*p, *a, dt, scale * a->e_circle.radius);
}

namespace
{
    void transform_ent_bone(xVec3& loc, xVec3& vel, const xParEmitterAsset& a,
                            const xMat4x3& mat);
}

// NOTE: retail takes (xVec3&, xVec3&, const xParEmitterAsset&, const xEnt&) -
// xParEmitterType.h is missing both `const`s. See the report.
xMat4x3* xParEmitterTransformEntBone(xVec3& loc, xVec3& vel, xParEmitterAsset& a, xEnt& ent)
{
    static xMat4x3 buffer_mat;
    xMat4x3* mat;
    xMat4x3* bones;

    bones = (xMat4x3*)ent.model->Mat;

    if (a.e_entbone.bone == 0)
    {
        mat = bones;
    }
    else
    {
        xMat4x3Mul(&buffer_mat, &bones[a.e_entbone.bone], bones);
        mat = &buffer_mat;
    }

    transform_ent_bone(loc, vel, a, *mat);

    return mat;
}

namespace
{
    void transform_ent_bone(xVec3& loc, xVec3& vel, const xParEmitterAsset& a,
                            const xMat4x3& mat)
    {
        if (a.e_entbone.flags & 0x1)
        {
            xMat3x3RMulVec(&vel, &mat, &a.vel);
        }
        else
        {
            vel = a.vel;
        }

        if (a.e_entbone.flags & 0x2)
        {
            if (a.e_entbone.flags & 0x4)
            {
                xVec3 flat = { a.e_entbone.offset.x, 0.0f, a.e_entbone.offset.z };

                xMat4x3Toworld(&loc, &mat, &flat);
                loc.y = a.e_entbone.offset.y;
            }
            else
            {
                xMat4x3Toworld(&loc, &mat, &a.e_entbone.offset);
            }
        }
        else
        {
            loc = mat.pos + a.e_entbone.offset;

            if (a.e_entbone.flags & 0x4)
            {
                loc.y = a.e_entbone.offset.y;
            }
        }

        if (a.e_entbone.flags & 0x4)
        {
            loc.y = a.e_entbone.offset.y;
        }
    }
}

// NOTE: retail takes (xVec3&, xVec3&, const xParEmitterAsset&, const xMat4x3&).
void xParEmitterTransformEntBone(xVec3& loc, xVec3& vel, xParEmitterAsset& a, xMat4x3& mat)
{
    transform_ent_bone(loc, vel, a, mat);
}

namespace
{
    xVec3 get_random_offset(const xPEEntBone& eb, const xMat4x3& mat);
}

// NOTE: retail takes (xPar*, xParEmitterAsset*, F32, const xMat4x3&).
void xParEmitterEmitEntBone(xPar* p, xParEmitterAsset* a, F32 dt, xMat4x3& mat)
{
    xVec3 off = get_random_offset(a->e_entbone, mat);

    p->m_pos += off;

    if (a->e_entbone.deflection != 0.0f)
    {
        p->m_vel += off * a->e_entbone.deflection * dt;
    }

    xParEmitterAngleVariation(p, a);
}

namespace
{
    xVec3 get_random_offset(const xPEEntBone& eb, const xMat4x3& mat)
    {
        xVec3 off;

        switch (eb.type)
        {
        case 0:
        {
            off = 0.0f;
            break;
        }
        case 1:
        {
            F32 ang = 6.2831855f * xurand();
            F32 z = 2.0f * xurand() - 1.0f;
            F32 up = xsqrt(-((z * z) - 1.0f));
            F32 rr = xurand();
            F32 rad = -((rr * (rr * rr)) - 1.0f);

            rad *= eb.radius;

            F32 sc = rad * up;

            off.assign(sc * icos(ang), sc * isin(ang), rad * z);
            break;
        }
        case 2:
        {
            F32 ang = 6.2831855f * xurand();
            F32 sc = eb.radius * xsqrt(xurand());

            off.assign(sc * isin(ang), 0.0f, sc * icos(ang));
            break;
        }
        case 3:
        {
            F32 ang = 6.2831855f * xurand();
            F32 sc = eb.radius * xsqrt(xurand());

            off = mat.up * (sc * isin(ang)) + mat.at * (sc * icos(ang));
            break;
        }
        case 4:
        {
            F32 ang = 6.2831855f * xurand();
            F32 sc = eb.radius * xsqrt(xurand());

            off = mat.right * (sc * isin(ang)) + mat.at * (sc * icos(ang));
            break;
        }
        case 5:
        {
            F32 ang = 6.2831855f * xurand();
            F32 sc = eb.radius * xsqrt(xurand());

            off = mat.right * (sc * isin(ang)) + mat.up * (sc * icos(ang));
            break;
        }
        case 6:
        {
            F32 ang = 6.2831855f * xurand();
            F32 z = 2.0f * xurand() - 1.0f;
            F32 sc = eb.radius * xsqrt(-((z * z) - 1.0f));

            off.assign(sc * icos(ang), sc * isin(ang), z * eb.radius);
            break;
        }
        case 7:
        {
            F32 ang = 6.2831855f * xurand();

            off.assign(eb.radius * isin(ang), 0.0f, eb.radius * icos(ang));
            break;
        }
        case 8:
        {
            F32 ang = 6.2831855f * xurand();
            F32 rad = eb.radius;

            off = mat.up * (rad * isin(ang)) + mat.at * (rad * icos(ang));
            break;
        }
        case 9:
        {
            F32 ang = 6.2831855f * xurand();
            F32 rad = eb.radius;

            off = mat.right * (rad * isin(ang)) + mat.at * (rad * icos(ang));
            break;
        }
        case 10:
        {
            F32 ang = 6.2831855f * xurand();
            F32 rad = eb.radius;

            off = mat.right * (rad * isin(ang)) + mat.up * (rad * icos(ang));
            break;
        }
        }

        return off;
    }
}

namespace
{
    xVec3 get_random_offset(const xBound& b, F32 expand, U32 subtype);
}

// NOTE: retail takes (xPar*, xParEmitterAsset*, F32, const xEnt*).
void xParEmitterEmitEntBound(xPar* p, xParEmitterAsset* a, F32 dt, xEnt* ent)
{
    xMat4x3* mat = (xMat4x3*)ent->model->Mat;

    if (a->e_entbound.flags & 0x1)
    {
        xMat3x3RMulVec(&p->m_vel, mat, &a->vel);
    }
    else
    {
        p->m_vel = a->vel;
    }

    xVec3 off = get_random_offset(ent->bound, a->e_entbound.expand, a->e_entbound.type);

    p->m_pos = mat->pos + off;

    if (a->e_entbound.deflection != 0.0f)
    {
        p->m_vel += off * a->e_entbound.deflection;
    }

    p->m_vel *= dt;

    xParEmitterAngleVariation(p, a);
}

namespace
{
    // NOTE: retail wrote the two marked expressions with `xVec3::operator+(F32)`
    // and `xVec3::operator*(const xVec3&)`, neither of which xVec3.h declares.
    // The forms below are the same arithmetic written with what is declared.
    // See the report for the header diff that restores the original shape.
    xVec3 get_random_offset(const xBound& b, F32 expand, U32 subtype)
    {
        xVec3 off;

        switch ((subtype << 3) | b.type)
        {
        case (0 << 3) | XBOUND_TYPE_SPHERE:
        {
            F32 ang = 6.2831855f * xurand();
            F32 z = 2.0f * xurand() - 1.0f;
            F32 up = xsqrt(-((z * z) - 1.0f));
            F32 rr = xurand();
            F32 rad = -((rr * (rr * rr)) - 1.0f);

            rad *= b.sph.r + expand;

            F32 sc = rad * up;

            off.assign(sc * icos(ang), sc * isin(ang), rad * z);
            break;
        }
        case (0 << 3) | XBOUND_TYPE_BOX:
        {
            off = b.box.box.upper - b.box.center;
            off += expand; // retail: off = (upper - center) + expand;

            off.x *= 2.0f * xurand() - 1.0f;
            off.y *= 2.0f * xurand() - 1.0f;
            off.z *= 2.0f * xurand() - 1.0f;
            break;
        }
        case (0 << 3) | XBOUND_TYPE_CYL:
        {
            break;
        }
        case (0 << 3) | XBOUND_TYPE_OBB:
        {
            off = b.box.box.upper;
            off += expand; // retail: off = upper + expand;

            off.x *= 2.0f * xurand() - 1.0f;
            off.y *= 2.0f * xurand() - 1.0f;
            off.z *= 2.0f * xurand() - 1.0f;

            xMat3x3RMulVec(&off, b.mat, &off);
            break;
        }
        case (1 << 3) | XBOUND_TYPE_SPHERE:
        {
            F32 ang = 6.2831855f * xurand();
            F32 z = 2.0f * xurand() - 1.0f;
            F32 rad = b.sph.r + expand;
            F32 sc = rad * xsqrt(-((z * z) - 1.0f));

            off.assign(sc * icos(ang), sc * isin(ang), z * rad);
            break;
        }
        case (1 << 3) | XBOUND_TYPE_BOX:
        {
            xVec3 face;
            U32 r = xrand() >> 13;
            F32 sign = (r & 0x100) ? 1.0f : -1.0f;

            if (r % 3 == 0)
            {
                face.assign(sign, 2.0f * xurand() - 1.0f, 2.0f * xurand() - 1.0f);
            }
            else if (r % 3 == 1)
            {
                face.assign(2.0f * xurand() - 1.0f, sign, 2.0f * xurand() - 1.0f);
            }
            else
            {
                face.assign(2.0f * xurand() - 1.0f, 2.0f * xurand() - 1.0f, sign);
            }

            off = b.box.box.upper - b.box.center;
            off += expand;

            // retail: off = ((upper - center) + expand) * face;
            off.x *= face.x;
            off.y *= face.y;
            off.z *= face.z;
            break;
        }
        case (1 << 3) | XBOUND_TYPE_CYL:
        {
            break;
        }
        case (1 << 3) | XBOUND_TYPE_OBB:
        {
            xVec3 face;
            U32 r = xrand() >> 13;
            F32 sign = (r & 0x100) ? 1.0f : -1.0f;

            if (r % 3 == 0)
            {
                face.assign(sign, 2.0f * xurand() - 1.0f, 2.0f * xurand() - 1.0f);
            }
            else if (r % 3 == 1)
            {
                face.assign(2.0f * xurand() - 1.0f, sign, 2.0f * xurand() - 1.0f);
            }
            else
            {
                face.assign(2.0f * xurand() - 1.0f, 2.0f * xurand() - 1.0f, sign);
            }

            off = b.box.box.upper;
            off += expand;

            // retail: off = (upper + expand) * face;
            off.x *= face.x;
            off.y *= face.y;
            off.z *= face.z;

            xMat3x3RMulVec(&off, b.mat, &off);
            break;
        }
        }

        return off;
    }
}
