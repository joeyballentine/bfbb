// The floor on how long the loading screen is up. What it is for is in
// iLoadScreen.h.

#include "iLoadScreen.h"

#include "iTime.h"

namespace
{
    F32 sMinTime;

    // The clock only runs between iLoadScreenBegin and the floor being
    // reached, so that a query made outside a load -- there are none today,
    // and this costs nothing -- cannot answer TRUE off a stale start time.
    S32 sHolding;
    iTime sStart;
}

void iLoadScreenSetMinTime(F32 seconds)
{
    sMinTime = seconds > 0.0f ? seconds : 0.0f;
}

void iLoadScreenBegin()
{
    sHolding = sMinTime > 0.0f;
    sStart = iTimeGet();
}

S32 iLoadScreenHolding()
{
    if (!sHolding)
    {
        return FALSE;
    }

    if (iTimeDiffSec(sStart, iTimeGet()) >= sMinTime)
    {
        sHolding = FALSE;
        return FALSE;
    }

    return TRUE;
}
