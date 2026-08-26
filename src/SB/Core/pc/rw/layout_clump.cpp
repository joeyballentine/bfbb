// Layout assertions for RpClump.
//
// Same job as layout.cpp -- no code, just the claims clump.cpp's
// reinterpret_casts rest on, checked by the compiler. Read layout.cpp's header
// comment for why any of this exists, and layout_geometry.cpp's for why
// PLUGINBASE contributes nothing to a librw layout and why appending a field
// to one of these structs would land in the plugin block rather than past the
// end of the allocation.
//
// The offsets below came from a throwaway program that printed offsetof for
// each member of rw::Clump, not from reading rwobjects.h.
//
// RpClump is its own file rather than a few more lines in layout_geometry.cpp
// because the clump group was written separately from the model path; the
// building blocks it needs (RwObject, RwLinkList, RwLLLink) are already
// asserted in layout.cpp and are not repeated here.

#include <rwcore.h>
#include <rpworld.h>

#include "rw.h"

#include <stddef.h>

#define SAME_SIZE(ours, theirs)                                                                    \
    static_assert(sizeof(ours) == sizeof(theirs), #ours " and " #theirs " differ in size")

#define SAME_OFFSET(ours, ourfield, theirs, theirfield)                                            \
    static_assert(offsetof(ours, ourfield) == offsetof(theirs, theirfield),                        \
                  #ours "." #ourfield " is not where " #theirs "." #theirfield " is")

// --- RpClump ---------------------------------------------------------------
//
// The reorder here is that librw keeps a World* between the camera list and
// the in-world link, where RenderWare keeps nothing and puts an
// RpClumpCallBack after the link instead. Ours takes librw's shape; see the
// comment on the struct in rpworld.h for why the callback is dropped rather
// than moved.
//
// SAME_SIZE is what makes that a claim and not a hope: if librw ever grows a
// field, the clump the shim hands back would be smaller than the one librw
// allocated and this line fails the build.

SAME_SIZE(RpClump, rw::Clump);
SAME_OFFSET(RpClump, object, rw::Clump, object);
SAME_OFFSET(RpClump, atomicList, rw::Clump, atomics);
SAME_OFFSET(RpClump, lightList, rw::Clump, lights);
SAME_OFFSET(RpClump, cameraList, rw::Clump, cameras);
SAME_OFFSET(RpClump, world, rw::Clump, world);
SAME_OFFSET(RpClump, inWorldLink, rw::Clump, inWorld);

// RpClumpGetFrame is a macro for rwObjectGetParent, and RpClumpAddAtomic walks
// from an RwLLLink back to the RpAtomic that contains it with the same
// arithmetic librw uses -- both of which need the atomic's link to be exactly
// where librw's is. That offset is asserted in layout_geometry.cpp; this is
// the note saying clump.cpp depends on it too.
static_assert(offsetof(RpAtomic, inClumpLink) == offsetof(rw::Atomic, inClump),
              "clump.cpp's list walk depends on RpAtomic.inClumpLink matching rw::Atomic.inClump");
