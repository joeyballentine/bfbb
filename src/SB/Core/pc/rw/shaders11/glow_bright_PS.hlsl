// The glow's bright pass: threshold the frame, on the way down to half size.
//
// One combiner in the Xbox's definition at va 0x286698:
//   PSRGBInputs[0] = 0x48200000 -- A is EXPAND_NORMAL of T0, B is 1 - ZERO
//   PSRGBOutputs[0] = 0xc0      -- AB -> R0, no shift and no bias
// EXPAND_NORMAL is 2c - 1, and the combiner clamps, so this keeps what is
// brighter than mid grey and rescales it to fill the range.
//
// Alpha is 1, not a strength dial: the final combiner takes it from
// PSFinalCombinerConstant0, which that definition holds as 0xff000000. The
// composite blends by source alpha, so the glow goes on at full strength and
// what governs it is the threshold above and the blur weights after.

#include "pixelConstants.h"

struct VS_out
{
	float4 Position  : SV_POSITION;
	float3 TexCoord0 : TEXCOORD0;
	float4 Color     : COLOR0;
};

Texture2D src : register(t0);
SamplerState srcSampler : register(s0);

float4 main(VS_out input) : SV_TARGET
{
	float3 c = src.Sample(srcSampler, input.TexCoord0.xy).rgb;
	DoAlphaTest(1.0f);
	return float4(saturate(c * 2.0f - 1.0f), 1.0f);
}
