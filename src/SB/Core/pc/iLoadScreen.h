#ifndef ILOADSCREEN_H
#define ILOADSCREEN_H

#include <types.h>

// PC-only: a floor on how long the loading screen is up, config.ini's
// video.load_time.
//
// The console's loading screen lasts exactly as long as the load, and the
// bubble wall rising over the still is sized to that. A disc took long enough
// for the wall to fill the screen; a host loads a scene in a fraction of a
// second, so the same screen is a flicker between two levels and the animation
// never plays.
//
// This is a minimum, not a delay: the clock runs from when the screen went up,
// so a load that takes longer than the floor waits for nothing. Nothing about
// the screen itself changes -- it is the console's, bubbles and all.
//
// Off restores what the port did before this existed: whatever speed the
// machine loads at.

// config.ini's video.load_time, in seconds. Pushed down from iSystem.cpp.
// Zero or less is off.
void iLoadScreenSetMinTime(F32 seconds);

// The loading screen has gone up; zGameScreenTransitionBegin. Starts the clock
// the floor is measured against.
void iLoadScreenBegin();

// Whether the screen still owes the floor time. zSceneInit draws one more
// loading-screen frame for every TRUE, so this is the whole of the feature.
S32 iLoadScreenHolding();

#endif
