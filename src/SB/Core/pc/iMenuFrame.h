#ifndef IMENUFRAME_H
#define IMENUFRAME_H

// PC-only: the menu's bamboo frame, rebuilt for a screen wider than the one it
// was drawn for.
//
// The frame is a single 80-vertex mesh and NOT a row of entities, so there is
// nothing to draw more of; and xModelRender2D takes its scale from the rect's
// WIDTH alone and applies it to both axes, so a wider rect enlarges the whole
// frame -- poles and all -- rather than stretching it. Neither is what a
// widescreen frame wants. The mesh itself is what has to change.
//
// What makes that possible is how the artist built it. Each rail is a cap, four
// copies of one bamboo tile, and another cap; each stile is four copies of the
// same tile turned on its side. Sixteen of the twenty quads share a single UV
// rectangle. So a wider frame is the same tile a few more times -- not a
// resample, not a guess, just the repeat the artist was already making.

struct RpAtomic;
struct RpClump;

// Rebuild this atomic's geometry to fit the screen's margin, from the mesh the
// artist made. Asked on every draw; it rebuilds only when the margin has
// changed since the last rebuild -- video.ui changing in the settings screen --
// and a margin of nothing puts the original back. Returns TRUE if it replaced
// the geometry, FALSE if there was nothing to do or if the mesh is not the
// frame this knows how to widen, in which case the atomic is left as it was.
//
// `rectWidth` is the normalized width the model is drawn into, which is what
// converts the screen's margin into the frame's own object space.
int iMenuFrameWiden(RpAtomic* atomic, float rectWidth);

// The model is being unloaded: forget its atomics and give back the original
// mesh kept for each. Called from iModel.cpp's unload, beside the other passes
// that keep something per model.
void iMenuFrameForget(RpClump* clump);

#endif
