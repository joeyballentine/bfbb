// The cruise bubble's screen distortion.
//
// A dependent texture read: sample a swirl map, use it to displace the texture
// coordinates, and read a copy of the screen at the displaced position.
//
// This is a reimplementation of the Xbox release's pixel shader, not a
// translation of it -- that one is Xbox microcode and D3D9 will not take it.
// What is reproduced is the behaviour the disassembly describes: one scalar
// offset field, one rotating displacement vector, one dependent read. See
// distort.cpp for where each number comes from.
//
// Interface is librw's im2d vertex shader, so the input layout is its VS_out.
// Fog and the vertex colour are deliberately ignored: the Xbox draws this pass
// SRC ONE / DEST ZERO with a white vertex colour, which is a straight
// overwrite of the frame buffer, and tinting or fogging it would be neither.

struct VS_out
{
    float4 Position  : POSITION;
    float3 TexCoord0 : TEXCOORD0;
    float4 Color     : COLOR0;
};

// Stage 0 is the copy of the screen, stage 1 the swirl map. The Xbox has them
// the other way round (0 = swirl, 2 = screen); the order is ours because stage
// 0 is the one librw's render state binds, and the screen copy is the texture
// that changes every frame.
sampler2D screen : register(s0);
sampler2D swirl  : register(s1);

// xy: the displacement vector, already in texture-coordinate units and already
// scaled by the effect's strength. zw unused.
//
// c1 rather than c0 because librw reserves c0 for the fog colour
// (PSLOC_fogColor in rwd3d.h).
float4 displace : register(c1);

float4 main(VS_out input) : COLOR
{
    float2 uv = input.TexCoord0.xy;

    // The map stores one signed scalar biased to the middle of the range, in
    // both red and green (they are equal at every texel in the shipped
    // texture). Red alone is the whole signal.
    //
    // Expanded to +/-1, not +/-0.5. The Xbox does this in the texture unit
    // rather than the shader: PSDotMapping in its pixel-shader definition is
    // PS_DOTMAPPING_MINUS1_TO_1_D3D for both dot-product stages, which is D3D's
    // signed reading of an unsigned byte -- (2v - 255)/255, so 0 is -1, 128 is
    // 0 and 255 is +1. Halving that would halve the whole effect.
    float d = tex2D(swirl, uv).r * 2.0f - 1.0f;

    return tex2D(screen, uv + d * displace.xy);
}
