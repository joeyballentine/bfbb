const char *distort_frag_src =
"// The cruise bubble's screen distortion, GL3. What it computes and where every\n"
"// part of it was recovered from are in shaders/distort_PS.hlsl.\n"

"// Stage 0 is the copy of the screen, stage 1 the swirl map -- the same way\n"
"// round as the D3D9 shader, and for the same reason.\n"
"uniform sampler2D tex0;\n"
"uniform sampler2D tex1;\n"

"// The displacement vector, in texture-coordinate units and already scaled by\n"
"// the effect's strength. The D3D9 shader takes it at c1.\n"
"uniform vec4 u_distortDisplace;\n"

"FSIN vec4 v_color;\n"
"FSIN vec2 v_tex0;\n"
"FSIN float v_fog;\n"

"void\n"
"main(void)\n"
"{\n"
"	// See glow_bright.frag for the flip.\n"
"	vec2 uv = vec2(v_tex0.x, 1.0 - v_tex0.y);\n"

"	// Red alone is the signal, expanded to +/-1. Both the reason and the\n"
"	// expansion are in the D3D9 shader.\n"
"	float d = texture(tex1, uv).r * 2.0 - 1.0;\n"

"	// V points the other way here, so a displacement written against D3D's\n"
"	// texture space would turn the warp the other way round. Negated so it\n"
"	// turns the way it does there.\n"
"	vec2 offset = vec2(u_distortDisplace.x, -u_distortDisplace.y);\n"

"	vec4 color = texture(tex0, uv + d*offset);\n"

"	// The vertex colour and the fog are ignored, as they are in the D3D9\n"
"	// shader: this pass overwrites the frame rather than tinting it.\n"
"	DoAlphaTest(color.a);\n"
"	FRAGCOLOR(color);\n"
"}\n"
;
