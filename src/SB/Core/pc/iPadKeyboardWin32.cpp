// The keyboard, for every Windows input backend. The argument for it living
// here rather than inside one of them is in iPadKeyboard.h.
//
//     WASD          left stick        IJKL        c-stick
//
// The two sticks are the part config.ini cannot move. Everything else is in
// its [keyboard] section, defaulted in iPadBind.cpp.

#include "iPadKeyboard.h"

#include "iPadBind.h"
#include "iPadHost.h"
#include "xPad.h"

#include <windows.h>

static iPadBind sKeyBind[IPAD_BIND_MAX_BUTTONS];

static bool KeyDown(S32 vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
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

static bool KeyInputHeld(S16 vk)
{
    return KeyDown(vk);
}

// Key names, for the right-hand side of a [keyboard] binding. Virtual-key
// codes are the ids, so the binder's lookup is the whole translation.
//
// Letters and digits are not listed: their VK codes ARE their upper-case ASCII
// values, so BuildKeyTokens appends the thirty-six of them rather than spelling
// out rows that say so. What is listed is everything whose name is not its
// character.
//
// Both sides of a modifier are named, and the bare name means either -- VK_
// SHIFT and friends are the "either" codes Windows already provides.
static const iPadBindToken kKeyTokens[] = {
    { "space", VK_SPACE },
    { "enter", VK_RETURN },
    { "tab", VK_TAB },
    { "escape", VK_ESCAPE },
    { "backspace", VK_BACK },
    { "shift", VK_SHIFT },
    { "lshift", VK_LSHIFT },
    { "rshift", VK_RSHIFT },
    { "ctrl", VK_CONTROL },
    { "lctrl", VK_LCONTROL },
    { "rctrl", VK_RCONTROL },
    { "alt", VK_MENU },
    { "lalt", VK_LMENU },
    { "ralt", VK_RMENU },
    { "up", VK_UP },
    { "down", VK_DOWN },
    { "left", VK_LEFT },
    { "right", VK_RIGHT },
    { "insert", VK_INSERT },
    { "delete", VK_DELETE },
    { "home", VK_HOME },
    { "end", VK_END },
    { "pageup", VK_PRIOR },
    { "pagedown", VK_NEXT },
    { "capslock", VK_CAPITAL },
    { "comma", VK_OEM_COMMA },
    { "period", VK_OEM_PERIOD },
    { "minus", VK_OEM_MINUS },
    { "equals", VK_OEM_PLUS },
    { "semicolon", VK_OEM_1 },
    { "slash", VK_OEM_2 },
    { "tilde", VK_OEM_3 },
    { "lbracket", VK_OEM_4 },
    { "backslash", VK_OEM_5 },
    { "rbracket", VK_OEM_6 },
    { "quote", VK_OEM_7 },
    { "f1", VK_F1 },
    { "f2", VK_F2 },
    { "f3", VK_F3 },
    { "f4", VK_F4 },
    { "f5", VK_F5 },
    { "f6", VK_F6 },
    { "f7", VK_F7 },
    { "f8", VK_F8 },
    { "f9", VK_F9 },
    { "f10", VK_F10 },
    { "f11", VK_F11 },
    { "f12", VK_F12 },
    { "numpad0", VK_NUMPAD0 },
    { "numpad1", VK_NUMPAD1 },
    { "numpad2", VK_NUMPAD2 },
    { "numpad3", VK_NUMPAD3 },
    { "numpad4", VK_NUMPAD4 },
    { "numpad5", VK_NUMPAD5 },
    { "numpad6", VK_NUMPAD6 },
    { "numpad7", VK_NUMPAD7 },
    { "numpad8", VK_NUMPAD8 },
    { "numpad9", VK_NUMPAD9 },
    { "numpadplus", VK_ADD },
    { "numpadminus", VK_SUBTRACT },
    { "numpadstar", VK_MULTIPLY },
    { "numpadslash", VK_DIVIDE },
    { "numpaddot", VK_DECIMAL },
};

static const S32 kKeyTokenCount = (S32)(sizeof(kKeyTokens) / sizeof(kKeyTokens[0]));

static iPadBindToken sTokens[kKeyTokenCount + 36];
static S32 sTokenCount;
static char sLetterNames[36][2];

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
        sTokens[sTokenCount].id = (S16)((i < 26) ? ('A' + i) : ('0' + (i - 26)));
        sTokenCount++;
    }
}

void iPadKeyboardInit()
{
    BuildKeyTokens();
    iPadBindLoad(IPAD_BIND_KEYBOARD, sTokens, sTokenCount, sKeyBind);
}

// GetActiveWindow rather than a window handle: it reports the active window of
// the CALLING THREAD's queue, so it is non-null exactly when one of our own
// windows has focus and null the moment the user alt-tabs away. That is the
// gate this needs, and it does not require iWindow to publish its HWND -- which
// iWindow.h says only the RenderWare shim may interpret.
void iPadKeyboardPoll(iPadHostState* s)
{
    s->connected = true;

    if (GetActiveWindow() == NULL)
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
    s->stick_x = KeyAxis('A', 'D');
    s->stick_y = KeyAxis('S', 'W');
    s->substick_x = KeyAxis('J', 'L');
    s->substick_y = KeyAxis('K', 'I');
}
