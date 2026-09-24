#include "iMenuWide.h"

#include "iScreen.h"
#include "xString.h"
#include "xstransvc.h"
#include "zUI.h"

namespace
{
    // 16:9's margin is 640 / 6, about 107.
    const F32 kMaxMargin = 120.0f;
} // namespace

F32 iMenuWideMargin()
{
    F32 m = iScreenAnchorMarginXF() * 640.0f;
    return m < kMaxMargin ? m : kMaxMargin;
}

void iMenuWidePlace(const char* name, F32 x, F32 w)
{
    zUIAsset* a = (zUIAsset*)xSTFindAsset(xStrHash(name), NULL);
    if (a == NULL)
    {
        return;
    }
    a->pos.x = x;
    a->dim[0] = (U16)(w > 0.0f ? w + 0.5f : 0.0f);
}
