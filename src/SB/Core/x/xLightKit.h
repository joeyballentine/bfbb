#ifndef XLIGHTKIT_H
#define XLIGHTKIT_H

#include <types.h>
#include <rwcore.h>
#include <rpworld.h>

struct xLightKitLight
{
    U32 type;
    RwRGBAReal color;
    F32 matrix[16];
    F32 radius;
    F32 angle;
    RpLight* platLight;
};

struct xLightKit
{
    U32 tagID;
    U32 groupID;
    U32 lightCount;
    xLightKitLight* lightList;
};

extern S32 iModelHack_DisablePrelight;
extern xLightKit* gLastLightKit;

xLightKit* xLightKit_Prepare(void* data);
void xLightKit_Enable(xLightKit* lkit, RpWorld* world);
xLightKit* xLightKit_GetCurrent(RpWorld* world);
void xLightKit_Destroy(xLightKit* lkit);

#ifdef PLATFORM_PC
// Scale a kit's lights to the time of day, from the colours the artists gave
// them rather than from whatever they hold now, so calling it repeatedly does
// not compound. Does nothing while the cycle is off. See iDayNight.h.
void xLightKit_DayNight(xLightKit* lkit);
#endif

#endif
