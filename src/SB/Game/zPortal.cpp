#include "xBase.h"
#include "xEvent.h"

#ifdef PLATFORM_PC
#include <stdio.h>
#include <stdlib.h>
#endif

#include "zPortal.h"
#include "zGlobals.h"
#include "zScene.h"

extern zGlobals globals;

void zPortalInit(void* portal, void* passet)
{
    zPortalInit((_zPortal*)portal, (xPortalAsset*)passet);
}

void zPortalInit(_zPortal* portal, xPortalAsset* passet)
{
    xBaseInit((xBase*)portal, (xBaseAsset*)passet);

    portal->passet = passet;
    portal->eventFunc = (xBaseEventCB)zPortalEventCB;

    if (portal->linkCount != 0)
    {
        portal->link = (xLinkAsset*)(portal->passet + 1);
    }
}

void zPortalReset(_zPortal* portal)
{
    xBaseReset((xBase*)portal, (xBaseAsset*)portal->passet);
}

void zPortalSave(_zPortal* ent, xSerial* s)
{
    xBaseSave((xBase*)ent, s);
}

void zPortalLoad(_zPortal* ent, xSerial* s)
{
    xBaseLoad((xBase*)ent, s);
}

S32 zPortalEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam, xBase* b3)
{
#ifdef PLATFORM_PC
    // The far end of the chain. Everything before this is confirmed working --
    // the player is detected inside triggers, the enter event fires, and every
    // link resolves its destination -- so if a portal never hears from one, the
    // link is carrying an event it does not act on.
    // Only what it acts on, and only when it acts. A scene load sends every
    // portal in it a handful of init events, which drowns the one line that
    // matters.
    if (toEvent == eEventTeleportPlayer && getenv("BFBB_EVENT") != NULL)
    {
        printf("bfbb: portal %08x got eEventTeleportPlayer, health %d\n", (unsigned)to->id,
               (int)globals.player.Health);
        fflush(stdout);
    }
#endif

    switch (toEvent)
    {
    case eEventReset:
    {
        zPortalReset((_zPortal*)to);
        break;
    }
    case eEventTeleportPlayer:
    {
        if (globals.player.Health != 0)
        {
            zSceneSwitch((_zPortal*)to, false);
        }
        break;
    }
    }
    return eEventEnable;
}
