#ifndef RPHANIM_H
#define RPHANIM_H

#include <rwsdk/rwcore.h>
#include <rwsdk/rtanim.h>

/* C compatibility: these headers use bare tag names as types. */
typedef struct RpHAnimNodeInfo RpHAnimNodeInfo;
typedef struct RpHAnimHierarchy RpHAnimHierarchy;


struct RpHAnimNodeInfo
{
    RwInt32 nodeID;
    RwInt32 nodeIndex;
    RwInt32 flags;
    RwFrame* pFrame;
};

#ifndef PLATFORM_PC
struct RpHAnimHierarchy
{
    RwInt32 flags;
    RwInt32 numNodes;
    RwMatrix* pMatrixArray;
    void* pMatrixArrayUnaligned;
    RpHAnimNodeInfo* pNodeInfo;
    RwFrame* parentFrame;
    RpHAnimHierarchy* parentHierarchy;
    RwInt32 rootParentOffset;
    RtAnimInterpolator* currentAnim;
};
#else
// Mirrored onto rw::HAnimHierarchy, the usual bargain: librw's field ORDER
// under RenderWare's field NAMES, asserted in rw/layout_hanim.cpp.
//
// **rootParentOffset is dropped**, because librw has no counterpart for it and
// nothing in the game reads it. The only two fields game code touches are
// pMatrixArray (three sites) and flags (one), and both sit ahead of where the
// two layouts diverge, so nothing has to move to accommodate this -- the field
// simply is not there, and reaching for it is a compile error rather than a
// silent read of currentAnim's low half.
//
// The same reasoning as RpAtomic's four dropped fields; see rpworld.h.
struct RpHAnimHierarchy
{
    RwInt32 flags;
    RwInt32 numNodes;
    RwMatrix* pMatrixArray;
    void* pMatrixArrayUnaligned;
    RpHAnimNodeInfo* pNodeInfo;
    RwFrame* parentFrame;
    RpHAnimHierarchy* parentHierarchy;
    RtAnimInterpolator* currentAnim; // librw calls this 'interpolator'
};
#endif

enum RpHAnimHierarchyFlag
{
    rpHANIMHIERARCHYSUBHIERARCHY = 0x01,
    rpHANIMHIERARCHYNOMATRICES = 0x02,
    rpHANIMHIERARCHYUPDATEMODELLINGMATRICES = 0x1000,
    rpHANIMHIERARCHYUPDATELTMS = 0x2000,
    rpHANIMHIERARCHYLOCALSPACEMATRICES = 0x4000,
    rpHANIMHIERARCHYFLAGFORCEENUMSIZEINT = RWFORCEENUMSIZEINT
};
typedef enum RpHAnimHierarchyFlag RpHAnimHierarchyFlag;

#ifdef __cplusplus
extern "C" {
#endif

extern void RpHAnimKeyFrameApply(void* matrix, void* voidIFrame);
extern void RpHAnimKeyFrameInterpolate(void* voidOut, void* voidIn1, void* voidIn2, RwReal time,
                                       void* customData);
extern void RpHAnimKeyFrameBlend(void* voidOut, void* voidIn1, void* voidIn2, RwReal alpha);
extern RtAnimAnimation* RpHAnimKeyFrameStreamRead(RwStream* stream, RtAnimAnimation* animation);
extern RwBool RpHAnimKeyFrameStreamWrite(const RtAnimAnimation* animation, RwStream* stream);
extern RwInt32 RpHAnimKeyFrameStreamGetSize(const RtAnimAnimation* animation);
extern void RpHAnimKeyFrameMulRecip(void* voidFrame, void* voidStart);
extern void RpHAnimKeyFrameAdd(void* voidOut, void* voidIn1, void* voidIn2);
extern RwBool RpHAnimPluginAttach(void);
extern RpHAnimHierarchy* RpHAnimHierarchyCreate(RwInt32 numNodes, RwUInt32* nodeFlags,
                                                RwInt32* nodeIDs, RpHAnimHierarchyFlag flags,
                                                RwInt32 maxInterpKeyFrameSize);
extern RpHAnimHierarchy* RpHAnimHierarchyDestroy(RpHAnimHierarchy* hierarchy);
extern RwBool RpHAnimFrameSetHierarchy(RwFrame* frame, RpHAnimHierarchy* hierarchy);
extern RpHAnimHierarchy* RpHAnimFrameGetHierarchy(RwFrame* frame);

#ifdef __cplusplus
}
#endif

#endif