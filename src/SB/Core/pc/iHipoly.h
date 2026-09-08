#ifndef IHIPOLY_H
#define IHIPOLY_H

#include <types.h>

#include <rwcore.h>
#include <rpworld.h>

// PC-only: `experimental.hipoly_assets`. Smooth a level's world and its models
// into curved surfaces as they load, instead of drawing the shipped facets.
// Nothing on disk changes; the geometry is rebuilt in memory between the
// stream read and the first frame. What the smoothing does to a mesh, and
// what it leaves alone, is at the top of iHipolyTess.cpp.

// Read once from the config.
S32 iHipolyEnabled();

// The world. Called by xJSP_MultiStreamRead with the level's clump complete
// and the shipped collision tree (the 0xBEEF01 payload) in hand. Every
// atomic's geometry is replaced by its tessellation, and a collision tree over
// the new triangles comes back in a buffer of `*outSize` bytes, or NULL when
// the setting is off or the world is left as shipped. Each new triangle keeps
// the flags of the one it was cut from, so what was ground is still ground.
void* iHipolyWorld(RpClump* clump, const void* coll, U32 collSize, U32* outSize);

// The buffer lives as long as the tree built over it. xJSP.cpp says which
// tree that is, and says when it goes.
void iHipolyWorldAttach(const void* colltree, void* buffer);
void iHipolyWorldDetach(const void* colltree);

// A model, as the asset loader takes it from iModelFileNew. Every atomic is
// replaced in place, or none is: collision proxies, locators and sprites are
// recognised by shape and left alone. Cutscene models never come here: a
// cutscene writes their vertices by index from its own streams.
void iHipolyModel(RpClump* clump);

#endif
