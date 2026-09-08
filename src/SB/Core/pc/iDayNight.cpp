#include "iDayNight.h"

#include "xVec3.h"
#include "xVec3Inlines.h"

#include <math.h>

namespace
{
    F32 sLength;
    F32 sPhase = 0.25f; // start at noon, which is where every level was painted

    // **Night is a blue picture, not a dark one.**
    //
    // The show lights its nights so you can read every shape, in a heavy blue
    // wash, and a cycle that fades to black instead just looks like the game has
    // turned off. So the ambient keeps better than half its daytime strength and
    // swings hard towards blue -- the multiplier is above 1 on the blue channel,
    // which brightens blue against the day rather than only taking red away.
    const F32 kNightLevel = 0.55f;
    const F32 kNightHue[3] = { 0.45f, 0.70f, 1.35f };

    // The moon: a dim blue key while the sun is under the horizon.
    //
    // Ambient alone is flat, and a level lit flat reads as fog rather than as
    // night. Keeping a directional in the sky through the small hours is what
    // leaves the geometry its shape.
    const F32 kMoonLevel = 0.30f;
    const F32 kMoonHue[3] = { 0.50f, 0.68f, 1.20f };

    // The horizon's colour, as a multiple of the sun's own. Blended in as the
    // sun drops, which is what makes dawn and dusk read as dawn and dusk.
    const F32 kHorizonHue[3] = { 1.00f, 0.62f, 0.36f };

    // How high in the sky the sun is, and how far under it. Exactly one of the
    // two is above zero away from the horizon, and both are zero at it, which is
    // what makes sunset hand over to moonrise without a seam.
    F32 Sun()
    {
        F32 s = sinf(sPhase * 2.0f * 3.14159265f);

        return s > 0.0f ? s : 0.0f;
    }

    F32 Moon()
    {
        F32 s = sinf(sPhase * 2.0f * 3.14159265f);

        return s < 0.0f ? -s : 0.0f;
    }

    F32 Lerp(F32 a, F32 b, F32 t)
    {
        return a + (b - a) * t;
    }
}

F32 iDayNightLength()
{
    return sLength;
}

void iDayNightSetLength(F32 seconds)
{
    // A day shorter than this is a strobe, not a cycle.
    sLength = seconds < 1.0f ? 0.0f : seconds;
}

S32 iDayNightActive()
{
    return sLength > 0.0f;
}

F32 iDayNightPhase()
{
    return sPhase;
}

void iDayNightAdvance(F32 seconds)
{
    if (!iDayNightActive() || seconds <= 0.0f)
    {
        return;
    }

    sPhase += seconds / sLength;
    sPhase -= floorf(sPhase);
}

void iDayNightRig(const iEnvBakedRig* noon, iEnvBakedRig* out)
{
    *out = *noon;

    if (!iDayNightActive() || !noon->valid || noon->count == 0)
        return;

    // **The level's key light is where the sun stands at noon.**
    //
    // Everything else follows from that one vector: its azimuth is the azimuth
    // the whole day runs along, and its height is the height the sun reaches.
    // Taking it rather than picking a direction means a level still looks like
    // itself at midday, which is the only moment the artists actually painted.
    xVec3 up = { 0.0f, 1.0f, 0.0f };
    xVec3 n;

    // dir is where the light travels, so the sun is the other way.
    n.assign(-noon->dir[0].x, -noon->dir[0].y, -noon->dir[0].z);
    xVec3Normalize(&n, &n);

    // A key light fitted below the horizon means the bake had no sun in it to
    // speak of. Reflect it up rather than running the day upside down.
    if (n.y < 0.0f)
    {
        n.y = -n.y;
    }

    // Where the sun rises: horizontal, square to the day's azimuth. A sun
    // straight overhead has no azimuth to be square to, so any horizontal
    // direction will do and this picks one.
    xVec3 east;

    xVec3Cross(&east, &up, &n);

    if (xVec3Length2(&east) < 0.0001f)
    {
        east.assign(1.0f, 0.0f, 0.0f);
    }
    else
    {
        xVec3Normalize(&east, &east);
    }

    // The great circle through east and the noon direction. east and n are
    // perpendicular by construction, so this needs no correction.
    F32 a = sPhase * 2.0f * 3.14159265f;
    F32 ca = cosf(a);
    F32 sa = sinf(a);

    xVec3 sun;

    sun.assign(east.x * ca + n.x * sa, east.y * ca + n.y * sa, east.z * ca + n.z * sa);
    xVec3Normalize(&sun, &sun);

    // Under the horizon the sun is the moon, and a light below the ground lights
    // nothing. Reflecting it back up puts the moon roughly where one belongs and
    // keeps a key on the geometry all night; the colour is what says which of
    // the two is in the sky.
    if (sun.y < 0.0f)
    {
        sun.y = -sun.y;
        xVec3Normalize(&sun, &sun);
    }

    // Back to a direction of travel for the light itself.
    out->dir[0].assign(-sun.x, -sun.y, -sun.z);

    // The fill lights are bounce, so they follow the sun round rather than
    // staying where the bake left them. Rotating each by the same turn the key
    // took keeps their relationship to it.
    xVec3 keyNoon = n;
    xVec3 keyNow = sun;
    F32 dot = keyNoon.x * keyNow.x + keyNoon.y * keyNow.y + keyNoon.z * keyNow.z;

    if (dot < 0.9999f)
    {
        xVec3 axis;

        xVec3Cross(&axis, &keyNoon, &keyNow);

        if (xVec3Length2(&axis) > 0.0001f)
        {
            xVec3Normalize(&axis, &axis);

            F32 ang = acosf(dot < -1.0f ? -1.0f : (dot > 1.0f ? 1.0f : dot));
            F32 c = cosf(ang);
            F32 s = sinf(ang);

            for (S32 k = 1; k < noon->count; k++)
            {
                xVec3 v;

                v.assign(-noon->dir[k].x, -noon->dir[k].y, -noon->dir[k].z);

                // Rodrigues, so this needs no matrix.
                F32 kd = axis.x * v.x + axis.y * v.y + axis.z * v.z;
                xVec3 cross;

                xVec3Cross(&cross, &axis, &v);

                xVec3 r;

                r.assign(v.x * c + cross.x * s + axis.x * kd * (1.0f - c),
                         v.y * c + cross.y * s + axis.y * kd * (1.0f - c),
                         v.z * c + cross.z * s + axis.z * kd * (1.0f - c));
                xVec3Normalize(&r, &r);

                out->dir[k].assign(-r.x, -r.y, -r.z);
            }
        }
    }
}

void iDayNightTint(F32 ambient[3], F32 directional[3])
{
    if (!iDayNightActive())
    {
        for (S32 i = 0; i < 3; i++)
        {
            ambient[i] = 1.0f;
            directional[i] = 1.0f;
        }

        return;
    }

    F32 day = Sun();
    F32 night = Moon();

    // Warm as the sun drops. Full at the horizon, gone by the time it is a
    // third of the way up, which is about where a sunset stops looking like one.
    F32 low = 1.0f - (day < 0.35f ? day / 0.35f : 1.0f);

    for (S32 i = 0; i < 3; i++)
    {
        // The sun on the way down and the moon on the way up, added rather than
        // chosen between: both are zero at the horizon, so the handover is a
        // crossfade and needs no branch.
        directional[i] = day * Lerp(1.0f, kHorizonHue[i], low) +
                         night * kMoonLevel * kMoonHue[i];

        F32 level = Lerp(kNightLevel, 1.0f, day);
        F32 hue = Lerp(kNightHue[i], 1.0f, day);

        ambient[i] = level * hue;
    }
}
