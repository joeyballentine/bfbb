#ifndef IRASTERFILL_H
#define IRASTERFILL_H

// How a raster wants its pixels, when the port fills one by hand.
//
// A raster holds its rows and its colour bytes the way librw's own
// rasterFromImage writes them, and the backends do not agree:
//
//   d3d/d3d.cpp     walks the image top-down, and an 8888 raster is BGRA.
//   gl/gl3raster.cpp starts at the LAST row and walks backwards, and an 8888
//                    raster is RGBA. Backwards because the GL3 fragment
//                    shaders sample 1.0 - v, which puts the picture the same
//                    way up D3D's texture space has it.
//
// Neither is wrong. But nothing at RwRasterLock says which one a raster is, so
// anything filling one itself has to ask, and this is where it asks.
// Getting it wrong is quiet: the glyph atlas came out upside down on GL3 and
// the game drew no menu text at all, because the empty third of the atlas is
// what the flip put under the letters.
//
// A RUNTIME question, not a compile-time one: an executable carries several
// backends and video.backend picks between them, so these read the one that
// opened rather than the ones that were linked.
//
// Two callers today, iFontTexture.cpp and iFMV.cpp. A third should come here
// rather than work it out again.

#include "rw/backend.h"

// Row 0 of the picture goes in the LAST row of the locked buffer.
#define IRASTERFILL_ROWS_BOTTOM_UP (iBackendIsGL3() ? 1 : 0)

#define IRASTERFILL_RED (iBackendIsGL3() ? 0 : 2)
#define IRASTERFILL_BLUE (iBackendIsGL3() ? 2 : 0)

#define IRASTERFILL_GREEN 1
#define IRASTERFILL_ALPHA 3

// Where row `y` of a picture `height` rows tall goes.
#define IRASTERFILL_ROW(y, height) (IRASTERFILL_ROWS_BOTTOM_UP ? ((height)-1 - (y)) : (y))

#endif
