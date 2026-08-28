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
//
// See PCPORT.md for the measurements this rests on.

struct RpAtomic;

// Rebuild this atomic's geometry wider, once. Returns TRUE if it replaced the
// geometry, FALSE if there was nothing to do (a 4:3 or pillarboxed screen has
// no margin to fill) or if the mesh is not the frame this knows how to widen,
// in which case the atomic is left exactly as it was.
//
// `rectWidth` is the normalized width the model is drawn into, which is what
// converts the screen's margin into the frame's own object space.
int iMenuFrameWiden(RpAtomic* atomic, float rectWidth);

#endif
