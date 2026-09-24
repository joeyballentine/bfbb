#include "iPadTokens.h"

#include <SDL3/SDL.h>

static const iPadBindToken kPadTokens[] = {
    { "a", PADIN_A },           { "b", PADIN_B },
    { "x", PADIN_X },           { "y", PADIN_Y },
    { "lb", PADIN_LB },         { "rb", PADIN_RB },
    { "lt", PADIN_LT },         { "rt", PADIN_RT },
    { "ls", PADIN_LS },         { "rs", PADIN_RS },
    { "back", PADIN_BACK },     { "start", PADIN_START },
    { "dpup", PADIN_DPUP },     { "dpdown", PADIN_DPDOWN },
    { "dpleft", PADIN_DPLEFT }, { "dpright", PADIN_DPRIGHT },
};

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

static iPadBindToken sKeyTokens[kKeyTokenCount + 36];
static S32 sKeyTokenCount;
static char sLetterNames[36][2];

// The number row is not contiguous with itself: SDL runs 1 through 9 and then
// puts 0 after them, in the order the keys sit on the board.
static S32 DigitScancode(S32 digit)
{
    return digit == 0 ? SDL_SCANCODE_0 : (SDL_SCANCODE_1 + digit - 1);
}

const iPadBindToken* iPadKeyTokens(S32* count)
{
    if (sKeyTokenCount == 0)
    {
        for (S32 i = 0; i < kKeyTokenCount; i++)
        {
            sKeyTokens[sKeyTokenCount++] = kKeyTokens[i];
        }

        for (S32 i = 0; i < 36; i++)
        {
            sLetterNames[i][0] = (i < 26) ? (char)('a' + i) : (char)('0' + (i - 26));
            sLetterNames[i][1] = '\0';

            sKeyTokens[sKeyTokenCount].name = sLetterNames[i];
            sKeyTokens[sKeyTokenCount].id =
                (S16)((i < 26) ? (SDL_SCANCODE_A + i) : DigitScancode(i - 26));
            sKeyTokenCount++;
        }
    }

    *count = sKeyTokenCount;
    return sKeyTokens;
}

const iPadBindToken* iPadPadTokens(S32* count)
{
    *count = (S32)(sizeof(kPadTokens) / sizeof(kPadTokens[0]));
    return kPadTokens;
}

S32 iPadInputFromSDLButton(S32 button)
{
    switch (button)
    {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return PADIN_A;
    case SDL_GAMEPAD_BUTTON_EAST:
        return PADIN_B;
    case SDL_GAMEPAD_BUTTON_WEST:
        return PADIN_X;
    case SDL_GAMEPAD_BUTTON_NORTH:
        return PADIN_Y;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return PADIN_LB;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return PADIN_RB;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return PADIN_LS;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return PADIN_RS;
    case SDL_GAMEPAD_BUTTON_BACK:
        return PADIN_BACK;
    case SDL_GAMEPAD_BUTTON_START:
        return PADIN_START;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return PADIN_DPUP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return PADIN_DPDOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return PADIN_DPLEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return PADIN_DPRIGHT;
    default:
        return -1;
    }
}
