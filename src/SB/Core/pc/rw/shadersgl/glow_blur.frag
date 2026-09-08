// The glow's blur pass, GL3. Four taps down one axis; the weights, the offsets
// and where they were read from are in shaders/glow_blur_PS.hlsl.

uniform sampler2D tex0;

// The D3D9 shader takes these at c1, c2 and c3. Registers are the wrong idea
// here, so they are named -- and registered before any shader is created, for
// the reason glow.cpp's registerGlowUniforms gives.
uniform vec4 u_glowWeights;
uniform vec4 u_glowOffs01;
uniform vec4 u_glowOffs23;

FSIN vec4 v_color;
FSIN vec2 v_tex0;
FSIN float v_fog;

void
main(void)
{
	// See glow_bright.frag. The offsets are added in this flipped space, which
	// turns the vertical pass upside down -- and the kernel is symmetric about
	// zero, so that is the same kernel.
	vec2 uv = vec2(v_tex0.x, 1.0 - v_tex0.y);

	vec4 color  = texture(tex0, uv + u_glowOffs01.xy) * u_glowWeights.x;
	color += texture(tex0, uv + u_glowOffs01.zw) * u_glowWeights.y;
	color += texture(tex0, uv + u_glowOffs23.xy) * u_glowWeights.z;
	color += texture(tex0, uv + u_glowOffs23.zw) * u_glowWeights.w;

	DoAlphaTest(color.a);
	FRAGCOLOR(color);
}
