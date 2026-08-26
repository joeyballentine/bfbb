#ifndef IPADHOST_H
#define IPADHOST_H

#include <types.h>

// PC-only. There is no GameCube counterpart to this file: the console reads
// its controllers through one API that is always present, so iPad.cpp there
// talks to PADRead directly. A host has several possible input sources and
// none of them is guaranteed to exist at build time, so the part of iPad that
// touches hardware is behind this seam and the rest -- the mapping onto
// _tagxPad, which is the part with the game's semantics in it -- is not.
//
// Buttons are reported in XPAD_BUTTON_* terms, already mapped. The GameCube
// path maps them from PADStatus bits inside iPadUpdate because that is the
// only layout it can receive; a host backend knows its own device and can name
// the buttons directly, so nothing is gained by round-tripping through a
// controller layout none of them have.

#define IPAD_MAX_CONTROLLERS 4

struct iPadHostState
{
    bool connected;

    // XPAD_BUTTON_* bits, from xPad.h.
    U32 buttons;

    // Full deflection at magnitude 1. Y is up-positive, as the GameCube's
    // stick reports it -- iPadUpdate negates it for the game, which wants
    // screen coordinates.
    F32 stick_x;
    F32 stick_y;
    F32 substick_x;
    F32 substick_y;
};

void iPadHostInit();
void iPadHostExit();

// Called once per iPadUpdate on port 0, mirroring where the GameCube build
// calls PADRead: one read per frame feeds every port.
void iPadHostPoll();

const iPadHostState* iPadHostGet(S32 port);

// 0 stops the motor, 1 runs it. The GameCube's motor has no intermediate
// setting, and PADControlMotor is the only thing retail ever calls.
void iPadHostRumble(S32 port, S32 on);

// Names the backend that was linked in, for the startup log.
const char* iPadHostName();

#endif
