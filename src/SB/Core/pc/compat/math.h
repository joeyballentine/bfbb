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

// __fabs and friends come from compat/intrin.h as real inline functions.
// Defining them as macros here instead was a mistake: include/intrin.h declares
// `double __frsqrte(double);`, and a function-like macro of the same name
// expands inside that declaration.
//
// Note that src/PowerPC_EABI_Support/include/math.h defines them as IDENTITY
// macros for non-CodeWarrior compilers, purely so clangd stops complaining --
// which would make FABS(x) return x. They are real here, and the selftest
// checks it.
#include <intrin.h>

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
