#ifndef IDISTORT_H
#define IDISTORT_H

#include <types.h>

// PC-only: the cruise bubble's screen distortion.
//
// `xScrFxDistortionRender` is an empty function in the shipped GameCube code
// with a live call site in `xScrFxRender`. On the Xbox it drew this: the screen
// re-read through a swirl-shaped offset field, so that piloting the Cruise
// Bubble warps the picture around the middle. This is that function's body.
//
// **What was recovered, and from where.** The Xbox build's
// `xScrFxDistortionRender` is at va 0x170a00 and its setup at 0x170750 (reached
// from `xScrFxInit`, so on that platform it lives in `iScrFxInit`). Between
// them they:
//
//   - build a 512x512 rwRASTERTYPECAMERATEXTURE and a camera to render it,
//   - resolve the texture asset "BXCruiseBubbleDistort" by hash,
//   - copy the frame buffer into the camera texture,
//   - bind the offset map and the copy to two texture stages,
//   - and draw one full-screen quad through a pixel shader, SRC ONE /
//     DEST ZERO, linear, clamped -- an overwrite, not a blend.
//
// The offset map ships in plat.HIP, which is preloaded at boot, so it is always
// resident. It is 512x512, rwRASTERFORMAT888, Xbox-swizzled; deswizzled it is a
// smooth centred swirl. Red and green are equal at every one of its 262144
// texels and blue and alpha are 255, so it carries ONE signed scalar, biased so
// that 128 means no displacement.
//
// **What is reimplemented rather than copied.** The Xbox pixel shader is not
// D3D8 bytecode at all -- it is a D3DPIXELSHADERDEF at va 0x2864d0, the NV2A's
// register-combiner and texture-addressing description, which D3D9 has no way
// to load. So rw/shaders/distort_PS.hlsl is written against what that struct
// says rather than translated from instructions. What it says:
//
//   PSTextureModes   0x2621  stage 0 PROJECT2D, 1 DOTPRODUCT, 2 DOT_ST
//   PSDotMapping     0x0011  both dot stages PS_DOTMAPPING_MINUS1_TO_1_D3D
//   PSInputTexture   0       the dot stages read stage 0's texel
//   PSCombinerCount  1       one combiner
//   PSRGBInputs[0]   0xca200000   A = T2 signed identity, B = 1 - ZERO
//   PSRGBOutputs[0]  0x000000c0   AB -> R0, no shift and no bias
//
// with both final-combiner input words zero. So the colour is the sampled
// screen and nothing else -- no tint, no fog, no vertex colour -- and the
// coordinates it is sampled at are dot products of the interpolated texture
// coordinates with the expanded swirl texel. With red and green equal and blue
// at 255 in that texture, those dot products reduce to the base coordinate plus
// the scalar times the rotating vector, which is what the shader computes.
//
// The one thing that only the definition could have told us is the expansion
// range: MINUS1_TO_1_D3D reads an unsigned byte as (2v - 255)/255, so the
// signal is +/-1. Written from the disassembly alone it would have been +/-0.5
// and the whole effect half as strong.

// Draw the distortion over the frame, for one frame.
//
//   cam     the camera being rendered. Its update is ENDED and BEGUN again
//           inside this call: copying the frame buffer needs the scene closed,
//           and the Xbox does the same dance for the same reason.
//   map     the offset field, already resolved from the asset store by the
//           caller -- this layer has no business reaching into it.
//   amount  0 for off, 1 for full. Nothing is drawn at 0, and the whole call is
//           a compare and a return.
//   width   the picture's size in pixels. The displacement is authored in
//   height  pixels, so it needs these to become texture coordinates, and the
//           Xbox passes them into its own wobble for the same reason.
void iDistortRender(RwCamera* cam, RwTexture* map, F32 amount, F32 width, F32 height);

// Whether iDistortRender does anything; config.ini's xbox.distortion. Pushed
// down from iSystem.cpp rather than read here, for the reason iGlow.h gives.
// The default is on. Off leaves xScrFxDistortionRender doing what it does on
// the GameCube, which is nothing.
void iDistortSetEnabled(S32 enabled);

#endif
