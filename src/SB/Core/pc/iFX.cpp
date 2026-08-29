// librw first, and it has to be: types.h defines `null` as a macro and librw
// spells one of its namespaces `null`, so a translation unit that takes the
// game's header first cannot then take librw's. Every other file in this
// directory reaches librw through the RenderWare C API in rw/ and never has to
// care; this one talks to rw:: directly.
#include <rw.h>

#include "iFX.h"

#include <types.h>

// The animated-UV pipeline: the one an atomic is given when an artist put a
// uvfx entry on its surface, so its texture coordinates scroll, rotate or scale
// over time. Water, conveyor belts and the Industrial Park lava draw through
// this.
//
// The state it animates from is four pairs of floats in xFX.cpp, which
// xEntSetupPipeline writes immediately before handing the model to
// xFXanimUVAtomicSetup. Nothing here owns or caches them: they are read at draw
// time, once per atomic, so a surface drawn twice in a frame with two different
// transforms comes out right.
//
// The transform is not written into the vertices because RpGeometry is shared
// between every atomic that instances a model -- rewriting texture coordinates
// would animate every copy at once, and one lava pool would drag the rest with
// it. So it happens after instancing, per draw, which on hardware means in a
// shader.
//
// The pieces split the way the GameCube's do:
//
//   librw   owns the transform. rw::uvTransform is the matrix,
//           rw::SetUVTransform writes it, and rw::GetUVTransformPipeline()
//           returns the platform's pipeline for applying it -- for D3D9 a
//           second copy of the default pipeline, drawing with a variant of the
//           default vertex shader that multiplies the texture coordinates
//           through the matrix (src/d3d/shaders/default_VS.hlsl, UVXFORM).
//   here    owns reading the game's globals into that matrix, by taking the
//           pipeline librw handed over and putting its own render in front of
//           librw's.
//
// gc/iFX.cpp is the same shape: it asks RenderWare for the GameCube AllInOne
// pipeline and calls RxGameCubeAllInOneSetRenderCallBack to put its own
// callback in, reading the same four globals into a GX 2x4 texture matrix.
// Swapping impl.render here is that call by another name.
//
// A platform whose librw side is not implemented returns nil from
// GetUVTransformPipeline, and so does this. xFX.cpp:883 then leaves the atomic
// on its default pipeline and draws it unanimated, which is GL3 today: the
// state and the pipeline hook are portable, the shader is not written.
//
// NOT IMPLEMENTED, because the GameCube does not implement it either: the
// second-pass state. xFXanimUV2PRotMat0, ...2PTrans, ...2PScale and
// xFXanimUV2PTexture are written by xEntSetupPipeline and read by nothing --
// grep gc/iFX.cpp and it uses only the first-pass four. The setters exist and
// keep working; a second pass would be new behaviour, not restored behaviour.

extern F32 xFXanimUVRotMat0[2];
extern F32 xFXanimUVRotMat1[2];
extern F32 xFXanimUVTrans[2];
extern F32 xFXanimUVScale[2];

// librw's render for the pipeline below, which this one wraps rather than
// replaces. It does the drawing; all this file adds is the matrix.
static void (*sAnimUVBaseRender)(rw::ObjPipeline* pipe, rw::Atomic* atomic) = NULL;

static void iFXanimUVRender(rw::ObjPipeline* pipe, rw::Atomic* atomic)
{
    // The GameCube's 2x4 texture matrix, laid out exactly as gc/iFX.cpp lays
    // it out, because it multiplies exactly the same vector: GX_TG_MTX2x4
    // feeds a texgen the vector (u, v, 1, 1), and the D3D9 shader builds the
    // same one.
    //
    // Which means BOTH of the last two columns are constants that add, and the
    // scale is in one of them. That is not a transcription slip -- retail
    // writes the scale there, so a uvfx scale of 1 shifts the coordinates by a
    // whole tile (invisible on a wrapped texture, which is why it survived)
    // and a scale of anything else shifts them by that. Scaling the rotation
    // instead would look more sensible and would not be what the game does.
    const rw::float32 xform[rw::NUMUVTRANSFORMELEMENTS] = {
        xFXanimUVRotMat0[0], xFXanimUVRotMat0[1], xFXanimUVTrans[0], xFXanimUVScale[0],
        xFXanimUVRotMat1[0], xFXanimUVRotMat1[1], xFXanimUVTrans[1], xFXanimUVScale[1],
    };

    rw::SetUVTransform(xform);
    sAnimUVBaseRender(pipe, atomic);

    // Nothing else reads the transform -- every other pipeline ignores it --
    // but leaving it set would make the next reader of rw::uvTransform see one
    // entity's animation, and a debugger see a value that is nobody's. Put it
    // back.
    rw::SetUVTransform(NULL);
}

RxPipeline* iFXanimUVCreatePipe()
{
    rw::ObjPipeline* pipe = rw::GetUVTransformPipeline();

    if (pipe == NULL)
    {
        return NULL;
    }

    // xFXanimUVCreate calls this once and keeps the result forever, so this
    // runs once. Wrapping twice would recurse.
    if (sAnimUVBaseRender == NULL)
    {
        sAnimUVBaseRender = pipe->impl.render;
        pipe->impl.render = iFXanimUVRender;
    }

    return reinterpret_cast<RxPipeline*>(pipe);
}
