// The boxed menu backdrop and the margins beside it; see iScreenUIBleedBoxed
// in iScreen.h.

#include "iScreen.h"

#include "rwplcore.h"
#include "xString.h"
#include <types.h>

namespace
{
    // The texture coordinate at (s, t) across the quad, 0..1 each way from the
    // (x1, y1) corner. `uv` is the four corners in the order zUI draws them:
    // (x1,y1), (x1,y2), (x2,y2), (x2,y1).
    void Lerp(const F32* uv, F32 s, F32 t, F32* u, F32* v)
    {
        const F32 topU = uv[0] + (uv[6] - uv[0]) * s;
        const F32 topV = uv[1] + (uv[7] - uv[1]) * s;
        const F32 botU = uv[2] + (uv[4] - uv[2]) * s;
        const F32 botV = uv[3] + (uv[5] - uv[3]) * s;
        *u = topU + (botU - topU) * t;
        *v = topV + (botV - topV) * t;
    }

    // One margin: screen rect (x1..x2, y1..y2) showing the texture from
    // (s1, t1) at its first corner to (s2, t2) at its opposite one.
    void Quad(F32 x1, F32 y1, F32 x2, F32 y2, F32 s1, F32 t1, F32 s2, F32 t2, F32 z,
              const F32* uv)
    {
        static RwImVertexIndex kIndex[6] = { 0, 1, 2, 0, 2, 3 };
        RwIm2DVertex v[4];

        const F32 xs[4] = { x1, x1, x2, x2 };
        const F32 ys[4] = { y1, y2, y2, y1 };
        const F32 ss[4] = { s1, s1, s2, s2 };
        const F32 ts[4] = { t1, t2, t2, t1 };

        for (int i = 0; i < 4; i++)
        {
            F32 u, w;
            Lerp(uv, ss[i], ts[i], &u, &w);
            RwIm2DVertexSetIntRGBA(&v[i], 0xFF, 0xFF, 0xFF, 0xFF);
            RwIm2DVertexSetScreenX(&v[i], xs[i]);
            RwIm2DVertexSetScreenY(&v[i], ys[i]);
            RwIm2DVertexSetScreenZ(&v[i], z);
            RwIm2DVertexSetU(&v[i], u, 0);
            RwIm2DVertexSetV(&v[i], w, 0);
        }

        RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, v, 4, kIndex, 6);
    }

    // The width of the strip repeated into the margins, as a fraction of the
    // texture: plain paper on the options backdrop, short of the panel shadow.
    const F32 kEdge = 0.06f;
} // namespace

S32 iScreenUIBleedBoxed(U32 textureID)
{
    static const U32 kOptions = xStrHash("ui_blue_alpha_3");
    return textureID == kOptions;
}

void iScreenUIDrawBleed(F32 x1, F32 y1, F32 x2, F32 y2, F32 z, const F32* uv)
{
    if (iScreenGetUIMode() != iSCREENUI_NATIVE)
    {
        return;
    }

    const F32 w = x2 - x1;
    const F32 h = y2 - y1;
    if (w <= 0.0f || h <= 0.0f)
    {
        return;
    }

    const F32 screenW = iScreenWidthF();
    const F32 screenH = iScreenHeightF();

    // Outward from each edge, tiles of the edge strip, every other one
    // mirrored, so no tile meets its neighbour with a step.
    F32 at = x1;
    for (S32 i = 0; at > 0.0f; i++)
    {
        const F32 far = at - kEdge * w;
        const F32 s = (i & 1) ? 0.0f : kEdge;
        Quad(far, y1, at, y2, s, 0.0f, kEdge - s, 1.0f, z, uv);
        at = far;
    }
    at = x2;
    for (S32 i = 0; at < screenW; i++)
    {
        const F32 far = at + kEdge * w;
        const F32 s = (i & 1) ? 1.0f : 1.0f - kEdge;
        Quad(at, y1, far, y2, 2.0f - kEdge - s, 0.0f, s, 1.0f, z, uv);
        at = far;
    }
    at = y1;
    for (S32 i = 0; at > 0.0f; i++)
    {
        const F32 far = at - kEdge * h;
        const F32 t = (i & 1) ? 0.0f : kEdge;
        Quad(x1, far, x2, at, 0.0f, t, 1.0f, kEdge - t, z, uv);
        at = far;
    }
    at = y2;
    for (S32 i = 0; at < screenH; i++)
    {
        const F32 far = at + kEdge * h;
        const F32 t = (i & 1) ? 1.0f : 1.0f - kEdge;
        Quad(x1, at, x2, far, 0.0f, 2.0f - kEdge - t, 1.0f, t, z, uv);
        at = far;
    }
}
