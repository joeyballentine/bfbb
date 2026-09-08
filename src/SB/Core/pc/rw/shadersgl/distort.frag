// The cruise bubble's screen distortion, GL3. What it computes and where every
// part of it was recovered from are in shaders/distort_PS.hlsl.

// Stage 0 is the copy of the screen, stage 1 the swirl map -- the same way
// round as the D3D9 shader, and for the same reason.
uniform sampler2D tex0;
uniform sampler2D tex1;

// The displacement vector, in texture-coordinate units and already scaled by
// the effect's strength. The D3D9 shader takes it at c1.
uniform vec4 u_distortDisplace;

FSIN vec4 v_color;
FSIN vec2 v_tex0;
FSIN float v_fog;

void
main(void)
{
	// See glow_bright.frag for the flip.
	vec2 uv = vec2(v_tex0.x, 1.0 - v_tex0.y);

	// Red alone is the signal, expanded to +/-1. Both the reason and the
	// expansion are in the D3D9 shader.
	float d = texture(tex1, uv).r * 2.0 - 1.0;

	// V points the other way here, so a displacement written against D3D's
	// texture space would turn the warp the other way round. Negated so it
	// turns the way it does there.
	vec2 offset = vec2(u_distortDisplace.x, -u_distortDisplace.y);

	vec4 color = texture(tex0, uv + d*offset);

	// The vertex colour and the fog are ignored, as they are in the D3D9
	// shader: this pass overwrites the frame rather than tinting it.
	DoAlphaTest(color.a);
	FRAGCOLOR(color);
}
