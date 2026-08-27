// RenderWare C API: the pieces with no object layout in them.
//
// Everything here is either a value operation, a byte-order helper, or a
// GameCube entry point that has no host counterpart. None of it waits on the
// object-layout decision in README.md.

#include <rwcore.h>

#include "rw.h"

// The header that DECLARES the two GameCube driver entry points at the bottom
// of this file, and the reason to include it rather than write the prototypes
// out here: it wraps them in extern "C", and without that the definitions get
// C++ linkage while every caller expects C linkage.
//
// That is not hypothetical. RwGameCubeSetMinRetraceCount below shipped with
// exactly that bug -- it has no declaring header at all -- and it was found by
// diffing symbol tables. These two were then written the same way and repeated
// it, and the port's first full link found them in seconds. Including the
// declaring header is what makes the compiler check this instead of the
// linker.
#include <rwsdk/driver/gcn/dlrendst.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

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
    (void)param;

    // BFBB_DUMPSHADOW: write the shadow buffer out, once, as a TGA.
    //
    // This is the moment it is finished -- xShadow.cpp:809 calls this straight
    // after rendering the caster and inverting -- so whatever is in the raster
    // here is exactly what gets projected onto the floor a moment later.
    //
    // Worth being able to see. A projected shadow is sampled with CLAMP
    // addressing, so every texel outside the caster's footprint comes from the
    // EDGE of this raster, and a dark edge paints the whole receiving surface
    // dark however small the caster is. Reading the code cannot tell a raster
    // that came out inverted from one that never got rendered into at all; the
    // picture can.
    static int dumped = 0;

    if (dumped < 4 && ras != NULL && getenv("BFBB_DUMPSHADOW") != NULL)
    {
        char path[64];
        snprintf(path, sizeof(path), "bfbb_shadow%d.tga", dumped);
        dumped++;

        rw::Raster* raster = reinterpret_cast<rw::Raster*>(ras);
        rw::Image* image = raster->toImage();

        if (image != NULL)
        {
            rw::writeTGA(image, path);
            image->destroy();
            printf("bfbb: wrote %s -- %dx%d depth %d format %04x type %d\n", path,
                   (int)raster->width, (int)raster->height, (int)raster->depth,
                   (unsigned)raster->format, (int)raster->type);
        }
        else
        {
            printf("bfbb: the shadow raster would not convert to an image "
                   "(%dx%d depth %d format %04x)\n",
                   (int)raster->width, (int)raster->height, (int)raster->depth,
                   (unsigned)raster->format);
        }

        fflush(stdout);
    }
}

// Caps the frame rate by making the console wait for N video retraces. The
// host paces frames in iVSync (iHostSleepUntilNs) instead, so this would be a
// second, conflicting throttle if it did anything.
//
// **Linkage is not decoration here.** Unlike every other RenderWare function
// in this directory, this one has no declaration in include/rwsdk -- zGame.cpp:78
// declares it itself, inside an `extern "C" { }` block, because on the console
// it comes out of a GameCube driver library rather than out of a header. So a
// definition written the ordinary way gets C++ linkage and the call in
// zGame.cpp:697 does not resolve to it. dlrendst.h does NOT cover this one --
// it declares the other two only -- so the extern "C" below is written by hand
// and has to stay. See TODO.md.
extern "C" void RwGameCubeSetMinRetraceCount(RwUInt8 count)
{
}

// The two GameCube driver entry points that xModelBucket.cpp calls UNGUARDED.
//
// They are here, next to RwGameCubeCameraTextureFlush and
// RwGameCubeSetMinRetraceCount, for the same reason those are: the call sites
// are in portable code (src/SB/Core/x), so the port either provides the
// function or edits game code. It provides the function. Their declarations
// are in include/rwsdk/driver/gcn/dlrendst.h, which is also where the PC build
// gets the GX_* constants they are called with.
//
// Between them these were the last thing keeping xModelBucket.cpp from
// compiling on PC -- the 198th unit of 198.

// **This one drops a real rendering behaviour, and it will be visible.**
//
// The top byte of a bucket's pipeFlags is an alpha test reference, and
// xModelBucket.cpp:520-533 turns it into one of two GX states:
//
//   ref != 0:  alpha >= ref     (GX_ALWAYS AND GX_GEQUAL ref), z compare after
//              texture, because the alpha test can now reject a pixel and the
//              early-z result would be wrong.
//   ref == 0:  alpha >= 1       (GX_GEQUAL 1 AND GX_ALWAYS), z compare before
//              texture -- i.e. discard only fully transparent pixels.
//
// That is cutout transparency: foliage, fences, grates, chain link. With this
// as a no-op they render as opaque quads, alpha and all. There is nothing to
// forward it to today -- RenderWare's portable render state vector has no
// alpha test function or reference (rwcore.h does not declare one, because the
// game never used the portable spelling), and LIBRW_PLATFORM=NULL has no
// device to set one on.
//
// So this is not "recorded but not rendered" like the shade mode in
// renderstate.cpp -- there is no Get for it and nothing would read the record.
// It is the specification, left where whoever writes the GL3 or D3D9 backend
// will find it. The two states above are the whole of what has to be
// reimplemented.
void RwGameCubeSetAlphaCompare(RwInt32 comp0, RwUInt8 ref0, RwInt32 op, RwInt32 comp1,
                               RwUInt8 ref1)
{
}

// Whether the z buffer is compared before or after the texture lookup.
//
// Unlike the alpha compare above, this one is CORRECT as a no-op. It is a TEV
// pipeline ordering knob specific to the GameCube's hardware: comparing z
// early saves texture bandwidth, and is only safe when nothing later in the
// pipeline can reject the pixel -- which is why the call above always pairs
// it with the alpha test state. A host renderer's driver makes that decision
// for itself from the pipeline state it was given, and neither OpenGL nor
// Direct3D exposes it as something to set.
void _rwDlRenderStateSetZCompLoc(RwInt32 zBeforeTex)
{
}
