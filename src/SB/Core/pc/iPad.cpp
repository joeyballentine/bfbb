#include "iPad.h"
#include "iPadHost.h"

#include <types.h>

#include "xPad.h"
#include "xTRC.h"

S32 iPadInit()
{
    iPadHostInit();
    return 1;
}

_tagxPad* iPadEnable(_tagxPad* pad, S16 port)
{
    pad->port = port;
    pad->slot = 0;
    pad->state = ePad_Enabled;
    gTrcPad[pad->port].state = TRC_PadInserted;
    pad->flags |= 3;
    pad->flags |= 4;
    return pad;
}

// Retail's curve, kept exactly. The argument is in GameCube stick units, where
// full deflection is about 40 after PADClamp; the 3.2 scale takes that to the
// 127 the game wants, and the two clamps are what give the stick its flat top.
// Reproducing the curve rather than mapping straight to 127 keeps the feel of
// the deadzone and saturation identical to the console.
S32 iPadConvStick(F32 value)
{
    F32 clampedValue;
    if (value > 40.0f)
    {
        clampedValue = 40.0f;
    }
    else if (value < -40.0f)
    {
        clampedValue = -40.0f;
    }
    else
    {
        clampedValue = value;
    }

    F32 convertedValue = 3.2f * clampedValue;

    if (convertedValue > 127.0f)
    {
        convertedValue = 127.0f;
    }
    else if (convertedValue < -127.0f)
    {
        convertedValue = -127.0f;
    }

    return convertedValue;
}

// PADClamp's ClampRegion, from src/dolphin/src/pad/Padclamp.c: the octagon the
// GameCube path hands iPadConvStick. maxStick/maxSubstick is the reach along an
// axis, xyStick/xySubstick the reach on a diagonal. minStick is deliberately
// absent -- it is the raw deadzone, and a host backend takes its own out and
// rescales before iPadHostState is filled in.
#define IPAD_STICK_MAX 72.0f
#define IPAD_STICK_XY 40.0f
#define IPAD_SUBSTICK_MAX 59.0f
#define IPAD_SUBSTICK_XY 31.0f

// Maps a host stick, which reports a unit CIRCLE, onto that octagon.
//
// The shape matters because of what the game does downstream. iPadConvStick
// clamps each axis at 40, and DampenControls in zEntPlayer.cpp measures the
// stick as max(|x|,|y|). On the octagon a diagonal reads 40 on both axes -- the
// same 40 an axis push saturates at -- so full deflection gives full speed in
// every direction. Scaling the host circle by 40 instead leaves it a circle:
// the diagonals only reach 28, the axis clamp never engages off-axis, and
// max(|x|,|y|) drops to 0.71 of full. That is the stick pulsing between a run
// and a walk four times per revolution, and sneaking near 45 degrees.
//
// Scaling by the axis reach and clamping puts full deflection on the octagon
// boundary at every angle, which is where the console's gate puts it. The
// arithmetic is ClampStick's, so the direction the clamp bends off-axis input
// into is the console's too.
//
// Named rather than static so the self-test can reach it: this is the only
// arithmetic in the file that a wrong answer would show up in as a handling
// bug rather than as nothing happening at all.
void iPadShapeStick(F32 inX, F32 inY, F32 max, F32 xy, F32* outX, F32* outY)
{
    F32 x = max * inX;
    F32 y = max * inY;
    F32 ax = (x < 0.0f) ? -x : x;
    F32 ay = (y < 0.0f) ? -y : y;
    F32 d = (ay <= ax) ? (xy * ax + (max - xy) * ay) : (xy * ay + (max - xy) * ax);

    if (xy * max < d)
    {
        F32 scale = (xy * max) / d;
        x *= scale;
        y *= scale;
    }

    *outX = x;
    *outY = y;
}

S32 iPadUpdate(_tagxPad* pad, U32* on)
{
    // One poll per frame feeds every port, which is where the GameCube build
    // calls PADRead.
    if (pad->port == 0)
    {
        iPadHostPoll();
    }

    const iPadHostState* host = iPadHostGet(pad->port);

    if (host == NULL || !host->connected)
    {
        xTRCPad(pad->port, TRC_PadMissing);
        return 0;
    }

    // The mapping from device buttons to XPAD_BUTTON_* happens in the backend,
    // because only the backend knows what device it is reading. The GameCube
    // build does it here instead, and needs the Z-modifier trick -- Z+L/R for
    // L2/R2 -- because the console has three shoulder buttons where the game
    // wants four. A host controller has four, so nothing has to be shared.
    *on = host->buttons;

    F32 x;
    F32 y;

    iPadShapeStick(host->stick_x, host->stick_y, IPAD_STICK_MAX, IPAD_STICK_XY, &x, &y);
    pad->analog1.x = iPadConvStick(x);
    pad->analog1.y = -iPadConvStick(y);

    iPadShapeStick(host->substick_x, host->substick_y, IPAD_SUBSTICK_MAX, IPAD_SUBSTICK_XY, &x, &y);
    pad->analog2.x = iPadConvStick(x);
    pad->analog2.y = -iPadConvStick(y);

    if (gTrcPad[pad->port].state != TRC_PadInserted)
    {
        xTRCPad(pad->port, TRC_PadInserted);
    }

    return 1;
}

// Maps one GameCube pad button onto one xPad button. Nothing on a host build
// produces PADStatus bits, so nothing here calls it; it stays because it is
// part of the interface iPad.h publishes and it costs one instruction.
S32 iPadConvFromGCN(U32 gcnButtons, U32 gcnMask, U32 xpadButton)
{
    return (gcnButtons & gcnMask) ? xpadButton : 0;
}

// Empty in retail too: the rumble envelope is computed in xPad.cpp, and the
// GameCube motor has no strength setting for it to apply.
void iPadRumbleFx(_tagxPad* p, _tagxRumble* r, F32 time_passed)
{
}

void iPadStopRumble(_tagxPad* pad)
{
    iPadHostRumble(pad->port, 0);
}

// Retail stops the motor on globals.currentActivePad, reaching into the game
// layer from the platform layer to do it. No caller exists -- all four call
// sites pass a pad -- so the host build stops every port instead of taking a
// dependency on zGlobals for a function nothing calls.
void iPadStopRumble()
{
    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        iPadHostRumble(i, 0);
    }
}

void iPadStartRumble(_tagxPad* pad, _tagxRumble* rumble)
{
    iPadHostRumble(pad->port, 1);
}

void iPadKill()
{
    iPadHostExit();
}
