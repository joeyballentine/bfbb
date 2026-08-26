#ifndef ISTUB_H
#define ISTUB_H

// The honest "not implemented yet" for the platform modules that have a PC
// header and no PC implementation.
//
// This is NOT the same thing as iPadHostNull.cpp or iSndHostNull.cpp. Those are
// real configurations -- a machine with no controller genuinely has no
// controller, and the game handles it. These are placeholders, and the
// difference has to stay visible, because a stub that quietly returns zero is
// indistinguishable from a working function that had nothing to do.
//
// So each one says so, once, the first time it is reached. Once and not every
// time because several of these are called per frame per object and a
// per-frame log would bury everything else; once and not never because the
// FIRST call is the interesting one -- the point of stubbing the platform layer
// at all is to get a link and then watch, in order, which module the startup
// path actually demands. That log line IS the worklist.
//
// When a module gets a real implementation, its file stops including this
// header. Nothing else has to change.

#include <stdio.h>

#define IPORT_STUB()                                                                               \
    do                                                                                             \
    {                                                                                              \
        static bool sReported = false;                                                             \
        if (!sReported)                                                                            \
        {                                                                                          \
            sReported = true;                                                                      \
            printf("[pcport] not implemented: %s\n", __FUNCTION__);                                \
            fflush(stdout);                                                                        \
        }                                                                                          \
    } while (0)

#endif
