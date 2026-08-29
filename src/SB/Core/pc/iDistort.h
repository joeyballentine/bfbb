#ifndef IDISTORT_H
#define IDISTORT_H

#include <types.h>

// PC-only: the cruise bubble's screen distortion.
//
// xScrFxDistortionRender is an empty function in the shipped GameCube code with
// a live call site in xScrFxRender. On the Xbox it drew this: the screen re-read
// through a swirl-shaped offset field, so piloting the Cruise Bubble warps the
// picture around the middle. This is that function's body.
//
// Recovered from the Xbox build, where xScrFxDistortionRender is at va 0x170a00
// and its setup at 0x170750, reached from xScrFxInit. Between them they:
//
//   - build a 512x512 rwRASTERTYPECAMERATEXTURE and a camera to render it,
//   - resolve the texture asset "BXCruiseBubbleDistort" by hash,
//   - copy the frame buffer into the camera texture,
//   - bind the offset map and the copy to two texture stages,
//   - and draw one full-screen quad through a pixel shader, SRC ONE /
//     DEST ZERO, linear, clamped -- an overwrite, not a blend.
//
// The offset map ships in plat.HIP, which is preloaded at boot, so it is always
// resident. 512x512, rwRASTERFORMAT888, Xbox-swizzled; deswizzled it is a
// smooth centred swirl. Red and green are equal at every one of its 262144
// texels and blue and alpha are 255, so it carries one signed scalar, biased so
// 128 means no displacement.
//
// The pixel shader is reimplemented rather than translated. The Xbox one is not
// D3D8 bytecode: it is a D3DPIXELSHADERDEF at va 0x2864d0, the NV2A's
// register-combiner and texture-addressing description, which D3D9 cannot load.
// rw/shaders/distort_PS.hlsl is written against what that struct says:
//
//   PSTextureModes   0x2621  stage 0 PROJECT2D, 1 DOTPRODUCT, 2 DOT_ST
//   PSDotMapping     0x0011  both dot stages PS_DOTMAPPING_MINUS1_TO_1_D3D
//   PSInputTexture   0       the dot stages read stage 0's texel
//   PSCombinerCount  1       one combiner
//   PSRGBInputs[0]   0xca200000   A = T2 signed identity, B = 1 - ZERO
//   PSRGBOutputs[0]  0x000000c0   AB -> R0, no shift and no bias
//
// Both final-combiner input words are zero, so the colour is the sampled screen
// and nothing else, and the coordinates it is sampled at are dot products of
// the interpolated texture coordinates with the expanded swirl texel. With red
// equal to green and blue at 255, those reduce to the base coordinate plus the
// scalar times the rotating vector.
//
// The expansion range is the part only the definition gives:
// MINUS1_TO_1_D3D reads an unsigned byte as (2v - 255)/255, so the signal is
// +/-1. From the disassembly alone it reads as +/-0.5, and the effect comes out
// half strength.

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
