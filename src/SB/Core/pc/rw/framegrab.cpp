// The frame grab; see iFrameGrab.h.
//
// The PNG is written uncompressed (deflate "stored" blocks), so it needs no
// zlib: the only arithmetic a reader checks is the CRC-32 of each chunk and
// the Adler-32 of the image data.

#include <rwcore.h>

#include "rw.h"

#include "iFrameGrab.h"
#include "iSnapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    U32 sCrcTable[256];
    bool sCrcReady;

    U32 Crc(U32 crc, const U8* p, size_t n)
    {
        if (!sCrcReady)
        {
            for (U32 i = 0; i < 256; i++)
            {
                U32 c = i;
                for (int k = 0; k < 8; k++)
                {
                    c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                }
                sCrcTable[i] = c;
            }
            sCrcReady = true;
        }

        crc = ~crc;
        for (size_t i = 0; i < n; i++)
        {
            crc = sCrcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
        }
        return ~crc;
    }

    void PutBE(U8* p, U32 v)
    {
        p[0] = (U8)(v >> 24);
        p[1] = (U8)(v >> 16);
        p[2] = (U8)(v >> 8);
        p[3] = (U8)v;
    }

    bool Chunk(FILE* f, const char* type, const U8* data, U32 len)
    {
        U8 head[8];
        PutBE(head, len);
        memcpy(head + 4, type, 4);

        U32 crc = Crc(0, head + 4, 4);
        crc = Crc(crc, data, len);

        U8 tail[4];
        PutBE(tail, crc);

        return fwrite(head, 1, 8, f) == 8 && (len == 0 || fwrite(data, 1, len, f) == len) &&
               fwrite(tail, 1, 4, f) == 4;
    }
} // namespace

S32 iFrameGrabWrite(const char* path, S32 maxWidth)
{
    // Kept between calls; the copy target is a render target, not worth
    // making per shot.
    static RwRaster* sFrame;

    if (path == NULL)
    {
        return FALSE;
    }

    RwRaster* frame = iSnapshotCopyFrame(sFrame);
    sFrame = frame;
    if (frame == NULL)
    {
        return FALSE;
    }

    rw::Image* img = reinterpret_cast<rw::Raster*>(frame)->toImage();
    if (img == NULL)
    {
        return FALSE;
    }
    if (img->depth != 32 || img->width <= 0 || img->height <= 0)
    {
        img->destroy();
        return FALSE;
    }

    const int sw = img->width;
    const int sh = img->height;
    int w = sw;
    int h = sh;
    if (maxWidth > 0 && w > maxWidth)
    {
        w = maxWidth;
        h = (int)((long long)sh * w / sw);
        if (h < 1)
        {
            h = 1;
        }
    }

    // Each row is a filter byte (0, none) and the pixels.
    const size_t rowBytes = 1 + (size_t)w * 3;
    const size_t rawSize = rowBytes * h;
    U8* raw = (U8*)malloc(rawSize);
    if (raw == NULL)
    {
        img->destroy();
        return FALSE;
    }

    // An area average, as the save stills do it.
    for (int y = 0; y < h; y++)
    {
        const int y0 = (int)((long long)y * sh / h);
        int y1 = (int)((long long)(y + 1) * sh / h);
        if (y1 <= y0)
        {
            y1 = y0 + 1;
        }

        U8* row = raw + rowBytes * y;
        row[0] = 0;

        for (int x = 0; x < w; x++)
        {
            const int x0 = (int)((long long)x * sw / w);
            int x1 = (int)((long long)(x + 1) * sw / w);
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

            U8* d = row + 1 + x * 3;
            d[0] = (U8)(r / n);
            d[1] = (U8)(g / n);
            d[2] = (U8)(b / n);
        }
    }

    img->destroy();

    // zlib stream: header, stored blocks of at most 65535 bytes, Adler-32.
    const size_t blocks = rawSize / 65535 + 1;
    const size_t zSize = 2 + rawSize + blocks * 5 + 4;
    U8* z = (U8*)malloc(zSize);
    if (z == NULL)
    {
        free(raw);
        return FALSE;
    }

    size_t o = 0;
    z[o++] = 0x78;
    z[o++] = 0x01;

    U32 a = 1;
    U32 b = 0;
    size_t left = rawSize;
    const U8* src = raw;
    do
    {
        const U32 n = left > 65535 ? 65535 : (U32)left;
        z[o++] = left == n ? 1 : 0;
        z[o++] = (U8)n;
        z[o++] = (U8)(n >> 8);
        z[o++] = (U8)~n;
        z[o++] = (U8)(~n >> 8);
        memcpy(z + o, src, n);
        o += n;

        for (U32 i = 0; i < n; i++)
        {
            a = (a + src[i]) % 65521;
            b = (b + a) % 65521;
        }

        src += n;
        left -= n;
    } while (left > 0);

    PutBE(z + o, (b << 16) | a);
    o += 4;
    free(raw);

    FILE* f = fopen(path, "wb");
    if (f == NULL)
    {
        free(z);
        return FALSE;
    }

    static const U8 kSignature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };

    U8 ihdr[13];
    PutBE(ihdr, (U32)w);
    PutBE(ihdr + 4, (U32)h);
    ihdr[8] = 8;  // bits per channel
    ihdr[9] = 2;  // RGB
    ihdr[10] = 0; // deflate
    ihdr[11] = 0; // adaptive filtering
    ihdr[12] = 0; // no interlace

    bool ok = fwrite(kSignature, 1, 8, f) == 8 && Chunk(f, "IHDR", ihdr, 13) &&
              Chunk(f, "IDAT", z, (U32)o) && Chunk(f, "IEND", NULL, 0);

    free(z);
    ok = fclose(f) == 0 && ok;
    return ok ? TRUE : FALSE;
}
