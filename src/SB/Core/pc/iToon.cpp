// librw's headers BEFORE the game's -- see iShadowMap.cpp for why the order is
// load-bearing.
#include <rw.h>

#include "iToon.h"

#include <rwcore.h>

#include "iHipoly.h"
#include "iScreen.h"
#include "rw/backend.h"
#include "xModel.h"
#include "xMathInlines.h"

#include <rpworld.h>
#include <rpmatfx.h>

#include <stdio.h>
#include <stdlib.h>
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

    inline void setToonRoomScale(F32 scale)
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::setToonRoomScale(scale);
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::setToonRoomScale(scale);
            return;
        }
#endif
        (void)scale;
    }

    inline void setOutlineInk(F32 saturation, F32 gamma)
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::setOutlineInk(saturation, gamma);
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::setOutlineInk(saturation, gamma);
            return;
        }
#endif
        (void)saturation;
        (void)gamma;
    }

    inline void setToonModelShade(F32 amount)
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::setToonModelShade(amount);
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::setToonModelShade(amount);
            return;
        }
#endif
        (void)amount;
    }

    inline void setOutlineInverted(S32 on)
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d::setOutlineInverted(on);
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::setOutlineInverted(on);
            return;
        }
#endif
        (void)on;
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

    inline void setOutlineAlpha(S32 allow)
    {
#ifdef RW_D3D9
        if (iBackendIsD3D9())
        {
            rw::d3d9::setOutlineAlpha(allow);
            return;
        }
#endif
#ifdef RW_GL3
        if (iBackendIsGL3())
        {
            rw::gl3::setOutlineAlpha(allow);
            return;
        }
#endif
        (void)allow;
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

// How wide the strip is.
//
// 64 is enough for hard bands, where all a width has to do is put an edge where
// it was asked. A row with a SOFT edge needs more: at 64 a third of the strip is
// 21 texels, a soft edge spans a handful of them, and point sampling turns that
// handful into its own little staircase. 256 gives an edge four times the room
// and costs four kilobytes.
static const S32 kRampWidth = 256;

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

    // How much of each band is spent turning into the next, as a fraction of
    // the band's width. 0 is a step.
    //
    // **A hard band is right on a character and wrong on a level, and the
    // reason is polygon size.** A band edge lands wherever the light term
    // crosses a value, so on a character -- hundreds of small triangles across
    // a curve -- the edge falls where the curve says and reads as drawn ink.
    // A level is built from a few large flat pieces: one piece has one normal,
    // so the whole of it crosses the edge at the same moment and the entire
    // surface changes tone at once. Turn the sun and the level snaps a piece at
    // a time; hold it still and two pieces that differ by a degree sit in
    // different bands, which draws their shared edge as a tone boundary. That
    // is the level's own polygons showing through the shading.
    //
    // A soft edge fixes all of it without giving up the flat tones: the middle
    // of each band is still one colour, and only the crossing is graded.
    F32 softness;
};

static const ToonRampRow kRampRows[ITOON_RAMP_ROWS] = {
    // Characters. The tuning everything else is measured against.
    { { 0.34f, 0.40f, 0.62f }, { 1.0f, 0.97f, 0.88f }, 0.45f, 0, 0.0f },

    // Metal. Harder and cooler at both ends: a reflective surface in the show
    // is drawn as two flat tones with a bright edge rather than as a graded
    // curve, so it loses a step and gains contrast. The robots are what this
    // is for.
    { { 0.22f, 0.26f, 0.42f }, { 1.0f, 1.0f, 1.0f }, 0.50f, -1, 0.0f },

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
    // The bands stay as hard as a character's in COUNT and in COLOUR, and only
    // their crossings are graded -- see softness. A third of a band is enough
    // to take the polygons out of a wall and little enough that a level still
    // reads as two tones and a line.
    { { 0.28f, 0.36f, 0.64f }, { 1.0f, 0.97f, 0.86f }, 0.42f, 0, 0.34f },

    // Props built out of flat panels, which the wooden tikis are.
    //
    // **The character's look exactly, and the row is here to say what a model
    // is.** A tone that is right on a curve is right on a panel too: pulling the
    // top band down and softening the crossings was tried and it took the cel
    // shading off the tikis, which is the one thing they were being drawn this
    // way for.
    //
    // What a panel does need is less rim. A rim traces a silhouette by watching
    // the facing turn, and the facing does not turn across a flat panel, so the
    // shader hands a panelled prop a fraction of the rim spread over a wide
    // edge. It reads the row to know which models those are. See ToonRimAmount.
    { { 0.34f, 0.40f, 0.62f }, { 1.0f, 0.97f, 0.88f }, 0.45f, 0, 0.0f },
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

        // Which band, and how far into the crossing out of it. A hard row
        // takes the whole band at one value; a soft one grades the last of it
        // into the next, smoothly at both ends so the join itself is not a
        // corner.
        F32 scaled = t * (F32)bands;
        F32 index = (F32)((S32)scaled);
        F32 into = scaled - index;
        F32 step = index;

        if (row->softness > 0.0f)
        {
            F32 x = (into - (1.0f - row->softness)) / row->softness;

            if (x > 0.0f)
            {
                if (x > 1.0f) x = 1.0f;

                step += x * x * (3.0f - 2.0f * x);
            }
        }

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
//
// **How dark is a setting, because a multiply alone is not enough.** Scaling a
// surface down holds its saturation and takes brightness off all of it, so the
// line lands halfway to grey however dark it is set. iScreenToonInk says what
// the other two do about that.

// **A pass of flat art, where only what the game NAMED still takes the look.**
//
// A cel ramp and a hull are both statements about a solid surface: the ramp
// says which way it faces the light, the hull says where it ends. A floor
// decal, a plant card and a particle are none of those. They are art laid on
// the picture, and banding their light only darkens them, while a hull traces
// the rectangle they were cut from rather than the shape their texture leaves
// behind.
//
// **But see-through is not the test, because a jellyfish is translucent.** It
// draws in the same pass as the decals and it is a character, so a rule that
// reads the alpha would take the look off both. What separates them is that a
// character was named by zToonOutlineFor and a decal only ever picked up the
// experimental.toon_all default. So the pause suspends the DEFAULT and leaves
// an explicit entry standing.
//
// The shading follows the same answer per draw, in iToonSetOutline below --
// iModelRender calls it either side of a model, which is the only place that
// knows whether this particular one was named.
static S32 sPaused;

// Which atomic the next iToonSetOutline is about. Set by iModelRender, which is
// the only place that has both the atomic and the moment.
static void* sOutlineAtomic;

void iToonOutlineAtomic(void* atomic)
{
    sOutlineAtomic = atomic;
}

void iToonPause(S32 on)
{
    sPaused = on ? 1 : 0;

    toonbackend::setToonShading(sPaused ? FALSE : iScreenToon(), iScreenToonBands(),
                                iScreenToonSaturation(), iScreenToonStrength());

    if (sPaused)
    {
        toonbackend::setOutlineMode(ITOON_OUTLINE_NONE);
    }
}

void iToonSetOutline(S32 mode)
{
    // A named model keeps the ramp through the paused pass, and everything else
    // in it stays plain. Outside the pause the shading is already on and this
    // writes what is there.
    if (sPaused)
    {
        toonbackend::setToonShading(mode != ITOON_OUTLINE_NONE ? iScreenToon() : FALSE,
                                    iScreenToonBands(), iScreenToonSaturation(),
                                    iScreenToonStrength());

        if (mode == ITOON_OUTLINE_NONE)
        {
            toonbackend::setOutlineMode(ITOON_OUTLINE_NONE);
            return;
        }
    }

    // A named character may be see-through and still be inked; anything that
    // reached the ink through the toon_all default may not.
    toonbackend::setOutlineAlpha(mode != ITOON_OUTLINE_NONE && iToonOutlineNamed(sOutlineAtomic));

    F32 ink = iScreenToonInk();

    toonbackend::setOutline(ink, ink, ink, iScreenToonOutline());
    toonbackend::setOutlineInk(iScreenToonInkSaturation(), iScreenToonInkGamma());

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

// Whether each is wound inside out. Same walk, same reason.
static S32 sInsideOut[kWeldSlots];

// And how flat each one is, which decides whether it is a sheet that has to be
// given thickness before a hull can go round it. Same walk again.
static F32 sFlat[kWeldSlots];

// Where a model stops being shaped and starts being flat, by the measure
// Flatness uses. A sphere scores a half and a cube a third, so this is clear of
// anything with form to it; a plate scores 0.95 and up.
static const F32 kFlatLow = 0.80f;
static const F32 kFlatHigh = 0.94f;

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

static int CompareU64(const void* a, const void* b)
{
    U64 x = *(const U64*)a;
    U64 y = *(const U64*)b;

    return x < y ? -1 : (x > y ? 1 : 0);
}

// **Written somewhere of its own, because the surface still needs its own
// normals.** This used to overwrite them, and everything that reads a normal
// afterwards was reading the hull's: the cel bands lost the corners the drawing
// wants square, and the rim -- which is a statement about a silhouette -- was
// answering from a normal 45 degrees off the face it was drawn on. See
// HullNormalGeometry, which is where these end up.
static void AveragedNormals(RpGeometry* geo, RwV3d* out)
{
    S32 n = geo->numVertices;
    RwV3d* verts = geo->morphTarget[0].verts;
    RwV3d* norms = geo->morphTarget[0].normals;

    for (S32 i = 0; i < n; i++)
    {
        out[i] = norms[i];
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
                out[i].x += norms[j].x;
                out[i].y += norms[j].y;
                out[i].z += norms[j].z;
                out[j].x += norms[i].x;
                out[j].y += norms[i].y;
                out[j].z += norms[i].z;
            }
        }
    }

    for (S32 i = 0; i < n; i++)
    {
        F32 len2 = out[i].x * out[i].x + out[i].y * out[i].y + out[i].z * out[i].z;

        if (len2 > 1e-12f)
        {
            F32 inv = 1.0f / xsqrt(len2);

            out[i].x *= inv;
            out[i].y *= inv;
            out[i].z *= inv;
        }
        else
        {
            out[i] = norms[i];
        }
    }
}

// **The hull's normal, in two texture coordinate sets of its own.**
//
// A model stores a hard edge by duplicating the vertex, one copy per face, each
// carrying its own face's normal. Inflating along those sends the copies in
// different directions and the faces come apart -- a gap exactly where the
// outline is meant to be -- so the hull needs one normal per position. The
// average is that normal, and it is no use to anything else: it describes a
// crease rather than either of the faces meeting at it.
//
// So the geometry is rebuilt with the average alongside rather than on top,
// (x,y) in set 1 and (z,0) in set 2. Fixed indices, so a vertex shader can name
// them, which means padding a model that ships one set and refusing one that
// ships two -- nothing in this game does, and the alternative is a shader that
// has to be told where to look.
//
// Same vertices, same triangles, same order. That matters: game code holds
// per-vertex arrays sized to whatever geometry an atomic had when it looked,
// and a UV animation writing them back through the atomic must land on the same
// vertex it read.
enum
{
    kHullNormalSet = 1,
    kHullSets = 3
};

static RpGeometry* HullNormalGeometry(RpGeometry* old)
{
    S32 nv = old->numVertices;
    S32 nt = old->numTriangles;
    RwV3d* avg = (RwV3d*)RwMalloc(nv * sizeof(RwV3d));

    if (avg == NULL)
    {
        return NULL;
    }

    AveragedNormals(old, avg);

    U32 flags = old->flags & ~(U32)(rw::Geometry::TRISTRIP | rw::Geometry::NATIVE |
                                    rw::Geometry::NATIVEINSTANCE);

    // The set count rides in the flags, above the flags themselves, and it
    // wins over what TEXTURED would have said.
    flags |= rw::Geometry::TEXTURED | ((U32)kHullSets << 16);

    rw::Geometry* built = rw::Geometry::create(nv, nt, flags);

    if (built == NULL)
    {
        RwFree(avg);
        return NULL;
    }

    RpGeometry* geo = (RpGeometry*)built;

    memcpy(geo->morphTarget[0].verts, old->morphTarget[0].verts, nv * sizeof(RwV3d));

    if (geo->morphTarget[0].normals != NULL && old->morphTarget[0].normals != NULL)
    {
        memcpy(geo->morphTarget[0].normals, old->morphTarget[0].normals, nv * sizeof(RwV3d));
    }

    if (geo->preLitLum != NULL && old->preLitLum != NULL)
    {
        memcpy(geo->preLitLum, old->preLitLum, nv * sizeof(RwRGBA));
    }

    for (S32 i = 0; i < nv; i++)
    {
        if (old->numTexCoordSets > 0)
        {
            geo->texCoords[0][i] = old->texCoords[0][i];
        }
        else
        {
            geo->texCoords[0][i].u = 0.0f;
            geo->texCoords[0][i].v = 0.0f;
        }

        geo->texCoords[kHullNormalSet][i].u = avg[i].x;
        geo->texCoords[kHullNormalSet][i].v = avg[i].y;
        geo->texCoords[kHullNormalSet + 1][i].u = avg[i].z;
        geo->texCoords[kHullNormalSet + 1][i].v = 0.0f;
    }

    memcpy(geo->triangles, old->triangles, nt * sizeof(RpTriangle));

    for (S32 m = 0; m < old->matList.numMaterials; m++)
    {
        ((rw::Geometry*)geo)->matList.appendMaterial((rw::Material*)old->matList.materials[m]);
    }

    // The skin, vertex for vertex, since neither the count nor the order moved.
    rw::Skin* oldSkin = rw::Skin::get((rw::Geometry*)old);

    if (oldSkin != NULL)
    {
        rw::Skin* skin = rwNewT(rw::Skin, 1, rw::MEMDUR_EVENT | rw::ID_SKIN);

        memset(skin, 0, sizeof(*skin));
        skin->init(oldSkin->numBones, oldSkin->numBones, nv);

        if (oldSkin->numBones != 0)
        {
            memcpy(skin->inverseMatrices, oldSkin->inverseMatrices, oldSkin->numBones * 64);
        }

        memcpy(skin->indices, oldSkin->indices, nv * 4);
        memcpy(skin->weights, oldSkin->weights, nv * 16);
        skin->findNumWeights(nv);
        skin->findUsedBones(nv);
        rw::Skin::set((rw::Geometry*)geo, skin);
    }

    ((rw::Geometry*)geo)->calculateBoundingSphere();
    ((rw::Geometry*)geo)->buildMeshes();
    RwFree(avg);

    return geo;
}

// A material with a reflection on it is metal. Nothing else in these models
// distinguishes a robot from a fish without the game naming it, and the game has
// no field that answers -- baseType says NPC for both.
static S32 HasReflection(RpGeometry* geo)
{
    for (S32 i = 0; i < geo->matList.numMaterials; i++)
    {
        RpMaterial* mat = geo->matList.materials[i];

        if (mat != NULL && RpMatFXMaterialGetEffects(mat) != rpMATFXEFFECTNULL)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static RpAtomic* ReflectVoteCB(RpAtomic* atomic, void* data)
{
    RpGeometry* geo = RpAtomicGetGeometry(atomic);

    if (geo != NULL && HasReflection(geo))
    {
        *(S32*)data = TRUE;
    }

    return atomic;
}

// **Asked of the whole model and not of the piece.** A golden spatula is two
// pieces and the reflection is on one of them; a fodder robot's is on all of it
// and the tar-tar robot has none at all. Deciding piece by piece smoothed half
// of a spatula and left the other half faceted, which is one object wearing two
// treatments.
static S32 ModelHasReflection(RpAtomic* atomic)
{
    RpClump* clump = atomic != NULL ? RpAtomicGetClump(atomic) : NULL;
    S32 any = FALSE;

    if (clump == NULL)
    {
        RpGeometry* geo = atomic != NULL ? RpAtomicGetGeometry(atomic) : NULL;

        return geo != NULL && HasReflection(geo);
    }

    RpClumpForAllAtomics(clump, ReflectVoteCB, &any);

    return any;
}

// The average written over the model's own, which is what everything did before
// the two were separated.
//
// **The shiny objects want it that way.** A reflection is the one thing that
// reads better with the corners rounded off: it slides across a smoothed surface
// and breaks into facets on a hard one, and these are small objects made of few
// faces, so the facets are most of what you see. The models with a reflection
// are the ones that measured as metal for the ramp, and that is the same
// question asked twice.
static void WeldInPlace(RpGeometry* geo)
{
    S32 n = geo->numVertices;
    RwV3d* avg = (RwV3d*)RwMalloc(n * sizeof(RwV3d));

    if (avg == NULL)
    {
        return;
    }

    AveragedNormals(geo, avg);

    // 0x4 is librw Geometry::LOCKNORMALS. rpworld.h declares the call and not
    // the flags, so the value is spelled out rather than named.
    RpGeometryLock(geo, 0x4);
    memcpy(geo->morphTarget[0].normals, avg, n * sizeof(RwV3d));
    RpGeometryUnlock(geo);
    RwFree(avg);
}

void iToonTextColor(U8* r, U8* g, U8* b)
{
    if (!iScreenToon())
    {
        return;
    }

    F32 bright = iScreenToonTextBrightness();
    F32 sat = iScreenToonTextSaturation();

    if (bright == 1.0f && sat == 1.0f)
    {
        return;
    }

    F32 c[3];
    S32 i;

    c[0] = *r * (1.0f / 255.0f);
    c[1] = *g * (1.0f / 255.0f);
    c[2] = *b * (1.0f / 255.0f);

    // The same weights the ink uses, so a colour that survives one survives the
    // other.
    F32 grey = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];

    for (i = 0; i < 3; i++)
    {
        F32 v = (grey + (c[i] - grey) * sat) * bright;

        c[i] = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }

    *r = (U8)(c[0] * 255.0f + 0.5f);
    *g = (U8)(c[1] * 255.0f + 0.5f);
    *b = (U8)(c[2] * 255.0f + 0.5f);
}

void iToonHullNormals(void* atomic)
{
    RpAtomic* a = (RpAtomic*)atomic;

    if (a == NULL)
    {
        return;
    }

    RpGeometry* geo = RpAtomicGetGeometry(a);

    if (geo == NULL || geo->numVertices <= 0 || geo->numTriangles <= 0 ||
        geo->morphTarget == NULL || geo->morphTarget[0].verts == NULL ||
        geo->morphTarget[0].normals == NULL)
    {
        return;
    }

    // Already carrying them, or carrying a second set of its own that the fixed
    // indices have no room beside.
    if (geo->numTexCoordSets >= kHullSets || geo->numTexCoordSets > 1)
    {
        return;
    }

    // A reflective model keeps the two normals as one. FillSlot does that walk;
    // this one would only get in its way.
    if (ModelHasReflection(a))
    {
        return;
    }

    RpGeometry* built = HullNormalGeometry(geo);

    if (built == NULL)
    {
        return;
    }

    // A reference of ours on the old one, so a pointer somebody took before now
    // still reads. iHipoly.cpp keeps one for the same reason.
    ((rw::Geometry*)geo)->addRef();
    RpAtomicSetGeometry(a, built, 0);
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

// Whether a geometry is wound inside out.
//
// **Four of the seven gate digits are, and only the hull can tell.** A plate
// looks the same from either side, so number_1 and number_5 shipped with every
// face pointing INTO the plate and nobody saw: with back faces culled the game
// draws the far sheet instead of the near one, which is still a digit. The
// hull sees it at once. It inflates along the normals, which point inward, so
// the copy shrinks; and it keeps the faces that point away from the camera,
// which is now the NEAR sheet. That is a sheet of ink in front of the digit,
// with the digit's own colour showing round it where the copy shrank -- the
// hull turned inside out, because the model is.
//
// The test is the signed volume: positive when the winding points out of the
// mesh, negative when it points in. It means nothing on a mesh with an open
// edge -- a sign, a plant card and a bucket all report negative and none of
// them is inside out -- so anything not closed is left as it is.
//
// A sky dome is closed and negative on purpose: it is seen from inside. Drawn
// the inside-out way its hull lands behind it instead of being culled away,
// which is the same picture.
static S32 InsideOut(RpGeometry* geo)
{
    S32 nv = geo->numVertices;
    S32 nt = geo->numTriangles;
    const RwV3d* v = geo->morphTarget[0].verts;
    const RpTriangle* tri = geo->triangles;

    if (nv <= 0 || nt < 4 || v == NULL || tri == NULL)
    {
        return FALSE;
    }

    // A hard edge is a duplicated vertex, so an edge's two faces name it by
    // different indices. Positions are what pair them. Quadratic, once, like
    // the weld.
    S32* canon = (S32*)RwMalloc(nv * sizeof(S32));
    U64* edge = (U64*)RwMalloc(nt * 3 * sizeof(U64));

    if (canon == NULL || edge == NULL)
    {
        RwFree(canon);
        RwFree(edge);
        return FALSE;
    }

    for (S32 i = 0; i < nv; i++)
    {
        canon[i] = i;

        for (S32 j = 0; j < i; j++)
        {
            if (SamePoint(&v[i], &v[j]))
            {
                canon[i] = canon[j];
                break;
            }
        }
    }

    // Closed means every directed edge has its reverse on a neighbour.
    S32 ne = 0;
    F64 volume = 0.0;

    for (S32 t = 0; t < nt; t++)
    {
        const RwV3d* a = &v[tri[t].vertIndex[0]];
        const RwV3d* b = &v[tri[t].vertIndex[1]];
        const RwV3d* c = &v[tri[t].vertIndex[2]];

        volume += (F64)a->x * ((F64)b->y * c->z - (F64)b->z * c->y) +
                  (F64)a->y * ((F64)b->z * c->x - (F64)b->x * c->z) +
                  (F64)a->z * ((F64)b->x * c->y - (F64)b->y * c->x);

        for (S32 k = 0; k < 3; k++)
        {
            U64 p = (U64)(U32)canon[tri[t].vertIndex[k]];
            U64 q = (U64)(U32)canon[tri[t].vertIndex[(k + 1) % 3]];

            if (p != q)
            {
                edge[ne++] = (p << 32) | q;
            }
        }
    }

    qsort(edge, ne, sizeof(U64), CompareU64);

    S32 closed = TRUE;

    for (S32 i = 0; i < ne && closed; i++)
    {
        U64 rev = (edge[i] << 32) | (edge[i] >> 32);

        closed = bsearch(&rev, edge, ne, sizeof(U64), CompareU64) != NULL;
    }

    RwFree(canon);
    RwFree(edge);

    return closed && volume < 0.0;
}

// **How much of this model's hull must come from its middle rather than its own
// normals, because it is too flat to widen along them.**
//
// A hull is only ever seen where it reaches PAST the silhouette, and pushing a
// vertex along its normal widens the silhouette by whatever part of that normal
// lies across the view. A flat model has almost none. A shiny object is a star
// cut from a plate: 0.95 of every normal points front or back, and inflating it
// by a twentieth of a unit moved its outline by a two-hundredth. Ink a tenth of
// the width asked for reads as no ink at all.
//
// The measure is the largest of the three mean absolute normal components: 1.0
// for a plate, a half for a sphere, a third for a cube. A character scores
// nothing here and is inked exactly as before.
static F32 Flatness(RpGeometry* geo)
{
    RwV3d* norms = geo->morphTarget[0].normals;
    S32 n = geo->numVertices;

    if (norms == NULL || n <= 0)
    {
        return 0.0f;
    }

    F32 sum[3] = { 0.0f, 0.0f, 0.0f };

    for (S32 i = 0; i < n; i++)
    {
        F32 len2 = norms[i].x * norms[i].x + norms[i].y * norms[i].y + norms[i].z * norms[i].z;

        if (len2 < 1e-12f)
        {
            continue;
        }

        F32 inv = 1.0f / xsqrt(len2);
        F32 c[3] = { norms[i].x * inv, norms[i].y * inv, norms[i].z * inv };

        for (S32 a = 0; a < 3; a++)
        {
            sum[a] += c[a] < 0.0f ? -c[a] : c[a];
        }
    }

    F32 best = sum[0];

    if (sum[1] > best)
    {
        best = sum[1];
    }
    if (sum[2] > best)
    {
        best = sum[2];
    }

    best /= (F32)n;

    if (best <= kFlatLow)
    {
        return 0.0f;
    }

    if (best >= kFlatHigh)
    {
        return 1.0f;
    }

    return (best - kFlatLow) / (kFlatHigh - kFlatLow);
}

// Everything a fresh slot holds. Both readers below can be the first to see a
// geometry, so both fill it the same way.
// **Ink follows a character's shape, not the scraps laid on his face.**
//
// SpongeBob's main atomic is nine separate islands of geometry in ONE material:
// a body of 1295 vertices, four of about 250 that are his eyes and his shoes,
// and four of 74 and 116 that are the details on his face. A hull round a detail
// draws a box round the detail, because that is what a hull does -- it traces the
// shape a mesh is cut from, and an eyelash laid on a cheek is its own little
// shape floating in front of one.
//
// **The rule for this already exists and could never fire.** The renderer refuses
// a mesh under a twentieth of its model, for exactly this reason and in those
// words. But a mesh IS a material, and the artists put a whole character in one,
// so the rule never had anything to act on. This gives it some: each island under
// the same twentieth is moved to a material of its own, one apiece so each lands
// under the rule by itself, and the renderer does the rest.
//
// The line between a detail and a part is a real gap and not a guess. On
// SpongeBob the small four are 2.7% and 4.3% of him and the next one up is 9.1%;
// on Patrick the body is 1886 vertices and the five details run 32 to 69.
//
// **Named characters only.** A bamboo wall is 25 poles of 74 vertices and every
// one of them is under a twentieth of the wall. Scenery is not details laid on a
// surface, and inking it whole is right.
enum
{
    kScrapDenominator = 20
};

// How much of a model its biggest island has to hold before the rest of it can
// be read as details laid on it. The models this was written for run 48% and up
// and the assemblies it must leave alone reach 29%.
static const F32 kScrapBody = 0.40f;

static S32 ScrapRoot(S32* root, S32 i)
{
    while (root[i] != i)
    {
        i = root[i];
    }

    return i;
}

static void ScrapJoin(S32* root, S32 a, S32 b)
{
    S32 ra = ScrapRoot(root, a);
    S32 rb = ScrapRoot(root, b);

    if (ra != rb)
    {
        root[ra] = rb;
    }
}

static void SplitScraps(RpGeometry* geo)
{
    S32 nv = geo->numVertices;
    S32 nt = geo->numTriangles;
    RwV3d* pos = geo->morphTarget != NULL ? geo->morphTarget[0].verts : NULL;

    if (nv <= 0 || nt <= 0 || pos == NULL || geo->matList.numMaterials <= 0)
    {
        return;
    }

    S32* root = (S32*)RwMalloc(nv * sizeof(S32));
    S32* size = (S32*)RwMalloc(nv * sizeof(S32));
    S32* moved = (S32*)RwMalloc(nv * sizeof(S32));

    if (root == NULL || size == NULL || moved == NULL)
    {
        RwFree(root);
        RwFree(size);
        RwFree(moved);
        return;
    }

    for (S32 i = 0; i < nv; i++)
    {
        root[i] = i;
        size[i] = 0;
        moved[i] = -1;
    }

    // One point, however many vertices sit on it. Quadratic, once, like the weld
    // beside it.
    for (S32 i = 0; i < nv; i++)
    {
        for (S32 j = 0; j < i; j++)
        {
            if (SamePoint(&pos[i], &pos[j]))
            {
                ScrapJoin(root, i, j);
                break;
            }
        }
    }

    for (S32 t = 0; t < nt; t++)
    {
        ScrapJoin(root, geo->triangles[t].vertIndex[0], geo->triangles[t].vertIndex[1]);
        ScrapJoin(root, geo->triangles[t].vertIndex[1], geo->triangles[t].vertIndex[2]);
    }

    for (S32 i = 0; i < nv; i++)
    {
        size[ScrapRoot(root, i)]++;
    }

    // **A body with details on it, and not an assembly of parts.**
    //
    // The rule below calls anything under a twentieth of the model a detail, and
    // that is only true of a model that has a body for the details to sit on.
    // A robot has none: measured in jf01, the fodder robot's biggest island is
    // 19% of it and the hammer robot's 29%, both of them a pile of similar
    // pieces, so the rule took 7 and 12 of their parts for scraps and the ink
    // came off a robot's arms and bolts. The models it was written for all have
    // one: SpongeBob 48%, Squidward 68%, a fish 85%.
    S32 biggest = 0;

    for (S32 i = 0; i < nv; i++)
    {
        if (ScrapRoot(root, i) == i && size[i] > biggest)
        {
            biggest = size[i];
        }
    }

    if ((F32)biggest < kScrapBody * (F32)nv)
    {
        RwFree(root);
        RwFree(size);
        RwFree(moved);
        return;
    }

    S32 limit = nv / kScrapDenominator;
    S32 anySmall = FALSE;
    S32 anyBig = FALSE;

    for (S32 i = 0; i < nv; i++)
    {
        if (ScrapRoot(root, i) != i)
        {
            continue;
        }

        if (size[i] < limit)
        {
            anySmall = TRUE;
        }
        else
        {
            anyBig = TRUE;
        }
    }

    // A model that is all details, or has none, is one the renderer already
    // handles correctly.
    if (!anySmall || !anyBig)
    {
        RwFree(root);
        RwFree(size);
        RwFree(moved);
        return;
    }

    S32 split = 0;

    for (S32 t = 0; t < nt; t++)
    {
        S32 r = ScrapRoot(root, geo->triangles[t].vertIndex[0]);

        if (size[r] >= limit)
        {
            continue;
        }

        if (moved[r] < 0)
        {
            S32 was = geo->triangles[t].matIndex;

            if (was < 0 || was >= geo->matList.numMaterials)
            {
                continue;
            }

            rw::Material* copy = ((rw::Material*)geo->matList.materials[was])->clone();

            if (copy == NULL)
            {
                continue;
            }

            S32 at = ((rw::Geometry*)geo)->matList.appendMaterial(copy);

            // The list holds a reference of its own now.
            copy->destroy();

            if (at < 0)
            {
                continue;
            }

            moved[r] = at;
            split++;
        }

        geo->triangles[t].matIndex = (RwInt16)moved[r];
    }

    RwFree(root);
    RwFree(size);
    RwFree(moved);

    if (split == 0)
    {
        return;
    }

    // 0x1 is librw's LOCKPOLYGONS, which drops the mesh header so the unlock
    // builds it again -- one mesh per material, which is the whole point.
    RpGeometryLock(geo, 0x1);
    RpGeometryUnlock(geo);
}

static void FillSlot(RpAtomic* atomic, RpGeometry* geo, S32 slot)
{
    sInsideOut[slot] = InsideOut(geo);
    sFlat[slot] = Flatness(geo);

    // Before anything reads the mesh header: the split rebuilds it.
    if (iToonOutlineNamed(atomic))
    {
        SplitScraps(geo);
    }

    // The one kind of model that still wants its normals averaged in place.
    // iToonHullNormals leaves these alone so this is the only walk they get.
    if (ModelHasReflection(atomic))
    {
        WeldInPlace(geo);
    }

    sSplitY[slot] = SplitHeight(geo);
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
        FillSlot(a, geo, slot);
    }

    return sSplitY[slot];
}

S32 iToonInsideOut(void* atomic)
{
    RpAtomic* a = (RpAtomic*)atomic;

    if (a == NULL)
    {
        return FALSE;
    }

    RpGeometry* geo = RpAtomicGetGeometry(a);

    if (geo == NULL || geo->numVertices <= 0 || geo->morphTarget == NULL ||
        geo->morphTarget[0].verts == NULL)
    {
        return FALSE;
    }

    S32 fresh = FALSE;
    S32 slot = WeldSlot(geo, &fresh);

    if (slot < 0)
    {
        return FALSE;
    }

    if (fresh)
    {
        FillSlot(a, geo, slot);
    }

    return sInsideOut[slot];
}

enum
{
    kInsideSlots = 16
};

// Geometries a caller says are meant to be inward facing. Only the sky domes,
// and only two or three of those in a level.
static RpGeometry* sSeenFromInside[kInsideSlots];
static S32 sSeenCount;

void iToonSeenFromInside(void* atomic)
{
    RpAtomic* a = (RpAtomic*)atomic;
    RpGeometry* geo = a != NULL ? RpAtomicGetGeometry(a) : NULL;

    if (geo == NULL)
    {
        return;
    }

    for (S32 i = 0; i < sSeenCount; i++)
    {
        if (sSeenFromInside[i] == geo)
        {
            return;
        }
    }

    if (sSeenCount < kInsideSlots)
    {
        sSeenFromInside[sSeenCount++] = geo;
    }
}

static S32 MeantToBeInward(RpGeometry* geo)
{
    for (S32 i = 0; i < sSeenCount; i++)
    {
        if (sSeenFromInside[i] == geo)
        {
            return TRUE;
        }
    }

    return FALSE;
}

// Wind a geometry the other way, which is the way it should have been.
//
// Swapping two corners of every triangle turns the mesh's faces outward, and
// negating the normals turns its shading outward with them; a mesh needs both
// or it is inside out in the other half. LOCKPOLYGONS drops the mesh header so
// the unlock rebuilds it, which changes its serial and makes the renderer
// instance the whole thing again -- the index buffer included, which no other
// lock reaches.
static void ReverseGeometry(RpGeometry* geo)
{
    RwV3d* norms = geo->morphTarget[0].normals;

    // 0x1 is librw Geometry::LOCKPOLYGONS and 0x4 LOCKNORMALS. rpworld.h
    // declares the call and not the flags, so the values are spelled out.
    RpGeometryLock(geo, 0x1 | 0x4);

    for (S32 t = 0; t < geo->numTriangles; t++)
    {
        RwUInt16 swap = geo->triangles[t].vertIndex[1];

        geo->triangles[t].vertIndex[1] = geo->triangles[t].vertIndex[2];
        geo->triangles[t].vertIndex[2] = swap;
    }

    if (norms != NULL)
    {
        for (S32 i = 0; i < geo->numVertices; i++)
        {
            norms[i].x = -norms[i].x;
            norms[i].y = -norms[i].y;
            norms[i].z = -norms[i].z;
        }
    }

    RpGeometryUnlock(geo);
}

// **Which way round this model's hull goes, and whether the model itself is the
// thing to correct.**
//
// A mesh wound inside out is wrong for every pass, not only the hull. The
// game's own draw culls back faces, and on such a mesh the faces pointing at
// the camera ARE the back ones, so it throws away the near side and draws the
// far side: a solid digit that reads as a hollow shell you can see into. Only
// the hull was fixed by drawing it the other way round, so the mesh is put
// right instead, once, and every pass is correct after it.
//
// Except where inward facing is what the artist meant. A sky dome is seen from
// inside, and the game says which models those are; theirs is the case the hull
// flag still serves, and it puts their hull behind them where the dome covers
// it.
// **Give a flat sheet real thickness, because an inverted hull cannot work on
// one.**
//
// A shiny object is 47 vertices and 45 triangles with 47 open boundary edges: a
// single open sheet, no back and no rim. The hull is that sheet inflated along
// its own normals with front faces culled, and every one of those normals points
// out of the front, so nothing lies behind the model to survive the cull and
// nothing points sideways to widen its silhouette. No ink width fixes that. It
// is the mesh.
//
// So the mesh is made solid: the sheet, a copy of it pushed out the back, and a
// rim of quads joining the two boundaries. The rim's own vertices carry the
// outward normal, which is the one the hull needs and the one the sheet never
// had. The weld that follows averages those against the sheet's, rounding the
// edge instead of creasing it.
//
// Once per geometry, and the result replaces the old one on the atomic, so every
// pickup sharing that asset gets it.
static RwV3d Normalized(const RwV3d* v)
{
    RwV3d out = { 0.0f, 0.0f, 0.0f };
    F32 len2 = v->x * v->x + v->y * v->y + v->z * v->z;

    if (len2 > 1e-20f)
    {
        F32 inv = 1.0f / xsqrt(len2);

        out.x = v->x * inv;
        out.y = v->y * inv;
        out.z = v->z * inv;
    }

    return out;
}

// How thick a solidified sheet becomes, against its own longest side. Enough to
// read as a solid from any angle without turning a coin into a block.
static const F32 kSolidDepth = 0.12f;

static RpGeometry* Solidified(RpGeometry* old)
{
    S32 nv = old->numVertices;
    S32 nt = old->numTriangles;
    RwV3d* pos = old->morphTarget[0].verts;
    RwV3d* nrm = old->morphTarget[0].normals;

    if (nv <= 0 || nt <= 0 || pos == NULL || nrm == NULL || nv * 2 + nt * 12 > 60000)
    {
        return NULL;
    }

    // The boundary: a directed edge whose opposite no triangle owns. Positions
    // pair them, because a hard edge is stored as a duplicated vertex.
    S32* canon = (S32*)RwMalloc(nv * sizeof(S32));
    U64* edge = (U64*)RwMalloc(nt * 3 * sizeof(U64));
    S32* rimA = (S32*)RwMalloc(nt * 3 * sizeof(S32));
    S32* rimB = (S32*)RwMalloc(nt * 3 * sizeof(S32));
    S32* rimT = (S32*)RwMalloc(nt * 3 * sizeof(S32));

    if (canon == NULL || edge == NULL || rimA == NULL || rimB == NULL || rimT == NULL)
    {
        RwFree(canon);
        RwFree(edge);
        RwFree(rimA);
        RwFree(rimB);
        RwFree(rimT);
        return NULL;
    }

    for (S32 i = 0; i < nv; i++)
    {
        canon[i] = i;

        for (S32 j = 0; j < i; j++)
        {
            if (SamePoint(&pos[i], &pos[j]))
            {
                canon[i] = canon[j];
                break;
            }
        }
    }

    S32 ne = 0;

    for (S32 t = 0; t < nt; t++)
    {
        for (S32 e = 0; e < 3; e++)
        {
            U64 p = (U64)(U32)canon[old->triangles[t].vertIndex[e]];
            U64 q = (U64)(U32)canon[old->triangles[t].vertIndex[(e + 1) % 3]];

            if (p != q)
            {
                edge[ne++] = (p << 32) | q;
            }
        }
    }

    qsort(edge, ne, sizeof(U64), CompareU64);

    S32 nrim = 0;

    for (S32 t = 0; t < nt; t++)
    {
        for (S32 e = 0; e < 3; e++)
        {
            S32 a = old->triangles[t].vertIndex[e];
            S32 b = old->triangles[t].vertIndex[(e + 1) % 3];
            U64 rev = ((U64)(U32)canon[b] << 32) | (U64)(U32)canon[a];

            if (canon[a] != canon[b] && bsearch(&rev, edge, ne, sizeof(U64), CompareU64) == NULL)
            {
                rimA[nrim] = a;
                rimB[nrim] = b;
                rimT[nrim] = t;
                nrim++;
            }
        }
    }

    RwFree(canon);
    RwFree(edge);

    if (nrim == 0)
    {
        RwFree(rimA);
        RwFree(rimB);
        RwFree(rimT);
        return NULL;
    }

    // The middle, so a rim quad can be turned to face away from it, and the
    // longest side, which sets how thick the solid becomes.
    RwV3d mid = { 0.0f, 0.0f, 0.0f };

    for (S32 i = 0; i < nv; i++)
    {
        mid.x += pos[i].x;
        mid.y += pos[i].y;
        mid.z += pos[i].z;
    }

    mid.x /= (F32)nv;
    mid.y /= (F32)nv;
    mid.z /= (F32)nv;

    F32 lo[3] = { pos[0].x, pos[0].y, pos[0].z };
    F32 hi[3] = { pos[0].x, pos[0].y, pos[0].z };

    for (S32 i = 1; i < nv; i++)
    {
        F32 c[3] = { pos[i].x, pos[i].y, pos[i].z };

        for (S32 k = 0; k < 3; k++)
        {
            if (c[k] < lo[k])
            {
                lo[k] = c[k];
            }
            if (c[k] > hi[k])
            {
                hi[k] = c[k];
            }
        }
    }

    F32 longest = hi[0] - lo[0];

    if (hi[1] - lo[1] > longest)
    {
        longest = hi[1] - lo[1];
    }
    if (hi[2] - lo[2] > longest)
    {
        longest = hi[2] - lo[2];
    }

    F32 depth = kSolidDepth * longest;

    if (depth <= 0.0f)
    {
        RwFree(rimA);
        RwFree(rimB);
        RwFree(rimT);
        return NULL;
    }

    // The sheet, the sheet again out the back, and four rim vertices per
    // boundary edge so the rim can carry a normal of its own.
    S32 outV = nv * 2 + nrim * 4;
    S32 outT = nt * 2 + nrim * 2;
    U32 flags = old->flags & ~(U32)(rw::Geometry::TRISTRIP | rw::Geometry::NATIVE |
                                    rw::Geometry::NATIVEINSTANCE);
    rw::Geometry* built = rw::Geometry::create(outV, outT, flags);

    if (built == NULL)
    {
        RwFree(rimA);
        RwFree(rimB);
        RwFree(rimT);
        return NULL;
    }

    RpGeometry* geo = (RpGeometry*)built;
    RwV3d* dpos = geo->morphTarget[0].verts;
    RwV3d* dnrm = geo->morphTarget[0].normals;

    for (S32 i = 0; i < nv; i++)
    {
        RwV3d unit = Normalized(&nrm[i]);

        dpos[i] = pos[i];
        dpos[nv + i].x = pos[i].x - unit.x * depth;
        dpos[nv + i].y = pos[i].y - unit.y * depth;
        dpos[nv + i].z = pos[i].z - unit.z * depth;

        if (dnrm != NULL)
        {
            dnrm[i] = unit;
            dnrm[nv + i].x = -unit.x;
            dnrm[nv + i].y = -unit.y;
            dnrm[nv + i].z = -unit.z;
        }

        for (S32 s = 0; s < old->numTexCoordSets && s < geo->numTexCoordSets; s++)
        {
            geo->texCoords[s][i] = old->texCoords[s][i];
            geo->texCoords[s][nv + i] = old->texCoords[s][i];
        }

        if (geo->preLitLum != NULL && old->preLitLum != NULL)
        {
            geo->preLitLum[i] = old->preLitLum[i];
            geo->preLitLum[nv + i] = old->preLitLum[i];
        }
    }

    for (S32 t = 0; t < nt; t++)
    {
        geo->triangles[t] = old->triangles[t];

        geo->triangles[nt + t].vertIndex[0] = (RwUInt16)(old->triangles[t].vertIndex[0] + nv);
        geo->triangles[nt + t].vertIndex[1] = (RwUInt16)(old->triangles[t].vertIndex[2] + nv);
        geo->triangles[nt + t].vertIndex[2] = (RwUInt16)(old->triangles[t].vertIndex[1] + nv);
        geo->triangles[nt + t].matIndex = old->triangles[t].matIndex;
    }

    S32 v = nv * 2;
    S32 tri = nt * 2;

    for (S32 r = 0; r < nrim; r++)
    {
        S32 a = rimA[r];
        S32 b = rimB[r];
        RwV3d face = Normalized(&nrm[a]);
        RwV3d along;

        along.x = pos[b].x - pos[a].x;
        along.y = pos[b].y - pos[a].y;
        along.z = pos[b].z - pos[a].z;

        // Across the edge and along the surface, which is the way out.
        RwV3d out;

        out.x = along.y * face.z - along.z * face.y;
        out.y = along.z * face.x - along.x * face.z;
        out.z = along.x * face.y - along.y * face.x;
        out = Normalized(&out);

        F32 away = (0.5f * (pos[a].x + pos[b].x) - mid.x) * out.x +
                   (0.5f * (pos[a].y + pos[b].y) - mid.y) * out.y +
                   (0.5f * (pos[a].z + pos[b].z) - mid.z) * out.z;

        if (away < 0.0f)
        {
            out.x = -out.x;
            out.y = -out.y;
            out.z = -out.z;
        }

        S32 af = v;
        S32 bf = v + 1;
        S32 ab = v + 2;
        S32 bb = v + 3;

        dpos[af] = dpos[a];
        dpos[bf] = dpos[b];
        dpos[ab] = dpos[nv + a];
        dpos[bb] = dpos[nv + b];

        if (dnrm != NULL)
        {
            dnrm[af] = out;
            dnrm[bf] = out;
            dnrm[ab] = out;
            dnrm[bb] = out;
        }

        for (S32 s = 0; s < old->numTexCoordSets && s < geo->numTexCoordSets; s++)
        {
            geo->texCoords[s][af] = old->texCoords[s][a];
            geo->texCoords[s][bf] = old->texCoords[s][b];
            geo->texCoords[s][ab] = old->texCoords[s][a];
            geo->texCoords[s][bb] = old->texCoords[s][b];
        }

        if (geo->preLitLum != NULL && old->preLitLum != NULL)
        {
            geo->preLitLum[af] = old->preLitLum[a];
            geo->preLitLum[bf] = old->preLitLum[b];
            geo->preLitLum[ab] = old->preLitLum[a];
            geo->preLitLum[bb] = old->preLitLum[b];
        }

        // Wound to agree with the outward normal, whichever way round the
        // boundary happens to run.
        RwV3d e1;
        RwV3d e2;

        e1.x = dpos[bf].x - dpos[af].x;
        e1.y = dpos[bf].y - dpos[af].y;
        e1.z = dpos[bf].z - dpos[af].z;
        e2.x = dpos[ab].x - dpos[af].x;
        e2.y = dpos[ab].y - dpos[af].y;
        e2.z = dpos[ab].z - dpos[af].z;

        F32 wound = (e1.y * e2.z - e1.z * e2.y) * out.x + (e1.z * e2.x - e1.x * e2.z) * out.y +
                    (e1.x * e2.y - e1.y * e2.x) * out.z;

        if (wound >= 0.0f)
        {
            geo->triangles[tri].vertIndex[0] = (RwUInt16)af;
            geo->triangles[tri].vertIndex[1] = (RwUInt16)bf;
            geo->triangles[tri].vertIndex[2] = (RwUInt16)ab;
            geo->triangles[tri].matIndex = old->triangles[rimT[r]].matIndex;
            tri++;

            geo->triangles[tri].vertIndex[0] = (RwUInt16)bf;
            geo->triangles[tri].vertIndex[1] = (RwUInt16)bb;
            geo->triangles[tri].vertIndex[2] = (RwUInt16)ab;
        }
        else
        {
            geo->triangles[tri].vertIndex[0] = (RwUInt16)af;
            geo->triangles[tri].vertIndex[1] = (RwUInt16)ab;
            geo->triangles[tri].vertIndex[2] = (RwUInt16)bf;
            geo->triangles[tri].matIndex = old->triangles[rimT[r]].matIndex;
            tri++;

            geo->triangles[tri].vertIndex[0] = (RwUInt16)bf;
            geo->triangles[tri].vertIndex[1] = (RwUInt16)ab;
            geo->triangles[tri].vertIndex[2] = (RwUInt16)bb;
        }

        geo->triangles[tri].matIndex = old->triangles[rimT[r]].matIndex;
        tri++;

        v += 4;
    }

    RwFree(rimA);
    RwFree(rimB);
    RwFree(rimT);

    for (S32 m = 0; m < old->matList.numMaterials; m++)
    {
        built->matList.appendMaterial((rw::Material*)old->matList.materials[m]);
    }

    built->calculateBoundingSphere();
    built->buildMeshes();

    return geo;
}

void iToonSolidify(void* atomic)
{
    RpAtomic* a = (RpAtomic*)atomic;

    if (a == NULL || !iScreenSolidFlatProps())
    {
        return;
    }

    RpGeometry* geo = RpAtomicGetGeometry(a);

    if (geo == NULL || geo->numVertices <= 0 || geo->morphTarget == NULL ||
        geo->morphTarget[0].verts == NULL || geo->morphTarget[0].normals == NULL)
    {
        return;
    }

    S32 fresh = FALSE;
    S32 slot = WeldSlot(geo, &fresh);

    if (slot < 0)
    {
        return;
    }

    if (fresh)
    {
        FillSlot(a, geo, slot);
    }

    // Only a sheet: flat enough that its own normals cannot widen it. A closed
    // model is already a solid and Solidified finds no boundary to build on; a
    // shaped one inks itself.
    if (sFlat[slot] <= 0.0f)
    {
        return;
    }

    RpGeometry* solid = Solidified(geo);

    if (solid != NULL)
    {
        RpAtomicSetGeometry(a, solid, 0);

        // And smoothed, if the level's models are. This mesh was built after
        // iHipolyModel ran over the asset, so it has to ask for itself; the rim
        // is the part that most wants it, because smoothing is what rounds the
        // edge the sheet never had.
        iHipolyAtomic(a);
    }
}

void iToonOutlineOrient(void* atomic)
{
    if (atomic == NULL || !iToonInsideOut(atomic))
    {
        toonbackend::setOutlineInverted(FALSE);
        return;
    }

    RpGeometry* geo = RpAtomicGetGeometry((RpAtomic*)atomic);

    if (MeantToBeInward(geo))
    {
        toonbackend::setOutlineInverted(TRUE);
        return;
    }

    S32 fresh = FALSE;
    S32 slot = WeldSlot(geo, &fresh);

    if (slot >= 0)
    {
        ReverseGeometry(geo);
        sInsideOut[slot] = FALSE;
    }

    toonbackend::setOutlineInverted(FALSE);
}

// How much of the traced model shade this draw takes. The world takes it and the
// things standing in the world do not: a house does not shade itself with the
// answer traced for the ground.
void iToonSetModelShade(F32 amount)
{
    toonbackend::setToonModelShade(amount);
}

// How bright the room this draw is in. The level's own draw sets it; a character
// standing in the level is lit by his own kit and keeps the room as a colour,
// which is what iToonSetRoomTint is for.
void iToonSetRoomLevel(F32 scale)
{
    toonbackend::setToonRoomScale(scale);
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

// Whether a model is built out of flat panels.
//
// Face directions are collected within ten degrees of each other and weighted by
// area, and a model with one direction holding an eighth of its surface is
// panels. Measured in jf01: a wooden tiki's largest holds 19% to 31%, the
// floating tiki's 14%, and SpongeBob's body 8% spread over more directions than
// this counts.
//
// The budget is a tracking cost and not part of the answer. Area past it counts
// towards the surface without joining a direction, so overrunning it can only
// make a model look less panelled than it is -- which is why the budget is large
// enough that a faceted prop's panels are all still being counted when its last
// triangle arrives.
//
// Face normals off the positions and not the vertex normals, so the answer is
// the same before and after the weld.
enum
{
    kPanelDirs = 128
};

static const F32 kPanelSame = 0.985f;
static const F32 kPanelShare = 0.125f;

static S32 IsPanelled(RpGeometry* geo)
{
    if (geo->morphTarget == NULL || geo->morphTarget[0].verts == NULL ||
        geo->numTriangles <= 0)
    {
        return FALSE;
    }

    const RwV3d* v = geo->morphTarget[0].verts;
    RwV3d dir[kPanelDirs];
    F32 area[kPanelDirs];
    S32 dirs = 0;
    F32 total = 0.0f;

    for (S32 t = 0; t < geo->numTriangles; t++)
    {
        const RwV3d* a = &v[geo->triangles[t].vertIndex[0]];
        const RwV3d* b = &v[geo->triangles[t].vertIndex[1]];
        const RwV3d* c = &v[geo->triangles[t].vertIndex[2]];
        RwV3d e0 = { b->x - a->x, b->y - a->y, b->z - a->z };
        RwV3d e1 = { c->x - a->x, c->y - a->y, c->z - a->z };
        RwV3d n = { e0.y * e1.z - e0.z * e1.y, e0.z * e1.x - e0.x * e1.z,
                    e0.x * e1.y - e0.y * e1.x };
        F32 len = xsqrt(n.x * n.x + n.y * n.y + n.z * n.z);

        if (len < 1e-12f)
        {
            continue;
        }

        RwV3d u = { n.x / len, n.y / len, n.z / len };
        S32 hit = -1;

        total += 0.5f * len;

        for (S32 d = 0; d < dirs; d++)
        {
            if (u.x * dir[d].x + u.y * dir[d].y + u.z * dir[d].z > kPanelSame)
            {
                hit = d;
                break;
            }
        }

        if (hit >= 0)
        {
            area[hit] += 0.5f * len;
        }
        else if (dirs < kPanelDirs)
        {
            dir[dirs] = u;
            area[dirs] = 0.5f * len;
            dirs++;
        }

        // Past the budget the area still counts towards the surface. Nothing
        // else to do with it: a direction nobody is tracking cannot hold a
        // panel's worth.
    }

    if (total <= 0.0f)
    {
        return FALSE;
    }

    for (S32 d = 0; d < dirs; d++)
    {
        if (area[d] / total >= kPanelShare)
        {
            return TRUE;
        }
    }

    return FALSE;
}

// **Asked of each piece and not of the model.** The floating tiki is a rounded
// body of 384 triangles carrying a faceted piece of 126, so a model answers for
// its pieces only if they all look alike, and these do not. What the row changes
// is the rim, and a rim steps across a flat panel whatever that panel is part of.
//
// A sheet is refused. Everything laid on a character -- his eyes, an eyelash, a
// decal -- is a single panel and would otherwise measure as a prop.
static S32 PieceIsPanelled(RpGeometry* geo, F32 flat)
{
    return flat <= 0.0f && IsPanelled(geo);
}

static S32 RampRowOf(RpGeometry* geo, F32 flat)
{
    if (HasReflection(geo))
    {
        return ITOON_RAMP_METAL;
    }

    if (PieceIsPanelled(geo, flat))
    {
        return ITOON_RAMP_PROP;
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
        FillSlot(a, geo, slot);
    }

    if (sRampRow[slot] == 0)
    {
        // Stored one higher than it is, so that zero can mean unanswered
        // without a second array of flags.
        sRampRow[slot] = RampRowOf(geo, sFlat[slot]) + 1;
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
static F32 RowLength(const RwV3d* r)
{
    return xsqrt(r->x * r->x + r->y * r->y + r->z * r->z);
}

// How much bigger the world is than the model's own units, by the middle of the
// matrix's three rows.
//
// **The hull is pushed in the model's units and the pixel widths are in the
// world's, so the two only agree when a model is placed at its own size.** A
// HUD model is not: xModelRender2D shears it into the corner of the frustum at
// about a tenth, so a floor that promises a pixel and a half delivered a sixth
// of a pixel and the ceiling crushed what was left. That is why the shiny in
// the counter and the jellyfish in the menu had no line worth the name.
//
// The middle row and not the largest, for the reason iToonOutlineThinCap gives:
// a HUD model's two screen-facing rows carry the same tenth and its third is
// whatever the shear left, so the middle is the one the silhouette is drawn at.
// A model standing in the level has three ones and divides by one.
static F32 ObjectScale(const RwMatrix* mat)
{
    if (mat == NULL)
    {
        return 1.0f;
    }

    F32 row[3];

    row[0] = RowLength(&mat->right);
    row[1] = RowLength(&mat->up);
    row[2] = RowLength(&mat->at);

    for (S32 i = 0; i < 2; i++)
    {
        for (S32 j = i + 1; j < 3; j++)
        {
            if (row[j] < row[i])
            {
                F32 swap = row[i];

                row[i] = row[j];
                row[j] = swap;
            }
        }
    }

    return row[1] > 1e-6f ? row[1] : 1.0f;
}

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
void iToonOutlineMaxWidth(const RwMatrix* mat)
{
    toonbackend::setOutlineMaxWidth(PixelsPerDepth(iScreenToonOutlineMax()) / ObjectScale(mat));
}

// How thin it may get, the same way. A fixed world width falls below a pixel
// somewhere down the level and the character stops being inked, which is the
// one thing an animated drawing never does.
void iToonOutlineMinWidth(const RwMatrix* mat)
{
    toonbackend::setOutlineMinWidth(PixelsPerDepth(iScreenToonOutlineMin()) / ObjectScale(mat));
}

enum
{
    kThinSlots = 512
};

// Geometries whose object-space size has been measured, and what it was. Held
// for the same reason the weld cache is: it is a property of the model and the
// renderer wants it on every draw.
static void* sThinGeo[kThinSlots];
static RwV3d sThinSize[kThinSlots];
static RwV3d sThinCentre[kThinSlots];

// How much of a model's own thickness the ink may take.
//
// A third reads as a line round the model rather than a border competing with
// it, and leaves margin for the perspective difference between the middle of
// the model and its corners.
static const F32 kThinInk = 0.35f;

// The slot holding this geometry's size, measuring it if this is the first
// sight of it, or -1 if there is nothing to measure.
static S32 ThinSlot(RpGeometry* geo)
{
    U32 i = PointerSlot(geo, kThinSlots);
    S32 tries = 0;

    while (sThinGeo[i] != NULL && tries < kThinSlots)
    {
        if (sThinGeo[i] == geo)
        {
            return (S32)i;
        }

        i = (i + 1) & (kThinSlots - 1);
        tries++;
    }

    if (tries >= kThinSlots)
    {
        return -1;
    }

    RwV3d* v = geo->morphTarget != NULL ? geo->morphTarget[0].verts : NULL;

    if (v == NULL || geo->numVertices <= 0)
    {
        return -1;
    }

    RwV3d lo = v[0];
    RwV3d hi = v[0];

    for (S32 k = 1; k < geo->numVertices; k++)
    {
        if (v[k].x < lo.x)
        {
            lo.x = v[k].x;
        }
        if (v[k].y < lo.y)
        {
            lo.y = v[k].y;
        }
        if (v[k].z < lo.z)
        {
            lo.z = v[k].z;
        }
        if (v[k].x > hi.x)
        {
            hi.x = v[k].x;
        }
        if (v[k].y > hi.y)
        {
            hi.y = v[k].y;
        }
        if (v[k].z > hi.z)
        {
            hi.z = v[k].z;
        }
    }

    sThinGeo[i] = geo;
    sThinSize[i].x = hi.x - lo.x;
    sThinSize[i].y = hi.y - lo.y;
    sThinSize[i].z = hi.z - lo.z;
    sThinCentre[i].x = 0.5f * (hi.x + lo.x);
    sThinCentre[i].y = 0.5f * (hi.y + lo.y);
    sThinCentre[i].z = 0.5f * (hi.z + lo.z);

    return (S32)i;
}

// **A model cannot be inked thicker than it is, and the pixel floor does not
// know that.**
//
// The floor is a pixel count turned into a world width by multiplying by view
// depth, so the ink grows without limit as a model recedes: a distant sign ends
// up with a band as wide as the sign. What reads as a line is a fraction of the
// thing it draws round, so the ceiling is lowered to what this model can carry.
//
// In world units, which the shader wants as a per-depth figure like the others,
// so it is divided by the depth of the model's middle -- near enough for an
// object small enough for this to matter at all.
void iToonOutlineThinCap(void* atomic, const RwMatrix* mat)
{
    RpAtomic* model = (RpAtomic*)atomic;

    if (model == NULL || mat == NULL)
    {
        return;
    }

    RpGeometry* geo = RpAtomicGetGeometry(model);

    if (geo == NULL)
    {
        return;
    }

    S32 slot = ThinSlot(geo);

    if (slot < 0)
    {
        return;
    }

    // **The middle of the three sides, not the smallest.**
    //
    // The smallest is the wrong one to measure a line against on anything that
    // is not a box. A shiny object is a plate 0.48 by 0.32 by 0.18: its band
    // grows in the plane of the plate, where it has a third of a unit to spare,
    // and holding it to a third of the 0.18 pinned it at the pixel floor and no
    // more. On a boxy model the middle and the smallest are the same number, so
    // nothing else moves.
    const RwV3d* size = &sThinSize[slot];
    F32 side[3];

    side[0] = size->x * RowLength(&mat->right);
    side[1] = size->y * RowLength(&mat->up);
    side[2] = size->z * RowLength(&mat->at);

    for (S32 i = 0; i < 2; i++)
    {
        for (S32 j = i + 1; j < 3; j++)
        {
            if (side[j] < side[i])
            {
                F32 swap = side[i];

                side[i] = side[j];
                side[j] = swap;
            }
        }
    }

    F32 thin = side[1];

    if (thin <= 0.0f)
    {
        return;
    }

    RwCamera* cam = RwCameraGetCurrentCamera();
    RwMatrix* cm = cam != NULL ? RwFrameGetLTM(RwCameraGetFrame(cam)) : NULL;

    if (cm == NULL)
    {
        return;
    }

    const RwV3d* c = &sThinCentre[slot];
    RwV3d world;

    world.x = mat->pos.x + c->x * mat->right.x + c->y * mat->up.x + c->z * mat->at.x;
    world.y = mat->pos.y + c->x * mat->right.y + c->y * mat->up.y + c->z * mat->at.y;
    world.z = mat->pos.z + c->x * mat->right.z + c->y * mat->up.z + c->z * mat->at.z;

    F32 depth = (world.x - cm->pos.x) * cm->at.x + (world.y - cm->pos.y) * cm->at.y +
                (world.z - cm->pos.z) * cm->at.z;

    if (depth < 1e-3f)
    {
        return;
    }

    F32 capped = kThinInk * thin / depth;

    // **Never under the floor, whatever the model's size says.** A small model
    // far away is thinner than a line is allowed to be, and a proportion of it
    // works out at a fraction of a pixel. The shader applies this ceiling last,
    // so it wins over the floor unless it is held here, and the floor is the
    // stronger claim: a line nobody can see is not a line.
    F32 lowest = PixelsPerDepth(iScreenToonOutlineMin());

    if (capped < lowest)
    {
        capped = lowest;
    }

    F32 standing = PixelsPerDepth(iScreenToonOutlineMax());

    if (standing <= 0.0f || capped < standing)
    {
        // Into the model's own units, the same as the two widths above.
        toonbackend::setOutlineMaxWidth(capped / ObjectScale(mat));
    }
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

void iToonSuppress(S32 on)
{
    toonbackend::setToonShading(on ? FALSE : iScreenToon(), iScreenToonBands(),
                                iScreenToonSaturation(), iScreenToonStrength());
}

void iToonPlainRegister(xModelInstance* model)
{
    iToonOutlineRegister(model, ITOON_OUTLINE_PLAINDRAW);
}

void iToonOutlineRegister(xModelInstance* model, S32 mode)
{
    // NONE is the absence of an answer and is not worth a slot -- the default
    // is what it falls through to. PLAINDRAW is an answer.
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

// Whether this atomic was NAMED, as against reaching the look through the
// experimental.toon_all default.
//
// It is the only thing that separates a jellyfish from a floor decal. Both are
// see-through and both are models; one is a character the game listed and the
// other picked the ink up from a setting.
S32 iToonOutlineNamed(void* atomic)
{
    if (atomic == NULL)
    {
        return FALSE;
    }

    U32 i = OutlineSlot(atomic);
    S32 tries = 0;

    while (sOutlineKey[i] != NULL && tries < kOutlineSlots)
    {
        if (sOutlineKey[i] == atomic)
        {
            return TRUE;
        }

        i = (i + 1) & (kOutlineSlots - 1);
        tries++;
    }

    return FALSE;
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

    // Paused, an unnamed model is plain: the default is exactly what a floor
    // decal picked the look up from.
    return sPaused ? ITOON_OUTLINE_NONE : sOutlineDefault;
}

void iToonShutdown()
{
    if (sRamp != NULL)
    {
        RwTextureDestroy(sRamp);
        sRamp = NULL;
    }
}
