#ifndef XFIXES_H
#define XFIXES_H

// Shared code's way in to the port's original-game bug fixes. iFixes.h says
// what each one is; there is nothing here on the GameCube, which has the bugs.
//
// The call sites in shared code are guarded with `#ifdef PLATFORM_PC` rather
// than wrapped in a macro that evaluates to nothing, because the fixes need
// locals to save state in and a console build should not carry those.

#ifdef PLATFORM_PC
#include "iFixes.h"
#endif

#endif
