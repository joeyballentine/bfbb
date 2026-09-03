// The analog stick deadzone. The argument for it is in iPadStick.h.

#include "iPadStick.h"

#include <math.h>

// Negative is auto: the backend's own constant stands.
static F32 sDeadzonePercent = -1.0f;

void iPadStickSetDeadzone(F32 percent)
{
    sDeadzonePercent = percent;
}

S32 iPadStickDeadzone(S32 fallback)
{
    if (sDeadzonePercent < 0.0f)
    {
        return fallback;
    }

    return (S32)(0.01f * sDeadzonePercent * 32767.0f);
}

void iPadStickConvert(S16 rawX, S16 rawY, S32 deadzone, F32* outX, F32* outY)
{
    F32 x = (F32)rawX;
    F32 y = (F32)rawY;

    F32 magnitude = sqrtf(x * x + y * y);
    if (magnitude <= (F32)deadzone)
    {
        *outX = 0.0f;
        *outY = 0.0f;
        return;
    }

    // Direction first, off the UNCLAMPED magnitude, so that it stays a unit
    // vector. Normalising by the clamped one instead lets a stick held to a
    // corner report sqrt(2) rather than 1, because both axes reach full scale
    // while the length they are divided by does not.
    F32 dirX = x / magnitude;
    F32 dirY = y / magnitude;

    // 32767 rather than 32768: the negative end reaches -32768 but the positive
    // end stops one short, and normalising by the larger value would leave full
    // deflection reading as slightly less than full.
    const F32 limit = 32767.0f;
    if (magnitude > limit)
    {
        magnitude = limit;
    }

    F32 scaled = (magnitude - (F32)deadzone) / (limit - (F32)deadzone);

    *outX = dirX * scaled;
    *outY = dirY * scaled;
}
