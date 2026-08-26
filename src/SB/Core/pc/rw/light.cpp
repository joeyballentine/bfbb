// RenderWare C API: RpLight.
//
// RpLight has librw's field order under RenderWare's field names (see
// include/rwsdk/rpworld.h and layout_camera.cpp), so an RpLight* IS an
// rw::Light*.

#include <rwcore.h>
#include <rpworld.h>

#include "rw.h"

static inline rw::Light* asLight(RpLight* light)
{
    return reinterpret_cast<rw::Light*>(light);
}

RpLight* RpLightCreate(RwInt32 type)
{
    // rpLIGHTDIRECTIONAL/AMBIENT/POINT/SPOT/SPOTSOFT are librw's
    // DIRECTIONAL/AMBIENT/POINT/SPOT/SOFTSPOT with the same values, asserted in
    // layout_camera.cpp. xLightKit.cpp passes the numbers raw (1, 2, 128, 130),
    // which is why that assertion is there.
    return reinterpret_cast<RpLight*>(rw::Light::create((rw::int32)type));
}

RwBool RpLightDestroy(RpLight* light)
{
    if (light == NULL)
    {
        return FALSE;
    }

    // librw asserts the light has already been taken out of its world and its
    // clump, which is RenderWare's rule too -- RpWorldRemoveLight first.
    asLight(light)->destroy();
    return TRUE;
}

RpLight* RpLightSetColor(RpLight* light, const RwRGBAReal* color)
{
    if (light == NULL || color == NULL)
    {
        return NULL;
    }

    // setColor also recomputes the "this light is grey" flag that both
    // libraries keep in object.privateFlags and use to take a cheaper lighting
    // path, so it is worth going through rather than assigning the field.
    //
    // It copies red, green and blue and leaves alpha at the 1.0 that
    // Light::create set. RenderWare's own alpha handling here could not be
    // checked against a source, and nothing in src/SB reads an RpLight's alpha,
    // so this does not guess at one.
    asLight(light)->setColor(color->red, color->green, color->blue);
    return light;
}

// librw has no setter for the radius, so this one is written out. On the
// RenderWare side the world plugin also marks the light's sector list stale
// here; librw has nothing to mark, because World::enumerateLights walks the
// sectors afresh every frame instead of caching which ones a light touches.
// That is the same reason RpLight has no WorldSectorsInLight on this side.
RpLight* RpLightSetRadius(RpLight* light, RwReal radius)
{
    if (light == NULL)
    {
        return NULL;
    }

    light->radius = radius;
    return light;
}

RpLight* RpLightSetConeAngle(RpLight* light, RwReal angle)
{
    if (light == NULL)
    {
        return NULL;
    }

    // Both libraries store -cos(angle) rather than the angle, so that the
    // per-fragment spot test is a dot product compare. setAngle does the
    // conversion; the field is not the angle and must not be assigned one.
    asLight(light)->setAngle(angle);
    return light;
}
