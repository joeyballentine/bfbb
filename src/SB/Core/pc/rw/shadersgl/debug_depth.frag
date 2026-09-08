// The depth buffer, drawn as a picture.
//
// A diagnostic, not a game effect. The depth copy arrives on stage 1 rather
// than stage 0 because stage 0 is the one librw's render state binds, and there
// is no raster to bind for a depth texture.

uniform sampler2D tex1;

// x: the near plane. y: the far plane. z: 0 for the greyscale view, 1 for
// contour bands. w unused.
uniform vec4 u_debugDepth;

FSIN vec4 v_color;
FSIN vec2 v_tex0;
FSIN float v_fog;

void
main(void)
{
	// See glow_bright.frag for the flip.
	vec2 uv = vec2(v_tex0.x, 1.0 - v_tex0.y);

	float d = texture(tex1, uv).r;

	float n = u_debugDepth.x;
	float f = u_debugDepth.y;

	// The projection writes d = f*(z - n) / ((f - n)*z), which is the same
	// mapping in both APIs. Turned back into a view-space distance so that the
	// picture reads as distance rather than as the buffer's own crowding.
	float denom = max(1.0 - d*(f - n)/f, 1.0/65536.0);
	float z = n / denom;
	float linear = clamp((z - n) / (f - n), 0.0, 1.0);

	vec3 c;
	if(u_debugDepth.z < 0.5)
	{
		// Near white, far black.
		c = vec3(1.0 - linear);
	}
	else
	{
		// Thirty-two contour bands, evenly spaced in distance. A surface drawn
		// at the wrong depth lands in the wrong band, which is a great deal
		// easier to see than a shade of grey.
		c = vec3(fract(linear * 32.0));
	}

	FRAGCOLOR(vec4(c, 1.0));
}
