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

// Lock and unlock, which nothing in the game needed until a movie did.
//
// RenderWare hands back a pointer to the raster's pixels and takes it away
// again on unlock, and librw's Raster does the same thing behind the same two
// calls. The lock MODE constants are the part worth checking rather than
// assuming, and layout_stream.cpp asserts them: rwRASTERLOCKWRITE is
// Raster::LOCKWRITE, and so on, so the flags pass straight through.
//
// The level argument is the mipmap level. RenderWare takes a RwUInt8 and librw
// an int32; every caller passes 0.
RwUInt8* RwRasterLock(RwRaster* raster, RwUInt8 level, RwInt32 lockMode)
{
    if (raster == NULL)
    {
        return NULL;
    }
    return asRaster(raster)->lock((rw::int32)level, (rw::int32)lockMode);
}

RwRaster* RwRasterUnlock(RwRaster* raster)
{
    if (raster == NULL)
    {
        return NULL;
    }
    asRaster(raster)->unlock(0);
    return raster;
}
