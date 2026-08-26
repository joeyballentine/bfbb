#include "xVec3.h"
#include "xCollide.h"
#include "xMathInlines.h"
#include "iMath.h"
#include "xMath.h"

#include <types.h>
// Paired-single inline assembly; there is no host equivalent and no reason to
// pretend otherwise.
#ifdef __MWERKS__
#include <fastmath.h>
#endif

const xVec3 xVec3::m_Null = { 0.0f, 0.0f, 0.0f };
const xVec3 xVec3::m_UnitAxisX = { 1.0f, 0.0f, 0.0f };
const xVec3 xVec3::m_UnitAxisY = { 0.0f, 1.0f, 0.0f };

F32 xVec3Normalize(xVec3* o, const xVec3* v)
{
    F32 x = v->x;
    F32 x2 = SQR(v->x);
    F32 y = v->y;
    F32 y2 = SQR(v->y);
    F32 z = v->z;
    F32 z2 = SQR(v->z);

    F32 len;
    F32 len2 = x2 + y2 + z2;

    if ((F32)iabs(len2 - 1.0f) <= 0.00001f)
    {
        o->x = x;
        o->y = y;
        o->z = z;
        len = 1.0f;
    }
    else if ((F32)iabs(len2) <= 0.00001f)
    {
        o->x = 0.0f;
        o->y = 1.0f;
        o->z = 0.0f;
        len = 0.0f;
    }
    else
    {
        len = xsqrt(len2);
        F32 inv_len = 1.0f / len;
        o->x = v->x * inv_len;
        o->y = v->y * inv_len;
        o->z = v->z * inv_len;
    }
    return len;
}

F32 xVec3NormalizeFast(xVec3* o, const xVec3* v)
{
    F32 x = v->x;
    F32 x2 = SQR(v->x);
    F32 y = v->y;
    F32 y2 = SQR(v->y);
    F32 z = v->z;
    F32 z2 = SQR(v->z);

    F32 len;
    F32 len2 = x2 + y2 + z2;

    if ((F32)iabs(len2 - 1.0f) <= 0.00001f)
    {
        o->x = x;
        o->y = y;
        o->z = z;
        len = 1.0f;
    }
    else if ((F32)iabs(len2) <= 0.00001f)
    {
        o->x = 0.0f;
        o->y = 1.0f;
        o->z = 0.0f;
        len = 0.0f;
    }
    else
    {
        xsqrtfast(len, len2);
        F32 inv_len = 1.0f / len;
        o->x = v->x * inv_len;
        o->y = v->y * inv_len;
        o->z = v->z * inv_len;
    }
    return len;
}

#ifdef __MWERKS__

void xVec3Copy(register xVec3* dst, const register xVec3* src)
{
    PSVECCopy(dst, src);
}

asm F32 xVec3Dot(const register xVec3* a, const register xVec3* b)
{
    PSVECDotProduct(a, b)
}

#else

// The GameCube bodies above are Gekko paired-single assembly: two floats per
// register, two multiplies per instruction. Nothing on a host has that, so
// these are what the assembly computes, written out.

void xVec3Copy(xVec3* dst, const xVec3* src)
{
    *dst = *src;
}

// PSVECDotProduct loads (y,z) and (x,y) as pairs, multiplies both halves at
// once, and finishes with ps_sum0, which adds ps0 of one register to ps1 of
// another. The order that falls out is (x*x2 + y*y2) + z*z2 -- and float
// addition is not associative, so the order is the part worth preserving.
//
// The console also gets the first two terms from a single ps_madd, one rounding
// where this has two. A host compiler with FMA contraction enabled will
// usually fuse them back; where it does not, the difference is one ulp.
F32 xVec3Dot(const xVec3* a, const xVec3* b)
{
    return (a->x * b->x + a->y * b->y) + a->z * b->z;
}

#endif
