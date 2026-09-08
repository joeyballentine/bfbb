const char *glow_bright_frag_src =
"// The glow's bright pass, GL3. The account of where the threshold comes from is\n"
"// in shaders/glow_bright_PS.hlsl; this is the same arithmetic.\n"

"uniform sampler2D tex0;\n"

"FSIN vec4 v_color;\n"
"FSIN vec2 v_tex0;\n"
"FSIN float v_fog;\n"

"void\n"
"main(void)\n"
"{\n"
"	// The V flip librw's own im2d fragment shader does. A camera texture is\n"
"	// stored with the top of the picture at v = 1, and the quad's coordinates\n"
"	// are written the other way up because they came from the D3D9 pass.\n"
"	vec2 uv = vec2(v_tex0.x, 1.0 - v_tex0.y);\n"

"	vec3 c = texture(tex0, uv).rgb;\n"
"	vec4 color = vec4(clamp(c*2.0 - 1.0, 0.0, 1.0), 1.0);\n"

"	// No fog term. The pass draws with FOGENABLE off, and the D3D9 shader\n"
"	// ignores fog for the same reason.\n"
"	DoAlphaTest(color.a);\n"
"	FRAGCOLOR(color);\n"
"}\n"
;
