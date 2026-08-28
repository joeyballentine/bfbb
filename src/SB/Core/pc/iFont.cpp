// TrueType glyphs for the game's fonts. The argument for it is in iFont.h.

#include "iFont.h"

#include "iScreen.h"

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
    S32 sUpscale;   // 0 is automatic; see iFontUpscale
    F32 sPadding = 0.5f;
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

    // The framebuffer the game's font atlases were authored against.
    const S32 kRetailHeight = 480;

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
    // The settings as well as the path. A setting that is read but never
    // reaches the pixels looks exactly like one that works, and this port has
    // already shipped one of those -- text.font_spacing was missing from the
    // settings table, so every value anyone tried was silently the default.
    printf("bfbb: text rendered from %s (upscale %d, padding %.3f, inset %d px)\n", path,
           (int)iFontUpscale(), (double)sPadding, (int)((F32)iFontUpscale() * sPadding + 0.5f));
    fflush(stdout);
    return TRUE;
}

S32 iFontAvailable()
{
    return sLoaded;
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

S32 iFontRasterize(const char* charset, S32 count, S32 cellW, S32 cellH, const iFontCell* cells,
                   S32 upscale, F32 padding, const U8** pixels, S32* width, S32* height,
                   S32* slotStride, S32* perRow)
{
    if (!sLoaded || charset == NULL || cells == NULL || count <= 0 || cellW <= 0 || cellH <= 0)
    {
        return FALSE;
    }

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
            fontFail("out of memory for the glyph atlas", NULL);
            return FALSE;
        }
        sAtlasCapacity = needed;
    }

    memset(sAtlas, 0, (size_t)needed);

    S32 drawn = 0;

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

        // The outline's own ink box at unit scale, so it can be stretched to
        // exactly the box the atlas glyph occupied.
        int ux0, uy0, ux1, uy1;
        stbtt_GetCodepointBitmapBox(&sFont, codepoint, 1.0f, 1.0f, &ux0, &uy0, &ux1, &uy1);

        if (ux1 <= ux0 || uy1 <= uy0)
        {
            // The font has no such glyph. The cell stays empty, which draws
            // nothing -- better than a wrong letter, and the layout is
            // unaffected because the metrics are still the atlas's.
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
        const F32 scaleX = (F32)targetW / (F32)(ux1 - ux0);
        const F32 scaleY = (F32)targetH / (F32)(uy1 - uy0);

        const S32 slotX = (i % columns) * slotW;
        const S32 slotY = (i / columns) * slotH;

        const S32 dstX = slotX + cell.x * upscale + pad;
        const S32 dstY = slotY + cell.y * upscale + pad;

        if (dstX < 0 || dstY < 0 || dstX + targetW > w || dstY + targetH > h)
        {
            continue;
        }

        stbtt_MakeCodepointBitmap(&sFont, sAtlas + (size_t)dstY * w + dstX, targetW, targetH, w,
                                  scaleX, scaleY, codepoint);
        drawn++;
    }

    if (drawn == 0)
    {
        fontFail("the font has none of the characters the game asks for", NULL);
        return FALSE;
    }

    *pixels = sAtlas;
    *width = w;
    *height = h;
    *slotStride = slotW;
    *perRow = columns;
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
