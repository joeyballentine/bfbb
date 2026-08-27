// RenderWare C API: RpSkin.
//
// The one group in this directory that needed no struct mirrored. RenderWare
// never defines RpSkin -- rpskin.h has the typedef and nothing else -- so the
// game only ever holds an RpSkin* and asks it questions, and an RpSkin* can
// simply BE an rw::Skin*. What the accessors hand back is not opaque, though:
// librw keeps the bones, weights and indices as three flat arrays, and the
// three casts below are claims about their strides. layout_geometry.cpp
// asserts each one.
//
// Where the attach goes. RpSkinPluginAttach is the function RenderWare gives
// for this, iSystem.cpp's RWAttachPlugins already calls it between
// RwEngineInit and RwEngineOpen, and that is exactly where librw needs
// registerSkinPlugin() -- it calls Geometry::registerPlugin and
// Atomic::registerPlugin, both of which grow object sizes that Engine::open
// freezes. So the attach belongs here rather than inside RwEngineInit: the
// ordering rule engine_start.cpp documents is already satisfied by the caller,
// and putting it in RwEngineInit would attach the plugin whether or not the
// game asked for it.

#include <rwcore.h>
#include <rpskin.h>
#include <rpworld.h>

#include "rw.h"

static inline rw::Skin* asSkin(RpSkin* skin)
{
    return reinterpret_cast<rw::Skin*>(skin);
}

RwBool RpSkinPluginAttach(void)
{
    rw::registerSkinPlugin();
    return TRUE;
}

RpSkin* RpSkinGeometryGetSkin(RpGeometry* geometry)
{
    if (geometry == NULL)
    {
        return NULL;
    }

    // Reads the Skin* out of the geometry's plugin block. Null when the
    // geometry was never skinned, which iModel.cpp and zFX.cpp both branch on.
    return reinterpret_cast<RpSkin*>(rw::Skin::get(reinterpret_cast<rw::Geometry*>(geometry)));
}

RwUInt32 RpSkinGetNumBones(RpSkin* skin)
{
    if (skin == NULL)
    {
        return 0;
    }

    return (RwUInt32)asSkin(skin)->numBones;
}

const RwMatrix* RpSkinGetSkinToBoneMatrices(RpSkin* skin)
{
    if (skin == NULL)
    {
        return NULL;
    }

    // librw stores these as sixteen floats per bone in one float array, which
    // is an RwMatrix each -- including the three pad words RwMatrix carries,
    // because the stream format they were read from is RenderWare's own 4x4.
    return reinterpret_cast<const RwMatrix*>(asSkin(skin)->inverseMatrices);
}

const RwMatrixWeights* RpSkinGetVertexBoneWeights(RpSkin* skin)
{
    if (skin == NULL)
    {
        return NULL;
    }

    // Four floats per vertex, which is one RwMatrixWeights.
    return reinterpret_cast<const RwMatrixWeights*>(asSkin(skin)->weights);
}

const RwUInt32* RpSkinGetVertexBoneIndices(RpSkin* skin)
{
    if (skin == NULL)
    {
        return NULL;
    }

    // Four bytes per vertex on librw's side, one packed word per vertex on
    // RenderWare's, and the packing order is where endianness shows up.
    //
    // iModel.cpp and zFX.cpp unpack with `(bones >> (8*j)) & 0xff` and pair
    // index j with weight j. On x86 that shift picks out memory byte j, so it
    // lands on librw's indices[vertex*4 + j] -- which is the byte librw itself
    // pairs with weights[vertex*4 + j] in its own skinning code. The cast is
    // right here for the same reason it would be wrong on the GameCube, where
    // the same shift picks byte 3.
    return reinterpret_cast<const RwUInt32*>(asSkin(skin)->indices);
}

RpAtomic* RpSkinAtomicSetType(RpAtomic* atomic, RpSkinType type)
{
    if (atomic == NULL)
    {
        return NULL;
    }

    // RenderWare picks between three skinning pipelines -- generic, matfx and
    // toon -- and librw used to have exactly one per platform, with
    // Skin::setPipeline casting the type to void. It honours the type now: the
    // three calls that ask for rpSKINTYPEMATFX (xFX.cpp, zEntCruiseBubble.cpp
    // and iModel.cpp, all wanting an environment-mapped skinned model) reach a
    // D3D9 pipeline that skins the vertices AND draws the effect, added on the
    // fork's bfbb-port branch as src/d3d/d3d9skinmatfx.cpp.
    //
    // Two things are still RenderWare's and not ours. rpSKINTYPETOON has no
    // pipeline, because nothing in this game asks for one -- these three calls
    // are the whole list. And a backend that registers no combined pipeline
    // (LIBRW_PLATFORM=NULL, and GL3 until someone writes its shaders) falls
    // back to plain skinning, which loses the effect but never the skinning.
    rw::Skin::setPipeline(reinterpret_cast<rw::Atomic*>(atomic), (rw::int32)type);
    return atomic;
}

// Bind an atomic's skin to a bone hierarchy.
//
// The skin plugin keeps one HAnimHierarchy* per ATOMIC (librw registers it as
// `Atomic::registerPlugin(sizeof(HAnimHierarchy*), ID_SKIN, ...)`), separately
// from the Skin itself which lives on the GEOMETRY. That split is what lets two
// atomics share one skinned geometry and animate independently, which is
// exactly what xModelBucket's duplicated atomics do.
//
// iModel.cpp calls this as it builds a model, right after RpHAnimHierarchyCreate.
RpAtomic* RpSkinAtomicSetHAnimHierarchy(RpAtomic* atomic, RpHAnimHierarchy* hierarchy)
{
    if (atomic == NULL)
    {
        return NULL;
    }

    rw::Skin::setHierarchy(reinterpret_cast<rw::Atomic*>(atomic),
                           reinterpret_cast<rw::HAnimHierarchy*>(hierarchy));
    return atomic;
}
