#include "iMorph.h"

#include "iStub.h"

// iMorph: not ported yet.
//
// Stubs, so that the port LINKS and can be run. Every one of these reports
// itself the first time it is called (see iStub.h), and that report is the
// worklist: the order the startup path demands these in is what decides
// which module gets written first.
//
// The real implementation is src/SB/Core/gc/iMorph.cpp. Read it before
// replacing anything here -- most of these modules are RenderWare calls and
// game logic rather than GameCube hardware, so a port is closer to a copy
// than it looks.

void iMorphOptimize(RpAtomic* model, S32 normals)
{
    IPORT_STUB();
}

void FastS16weight2(F32* dest, S16** v_array, S16* weight, S32 count, F32 scale)
{
    IPORT_STUB();
}

// Multi-line in iMorph.h, same reason as the others.
void iMorphRender(RpAtomic* model, RwMatrix* mat, S16** v_array, S16* weight, U32 normals,
                  F32 scale)
{
    IPORT_STUB();
}
