#include "iPadHost.h"

// The backend that is always available: no controllers, ever.
//
// This is not a placeholder for a missing implementation -- it is what the
// port does when it is built without any input library, and it is the correct
// answer in that configuration. iPadUpdate reports the pad as missing, the
// game shows its "please reconnect" screen, and nothing reads uninitialised
// state. A real backend replaces this file in the link, not around it.

static iPadHostState sState[IPAD_MAX_CONTROLLERS];

void iPadHostInit()
{
    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        sState[i].connected = false;
        sState[i].buttons = 0;
        sState[i].stick_x = 0.0f;
        sState[i].stick_y = 0.0f;
        sState[i].substick_x = 0.0f;
        sState[i].substick_y = 0.0f;
    }
}

void iPadHostExit()
{
}

void iPadHostPoll()
{
}

const iPadHostState* iPadHostGet(S32 port)
{
    if (port < 0 || port >= IPAD_MAX_CONTROLLERS)
    {
        return NULL;
    }

    return &sState[port];
}

void iPadHostRumble(S32 port, S32 on)
{
}

const char* iPadHostName()
{
    return "null (no input backend)";
}

const char* iPadHostBoundInput(U32)
{
    return NULL;
}

const char* iPadHostPadKind()
{
    return NULL;
}
