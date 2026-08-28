// TrueType glyphs for the game's fonts. The argument for it is in iFont.h.

#include "iFont.h"

#include <rwcore.h>

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
    // The file, kept for the life of the process. stb_truetype does not copy it
    // -- stbtt_fontinfo points into these bytes and every glyph is read from
    // them on demand -- so freeing this would invalidate the font.
    U8* sFile;
    S32 sFileSize;

    stbtt_fontinfo sFont;
    S32 sLoaded;
    F32 sScale = 1.0f;
    S32 sFailed;

    // The atlas. One allocation, reused: the game builds four fonts at startup
    // and never again, so this is only ever grown to the largest of them.
    U8* sAtlas;
    S32 sAtlasCapacity;
    S32 sAtlasWidth;
    S32 sAtlasHeight;

    // A gap between glyphs, so that the linear filter the game draws text with
    // cannot pull a neighbour's ink into the edge of a character. One pixel is
    // enough at 1:1 and this is drawn magnified, so it is two.
    const S32 kPadding = 2;

    void fontFail(const char* what, const char* detail)
    {
        sFailed = 1;
        printf("bfbb: the TrueType font is off -- %s%s%s\n", what, detail != NULL ? ": " : "",
               detail != NULL ? detail : "");
        fflush(stdout);
    }
}

S32 iFontLoad(const char* path)
{
    if (sLoaded || sFailed)
    {
        return sLoaded;
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
        fontFail("no such file", path);
        return FALSE;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0)
    {
        fclose(f);
        fontFail("the file is empty", path);
        return FALSE;
    }

    sFile = (U8*)malloc((size_t)size);
    if (sFile == NULL)
    {
        fclose(f);
        fontFail("out of memory reading", path);
        return FALSE;
    }

    size_t got = fread(sFile, 1, (size_t)size, f);
    fclose(f);

    if (got != (size_t)size)
    {
        free(sFile);
        sFile = NULL;
        fontFail("the file could not be read", path);
        return FALSE;
    }

    sFileSize = (S32)size;

    // Offset 0: the first font of a collection, which for a plain .ttf is the
    // only one. A .ttc would need a choice, and nothing asks for one.
    if (!stbtt_InitFont(&sFont, sFile, stbtt_GetFontOffsetForIndex(sFile, 0)))
    {
        free(sFile);
        sFile = NULL;
        fontFail("not a TrueType font", path);
        return FALSE;
    }

    sLoaded = 1;
    printf("bfbb: text rendered from %s\n", path);
    fflush(stdout);
    return TRUE;
}

S32 iFontAvailable()
{
    return sLoaded;
}

void iFontSetScale(F32 scale)
{
    // A scale of zero or less would divide the cell to nothing. Refused
    // rather than clamped, so a typo is reported instead of silently
    // producing text nobody can read.
    if (scale > 0.0f)
    {
        sScale = scale;
    }
    else
    {
        printf("bfbb: config: text.font_scale must be greater than zero; keeping %.3f\n",
               (double)sScale);
        fflush(stdout);
    }
}

F32 iFontScale()
{
    return sScale;
}

S32 iFontRasterize(const char* charset, S32 count, S32 pixelHeight, char refChar,
                   F32 inkFraction, F32 baselineFraction, F32 scale, const U8** pixels,
                   S32* width, S32* height, iFontGlyph* glyphs, F32* cellHeight,
                   F32* baselineRow)
{
    if (!sLoaded || charset == NULL || glyphs == NULL || count <= 0 || pixelHeight <= 0)
    {
        return FALSE;
    }

    const F32 pxscale = stbtt_ScaleForPixelHeight(&sFont, (float)pixelHeight);

    int ascentI, descentI, lineGapI;
    stbtt_GetFontVMetrics(&sFont, &ascentI, &descentI, &lineGapI);
    const F32 ascent = ascentI * pxscale;
    const F32 descent = descentI * pxscale; // negative, below the baseline

    // **One cell and one baseline for the whole font, measured from the ink.**
    //
    // Not from the font's declared ascent and descent. Those describe a line
    // box with room above and below the letters, and the game's atlas has no
    // such room -- reset_font_spacing measures its cell from the artwork, so a
    // capital fills the cell top to bottom. Measured the same way here, a
    // substituted font sits at the size the game expects rather than inset
    // inside a box the game knows nothing about.
    S32 inkTop = 0;
    S32 inkBottom = 0;
    S32 haveInk = 0;

    for (S32 i = 0; i < count; i++)
    {
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&sFont, (S32)(U8)charset[i], pxscale, pxscale, &x0, &y0, &x1, &y1);

        if (x1 <= x0 || y1 <= y0)
        {
            continue;
        }

        // stb measures y downward from the baseline, so y0 is negative above.
        if (!haveInk || y0 < inkTop)
        {
            inkTop = y0;
        }
        if (!haveInk || y1 > inkBottom)
        {
            inkBottom = y1;
        }
        haveInk = 1;
    }

    if (!haveInk)
    {
        fontFail("the font has none of the characters the game asks for", NULL);
        return FALSE;
    }

    // **The cell comes from the atlas being replaced, when it can be measured.**
    //
    // What decides the apparent size is how much of the cell the ink fills: the
    // drawn quad is always the whole cell, so ink over cell IS how big the text
    // looks. Two rules were tried and both were wrong, because neither has
    // anything to do with the white space an artist left around the capitals --
    // the font's ink box made text a third too large, and its declared line box
    // a few percent too large.
    //
    // So the caller measures its own atlas and passes the ratio, and the cell
    // is padded until this font fills it the same way. The baseline is placed
    // by the same measurement.
    // The reference glyph, measured the same way the caller measured it in the
    // atlas. inkTop/inkBottom above are the whole character set and are used
    // only to keep anything from being clipped.
    int refX0 = 0, refY0 = 0, refX1 = 0, refY1 = 0;
    stbtt_GetCodepointBitmapBox(&sFont, (S32)(U8)refChar, pxscale, pxscale, &refX0, &refY0, &refX1,
                                &refY1);

    const S32 refInk = refY1 - refY0;

    S32 baseline = -inkTop;
    S32 cell = inkBottom - inkTop;

    if (inkFraction > 0.0f && inkFraction <= 1.0f && refInk > 0)
    {
        // The reference glyph must end up filling the same share of the cell it
        // fills in the atlas, and sitting at the same height in it.
        cell = (S32)((F32)refInk / inkFraction + 0.5f);

        if (baselineFraction > 0.0f && baselineFraction <= 1.0f)
        {
            // Where the glyph's ink BOTTOM should land, minus how far that is
            // below the baseline -- which for a capital is nothing.
            baseline = (S32)((F32)cell * baselineFraction + 0.5f) - refY1;
        }
        else
        {
            baseline = -refY0;
        }
    }
    else
    {
        // Nothing measured: the font's own declared line box, which is the
        // right shape of thing even when it is not the right size.
        baseline = (S32)(ascent + 0.5f);
        if (-inkTop > baseline)
        {
            baseline = -inkTop;
        }

        S32 below = (S32)(-descent + 0.5f);
        if (inkBottom > below)
        {
            below = inkBottom;
        }
        cell = baseline + below;
    }

    // Never clip: the ink has to fit above and below wherever the baseline
    // ended up.
    if (baseline < -inkTop)
    {
        baseline = -inkTop;
    }
    if (cell < baseline + inkBottom)
    {
        cell = baseline + inkBottom;
    }

    // The caller's nudge, for taste. Padding the cell shrinks the ink's share
    // of it, and the baseline moves with it so the text does not drift.
    if (scale > 0.0f && scale != 1.0f)
    {
        cell = (S32)((F32)cell / scale + 0.5f);
        baseline = (S32)((F32)baseline / scale + 0.5f);
    }

    // Widest ink, so the slots are all the same and the layout is a grid.
    S32 widest = 1;
    for (S32 i = 0; i < count; i++)
    {
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&sFont, (S32)(U8)charset[i], pxscale, pxscale, &x0, &y0, &x1, &y1);
        if (x1 - x0 > widest)
        {
            widest = x1 - x0;
        }
    }

    const S32 slotW = widest + kPadding * 2;
    const S32 slotH = cell + kPadding * 2;

    S32 perRow = 1;
    while (perRow * perRow < count)
    {
        perRow++;
    }

    const S32 rows = (count + perRow - 1) / perRow;

    S32 w = 1;
    while (w < perRow * slotW)
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
            fontFail("out of memory for the glyph atlas", NULL);
            return FALSE;
        }
        sAtlasCapacity = needed;
    }

    memset(sAtlas, 0, (size_t)needed);
    sAtlasWidth = w;
    sAtlasHeight = h;

    const F32 iw = 1.0f / (F32)w;
    const F32 ih = 1.0f / (F32)h;

    for (S32 i = 0; i < count; i++)
    {
        const S32 slotX = (i % perRow) * slotW + kPadding;
        const S32 slotY = (i / perRow) * slotH + kPadding;

        // Latin-1: the game's tables are indexed by byte and carry accented
        // characters at 0xC0 and up, which is what those code points mean in
        // Unicode too.
        const S32 codepoint = (S32)(U8)charset[i];

        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&sFont, codepoint, pxscale, pxscale, &x0, &y0, &x1, &y1);

        const S32 gw = x1 - x0;
        const S32 gh = y1 - y0;

        if (gw > 0 && gh > 0)
        {
            // Flush left in its slot, and on the shared baseline. Flush left
            // because the game's advance IS the ink width -- it carries no side
            // bearing, and adds letter spacing of its own instead.
            const S32 dstY = slotY + baseline + y0;
            stbtt_MakeCodepointBitmap(&sFont, sAtlas + dstY * w + slotX, gw, gh, w, pxscale, pxscale,
                                      codepoint);
        }

        // The full cell height, and the ink's width. Every glyph's rect is the
        // same height for the same reason the game's is.
        glyphs[i].u0 = (F32)slotX * iw;
        glyphs[i].v0 = (F32)slotY * ih;
        glyphs[i].u1 = (F32)(slotX + (gw > 0 ? gw : 0)) * iw;
        glyphs[i].v1 = (F32)(slotY + cell) * ih;
        glyphs[i].inkWidth = (F32)(gw > 0 ? gw : 0);
    }

    *pixels = sAtlas;
    *width = w;
    *height = h;
    *cellHeight = (F32)cell;
    *baselineRow = (F32)baseline;
    return TRUE;
}

RwTexture* iFontMakeTexture(const U8* coverage, S32 width, S32 height)
{
    if (coverage == NULL || width <= 0 || height <= 0)
    {
        return NULL;
    }

    RwRaster* raster = RwRasterCreate(width, height, 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
    if (raster == NULL)
    {
        fontFail("a texture could not be made for the glyph atlas", NULL);
        return NULL;
    }

    RwUInt8* dst = RwRasterLock(raster, 0, rwRASTERLOCKWRITE | rwRASTERLOCKNOFETCH);
    if (dst == NULL)
    {
        RwRasterDestroy(raster);
        fontFail("the glyph atlas could not be locked", NULL);
        return NULL;
    }

    const S32 stride = raster->stride;

    for (S32 y = 0; y < height; y++)
    {
        RwUInt8* row = dst + (size_t)y * stride;
        const U8* src = coverage + (size_t)y * width;

        for (S32 x = 0; x < width; x++)
        {
            // BGRA, which is what an 8888 raster is on D3D9. White everywhere,
            // so the game's vertex colour is the only thing that tints a glyph,
            // and the shape lives entirely in alpha.
            row[x * 4 + 0] = 0xFF;
            row[x * 4 + 1] = 0xFF;
            row[x * 4 + 2] = 0xFF;
            row[x * 4 + 3] = src[x];
        }
    }

    RwRasterUnlock(raster);

    RwTexture* texture = RwTextureCreate(raster);
    if (texture == NULL)
    {
        RwRasterDestroy(raster);
        fontFail("a texture could not be made for the glyph atlas", NULL);
        return NULL;
    }

    // Linear, and clamped. The game sets the filter on its own font textures
    // too (init_font_data does it right after the asset lookup); clamping is
    // what stops the padding row at the edge of the atlas wrapping around.
    texture->filterAddressing = (texture->filterAddressing & 0xFFFFFF00) | rwFILTERLINEAR;

    return texture;
}
