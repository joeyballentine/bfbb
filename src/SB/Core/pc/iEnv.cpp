#include "iEnv.h"

#include "iStub.h"

// iEnv: not ported yet.
//
// Stubs, so that the port LINKS and can be run. Every one of these reports
// itself the first time it is called (see iStub.h), and that report is the
// worklist: the order the startup path demands these in is what decides
// which module gets written first.
//
// The real implementation is src/SB/Core/gc/iEnv.cpp. Read it before
// replacing anything here -- most of these modules are RenderWare calls and
// game logic rather than GameCube hardware, so a port is closer to a copy
// than it looks.

void iEnvLoad(iEnv* env, const void* data, U32 datasize, S32 dataType)
{
    IPORT_STUB();
}

void iEnvFree(iEnv* env)
{
    IPORT_STUB();
}

void iEnvDefaultLighting(iEnv*)
{
    IPORT_STUB();
}

void iEnvLightingBasics(iEnv*, xEnvAsset*)
{
    IPORT_STUB();
}

void iEnvRender(iEnv* env)
{
    IPORT_STUB();
}

void iEnvEndRenderFX(iEnv*)
{
    IPORT_STUB();
}
