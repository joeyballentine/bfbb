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
#include "xPad.h"

#include <SDL3/SDL.h>

static iPadBind sKeyBind[IPAD_BIND_MAX_BUTTONS];

// SDL has a scancode for each side of a modifier and none for "either", which
// is what the bare names have always meant here. These ids stand for the pair
// and are resolved in KeyDown; they are above SDL_SCANCODE_COUNT so they can
// never collide with a real one.
#define KEY_EITHER_SHIFT 1000
#define KEY_EITHER_CTRL 1001
#define KEY_EITHER_ALT 1002

static bool ScancodeDown(S32 code)
{
    const bool* state = SDL_GetKeyboardState(NULL);

    if (state == NULL || code < 0 || code >= SDL_SCANCODE_COUNT)
    {
        return false;
    }

    return state[code];
}

static bool KeyDown(S32 id)
{
    switch (id)
    {
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

// Key names, for the right-hand side of a [keyboard] binding. The names are the
// ones config.ini has always used, so a file written by an older build still
// parses; only what they resolve to changed.
//
// Letters and digits are not listed: they are appended below, because writing
// out thirty-six rows that each say "this key is called what is on it" is
// noise. What is listed is everything whose name is not its character.
static const iPadBindToken kKeyTokens[] = {
    { "space", SDL_SCANCODE_SPACE },
    { "enter", SDL_SCANCODE_RETURN },
    { "tab", SDL_SCANCODE_TAB },
    { "escape", SDL_SCANCODE_ESCAPE },
    { "backspace", SDL_SCANCODE_BACKSPACE },
    { "shift", KEY_EITHER_SHIFT },
    { "lshift", SDL_SCANCODE_LSHIFT },
    { "rshift", SDL_SCANCODE_RSHIFT },
    { "ctrl", KEY_EITHER_CTRL },
    { "lctrl", SDL_SCANCODE_LCTRL },
    { "rctrl", SDL_SCANCODE_RCTRL },
    { "alt", KEY_EITHER_ALT },
    { "lalt", SDL_SCANCODE_LALT },
    { "ralt", SDL_SCANCODE_RALT },
    { "up", SDL_SCANCODE_UP },
    { "down", SDL_SCANCODE_DOWN },
    { "left", SDL_SCANCODE_LEFT },
    { "right", SDL_SCANCODE_RIGHT },
    { "insert", SDL_SCANCODE_INSERT },
    { "delete", SDL_SCANCODE_DELETE },
    { "home", SDL_SCANCODE_HOME },
    { "end", SDL_SCANCODE_END },
    { "pageup", SDL_SCANCODE_PAGEUP },
    { "pagedown", SDL_SCANCODE_PAGEDOWN },
    { "capslock", SDL_SCANCODE_CAPSLOCK },
    { "comma", SDL_SCANCODE_COMMA },
    { "period", SDL_SCANCODE_PERIOD },
    { "minus", SDL_SCANCODE_MINUS },
    { "equals", SDL_SCANCODE_EQUALS },
    { "semicolon", SDL_SCANCODE_SEMICOLON },
    { "slash", SDL_SCANCODE_SLASH },
    { "tilde", SDL_SCANCODE_GRAVE },
    { "lbracket", SDL_SCANCODE_LEFTBRACKET },
    { "backslash", SDL_SCANCODE_BACKSLASH },
    { "rbracket", SDL_SCANCODE_RIGHTBRACKET },
    { "quote", SDL_SCANCODE_APOSTROPHE },
    { "f1", SDL_SCANCODE_F1 },
    { "f2", SDL_SCANCODE_F2 },
    { "f3", SDL_SCANCODE_F3 },
    { "f4", SDL_SCANCODE_F4 },
    { "f5", SDL_SCANCODE_F5 },
    { "f6", SDL_SCANCODE_F6 },
    { "f7", SDL_SCANCODE_F7 },
    { "f8", SDL_SCANCODE_F8 },
    { "f9", SDL_SCANCODE_F9 },
    { "f10", SDL_SCANCODE_F10 },
    { "f11", SDL_SCANCODE_F11 },
    { "f12", SDL_SCANCODE_F12 },
    { "numpad0", SDL_SCANCODE_KP_0 },
    { "numpad1", SDL_SCANCODE_KP_1 },
    { "numpad2", SDL_SCANCODE_KP_2 },
    { "numpad3", SDL_SCANCODE_KP_3 },
    { "numpad4", SDL_SCANCODE_KP_4 },
    { "numpad5", SDL_SCANCODE_KP_5 },
    { "numpad6", SDL_SCANCODE_KP_6 },
    { "numpad7", SDL_SCANCODE_KP_7 },
    { "numpad8", SDL_SCANCODE_KP_8 },
    { "numpad9", SDL_SCANCODE_KP_9 },
    { "numpadplus", SDL_SCANCODE_KP_PLUS },
    { "numpadminus", SDL_SCANCODE_KP_MINUS },
    { "numpadstar", SDL_SCANCODE_KP_MULTIPLY },
    { "numpadslash", SDL_SCANCODE_KP_DIVIDE },
    { "numpaddot", SDL_SCANCODE_KP_PERIOD },
};

static const S32 kKeyTokenCount = (S32)(sizeof(kKeyTokens) / sizeof(kKeyTokens[0]));

static iPadBindToken sTokens[kKeyTokenCount + 36];
static S32 sTokenCount;
static char sLetterNames[36][2];

// The number row is not contiguous with itself: SDL runs 1 through 9 and then
// puts 0 after them, in the order the keys sit on the board.
static S32 DigitScancode(S32 digit)
{
    return digit == 0 ? SDL_SCANCODE_0 : (SDL_SCANCODE_1 + digit - 1);
}

static void BuildKeyTokens()
{
    sTokenCount = 0;

    for (S32 i = 0; i < kKeyTokenCount; i++)
    {
        sTokens[sTokenCount++] = kKeyTokens[i];
    }

    for (S32 i = 0; i < 36; i++)
    {
        sLetterNames[i][0] = (i < 26) ? (char)('a' + i) : (char)('0' + (i - 26));
        sLetterNames[i][1] = '\0';

        sTokens[sTokenCount].name = sLetterNames[i];
        sTokens[sTokenCount].id =
            (S16)((i < 26) ? (SDL_SCANCODE_A + i) : DigitScancode(i - 26));
        sTokenCount++;
    }
}

void iPadKeyboardInit()
{
    BuildKeyTokens();
    iPadBindLoad(IPAD_BIND_KEYBOARD, sTokens, sTokenCount, sKeyBind);
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
