#ifndef BFBB_PC_COMPAT_INTRIN_H
#define BFBB_PC_COMPAT_INTRIN_H

// CodeWarrior's PowerPC intrinsics. include/intrin.h declares them so that
// clangd can parse the sources; the compiler itself lowers each to a single
// instruction and never calls a function. A host has neither the builtin nor a
// body, so this shadows that header and defines the ones the game uses.
//
// Only two are used anywhere in src/SB: __fabs (5 files) and __frsqrte (3).
// The rest are declared, exactly as the GameCube header declares them, so that
// anything reaching for one fails to link rather than silently doing nothing.

#ifdef __cplusplus
extern "C" {
#endif

void __eieio(void);
void __sync(void);
void __isync(void);
int __abs(int);
double __fnabs(double);
long __labs(long);
void __sthbrx(unsigned short, void*, int);
void __stwbrx(unsigned int, void*, int);
double __setflm(double);
int __rlwinm(int, int, int, int);
int __rlwnm(int, int, int, int);
int __rlwimi(int, int, int, int, int);
void __dcbf(void*, int);
void __dcbt(void*, int);
void __dcbst(void*, int);
void __dcbtst(void*, int);
void __dcbz(void*, int);
int __mulhw(int, int);
unsigned int __mulhwu(unsigned int, unsigned int);
double __fmadd(double, double, double);
double __fmsub(double, double, double);
double __fnmadd(double, double, double);
double __fnmsub(double, double, double);
float __fmadds(float, float, float);
float __fmsubs(float, float, float);
float __fnmadds(float, float, float);
float __fnmsubs(float, float, float);
double __mffs(void);
float __fnabsf(float);

// Declared, not defined. glibc's <math.h> declares both of these too -- they
// are its internal aliases for fabs -- so a definition here would be a static
// redeclaration of an extern. It does not define them, though, so the bodies
// are in src/SB/Core/pc/intrin.cpp.
double __fabs(double);
float __fabsf(float);

#include <emmintrin.h>

// PowerPC's reciprocal-square-root estimate is specified to roughly 12 bits of
// mantissa. The exact value is more accurate, not less -- but it is a different
// value, so anything tuned against the estimate's error drifts slightly. See
// "Floating point divergence" in PCPORT.md.
//
// The square root is taken with an SSE intrinsic rather than __builtin_sqrt or
// a call to sqrt, and that is deliberate. At -O0 clang lowers __builtin_sqrt to
// a CALL to sqrt, so anything that overrides sqrt captures this function too --
// and src/SB/Core/x/xSpline.cpp used to define a global sqrt implemented in
// terms of __frsqrte. sqrt called __frsqrte called sqrt, and the boot recursed
// until the stack died. sqrtsd cannot route back through a symbol the game
// might define.
static inline double __frsqrte(double x)
{
    return 1.0 / _mm_cvtsd_f64(_mm_sqrt_sd(_mm_setzero_pd(), _mm_set_sd(x)));
}

#ifdef __cplusplus
}
#endif

#endif
