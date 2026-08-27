#include <types.h>

#ifdef PLATFORM_PC
#include <stdio.h>
#include <stdlib.h>
#endif

#include "xEvent.h"
#include "xString.h"
#include "xstransvc.h"

#include "zScene.h"

char zEventLogBuf[20][256];

void zEntEvent(char* to, U32 toEvent)
{
    U32 id = xStrHash(to);
    xBase* sendTo = zSceneFindObject(id);

    if (sendTo)
    {
        zEntEvent(sendTo, toEvent);
    }
}

void zEntEvent(U32 toID, U32 toEvent)
{
    xBase* sendTo = zSceneFindObject(toID);

    if (sendTo)
    {
        zEntEvent(sendTo, toEvent);
    }
}

void zEntEvent(U32 toID, U32 toEvent, F32 toParam0, F32 toParam1, F32 toParam2,
               F32 toParam3)
{
    xBase* sendTo;
    F32 toParam[4];

    toParam[0] = toParam0;
    toParam[1] = toParam1;
    toParam[2] = toParam2;
    toParam[3] = toParam3;

    sendTo = zSceneFindObject(toID);

    if (sendTo)
    {
        zEntEvent(sendTo, toEvent, toParam);
    }
}

void zEntEvent(xBase* to, U32 toEvent)
{
    zEntEvent(NULL, 0, to, toEvent, NULL, NULL, 0);
}

void zEntEvent(xBase* to, U32 toEvent, F32 toParam0, F32 toParam1, F32 toParam2,
               F32 toParam3)
{
    F32 toParam[4];

    toParam[0] = toParam0;
    toParam[1] = toParam1;
    toParam[2] = toParam2;
    toParam[3] = toParam3;

    zEntEvent(to, toEvent, toParam, NULL);
}

void zEntEvent(xBase* to, U32 toEvent, const F32* toParam)
{
    zEntEvent(NULL, 0, to, toEvent, toParam, NULL, 0);
}

void zEntEvent(xBase* to, U32 toEvent, const F32* toParam, xBase* toParamWidget)
{
    zEntEvent(NULL, 0, to, toEvent, toParam, toParamWidget, 0);
}

void zEntEvent(xBase* from, xBase* to, U32 toEvent)
{
    zEntEvent(from, 0, to, toEvent, NULL, NULL, 0);
}

void zEntEvent(xBase* from, xBase* to, U32 toEvent, const F32* toParam)
{
    zEntEvent(from, 0, to, toEvent, toParam, NULL, 0);
}

void zEntEvent(xBase* from, U32 fromEvent, xBase* to, U32 toEvent, const F32* toParam,
               xBase* toParamWidget, S32 forceEvent)
{
    if (toEvent == eEventDisable)
    {
        xBaseDisable(to);
    }
    else if (toEvent == eEventEnable)
    {
        xBaseEnable(to);
    }

    if (to->eventFunc && (xBaseIsEnabled(to) || forceEvent))
    {
        to->eventFunc(from, to, toEvent, toParam, toParamWidget);
    }

    if (xBaseIsEnabled(to) && to->linkCount)
    {
        xLinkAsset* idx = to->link;

        for (S32 i = 0; i < to->linkCount; i++, idx++)
        {
            if (toEvent == idx->srcEvent)
            {
                if (!idx->chkAssetID || (from && idx->chkAssetID == from->id))
                {
                    xBase* sendTo = zSceneFindObject(idx->dstAssetID);

#ifdef PLATFORM_PC
                    // BFBB_EVENT: a link that matched and went nowhere.
                    //
                    // This is the last silent step in the chain. The trigger
                    // fires, the link's srcEvent matches, and then the
                    // destination is looked up by asset id -- and if that
                    // lookup comes back empty the event is dropped with no
                    // trace, which is indistinguishable from the trigger never
                    // having fired. A level change is exactly this: a trigger
                    // linked to a portal, so a portal that does not resolve is
                    // a door that does nothing.
                    // Every forward, not only the dropped ones. Nothing was
                    // being dropped, which left the question of WHICH events
                    // are travelling -- a trigger whose link carries the wrong
                    // dstEvent reaches its portal and asks it for the wrong
                    // thing, and that is as silent as not arriving.
                    // Filtered rather than capped. A scene load broadcasts
                    // init events to every object in it, which exhausted a flat
                    // budget of sixty lines before the player had moved -- so
                    // the log went quiet exactly when the interesting scene
                    // started. Only player enter/exit links and anything asking
                    // for a teleport are worth a line.
                    if (getenv("BFBB_EVENT") != NULL &&
                        (idx->srcEvent == eEventEnterPlayer || idx->srcEvent == eEventExitPlayer ||
                         idx->srcEvent == eEventPadPressR1 || idx->srcEvent == eEventDone ||
                         idx->dstEvent == eEventTeleportPlayer || idx->dstEvent == eEventUISelect ||
                         idx->dstEvent == eEventUIFocusOn || idx->dstEvent == eEventUIFocusOn_Select))
                    {
                        static int said = 0;
                        if (said < 200)
                        {
                            said++;
                            printf("bfbb: link %08x src %u -> %08x dst %u  %s\n",
                                   (unsigned)to->id, (unsigned)idx->srcEvent,
                                   (unsigned)idx->dstAssetID, (unsigned)idx->dstEvent,
                                   sendTo ? "delivered" : "NOT FOUND, dropped");
                            fflush(stdout);
                        }
                    }
#endif

                    if (sendTo)
                    {
                        xBase* b = NULL;

                        if (idx->paramWidgetAssetID)
                        {
                            b = zSceneFindObject(idx->paramWidgetAssetID);

                            if (!b)
                            {
                                b = (xBase*)xSTFindAsset(idx->paramWidgetAssetID, NULL);
                            }
                        }

                        zEntEvent(to, toEvent, sendTo, idx->dstEvent, idx->param, b, 0);
                    }
                }
            }
        }
    }
}
