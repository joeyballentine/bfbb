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

// The metrics of one glyph, in the two spaces xFont wants them in.
//
// Texture coordinates are normalized to the atlas. The rest are in EM units of
// the line box, which is the space font_data::bounds already works in: the
// game scales them by the character cell it has positioned, so 1.0 of height is
// one line and `advance` is what the layout steps by.
struct iFontGlyph
{
    // The glyph's rect in the atlas, normalized. Its HEIGHT is the whole cell
    // for every glyph, not the ink: the game's own atlas works that way, and
    // its vertical metrics are one baseline for the whole font rather than
    // anything per character.
    F32 u0;
    F32 v0;
    F32 u1;
    F32 v1;

    // The ink's width in atlas pixels, which is also what the pen advances by.
    // The game measures its own atlas the same way (reset_font_spacing scans
    // the image for each glyph's ink) and adds any letter spacing above this.
    F32 inkWidth;
};
// Read a TrueType file. FALSE if it is not there or is not a font, reported
// once -- the caller carries on with the game's own atlas either way.
//
// Idempotent per path. Called by iSystem before the first font is built.
S32 iFontLoad(const char* path);

// Whether a font was loaded and glyphs can be asked for.
S32 iFontAvailable();

// config.ini's [text] font_scale, pushed down by iSystem the way every other
// setting is. 1.0 unless set.
void iFontSetScale(F32 scale);
F32 iFontScale();

// Rasterise `count` characters of `charset` into one atlas.
//
// `pixelHeight` is the line box in atlas pixels -- how sharp the result is, and
// nothing else; the game scales the quads to whatever size it draws text at.
//
// On success the atlas is written to `pixels` as 8-bit coverage, `width` and
// `height` give its size, and `glyphs[i]` describes charset[i]. The buffer is
// owned by this module and stays valid until the next call.
//
// FALSE if the atlas would not fit or the font has none of the characters.
// `cellHeight` comes back as the cell every glyph was drawn into, and
// `baselineRow` as how far down that cell the baseline sits. Both are in atlas
// pixels and both are font-wide, because that is the shape the game's metrics
// have: one baseline, one cell, and a per-glyph width.
// `baselineFraction` is where the caller's OWN atlas puts the baseline in its
// cell -- the game's font 0 has a 22-pixel cell with the baseline 19 down, so
// 0.864. The cell here is padded to match it, which is what makes a substituted
// font sit at the size the game's does rather than merely somewhere sensible:
// the quad is always the whole cell, so how much of the cell the ink fills IS
// the apparent size. Pass 0 to just use the font's own ink box.
// `scale` is a final nudge, 1.0 for none. The cell is divided by it, so a
// smaller number pads the cell and the text comes out smaller -- in BOTH axes,
// since the horizontal unit is derived from the cell. It exists because the last
// few percent depend on which face you substitute: a font's declared line box
// and a hand-authored atlas cell are different things by a few percent, and no
// derivation gets every font right.
// `inkFraction` is how much of its cell the atlas being replaced fills, and
// `baselineFraction` is where the baseline sits in it -- both measured from that
// atlas by the caller. The cell here is padded so this font fills it the same
// way, which is what makes the substitute the same SIZE rather than merely a
// sensible one. Pass 0 for either to fall back to this font's own line box.
// `refChar` is the character the two fractions were measured from, and it is
// measured here too. It has to be the SAME glyph on both sides: the atlas draws
// accented capitals that a substituted font often lacks, so anything measured
// over the whole character set compares an accent mark against nothing and
// makes every ordinary capital too large.
S32 iFontRasterize(const char* charset, S32 count, S32 pixelHeight, char refChar,
                   F32 inkFraction, F32 baselineFraction, F32 scale, const U8** pixels,
                   S32* width, S32* height, iFontGlyph* glyphs, F32* cellHeight,
                   F32* baselineRow);

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
