// Fit a TrueType face to the atlas it stands in for, without the game.
//
// `[text] font_padding` and `[text] font_weight` are tuned by their effect on
// one thing: how much of the substituted letterform lands on the ink of the
// glyph it replaces. That is a number, and asking the game for it means a
// launch, a load and a look per value. This reads the atlas out of a
// BFBB_FONTDUMP file, rasterises the same face through the same iFontRasterize
// the game uses, and sweeps both settings in a second.
//
//     bfbb.exe with BFBB_FONTDUMP=fonts.bin        (once, to capture the atlas)
//     fontfit fonts.bin myfont.ttf                 (as often as you like)
//
// The fit is coverage agreement: the ink both letterforms share over the ink
// either of them has. 100% would be the same glyph twice. It is a guide and not
// a verdict -- a different typeface is a different typeface, and the point is to
// find where a face stops disagreeing about SIZE and WEIGHT so that what is left
// is only the shape you chose it for.

#include "iFont.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// iFont asks for this to size a glyph against the screen. Offline there is no
// screen, so every sweep is run at an upscale named on the command line.
S32 iScreenHeight()
{
    return 0;
}

// And this to find the system's own sans serif. Nothing here asks for one --
// the face is named on the command line -- but iFont.cpp is linked whole, so
// the symbol has to resolve. Answered properly rather than stubbed false:
// a stub that lies is a trap for whoever calls it next.
bool iHostPathExists(const char* path)
{
    FILE* f = fopen(path, "rb");

    if (f == NULL)
    {
        return false;
    }

    fclose(f);
    return true;
}

namespace
{
    struct dumped_font
    {
        char name[32];
        S32 count;
        S32 cellW;
        S32 cellH;
        char charset[128];
        iFontCell cells[127];
        iFontAtlas atlas;
        U8* coverage;
    };

    const S32 kMaxFonts = 8;

    dumped_font sFonts[kMaxFonts];
    S32 sFontCount;

    S32 readFonts(const char* path)
    {
        FILE* f = fopen(path, "rb");
        if (f == NULL)
        {
            printf("fontfit: no such dump: %s\n", path);
            return 0;
        }

        while (sFontCount < kMaxFonts)
        {
            char magic[4];
            if (fread(magic, 1, 4, f) != 4)
            {
                break;
            }

            if (memcmp(magic, "BFFD", 4) != 0)
            {
                printf("fontfit: %s is not a font dump\n", path);
                break;
            }

            dumped_font& d = sFonts[sFontCount];
            S32 header[5];

            if (fread(d.name, 1, sizeof(d.name), f) != sizeof(d.name) ||
                fread(header, sizeof(S32), 5, f) != 5)
            {
                break;
            }

            d.count = header[0];
            d.cellW = header[1];
            d.cellH = header[2];
            d.atlas.width = header[3];
            d.atlas.height = header[4];

            if (d.count <= 0 || d.count > 127 || d.atlas.width <= 0 || d.atlas.height <= 0)
            {
                printf("fontfit: %s holds a record that does not make sense\n", path);
                break;
            }

            const size_t pixels = (size_t)d.atlas.width * (size_t)d.atlas.height;
            d.coverage = (U8*)malloc(pixels);

            if (d.coverage == NULL ||
                fread(d.charset, 1, (size_t)d.count, f) != (size_t)d.count ||
                fread(d.cells, sizeof(iFontCell), (size_t)d.count, f) != (size_t)d.count ||
                fread(d.coverage, 1, pixels, f) != pixels)
            {
                printf("fontfit: %s ends in the middle of a record\n", path);
                break;
            }

            d.atlas.coverage = d.coverage;
            sFontCount++;
        }

        fclose(f);
        return sFontCount;
    }

    struct fit
    {
        double agreement;
        double ink;
    };

    // One rasterisation at the settings currently in iFont, measured two ways.
    fit measure(const dumped_font& d, S32 upscale)
    {
        const U8* pixels = NULL;
        const U8* overlay = NULL;
        S32 width = 0;
        S32 height = 0;
        S32 slotStride = 0;
        S32 columns = 0;

        fit f;
        f.agreement = -1.0;
        f.ink = 0.0;

        if (!iFontRasterize(IFONT_FACE_SB, d.charset, d.count, d.cellW, d.cellH, d.cells, upscale,
                            iFontPadding(), &d.atlas, &pixels, &overlay, &width, &height,
                            &slotStride, &columns) ||
            overlay == NULL)
        {
            return f;
        }

        f.agreement = (double)iFontOverlayAgreement();
        f.ink = (double)iFontOverlayInk();
        return f;
    }
}

    // Where the ink actually ended up in one slot, in slot pixels.
    bool inkBox(const U8* pixels, S32 stride, S32 slotX, S32 slotY, S32 slotW, S32 slotH, S32& bx,
                S32& by, S32& bw, S32& bh)
    {
        S32 minx = slotW;
        S32 miny = slotH;
        S32 maxx = -1;
        S32 maxy = -1;

        for (S32 y = 0; y < slotH; y++)
        {
            const U8* row = pixels + (size_t)(slotY + y) * stride + slotX;

            for (S32 x = 0; x < slotW; x++)
            {
                // Anything above the anti-aliased fringe, so that a glyph's
                // edge ramp does not widen every box by a pixel.
                if (row[x] > 32)
                {
                    if (x < minx) minx = x;
                    if (x > maxx) maxx = x;
                    if (y < miny) miny = y;
                    if (y > maxy) maxy = y;
                }
            }
        }

        if (maxx < minx || maxy < miny)
        {
            return false;
        }

        bx = minx;
        by = miny;
        bw = maxx + 1 - minx;
        bh = maxy + 1 - miny;
        return true;
    }

    // Per glyph: where the substitute's ink landed against the box the atlas
    // had it in. Both are measured the same way in the same slot, so a number
    // here is a real displacement and not a difference of convention.
    void reportGlyphs(const dumped_font& d, S32 upscale)
    {
        const U8* pixels = NULL;
        const U8* overlay = NULL;
        S32 width = 0, height = 0, slotStride = 0, columns = 0;

        if (!iFontRasterize(IFONT_FACE_SB, d.charset, d.count, d.cellW, d.cellH, d.cells, upscale,
                            iFontPadding(), &d.atlas, &pixels, &overlay, &width, &height,
                            &slotStride, &columns))
        {
            return;
        }

        const S32 slotH = d.cellH * upscale;
        double sumTop = 0.0, sumBottom = 0.0, sumLeft = 0.0, sumRight = 0.0;
        double sumBottomAbs = 0.0;
        double bottoms[127];
        S32 measured = 0;
        S32 onLine = 0;

        // The atlas's own baseline, found the same way iFont finds it.
        S32 votes[256];
        S32 baseline = 0;
        memset(votes, 0, sizeof(votes));
        for (S32 i = 0; i < d.count; i++)
        {
            const S32 b = d.cells[i].y + d.cells[i].h;
            if (d.cells[i].h > 0 && b >= 0 && b < 256 && ++votes[b] > votes[baseline])
            {
                baseline = b;
            }
        }

        printf("\n  glyph   atlas box (x,y,w,h)      substitute        off\n");

        for (S32 i = 0; i < d.count; i++)
        {
            const S32 slotX = (i % columns) * slotStride;
            const S32 slotY = (i / columns) * slotH;

            S32 bx, by, bw, bh;
            if (!inkBox(pixels, width, slotX, slotY, slotStride, slotH, bx, by, bw, bh))
            {
                continue;
            }

            const S32 ax = d.cells[i].x * upscale;
            const S32 ay = d.cells[i].y * upscale;
            const S32 aw = d.cells[i].w * upscale;
            const S32 ah = d.cells[i].h * upscale;

            // Only the glyphs the ATLAS rests on the baseline. A descender
            // belongs below it and a round letter overshoots it, so counting
            // those measures the alphabet rather than the alignment.
            if (onLine < 127 && d.cells[i].y + d.cells[i].h == baseline)
            {
                bottoms[onLine] = (by + bh) - baseline * upscale;
                sumBottomAbs += bottoms[onLine];
                onLine++;
            }

            sumLeft += bx - ax;
            sumTop += by - ay;
            sumRight += (bx + bw) - (ax + aw);
            sumBottom += (by + bh) - (ay + ah);
            measured++;

            const int c = (unsigned char)d.charset[i];
            char label[8];
            if (c >= 0x20 && c < 0x7F) { label[0] = (char)c; label[1] = 0; }
            else { snprintf(label, sizeof(label), "\\x%02X", c); }
            const bool loud = (by - ay) * (by - ay) > upscale * upscale * 4 ||
                              (by + bh - ay - ah) * (by + bh - ay - ah) > upscale * upscale * 4;

            if (loud)
            {
                printf("  %-6s  %4d %4d %4d %4d      %4d %4d %4d %4d   dy %+d  dbot %+d\n",
                       label, ax, ay, aw, ah, bx,
                       by, bw, bh, by - ay, (by + bh) - (ay + ah));
            }
        }

        if (measured > 0)
        {
            // Where the ink bottoms land relative to the atlas's baseline. A
            // line of type has ONE baseline, so the spread here is the number
            // that says whether the letters sit on it or bounce around it.
            const double mean = onLine > 0 ? sumBottomAbs / onLine : 0.0;
            double var = 0.0;

            for (S32 k = 0; k < onLine; k++)
            {
                var += (bottoms[k] - mean) * (bottoms[k] - mean);
            }

            printf("\n  the %d glyphs that rest on the baseline: mean %+.2f from it, spread %.2f px\n",
                   (int)onLine, mean, onLine > 0 ? sqrt(var / onLine) : 0.0);

            printf("\n  mean offset over %d glyphs, in upscaled pixels:"
                   "  left %+.2f  top %+.2f  right %+.2f  bottom %+.2f\n",
                   (int)measured, sumLeft / measured, sumTop / measured, sumRight / measured,
                   sumBottom / measured);
        }
    }
int main(int argc, char** argv)
{
    if (argc < 3)
    {
        printf("usage: fontfit <dump> <font.ttf> [upscale]\n"
               "\n"
               "  <dump>      a file written by running the game with BFBB_FONTDUMP set\n"
               "  <font.ttf>  the face to fit\n"
               "  [upscale]   what [text] font_upscale resolves to at the resolution you\n"
               "              play at -- the render height over 480, rounded. 3 for 1440p,\n"
               "              4 for 4K. Default 3.\n");
        return 2;
    }

    const S32 upscale = argc > 3 ? (S32)atoi(argv[3]) : 3;

    bool glyphs = false;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--glyphs") == 0)
        {
            glyphs = true;
        }
        if (strcmp(argv[i], "--width") == 0)
        {
            iFontSetFit(IFONT_FACE_SB, IFONT_FIT_WIDTH);
        }
        if (strcmp(argv[i], "--natural") == 0)
        {
            iFontSetFit(IFONT_FACE_SB, IFONT_FIT_NATURAL);
        }
    }

    if (readFonts(argv[1]) == 0)
    {
        return 1;
    }

    iFontSetUpscale(upscale);
    iFontSetOverlay(TRUE);

    if (!iFontLoad(IFONT_FACE_SB, argv[2]))
    {
        return 1;
    }

    // Padding is quantised: it is turned into a whole-pixel inset at the
    // upscale being drawn at, so at upscale 3 only steps of a third of an atlas
    // pixel are distinguishable and neighbouring rows come out identical. That
    // is the setting being honest about its resolution, not the sweep wasting
    // its time -- and it says how finely the value is worth choosing.
    static const F32 kPadding[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    static const F32 kWeight[] = { 0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.6f, 0.8f, 1.0f, 1.4f, 2.0f };

    for (S32 fi = 0; fi < sFontCount; fi++)
    {
        const dumped_font& d = sFonts[fi];

        printf("\n%s: %d glyphs in a %dx%d cell, drawn at upscale %d\n", d.name, (int)d.count,
               (int)d.cellW, (int)d.cellH, (int)upscale);

        iFontSetPadding(0.25f);
        iFontSetWeight(IFONT_FACE_SB, 0.0f);
        measure(d, upscale);

        printf("  the face supplies %d of them; the rest are drawn from the atlas and are not\n"
               "  measured below\n",
               (int)iFontOverlaySubstituted());

        if (glyphs)
        {
            reportGlyphs(d, upscale);
            continue;
        }

        const size_t pads = sizeof(kPadding) / sizeof(kPadding[0]);
        const size_t weights = sizeof(kWeight) / sizeof(kWeight[0]);

        F32 bestPadding = 0.0f;
        F32 bestWeight = 0.0f;
        double best = -1.0;
        double bestInk = 0.0;

        // The same again with no ink ceiling, for a face that is heavier than
        // the atlas everywhere -- there is still a best setting, it just is not
        // one that got there by matching the weight.
        F32 anyPadding = 0.0f;
        F32 anyWeight = 0.0f;
        double any = -1.0;
        double anyInk = 0.0;
        double lightest = 1e9;

        for (S32 pass = 0; pass < 2; pass++)
        {
            printf("\n  %s  ", pass == 0 ? "fit %     " : "ink x     ");
            for (size_t w = 0; w < weights; w++)
            {
                printf("  w%-4.1f", (double)kWeight[w]);
            }
            printf("\n");

            for (size_t p = 0; p < pads; p++)
            {
                printf("  pad %5.2f  ", (double)kPadding[p]);

                for (size_t w = 0; w < weights; w++)
                {
                    iFontSetPadding(kPadding[p]);
                    iFontSetWeight(IFONT_FACE_SB, kWeight[w]);

                    const fit f = measure(d, upscale);
                    printf("  %6.2f", pass == 0 ? f.agreement : f.ink);

                    // The best fit among the settings that have not run away
                    // with the ink. Past kInkCeiling the letters are filling
                    // their boxes and agreement stops meaning anything, so a
                    // higher number there is not a better font -- see
                    // iFontOverlayInk.
                    const double kInkCeiling = 1.10;

                    if (pass == 0 && f.ink <= kInkCeiling && f.agreement > best)
                    {
                        best = f.agreement;
                        bestInk = f.ink;
                        bestPadding = kPadding[p];
                        bestWeight = kWeight[w];
                    }

                    if (pass == 0 && f.ink > 0.0 && f.ink < lightest)
                    {
                        lightest = f.ink;
                    }

                    if (pass == 0 && f.agreement > any)
                    {
                        any = f.agreement;
                        anyInk = f.ink;
                        anyPadding = kPadding[p];
                        anyWeight = kWeight[w];
                    }
                }

                printf("\n");
            }
        }

        if (best < 0.0)
        {
            // Nothing in the sweep laid down as little ink as the atlas does,
            // so this face is heavier than the one it is replacing at every
            // setting and font_weight has nothing to add. Worth saying outright
            // rather than recommending the lightest row as if it were a fit.
            printf("\n  this face is heavier than the atlas at every setting -- the lightest\n"
                   "  is still %.2fx its ink, so leave font_weight at 0.\n",
                   lightest);
            best = any;
            bestPadding = anyPadding;
            bestWeight = 0.0f;
        }

        printf("\n  best fit %.2f%% at %.2fx the atlas's ink:\n\n"
               "    [text]\n    font_padding = %g\n    font_weight = %g\n",
               best, best == any ? anyInk : bestInk, (double)bestPadding, (double)bestWeight);
    }

    return 0;
}
