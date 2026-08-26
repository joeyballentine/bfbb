#include "iParMgr.h"

#include "iStub.h"

// iParMgr: not ported yet.
//
// Stubs, so that the port LINKS and can be run. Every one of these reports
// itself the first time it is called (see iStub.h), and that report is the
// worklist: the order the startup path demands these in is what decides
// which module gets written first.
//
// The real implementation is src/SB/Core/gc/iParMgr.cpp. Read it before
// replacing anything here -- most of these modules are RenderWare calls and
// game logic rather than GameCube hardware, so a port is closer to a copy
// than it looks.

tagiRenderArrays gRenderArr;
tagiRenderInput gRenderBuffer;

void iParMgrInit()
{
    IPORT_STUB();
}

void iParMgrUpdate(F32 elapsedTime)
{
    IPORT_STUB();
}

void iParMgrRender()
{
    IPORT_STUB();
}

void iParMgrRenderParSys_Streak(void* data, xParGroup* ps)
{
    IPORT_STUB();
}

void iParMgrRenderParSys_QuadStreak(void* data, xParGroup* ps)
{
    IPORT_STUB();
}

void iParMgrRenderParSys_InvStreak(void* data, xParGroup* ps)
{
    IPORT_STUB();
}

void iParMgrRenderParSys_Flat(void* data, xParGroup* ps)
{
    IPORT_STUB();
}

void iParMgrRenderParSys_Static(void* data, xParGroup* ps)
{
    IPORT_STUB();
}

void iParMgrRenderParSys_Ground(void* data, xParGroup* ps)
{
    IPORT_STUB();
}

void iParMgrRenderParSys_Sprite(void* data, xParGroup* ps)
{
    IPORT_STUB();
}
