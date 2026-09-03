#ifndef XMORPH_H
#define XMORPH_H

#include <types.h>

#include "iMorph.h"
#include "xMath3.h"

#include <rwcore.h>
#include <rpworld.h>

struct xMorphSeqFile
{
    U32 Magic;
    U32 Flags;
    U32 TimeCount;
    U32 ModelCount;
};

struct xMorphFrame
{
    RpAtomic* Model;
    F32 RecipTime;
    F32 Scale;
    U16 Flags;
    U16 NumVerts;
    S16* Targets[4];
    S16 WeightStart[4];
    S16 WeightEnd[4];
};

struct xMorphTargetFile
{
    U32 Magic;
    U16 NumTargets;
    U16 NumVerts;
    U32 Flags;
    F32 Scale;
    xVec3 Center;
    F32 Radius;
};

// The asset ids behind the frames, replaced by the pointers they resolve to
// while the sequence is set up. A word on disc, so a word wide enough to hold
// the pointer that goes back into it. Anim_Read in zAssetTypes.cpp widens the
// slots along with the frames.
#ifdef BFBB_PTR64
typedef void* xMorphAssetSlot;
#else
typedef U32 xMorphAssetSlot;
#endif

typedef void*(*xMorphFindAssetCallback)(U32, char*);

xMorphSeqFile* xMorphSeqSetup(void* data, xMorphFindAssetCallback FindAssetCB);
void xMorphRender(xMorphSeqFile* seq, RwMatrix* mat, F32 time);
F32 xMorphSeqDuration(xMorphSeqFile* seq);

#endif