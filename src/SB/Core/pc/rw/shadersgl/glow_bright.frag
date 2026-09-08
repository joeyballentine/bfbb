// The glow's bright pass, GL3. The account of where the threshold comes from is
// in shaders/glow_bright_PS.hlsl; this is the same arithmetic.

uniform sampler2D tex0;

FSIN vec4 v_color;
FSIN vec2 v_tex0;
FSIN float v_fog;

void
main(void)
{
	// The V flip librw's own im2d fragment shader does. A camera texture is
	// stored with the top of the picture at v = 1, and the quad's coordinates
	// are written the other way up because they came from the D3D9 pass.
	vec2 uv = vec2(v_tex0.x, 1.0 - v_tex0.y);

	vec3 c = texture(tex0, uv).rgb;
	vec4 color = vec4(clamp(c*2.0 - 1.0, 0.0, 1.0), 1.0);

	// No fog term. The pass draws with FOGENABLE off, and the D3D9 shader
	// ignores fog for the same reason.
	DoAlphaTest(color.a);
	FRAGCOLOR(color);
}
