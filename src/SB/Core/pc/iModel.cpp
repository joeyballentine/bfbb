#include "iModel.h"

#include "iStub.h"

// iModel: not ported yet.
//
// Stubs, so that the port LINKS and can be run. Every one of these reports
// itself the first time it is called (see iStub.h), and that report is the
// worklist: the order the startup path demands these in is what decides
// which module gets written first.
//
// The real implementation is src/SB/Core/gc/iModel.cpp. Read it before
// replacing anything here -- most of these modules are RenderWare calls and
// game logic rather than GameCube hardware, so a port is closer to a copy
// than it looks.

void iModelInit()
{
    IPORT_STUB();
}

U32 iModelNumBones(RpAtomic* model)
{
    IPORT_STUB();
    return 0;
}

S32 iModelCull(RpAtomic* model, RwMatrixTag* mat)
{
    IPORT_STUB();
    return 0;
}

S32 iModelSphereCull(xSphere* sphere)
{
    IPORT_STUB();
    return 0;
}

RpAtomic* iModelFileNew(void* buffer, U32 size)
{
    IPORT_STUB();
    return NULL;
}

RpAtomic* iModelFile_RWMultiAtomic(RpAtomic* model)
{
    IPORT_STUB();
    return NULL;
}

void iModelSetMaterialTexture(RpAtomic* model, void* texture)
{
    IPORT_STUB();
}

void iModelResetMaterial(RpAtomic* model)
{
    IPORT_STUB();
}

void iModelUnload(RpAtomic* userdata)
{
    IPORT_STUB();
}

S32 iModelCullPlusShadow(RpAtomic* model, RwMatrix* mat, xVec3* shadowVec, S32* shadowOutside)
{
    IPORT_STUB();
    return 0;
}

void iModelTagEval(RpAtomic* model, const xModelTag* tag, RwMatrixTag* mat, xVec3* dest)
{
    IPORT_STUB();
}

U32 iModelTagSetup(xModelTag* tag, RpAtomic* model, F32 x, F32 y, F32 z)
{
    IPORT_STUB();
    return 0;
}

void iModelSetMaterialAlpha(RpAtomic* model, U8 alpha)
{
    IPORT_STUB();
}

U32 iModelVertCount(RpAtomic* model)
{
    IPORT_STUB();
    return 0;
}

void iModelMaterialMul(RpAtomic* model, F32 rm, F32 gm, F32 bm)
{
    IPORT_STUB();
}

void iModelRender(RpAtomic* model, RwMatrix* mat)
{
    IPORT_STUB();
}

void iModelAnimMatrices(RpAtomic* model, xQuat* quat, xVec3* tran, RwMatrixTag* mat)
{
    IPORT_STUB();
}

// The four the generator missed, added by hand.
//
// Two of them are multi-line declarations in iModel.h, which its line-at-a-time
// regex cannot see. The other two are overloads taking xModelTagWithNormal, and
// those are not declared in iModel.h at ALL -- zNPCTypeBossSB2.cpp:83-85
// declares them locally, the way several units declare the functions they reach
// for. Anything regenerating this file has to add them back.

U32 iModelVertEval(RpAtomic* model, U32 index, U32 count, RwMatrixTag* mat, xVec3* vert,
                   xVec3* dest)
{
    IPORT_STUB();
    return 0;
}

U32 iModelNormalEval(xVec3* out, const RpAtomic& m, const RwMatrixTag* mat, size_t index, S32 size,
                     const xVec3* in)
{
    IPORT_STUB();
    return 0;
}

U32 iModelTagSetup(xModelTagWithNormal* tag, RpAtomic* model, F32 x, F32 y, F32 z)
{
    IPORT_STUB();
    return 0;
}

void iModelTagEval(RpAtomic* model, const xModelTagWithNormal* tag, RwMatrixTag* mat, xVec3* dest,
                   xVec3* normalDest)
{
    IPORT_STUB();
}
