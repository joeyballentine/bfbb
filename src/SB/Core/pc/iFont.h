#ifndef IFONT_H
#define IFONT_H

#include <types.h>

// PC-only: the game's bitmap font, rasterised from a TrueType file instead.
//
// **Why this exists.** The game's fonts are texture atlases authored for a
// 640x480 framebuffer -- font1_sb, which the comment in xFont.cpp calls "the
// SpongeBob font", is an 18x22 cell grid. At 640x480 that is exactly right. At
// 4K it is magnified sixfold with a linear filter, and text is the first thing
// anyone notices going soft, because letterforms have edges that a photograph
// of a sponge does not.
//
// The typeface is not being changed for its own sake. Rasterising the SAME
// letterforms from an outline at the size they will actually be drawn is a
// sharpness fix; the game keeps its own layout, spacing and colours.
//
// **No font ships with the port.** iFontLoad takes a path, and config.ini's
// [text] font names it -- empty by default, which is the game's own atlas and
// exactly what the console draws. tools/getfont.py fetches one.
//
// **What it replaces, and what it does not.** Only the glyph atlas and the
// per-glyph metrics. Everything above -- where a line breaks, how a textbox is
// justified, the colour tags, the inline model and texture tags -- is the
// game's and is untouched, so a font that fails to load costs nothing but
// sharpness.

// Where one glyph's ink sits inside its cell, in atlas pixels. The caller
// measures these off the atlas being replaced; the substitute is drawn INTO
// them rather than measured against them, which is what keeps the game's layout
// exactly as it was.
struct iFontCell
{
    S32 x;
    S32 y;
    S32 w;
    S32 h;
};

// Read a TrueType file. FALSE if it is not there or is not a font, reported
// once -- the caller carries on with the game's own atlas either way.
//
// Idempotent per path. Called by iSystem before the first font is built.
S32 iFontLoad(const char* path);

// Whether a font was loaded and glyphs can be asked for.
S32 iFontAvailable();

// config.ini's [text] font_upscale: how many times the atlas cell's own
// resolution to draw at. 4 unless set.
//
// This is the ONLY knob a substituted font needs, and it is a sharpness setting
// rather than a taste one -- a glyph lands in the box the artwork had it in
// whatever this is, so raising it cannot move anything, only add pixels. The
// game's cell is 18x22, so 4 draws at 72x88 and a whole font stays well under a
// megabyte.
void iFontSetUpscale(S32 upscale);
S32 iFontUpscale();

// config.ini's [text] font_padding: how far to inset a glyph inside the box
// the artwork had it in, in ATLAS pixels. Half a pixel unless set.
//
// find_bounds measures that box by testing for any non-zero alpha at all, so
// it includes the whole anti-aliased fringe -- the atlas glyph's solid body
// stops short of it by about half a pixel on each side. An outline drawn to
// fill the box exactly therefore reads as slightly too big, letters in the
// right places but too heavy for them.
void iFontSetPadding(F32 padding);
F32 iFontPadding();

// Draw `count` glyphs of `charset` into a grid of cells, each one stretched to
// fill the rect `cells[i]` gives it.
//
// `cellW` and `cellH` are the atlas cell those rects are relative to, and
// `upscale` is how much sharper than that to draw -- the whole point being that
// the game's cell is 18x22 and is drawn far larger than that.
//
// Nothing about the game's metrics is returned, because nothing about them
// changes: the glyph lands exactly where the atlas had it, so the advance, the
// baseline and the letter spacing are all still the game's own.
S32 iFontRasterize(const char* charset, S32 count, S32 cellW, S32 cellH, const iFontCell* cells,
                   S32 upscale, F32 padding, const U8** pixels, S32* width, S32* height,
                   S32* slotStride, S32* perRow);

// The atlas as a texture the game can draw with.
//
// White with the coverage in alpha, because that is what the game's own font
// textures are: every glyph is drawn multiplied by a vertex colour, so the ink
// has to carry its shape in alpha and nothing in RGB.
//
// Declared here rather than left to the caller because xFont is shared code and
// has no business knowing how a raster is made. NULL if it could not be.
struct RwTexture;
RwTexture* iFontMakeTexture(const U8* coverage, S32 width, S32 height);

#endif
