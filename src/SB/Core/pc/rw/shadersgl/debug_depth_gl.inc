const char *debug_depth_frag_src =
"// The depth buffer, drawn as a picture.\n"
"//\n"
"// A diagnostic, not a game effect. The depth copy arrives on stage 1 rather\n"
"// than stage 0 because stage 0 is the one librw's render state binds, and there\n"
"// is no raster to bind for a depth texture.\n"

"uniform sampler2D tex1;\n"

"// x: the near plane. y: the far plane. z: 0 for the greyscale view, 1 for\n"
"// contour bands. w unused.\n"
"uniform vec4 u_debugDepth;\n"

"FSIN vec4 v_color;\n"
"FSIN vec2 v_tex0;\n"
"FSIN float v_fog;\n"

"void\n"
"main(void)\n"
"{\n"
"	// See glow_bright.frag for the flip.\n"
"	vec2 uv = vec2(v_tex0.x, 1.0 - v_tex0.y);\n"

"	float d = texture(tex1, uv).r;\n"

"	float n = u_debugDepth.x;\n"
"	float f = u_debugDepth.y;\n"

"	// The projection writes d = f*(z - n) / ((f - n)*z), which is the same\n"
"	// mapping in both APIs. Turned back into a view-space distance so that the\n"
"	// picture reads as distance rather than as the buffer's own crowding.\n"
"	float denom = max(1.0 - d*(f - n)/f, 1.0/65536.0);\n"
"	float z = n / denom;\n"
"	float linear = clamp((z - n) / (f - n), 0.0, 1.0);\n"

"	vec3 c;\n"
"	if(u_debugDepth.z < 0.5)\n"
"	{\n"
"		// Near white, far black.\n"
"		c = vec3(1.0 - linear);\n"
"	}\n"
"	else\n"
"	{\n"
"		// Thirty-two contour bands, evenly spaced in distance. A surface drawn\n"
"		// at the wrong depth lands in the wrong band, which is a great deal\n"
"		// easier to see than a shade of grey.\n"
"		c = vec3(fract(linear * 32.0));\n"
"	}\n"

"	FRAGCOLOR(vec4(c, 1.0));\n"
"}\n"
;
