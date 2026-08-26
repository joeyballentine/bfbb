#include <intrin.h>

// CodeWarrior lowers these to single PowerPC instructions and never emits a
// call, so nothing on the GameCube side needs a body. glibc declares __fabs and
// __fabsf -- they are its internal aliases for fabs -- but does not define
// them, so linking a host build that calls either fails without these.
//
// Five files in src/SB call __fabs directly, and FABS() in compat/math.h is
// defined in terms of it.

double __fabs(double x)
{
    return __builtin_fabs(x);
}

float __fabsf(float x)
{
    return __builtin_fabsf(x);
}
