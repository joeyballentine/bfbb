#include "iLight.h"

#include "iStub.h"

// iLight: not ported yet.
//
// Stubs, so that the port LINKS and can be run. Every one of these reports
// itself the first time it is called (see iStub.h), and that report is the
// worklist: the order the startup path demands these in is what decides
// which module gets written first.
//
// The real implementation is src/SB/Core/gc/iLight.cpp. Read it before
// replacing anything here -- most of these modules are RenderWare calls and
// game logic rather than GameCube hardware, so a port is closer to a copy
// than it looks.

RpWorld* gLightWorld;

void iLightInit(RpWorld* world)
{
    IPORT_STUB();
}

iLight* iLightCreate(iLight* light, U32 type)
{
    IPORT_STUB();
    return NULL;
}

void iLightModify(iLight* light, U32 flags)
{
    IPORT_STUB();
}

void iLightSetColor(iLight* light, _xFColor* col)
{
    IPORT_STUB();
}

void iLightSetPos(iLight* light, xVec3* pos)
{
    IPORT_STUB();
}

void iLightDestroy(iLight* light)
{
    IPORT_STUB();
}

void iLightEnv(iLight* light, S32 env)
{
    IPORT_STUB();
}
