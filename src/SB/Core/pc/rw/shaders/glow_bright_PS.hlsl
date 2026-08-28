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

struct VS_out
{
    float4 Position  : POSITION;
    float3 TexCoord0 : TEXCOORD0;
    float4 Color     : COLOR0;
};

sampler2D src : register(s0);

float4 main(VS_out input) : COLOR
{
    float3 c = tex2D(src, input.TexCoord0.xy).rgb;
    return float4(saturate(c * 2.0f - 1.0f), 1.0f);
}
