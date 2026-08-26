#include "iFX.h"

#include <types.h>

// The animated-UV pipeline, and the port does not have one.
//
// **This is a refusal, not a stub, and the difference is that the game already
// handles it.** xFX.cpp:883 reads
//
//     if (xFXanimUVPipeline == 0) { return atomic; }
//
// so an atomic that would have been given this pipeline keeps its default one
// and renders normally. Nothing checks for the failure, nothing logs, and
// nothing breaks. What is lost is the animation: surfaces whose texture
// coordinates scroll, rotate or scale over time -- water, conveyor belts, the
// lava in the Industrial Park, anything an artist gave a uvfx entry -- draw
// with static texture coordinates instead of moving ones.
//
// WHY there is nothing to forward to, so that the next person does not go
// looking. gc/iFX.cpp is 213 lines and essentially all of it is GameCube: it
// builds a custom RenderWare pipeline around RxNodeDefinitionGetGameCubeAtomicAllInOne,
// installs a render callback, and that callback walks the instanced display
// lists by hand (GXCallDisplayList), sets the GameCube's own 2x4 texture matrix
// (GXLoadTexMtxImm / GXSetTexCoordGen) from the xFXanimUV* globals, and reads a
// GameCube raster extension to decide z-compare location. librw has no
// equivalent of ANY of that -- no GameCube pipeline, no display lists, and no
// texture matrix on a material at all.
//
// WHAT A REAL IMPLEMENTATION WOULD TAKE, in rough order of preference:
//
//   1. A texture matrix in librw, surfaced as material state and applied in the
//      GL3 and D3D9 pipelines' vertex shaders. That is the same shape as the
//      GameCube's and the same shape as the fix iDraw.cpp wants for its write
//      mask -- both are librw changes rather than game-code ones.
//   2. A custom rw::ObjPipeline here that transforms texture coordinates on the
//      way to the vertex buffer. Portable, but it duplicates a pipeline per
//      backend.
//
// Rewriting the geometry's texture coordinates each frame is NOT one of the
// options: RpGeometry is shared between every atomic that instances the model,
// so animating one surface would animate all of them.
//
// The four setters this pipeline reads -- xFXanimUVSetTranslation, SetScale,
// SetAngle and the 2P variants -- are in xFX.cpp and keep working. They write
// globals that nothing on this side reads yet, which is exactly the state a
// future implementation wants to find them in.

RxPipeline* iFXanimUVCreatePipe()
{
    return NULL;
}
