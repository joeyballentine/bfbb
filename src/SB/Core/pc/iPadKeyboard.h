#ifndef IPADKEYBOARD_H
#define IPADKEYBOARD_H

#include <types.h>

struct iPadHostState;

// PC-only: the keyboard standing in for port 0 when no controller is on it. A
// keyboard is not a second player; it is what makes the port playable on a
// machine with no pad.
//
// Shared by the input backends rather than owned by one. Which controller API
// a backend talks to says nothing about how it should read a keyboard, and the
// SDL backend deliberately does NOT use SDL's: SDL_GetKeyboardState wants the
// video subsystem and an event loop tied to an SDL window, and iWindow owns
// the only window this process has. Reading the async key state instead means
// neither backend has to negotiate for it.

void iPadKeyboardInit();

// Fills `s` from the keys held right now, or with nothing at all when the
// game does not have focus.
void iPadKeyboardPoll(iPadHostState* s);

#endif
