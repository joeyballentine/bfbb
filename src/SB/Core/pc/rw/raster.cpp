// RenderWare C API: RwRaster.
//
// RwRaster is mirrored onto rw::Raster (see include/rwsdk/rwcore.h and
// layout_stream.cpp), so an RwRaster* IS an rw::Raster* and these are casts
// and calls.

#include <rwcore.h>

#include "rw.h"

static inline rw::Raster* asRaster(RwRaster* r)
{
    return reinterpret_cast<rw::Raster*>(r);
}

RwRaster* RwRasterCreate(RwInt32 width, RwInt32 height, RwInt32 depth, RwInt32 flags)
{
    // RenderWare packs type, allocation flags and pixel format into one word
    // and lets the driver take it apart; librw's `format` parameter is that
    // same word, split the same way (type = w & 7, flags = w & 0xF8,
    // format = w & 0xFF00). layout_stream.cpp asserts the constants match, so
    // this passes it straight through.
    //
    // The platform argument is left at 0, which tells librw to use whichever
    // driver the engine was started with.
    return reinterpret_cast<RwRaster*>(rw::Raster::create(width, height, depth, flags, 0));
}

RwBool RwRasterDestroy(RwRaster* raster)
{
    if (raster == NULL)
    {
        return FALSE;
    }

    asRaster(raster)->destroy();
    return TRUE;
}
