#ifndef IPADBIND_H
#define IPADBIND_H

#include <types.h>

// PC-only: the control bindings from config.ini, parsed. There is no GameCube
// counterpart -- a console's controller is the one the game was built around,
// and every button it has already means something.
//
// The left-hand side of a binding is a button the GAME reads, named as the
// console named it. That vocabulary rather than an action's -- "jump", "bash"
// -- because the buttons are context-dependent: the GameCube's A is jump, and
// confirm, and talk, and one row cannot say so. The generated config.ini puts
// the common meaning in a trailing comment instead, where being approximate
// costs nothing.
//
// The right-hand side is written in the DEVICE's names, which differ per
// backend, so this file does not know any of them. A backend passes its own
// token table in. What is shared is the grammar:
//
//     a  = x                one input
//     a  = x, ls            either input on its own
//     l2 = lt+rb            both at once
//     l1 = lt+!rb           held, and something else not held
//     l1 =                  nothing; the button can never be pressed
//
// Negation is not a flourish. gc/iPad.cpp reads the GameCube's three shoulders
// as four buttons by making Z a modifier, and the two arms of that are
// exclusive: Z+L is L2 and is NOT also L1. Without `!` the port's default
// bindings could not say that, and holding the modifier would press both.

#define IPAD_BIND_ALTS 4
#define IPAD_BIND_CHORD 3

// Enough rows for kPadBindButtons, so a backend can hold one parsed binding
// per button in a plain array. The table is a compile-time list and the count
// below is not a constant expression, so the two are checked against each
// other at startup rather than by the compiler.
#define IPAD_BIND_MAX_BUTTONS 24

// One parsed binding. Empty -- `alts` of 0 -- is a button nothing presses.
struct iPadBind
{
    S16 id[IPAD_BIND_ALTS][IPAD_BIND_CHORD];
    U8 negate[IPAD_BIND_ALTS][IPAD_BIND_CHORD];
    S8 length[IPAD_BIND_ALTS];
    S8 alts;
};

// A device input's name and whatever the backend wants to call it by. `id` is
// opaque here: XInput numbers its own buttons, the keyboard uses virtual-key
// codes, and neither has to explain itself.
struct iPadBindToken
{
    const char* name;
    S16 id;
};

// One button the game reads, and what presses it out of the box.
//
// This table is the single source of truth for three things that must agree:
// the keys config.ini accepts, the values a generated config.ini is written
// with, and the defaults a missing config.ini runs on. iConfig.cpp reads it for
// the first two; the backends read it for the third.
struct iPadBindButton
{
    // As it appears left of the '=', and as the console named it.
    const char* name;

    // XPAD_BUTTON_*, which is what the game actually tests.
    U32 mask;

    // Default bindings, in each backend's own token names.
    const char* pad;
    const char* key;

    // What the button does, for the trailing comment in a generated file.
    // NULL where there is nothing worth saying.
    const char* does;
};

extern const iPadBindButton kPadBindButtons[];
extern const S32 kPadBindButtonCount;

// The row for `name`, or NULL. Case-insensitive.
const iPadBindButton* iPadBindFind(const char* name);

// Parse one binding. `what` names the setting for the diagnostics -- an unknown
// token, too many alternatives, a chord of nothing but negations -- which are
// printed rather than returned, as the rest of the config layer does it.
//
// A binding that does not parse comes back EMPTY rather than half-applied, and
// false is returned: a typo should cost one button, not leave it bound to the
// part of the line that happened to be readable.
bool iPadBindParse(const char* text, const iPadBindToken* tokens, S32 tokenCount, const char* what,
                   iPadBind* out);

// Is the binding satisfied right now? `held` answers for one device input id.
bool iPadBindHeld(const iPadBind& bind, bool (*held)(S16 id));

// Which config.ini section a device reads, and which column of the table it
// defaults from. The two always travel together -- a device that read [pad]
// and defaulted from the keyboard column would be a silent mess -- so they are
// one argument rather than two.
enum iPadBindDevice
{
    IPAD_BIND_PAD,
    IPAD_BIND_KEYBOARD
};

// Every button's binding for one device, read from config.ini and parsed with
// that device's token table. `out` must hold IPAD_BIND_MAX_BUTTONS.
//
// Shared rather than written per backend: the loop is the same either way and
// only the token table differs, which is the whole reason ids are opaque here.
void iPadBindLoad(iPadBindDevice device, const iPadBindToken* tokens, S32 tokenCount,
                  iPadBind* out);

#endif
