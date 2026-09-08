// The glyph atlas as a texture the game can draw with. Split from iFont.cpp so
// that the rasteriser links without a renderer -- tools/fontfit sweeps the same
// code offline, and dragging RenderWare into that would mean standing up a
// graphics device to answer a question about pixels in a buffer.

#include "iFont.h"

#include <rwcore.h>

#include <stdio.h>

namespace
{
    void textureFail(const char* what)
    {
        printf("bfbb: the glyph atlas is off -- %s\n", what);
        fflush(stdout);
    }

    // Which way up the rows go, and which way round the colour bytes are.
    //
    // A raster holds its rows the way librw's own rasterFromImage writes them,
    // and the two backends disagree. d3d/d3d.cpp walks the image top-down;
    // gl/gl3raster.cpp starts at the LAST row and walks backwards, because the
    // GL3 fragment shaders sample 1.0 - v to put the picture the same way up
    // D3D's texture space has it. Neither is wrong -- but anything that fills a
    // raster by hand has to write the way the backend it is linked against
    // reads, and there is nothing at the lock that says which that is.
    //
    // Written top-down on GL3, the atlas lands upside down under that flip. The
    // glyphs are in the top half of a 512x512 atlas, so the game sampled the
    // empty half and drew nothing at all on the menus.
    //
    // The colour order goes the same way: an 8888 raster is BGRA on D3D and
    // RGBA on GL3. That does not show on a glyph, whose three colour channels
    // are all 0xFF, but the overlay below writes real colours.
#if defined(RW_GL3)
    const bool kRowsBottomUp = true;
    const S32 kRedByte = 0;
    const S32 kBlueByte = 2;
#else
    const bool kRowsBottomUp = false;
    const S32 kRedByte = 2;
    const S32 kBlueByte = 0;
#endif
    const S32 kGreenByte = 1;
    const S32 kAlphaByte = 3;
}

RwTexture* iFontMakeTexture(const U8* coverage, const U8* overlay, S32 width, S32 height)
{
    if (coverage == NULL || width <= 0 || height <= 0)
    {
        return NULL;
    }

    RwRaster* raster = RwRasterCreate(width, height, 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
    if (raster == NULL)
    {
        textureFail("a texture could not be made for it");
        return NULL;
    }

    RwUInt8* dst = RwRasterLock(raster, 0, rwRASTERLOCKWRITE | rwRASTERLOCKNOFETCH);
    if (dst == NULL)
    {
        RwRasterDestroy(raster);
        textureFail("it could not be locked");
        return NULL;
    }

    const S32 stride = raster->stride;

    for (S32 y = 0; y < height; y++)
    {
        const S32 dstY = kRowsBottomUp ? (height - 1 - y) : y;

        RwUInt8* row = dst + (size_t)dstY * stride;
        const U8* src = coverage + (size_t)y * width;

        const U8* old = overlay != NULL ? overlay + (size_t)y * width : NULL;

        for (S32 x = 0; x < width; x++)
        {
            // White everywhere, so the game's vertex colour is the only thing
            // that tints a glyph, and the shape lives entirely in alpha.
            row[x * 4 + kRedByte] = 0xFF;
            row[x * 4 + kGreenByte] = 0xFF;
            row[x * 4 + kBlueByte] = 0xFF;
            row[x * 4 + kAlphaByte] = src[x];

            if (old != NULL)
            {
                // The debug overlay, and the signal is in ALPHA rather than in
                // a colour: the game multiplies every glyph by its text colour,
                // so a channel is not something this can rely on -- the
                // copyright screen's yellow zeroes blue outright. Alpha is
                // multiplied only by the text's own alpha, which is opaque.
                //
                // So what is drawn is the DISAGREEMENT between the two
                // letterforms. Where the outline and the glyph it replaces
                // cover the same pixel they cancel and nothing is drawn; where
                // one has ink and the other does not it lights up. A glyph the
                // substitute sizes correctly nearly vanishes, and what is left
                // on screen is the error -- which is what font_padding moves.
                //
                // The colour is a second cue for anyone whose text is light:
                // green where the outline is the one with ink, red where the
                // atlas is.
                const S32 d = (S32)src[x] - (S32)old[x];

                row[x * 4 + kBlueByte] = 0x20;
                row[x * 4 + kGreenByte] = d > 0 ? 0xFF : 0x40;
                row[x * 4 + kRedByte] = d > 0 ? 0x40 : 0xFF;
                row[x * 4 + kAlphaByte] = (U8)(d < 0 ? -d : d);
            }
        }
    }

    RwRasterUnlock(raster);

    RwTexture* texture = RwTextureCreate(raster);
    if (texture == NULL)
    {
        RwRasterDestroy(raster);
        textureFail("a texture could not be made for it");
        return NULL;
    }

    // Linear, and clamped. The game sets the filter on its own font textures
    // too (init_font_data does it right after the asset lookup); clamping is
    // what stops the padding row at the edge of the atlas wrapping around.
    texture->filterAddressing = (texture->filterAddressing & 0xFFFFFF00) | rwFILTERLINEAR;

    return texture;
}
