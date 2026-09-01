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

// The device input bound to one XPAD_BUTTON_*, by the name config.ini writes
// it as -- "a", "lt". NULL when nothing is bound to it, and NULL when what is
// bound is a chord: `l2 = lt+rb` is two inputs and there is no single name for
// it.
//
// For the button prompts. A glyph has to follow the BINDING rather than the
// console, or the game would keep drawing the disc's button while a different
// one presses it. Only a backend can answer: the parsed bindings hold opaque
// ids and only the backend has the table that names them. See iPadGlyph.h.
const char* iPadHostBoundInput(U32 xpadButton);

// What sort of controller is on port 0, as the name of a glyph set: "xbox",
// "gamecube", "ps2". NULL when there is no pad, or when the backend cannot
// tell one kind from another -- `video.button_icons = auto` then falls back on
// the Xbox set, which is what the game's own assets are.
const char* iPadHostPadKind();

// Which device input carries the letter `letter` ('A', 'B', 'X' or 'Y') on the
// controller on port 0, by the name config.ini writes it as. NULL when there is
// no pad, or none of its buttons is printed with that letter.
//
// This exists because a face button's LETTER and its POSITION are independent.
// A GameCube pad prints B west of the stick and X east of it; an Xbox pad has
// them the other way round. So "the button that spins" cannot be written as a
// position if it is meant to mean what the GameCube meant by it, and it cannot
// be written as one of our own token names either, since those are positions.
// input.preset says a letter and this finds it. See iPadBind.h.
//
// A pad whose buttons are printed with shapes rather than letters answers on
// the conventional equivalence -- cross is A, circle B, square X, triangle Y --
// so a preset does not have to enumerate every family of controller.
const char* iPadHostInputForLabel(char letter);

#endif
