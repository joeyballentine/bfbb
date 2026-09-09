#include "iDayNight.h"

#include "xVec3.h"
#include "xVec3Inlines.h"

#include <math.h>

namespace
{
    F32 sLength;
    F32 sPhase = 0.25f; // start at noon, which is where every level was painted

    // **No light below carries a level. They only say which way the sun is.**
    //
    // Every hue here is scaled to hold its luminance, and nothing scales a light
    // up or down with the hour. iDayNightScreen is the only thing that darkens
    // the picture, and it reaches lit and unlit surfaces alike. A light that fell
    // as well would take the lit ones down twice, and the world and Spongebob
    // would go black against a crater sitting at noon.
    //
    // The cost is that night has a day's contrast, where a real night is
    // flatter. Moving that energy out of the key and into the ambient needs the
    // rig's own split between the two, and a tint that runs over every kit in
    // the game does not have it.

    // The hue an ambient takes at night: cyan, luminance held. The show's night
    // fill is the ground bouncing green-blue back up, not a blue sky, so green
    // rises with blue and only red falls.
    const F32 kNightHue[3] = { 0.80f, 1.06f, 1.20f };

    // The moon's, for the key light while the sun is under the horizon.
    const F32 kMoonHue[3] = { 0.84f, 1.01f, 1.37f };

    // The sun's at the horizon, which is what makes dawn and dusk read as dawn
    // and dusk.
    const F32 kHorizonHue[3] = { 1.42f, 0.88f, 0.51f };

    // How wide the handover from sun to moon is, in sines of the sun's height.
    // Narrow, so the key changes colour close to the horizon and holds the one
    // it has for the rest of the day.
    const F32 kHandover = 0.15f;

    // How far the sun has to climb to lose the horizon's colour. About where a
    // sunset stops looking like one.
    const F32 kHorizonBand = 0.35f;

    // **Night is a saturated picture, not a dark one.**
    //
    // What the whole frame is multiplied by at midnight. Red is nearly gone
    // while green and blue are mostly kept, so the sand lands on a bright cyan
    // and the picture reads as night by its colour rather than by its level.
    // Taking all three down together gives a grey scene at low brightness, which
    // is a dimmer switch and not a night.
    const F32 kScreenNight[3] = { 0.26f, 0.58f, 0.64f };

    // Added after that multiply, so nothing in the frame reaches black. A
    // surface no light finds sits in deep blue instead of in a hole, which is
    // what lets every shape stay readable at midnight.
    const F32 kScreenLift[3] = { 0.02f, 0.05f, 0.15f };

    // How high the sun is: 1 straight overhead, 0 at the horizon, negative once
    // it is under it and the moon has the sky.
    F32 SunHeight()
    {
        return sinf(sPhase * 2.0f * 3.14159265f);
    }

    F32 Clamp01(F32 v)
    {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
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

void iDayNightScreen(F32 mul[3], F32 add[3])
{
    if (!iDayNightActive())
    {
        for (S32 i = 0; i < 3; i++)
        {
            mul[i] = 1.0f;
            add[i] = 0.0f;
        }

        return;
    }

    F32 day = Clamp01(SunHeight());

    for (S32 i = 0; i < 3; i++)
    {
        mul[i] = Lerp(kScreenNight[i], 1.0f, day);
        add[i] = Lerp(kScreenLift[i], 0.0f, day);
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

    F32 h = SunHeight();
    F32 day = Clamp01(h);

    // Which of the two is in the sky, blended across a band either side of the
    // horizon so sunset hands over to moonrise without a step. Both are at full
    // strength throughout; the colour is all that says which one it is.
    F32 sunlit = Clamp01(0.5f + 0.5f * h / kHandover);

    // Warm as the sun drops, full by the time it reaches the horizon.
    F32 low = 1.0f - Clamp01(day / kHorizonBand);

    for (S32 i = 0; i < 3; i++)
    {
        directional[i] = Lerp(kMoonHue[i], Lerp(1.0f, kHorizonHue[i], low), sunlit);
        ambient[i] = Lerp(kNightHue[i], 1.0f, day);
    }
}
