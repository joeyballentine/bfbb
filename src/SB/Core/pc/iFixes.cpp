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

    // How long a target can go without a melee hit before the next one is a
    // new contact. One attack's hits on a target land on consecutive updates;
    // two attacks are further apart than this.
    const F64 kMeleeContactGap = 0.1;

    // Targets in contact at once. A spin touches two or three at most; when
    // this fills, the stalest entry goes, which has long since ended.
    const S32 kMeleeContacts = 16;

    struct MeleeContact
    {
        const void* ent;
        F64 last;
    };

    MeleeContact sMeleeContacts[kMeleeContacts];

    S32 sMenuRope = TRUE;
    S32 sSkyClip = TRUE;
    S32 sMeleeOnce = TRUE;
    S32 sSpatulaSpin = TRUE;
}

S32 iFixMenuRope()
{
    return sMenuRope;
}

void iFixSetMenuRope(S32 on)
{
    sMenuRope = on;
}

S32 iFixSkyClip()
{
    return sSkyClip;
}

void iFixSetSkyClip(S32 on)
{
    sSkyClip = on;
}

S32 iFixMeleeOnce()
{
    return sMeleeOnce;
}

void iFixSetMeleeOnce(S32 on)
{
    sMeleeOnce = on;
}

S32 iFixSpatulaSpin()
{
    return sSpatulaSpin;
}

void iFixSetSpatulaSpin(S32 on)
{
    sSpatulaSpin = on;
}

S32 iFixMeleeNewContact(const void* ent, F64 now)
{
    if (!sMeleeOnce || ent == NULL)
    {
        return TRUE;
    }

    for (S32 i = 0; i < kMeleeContacts; i++)
    {
        if (sMeleeContacts[i].ent == ent)
        {
            F64 gap = now - sMeleeContacts[i].last;
            sMeleeContacts[i].last = now;

            // A clock that went backwards is a new game, not a contact.
            return gap < 0.0 || gap > kMeleeContactGap;
        }
    }

    MeleeContact* slot = &sMeleeContacts[0];
    for (S32 i = 0; i < kMeleeContacts; i++)
    {
        if (sMeleeContacts[i].ent == NULL)
        {
            slot = &sMeleeContacts[i];
            break;
        }
        if (sMeleeContacts[i].last < slot->last)
        {
            slot = &sMeleeContacts[i];
        }
    }

    slot->ent = ent;
    slot->last = now;
    return TRUE;
}

S32 iFixSkyDomeToFarPlane(U32 sceneID, RwMatrix* mat, const RwV3d* eye, const RwSphere* world,
                          RwMatrix* saved)
{
    if (!sSkyClip || sceneID != 'GL03' || mat == NULL || eye == NULL || world == NULL ||
        saved == NULL)
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
