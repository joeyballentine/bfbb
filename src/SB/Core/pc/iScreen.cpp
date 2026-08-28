// The render size. The argument for it being one number is in iScreen.h.

#include "iScreen.h"

#include <stdio.h>

namespace
{
    // Retail's framebuffer, and the port's default. Everything the game draws
    // in pixels was authored against these two numbers.
    const S32 kDefaultWidth = 640;
    const S32 kDefaultHeight = 480;

    // D3D9's largest guaranteed texture dimension on hardware that reports
    // D3DPTEXTURECAPS_POW2 relief is 8192 on everything shipped this century,
    // and a render target is a texture. Past this the camera rasters simply
    // fail to allocate, which surfaces much further on as a black screen; a
    // number this far out is a typo rather than a request.
    const S32 kMaxDimension = 16384;

    S32 sWidth = kDefaultWidth;
    S32 sHeight = kDefaultHeight;
}

S32 iScreenWidth()
{
    return sWidth;
}

S32 iScreenHeight()
{
    return sHeight;
}

F32 iScreenWidthF()
{
    return (F32)sWidth;
}

F32 iScreenHeightF()
{
    return (F32)sHeight;
}

void iScreenSetSize(S32 width, S32 height)
{
    if (width <= 0 || height <= 0 || width > kMaxDimension || height > kMaxDimension)
    {
        printf("bfbb: %dx%d is not a resolution this can render at; staying at %dx%d\n",
               (int)width, (int)height, (int)sWidth, (int)sHeight);
        fflush(stdout);
        return;
    }

    sWidth = width;
    sHeight = height;
}
