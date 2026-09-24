// The keyboard, standing in for port 0 when no controller is on it. The
// argument for it living behind its own seam rather than inside an input
// backend is in iPadKeyboard.h.
//
//     WASD          left stick        IJKL        c-stick
//
// The two sticks are the part config.ini cannot move. Everything else is in
// its [keyboard] section, defaulted in iPadBind.cpp.
//
// **Keys are SCANCODES, not characters.** SDL names a scancode after the key in
// that position on a US layout, so "a" here means the key left of "s"
// regardless of what is printed on it. That is what a game wants: WASD is a
// shape under the left hand, and on an AZERTY keyboard the old virtual-key
// version put it under ZQSD and needed rebinding before the game could be
// played at all. The cost is that a binding written as `jump = a` is that
// physical key rather than the letter A on a layout where the two differ.

#include "iPadKeyboard.h"

#include "iPadBind.h"
#include "iPadHost.h"
#include "iPadTokens.h"
#include "xPad.h"

#include <SDL3/SDL.h>

#include <stdio.h>

static iPadBind sKeyBind[IPAD_BIND_MAX_BUTTONS];

static bool ScancodeDown(S32 code)
{
    const bool* state = SDL_GetKeyboardState(NULL);

    if (state == NULL || code < 0 || code >= SDL_SCANCODE_COUNT)
    {
        return false;
    }

    return state[code];
}

// An Enter pressed while Alt is held is the windowed toggle (iWindowSDL.cpp),
// not a game button. It stays swallowed until released, so letting go of Alt
// first does not then press Start.
static bool sEnterSwallowed[2];

static bool EnterDown(S32 code, bool* swallowed)
{
    bool down = ScancodeDown(code);
    if (!down)
    {
        *swallowed = false;
    }
    else if (ScancodeDown(SDL_SCANCODE_LALT) || ScancodeDown(SDL_SCANCODE_RALT))
    {
        *swallowed = true;
    }
    return down && !*swallowed;
}

static bool KeyDown(S32 id)
{
    switch (id)
    {
    case SDL_SCANCODE_RETURN:
        return EnterDown(id, &sEnterSwallowed[0]);
    case SDL_SCANCODE_KP_ENTER:
        return EnterDown(id, &sEnterSwallowed[1]);
    case KEY_EITHER_SHIFT:
        return ScancodeDown(SDL_SCANCODE_LSHIFT) || ScancodeDown(SDL_SCANCODE_RSHIFT);
    case KEY_EITHER_CTRL:
        return ScancodeDown(SDL_SCANCODE_LCTRL) || ScancodeDown(SDL_SCANCODE_RCTRL);
    case KEY_EITHER_ALT:
        return ScancodeDown(SDL_SCANCODE_LALT) || ScancodeDown(SDL_SCANCODE_RALT);
    default:
        return ScancodeDown(id);
    }
}

static F32 KeyAxis(S32 negative, S32 positive)
{
    F32 v = 0.0f;
    if (KeyDown(negative))
    {
        v -= 1.0f;
    }
    if (KeyDown(positive))
    {
        v += 1.0f;
    }
    return v;
}

static bool KeyInputHeld(S16 id)
{
    return KeyDown(id);
}

void iPadKeyboardInit()
{
    S32 count;
    const iPadBindToken* tokens = iPadKeyTokens(&count);
    iPadBindLoad(IPAD_BIND_KEYBOARD, tokens, count, sKeyBind);
}

void iPadKeyboardPoll(iPadHostState* s)
{
    s->connected = true;

    // SDL_GetKeyboardState reads a table the event pump maintains, so it is
    // only as fresh as the last pump. iWindowPump and iPadHostPoll both pump,
    // but neither is this file's to depend on being called first.
    SDL_PumpEvents();

    // SDL_GetKeyboardFocus is null exactly when no window of ours has the
    // keyboard, which is the gate this needs and the one thing GetActiveWindow
    // was doing here before.
    if (SDL_GetKeyboardFocus() == NULL)
    {
        // Focus is elsewhere. Still connected -- reporting the pad as unplugged
        // because the user alt-tabbed would drop the game into its reconnect
        // screen -- but nothing held, so the character stops rather than
        // running on with a key the game never sees released.
        s->buttons = 0;
        s->stick_x = 0.0f;
        s->stick_y = 0.0f;
        s->substick_x = 0.0f;
        s->substick_y = 0.0f;
        return;
    }

    U32 on = 0;
    for (S32 i = 0; i < kPadBindButtonCount; i++)
    {
        if (iPadBindHeld(sKeyBind[i], KeyInputHeld))
        {
            on |= kPadBindButtons[i].mask;
        }
    }

    s->buttons = on;

    // Y is up-positive here, as iPadHost.h specifies and as the GameCube stick
    // reports it; iPadUpdate negates it on the way to the game.
    s->stick_x = KeyAxis(SDL_SCANCODE_A, SDL_SCANCODE_D);
    s->stick_y = KeyAxis(SDL_SCANCODE_S, SDL_SCANCODE_W);
    s->substick_x = KeyAxis(SDL_SCANCODE_J, SDL_SCANCODE_L);
    s->substick_y = KeyAxis(SDL_SCANCODE_K, SDL_SCANCODE_I);
}

S32 iPadKeyboardCapture(S32 prime, char* out, S32 size)
{
    static bool sWasDown[SDL_SCANCODE_COUNT];

    SDL_PumpEvents();
    const bool* state = SDL_GetKeyboardState(NULL);
    if (state == NULL)
    {
        return FALSE;
    }

    S32 count;
    const iPadBindToken* tokens = iPadKeyTokens(&count);

    S32 found = -1;
    for (S32 code = 0; code < SDL_SCANCODE_COUNT; code++)
    {
        const bool down = state[code];
        if (down && !sWasDown[code] && !prime && found < 0 &&
            iPadBindTokenName((S16)code, tokens, count) != NULL)
        {
            found = code;
        }
        sWasDown[code] = down;
    }

    if (found < 0)
    {
        return FALSE;
    }

    snprintf(out, (size_t)size, "%s", iPadBindTokenName((S16)found, tokens, count));
    return TRUE;
}
