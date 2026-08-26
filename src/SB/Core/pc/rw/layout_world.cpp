// Layout assertions for RpWorld.
//
// Same job as layout.cpp -- see the comment at the top of that file -- split
// out because this group is worked on separately. Every offset below was taken
// from the compiler with a throwaway offsetof program, not read off librw's
// header.
//
// This one carries an assertion the other layout files do not need. RpWorld is
// mirrored in two halves: the first four members ARE rw::World, and everything
// after them lives in a plugin block that RpWorldPluginAttach reserves. The
// C declaration puts that block at a fixed offset, so the head assertions are
// not enough on their own -- the tail has to start exactly where librw hands
// out the first plugin, which is sizeof(rw::World). That is asserted here at
// compile time and checked again at runtime against the offset librw actually
// returns, in RpWorldPluginAttach.

#include <rwcore.h>
#include <rpworld.h>

#include "rw.h"

#include <stddef.h>

#define SAME_SIZE(ours, theirs)                                                                    \
    static_assert(sizeof(ours) == sizeof(theirs), #ours " and " #theirs " differ in size")

#define SAME_OFFSET(ours, ourfield, theirs, theirfield)                                            \
    static_assert(offsetof(ours, ourfield) == offsetof(theirs, theirfield),                        \
                  #ours "." #ourfield " is not where " #theirs "." #theirfield " is")

// --- RpWorld, the librw half -----------------------------------------------
//
// librw's order is localLights, globalLights, clumps; RenderWare's is
// clumpList, lightList, directionalLightList. Ours keeps RenderWare's NAMES at
// librw's OFFSETS. The two light lists are the same split under different
// names -- librw's "local" is RenderWare's positioned lights (rpLIGHTPOINT and
// above) and librw's "global" is the directional and ambient ones -- so getting
// the pair the wrong way round would silently light the world from the wrong
// set. Hence one assertion each rather than a size check on the group.

SAME_OFFSET(RpWorld, object, rw::World, object);
SAME_OFFSET(RpWorld, lightList, rw::World, localLights);
SAME_OFFSET(RpWorld, directionalLightList, rw::World, globalLights);
SAME_OFFSET(RpWorld, clumpList, rw::World, clumps);

// NOT SAME_SIZE: ours is deliberately larger. rw::World ends where the plugin
// block begins, and everything from RpWorld::flags down lives in it.
static_assert(offsetof(RpWorld, flags) == sizeof(rw::World),
              "RpWorld's plugin tail does not start where librw's first World plugin does");

static_assert(sizeof(RpWorld) > sizeof(rw::World),
              "RpWorld has no tail left -- the plugin block would be zero bytes");

// --- the enumerations that cast straight through ---------------------------
//
// rpWORLD is the object type RwObject::type carries, and librw's World::ID is
// what World::create writes into it. RpWorldGetBBox and the rest never check
// it, but rwObjectGetType does, and a mismatch would make every world look like
// some other kind of object.

static_assert((int)rpWORLD == (int)rw::World::ID, "the world's object type renumbered");

// The light-type split that decides which of the two lists RpWorldAddLight
// puts a light in. Asserted here as well as in layout_camera.cpp because it is
// the world that acts on it: librw's World::addLight compares against
// Light::POINT, and rpLIGHTPOSITIONINGSTART is RenderWare's name for the same
// boundary.
static_assert((int)rpLIGHTPOSITIONINGSTART == (int)rw::Light::POINT,
              "the positioned-light boundary moved");

// --- RpWorldSector is NOT mirrored -----------------------------------------
//
// There is nothing to mirror it onto: librw has no world sector type, no plane
// sectors and no world chunk reader, so RpWorldSector in rpworld.h keeps
// RenderWare's own layout untouched and nothing allocates one. The assertion
// that matters about it is therefore not an offset but the fact that no
// RpWorldSector is ever handed to librw -- see collision_world.cpp.
//
// One thing is worth pinning down anyway, because a world reader will have to
// honour it: iCollide.cpp and xCollide.cpp read
// `sector->polygons[tri->index].matIndex`, so RpPolygon's stride and the
// position of matIndex within it are load-bearing for the object ids the whole
// collision system dispatches on.
static_assert(sizeof(RpPolygon) == 8, "RpPolygon is not four uint16s");
static_assert(offsetof(RpPolygon, matIndex) == 0, "RpPolygon::matIndex moved");
static_assert(offsetof(RpPolygon, vertIndex) == 2, "RpPolygon::vertIndex moved");
