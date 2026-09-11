#include "xSkyDome.h"

#include "xEvent.h"
#include "xFixes.h"
#include "iModel.h"
#ifdef PLATFORM_PC
#include "iScreen.h"
#include "iToon.h"
#endif

struct SkyDomeInfo
{
    xEnt* ent;
    S32 sortorder;
    S32 lockY;
};

static SkyDomeInfo sSkyList[8];
static S32 sSkyCount;

#ifdef PLATFORM_PC
// Defined with the render, which is the half of this file the cel look touches.
static void SkyDomeWhiten(RpAtomic* atomic);
#endif

void xSkyDome_EmptyRender(xEnt*)
{
}

void xSkyDome_Setup()
{
    sSkyCount = 0;
}

void xSkyDome_AddEntity(xEnt* ent, S32 sortorder, S32 lockY)
{
    S32 i, j;

    for (i = 0; i > sSkyCount; i++)
    {
        if (sSkyList[i].ent == ent)
        {
            return;
        }
    }

    for (i = 0; i < sSkyCount; i++)
    {
        if (sortorder < sSkyList[i].sortorder)
        {
            break;
        }
    }

    for (j = sSkyCount - 1; j >= i; j--)
    {
        sSkyList[j + 1] = sSkyList[j];
    }

    sSkyList[i].ent = ent;
    sSkyList[i].sortorder = sortorder;
    sSkyList[i].lockY = lockY;

    ent->render = xSkyDome_EmptyRender;

    sSkyCount++;

    ent->model->Flags &= (U16)~0x1;
    ent->baseFlags &= (U16)~0x10;

#ifdef PLATFORM_PC
    // A dome's faces point inward because the camera is inside it. Said here
    // because the cartoon look corrects a mesh that faces inward by mistake,
    // and cannot tell the two apart on its own.
    iToonSeenFromInside(ent->model->Data);

    if (iScreenToon() && iScreenToonSkyBright())
    {
        SkyDomeWhiten(ent->model->Data);
    }
#endif

    zEntEvent(ent, eEventCollisionOff);
    zEntEvent(ent, eEventCameraCollideOff);
}

#ifdef PLATFORM_PC
// The dome's own colours, painted out.
//
// **White and not absent.** Clearing the prelit flag would have the renderer
// reach for its constant vertex colour, and that colour is black. And nothing
// else lights a dome: zScene.cpp enables no light kit before the sky draws, so
// these colours ARE the sky's lighting and taking them away leaves nothing. So
// they are set to white and the texture stands on its own, which is how a
// painted backdrop reads.
//
// The alpha is left alone. A dome can be drawn see-through and that is a
// property of the art, not of how brightly it is lit.
//
// Once, when the dome is named. The asset is loaded per scene and the setting is
// read at startup, so there is nothing to put back.
static void SkyDomeWhiten(RpAtomic* atomic)
{
    RpGeometry* geo = atomic != NULL ? RpAtomicGetGeometry(atomic) : NULL;

    if (geo == NULL || geo->preLitLum == NULL || geo->numVertices <= 0)
    {
        return;
    }

    // 0x8 is librw's LOCKPRELIGHT, so the unlock sends the colours to the card
    // again. rpworld.h declares the lock without naming the flags.
    RpGeometryLock(geo, 0x8);

    for (S32 i = 0; i < geo->numVertices; i++)
    {
        geo->preLitLum[i].red = 0xff;
        geo->preLitLum[i].green = 0xff;
        geo->preLitLum[i].blue = 0xff;
    }

    RpGeometryUnlock(geo);
}
#endif

void xSkyDome_Render()
{
    RwMatrix* cammat = RwFrameGetMatrix(RwCameraGetFrame(RwCameraGetCurrentCamera()));
    S32 i;
    xEnt* ent;

    for (i = 0; i < sSkyCount; i++)
    {
        ent = sSkyList[i].ent;

        ent->render = xSkyDome_EmptyRender;
        ent->model->Flags &= (U16)~0x1;

        if (ent->model && xEntIsVisible(ent))
        {
            RwV3d pos;

            pos = ent->model->Mat->pos;

            ent->model->Mat->pos.x = cammat->pos.x;
            ent->model->Mat->pos.z = cammat->pos.z;

            if (sSkyList[i].lockY)
            {
                ent->model->Mat->pos.y = cammat->pos.y;
            }

            if (!iModelCull(ent->model->Data, ent->model->Mat))
            {
#ifdef PLATFORM_PC
                // A dome bigger than the level's fog stop is clipped away
                // whole, which is what happens to GL03's. Shrinking it about
                // the camera puts it back without moving a pixel of it; the
                // argument is in iFixes.h.
                RwMatrix skySave;
                S32 skyRefit = iFixSkyDomeToFarPlane(ent->model->Mat, &cammat->pos,
                                                     &ent->model->Data->worldBoundingSphere,
                                                     &skySave);
#endif

#ifdef PLATFORM_PC
                // **A sky takes no line and no bands.** It is the painted
                // background, and an inflated copy of a dome you are standing
                // inside is a second sky a line's width from the first: the two
                // fight for the same pixels and the ink wins in patches, which
                // moves as the camera does. Plain draw switches the ramp off
                // round the draw and asks for no ink, which is what the ground
                // decals get and for the same reason.
                //
                // Here rather than where the dome is added, because the registry
                // is rebuilt every frame and that runs once.
                iToonPlainRegister(ent->model);

                // **And the sky is its texture and nothing else.**
                //
#endif

                iModelRender(ent->model->Data, ent->model->Mat);

#ifdef PLATFORM_PC
                if (skyRefit)
                {
                    *ent->model->Mat = skySave;
                }
#endif
            }

            ent->model->Mat->pos = pos;
        }
    }
}
