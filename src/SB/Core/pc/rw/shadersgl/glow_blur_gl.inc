const char *glow_blur_frag_src =
"// The glow's blur pass, GL3. Four taps down one axis; the weights, the offsets\n"
"// and where they were read from are in shaders/glow_blur_PS.hlsl.\n"

"uniform sampler2D tex0;\n"

"// The D3D9 shader takes these at c1, c2 and c3. Registers are the wrong idea\n"
"// here, so they are named -- and registered before any shader is created, for\n"
"// the reason glow.cpp's registerGlowUniforms gives.\n"
"uniform vec4 u_glowWeights;\n"
"uniform vec4 u_glowOffs01;\n"
"uniform vec4 u_glowOffs23;\n"

"FSIN vec4 v_color;\n"
"FSIN vec2 v_tex0;\n"
"FSIN float v_fog;\n"

"void\n"
"main(void)\n"
"{\n"
"	// See glow_bright.frag. The offsets are added in this flipped space, which\n"
"	// turns the vertical pass upside down -- and the kernel is symmetric about\n"
"	// zero, so that is the same kernel.\n"
"	vec2 uv = vec2(v_tex0.x, 1.0 - v_tex0.y);\n"

"	vec4 color  = texture(tex0, uv + u_glowOffs01.xy) * u_glowWeights.x;\n"
"	color += texture(tex0, uv + u_glowOffs01.zw) * u_glowWeights.y;\n"
"	color += texture(tex0, uv + u_glowOffs23.xy) * u_glowWeights.z;\n"
"	color += texture(tex0, uv + u_glowOffs23.zw) * u_glowWeights.w;\n"

"	DoAlphaTest(color.a);\n"
"	FRAGCOLOR(color);\n"
"}\n"
;
