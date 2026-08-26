#ifndef IMATH_H
#define IMATH_H

#include <types.h>

#include <math.h>

// The GameCube build reaches fabsf through CodeWarrior's __fabs intrinsic,
// which lowers to a single fabs instruction. On a host build the libm call is
// what every compiler already recognises and inlines, so there is nothing to
// gain from an intrinsic spelling here.
#define iabs(x) fabsf((F32)(x))

F32 isin(F32 x);
F32 icos(F32 x);
F32 itan(F32 x);

#endif
