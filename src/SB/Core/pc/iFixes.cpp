// The switches for the original game's bugs, and the one piece of geometry the
// sky fix needs. What each fix is for is in iFixes.h.

#include "iFixes.h"

#include <rwcore.h>

#include <math.h>

namespace
{
    // How much of the far plane the dome is allowed to reach. The clip is on
    // the plane itself, so a dome refitted exactly onto it would sit on the
    // boundary and lose whichever vertices round the wrong way.
    const F32 kFarPlaneMargin = 0.98f;

    S32 sSkyClip = TRUE;
}

S32 iFixSkyClip()
{
    return sSkyClip;
}

void iFixSetSkyClip(S32 on)
{
    sSkyClip = on;
}

S32 iFixSkyDomeToFarPlane(RwMatrix* mat, const RwV3d* eye, const RwSphere* world, RwMatrix* saved)
{
    if (!sSkyClip || mat == NULL || eye == NULL || world == NULL || saved == NULL)
    {
        return FALSE;
    }

    RwCamera* camera = RwCameraGetCurrentCamera();
    if (camera == NULL)
    {
        return FALSE;
    }

    F32 dx = world->center.x - eye->x;
    F32 dy = world->center.y - eye->y;
    F32 dz = world->center.z - eye->z;

    // The farthest the model can reach from the eye. The sphere is the one
    // iModelCull just built, so this costs a square root and nothing else.
    F32 reach = sqrtf(dx * dx + dy * dy + dz * dz) + world->radius;
    F32 room = kFarPlaneMargin * camera->farPlane;

    if (reach <= room || room <= 0.0f)
    {
        return FALSE;
    }

    F32 k = room / reach;

    *saved = *mat;

    // Radially about the eye: the basis shrinks by k and the origin moves the
    // same fraction of the way in, so every vertex ends at eye + k*(v - eye)
    // and keeps its direction.
    mat->right.x *= k;
    mat->right.y *= k;
    mat->right.z *= k;
    mat->up.x *= k;
    mat->up.y *= k;
    mat->up.z *= k;
    mat->at.x *= k;
    mat->at.y *= k;
    mat->at.z *= k;
    mat->pos.x = eye->x + k * (mat->pos.x - eye->x);
    mat->pos.y = eye->y + k * (mat->pos.y - eye->y);
    mat->pos.z = eye->z + k * (mat->pos.z - eye->z);

    return TRUE;
}
