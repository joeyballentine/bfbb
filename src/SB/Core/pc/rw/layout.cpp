// Every claim that a RenderWare object and its librw counterpart are the same
// bytes, checked by the compiler.
//
// This file has no code in it on purpose. Its whole job is to fail the build
// the moment one of those layouts stops being true -- because the failure it
// prevents is silent: game code would keep compiling and start reading the
// wrong field. See README.md for why the port mirrors layouts rather than
// converting at the seam.
//
// When you mirror a new type in include/rwsdk, add its offsets here in the same
// commit. A mirrored type with no assertions is worse than an unmirrored one.

#include <rwcore.h>

#include "rw.h"

#include <stddef.h>

#define SAME_SIZE(ours, theirs)                                                                    \
    static_assert(sizeof(ours) == sizeof(theirs), #ours " and " #theirs " differ in size")

#define SAME_OFFSET(ours, ourfield, theirs, theirfield)                                            \
    static_assert(offsetof(ours, ourfield) == offsetof(theirs, theirfield),                        \
                  #ours "." #ourfield " is not where " #theirs "." #theirfield " is")

// --- the building blocks ---------------------------------------------------

SAME_SIZE(RwObject, rw::Object);
SAME_OFFSET(RwObject, type, rw::Object, type);
SAME_OFFSET(RwObject, subType, rw::Object, subType);
SAME_OFFSET(RwObject, flags, rw::Object, flags);
SAME_OFFSET(RwObject, privateFlags, rw::Object, privateFlags);
SAME_OFFSET(RwObject, parent, rw::Object, parent);

SAME_SIZE(RwLLLink, rw::LLLink);
SAME_OFFSET(RwLLLink, next, rw::LLLink, next);
SAME_OFFSET(RwLLLink, prev, rw::LLLink, prev);

SAME_SIZE(RwLinkList, rw::LinkList);
SAME_OFFSET(RwLinkList, link, rw::LinkList, link);

// --- RwFrame ---------------------------------------------------------------
//
// The port reorders this one: librw puts objectList before the matrices and
// RenderWare puts it after. Ours keeps RenderWare's NAMES at librw's OFFSETS,
// which is what lets game code compile unmodified. `modelling` is librw's
// `matrix`.

SAME_SIZE(RwFrame, rw::Frame);
SAME_OFFSET(RwFrame, object, rw::Frame, object);
SAME_OFFSET(RwFrame, inDirtyListLink, rw::Frame, inDirtyList);
SAME_OFFSET(RwFrame, objectList, rw::Frame, objectList);
SAME_OFFSET(RwFrame, modelling, rw::Frame, matrix);
SAME_OFFSET(RwFrame, ltm, rw::Frame, ltm);
SAME_OFFSET(RwFrame, child, rw::Frame, child);
SAME_OFFSET(RwFrame, next, rw::Frame, next);
SAME_OFFSET(RwFrame, root, rw::Frame, root);
