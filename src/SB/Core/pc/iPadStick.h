#ifndef IPADSTICK_H
#define IPADSTICK_H

#include <types.h>

// PC-only: an analog stick's two signed 16-bit axes, with the deadzone taken
// out and the rest rescaled to a unit vector. Shared by every input backend
// rather than owned by one, because every device a host has reports its sticks
// this way and none of the arithmetic below is specific to an API.
//
// What the game does with the result is in iPad.cpp: the unit circle out of
// here is shaped onto the GameCube's octagon before iPadConvStick sees it.

// XInput's own documented deadzones, which SDL does not publish an equivalent
// of -- its gamepad axes are the raw device values, same as XInput's. Named
// here so a backend needs no XInput header to use them.
#define IPAD_STICK_DEADZONE_LEFT 7849
#define IPAD_STICK_DEADZONE_RIGHT 8689

// Applied RADIALLY rather than per axis: a square deadzone lets a stick pushed
// exactly diagonally register while the same deflection along one axis does
// not, which shows up in this game as a character that will not walk slowly in
// one direction but will in another.
//
// The magnitude is rescaled from the deadzone edge, so the first movement past
// it is small instead of a jump straight to the deadzone fraction.
void iPadStickConvert(S16 rawX, S16 rawY, S32 deadzone, F32* outX, F32* outY);

#endif
