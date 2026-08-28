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
#ifndef __MWERKS__
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
#if defined(__MWERKS__)
#define WEAK __declspec(weak)
#elif defined(__GNUC__) || defined(__clang__)
#define WEAK __attribute__((weak))
#else
#define WEAK
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
