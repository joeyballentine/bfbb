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

    // The shape everything the game draws was authored in.
    const F32 kUIAspect = 4.0f / 3.0f;

    S32 sWidth = kDefaultWidth;
    S32 sHeight = kDefaultHeight;

    // Derived once here rather than at every call site, all of which are in
    // per-frame drawing code.
    F32 sAspect = (F32)kDefaultHeight / (F32)kDefaultWidth;
    F32 sUIWidth = (F32)kDefaultWidth;
    F32 sUIHeight = (F32)kDefaultHeight;
    F32 sUIOriginX = 0.0f;
    F32 sUIOriginY = 0.0f;
    F32 sUIFracX = 1.0f;
    F32 sUIFracY = 1.0f;

    // The largest 4:3 rectangle that fits, centred. The same fit iFMV.cpp puts
    // a 4:3 movie through, and for the same reason: what is left over stays
    // black rather than being filled by stretching the picture into it.
    void deriveUIBox()
    {
        F32 w = (F32)sWidth;
        F32 h = (F32)sHeight;

        sAspect = h / w;

        sUIWidth = w;
        sUIHeight = w / kUIAspect;
        if (sUIHeight > h)
        {
            sUIHeight = h;
            sUIWidth = h * kUIAspect;
        }

        sUIOriginX = 0.5f * (w - sUIWidth);
        sUIOriginY = 0.5f * (h - sUIHeight);

        sUIFracX = sUIWidth / w;
        sUIFracY = sUIHeight / h;
    }
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

F32 iScreenAspectF()
{
    return sAspect;
}

F32 iScreenUIWidthF()
{
    return sUIWidth;
}

F32 iScreenUIHeightF()
{
    return sUIHeight;
}

F32 iScreenUIOriginXF()
{
    return sUIOriginX;
}

F32 iScreenUIOriginYF()
{
    return sUIOriginY;
}

F32 iScreenUIFracXF()
{
    return sUIFracX;
}

F32 iScreenUIFracYF()
{
    return sUIFracY;
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

    deriveUIBox();
}
