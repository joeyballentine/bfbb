// The pixel stage's constant registers, as the D3D11 backend keeps them.
//
// librw's shaders11/pixelConstants.h says why the register file survives into
// D3D11 and how packoffset spells a c-register. This is the port's own view of
// the same buffer: c0 is librw's fog colour and c7 its alpha test, and c1 to c3
// are the scratch registers a pass owns while it is drawing.
//
// A cbuffer has to be declared in one piece, so a pass that wants c1 to c3
// defines PSTAIL before including this and its registers land inside the same
// block.

#ifndef PSTAIL
#define PSTAIL
#endif

cbuffer PSShared : register(b0)
{
	float4 fogColor  : packoffset(c0);
	float4 alphaTest : packoffset(c7);
	PSTAIL
};

// rw::AlphaTestFunc.
#define ALPHAALWAYS 0
#define ALPHAGREATEREQUAL 1
#define ALPHALESS 2

// The alpha test, which D3D9 did in the output merger and D3D11 has no state
// for. A pass drawn through im2dOverridePS is subject to it exactly as the
// shader it replaces would have been.
void DoAlphaTest(float alpha)
{
	int func = (int)alphaTest.x;
	if(func == ALPHAGREATEREQUAL)
		clip(alpha - alphaTest.y);
	else if(func == ALPHALESS)
		clip(alphaTest.y - alpha - 1.0/512.0);
}
