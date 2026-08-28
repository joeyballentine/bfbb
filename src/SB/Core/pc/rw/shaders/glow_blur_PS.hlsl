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

struct VS_out
{
    float4 Position  : POSITION;
    float3 TexCoord0 : TEXCOORD0;
    float4 Color     : COLOR0;
};

sampler2D src : register(s0);

// c0 is librw's fog colour, so these start at c1.
float4 weights : register(c1);   // the four tap weights
float4 offs01  : register(c2);   // taps 0 and 1, in texture coordinates
float4 offs23  : register(c3);   // taps 2 and 3

float4 main(VS_out input) : COLOR
{
    float2 uv = input.TexCoord0.xy;

    float4 c  = tex2D(src, uv + offs01.xy) * weights.x;
    c += tex2D(src, uv + offs01.zw) * weights.y;
    c += tex2D(src, uv + offs23.xy) * weights.z;
    c += tex2D(src, uv + offs23.zw) * weights.w;

    return c;
}
