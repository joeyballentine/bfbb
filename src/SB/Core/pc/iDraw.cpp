#include "iDraw.h"

#include "iStub.h"

// iDraw: not ported yet.
//
// Stubs, so that the port LINKS and can be run. Every one of these reports
// itself the first time it is called (see iStub.h), and that report is the
// worklist: the order the startup path demands these in is what decides
// which module gets written first.
//
// The real implementation is src/SB/Core/gc/iDraw.cpp. Read it before
// replacing anything here -- most of these modules are RenderWare calls and
// game logic rather than GameCube hardware, so a port is closer to a copy
// than it looks.

void iDrawSetFBMSK(U32 abgr)
{
    IPORT_STUB();
}

void iDrawBegin()
{
    IPORT_STUB();
}

void iDrawEnd()
{
    IPORT_STUB();
}
