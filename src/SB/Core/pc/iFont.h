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
// [text] font and font_sans name them -- empty by default, which is the game's
// own atlas and exactly what the console draws. tools/getfont.py fetches one.
//
// **The game has more than one face.** Four atlases, and they are not the same
// typeface: font1_sb is the SpongeBob face and font_numbers its numerals, but
// font_sb is a plain sans serif -- the copyright screen and xTRC's memory card
// and controller messages are drawn in it -- and the fourth is a 6x8 system
// font. So there is a font per face rather than one for the game, and the
// system font keeps its own pixels: at that size an outline is not what it is.
// font_source in xFont.cpp maps the four onto these.
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

    // The cell itself, in the atlas the rect above was measured in. Only the
    // debug overlay uses it, to find the glyph it is drawing over.
    S32 cellX;
    S32 cellY;
};

// The atlas being replaced, as coverage bytes. iFontRasterize takes one only
// for the debug overlay; NULL is the normal path.
struct iFontAtlas
{
    const U8* coverage;
    S32 width;
    S32 height;
};

// Which face a call is about.
enum iFontFace
{
    IFONT_FACE_SB, // font1_sb and font_numbers -- the SpongeBob face
    IFONT_FACE_SANS, // font_sb -- the copyright screen, the TRC messages
    IFONT_FACE_COUNT
};

// Read a TrueType file. FALSE if it is not there or is not a font, reported
// once -- the caller carries on with the game's own atlas either way.
//
// Idempotent per path. Called by iSystem before the first font is built.
S32 iFontLoad(iFontFace face, const char* path);

// Whether a font was loaded for that face and glyphs can be asked for.
S32 iFontAvailable(iFontFace face);

// Where the host keeps a sans serif to stand in for font_sb, which is what
// [text] font_sans = auto resolves to. Arial, because that is the face the
// atlas is, falling back to the metric-compatible Liberation Sans off Windows.
// FALSE if the host has none, and the game's own atlas is then used.
S32 iFontSystemSans(char* out, S32 outsize);

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

// config.ini's [text] font_weight: how much to thicken a glyph's strokes, in
// ATLAS pixels. 0 unless set, which is the outline as the face draws it.
//
// The game's atlases are hand-drawn and heavier than most text faces at the
// same size, so an outline substituted for one can land the right size and
// still read as too light. This grows the ink by a fraction of a pixel in every
// direction, which is weight rather than size: a stroke gains it on both sides
// and a letter only on its outside.
//
// It does make a glyph fractionally larger -- the growth has to go somewhere --
// so a heavy setting wants a little more font_padding to sit back in its box.
// Per face, because the two are different typefaces and the atlases they stand
// in for are drawn at different weights: measured against the game's own, the
// SpongeBob face wants a little and the sans wants none at all.
void iFontSetWeight(iFontFace face, F32 weight);
F32 iFontWeight(iFontFace face);

// BFBB_FONTDIFF: draw each glyph of the atlas being replaced over the outline
// that replaces it, so the two can be compared where the game actually draws
// them. The outline goes in green and the game's own glyph in red, so they are
// yellow where they agree and a coloured fringe is a glyph the substitute sizes
// differently.
//
// A diagnostic, and it looks like one: the game multiplies a glyph by the text
// colour, so the two channels only survive on text drawn light. Off by default.
void iFontSetOverlay(S32 on);
S32 iFontOverlay();

// How much of the two letterforms landed on the same pixels, as a percentage:
// the ink they share over the ink either of them has. Set by the last
// iFontRasterize that was given an atlas to draw over, and the number
// tools/fontfit sweeps -- 100 would be the same glyph twice, and what moves it
// is font_padding and font_weight.
//
// Alignment is what it measures first: two glyphs drawn in the same box agree
// substantially, two drawn in different boxes agree almost nowhere.
F32 iFontOverlayAgreement();

// The substitute's total ink over the original's, from the same pass. 1.0 is a
// face laying down as much ink as the atlas it replaces, which is what "as bold
// as the game's own font" means measured rather than judged.
//
// It answers a question agreement cannot. Agreement stops discriminating once a
// glyph is thick enough to fill the box it is drawn in: past that, everything
// the original has is covered, the union is the box, and the ratio settles at
// whatever fraction of its box the original filled -- a number that can look
// like a good fit while the letters have gone to blobs. Ink says outright that
// there is too much of it.
F32 iFontOverlayInk();

// BFBB_FONTDUMP: write one font's atlas and glyph boxes to a file, so the fit
// can be measured without the game.
//
// Everything the substitution reads about the font it replaces, in one blob:
// the character set, the cell, the ink boxes and the atlas coverage itself.
// tools/fontfit replays iFontRasterize against it and sweeps padding and weight
// offline, which is the whole reason this exists -- those two are tuned by
// their effect on the fit, and launching the game to see one number is a slow
// way to ask.
//
// Appends, so one launch captures every font. Named by iSystem from the
// environment; empty writes nothing.
void iFontSetDumpPath(const char* path);
S32 iFontDumpWanted();
S32 iFontDump(const char* name, const char* charset, S32 count, S32 cellW, S32 cellH,
              const iFontCell* cells, const iFontAtlas* atlas);

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
//
// `original` is the atlas being replaced and is only read when the overlay is
// on, in which case `overlay` comes back holding the game's own glyphs scaled
// into the same boxes. Pass both as NULL otherwise.
S32 iFontRasterize(iFontFace face, const char* charset, S32 count, S32 cellW, S32 cellH,
                   const iFontCell* cells, S32 upscale, F32 padding, const iFontAtlas* original,
                   const U8** pixels, const U8** overlay, S32* width, S32* height, S32* slotStride,
                   S32* perRow);

// The atlas as a texture the game can draw with.
//
// White with the coverage in alpha, because that is what the game's own font
// textures are: every glyph is drawn multiplied by a vertex colour, so the ink
// has to carry its shape in alpha and nothing in RGB.
//
// `overlay`, when the debug overlay is on, is the second coverage plane
// iFontRasterize returned: it goes in red and the substitute in green, so the
// ink is no longer white and the two letterforms can be told apart.
//
// Declared here rather than left to the caller because xFont is shared code and
// has no business knowing how a raster is made. NULL if it could not be.
struct RwTexture;
RwTexture* iFontMakeTexture(const U8* coverage, const U8* overlay, S32 width, S32 height);

#endif
