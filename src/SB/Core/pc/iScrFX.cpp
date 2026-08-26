#include "iScrFX.h"

#include "iStub.h"

// iScrFX: not ported yet.
//
// Stubs, so that the port LINKS and can be run. Every one of these reports
// itself the first time it is called (see iStub.h), and that report is the
// worklist: the order the startup path demands these in is what decides
// which module gets written first.
//
// The real implementation is src/SB/Core/gc/iScrFX.cpp. Read it before
// replacing anything here -- most of these modules are RenderWare calls and
// game logic rather than GameCube hardware, so a port is closer to a copy
// than it looks.

void iScrFxInit()
{
    IPORT_STUB();
}

void iScrFxBegin()
{
    IPORT_STUB();
}

void iScrFxEnd()
{
    IPORT_STUB();
}

void iScrFxDrawBox(F32 x1, F32 y1, F32 x2, F32 y2, U8 red, U8 green, U8 blue, U8 alpha)
{
    IPORT_STUB();
}

void iCameraMotionBlurActivate(U32 activate)
{
    IPORT_STUB();
}

void iCameraSetBlurriness(F32 amount)
{
    IPORT_STUB();
}

void iScrFxCameraCreated(RwCamera* pCamera)
{
    IPORT_STUB();
}

void iScrFxCameraEndScene(RwCamera* pCamera)
{
    IPORT_STUB();
}

void iScrFxPostCameraEnd(RwCamera* pCamera)
{
    IPORT_STUB();
}

S32 iScrFxCameraDestroyed(RwCamera* pCamera)
{
    IPORT_STUB();
    return 0;
}
