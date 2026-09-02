// The glow's blur pass, run twice: down the vertical axis and then the
// horizontal one.
//
// Four taps, from the Xbox's definition at va 0x286788 -- all four texture
// stages PROJECT2D, three combiners computing
//   R0 = c0*T0 + c1*T1;  R1 = c2*T2 + c3*T3;  R0 = R0 + R1
// so a weighted sum of four samples. The weights and offsets are the two
// tables the Xbox hands its blur helper, at va 0x286878 and 0x2868a8:
//
//   vertical    1/3 at (0,+1)  1/6 at (0,+3)  1/3 at (0,-1)  1/6 at (0,-3)
//   horizontal  1/3 at (+1,0)  1/6 at (+3,0)  1/3 at (-1,0)  1/6 at (-3,0)
//
// The weights sum to exactly one, the offsets are in texels, and the two passes
// are the same kernel turned through ninety degrees -- so one shader does both
// and the caller supplies the axis.

// The four weights, then the four offsets in texture coordinates. c0 is
// librw's fog colour, so these start at c1.
#define PSTAIL float4 weights : packoffset(c1); float4 offs01 : packoffset(c2); float4 offs23 : packoffset(c3);

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
	float2 uv = input.TexCoord0.xy;

	float4 c  = src.Sample(srcSampler, uv + offs01.xy) * weights.x;
	c += src.Sample(srcSampler, uv + offs01.zw) * weights.y;
	c += src.Sample(srcSampler, uv + offs23.xy) * weights.z;
	c += src.Sample(srcSampler, uv + offs23.zw) * weights.w;

	DoAlphaTest(c.a);
	return c;
}
