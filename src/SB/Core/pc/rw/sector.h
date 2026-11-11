#ifndef SB_CORE_PC_RW_SECTOR_H
#define SB_CORE_PC_RW_SECTOR_H

// A world's sector tree, built by RpWorldStreamRead in world.cpp and walked by
// RpWorldRender there and by RpCollisionWorldForAllIntersections in
// collision_world.cpp.
//
// RpWorld::rootSector points at either kind of node, and RpSector::type tells
// them apart: an atomic sector's type is rwSECTORATOMIC, a plane sector's is
// the byte offset of its axis in an RwV3d (0, 4 or 8).

#include <rwcore.h>
#include <rpworld.h>

// Declared rather than included: librw's rw.h has no include guard.
namespace rw
{
    struct Atomic;
}

#define rwSECTORATOMIC (-1)

struct RpPlaneSector
{
    RwInt32 type;
    RwReal value;
    RpSector* leftSubTree;
    RpSector* rightSubTree;
    // Everything in the left subtree lies below leftValue on the axis and
    // everything in the right subtree above rightValue. Both equal value
    // unless the world's sectors overlap.
    RwReal leftValue;
    RwReal rightValue;
};

namespace rwshim
{
    // An atomic sector: RenderWare's struct, then what the port keeps beside it.
    struct AtomicSector
    {
        RpWorldSector sector;

        // Draws the sector, and owns the geometry that sector.vertices,
        // texCoords and preLitLum point into. NULL when the sector has no
        // polygons.
        rw::Atomic* atomic;

        // Whether the stream gave the sector collision data. RenderWare's
        // collision queries skip a sector without it.
        bool collides;
    };

    inline RwReal axisValue(const RwV3d& v, RwInt32 type)
    {
        return type == 0 ? v.x : type == 4 ? v.y : v.z;
    }
} // namespace rwshim

#endif
