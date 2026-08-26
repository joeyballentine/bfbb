#ifndef DLRENDST_H
#define DLRENDST_H

#include "rwsdk/rwcore.h"


#ifdef __cplusplus
extern "C" {
#endif

extern void _rwDlRenderStateSetZCompLoc(RwInt32 zBeforeTex);
extern void RwGameCubeSetAlphaCompare(RwInt32 comp0, RwUInt8 ref0, RwInt32 op, RwInt32 comp1, RwUInt8 ref1);

#ifdef PLATFORM_PC
// The comparison constants these two are called with.
//
// On the console they come from <dolphin/gx/GXEnum.h>, which xModelBucket.cpp
// picks up transitively and the PC build has no reason to compile. The three
// values the game passes are spelled out here instead, with the numbering
// GXEnum.h gives them, so that xModelBucket.cpp -- which is portable code in
// Core/x, and calls both of these UNGUARDED -- compiles unmodified.
//
// An anonymous enum rather than a GXCompare/GXAlphaOp typedef: the port is not
// declaring GameCube types, it is naming the two arguments these functions
// take. Both parameters are RwInt32 anyway.
enum
{
    GX_NEVER = 0,
    GX_LESS = 1,
    GX_EQUAL = 2,
    GX_LEQUAL = 3,
    GX_GREATER = 4,
    GX_NEQUAL = 5,
    GX_GEQUAL = 6,
    GX_ALWAYS = 7
};

enum
{
    GX_AOP_AND = 0,
    GX_AOP_OR = 1,
    GX_AOP_XOR = 2,
    GX_AOP_XNOR = 3
};
#endif


#ifdef __cplusplus
}
#endif

#endif
