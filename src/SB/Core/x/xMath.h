#ifndef XMATH_H
#define XMATH_H

#include <types.h>

#include "iMath.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define xabs(x) iabs(x)

#define xeq(a, b, e) (xabs((a) - (b)) <= (e))
#define xfeq0(x) (((x) >= -1e-5f) && ((x) <= 1e-5f))

#define CLAMP(x, a, b) (MAX((a), MIN((x), (b))))
#define xlerp(a, b, t) ((a) + (t) * ((b) - (a)))

#define SQR(x) ((x) * (x))

#define ALIGN(x, a) ((x) + ((a)-1) & ~((a)-1))

// Override these to point to their corresponding symbols in .sdata2
// For example:
//     #undef PI
//     #undef ONEEIGHTY
//     #define PI _771_1
//     #define ONEEIGHTY _778_0
#define PI 3.1415927f
#define ONEEIGHTY 180.0f

#define DEG2RAD(x) ((PI) * (x) / (ONEEIGHTY))
#define RAD2DEG(x) ((ONEEIGHTY) * (x) / (PI))

#define FLOAT_MAX 1e38f
#define FLOAT_MIN -1e38f

#define XRAY3_USE_MIN (1 << 10)
#define XRAY3_USE_MAX (1 << 11)

struct xFuncPiece
{
    F32 coef[5];
    F32 end;
    S32 order;
    xFuncPiece* next;
};

F32 xlog(F32 f);

void xMathInit();
void xMathExit();
F32 xatof(const char* x);
void xsrand(U32 seed);
U32 xrand();
F32 xurand();
U32 xMathSolveQuadratic(F32 a, F32 b, F32 c, F32* x1, F32* x2);
U32 xMathSolveCubic(F32 a, F32 b, F32 c, F32 d, F32* x1, F32* x2, F32* x3);
F32 xAngleClamp(F32 a);
F32 xAngleClampFast(F32 a);
F32 xDangleClamp(F32 a);
void xAccelMove(F32& x, F32& v, F32 a, F32 dt, F32 endx, F32 maxv);
F32 xAccelMoveTime(F32 dx, F32 a, F32, F32 maxv);
void xAccelMove(F32& x, F32& v, F32 a, F32 dt, F32 maxv);
void xAccelStop(F32& x, F32& v, F32 a, F32 dt);
F32 xFuncPiece_Eval(xFuncPiece* func, F32 param, xFuncPiece** iterator);
void xFuncPiece_EndPoints(xFuncPiece* func, F32 pi, F32 pf, F32 fi, F32 ff);
void xFuncPiece_ShiftPiece(xFuncPiece* shift, xFuncPiece* func, F32 newZero);
F32 xSCurve(F32 t, F32 softness);
F32 xSCurve(F32 t);
void xsqrtfast(F32& dst, F32 num);

F32 xrmod(F32 ang);

#ifdef PLATFORM_PC
// Rescales a per-FRAME particle count to the frame's own length. Retail spawns
// `count` particles every frame at a sixtieth of a second, which is a rate of
// `60 * count` a second; this returns what that rate asks for in `dt` seconds.
// The leftover fraction is resolved by a coin flip, so the average holds
// without the caller keeping an accumulator.
U32 xFrameEmitCount(F32 count, F32 dt);

// The same for a per-FRAME probability. `chance` is how often a frame spawned
// on the console; the result is the chance for a frame of `dt` seconds that
// keeps the same number of spawns a second.
F32 xFrameEmitChance(F32 chance, F32 dt);

// A per-FRAME exponential approach rebased onto the frame's own length. `k` is
// the fraction of the remaining distance a console frame closed; the result is
// the fraction for a frame of `dt` seconds. What compounds is the part left
// over, hence the 1 - k on both sides.
//
// `k` is clamped into [0,1] first. Several callers read it straight out of
// level data with no bound of their own, and xpow of a negative base with a
// fractional exponent is a NaN -- one that never washes out once it has reached
// a position, a yaw or a quaternion, and that retail's plain multiply could not
// produce.
F32 xFrameApproach(F32 k, F32 dt);
#endif

template <class T> T range_limit(T v, T minv, T maxv);

// The primary has no definition anywhere; every use resolves to one of these,
// which are defined in the units that needed them -- xCamera and
// zNPCTypeBossPlankton for F32, xScene for U16, zDiscoFloor for S32 and size_t.
// Declaring them here is what makes a use see the specialization before it
// would otherwise instantiate the primary. Standard C++ requires that;
// CodeWarrior does not care, which is why they were only ever declared where
// they were defined.
template <> F32 range_limit<F32>(F32 v, F32 minv, F32 maxv);
template <> U16 range_limit<U16>(U16 v, U16 minv, U16 maxv);
template <> S32 range_limit<S32>(S32 v, S32 minv, S32 maxv);
template <> size_t range_limit<size_t>(size_t v, size_t minv, size_t maxv);

#endif
