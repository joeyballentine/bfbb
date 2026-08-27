// Converting geometry off its authored platform.
//
// **The port reads the XBOX asset set and renders with the D3D9 backend, and
// those are not the same platform as far as librw is concerned.**
//
// An Xbox DFF's geometry is NATIVE: its vertices, indices and -- the part that
// matters here -- its skin weights and bone indices live in a platform-specific
// blob rather than in the portable fields. xbox::readNativeSkin
// (d3d/xboxskin.cpp:31) reads that blob into skin->platformData and calls
// skin->init(numBones, 0, 0), which leaves skin->indices and skin->weights
// EMPTY. The d3d9 skin pipeline reads exactly those two arrays to build its
// vertex buffer, so a skinned Xbox model rendered by the d3d9 pipeline is
// skinned by whatever those empty arrays happen to contain -- every vertex
// weighted onto arbitrary bones. On screen that is a character whose
// unskinned parts are correct and whose skinned parts explode into spikes.
//
// librw has the conversion. xbox::uninstance (d3d/xbox.cpp:249) rebuilds the
// portable meshes from the native instance data, calls the pipeline's
// uninstanceCB -- skinUninstanceCB for a skinned one, which is what fills
// skin->indices and skin->weights -- regenerates the triangles and clears the
// NATIVE flag. What it does not do is call itself: NOTHING in librw converts a
// geometry off its authored platform, exactly as nothing calls
// Raster::convertTexToCurrentPlatform. Both are the application's job, and this
// is the geometry half of the same obligation the port already meets for
// textures in rw/texture.cpp.
//
// Which pipeline does the uninstancing matters. Both the plain and the skinned
// Xbox pipelines are xbox::ObjPipeline and share impl.uninstance; they differ
// only in the uninstanceCB, and using the plain one on a skinned geometry would
// rebuild the meshes and leave the skin arrays as empty as it found them. So a
// geometry with a skin goes through skinGlobals.pipelines[PLATFORM_XBOX], which
// xbox::initSkin installs, and everything else through the Xbox driver's
// default pipeline.

#include <rwcore.h>
#include <rpworld.h>

#include "rw.h"

#include "convert.h"

#include <stdio.h>

namespace
{
    int sComplaints = 0;
    const int kMaxComplaints = 4;

    void Complain(const char* what, rw::Geometry* geo)
    {
        if (sComplaints >= kMaxComplaints)
        {
            return;
        }

        sComplaints++;
        printf("bfbb: could not convert a geometry off its authored platform -- %s\n", what);
        printf("bfbb:   geometry %p platform %d numVertices %d\n", (void*)geo,
               geo != NULL && geo->instData != NULL ? (int)geo->instData->platform : -1,
               geo != NULL ? (int)geo->numVertices : -1);

        if (sComplaints == kMaxComplaints)
        {
            printf("bfbb:   (further reports are silent)\n");
        }

        fflush(stdout);
    }
}

void rwConvertAtomicToCurrentPlatform(rw::Atomic* atomic)
{
    if (atomic == NULL || atomic->geometry == NULL)
    {
        return;
    }

    rw::Geometry* geo = atomic->geometry;

    // Already portable, or never was anything else.
    if ((geo->flags & rw::Geometry::NATIVE) == 0 || geo->instData == NULL)
    {
        return;
    }

    // Authored for the platform we are rendering with: librw's own pipeline
    // handles it and converting would throw away the instance data it wants.
    if (geo->instData->platform == rw::platform)
    {
        return;
    }

    if (geo->instData->platform != rw::PLATFORM_XBOX)
    {
        // Only the Xbox path is written, because that is the asset set the port
        // reads. A PS2 or WDGL geometry would need its own arm; saying so is
        // better than running the Xbox one over data that is not Xbox, which
        // is what xbox::uninstance's own assert would catch a moment later.
        Complain("only Xbox geometry can be converted", geo);
        return;
    }

    rw::ObjPipeline* pipe;

    if (rw::Skin::get(geo) != NULL)
    {
        pipe = (rw::ObjPipeline*)rw::skinGlobals.pipelines[rw::PLATFORM_XBOX];
        if (pipe == NULL)
        {
            // Means registerSkinPlugin did not run before the engine started,
            // so xbox::initSkin never installed it.
            Complain("the Xbox skin pipeline is not registered", geo);
            return;
        }
    }
    else
    {
        rw::Driver* driver = rw::engine->driver[rw::PLATFORM_XBOX];
        pipe = driver != NULL ? driver->defaultPipeline : NULL;
        if (pipe == NULL)
        {
            Complain("the Xbox driver has no default pipeline", geo);
            return;
        }
    }

    pipe->uninstance(atomic);
}

void rwConvertClumpToCurrentPlatform(rw::Clump* clump)
{
    if (clump == NULL)
    {
        return;
    }

    FORLIST(link, clump->atomics)
    {
        rwConvertAtomicToCurrentPlatform(rw::Atomic::fromClump(link));
    }
}
