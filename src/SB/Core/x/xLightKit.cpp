#include "xLightKit.h"

#ifdef PLATFORM_PC
#include "iDayNight.h"
#endif
#include "xMath.h"

#include <types.h>
#include <string.h>

#ifdef PLATFORM_PC
#include <stdio.h>
#include <stdlib.h>
#endif

S32 iModelHack_DisablePrelight;
xLightKit* gLastLightKit;

xLightKit* xLightKit_Prepare(void* data)
{
    xLightKit* lkit = (xLightKit*)data;
#ifdef BFBB_PTR64
    // 16 bytes into the asset is where the lights start on disc and where
    // sizeof(xLightKit) puts them on a 32-bit build. Where the struct is wider
    // the two part company, and every caller here hands over a real xLightKit
    // with its lights behind it -- LKIT assets included, because LightKit_Read
    // in zAssetTypes.cpp lays them out that way.
    lkit->lightList = (xLightKitLight*)(lkit + 1);
    xLightKitLight* currlight = (xLightKitLight*)(lkit + 1);
#else
    lkit->lightList = (xLightKitLight*)((int*)data + 4);
    xLightKitLight* currlight = (xLightKitLight*)((int*)data + 4);
#endif

    for (int i = 0; i < lkit->lightCount; currlight++, i++)
    {
        if (currlight->platLight != NULL)
        {
            return lkit;
        }

        // If any of the colors is greater than 1.0, normalize back to 0-1
        if (currlight->color.red > 1.0f || currlight->color.green > 1.0f ||
            currlight->color.blue > 1.0f)
        {
            F32 s;
            s = MAX(MAX(currlight->color.red, currlight->color.green), currlight->color.blue);
            s = MAX(s, 0.00001f);
            s = 1.0f / s;
            currlight->color.red *= s;
            currlight->color.green *= s;
            currlight->color.blue *= s;
        }

        switch (currlight->type)
        {
        case 1:
            currlight->platLight = RpLightCreate(2);
            break;
        case 2:
            currlight->platLight = RpLightCreate(1);
            break;
        case 3:
            currlight->platLight = RpLightCreate(128);
            break;
        case 4:
            currlight->platLight = RpLightCreate(130);
            break;
        default:
            break;
        }
        RpLightSetColor(currlight->platLight, &currlight->color);
        if (currlight->type >= 2)
        {
            RwFrame* frame = RwFrameCreate();
            RwMatrixTag tmpmat;

            memset(&tmpmat, 0, 64);
            tmpmat.right.x = -currlight->matrix[0];
            tmpmat.right.y = -currlight->matrix[1];
            tmpmat.right.z = -currlight->matrix[2];
            tmpmat.up.x = currlight->matrix[4];
            tmpmat.up.y = currlight->matrix[5];
            tmpmat.up.z = currlight->matrix[6];
            tmpmat.at.x = -currlight->matrix[8];
            tmpmat.at.y = -currlight->matrix[9];
            tmpmat.at.z = -currlight->matrix[10];
            tmpmat.pos.x = currlight->matrix[12];
            tmpmat.pos.y = currlight->matrix[13];
            tmpmat.pos.z = currlight->matrix[14];
            RwV3dNormalize(&tmpmat.right, &tmpmat.right);
            RwV3dNormalize(&tmpmat.up, &tmpmat.up);
            RwV3dNormalize(&tmpmat.at, &tmpmat.at);
            RwFrameTransform(frame, &tmpmat, rwCOMBINEREPLACE);
            _rwObjectHasFrameSetFrame(currlight->platLight, frame);
        }
        if (currlight->type >= 3)
        {
            RpLightSetRadius(currlight->platLight, currlight->radius);
        }
        if (currlight->type >= 4)
        {
            RpLightSetConeAngle(currlight->platLight, currlight->angle);
        }
    }

    return (xLightKit*)data;
}

#ifdef PLATFORM_PC
void xLightKit_DayNight(xLightKit* lkit)
{
    if (lkit == NULL || !iDayNightActive())
    {
        return;
    }

    F32 amb[3];
    F32 dir[3];

    iDayNightTint(amb, dir);

    for (U32 i = 0; i < lkit->lightCount; i++)
    {
        xLightKitLight* l = &lkit->lightList[i];

        if (l->platLight == NULL)
        {
            continue;
        }

        // Type 1 is the ambient; everything else is a light with a direction.
        // xLightKit_Prepare switches on the same numbers.
        const F32* mul = (l->type == 1) ? amb : dir;
        RwRGBAReal c;

        c.red = l->color.red * mul[0];
        c.green = l->color.green * mul[1];
        c.blue = l->color.blue * mul[2];
        c.alpha = l->color.alpha;

        RpLightSetColor(l->platLight, &c);
    }
}
#endif

void xLightKit_Enable(xLightKit* lkit, RpWorld* world)
{
#ifdef PLATFORM_PC
    // Every kit gets the time of day on its way in, world and characters alike.
    // One place, so a character and the ground it stands on cannot disagree.
    xLightKit_DayNight(lkit);
#endif

    if (lkit != gLastLightKit)
    {
        int i;
        if (gLastLightKit != NULL)
        {
            for (i = 0; i < gLastLightKit->lightCount; i++)
            {
                RpWorldRemoveLight(world, gLastLightKit->lightList[i].platLight);
            }
        }
        gLastLightKit = lkit;
        if (lkit != NULL)
        {
            iModelHack_DisablePrelight = 1;
            for (i = 0; i < lkit->lightCount; i++)
            {
                RpWorldAddLight(world, lkit->lightList[i].platLight);
            }
        }
        else
        {
            iModelHack_DisablePrelight = 0;
        }
    }
}

xLightKit* xLightKit_GetCurrent(RpWorld* world)
{
    return gLastLightKit;
}

void xLightKit_Destroy(xLightKit* lkit)
{
    if (lkit == NULL)
    {
        return;
    }

    int i;
    xLightKitLight* currLight = lkit->lightList;

    for (i = 0; i < lkit->lightCount; currLight++, i++)
    {
        if (currLight->platLight != NULL)
        {
            _rwFrameSyncDirty();
            RwFrame* tframe = (RwFrame*)(currLight->platLight->object).object.parent;
            if (tframe != NULL)
            {
                _rwObjectHasFrameSetFrame(currLight->platLight, 0);
                RwFrameDestroy(tframe);
            }
            RpLightDestroy(currLight->platLight);
            currLight->platLight = 0;
        }
    }
}
