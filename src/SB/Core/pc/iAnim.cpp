#include "iAnim.h"

#include "iStub.h"

// iAnim: not ported yet.
//
// Stubs, so that the port LINKS and can be run. Every one of these reports
// itself the first time it is called (see iStub.h), and that report is the
// worklist: the order the startup path demands these in is what decides
// which module gets written first.
//
// The real implementation is src/SB/Core/gc/iAnim.cpp. Read it before
// replacing anything here -- most of these modules are RenderWare calls and
// game logic rather than GameCube hardware, so a port is closer to a copy
// than it looks.

U8* giAnimScratch;

void iAnimInit()
{
    IPORT_STUB();
}

F32 iAnimDuration(void* RawData)
{
    IPORT_STUB();
    return 0.0f;
}

U32 iAnimBoneCount(void* RawData)
{
    IPORT_STUB();
    return 0;
}

void iAnimEval(void* RawData, float time, unsigned int flags, class xVec3* tran, class xQuat* quat)
{
    IPORT_STUB();
}

// A multi-line declaration in iAnim.h, which the generator's line-at-a-time
// regex cannot see.
void iAnimBlend(F32 BlendFactor, F32 BlendRecip, U16* BlendTimeOffset, F32* BoneTable,
                U32 BoneCount, xVec3* Tran1, xQuat* Quat1, xVec3* Tran2, xQuat* Quat2,
                xVec3* TranDest, xQuat* QuatDest)
{
    IPORT_STUB();
}
