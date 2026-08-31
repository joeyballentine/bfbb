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
    F32 sUIMarginX;
    F32 sUIMarginY;

    iScreenUIMode sUIMode = iSCREENUI_PILLARBOX;

    // How far from the middle a widget has to be authored before it belongs to
    // an edge rather than to the centre of the screen.
    //
    // The number is not arbitrary. Retail's HUD is five groups, and measuring
    // where they sit gives their centres as: the health meter at 0.13, the
    // pickup counter at 0.13 and 0.21, the spatula counter at 0.48 and 0.54,
    // the shiny counter at 0.66 and 0.74, the sock counter at 0.78 and 0.86.
    // A zone of a tenth either side of the middle -- edges at 0.4 and 0.6 --
    // puts every one of those groups WHOLLY inside one zone, with the nearest
    // miss two thirds of the zone away from a boundary. That is what keeps a
    // number beside its icon: both halves get the same offset.
    const F32 kAnchorZone = 0.1f;

    // The offset in force, in UI units. Set per widget, because which edge a
    // widget belongs to is a property of the widget and not of the screen.
    F32 sAnchorOffsetX = 0.0f;
    F32 sAnchorOffsetY = 0.0f;

    // Left edge, right edge, or neither.
    F32 anchorOffset(F32 centre, F32 margin)
    {
        if (centre < 0.5f - kAnchorZone)
        {
            return -margin;
        }

        if (centre > 0.5f + kAnchorZone)
        {
            return margin;
        }

        return 0.0f;
    }

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

        // How far outside the box the screen reaches, in UI units. Zero
        // when the box IS the screen, which is every 4:3 render size.
        sUIMarginX = (1.0f - sUIFracX) / (2.0f * sUIFracX);
        sUIMarginY = (1.0f - sUIFracY) / (2.0f * sUIFracY);
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

F32 iScreenUIMarginXF()
{
    return sUIMarginX;
}

F32 iScreenUIMarginYF()
{
    return sUIMarginY;
}

F32 iScreenAnchorMarginXF()
{
    return sUIMode == iSCREENUI_NATIVE ? sUIMarginX : 0.0f;
}

F32 iScreenAnchorMarginYF()
{
    return sUIMode == iSCREENUI_NATIVE ? sUIMarginY : 0.0f;
}

namespace
{
    S32 sUICover = 0;
}

void iScreenSetUICover(S32 on)
{
    sUICover = on;
}

S32 iScreenUICover()
{
    return sUICover;
}

F32 iScreenStretchX(F32 n)
{
    if (sUIMode != iSCREENUI_NATIVE)
    {
        return sUIOriginX + sUIWidth * n;
    }

    return (F32)sWidth * n;
}

F32 iScreenStretchY(F32 n)
{
    if (sUIMode != iSCREENUI_NATIVE)
    {
        return sUIOriginY + sUIHeight * n;
    }

    return (F32)sHeight * n;
}

void iScreenSetUIMode(iScreenUIMode mode)
{
    sUIMode = mode;

    // Nothing is anchored until a widget asks to be, and pillarbox never
    // anchors anything at all.
    sAnchorOffsetX = 0.0f;
    sAnchorOffsetY = 0.0f;
}

void iScreenSetAnchorRect(F32 x, F32 y, F32 w, F32 h)
{
    if (sUIMode != iSCREENUI_NATIVE)
    {
        sAnchorOffsetX = 0.0f;
        sAnchorOffsetY = 0.0f;
        return;
    }

    sAnchorOffsetX = anchorOffset(x + 0.5f * w, sUIMarginX);
    sAnchorOffsetY = anchorOffset(y + 0.5f * h, sUIMarginY);
}

F32 iScreenAnchorX(F32 x)
{
    return x + sAnchorOffsetX;
}

F32 iScreenAnchorY(F32 y)
{
    return y + sAnchorOffsetY;
}

// Samples per pixel, from config.ini's video.msaa. Held here for the reason the
// render size is: RenderWareInit reads it when it makes the render surfaces,
// and the code that does must not learn what config.ini is.
static S32 sMultiSample = 1;

S32 iScreenMultiSample()
{
    return sMultiSample;
}

void iScreenSetMultiSample(S32 samples)
{
    // 1 is off. D3D9 names its levels after the sample count, so the number is
    // the setting, and anything that is not a level the device grants falls
    // back to none when the surfaces are made.
    if (samples < 1 || samples > 16)
    {
        printf("bfbb: %d is not a sample count this can render at; staying at %d\n",
               (int)samples, (int)sMultiSample);
        fflush(stdout);
        return;
    }

    sMultiSample = samples;
}

// Whether the alpha edges of a cutout are drawn as sample coverage. Needs
// samples to spread across, so it does nothing at one; that is not a mistake to
// report, it is just what the two settings together mean.
static S32 sAlphaToCoverage = 1;

S32 iScreenAlphaToCoverage()
{
    return sAlphaToCoverage;
}

void iScreenSetAlphaToCoverage(S32 on)
{
    sAlphaToCoverage = on ? 1 : 0;
}

// Whether lighting is summed per pixel rather than per vertex. Held here with
// the other two for the same reason: RenderWareInit pushes it into librw, and
// the code that does must not learn what config.ini is.
static S32 sPerPixelLighting = 1;

S32 iScreenPerPixelLighting()
{
    return sPerPixelLighting;
}

void iScreenSetPerPixelLighting(S32 on)
{
    sPerPixelLighting = on ? 1 : 0;
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
