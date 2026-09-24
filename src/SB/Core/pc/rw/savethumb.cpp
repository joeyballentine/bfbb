// The save stills. What they are and when they are taken is in iSaveThumb.h;
// this is the reading back, the scaling down, the file and the texture.
//
// Beside the shim because the frame it reads is a raster and the texture it
// makes is librw's, and neither conversion belongs outside it.

#include <rwcore.h>

#include "rw.h"

#include "iSaveThumb.h"
#include "iSnapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    // The still's width; its height follows the frame's shape. Enough for the
    // thumbnail box several times over, and 100-odd KB on disk.
    const int kWidth = 256;
    const int kMaxHeight = 256;

    U8 sPixels[kWidth * kMaxHeight * 3];
    int sHeight;
    bool sHave;

    // A TGA header, uncompressed true colour, top-left origin.
    void PutHeader(U8* h, int w, int hgt)
    {
        memset(h, 0, 18);
        h[2] = 2;
        h[12] = (U8)(w & 0xFF);
        h[13] = (U8)(w >> 8);
        h[14] = (U8)(hgt & 0xFF);
        h[15] = (U8)(hgt >> 8);
        h[16] = 24;
        h[17] = 0x20;
    }
} // namespace

void iSaveThumbCapture()
{
    sHave = false;

    RwRaster* frame = iSnapshotLastFrame();
    if (frame == NULL)
    {
        return;
    }

    rw::Image* img = reinterpret_cast<rw::Raster*>(frame)->toImage();
    if (img == NULL)
    {
        return;
    }
    if (img->depth != 32 || img->width <= 0 || img->height <= 0)
    {
        img->destroy();
        return;
    }

    const int sw = img->width;
    const int sh = img->height;
    int h = (int)((long long)kWidth * sh / sw);
    if (h < 1)
    {
        h = 1;
    }
    if (h > kMaxHeight)
    {
        h = kMaxHeight;
    }

    // An area average: every source pixel lands in exactly one still pixel,
    // which is what keeps a 4K frame from turning to noise at 256 wide.
    for (int y = 0; y < h; y++)
    {
        const int y0 = (int)((long long)y * sh / h);
        int y1 = (int)((long long)(y + 1) * sh / h);
        if (y1 <= y0)
        {
            y1 = y0 + 1;
        }

        for (int x = 0; x < kWidth; x++)
        {
            const int x0 = (int)((long long)x * sw / kWidth);
            int x1 = (int)((long long)(x + 1) * sw / kWidth);
            if (x1 <= x0)
            {
                x1 = x0 + 1;
            }

            unsigned r = 0, g = 0, b = 0, n = 0;
            for (int sy = y0; sy < y1; sy++)
            {
                const U8* p = img->pixels + sy * img->stride + x0 * 4;
                for (int sx = x0; sx < x1; sx++, p += 4)
                {
                    r += p[0];
                    g += p[1];
                    b += p[2];
                    n++;
                }
            }

            U8* d = &sPixels[(y * kWidth + x) * 3];
            d[0] = (U8)(r / n);
            d[1] = (U8)(g / n);
            d[2] = (U8)(b / n);
        }
    }

    img->destroy();
    sHeight = h;
    sHave = true;
}

S32 iSaveThumbHave()
{
    return sHave ? TRUE : FALSE;
}

S32 iSaveThumbWrite(const char* path)
{
    if (!sHave || path == NULL)
    {
        return FALSE;
    }

    FILE* f = fopen(path, "wb");
    if (f == NULL)
    {
        return FALSE;
    }

    U8 header[18];
    PutHeader(header, kWidth, sHeight);
    bool ok = fwrite(header, 1, sizeof(header), f) == sizeof(header);

    // TGA stores blue first.
    U8 row[kWidth * 3];
    for (int y = 0; ok && y < sHeight; y++)
    {
        const U8* s = &sPixels[y * kWidth * 3];
        for (int x = 0; x < kWidth; x++)
        {
            row[x * 3 + 0] = s[x * 3 + 2];
            row[x * 3 + 1] = s[x * 3 + 1];
            row[x * 3 + 2] = s[x * 3 + 0];
        }
        ok = fwrite(row, 1, sizeof(row), f) == sizeof(row);
    }

    ok = (fclose(f) == 0) && ok;
    return ok ? TRUE : FALSE;
}

RwTexture* iSaveThumbLoad(const char* path, F32* aspect)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL)
    {
        return NULL;
    }

    // Only what iSaveThumbWrite writes: anything else is not a still of ours.
    U8 header[18];
    if (fread(header, 1, sizeof(header), f) != sizeof(header) || header[2] != 2 ||
        header[16] != 24 || header[0] != 0)
    {
        fclose(f);
        return NULL;
    }

    const int w = header[12] | (header[13] << 8);
    const int h = header[14] | (header[15] << 8);
    const bool topDown = (header[17] & 0x20) != 0;
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024)
    {
        fclose(f);
        return NULL;
    }

    rw::Image* img = rw::Image::create(w, h, 32);
    img->allocate();

    U8* row = (U8*)malloc((size_t)w * 3);
    bool ok = row != NULL;
    for (int y = 0; ok && y < h; y++)
    {
        ok = fread(row, 1, (size_t)w * 3, f) == (size_t)w * 3;
        U8* d = img->pixels + (topDown ? y : h - 1 - y) * img->stride;
        for (int x = 0; ok && x < w; x++)
        {
            d[x * 4 + 0] = row[x * 3 + 2];
            d[x * 4 + 1] = row[x * 3 + 1];
            d[x * 4 + 2] = row[x * 3 + 0];
            d[x * 4 + 3] = 255;
        }
    }
    free(row);
    fclose(f);

    if (!ok)
    {
        img->destroy();
        return NULL;
    }

    rw::Raster* raster = rw::Raster::createFromImage(img);
    img->destroy();
    if (raster == NULL)
    {
        return NULL;
    }

    rw::Texture* tex = rw::Texture::create(raster);
    if (tex == NULL)
    {
        raster->destroy();
        return NULL;
    }
    tex->setFilter(rw::Texture::LINEAR);

    if (aspect != NULL)
    {
        *aspect = (F32)w / (F32)h;
    }
    return reinterpret_cast<RwTexture*>(tex);
}

void iSaveThumbFree(RwTexture* texture)
{
    if (texture != NULL)
    {
        reinterpret_cast<rw::Texture*>(texture)->destroy();
    }
}
