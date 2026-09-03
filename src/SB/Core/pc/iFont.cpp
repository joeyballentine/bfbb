// TrueType glyphs for the game's fonts. The argument for it is in iFont.h.
//
// Rasterising only: nothing here knows what a texture is, which is what lets
// tools/fontfit link it without a renderer. iFontMakeTexture, the one part
// that does, is in iFontTexture.cpp.

#include "iFont.h"

#include "iHost.h"
#include "iScreen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// stb_truetype, vendored in third_party/stb. Public domain, single header, no
// build system of its own -- which is the whole reason it is the one used here:
// the port needs a glyph rasteriser and nothing else a font library would bring.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace
{
    // One per face, because the game's atlases are not all the same typeface.
    struct face_state
    {
        // The file, kept for the life of the process. stb_truetype does not
        // copy it -- stbtt_fontinfo points into these bytes and every glyph is
        // read from them on demand -- so freeing this would invalidate the
        // font.
        U8* file;
        S32 fileSize;

        stbtt_fontinfo font;
        S32 loaded;
        S32 failed;
        F32 weight;
        S32 weightAuto;
        iFontFit fit;

        // Whether the characters this face does not have have been named. Once
        // per font: iFontAutoFit rasterises the same one dozens of times over
        // and the answer does not change between them.
        S32 saidWhatIsMissing;

        // What iFontAutoFit settled on, and at what upscale. The SpongeBob face
        // is asked for twice -- font1_sb and its numerals -- and the sweep is
        // the expensive part of building a font, so it is run for the first of
        // them and remembered.
        S32 tuned;
        S32 tunedUpscale;
        F32 tunedPadding;
        F32 tunedWeight;
    };

    face_state sFaces[IFONT_FACE_COUNT];

    // Sizing, which is about the atlas cell rather than the typeface, and so is
    // the same for every face.
    S32 sUpscale;   // 0 is automatic; see iFontUpscale
    F32 sPadding = 0.5f;
    S32 sPaddingAuto;

    // The atlas. One allocation, reused: the game builds four fonts at startup
    // and never again, so this is only ever grown to the largest of them.
    U8* sAtlas;
    S32 sAtlasCapacity;
    S32 sAtlasWidth;
    S32 sAtlasHeight;

    // The same again, holding the atlas being replaced. Only allocated when the
    // debug overlay is on, so the normal path costs nothing.
    U8* sOverlay;
    S32 sOverlayCapacity;
    S32 sOverlayOn;
    F32 sAgreement;
    F32 sInk;
    S32 sSubstituted;
    S32 sGlyphs;

    // One glyph's worth of workspace, for the thickening pass -- a max filter
    // cannot read and write the same pixels.
    U8* sScratch;
    S32 sScratchCapacity;

    // Where BFBB_FONTDUMP points, if anywhere.
    char sDumpPath[512];

    // Copy a rect of coverage into another, scaled to fit. Nearest neighbour:
    // both callers are magnifying an atlas several times over, and a filter
    // would only soften the edges being compared or substituted.
    void blitScaled(const U8* src, S32 srcStride, S32 sx, S32 sy, S32 sw, S32 sh, U8* dst,
                    S32 dstStride, S32 dx, S32 dy, S32 dw, S32 dh)
    {
        if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
        {
            return;
        }

        for (S32 y = 0; y < dh; y++)
        {
            const U8* srcRow = src + (size_t)(sy + y * sh / dh) * srcStride;
            U8* dstRow = dst + (size_t)(dy + y) * dstStride + dx;

            for (S32 x = 0; x < dw; x++)
            {
                dstRow[x] = srcRow[sx + x * sw / dw];
            }
        }
    }

    // Grow the ink in `w` by `h` coverage bytes outward by `distance` pixels,
    // 0 to 1. Called once per whole pixel of weight and once more for the
    // remainder.
    //
    // **Why this adds rather than taking a maximum.** The obvious thickening is
    // to give every pixel the largest of itself and its neighbours, faded by
    // the distance wanted. That grows the ink by exactly ONE pixel however
    // faint the fade is, because a neighbour is a whole pixel away -- so the
    // real distance is one buffer pixel, which is 1/upscale of an ATLAS pixel,
    // and a weight that reads correctly at one render height is wrong at
    // another. The setting is in atlas pixels so that it does not depend on the
    // resolution, and that made it depend on the resolution.
    //
    // A glyph edge is not a step, though: it is a ramp about a pixel wide, from
    // nothing to solid, and the letterform's true edge is where that ramp
    // passes half. Adding a fraction of full coverage to the ramp slides the
    // half-way point out by that fraction of a pixel -- which is a real
    // sub-pixel distance, and the same distance at any upscale.
    //
    // Scaled by the neighbourhood rather than added flat: away from any ink
    // there is no ramp to move and no neighbour to take it from, so nothing
    // happens. A flat addition would raise the empty space around the letters
    // into a grey haze over every glyph box.
    void dilate(U8* pixels, S32 w, S32 h, S32 stride, F32 distance)
    {
        if (distance <= 0.0f || w <= 0 || h <= 0)
        {
            return;
        }

        const S32 needed = w * h;

        if (needed > sScratchCapacity)
        {
            free(sScratch);
            sScratch = (U8*)malloc((size_t)needed);
            if (sScratch == NULL)
            {
                sScratchCapacity = 0;
                return;
            }
            sScratchCapacity = needed;
        }

        for (S32 y = 0; y < h; y++)
        {
            memcpy(sScratch + (size_t)y * w, pixels + (size_t)y * stride, (size_t)w);
        }

        for (S32 y = 0; y < h; y++)
        {
            for (S32 x = 0; x < w; x++)
            {
                U8 neighbour = 0;

                for (S32 dy = -1; dy <= 1; dy++)
                {
                    const S32 sy = y + dy;
                    if (sy < 0 || sy >= h)
                    {
                        continue;
                    }

                    for (S32 dx = -1; dx <= 1; dx++)
                    {
                        const S32 sx = x + dx;
                        if (sx < 0 || sx >= w || (dx == 0 && dy == 0))
                        {
                            continue;
                        }

                        const U8 v = sScratch[(size_t)sy * w + sx];
                        if (v > neighbour)
                        {
                            neighbour = v;
                        }
                    }
                }

                U8* p = pixels + (size_t)y * stride + x;

                // The brightest coverage anywhere around this pixel, itself
                // included: how much ink there is locally to move.
                if (*p > neighbour)
                {
                    neighbour = *p;
                }

                const F32 grown = (F32)*p + distance * (F32)neighbour;

                *p = grown > 255.0f ? (U8)255 : (U8)grown;
            }
        }
    }

    // A gap between glyphs, so that the linear filter the game draws text with
    // cannot pull a neighbour's ink into the edge of a character. One pixel is
    // enough at 1:1 and this is drawn magnified, so it is two.
    const S32 kPadding = 2;

    // The framebuffer the game's font atlases were authored against.
    const S32 kRetailHeight = 480;

    const char* faceName(iFontFace face)
    {
        return face == IFONT_FACE_SANS ? "the sans serif" : "the SpongeBob font";
    }

    S32 validFace(iFontFace face)
    {
        return face >= 0 && face < IFONT_FACE_COUNT;
    }

    void fontFail(iFontFace face, const char* what, const char* detail)
    {
        if (validFace(face))
        {
            sFaces[face].failed = 1;
        }

        printf("bfbb: %s is off -- %s%s%s\n", faceName(face), what,
               detail != NULL ? ": " : "", detail != NULL ? detail : "");
        fflush(stdout);
    }
}

S32 iFontLoad(iFontFace face, const char* path)
{
    if (!validFace(face))
    {
        return FALSE;
    }

    face_state& fs = sFaces[face];

    if (fs.loaded || fs.failed)
    {
        return fs.loaded;
    }

    if (path == NULL || path[0] == '\0')
    {
        // Not asked for. Not a failure, and not worth a line of output: the
        // game's own atlas is the default and what the console draws.
        return FALSE;
    }

    FILE* f = fopen(path, "rb");
    if (f == NULL)
    {
        fontFail(face, "no such file", path);
        return FALSE;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0)
    {
        fclose(f);
        fontFail(face, "the file is empty", path);
        return FALSE;
    }

    fs.file = (U8*)malloc((size_t)size);
    if (fs.file == NULL)
    {
        fclose(f);
        fontFail(face, "out of memory reading", path);
        return FALSE;
    }

    size_t got = fread(fs.file, 1, (size_t)size, f);
    fclose(f);

    if (got != (size_t)size)
    {
        free(fs.file);
        fs.file = NULL;
        fontFail(face, "the file could not be read", path);
        return FALSE;
    }

    fs.fileSize = (S32)size;

    // Offset 0: the first font of a collection, which for a plain .ttf is the
    // only one. A .ttc would need a choice, and nothing asks for one.
    if (!stbtt_InitFont(&fs.font, fs.file, stbtt_GetFontOffsetForIndex(fs.file, 0)))
    {
        free(fs.file);
        fs.file = NULL;
        fontFail(face, "not a TrueType font", path);
        return FALSE;
    }

    fs.loaded = 1;
    fs.saidWhatIsMissing = FALSE;
    // The settings as well as the path. A setting that is read but never
    // reaches the pixels looks exactly like one that works, and this port has
    // already shipped one of those -- text.font_spacing was missing from the
    // settings table, so every value anyone tried was silently the default.
    // An auto one is not known yet: the sweep needs the atlas it is replacing,
    // and that does not exist until the game builds the font. It prints its own
    // line when it does.
    char paddingText[64];
    char weightText[64];

    if (sPaddingAuto)
    {
        strcpy(paddingText, "padding auto");
    }
    else
    {
        sprintf(paddingText, "padding %.3f, inset %d px", (double)sPadding,
                (int)((F32)iFontUpscale() * sPadding + 0.5f));
    }

    if (fs.weightAuto)
    {
        strcpy(weightText, "weight auto");
    }
    else
    {
        sprintf(weightText, "weight %.2f", (double)fs.weight);
    }

    printf("bfbb: %s rendered from %s (upscale %d, %s, %s, %s)%s\n", faceName(face), path,
           (int)iFontUpscale(), paddingText, weightText,
           fs.fit == IFONT_FIT_NATURAL ? "its own metrics"
                                       : (fs.fit == IFONT_FIT_WIDTH ? "its own width"
                                                                    : "stretched to the box"),
           sOverlayOn ? ", BFBB_FONTDIFF overlay on" : "");
    fflush(stdout);
    return TRUE;
}

S32 iFontAvailable(iFontFace face)
{
    return validFace(face) ? sFaces[face].loaded : FALSE;
}

S32 iFontSystemSans(char* out, S32 outsize)
{
    if (out == NULL || outsize <= 0)
    {
        return FALSE;
    }

    out[0] = '\0';

    // Arial where Windows keeps it, asked for rather than assumed -- the drive
    // is not always C.
    const char* root = getenv("SystemRoot");
    if (root == NULL)
    {
        root = getenv("WINDIR");
    }

    if (root != NULL && root[0] != '\0')
    {
        snprintf(out, (size_t)outsize, "%s/Fonts/arial.ttf", root);
        if (iHostPathExists(out))
        {
            return TRUE;
        }
    }

    // Elsewhere: Microsoft's own font if someone installed it, then Liberation
    // Sans, which was drawn to Arial's metrics.
    static const char* const kElsewhere[] = {
        "/usr/share/fonts/truetype/msttcorefonts/Arial.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
        "/Library/Fonts/Arial.ttf",
    };

    for (size_t i = 0; i < sizeof(kElsewhere) / sizeof(kElsewhere[0]); i++)
    {
        if (iHostPathExists(kElsewhere[i]))
        {
            snprintf(out, (size_t)outsize, "%s", kElsewhere[i]);
            return TRUE;
        }
    }

    out[0] = '\0';
    return FALSE;
}

void iFontSetUpscale(S32 upscale)
{
    // Zero and below mean automatic. Above, clamped rather than refused: this
    // cannot make the text WRONG, only blurrier or larger in memory, so a silly
    // number is worth quietly correcting instead of stopping for.
    if (upscale > 16)
    {
        upscale = 16;
    }
    if (upscale < 0)
    {
        upscale = 0;
    }

    sUpscale = upscale;
}

S32 iFontUpscale()
{
    if (sUpscale > 0)
    {
        return sUpscale;
    }

    // **Automatic: as sharp as the screen, and no sharper.**
    //
    // The atlas is authored against a 480-line framebuffer and drawn magnified
    // by however much taller the render size is, so that ratio IS the right
    // multiplier -- it rasterises a glyph at roughly one atlas pixel per screen
    // pixel. Below it the text is blurrier than the display can show; above it
    // the extra pixels are thrown away by the filter and the letters read as
    // crisper than everything around them, which is its own kind of wrong on a
    // game whose art is a photograph of a sponge.
    //
    // At 640x480 this is 1, which is the console's own resolution and so exactly
    // the softness the game shipped with.
    //
    // Read here rather than when the setting is stored, because that happens
    // before the window is open and the render size is not final until it is.
    S32 height = iScreenHeight();
    if (height <= 0)
    {
        return 1;
    }

    S32 automatic = (height + kRetailHeight / 2) / kRetailHeight;

    if (automatic < 1)
    {
        automatic = 1;
    }
    if (automatic > 16)
    {
        automatic = 16;
    }

    return automatic;
}

void iFontSetPadding(F32 padding)
{
    // Negative would grow the glyph past the box the artwork had it in,
    // which is a legitimate thing to want and cannot break anything: the
    // metrics are still the game's, so only the ink moves.
    sPadding = padding;
}

F32 iFontPadding()
{
    return sPadding;
}

void iFontSetPaddingAuto(S32 on)
{
    sPaddingAuto = on;
}

S32 iFontPaddingAuto()
{
    return sPaddingAuto;
}

void iFontSetWeightAuto(iFontFace face, S32 on)
{
    if (validFace(face))
    {
        sFaces[face].weightAuto = on;
    }
}

S32 iFontWeightAuto(iFontFace face)
{
    return validFace(face) ? sFaces[face].weightAuto : FALSE;
}

void iFontSetWeight(iFontFace face, F32 weight)
{
    if (!validFace(face))
    {
        return;
    }

    // Clamped rather than refused, like the upscale: too much is a fat blob and
    // is obvious on sight, not something worth stopping the game for.
    if (weight < 0.0f)
    {
        weight = 0.0f;
    }
    if (weight > 4.0f)
    {
        weight = 4.0f;
    }

    sFaces[face].weight = weight;
}

F32 iFontWeight(iFontFace face)
{
    return validFace(face) ? sFaces[face].weight : 0.0f;
}

void iFontSetFit(iFontFace face, iFontFit fit)
{
    if (validFace(face))
    {
        sFaces[face].fit = fit;
    }
}

iFontFit iFontFitOf(iFontFace face)
{
    // IFONT_FIT_BOX is zero, so a face nothing has set -- in a tool that only
    // rasterises, say -- gets the behaviour the substitution has always had.
    return validFace(face) ? sFaces[face].fit : IFONT_FIT_BOX;
}

void iFontSetOverlay(S32 on)
{
    sOverlayOn = on;
}

S32 iFontOverlay()
{
    return sOverlayOn;
}

F32 iFontOverlayAgreement()
{
    return sAgreement;
}

F32 iFontOverlayInk()
{
    return sInk;
}

S32 iFontOverlaySubstituted()
{
    return sSubstituted;
}

S32 iFontOverlayGlyphs()
{
    return sGlyphs;
}

void iFontSetDumpPath(const char* path)
{
    if (path == NULL || path[0] == '\0')
    {
        sDumpPath[0] = '\0';
        return;
    }

    snprintf(sDumpPath, sizeof(sDumpPath), "%s", path);
}

S32 iFontDumpWanted()
{
    return sDumpPath[0] != 0;
}

S32 iFontDump(const char* name, const char* charset, S32 count, S32 cellW, S32 cellH,
              const iFontCell* cells, const iFontAtlas* atlas)
{
    if (sDumpPath[0] == '\0' || name == NULL || charset == NULL || cells == NULL ||
        atlas == NULL || atlas->coverage == NULL || count <= 0)
    {
        return FALSE;
    }

    FILE* f = fopen(sDumpPath, "ab");
    if (f == NULL)
    {
        printf("bfbb: the font dump could not be written: %s\n", sDumpPath);
        fflush(stdout);
        return FALSE;
    }

    // A flat record, little-endian by being written as it sits in memory: this
    // is read back by a tool built from this same tree, on this same machine,
    // and a format with a version and an endianness would be pretending
    // otherwise.
    char label[32];
    memset(label, 0, sizeof(label));
    snprintf(label, sizeof(label), "%s", name);

    const S32 header[5] = { count, cellW, cellH, atlas->width, atlas->height };

    fwrite("BFFD", 1, 4, f);
    fwrite(label, 1, sizeof(label), f);
    fwrite(header, sizeof(S32), 5, f);
    fwrite(charset, 1, (size_t)count, f);
    fwrite(cells, sizeof(iFontCell), (size_t)count, f);
    fwrite(atlas->coverage, 1, (size_t)(atlas->width * atlas->height), f);
    fclose(f);

    printf("bfbb: dumped the %s atlas (%d glyphs, %dx%d cell) to %s\n", name, (int)count,
           (int)cellW, (int)cellH, sDumpPath);
    fflush(stdout);
    return TRUE;
}

S32 iFontRasterize(iFontFace face, const char* charset, S32 count, S32 cellW, S32 cellH,
                   const iFontCell* cells, S32 upscale, F32 padding, const iFontAtlas* source,
                   const U8** pixels, const U8** overlay, S32* width, S32* height, S32* slotStride,
                   S32* perRow)
{
    if (!iFontAvailable(face) || charset == NULL || cells == NULL || count <= 0 || cellW <= 0 ||
        cellH <= 0)
    {
        return FALSE;
    }

    const stbtt_fontinfo& font = sFaces[face].font;

    if (upscale < 1)
    {
        upscale = 1;
    }

    // The cell, magnified. Nothing about the game's metrics changes -- this is
    // the same 18x22 cell it always was, with more pixels in it.
    const S32 slotW = cellW * upscale;
    const S32 slotH = cellH * upscale;

    S32 columns = 1;
    while (columns * columns < count)
    {
        columns++;
    }

    const S32 rows = (count + columns - 1) / columns;

    // Powers of two, which every backend is happiest with.
    S32 w = 1;
    while (w < columns * slotW)
    {
        w *= 2;
    }
    S32 h = 1;
    while (h < rows * slotH)
    {
        h *= 2;
    }

    const S32 needed = w * h;
    if (needed > sAtlasCapacity)
    {
        free(sAtlas);
        sAtlas = (U8*)malloc((size_t)needed);
        if (sAtlas == NULL)
        {
            sAtlasCapacity = 0;
            fontFail(face, "out of memory for the glyph atlas", NULL);
            return FALSE;
        }
        sAtlasCapacity = needed;
    }

    memset(sAtlas, 0, (size_t)needed);

    // The second plane, when there is something to draw over.
    const S32 wantOverlay = sOverlayOn && source != NULL && source->coverage != NULL &&
                            source->width > 0 && source->height > 0;

    if (wantOverlay)
    {
        if (needed > sOverlayCapacity)
        {
            free(sOverlay);
            sOverlay = (U8*)malloc((size_t)needed);
            if (sOverlay == NULL)
            {
                sOverlayCapacity = 0;
                fontFail(face, "out of memory for the debug overlay", NULL);
                return FALSE;
            }
            sOverlayCapacity = needed;
        }

        memset(sOverlay, 0, (size_t)needed);
    }

    S32 drawn = 0;

    // For the natural fit: the line every glyph sits on, and the one scale the
    // whole face is drawn at.
    //
    // The baseline is the commonest ink bottom across the glyphs. Most letters
    // of any alphabet rest on it, so the mode of that measurement IS it, and a
    // handful of descenders cannot move a mode. The scale comes from the
    // tallest glyph resting there, which is a capital, so matching its height
    // matches the face's cap height to the artwork's.
    S32 baseline = 0;
    F32 faceScale = 0.0f;

    if (sFaces[face].fit == IFONT_FIT_NATURAL)
    {
        S32 votes[256];
        memset(votes, 0, sizeof(votes));

        for (S32 i = 0; i < count; i++)
        {
            const S32 bottom = cells[i].y + cells[i].h;

            if (cells[i].h > 0 && bottom >= 0 && bottom < 256 && ++votes[bottom] > votes[baseline])
            {
                baseline = bottom;
            }
        }

        S32 tallest = 0;
        S32 reference = -1;

        for (S32 i = 0; i < count; i++)
        {
            if (cells[i].y + cells[i].h == baseline && cells[i].h > tallest &&
                stbtt_FindGlyphIndex(&font, (S32)(U8)charset[i]) != 0)
            {
                tallest = cells[i].h;
                reference = i;
            }
        }

        if (reference >= 0)
        {
            int rx0, ry0, rx1, ry1;
            stbtt_GetCodepointBitmapBox(&font, (S32)(U8)charset[reference], 1.0f, 1.0f, &rx0, &ry0,
                                        &rx1, &ry1);

            if (ry1 > ry0)
            {
                faceScale = (F32)(tallest * upscale) / (F32)(ry1 - ry0);
            }
        }

        if (faceScale <= 0.0f)
        {
            // Nothing in the charset to measure against. Filling the boxes is
            // not what was asked for, but it is never blank.
            sFaces[face].fit = IFONT_FIT_BOX;
        }
    }

    // Characters the face does not have, which are drawn from the atlas
    // instead. Worth saying out loud: it is the difference between a font that
    // fits and one that fits except for the character the save screen counts
    // completion in.
    S32 fellBack = 0;
    S32 fellBackLen = 0;
    char fellBackChars[160];
    fellBackChars[0] = '\0';
    bool fromAtlas[127];
    memset(fromAtlas, 0, sizeof(fromAtlas));

    for (S32 i = 0; i < count; i++)
    {
        const iFontCell& cell = cells[i];

        if (cell.w <= 0 || cell.h <= 0)
        {
            // No ink in the atlas either -- a space. Nothing to draw, and the
            // game's own metrics already say so.
            continue;
        }

        // Latin-1: the game's tables are indexed by byte and carry accented
        // characters at 0xC0 and up, which is what those code points mean in
        // Unicode too.
        const S32 codepoint = (S32)(U8)charset[i];

        const S32 slotX = (i % columns) * slotW;
        const S32 slotY = (i / columns) * slotH;

        // Whether the FACE HAS this character, which is a different question
        // from whether its glyph has ink. A font answers for a character it
        // does not have with .notdef, and .notdef is usually a hollow
        // rectangle -- so testing the bitmap for ink says "yes, a glyph" and
        // puts a box on screen where the game had a letter. SpongeBoyTT1 has no
        // %, and the save screen counts in them.
        if (stbtt_FindGlyphIndex(&font, codepoint) == 0)
        {
            // Fall back to the glyph being replaced, at the magnification the
            // atlas would have been drawn at anyway. The alternative is a hole
            // in the text; this way a face missing a few characters costs their
            // sharpness and nothing else.
            if (source != NULL)
            {
                blitScaled(source->coverage, source->width, cell.cellX + cell.x,
                           cell.cellY + cell.y, cell.w, cell.h, sAtlas, w,
                           slotX + cell.x * upscale, slotY + cell.y * upscale, cell.w * upscale,
                           cell.h * upscale);

                // And into the overlay, so that a glyph taken from the atlas is
                // compared against the atlas rather than against nothing. Left
                // out, every fallback counted as ink the original did not have
                // and dragged the fit of any face missing a character.
                if (wantOverlay)
                {
                    blitScaled(source->coverage, source->width, cell.cellX + cell.x,
                               cell.cellY + cell.y, cell.w, cell.h, sOverlay, w,
                               slotX + cell.x * upscale, slotY + cell.y * upscale,
                               cell.w * upscale, cell.h * upscale);
                }
            }

            // Escaped, because more than half of this charset is Latin-1
            // accents: printing those raw puts whatever the console makes of a
            // high byte in the middle of the list and the readable ones are
            // lost in it.
            if (fellBackLen + 5 < (S32)sizeof(fellBackChars))
            {
                fellBackLen += snprintf(fellBackChars + fellBackLen,
                                        sizeof(fellBackChars) - (size_t)fellBackLen,
                                        (codepoint >= 0x20 && codepoint < 0x7F) ? "%c" : "\\x%02X",
                                        codepoint);
            }

            if (i < (S32)(sizeof(fromAtlas) / sizeof(fromAtlas[0])))
            {
                fromAtlas[i] = true;
            }

            fellBack++;
            continue;
        }

        // The outline's own ink box at unit scale, so it can be stretched to
        // exactly the box the atlas glyph occupied.
        int ux0, uy0, ux1, uy1;
        stbtt_GetCodepointBitmapBox(&font, codepoint, 1.0f, 1.0f, &ux0, &uy0, &ux1, &uy1);

        if (ux1 <= ux0 || uy1 <= uy0)
        {
            // A glyph with no ink -- a space. Nothing to draw, and the game's
            // metrics already say how wide it is.
            continue;
        }

        // Inset by the fringe find_bounds counted as ink. See iFont.h.
        const S32 pad = (S32)(padding * (F32)upscale + 0.5f);

        S32 targetW = cell.w * upscale - pad * 2;
        S32 targetH = cell.h * upscale - pad * 2;

        if (targetW < 1)
        {
            targetW = 1;
        }
        if (targetH < 1)
        {
            targetH = 1;
        }

        // Per glyph, and separately per axis: this is what makes a letter land
        // in the same box the artwork had it in, whatever the outline's own
        // proportions are. A face whose M is relatively wider than the atlas's
        // is squeezed by that difference rather than pushing everything after
        // it along the line.
        F32 scaleX = (F32)targetW / (F32)(ux1 - ux0);
        const F32 scaleY = (F32)targetH / (F32)(uy1 - uy0);

        // Unless the face is to keep its own proportions, in which case the
        // height is what it is scaled by and only the width follows. Measured:
        // this leaves every baseline-resting glyph exactly on the baseline,
        // which is where a shared-baseline placement from the face's own
        // metrics did NOT put them -- rounding and per-glyph overshoot moved
        // each letter off the line by a few pixels.
        S32 inset = 0;
        F32 scale = scaleY;
        S32 dstX;
        S32 dstY;

        if (sFaces[face].fit == IFONT_FIT_WIDTH)
        {
            S32 natural = (S32)((F32)(ux1 - ux0) * scaleY + 0.5f);

            if (natural < 1)
            {
                natural = 1;
            }

            // Never wider than the box: the game samples exactly that rect out
            // of the texture, so anything wider is simply cut off.
            if (natural > targetW)
            {
                natural = targetW;
            }

            inset = (targetW - natural) / 2;
            scaleX = (F32)natural / (F32)(ux1 - ux0);
            targetW = natural;
        }

        if (sFaces[face].fit == IFONT_FIT_NATURAL)
        {
            // The glyph at the face's own scale, placed by its own metrics
            // against the shared baseline. gy0 is measured from that line and
            // is negative above it, so this is the letter's own rise and drop
            // rather than the box's.
            int gx0, gy0, gx1, gy1;
            stbtt_GetCodepointBitmapBox(&font, codepoint, faceScale, faceScale, &gx0, &gy0, &gx1,
                                        &gy1);

            targetW = gx1 - gx0;
            targetH = gy1 - gy0;
            scaleX = faceScale;
            scale = faceScale;

            if (targetW < 1 || targetH < 1)
            {
                continue;
            }

            // Centred in the ink box the atlas had, because that rect is
            // exactly what the game samples out of the texture.
            dstX = slotX + cell.x * upscale + (cell.w * upscale - targetW) / 2;
            dstY = slotY + baseline * upscale + gy0;

            // Inside its own slot whatever the face does, so a tall or wide
            // letter is clipped rather than written over its neighbour.
            if (dstX < slotX)
            {
                dstX = slotX;
            }
            if (dstY < slotY)
            {
                dstY = slotY;
            }
            if (targetW > slotX + slotW - dstX)
            {
                targetW = slotX + slotW - dstX;
            }
            if (targetH > slotY + slotH - dstY)
            {
                targetH = slotY + slotH - dstY;
            }

            if (targetW < 1 || targetH < 1)
            {
                continue;
            }
        }
        else
        {
            dstX = slotX + cell.x * upscale + pad + inset;
            dstY = slotY + cell.y * upscale + pad;
        }

        if (dstX < 0 || dstY < 0 || dstX + targetW > w || dstY + targetH > h)
        {
            continue;
        }

        stbtt_MakeCodepointBitmap(&font, sAtlas + (size_t)dstY * w + dstX, targetW, targetH, w,
                                  scaleX, scale, codepoint);

        // Weight, in the atlas pixels the setting is written in, at the
        // resolution this is being drawn at.
        F32 remaining = sFaces[face].weight * (F32)upscale;

        while (remaining > 0.0f)
        {
            dilate(sAtlas + (size_t)dstY * w + dstX, targetW, targetH, w,
                   remaining < 1.0f ? remaining : 1.0f);
            remaining -= 1.0f;
        }


        drawn++;

        if (wantOverlay)
        {
            // The glyph this one replaces, stretched into the same box by the
            // same two factors. Nearest neighbour: this is drawn magnified
            // several times over and a filter would only soften the edge that
            // is being compared.
            // Into the box the atlas has it in, not into wherever the
            // substitute landed: the question the overlay answers is whether
            // the two coincide, so the original has to be drawn where it is.
            blitScaled(source->coverage, source->width, cell.cellX + cell.x, cell.cellY + cell.y,
                       cell.w, cell.h, sOverlay, w, slotX + cell.x * upscale,
                       slotY + cell.y * upscale, cell.w * upscale, cell.h * upscale);
        }
    }

    if (drawn == 0)
    {
        fontFail(face, "the font has none of the characters the game asks for", NULL);
        return FALSE;
    }

    if (fellBack > 0 && !sFaces[face].saidWhatIsMissing)
    {
        sFaces[face].saidWhatIsMissing = TRUE;
        printf("bfbb: %s has no %s%s -- %d of %d drawn from the game's own atlas\n",
               faceName(face), fellBackChars,
               fellBackLen + 5 >= (S32)sizeof(fellBackChars) ? "..." : "", (int)fellBack,
               (int)count);
        fflush(stdout);
    }

    if (wantOverlay)
    {
        // How much of the two letterforms lands on the same pixels; see
        // iFontOverlayAgreement.
        //
        // Slot by slot, and only the slots the face supplied. A glyph taken
        // from the atlas IS the atlas, so it agrees with itself perfectly and
        // would score a face for every character it does not have -- which
        // would rank a face with half the alphabet missing above one that has
        // all of it.
        double both = 0.0;
        double either = 0.0;
        double mine = 0.0;
        double theirs = 0.0;

        for (S32 i = 0; i < count; i++)
        {
            if (i < (S32)(sizeof(fromAtlas) / sizeof(fromAtlas[0])) && fromAtlas[i])
            {
                continue;
            }

            const S32 slotX = (i % columns) * slotW;
            const S32 slotY = (i / columns) * slotH;

            for (S32 y = 0; y < slotH; y++)
            {
                const size_t row = (size_t)(slotY + y) * w + slotX;

                for (S32 x = 0; x < slotW; x++)
                {
                    const U8 a = sAtlas[row + x];
                    const U8 b = sOverlay[row + x];
                    both += a < b ? a : b;
                    either += a > b ? a : b;
                    mine += a;
                    theirs += b;
                }
            }
        }

        sAgreement = (F32)(either > 0.0 ? 100.0 * both / either : 0.0);
        sInk = (F32)(theirs > 0.0 ? mine / theirs : 0.0);
    }

    sSubstituted = count - fellBack;
    sGlyphs = count;

    *pixels = sAtlas;
    if (overlay != NULL)
    {
        *overlay = wantOverlay ? sOverlay : NULL;
    }
    *width = w;
    *height = h;
    *slotStride = slotW;
    *perRow = columns;
    return TRUE;
}

S32 iFontAutoFit(iFontFace face, const char* charset, S32 count, S32 cellW, S32 cellH,
                 const iFontCell* cells, S32 upscale, const iFontAtlas* source, F32* padding,
                 F32* weight)
{
    F32 resolvedPadding = sPadding;
    F32 resolvedWeight = iFontWeight(face);

    const S32 searchPadding = sPaddingAuto;
    const S32 searchWeight = iFontWeightAuto(face);

    S32 resolved = FALSE;

    if (validFace(face) && (searchPadding || searchWeight) && iFontAvailable(face))
    {
        face_state& f = sFaces[face];

        if (upscale < 1)
        {
            upscale = 1;
        }

        if (f.tuned && f.tunedUpscale == upscale)
        {
            resolvedPadding = f.tunedPadding;
            resolvedWeight = f.tunedWeight;
            resolved = TRUE;
        }
        else if (source != NULL && source->coverage != NULL && source->width > 0 &&
                 source->height > 0)
        {
            // The insets that are actually distinct at this size. Padding is
            // applied as a whole number of raster pixels, so at upscale 3 there
            // are three settings between one atlas pixel and none and asking for
            // anything between them measures the same font twice.
            F32 pads[8];
            S32 padCount = 1;

            if (searchPadding)
            {
                padCount = upscale < 4 ? upscale + 1 : 5;

                if (padCount > (S32)(sizeof(pads) / sizeof(pads[0])))
                {
                    padCount = (S32)(sizeof(pads) / sizeof(pads[0]));
                }

                for (S32 i = 0; i < padCount; i++)
                {
                    pads[i] = (F32)i / (F32)upscale;
                }
            }
            else
            {
                pads[0] = sPadding;
            }

            // Fine where it matters and coarse where it does not: past about
            // half an atlas pixel the strokes have merged and the difference
            // between one setting and the next is a blob either way.
            static const F32 kWeights[] = { 0.0f, 0.05f, 0.1f, 0.15f, 0.2f,
                                            0.3f, 0.4f,  0.6f, 0.8f };

            const S32 weightCount =
                searchWeight ? (S32)(sizeof(kWeights) / sizeof(kWeights[0])) : 1;

            const F32 savedPadding = sPadding;
            const F32 savedWeight = f.weight;
            const S32 savedOverlay = sOverlayOn;

            // The agreement is only measured when the overlay plane is being
            // built, and that is the whole point of the pass.
            sOverlayOn = TRUE;

            F32 bestPadding = pads[0];
            F32 bestWeight = searchWeight ? kWeights[0] : savedWeight;
            F32 best = -1.0f;

            // The same again ignoring the ink ceiling, for a face already
            // heavier than the atlas at its lightest -- there is still a best
            // inset, it just is not one that got there by matching the weight.
            F32 lightPadding = pads[0];
            F32 light = -1.0f;

            for (S32 pi = 0; pi < padCount; pi++)
            {
                for (S32 wi = 0; wi < weightCount; wi++)
                {
                    const F32 w = searchWeight ? kWeights[wi] : savedWeight;

                    f.weight = w;

                    const U8* pixels = NULL;
                    const U8* overlay = NULL;
                    S32 aw = 0;
                    S32 ah = 0;
                    S32 stride = 0;
                    S32 columns = 0;

                    if (!iFontRasterize(face, charset, count, cellW, cellH, cells, upscale,
                                        pads[pi], source, &pixels, &overlay, &aw, &ah, &stride,
                                        &columns))
                    {
                        continue;
                    }

                    // Past this the substitute is laying down substantially more
                    // ink than the artwork does, which means its letters are
                    // filling their boxes: everything the original has is
                    // covered, the union is the box, and agreement settles at a
                    // number that looks like a fit and is a blob. See
                    // iFontOverlayInk.
                    const F32 kInkCeiling = 1.10f;

                    if (sInk <= kInkCeiling && sAgreement > best)
                    {
                        best = sAgreement;
                        bestPadding = pads[pi];
                        bestWeight = w;
                    }

                    if (wi == 0 && sAgreement > light)
                    {
                        light = sAgreement;
                        lightPadding = pads[pi];
                    }
                }
            }

            if (best < 0.0f)
            {
                bestPadding = lightPadding;
                bestWeight = searchWeight ? kWeights[0] : savedWeight;
                best = light;
            }

            sOverlayOn = savedOverlay;
            sPadding = savedPadding;
            f.weight = savedWeight;

            if (best >= 0.0f)
            {
                f.tuned = TRUE;
                f.tunedUpscale = upscale;
                f.tunedPadding = bestPadding;
                f.tunedWeight = bestWeight;

                resolvedPadding = bestPadding;
                resolvedWeight = bestWeight;
                resolved = TRUE;

                printf("bfbb: %s fits the atlas best at font_padding %.2f, font_weight "
                       "%.2f -- %.0f%% of the ink lands on the artwork's\n",
                       faceName(face), (double)bestPadding, (double)bestWeight, (double)best);
                fflush(stdout);
            }
        }
    }

    if (padding != NULL)
    {
        *padding = resolvedPadding;
    }
    if (weight != NULL)
    {
        *weight = resolvedWeight;
    }

    return resolved;
}
