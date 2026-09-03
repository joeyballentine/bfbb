#ifndef BFBB_TYPES_H
#define BFBB_TYPES_H

#include "macros.h"

// Note: only include this header inside BFBB-related headers/source code files.
// Don't include this in any RenderWare, system, bink, etc. files

#ifdef GAMECUBE
typedef signed char S8;
typedef signed short S16;
typedef signed int S32;
typedef signed long long S64;

typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned int U32;
typedef unsigned long long U64;

typedef float F32;
typedef double F64;
#elif !defined(__MWERKS__)
// Host builds (see docs/PCPORT.md). The GameCube widths above are what the game
// code assumes everywhere; on a modern host only <stdint.h> guarantees them,
// because `long` is 64-bit on LP64 and `int` is not guaranteed 32.
#include <stdint.h>

typedef int8_t S8;
typedef int16_t S16;
typedef int32_t S32;
typedef int64_t S64;

typedef uint8_t U8;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;

typedef float F32;
typedef double F64;
#endif

#ifdef NULL
#undef NULL
#endif
#define NULL 0

#ifdef TRUE
#undef TRUE
#endif
#define TRUE 1

#ifdef FALSE
#undef FALSE
#endif
#define FALSE 0

// Stub __declspec out for compilers that cannot parse it. CodeWarrior is the
// one compiler where it is meaningful -- it carries `weak` and the `section`
// placements for .init/.ctors/.dtors -- so it must NOT be stubbed there.
//
// Nor on the PC build, where it is not merely useless but wrong. Windows uses
// __declspec for things a host header needs to mean what it says: the CRT's
// <wchar.h> defines `__declspec(selectany) int _Avx2WmemEnabledWeakValue = 0;`
// at file scope, and with the stub in place that is an ordinary definition in
// every translation unit that reaches the header -- which the linker then
// rejects as a duplicate against any library built without our headers. SDL is
// how this was found. There is nothing to stub anyway: the four __declspec
// sites left in src/SB are all inside `#ifdef __MWERKS__` or `#ifdef GAMECUBE`,
// and DECL_SECTION/DECL_WEAK in macros.h already have host spellings.
//
// The stub stays for a compiler with neither define -- clangd parsing src/gc,
// src/dolphin and src/bink, which do write __declspec(section) unguarded.
#if !defined(__MWERKS__) && !defined(PLATFORM_PC)
#define __declspec(x)
// #define asm
#endif

// WEAK has to survive that stub, and on the PC build it did not.
//
// `#define __declspec(x)` above expands __declspec(weak) to NOTHING, so every
// WEAK definition in the game -- iFileAsyncService, xVec3LengthFast, xSCurve,
// xVec3DistFast, xMat3x3MulRotC, xVec3ScaleC, xModelAnimCollStop -- became a
// strong definition on any compiler that is not CodeWarrior. They are WEAK
// because CodeWarrior emitted one copy of each into whichever object it
// happened to pick, and the decomp reproduces that placement; a host linker
// sees two strong definitions of the same function and refuses.
//
// Found by linking. It cannot be found by compiling: each translation unit is
// individually correct, and tools/pcprogress.py compiles them one at a time.
// iFileAsyncService was the one that fired, against the port's own iFile.cpp.
//
// GCC and Clang spell it __attribute__((weak)), which they support on COFF as
// a weak external -- the same semantics CodeWarrior gives __declspec(weak),
// and what the definitions are asking for.
//
// MinGW is the exception and needs a different spelling. GCC has no weak
// externals on PE/COFF, so it emulates them: the body is emitted under a
// mangled alias and the real name is left UNDEFINED with a fallback --
//
//     T .weak.__Z11xMat3x3SMulP7xMat3x3PKS_f.__ZN5xVec36createEfff
//     w __Z11xMat3x3SMulP7xMat3x3PKS_f
//
// -- so every caller in another object is an undefined reference at link, and
// the fallback is whatever unrelated symbol the compiler picked. `inline` is
// the spelling that does work there: a COMDAT, which is what weak_odr is on
// this file format and what several objects defining one function need.
// `used` because the body has to be emitted even when the defining unit never
// calls it, which is the whole reason WEAK is here.
#if defined(__MWERKS__)
#define WEAK __declspec(weak)
#elif defined(__MINGW32__)
#define WEAK inline __attribute__((used))
#elif defined(__GNUC__) || defined(__clang__)
#define WEAK __attribute__((weak))
#else
#define WEAK
#endif

// For a function DECLARED in a header and DEFINED `inline` in exactly one .cpp,
// which other units then call. CodeWarrior emits an out-of-line copy from the
// defining unit and everything else links against it -- retail's placement, and
// several Matching units are built against it, so the console must keep the
// bare `inline`.
//
// Clang does the same thing at -O0 and NOT at -O2. At -O2 it inlines the
// defining unit's own call, emits no body at all, and every other unit's call
// is undefined at link. That is why the port linked in Debug and not in
// Release, in sixteen places at once: SMOOTH, LERP, xpow, xfmod, xBoxFromSphere,
// xSndPlay3D, xVec2::length, xfont::render, xtextbox::yextent, xParEmitterEmit,
// NPCHazard::SetNPCOwner/NotifyCBSet, range_limit<F32>.
//
// WEAK forces the body out; `inline` stays so the linkage is weak_odr rather
// than a plain weak external. Both are emitted, but only weak_odr is allowed to
// appear in several objects at once, and two of these definitions live in
// headers (SMOOTH in xMathInlines.h, xSndPlay3D in zEnt.h) where every
// including unit emits one. Dropping `inline` there is eight duplicate-symbol
// errors, which is how this was found.
//
// Templates cannot use this -- a unit that cannot see the definition cannot
// instantiate it either -- and take an explicit instantiation next to the
// definition instead.
#if defined(PLATFORM_PC) && defined(__MINGW32__)
// WEAK already carries the `inline`, for the reason above, and repeating it
// here is "duplicate 'inline'".
#define SHARED_INLINE WEAK
#elif defined(PLATFORM_PC)
#define SHARED_INLINE WEAK inline
#else
#define SHARED_INLINE inline
#endif

#if defined(GAMECUBE) || defined(__MWERKS__)
typedef signed char s8;
typedef signed short s16;
typedef signed long s32;
typedef signed long long s64;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef unsigned long size_t;
typedef unsigned long long u64;
#else
// `long` is 32-bit on the GameCube ABI and 64-bit on LP64 hosts, so the
// spellings above are only correct there. size_t is the host's, not ours --
// redefining it would fight every libc header the port includes.
#include <stddef.h>

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#endif

typedef unsigned short ushort;
typedef unsigned int uint;

typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile u64 vu64;
typedef volatile s8 vs8;
typedef volatile s16 vs16;
typedef volatile s32 vs32;
typedef volatile s64 vs64;

typedef float f32;
typedef double f64;
typedef volatile f32 vf32;
typedef volatile f64 vf64;

typedef int BOOL;

typedef int unknown;

#ifndef __cplusplus
typedef unsigned short wchar_t;
typedef wchar_t wint_t;
#endif

// Basic defines to allow newer-like C++ code to be written
#define TRUE 1
#define FALSE 0

#define null 0

#ifndef NULL
#define NULL 0
#endif

#ifdef __MWERKS__
#define UINT32_MAX 0xffffffff
#else
// <stdint.h> already defines this on a host build, and spells it differently,
// so redefining it unconditionally is a hard error there.
#ifndef UINT32_MAX
#define UINT32_MAX 0xffffffff
#endif
#endif

#endif // !TYPES_H
