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

// One atomic, for a mesh that did not exist when the model loaded.
// experimental.solid_flat_props rebuilds a flat prop as a solid at its first
// draw, long after iHipolyModel has been and gone, and the rim it adds wants
// smoothing like everything else. Same treatment as a model's, over a welding
// domain of one.
void iHipolyAtomic(RpAtomic* atomic);

// F8, for before-and-after shots: every replaced atomic draws its shipped
// geometry instead of its smoothed one, both kept. `down` is the key's
// state this frame; the swap is on the press. Only the draw changes: the
// atomic holds the smoothed geometry throughout, and collision stays on it.
void iHipolyHotkey(S32 down);

// A clump is about to be destroyed: forget what was kept for its atomics.
void iHipolyForget(RpClump* clump);

#endif
