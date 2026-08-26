// RenderWare C API: the pieces with no object layout in them.
//
// Everything here is either a value operation, a byte-order helper, or a
// GameCube entry point that has no host counterpart. None of it waits on the
// object-layout decision in README.md.

#include <rwcore.h>

#include "rw.h"

#include <stddef.h>

// RenderWare streams are little-endian on every platform, so this converts a
// buffer from stream order to the host's. On the GameCube that is a swap; on
// x86 the two already agree and the correct conversion is to do nothing.
//
// Not a stub: identity IS the conversion here. The GameCube build never reaches
// this file, and a big-endian host would need the swap put back -- which is why
// the byte order is tested rather than assumed.
void* RwMemNative32(void* mem, RwUInt32 size)
{
    static_assert(sizeof(RwUInt32) == 4, "RwUInt32 is not 32 bits");

    const RwUInt32 probe = 1;
    if (*(const unsigned char*)&probe != 1)
    {
        // Big-endian host. Nothing reaches here today, and silently returning
        // unconverted bytes would corrupt every stream read, so say so.
        RwUInt8* p = (RwUInt8*)mem;
        for (RwUInt32 i = 0; i + 4 <= size; i += 4)
        {
            RwUInt8 t = p[i];
            p[i] = p[i + 3];
            p[i + 3] = t;
            t = p[i + 1];
            p[i + 1] = p[i + 2];
            p[i + 2] = t;
        }
    }

    return mem;
}

// RenderWare's last-error slot. librw keeps its own error state, but nothing in
// the game reads more than "was there one", and mapping librw's codes onto
// RenderWare's plugin/code pairs would be inventing a correspondence that does
// not exist. Reports no error, which is what librw's own err is when a call
// succeeds; a caller that needs more should be given librw's error directly.
RwError* RwErrorGet(RwError* code)
{
    if (code != NULL)
    {
        code->pluginID = 0;
        code->errorCode = 0;
    }
    return code;
}

// ---------------------------------------------------------------------------
// GameCube entry points.
//
// These are not RenderWare's portable API -- they are the GameCube driver's,
// and librw has no counterpart because there is nothing to have one of.

// Flushes a camera's raster out of the GameCube's embedded framebuffer into
// main memory, so it can be used as a texture. xShadow does this to read back
// the shadow buffer. A host renderer renders to a texture directly and has
// nothing to flush.
void RwGameCubeCameraTextureFlush(RwRaster* ras, RwUInt32 param)
{
}

// Caps the frame rate by making the console wait for N video retraces. The
// host paces frames in iVSync (iHostSleepUntilNs) instead, so this would be a
// second, conflicting throttle if it did anything.
//
// **extern "C" is not decoration.** Unlike every other RenderWare function in
// this directory, this one has no declaration in include/rwsdk -- zGame.cpp:78
// declares it itself, inside an `extern "C" { }` block, because on the console
// it comes out of a GameCube driver library rather than out of a header. So a
// definition written the ordinary way here gets C++ linkage and the call in
// zGame.cpp:697 does not resolve to it. The failure is a link error naming a
// symbol that visibly exists in the object file, which is a bad afternoon; it
// was found by diffing what the game references against what this directory
// defines, not by reading the code. See TODO.md.
extern "C" void RwGameCubeSetMinRetraceCount(RwUInt8 count)
{
}
