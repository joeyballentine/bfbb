// The `fancy` loading transition. What it is and why it is split across two
// call sites is in iLoadTransition.h.

#include "iLoadTransition.h"

#include "iScreen.h"
#include "iSnapshot.h"

#include <rwcore.h>

namespace
{
    // How long the still takes to leave. This is the whole of the fancy
    // transition: the loading screen itself is the still and nothing else, and
    // the bubbles belong to the wipe.
    //
    // An earlier version was four times this and held the loading screen up
    // beforehand so a wall could run there as well. Both were wrong. The hold
    // read as two animations back to back -- two populations, spawned at two
    // different cameras, and nothing makes them look like one -- and two
    // seconds of wipe is long enough to sit through rather than watch.
    const F32 kWipeSeconds = 0.5f;

    // How long the bubbles get to cover the screen before the still starts to
    // leave. The wall rises from below the bottom edge to past the top over
    // this, and only then does the still move -- so the level is uncovered
    // behind a screen full of bubbles rather than in plain view.
    const F32 kCoverSeconds = 0.35f;

    // Where the rising wall starts and ends, up the screen. Far enough past
    // either edge that the band is entirely off screen at both ends: it must
    // not appear in shot already half way up, and it must not still be there
    // when the still starts to go.
    const F32 kBubbleFrom = -0.10f;
    const F32 kBubbleTo = 1.30f;

    // The soft edge of the wipe, as a fraction of the screen. A hard edge
    // reads as a rectangle sliding up; this reads as the picture dissolving
    // from the bottom.
    const F32 kEdgeFraction = 0.22f;

    // How often the wipe spawns a bubble wall. Fast, because half a second is
    // no time for a wall to build: the pool holds 768 and a wall is 50, so at
    // this rate it is full an eighth of a second in and every spawn after that
    // is a no-op until something dies. Nothing does -- a bubble lives 1.75
    // seconds, well past the end of the wipe.
    const F32 kBubbleInterval = 1.0f / 120.0f;

    S32 sFancy;

    // The wipe owns the still, and how far through it is.
    S32 sWiping;
    F32 sWipeElapsed;
    F32 sBubbleAge;

    // Where the bottom of the still is this frame, as a fraction of the way up
    // the screen: 0 at the bottom edge, 1 at the top. Negative while the still
    // is whole and its soft edge is below the screen.
    F32 sEdgeUp;

    // And where this frame's bubbles go, in the same terms. The rising wall
    // while it is covering the screen, the still's own edge once it is.
    F32 sBubbleUp;

    void wipeFinish()
    {
        sWiping = FALSE;
        sWipeElapsed = 0.0f;
        sBubbleAge = 0.0f;

        // The loading screen's frames were kept out of the snapshot so it
        // could not photograph itself, and the wipe's frames with them --
        // they are half the old level. Presents from here are the level.
        iSnapshotRelease();
    }

    // One horizontal band of the still, alpha ramped down the way the wipe
    // wants it. Screen y counts down from the top, so the reveal is a band
    // travelling towards zero.
    void band(F32 w, F32 h, F32 yTop, F32 yBottom, F32 aTop, F32 aBottom)
    {
        if (yBottom <= yTop)
        {
            return;
        }

        rwGameCube2DVertex vx[4];
        F32 z = RwIm2DGetFarScreenZ();

        const F32 px[4] = { 0.0f, 0.0f, w, w };
        const F32 py[4] = { yTop, yBottom, yTop, yBottom };
        const F32 pa[4] = { aTop, aBottom, aTop, aBottom };

        for (S32 i = 0; i < 4; i++)
        {
            vx[i].x = px[i];
            vx[i].y = py[i];
            vx[i].z = z;

            // White, like the loading screen draws the still: the picture is
            // the picture, not something being tinted. The alpha is the wipe.
            vx[i].emissiveColor.red = 255;
            vx[i].emissiveColor.green = 255;
            vx[i].emissiveColor.blue = 255;
            vx[i].emissiveColor.alpha = (RwUInt8)(pa[i] + 0.5f);

            vx[i].u = px[i] / w;
            vx[i].v = py[i] / h;
        }

        RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, &vx[0], 4);
    }

    void wipeDraw(RwRaster* ras, F32 t)
    {
        const F32 w = iScreenWidthF();
        const F32 h = iScreenHeightF();
        const F32 edge = h * kEdgeFraction;

        // Where the still ends this frame. It starts a full edge-width BELOW
        // the screen so that the first frame is the whole picture, opaque --
        // starting it at h would show the soft edge already eaten into the
        // bottom of the still, which reads as a pop.
        const F32 bottom = (h + edge) * (1.0f - t);
        const F32 top = bottom - edge;

        sEdgeUp = 1.0f - bottom / h;

        if (t > 0.0f)
        {
            sBubbleUp = sEdgeUp;
        }

        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
        RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)ras);

        // Everything above the soft edge is the still, untouched.
        if (top > 0.0f)
        {
            band(w, h, 0.0f, top < h ? top : h, 255.0f, 255.0f);
        }

        // And the edge itself, clipped to the screen. Both ends are evaluated
        // from the ramp rather than assumed to be 255 and 0, because once the
        // band is half off the top of the screen its visible top is partway
        // down the ramp.
        F32 y0 = top > 0.0f ? top : 0.0f;
        F32 y1 = bottom < h ? bottom : h;

        if (y1 > y0)
        {
            band(w, h, y0, y1, 255.0f * ((bottom - y0) / edge), 255.0f * ((bottom - y1) / edge));
        }

        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    }
}

void iLoadTransitionSetFancy(S32 fancy)
{
    sFancy = fancy ? TRUE : FALSE;
}

S32 iLoadTransitionFancy()
{
    return sFancy;
}

void iLoadTransitionBegin()
{
    if (sWiping)
    {
        // A scene change inside the wipe's own seconds. The still the wipe was
        // holding is two levels old now, and no frame has been captured since
        // -- the latch saw to that -- so there is no right picture to hand the
        // new loading screen. Throw it away and let the background asset stand
        // in, which is the console's loading screen exactly.
        sWiping = FALSE;
        iSnapshotDiscard();
    }
}

S32 iLoadTransitionWiping()
{
    return sWiping;
}

F32 iLoadTransitionWipeEdge()
{
    return sEdgeUp;
}

F32 iLoadTransitionBubbleUp()
{
    return sBubbleUp;
}

S32 iLoadTransitionStartWipe()
{
    if (!sFancy)
    {
        return FALSE;
    }

    // No still to wipe: the first boot has no previous frame, xbox.snapshot is
    // off, or a device reset emptied it. The loading screen drew the background
    // asset over those, and an asset is not a picture of anywhere -- wiping it
    // off the new level would reveal nothing.
    if (iSnapshotBackgroundTexture() == NULL)
    {
        return FALSE;
    }

    sWiping = TRUE;
    sWipeElapsed = 0.0f;

    // Both below the screen, which is where they start.
    sEdgeUp = -kEdgeFraction;
    sBubbleUp = kBubbleFrom;

    // Due immediately: the first frame of the wipe is the first frame anyone
    // sees the bubbles, and waiting an interval for it wastes a fiftieth of
    // the whole transition.
    sBubbleAge = kBubbleInterval;

    return TRUE;
}

S32 iLoadTransitionWipeFrame(F32 dt)
{
    if (!sWiping)
    {
        return FALSE;
    }

    RwTexture* tex = iSnapshotBackgroundTexture();
    RwRaster* ras = (tex != NULL) ? (RwRaster*)tex->raster : NULL;

    if (ras == NULL)
    {
        // A device reset mid-wipe. There is nothing left to draw and the
        // release below is what matters.
        wipeFinish();
        return FALSE;
    }

    sWipeElapsed += dt;

    // The still does not move until the bubbles have covered the screen.
    F32 t = (sWipeElapsed - kCoverSeconds) / kWipeSeconds;

    if (t < 0.0f)
    {
        t = 0.0f;

        // Still covering: lay the wall along its own rising front. Once it is
        // covered the front is off the top of the screen and there is nothing
        // up there to spawn into, so the bubbles move to the still's edge and
        // the ones already up carry the cover.
        sBubbleUp = kBubbleFrom +
                    (kBubbleTo - kBubbleFrom) * (sWipeElapsed / kCoverSeconds);
    }

    if (t >= 1.0f)
    {
        wipeFinish();
        return FALSE;
    }

    wipeDraw(ras, t);

    sBubbleAge += dt;

    if (sBubbleAge >= kBubbleInterval)
    {
        sBubbleAge = 0.0f;
        return TRUE;
    }

    return FALSE;
}
