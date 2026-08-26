// RpHAnimHierarchy and RpHAnimNodeInfo against librw's, checked by the compiler.
//
// The rule from README.md: a type mirrored in include/rwsdk gets its size and
// every field offset asserted, in the same commit that mirrors it. These two
// matter more than most -- xAnim writes bone matrices through pMatrixArray
// every frame, and a wrong offset there animates a model from the wrong memory
// rather than failing.

#include <rwcore.h>
#include <rphanim.h>

#include "rw.h"

#include <stddef.h>

#define SAME_OFFSET(ours, ourfield, theirs, theirfield)                                            \
    static_assert(offsetof(ours, ourfield) == offsetof(theirs, theirfield),                        \
                  #ours "." #ourfield " is not where " #theirs "." #theirfield " is")

// --- RpHAnimNodeInfo -------------------------------------------------------
//
// Identical without reordering: RenderWare's {nodeID, nodeIndex, flags, pFrame}
// is librw's {id, index, flags, frame} field for field. Asserted anyway,
// because iModel.cpp indexes an array of these by node and a stride change
// would walk the skeleton wrong.

static_assert(sizeof(RpHAnimNodeInfo) == sizeof(rw::HAnimNodeInfo),
              "RpHAnimNodeInfo and rw::HAnimNodeInfo differ in size");
SAME_OFFSET(RpHAnimNodeInfo, nodeID, rw::HAnimNodeInfo, id);
SAME_OFFSET(RpHAnimNodeInfo, nodeIndex, rw::HAnimNodeInfo, index);
SAME_OFFSET(RpHAnimNodeInfo, flags, rw::HAnimNodeInfo, flags);
SAME_OFFSET(RpHAnimNodeInfo, pFrame, rw::HAnimNodeInfo, frame);

// --- RpHAnimHierarchy ------------------------------------------------------
//
// The port drops rootParentOffset, which sat between parentHierarchy and
// currentAnim in RenderWare's and has no librw counterpart. Everything the game
// reads -- pMatrixArray at three sites, flags at one -- is ahead of that, so
// nothing had to move; the tail is one field shorter and reaching for the
// missing one is a compile error.

static_assert(sizeof(RpHAnimHierarchy) == sizeof(rw::HAnimHierarchy),
              "RpHAnimHierarchy and rw::HAnimHierarchy differ in size");
SAME_OFFSET(RpHAnimHierarchy, flags, rw::HAnimHierarchy, flags);
SAME_OFFSET(RpHAnimHierarchy, numNodes, rw::HAnimHierarchy, numNodes);
SAME_OFFSET(RpHAnimHierarchy, pMatrixArray, rw::HAnimHierarchy, matrices);
SAME_OFFSET(RpHAnimHierarchy, pMatrixArrayUnaligned, rw::HAnimHierarchy, matricesUnaligned);
SAME_OFFSET(RpHAnimHierarchy, pNodeInfo, rw::HAnimHierarchy, nodeInfo);
SAME_OFFSET(RpHAnimHierarchy, parentFrame, rw::HAnimHierarchy, parentFrame);
SAME_OFFSET(RpHAnimHierarchy, parentHierarchy, rw::HAnimHierarchy, parentHierarchy);
SAME_OFFSET(RpHAnimHierarchy, currentAnim, rw::HAnimHierarchy, interpolator);

// The hierarchy flags iModel and xAnim pass straight through.
static_assert((int)rpHANIMHIERARCHYSUBHIERARCHY == (int)rw::HAnimHierarchy::SUBHIERARCHY,
              "SUBHIERARCHY renumbered");
static_assert((int)rpHANIMHIERARCHYNOMATRICES == (int)rw::HAnimHierarchy::NOMATRICES,
              "NOMATRICES renumbered");
static_assert((int)rpHANIMHIERARCHYUPDATEMODELLINGMATRICES ==
                  (int)rw::HAnimHierarchy::UPDATEMODELLINGMATRICES,
              "UPDATEMODELLINGMATRICES renumbered");
static_assert((int)rpHANIMHIERARCHYUPDATELTMS == (int)rw::HAnimHierarchy::UPDATELTMS,
              "UPDATELTMS renumbered");
