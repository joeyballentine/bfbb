#ifndef BFBB_PC_COMPAT_MATH_H
#define BFBB_PC_COMPAT_MATH_H

// The game's sources include <math.h> and get CodeWarrior's MSL, which carries
// a set of extensions that standard C does not: FABS, SQUARE, TAU, the __fabs
// family of intrinsics. src/PowerPC_EABI_Support/include/math.h is the copy the
// GameCube build sees. This is its host counterpart -- the same extensions, on
// top of the real <math.h>.
//
// It shadows the system header on the PC include path and chains to it with
// include_next, so everything standard still comes from the system.
#include_next <math.h>

#ifdef __cplusplus
extern "C" {
#endif

// The GameCube header defines these as identity macros under #ifndef __MWERKS__
// -- purely so clangd stops complaining, since only CodeWarrior ever compiles
// that path. Identity would make FABS(x) return x, so they are real here.
#define __fabs(x) fabs(x)
#define __fabsf(x) fabsf(x)

// PowerPC's reciprocal-square-root estimate. The hardware instruction is
// specified to about 12 bits of mantissa and the exact value is more accurate,
// not less -- but it is a different value, so anything the game tuned against
// the estimate's error will drift slightly. See "Floating point divergence" in
// PCPORT.md.
#define __frsqrte(x) (1.0 / sqrt(x))

#define FABS(x) (float)__fabs(x)

#define SQUARE(v) ((v) * (v))

#define TAU 6.2831855f
#define PI 3.1415927f
#define HALF_PI 1.5707964f

#define LONG_TAU 6.2831854820251465

#ifdef __cplusplus
}
#endif

#endif
