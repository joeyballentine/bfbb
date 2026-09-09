// librw's headers BEFORE the game's -- see iShadowMap.cpp for why the order is
// load-bearing.
#include <rw.h>

#include "iToon.h"

#include <rwcore.h>

#include "iScreen.h"
#include "rw/backend.h"
#include "xModel.h"
#include "xMathInlines.h"

#include <rpworld.h>
#include <rpmatfx.h>

#include <stdio.h>
#include <string.h>

// **D3D9 and GL3 both draw this, and one build carries both.**
//
// The cel look is shaders, and librw has them in the D3D9 tree and the GL3 tree
// but not the D3D11 one. Which of the three is drawing is not known until
// iBackendResolve has run, so the pick is per call rather than per build --
// the same dispatch rw/glow.cpp takes, and for the same reason. A backend with
// no permutation lands on the empty body and the game draws as it did without
// the setting.
namespace toonbackend
{
    inline void setToonRamp(rw::Texture* t)
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::setToonRamp(t);
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::setToonRamp(t);
            return;
        }
#endif
        (void)t;
    }

    inline void setToonRoomTint(F32 r, F32 g, F32 b)
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::setToonRoomTint(r, g, b);
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::setToonRoomTint(r, g, b);
            return;
        }
#endif
        (void)r;
        (void)g;
        (void)b;
    }

    inline void clearToonRoomTint()
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::clearToonRoomTint();
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::clearToonRoomTint();
            return;
        }
#endif
    }

    inline void setToonLightDir(F32 x, F32 y, F32 z)
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::setToonLightDir(x, y, z);
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::setToonLightDir(x, y, z);
            return;
        }
#endif
        (void)x;
        (void)y;
        (void)z;
    }

    inline void clearToonLightDir()
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::clearToonLightDir();
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::clearToonLightDir();
            return;
        }
#endif
    }

    inline void setOutline(F32 r, F32 g, F32 b, F32 w)
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::setOutline(r, g, b, w);
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::setOutline(r, g, b, w);
            return;
        }
#endif
        (void)r;
        (void)g;
        (void)b;
        (void)w;
    }

    inline void setOutlineLower(F32 r, F32 g, F32 b)
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::setOutlineLower(r, g, b);
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::setOutlineLower(r, g, b);
            return;
        }
#endif
        (void)r;
        (void)g;
        (void)b;
    }

    inline void setOutlineFlat(S32 a, S32 b)
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::setOutlineFlat(a, b);
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::setOutlineFlat(a, b);
            return;
        }
#endif
        (void)a;
        (void)b;
    }

    inline void setOutlineSplit(F32 y)
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::setOutlineSplit(y);
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::setOutlineSplit(y);
            return;
        }
#endif
        (void)y;
    }

    inline void setOutlineMode(S32 mode)
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::setOutlineMode(mode);
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::setOutlineMode(mode);
            return;
        }
#endif
        (void)mode;
    }

    inline void setToonLook(F32 wrap, F32 rim, F32 rimStart, F32 occlusion, F32 hardness)
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::setToonLook(wrap, rim, rimStart, occlusion, hardness);
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::setToonLook(wrap, rim, rimStart, occlusion, hardness);
            return;
        }
#endif
        (void)wrap;
        (void)rim;
        (void)rimStart;
        (void)occlusion;
        (void)hardness;
    }

    inline void setToonRampRow(S32 row)
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::setToonRampRow(row);
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::setToonRampRow(row);
            return;
        }
#endif
        (void)row;
    }

    inline void setOutlineMinWidth(F32 w)
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::setOutlineMinWidth(w);
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::setOutlineMinWidth(w);
            return;
        }
#endif
        (void)w;
    }

    inline void setToonShading(S32 on, F32 bands, F32 saturation, F32 strength)
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::setToonShading(on, bands, saturation, strength);
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::setToonShading(on, bands, saturation, strength);
            return;
        }
#endif
        (void)on;
        (void)bands;
        (void)saturation;
        (void)strength;
    }

    inline void setOutlineMaxWidth(F32 w)
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::setOutlineMaxWidth(w);
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::setOutlineMaxWidth(w);
            return;
        }
#endif
        (void)w;
    }
}

#if defined(RW_D3D9) || defined(RW_GL3)
#define TOON_HAVE_BACKEND 1
#endif

// The cel ramp. iToon.h says what it is for; this is how it is made.

static RwTexture* sRamp;

// How wide the strip is. The bands are cut out of it, so this only has to be
// fine enough that a band edge lands where it was asked to -- 64 puts every
// edge within a sixty-fourth of the light range, which is finer than the eye
// can place a terminator.
static const S32 kRampWidth = 64;

// **One ramp is not enough, because skin does not band like sheet metal.**
//
// The strip is a stack of them, one per row, and the game names a row per draw.
// A row is a whole look: its own two ends, its own terminator, its own number
// of steps. That is what a ramp texture is FOR -- the shader reads a colour and
// asks no questions, so all of this is data rather than code.
//
// header.frag hardcodes TOON_RAMP_ROWS to match, because a shader cannot ask
// how tall its own texture is and the row coordinate has to be divided by
// something.
struct ToonRampRow
{
    // The two ends, as multipliers on the light's own colour.
    //
    // **The shadow end is not grey.** It is darker AND cooler, which is the
    // whole point: a shadow in the show is painted a different colour, not the
    // same colour with less of it. The lit end sits barely under one and
    // slightly warm, so a lit surface reads as the artwork plus a little sun.
    F32 shadow[3];
    F32 lit[3];

    // Where the terminator sits within the row.
    //
    // Not the middle. A face lit from the front is mostly lit, and a
    // terminator at half puts the dividing line across the middle of every
    // character's face -- which is where a lighting model puts it and where an
    // animator does not.
    F32 terminator;

    // How many steps, against the setting. The setting is what a character
    // gets; a row that wants to be harder or softer than him says so here.
    S32 bandBias;
};

static const ToonRampRow kRampRows[ITOON_RAMP_ROWS] = {
    // Characters. The tuning everything else is measured against.
    { { 0.34f, 0.40f, 0.62f }, { 1.0f, 0.97f, 0.88f }, 0.45f, 0 },

    // Metal. Harder and cooler at both ends: a reflective surface in the show
    // is drawn as two flat tones with a bright edge rather than as a graded
    // curve, so it loses a step and gains contrast. The robots are what this
    // is for.
    { { 0.22f, 0.26f, 0.42f }, { 1.0f, 1.0f, 1.0f }, 0.50f, -1 },

    // The world.
    //
    // **A softer row was the obvious call and it was wrong.** The first version
    // of this gave a background a lighter shadow and a fourth step, on the
    // theory that hard bands across a wall read as a mistake -- and what it
    // actually did was take the cartoon out of the levels, which is the one
    // thing they were being cel-shaded for.
    //
    // So it is as hard as a character and separated further by COLOUR instead:
    // the shadow end carries more blue than the character row and sits a little
    // deeper, and the lit end is warmer. Vibrance in a drawing comes from the
    // lit and shade tones being different HUES rather than from either of them
    // being more saturated -- which is what a painter does and what turning
    // toon_saturation up cannot imitate.
    { { 0.28f, 0.36f, 0.64f }, { 1.0f, 0.97f, 0.86f }, 0.42f, 0 },

    // Spare. Point sampling means a row nobody asks for costs nothing, and a
    // power of two keeps the row coordinate exact.
    { { 0.34f, 0.40f, 0.62f }, { 1.0f, 0.97f, 0.88f }, 0.45f, 0 },
};

// Which way round a 8888 raster stores its channels on this backend.
#ifdef RW_D3D9
static const S32 kRasterIsBGRA = TRUE;
#else
static const S32 kRasterIsBGRA = FALSE;
#endif

static U8 ToByte(F32 v)
{
    S32 i = (S32)(v * 255.0f + 0.5f);

    if (i < 0) i = 0;
    if (i > 255) i = 255;

    return (U8)i;
}

// One row of the strip.
//
// `bands` is the setting; the row's own bias moves it. The terminator is put in
// by warping the light term before it is cut rather than by cutting unevenly:
// below it the whole shadow side is squeezed into the first band, above it the
// rest are spread over what remains. One expression, and moving the terminator
// moves every band with it.
static void WriteRampRow(U8* px, const ToonRampRow* row, S32 bands)
{
    bands += row->bandBias;

    if (bands < 2) bands = 2;
    if (bands > 8) bands = 8;

    for (S32 x = 0; x < kRampWidth; x++)
    {
        F32 l = (x + 0.5f) / (F32)kRampWidth;
        F32 t;

        if (l < row->terminator)
        {
            t = 0.5f * l / row->terminator;
        }
        else
        {
            t = 0.5f + 0.5f * (l - row->terminator) / (1.0f - row->terminator);
        }

        F32 step = (F32)((S32)(t * bands));

        if (step > bands - 1) step = (F32)(bands - 1);

        F32 f = step / (F32)(bands - 1);

        U8 r = ToByte(row->shadow[0] + (row->lit[0] - row->shadow[0]) * f);
        U8 g = ToByte(row->shadow[1] + (row->lit[1] - row->shadow[1]) * f);
        U8 b = ToByte(row->shadow[2] + (row->lit[2] - row->shadow[2]) * f);

        // **The channel order is the backend's, not RGBA.**
        //
        // A 8888 raster is GL_RGBA on GL3 and D3DFMT_A8R8G8B8 on D3D9, and the
        // second is B, G, R, A in memory. Writing red first either way put the
        // ramp in backwards on D3D9: its cool shadow of 0.34 0.40 0.62 came
        // out as 0.62 0.40 0.34, a warm orange, so every shadow in the game
        // turned orange and a blue room multiplied by it went grey.
        //
        // Worth noticing that this is the same trap as reading SpongeBob's
        // texture, which is palettised BGRA and comes out cyan taken for RGBA.
        // RenderWare says 8888 and means whatever the device means by it.
        px[x * 4 + 0] = kRasterIsBGRA ? b : r;
        px[x * 4 + 1] = g;
        px[x * 4 + 2] = kRasterIsBGRA ? r : b;
        px[x * 4 + 3] = 255;
    }
}

void iToonInit(S32 bands)
{
    if (sRamp != NULL)
    {
        return;
    }

#ifndef TOON_HAVE_BACKEND
    // Nothing to say about it: a backend without the shaders is not a failure,
    // and the game is about to draw exactly as it did before.
    return;
#endif

    if (bands < 2)
    {
        bands = 2;
    }
    if (bands > 8)
    {
        bands = 8;
    }

    // Type AND format, not just type. A bare type leaves the format zero,
    // which is not a format the device can make -- the shadow map gets away
    // with it because a camera texture takes the frame buffer's.
    RwRaster* raster =
        RwRasterCreate(kRampWidth, ITOON_RAMP_ROWS, 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);

    if (raster == NULL)
    {
        printf("bfbb: no cel ramp; the toon look falls back to plain banding\n");
        fflush(stdout);
        return;
    }

    U8* px = RwRasterLock(raster, 0, rwRASTERLOCKWRITE);

    if (px == NULL)
    {
        RwRasterDestroy(raster);
        printf("bfbb: the cel ramp could not be written\n");
        fflush(stdout);
        return;
    }

    // A locked raster is addressed by its stride, not by its width. The two
    // happen to agree on a 32-bit raster this narrow, and relying on that is
    // exactly how a second row ends up written over the first.
    S32 stride = RwRasterGetStride(raster);

    // **Bounded by the raster and not by the constant.** These have to agree,
    // and when they did not -- the strip grew to four rows while the raster
    // creation above still asked for one -- the loop wrote three rows past a
    // 256-byte allocation and the game died on the next free, hundreds of
    // frames later, with a heap corruption and no stack. Reading the height
    // back costs nothing and turns that into a missing band.
    S32 rows = RwRasterGetHeight(raster);

    if (rows > ITOON_RAMP_ROWS)
    {
        rows = ITOON_RAMP_ROWS;
    }

    for (S32 row = 0; row < rows; row++)
    {
        WriteRampRow(px + row * stride, &kRampRows[row], bands);
    }

    RwRasterUnlock(raster);

    sRamp = RwTextureCreate(raster);

    if (sRamp == NULL)
    {
        RwRasterDestroy(raster);
        printf("bfbb: the cel ramp has no texture\n");
        fflush(stdout);
        return;
    }

    // **Point sampled, and clamped.** A band edge is the entire point of the
    // thing; filtering it turns each one back into the smooth ramp it was made
    // to replace. Clamped so the two ends hold rather than wrapping the lit
    // colour around to the shadow one.
    RwTextureSetFilterMode(sRamp, rwFILTERNEAREST);
    RwTextureSetAddressing(sRamp, rwTEXTUREADDRESSCLAMP);

    toonbackend::setToonRamp(reinterpret_cast<rw::Texture*>(sRamp));

    printf("bfbb: cel ramp %d rows, %d bands, terminator at %.0f%%\n",
           (int)ITOON_RAMP_ROWS, (int)bands, kRampRows[0].terminator * 100.0f);
    fflush(stdout);
}

// What an ink is: the surface it surrounds, darkened. Flat on all three
// channels, or it tints every character towards whatever it favours.
//
// A named colour was tried for SpongeBob -- the exact green of the holes in his
// texture, 167 180 8, measured out of chr_sb05.RW3 -- on the grounds that the
// show inks him in it. It is the right green and it is the wrong idea: it is a
// fixed colour sitting on a character whose own shading moves with the room, so
// the moment the lighting is anything but neutral the line and the surface it
// surrounds disagree. A darkened copy of the surface cannot disagree with it.
static const F32 kInkScale = 0.35f;

// **The see-through half of a frame is not drawn, it is painted over.**
//
// A cel ramp and a hull are both statements about a solid surface: the ramp
// says which way it faces the light, the hull says where it ends. A floor
// decal, a plant card, a particle and the number that floats off a clam are
// none of those. They are flat art laid on the picture, and cutting their
// lighting into bands only darkens them, while a hull traces the rectangle they
// were cut from rather than the shape their texture leaves behind.
//
// So the whole alpha pass runs with the look off. One bracket rather than a
// test per mesh, because the game already sorts the frame into a solid half and
// a see-through half and this is that seam.
static S32 sPaused;

void iToonPause(S32 on)
{
    sPaused = on ? 1 : 0;

    if (sPaused)
    {
        toonbackend::setToonShading(FALSE, iScreenToonBands(), iScreenToonSaturation(),
                                    iScreenToonStrength());
        toonbackend::setOutlineMode(ITOON_OUTLINE_NONE);
        return;
    }

    toonbackend::setToonShading(iScreenToon(), iScreenToonBands(), iScreenToonSaturation(),
                                iScreenToonStrength());
}

void iToonSetOutline(S32 mode)
{
    if (sPaused)
    {
        return;
    }

    toonbackend::setOutline(kInkScale, kInkScale, kInkScale, iScreenToonOutline());

    // Two tones only where there are two: the upper ink is the surface
    // darkened, as everywhere, and the lower one is flat black, because his
    // trousers are inked black in the show whatever they are painted.
    toonbackend::setOutlineLower(0.0f, 0.0f, 0.0f);
    toonbackend::setOutlineFlat(FALSE, TRUE);

    toonbackend::setOutlineMode(mode);
}


// Welding, so the hull does not split at a hard corner.
//
// **This is what leaves the corners open, and it is not a bug in the hull.** A
// model stores a hard edge by DUPLICATING the vertex: one copy per face, each
// carrying its own face's normal. Inflating along those normals sends the two
// copies in two different directions, and the faces that used to meet at that
// edge come apart -- a gap exactly where the outline is supposed to be
// strongest, which on a shape as boxy as SpongeBob is every corner he has.
//
// The cure is to inflate along an average instead: every copy of a position
// moves the same way, so the surface stays closed while it swells. The GameCube
// version did this into a second copy of the mesh; here it is done in place,
// because the vertex buffer has one normal per vertex and no room for a second.
//
// **So it changes the shading too**, and in the right direction: a welded
// normal is a smoothed one, and smooth shading over a cel ramp is what puts a
// single flat band on a character's side instead of one band per facet. It is
// only done to characters, which are the only things drawn this way.
//
// Once per geometry. The lock and unlock are what make librw rebuild the
// vertex buffer afterwards -- without them the new normals sit in memory that
// nothing reads again.

enum
{
    kWeldSlots = 512
};

// Geometries already welded, so a character costs this once rather than once a
// frame. Cleared when a level unloads would be better; a stale entry only means
// a geometry at a recycled address is not welded a second time, and it was
// already welded.
static void* sWelded[kWeldSlots];

// Where the two inks meet on each of them, in object space, worked out from the
// same walk that welds. Kept because it is a property of the model and the
// renderer needs it every draw.
static F32 sSplitY[kWeldSlots];

static U32 PointerSlot(void* p, S32 slots)
{
    U32 h = (U32)(uintptr_t)p;

    h ^= h >> 16;
    h *= 0x2C1B3C6Du;
    h ^= h >> 15;

    return h & (slots - 1);
}

// The slot this geometry occupies, claiming one if it is new. `fresh` says
// whether it had to be claimed.
static S32 WeldSlot(void* geo, S32* fresh)
{
    U32 i = PointerSlot(geo, kWeldSlots);
    S32 tries = 0;

    while (sWelded[i] != NULL && tries < kWeldSlots)
    {
        if (sWelded[i] == geo)
        {
            *fresh = FALSE;
            return (S32)i;
        }

        i = (i + 1) & (kWeldSlots - 1);
        tries++;
    }

    if (tries >= kWeldSlots)
    {
        *fresh = FALSE;
        return -1;
    }

    sWelded[i] = geo;
    *fresh = TRUE;
    return (S32)i;
}

static S32 SamePoint(const RwV3d* a, const RwV3d* b)
{
    return a->x == b->x && a->y == b->y && a->z == b->z;
}

static void WeldGeometry(RpGeometry* geo)
{
    S32 n = geo->numVertices;

    if (n <= 0 || geo->morphTarget == NULL || geo->morphTarget[0].verts == NULL ||
        geo->morphTarget[0].normals == NULL)
    {
        return;
    }

    RwV3d* verts = geo->morphTarget[0].verts;
    RwV3d* norms = geo->morphTarget[0].normals;
    RwV3d* summed = (RwV3d*)RwMalloc(n * sizeof(RwV3d));

    if (summed == NULL)
    {
        return;
    }

    for (S32 i = 0; i < n; i++)
    {
        summed[i] = norms[i];
    }

    // Quadratic in the worst case, and that is fine: a character is a couple of
    // thousand vertices and this runs once. A hash would be faster and is the
    // right answer if it ever runs on the world.
    for (S32 i = 0; i < n; i++)
    {
        for (S32 j = i + 1; j < n; j++)
        {
            if (SamePoint(&verts[i], &verts[j]))
            {
                summed[i].x += norms[j].x;
                summed[i].y += norms[j].y;
                summed[i].z += norms[j].z;
                summed[j].x += norms[i].x;
                summed[j].y += norms[i].y;
                summed[j].z += norms[i].z;
            }
        }
    }

    // 0x4 is librw Geometry::LOCKNORMALS. rpworld.h declares the call and
    // not the flags, so the value is spelled out rather than named.
    RpGeometryLock(geo, 0x4);

    for (S32 i = 0; i < n; i++)
    {
        F32 len2 = summed[i].x * summed[i].x + summed[i].y * summed[i].y +
                   summed[i].z * summed[i].z;

        if (len2 > 1e-12f)
        {
            F32 inv = 1.0f / xsqrt(len2);

            norms[i].x = summed[i].x * inv;
            norms[i].y = summed[i].y * inv;
            norms[i].z = summed[i].z * inv;
        }
    }

    RpGeometryUnlock(geo);
    RwFree(summed);
}

// Where the model's lower ink starts, from the bind pose.
//
// **The real vertex range, not the bounding sphere.** The sphere was the cheap
// way to get a height and it is wrong for the shape it is asked about: a sphere
// around something boxy reaches well below the box, so a fraction of its radius
// lands under the model and the upper ink bleeds down over the trousers. Thirty
// per cent of the actual range is what the GameCube version used and it is
// right for the same reason -- it is measured on him rather than on a sphere he
// happens to fit inside.
static F32 SplitHeight(RpGeometry* geo)
{
    const RwV3d* v = geo->morphTarget[0].verts;
    F32 lo = v[0].y;
    F32 hi = v[0].y;

    for (S32 i = 1; i < geo->numVertices; i++)
    {
        if (v[i].y < lo) lo = v[i].y;
        if (v[i].y > hi) hi = v[i].y;
    }

    return lo + (hi - lo) * 0.30f;
}

F32 iToonWeld(void* atomic)
{
    RpAtomic* a = (RpAtomic*)atomic;

    if (a == NULL)
    {
        return -1.0e30f;
    }

    RpGeometry* geo = RpAtomicGetGeometry(a);

    if (geo == NULL || geo->numVertices <= 0 || geo->morphTarget == NULL ||
        geo->morphTarget[0].verts == NULL)
    {
        return -1.0e30f;
    }

    S32 fresh = FALSE;
    S32 slot = WeldSlot(geo, &fresh);

    if (slot < 0)
    {
        return -1.0e30f;
    }

    if (fresh)
    {
        WeldGeometry(geo);
        sSplitY[slot] = SplitHeight(geo);
    }

    return sSplitY[slot];
}

void iToonSetRampRow(S32 row)
{
    if (row < 0 || row >= ITOON_RAMP_ROWS)
    {
        row = ITOON_RAMP_CHARACTER;
    }

    toonbackend::setToonRampRow(row);
}

// Which row each geometry wants, alongside where its inks meet. Same slot, same
// walk, same reason: it is a property of the model and the renderer asks every
// draw.
static S32 sRampRow[kWeldSlots];

static S32 RampRowOf(RpGeometry* geo)
{
    // A material with a reflection on it is metal. Nothing else in these models
    // distinguishes a robot from a fish without the game naming it, and the
    // game has no field that answers -- baseType says NPC for both.
    for (S32 i = 0; i < geo->matList.numMaterials; i++)
    {
        RpMaterial* mat = geo->matList.materials[i];

        if (mat != NULL && RpMatFXMaterialGetEffects(mat) != rpMATFXEFFECTNULL)
        {
            return ITOON_RAMP_METAL;
        }
    }

    return ITOON_RAMP_CHARACTER;
}

S32 iToonRampRowFor(void* atomic)
{
    RpAtomic* a = (RpAtomic*)atomic;

    if (a == NULL)
    {
        return ITOON_RAMP_CHARACTER;
    }

    RpGeometry* geo = RpAtomicGetGeometry(a);

    if (geo == NULL)
    {
        return ITOON_RAMP_CHARACTER;
    }

    S32 fresh = FALSE;
    S32 slot = WeldSlot(geo, &fresh);

    if (slot < 0)
    {
        return ITOON_RAMP_CHARACTER;
    }

    // The weld runs on the same slot and claims it first. Either it has already
    // been here, or this call is what claims the slot and has to fill it.
    if (fresh)
    {
        WeldGeometry(geo);
        sSplitY[slot] = SplitHeight(geo);
    }

    if (sRampRow[slot] == 0)
    {
        // Stored one higher than it is, so that zero can mean unanswered
        // without a second array of flags.
        sRampRow[slot] = RampRowOf(geo) + 1;
    }

    return sRampRow[slot] - 1;
}

// A number of pixels as world units per unit of view depth, or 0 if the camera
// cannot say.
//
// **The renderer cannot work this out and the game can.** Turning a width in
// pixels into one in world units needs the camera's view window and the height
// of the picture, and librw is handed neither -- it gets a projection matrix
// that has already swallowed both. So the sum happens here.
//
// A view window is the half-extent of the picture at unit distance, so the
// number of pixels an offset d covers at depth z is d*H / (2*window.y*z).
// Turned round: the width that covers a given number of pixels is that many
// times 2*window.y/H, times z. The shader has z as clip w.
static F32 PixelsPerDepth(F32 pixels)
{
    if (pixels <= 0.0f)
    {
        return 0.0f;
    }

    RwCamera* cam = RwCameraGetCurrentCamera();
    const RwV2d* vw = cam != NULL ? RwCameraGetViewWindow(cam) : NULL;
    F32 h = iScreenHeightF();

    if (vw == NULL || h < 1.0f)
    {
        return 0.0f;
    }

    return 2.0f * pixels * vw->y / h;
}

// How thick the ink may get, in the same units.
//
// **The width is in world units, so it swells as the camera closes.** That is
// right for a thing in the world and wrong for a drawn line, which holds one
// weight whatever the shot -- walk up to a character and his outline turns into
// a marker stroke. The cap is in pixels for the same reason the floor is: it is
// a statement about how the line READS, and how it reads is a screen measure.
//
// Zero is no cap, which is the shipped behaviour of the branch this came from.
void iToonOutlineMaxWidth()
{
    toonbackend::setOutlineMaxWidth(PixelsPerDepth(iScreenToonOutlineMax()));
}

// How thin it may get, the same way. A fixed world width falls below a pixel
// somewhere down the level and the character stops being inked, which is the
// one thing an animated drawing never does.
void iToonOutlineMinWidth()
{
    toonbackend::setOutlineMinWidth(PixelsPerDepth(iScreenToonOutlineMin()));
}

// The colour of the room, held between the scene setting it and each character
// being drawn.
static F32 sRoomTint[3];
static S32 sRoomTintValid;

void iToonSetRoomTint(const F32* rgb)
{
    if (rgb == NULL)
    {
        sRoomTintValid = FALSE;
        return;
    }

    // Normalized to its brightest channel. What is wanted from a room is its
    // COLOUR: a character in a dim room should be the colour of the room, not
    // dimmed to nothing on top of the shading the ramp already gives him.
    F32 m = rgb[0];

    if (rgb[1] > m) m = rgb[1];
    if (rgb[2] > m) m = rgb[2];

    if (m < 1e-4f)
    {
        sRoomTintValid = FALSE;
        return;
    }

    sRoomTint[0] = rgb[0] / m;
    sRoomTint[1] = rgb[1] / m;
    sRoomTint[2] = rgb[2] / m;
    sRoomTintValid = TRUE;
}

void iToonRoomTintApply()
{
    if (sRoomTintValid)
    {
        toonbackend::setToonRoomTint(sRoomTint[0], sRoomTint[1], sRoomTint[2]);
    }
}

void iToonRoomTintClear()
{
    toonbackend::clearToonRoomTint();
}

void iToonOutlineSplit(F32 y, S32 mode)
{
    // Below every vertex for a one-ink model, which is a split that never fires
    // rather than a second path through the shader.
    toonbackend::setOutlineSplit(mode == ITOON_OUTLINE_TWOTONE ? y : -1.0e30f);
}

void iToonFaceLight(const RwMatrixTag* mat)
{
    S32 mode = iScreenToonFaceLight();

    if (mode == ITOON_LIGHT_FACE)
    {
        if (mat == NULL)
        {
            return;
        }

        // Straight back through him from the front, so his face is squarest to
        // the light. `at` is the model's forward; the light travels the other
        // way.
        toonbackend::setToonLightDir(-mat->at.x, -mat->at.y, -mat->at.z);
    }
    else if (mode == ITOON_LIGHT_CAMERA)
    {
        RwCamera* cam = RwCameraGetCurrentCamera();
        RwFrame* frame = cam ? (RwFrame*)cam->object.object.parent : NULL;

        if (frame == NULL)
        {
            return;
        }

        // Away from the viewer, straight down the lens. Whichever side of a
        // character you can see is the side facing the light, so he is lit
        // when he turns towards you and shaded when he walks off -- which the
        // show does too, and which the model's own front cannot do because it
        // does not know where you are standing.
        //
        // No negation: the camera's `at` already points into the scene, which
        // is the direction the light needs to travel.
        RwMatrixTag* m = &frame->ltm;

        toonbackend::setToonLightDir(m->at.x, m->at.y, m->at.z);
    }
}

void iToonFaceLightClear()
{
    toonbackend::clearToonLightDir();
}

// Which atomics are outlined this frame.
//
// Open addressing over the atomic pointer, rebuilt every frame. A level has a
// few dozen characters and each has a handful of atomics, so the table never
// fills; a scene that somehow overran it would lose outlines rather than
// misplace them.
enum
{
    kOutlineSlots = 256
};

static void* sOutlineKey[kOutlineSlots];
static S32 sOutlineMode[kOutlineSlots];

static U32 OutlineSlot(void* atomic)
{
    U32 h = (U32)(uintptr_t)atomic;

    h ^= h >> 16;
    h *= 0x2C1B3C6Du;
    h ^= h >> 15;

    return h & (kOutlineSlots - 1);
}

void iToonOutlineClear()
{
    memset(sOutlineKey, 0, sizeof(sOutlineKey));
}

void iToonOutlineRegister(xModelInstance* model, S32 mode)
{
    if (mode == ITOON_OUTLINE_NONE)
    {
        return;
    }

    // The whole chain. A character is several models linked through Next --
    // SpongeBob's body, his eyes, whatever he is holding -- and xModelRender
    // walks all of them, so all of them need the mark.
    for (xModelInstance* m = model; m != NULL; m = m->Next)
    {
        void* key = m->Data;

        if (key == NULL)
        {
            continue;
        }

        U32 i = OutlineSlot(key);
        S32 tries = 0;

        while (sOutlineKey[i] != NULL && sOutlineKey[i] != key && tries < kOutlineSlots)
        {
            i = (i + 1) & (kOutlineSlots - 1);
            tries++;
        }

        if (tries < kOutlineSlots)
        {
            sOutlineKey[i] = key;
            sOutlineMode[i] = mode;
        }
    }
}

// What a model that nobody registered gets.
//
// **This is where "everything" is spelled, and it costs one branch.** The
// alternative is walking the scene and registering every entity, which means
// finding every list that draws one -- the entity walk, the pickups, the simple
// objects, the shrapnel, the glyphs, the hazards -- and adding a call to each.
// Flipping the default reaches all of them at once, because every one of them
// is drawn through iModelRender and iModelRender asks this.
//
// The level is not reached and cannot be: iEnv.cpp draws the world through
// RpAtomicRender and never comes here. That is the line the setting draws.
static S32 sOutlineDefault = ITOON_OUTLINE_NONE;

void iToonSetOutlineDefault(S32 mode)
{
    sOutlineDefault = mode;
}

S32 iToonOutlineFind(void* atomic)
{
    if (atomic == NULL)
    {
        return ITOON_OUTLINE_NONE;
    }

    U32 i = OutlineSlot(atomic);
    S32 tries = 0;

    while (sOutlineKey[i] != NULL && tries < kOutlineSlots)
    {
        if (sOutlineKey[i] == atomic)
        {
            return sOutlineMode[i];
        }

        i = (i + 1) & (kOutlineSlots - 1);
        tries++;
    }

    return sOutlineDefault;
}

void iToonShutdown()
{
    if (sRamp != NULL)
    {
        RwTextureDestroy(sRamp);
        sRamp = NULL;
    }
}
