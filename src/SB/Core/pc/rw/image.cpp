// RenderWare C API: RwImage.
//
// RwImage is mirrored onto rw::Image (see include/rwsdk/rwcore.h and
// layout_stream.cpp), so an RwImage* IS an rw::Image*. Three of these four are
// a cast and a call; RwImageSetFromRaster is written out, because librw's
// nearest equivalent returns a new image rather than filling the caller's.

#include <rwcore.h>

#include "rw.h"

#include <string.h>

static inline rw::Image* asImage(RwImage* i)
{
    return reinterpret_cast<rw::Image*>(i);
}

static inline rw::Raster* asRaster(RwRaster* r)
{
    return reinterpret_cast<rw::Raster*>(r);
}

RwImage* RwImageCreate(RwInt32 width, RwInt32 height, RwInt32 depth)
{
    return reinterpret_cast<RwImage*>(rw::Image::create(width, height, depth));
}

RwBool RwImageDestroy(RwImage* image)
{
    if (image == NULL)
    {
        return FALSE;
    }

    asImage(image)->destroy();
    return TRUE;
}

RwImage* RwImageAllocatePixels(RwImage* image)
{
    if (image == NULL)
    {
        return NULL;
    }

    rw::Image* img = asImage(image);
    img->allocate();

    // librw's allocate() has no failure return, so the result is what says
    // whether it worked. It only allocates a palette for 4- and 8-bit images,
    // which is also what RenderWare does.
    if (img->pixels == NULL)
    {
        return NULL;
    }
    return image;
}

// RenderWare fills an image the caller already made; librw's counterpart,
// Raster::toImage, makes one of its own. The conversion out of whatever the
// driver keeps in video memory is the driver's business either way, so this
// asks librw for that image and then moves it into the caller's -- rather than
// duplicating pixel-format knowledge that belongs in the driver.
RwImage* RwImageSetFromRaster(RwImage* image, RwRaster* raster)
{
    if (image == NULL || raster == NULL)
    {
        return NULL;
    }

    rw::Image* dst = asImage(image);
    if (dst->pixels == NULL)
    {
        // RenderWare requires RwImageAllocatePixels first; there is nowhere to
        // put the result otherwise.
        return NULL;
    }

    rw::Image* src = asRaster(raster)->toImage();
    if (src == NULL)
    {
        return NULL;
    }

    RwImage* result = NULL;

    // Nothing here resamples: RenderWare's RwImageSetFromRaster does not
    // either, and a caller that wants a different size uses RwImageResample.
    if (src->width == dst->width && src->height == dst->height)
    {
        if (src->depth != dst->depth)
        {
            if (dst->depth == 32)
            {
                src->convertTo32();
            }
            else if (dst->depth == 8 || dst->depth == 4)
            {
                // Quantises down to a palette, which is lossy -- but it is the
                // conversion RenderWare performs here too.
                src->palettize(dst->depth);
            }
            // 16- and 24-bit destinations fall through with src->depth still
            // wrong and are rejected below. librw has no converter for them,
            // and inventing one here would put pixel-format code in the shim.
        }

        if (src->depth == dst->depth)
        {
            rw::int32 row = src->stride < dst->stride ? src->stride : dst->stride;
            for (rw::int32 y = 0; y < dst->height; y++)
            {
                memcpy(dst->pixels + y * dst->stride, src->pixels + y * src->stride, row);
            }

            if (dst->palette != NULL && src->palette != NULL)
            {
                memcpy(dst->palette, src->palette, (1 << dst->depth) * 4);
            }

            result = image;
        }
    }

    src->destroy();
    return result;
}
