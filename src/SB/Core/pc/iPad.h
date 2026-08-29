#ifndef IPAD_H
#define IPAD_H

#include <types.h>

struct _tagiPad
{
    S32 port;
};

struct _tagxPad;
struct _tagxRumble;

S32 iPadInit();
_tagxPad* iPadEnable(_tagxPad* pad, S16 port);
S32 iPadConvStick(float value);

// PC-only. The GameCube gets this shape from PADClamp, which a host build has
// no equivalent of. See iPad.cpp.
void iPadShapeStick(F32 inX, F32 inY, F32 max, F32 xy, F32* outX, F32* outY);
S32 iPadUpdate(_tagxPad* pad, U32* on);
S32 iPadConvFromGCN(U32 a, U32 b, U32 c);
void iPadRumbleFx(_tagxPad* p, _tagxRumble* r, F32 time_passed);
void iPadStopRumble(_tagxPad* pad);
void iPadStopRumble();
void iPadStartRumble(_tagxPad* pad, _tagxRumble* rumble);
void iPadKill();

#endif
