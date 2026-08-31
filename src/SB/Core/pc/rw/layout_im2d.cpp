// RwIm2DVertex against the linked backend's Im2DVertex, checked by the compiler.
//
// Separate from layout.cpp because it is the one mirrored type whose target
// CHANGES with the render backend: rw::d3d::Im2DVertex and rw::gl3::Im2DVertex
// are different structs, and under LIBRW_PLATFORM=NULL there is no such struct
// at all. So the assertions here are per backend, and the NULL build gets the
// one thing it can still check -- that the port's own layout is what
// rwplcore.h says it is.
//
// What makes this worth a file of its own: the vertex is handed to the backend
// as raw bytes through a vertex declaration built from offsetof on librw's
// struct (d3dimmed.cpp:47). Nothing at the seam would notice a mismatch. The
// game would draw, and it would draw garbage.

#include <rwcore.h>

#include "rw.h"

#include <stddef.h>

#define SAME_SIZE(ours, theirs)                                                                    \
    static_assert(sizeof(ours) == sizeof(theirs), #ours " and " #theirs " differ in size")

#define SAME_OFFSET(ours, ourfield, theirs, theirfield)                                            \
    static_assert(offsetof(ours, ourfield) == offsetof(theirs, theirfield),                        \
                  #ours "." #ourfield " is not where " #theirs "." #theirfield " is")

// The port's own shape, backend or no backend. These are the offsets the
// twelve RwIm2DVertexSet* macros in rwplcore.h assume.
static_assert(offsetof(RwIm2DVertex, x) == 0, "RwIm2DVertex.x must lead the struct");
static_assert(offsetof(RwIm2DVertex, y) == 4, "RwIm2DVertex.y moved");
static_assert(offsetof(RwIm2DVertex, z) == 8, "RwIm2DVertex.z moved");
static_assert(offsetof(RwIm2DVertex, w) == 12, "RwIm2DVertex.w moved");
static_assert(offsetof(RwIm2DVertex, emissiveColor) == 16, "RwIm2DVertex.emissiveColor moved");
static_assert(offsetof(RwIm2DVertex, u) == 20, "RwIm2DVertex.u moved");
static_assert(offsetof(RwIm2DVertex, v) == 24, "RwIm2DVertex.v moved");
static_assert(sizeof(RwIm2DVertex) == 28, "RwIm2DVertex is not 28 bytes");

// The colour bytes. This is the assertion that catches a swapped red and blue,
// which is otherwise a bug you find by looking at the screen and guessing.
static_assert(sizeof(RwIm2DVertexRGBA) == 4, "the vertex colour is not four bytes");

#if defined(RW_D3D9) || defined(RW_D3D8)

// D3D packs the colour with COLOR_ARGB(a, r, g, b), so the bytes run
// blue, green, red, alpha from the low address up.
static_assert(offsetof(RwIm2DVertexRGBA, blue) == 0, "D3D wants blue in the low byte");
static_assert(offsetof(RwIm2DVertexRGBA, green) == 1, "D3D wants green second");
static_assert(offsetof(RwIm2DVertexRGBA, red) == 2, "D3D wants red third");
static_assert(offsetof(RwIm2DVertexRGBA, alpha) == 3, "D3D wants alpha in the high byte");

SAME_SIZE(RwIm2DVertex, rw::d3d::Im2DVertex);
SAME_OFFSET(RwIm2DVertex, x, rw::d3d::Im2DVertex, x);
SAME_OFFSET(RwIm2DVertex, y, rw::d3d::Im2DVertex, y);
SAME_OFFSET(RwIm2DVertex, z, rw::d3d::Im2DVertex, z);
SAME_OFFSET(RwIm2DVertex, w, rw::d3d::Im2DVertex, w);
SAME_OFFSET(RwIm2DVertex, emissiveColor, rw::d3d::Im2DVertex, color);
SAME_OFFSET(RwIm2DVertex, u, rw::d3d::Im2DVertex, u);
SAME_OFFSET(RwIm2DVertex, v, rw::d3d::Im2DVertex, v);

// The Im3D vertex, which had no assertion at all until a swapped melee streak
// found the gap.
//
// librw packs the colour as one D3DCOLOR word; the port keeps four named bytes
// so that RwIm3DVertexSetRGBA can write them by name. The two only agree if the
// port's BLUE sits where librw's word starts, because D3DCOLOR is ARGB and this
// host is little-endian. Get it wrong and red and blue trade places on every
// Im3D primitive in the game -- which is invisible for the greyscale callers
// and glaring on the coloured ones.
SAME_SIZE(RwIm3DVertex, rw::d3d::Im3DVertex);
SAME_OFFSET(RwIm3DVertex, x, rw::d3d::Im3DVertex, position.x);
SAME_OFFSET(RwIm3DVertex, nx, rw::d3d::Im3DVertex, normal.x);
SAME_OFFSET(RwIm3DVertex, b, rw::d3d::Im3DVertex, color);
SAME_OFFSET(RwIm3DVertex, u, rw::d3d::Im3DVertex, u);
SAME_OFFSET(RwIm3DVertex, v, rw::d3d::Im3DVertex, v);
static_assert(offsetof(RwIm3DVertex, g) == offsetof(RwIm3DVertex, b) + 1,
              "D3DCOLOR wants green above blue");
static_assert(offsetof(RwIm3DVertex, r) == offsetof(RwIm3DVertex, b) + 2,
              "D3DCOLOR wants red above green");
static_assert(offsetof(RwIm3DVertex, a) == offsetof(RwIm3DVertex, b) + 3,
              "D3DCOLOR wants alpha in the high byte");

#elif defined(RW_GL3)

static_assert(offsetof(RwIm2DVertexRGBA, red) == 0, "GL3 wants red in the low byte");
static_assert(offsetof(RwIm2DVertexRGBA, green) == 1, "GL3 wants green second");
static_assert(offsetof(RwIm2DVertexRGBA, blue) == 2, "GL3 wants blue third");
static_assert(offsetof(RwIm2DVertexRGBA, alpha) == 3, "GL3 wants alpha in the high byte");

SAME_SIZE(RwIm2DVertex, rw::gl3::Im2DVertex);
SAME_OFFSET(RwIm2DVertex, x, rw::gl3::Im2DVertex, x);
SAME_OFFSET(RwIm2DVertex, y, rw::gl3::Im2DVertex, y);
SAME_OFFSET(RwIm2DVertex, z, rw::gl3::Im2DVertex, z);
SAME_OFFSET(RwIm2DVertex, w, rw::gl3::Im2DVertex, w);
SAME_OFFSET(RwIm2DVertex, emissiveColor, rw::gl3::Im2DVertex, r);
SAME_OFFSET(RwIm2DVertex, u, rw::gl3::Im2DVertex, u);
SAME_OFFSET(RwIm2DVertex, v, rw::gl3::Im2DVertex, v);

// The Im3D vertex, which the D3D9 arm above checks and this one did not.
//
// librw's gl3 Im3DVertex keeps four separate colour bytes in RGBA order rather
// than D3D's packed ARGB word, so the port's declaration in rwcore.h follows the
// backend -- and these are the assertions that say so. Without them a GL3 build
// took the D3D byte order from an `#ifdef PLATFORM_PC` and crossed red and blue
// on every Im3D primitive, which is invisible for the greyscale callers and
// glaring on the melee streaks.
SAME_SIZE(RwIm3DVertex, rw::gl3::Im3DVertex);
SAME_OFFSET(RwIm3DVertex, x, rw::gl3::Im3DVertex, position.x);
SAME_OFFSET(RwIm3DVertex, nx, rw::gl3::Im3DVertex, normal.x);
SAME_OFFSET(RwIm3DVertex, r, rw::gl3::Im3DVertex, r);
SAME_OFFSET(RwIm3DVertex, g, rw::gl3::Im3DVertex, g);
SAME_OFFSET(RwIm3DVertex, b, rw::gl3::Im3DVertex, b);
SAME_OFFSET(RwIm3DVertex, a, rw::gl3::Im3DVertex, a);
SAME_OFFSET(RwIm3DVertex, u, rw::gl3::Im3DVertex, u);
SAME_OFFSET(RwIm3DVertex, v, rw::gl3::Im3DVertex, v);

#endif
