#ifndef IMENUWIDE_H
#define IMENUWIDE_H

#include <types.h>

// The PC screens' layout on a screen wider than 4:3 with video.ui = native,
// where the bamboo frame reaches the screen edges: the widgets move out
// toward it instead of staying in the 4:3 box.
//
// How far past the box, each side, the screens spread into, in the 640x480
// units the menus are laid out in. 0 in pillarbox and at 4:3, so the authored
// layout is used unchanged there. Capped a little past 16:9's, so an
// ultrawide screen keeps a 16:9 layout in its middle.
F32 iMenuWideMargin();

// Puts the widget `name` at `x`, `w` wide, keeping its y and height.
void iMenuWidePlace(const char* name, F32 x, F32 w);

#endif
