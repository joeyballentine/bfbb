#ifndef IPADKEYBOARD_H
#define IPADKEYBOARD_H

#include <types.h>

struct iPadHostState;

// PC-only: the keyboard standing in for port 0 when no controller is on it. A
// keyboard is not a second player; it is what makes the port playable on a
// machine with no pad.
//
// Behind its own seam rather than inside an input backend. Which controller API
// a backend talks to says nothing about how it should read a keyboard, and the
// game asks for the two together: the keyboard is what port 0 falls back to,
// not a device of its own.

void iPadKeyboardInit();

// Fills `s` from the keys held right now, or with nothing at all when the
// game does not have focus.
void iPadKeyboardPoll(iPadHostState* s);

#endif
