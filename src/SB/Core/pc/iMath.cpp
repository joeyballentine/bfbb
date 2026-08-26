#include "iMath.h"

#include <math.h>

// Retail routes these through MSL's std::sinf, which is a float wrapper over
// the double-precision libm call. Calling sinf directly is the same function
// with one less layer; the result differs from the GameCube's only where the
// GameCube's own libm differed from glibc's, which is below the precision the
// game's callers care about.

F32 isin(F32 x)
{
    return sinf(x);
}

F32 icos(F32 x)
{
    return cosf(x);
}

F32 itan(F32 x)
{
    return tanf(x);
}
