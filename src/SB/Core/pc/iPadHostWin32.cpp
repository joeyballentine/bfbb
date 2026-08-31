#include "iImgui.h"
#include "iPadHost.h"

#include "xPad.h"

// The Win32 input backend: XInput for controllers, with the keyboard standing
// in for port 0 when nothing is plugged into it.
//
// XInput is loaded at run time rather than linked. Three DLLs export the same
// two entry points this file needs, they ship with different Windows versions,
// and none of them is guaranteed to be present -- a Windows install stripped of
// the DirectX runtime has none. Linking against an import library would turn
// that into a process that refuses to start, which is a worse failure than the
// one the seam already handles: with no XInput, every port reports no
// controller and the keyboard covers port 0.
//
// What this file does NOT do is read the keyboard through the window's message
// queue. iWindow owns that queue and the pad is polled from iPadUpdate, so the
// two would have to be wired together for no gain -- the game samples the pad
// once a frame and wants level, not edges, which is exactly what the async key
// state gives. xPad.cpp derives pressed/released from consecutive frames
// itself.

#include "iConfig.h"
#include "iImgui.h"
#include "iHost.h"
#include "iPadBind.h"

#include <windows.h>
#include <xinput.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// XInput, resolved at run time.

typedef DWORD(WINAPI* XInputGetStateFn)(DWORD, XINPUT_STATE*);
typedef DWORD(WINAPI* XInputSetStateFn)(DWORD, XINPUT_VIBRATION*);

static HMODULE sXInput;
static XInputGetStateFn sXInputGetState;
static XInputSetStateFn sXInputSetState;

// What each XInput slot is reporting, before any of it is assigned to a game
// port. The two are separated because config.ini can choose which slot the
// game plays on: sSlot is indexed the way Windows counts controllers, sState
// the way the game counts ports, and sPortSlot is the map between them.
static iPadHostState sSlot[IPAD_MAX_CONTROLLERS];
static S32 sPortSlot[IPAD_MAX_CONTROLLERS];

static iPadHostState sState[IPAD_MAX_CONTROLLERS];
static bool sKeyboardOnPort0;

// input.controller: the XInput slot pinned to port 0, or -1 for "the first one
// that answers". See ChooseController.
static S32 sPinnedSlot = -1;

// Read once, so the per-frame cost is a load.
static const bool sReportPad = getenv("BFBB_PAD") != NULL;

// When each EMPTY port may be asked about again.
//
// XInputGetState on a port with nothing in it does not fail cheaply: it walks
// the device tree through the configuration manager, which costs milliseconds.
// Asking all four ports every frame with one controller plugged in spends most
// of a frame's budget discovering three times over that three ports are still
// empty, and the game visibly crawls -- the watchdog catches the main thread
// inside DevObjGetClassDevs under XInputGetState.
//
// So an empty port is re-checked on a timer instead of every frame. A port that
// answered last time is still polled every frame; only the silent ones wait.
#define IPAD_EMPTY_PORT_RECHECK_MS 2000

static ULONGLONG sNextProbe[IPAD_MAX_CONTROLLERS];

// In the order Windows prefers them. 1_4 ships with Windows 8 and later, 1_3
// comes from the legacy DirectX redistributable, and 9_1_0 is the subset that
// has been in every Windows since Vista -- it is the one that is almost always
// there, and the only one of the three whose absence is worth reporting.
static const char* const kXInputDlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };

static void LoadXInput()
{
    for (S32 i = 0; i < (S32)(sizeof(kXInputDlls) / sizeof(kXInputDlls[0])); i++)
    {
        sXInput = LoadLibraryA(kXInputDlls[i]);
        if (sXInput == NULL)
        {
            continue;
        }

        sXInputGetState = (XInputGetStateFn)GetProcAddress(sXInput, "XInputGetState");
        sXInputSetState = (XInputSetStateFn)GetProcAddress(sXInput, "XInputSetState");

        if (sXInputGetState != NULL && sXInputSetState != NULL)
        {
            return;
        }

        // A DLL with the right name and the wrong exports is not usable. Drop
        // it and keep looking rather than holding a handle that does nothing.
        FreeLibrary(sXInput);
        sXInput = NULL;
        sXInputGetState = NULL;
        sXInputSetState = NULL;
    }
}

// ---------------------------------------------------------------------------
// Sticks
//
// XInput's own documented deadzones, applied radially rather than per axis: a
// square deadzone lets a stick pushed exactly diagonally register while the
// same deflection along one axis does not, which shows up in this game as a
// character that will not walk slowly in one direction but will in another.
// The magnitude is rescaled from the deadzone edge, so the first movement past
// it is small instead of a jump straight to the deadzone fraction.

// Named rather than static so the self-test can reach it: the radial deadzone
// and the rescale are the only real arithmetic in this file, and a test that
// could only drive them through a physical controller would not be run.
void iPadHostWin32ConvertStick(S16 rawX, S16 rawY, S32 deadzone, F32* outX, F32* outY)
{
    F32 x = (F32)rawX;
    F32 y = (F32)rawY;

    F32 magnitude = sqrtf(x * x + y * y);
    if (magnitude <= (F32)deadzone)
    {
        *outX = 0.0f;
        *outY = 0.0f;
        return;
    }

    // Direction first, off the UNCLAMPED magnitude, so that it stays a unit
    // vector. Normalising by the clamped one instead lets a stick held to a
    // corner report sqrt(2) rather than 1, because both axes reach full scale
    // while the length they are divided by does not.
    F32 dirX = x / magnitude;
    F32 dirY = y / magnitude;

    // 32767 rather than 32768: the negative end reaches -32768 but the positive
    // end stops one short, and normalising by the larger value would leave full
    // deflection reading as slightly less than full.
    const F32 limit = 32767.0f;
    if (magnitude > limit)
    {
        magnitude = limit;
    }

    F32 scaled = (magnitude - (F32)deadzone) / (limit - (F32)deadzone);

    *outX = dirX * scaled;
    *outY = dirY * scaled;
}

// ---------------------------------------------------------------------------
// Buttons
//
// Face buttons are mapped by name: the GameCube and the Xbox controller both
// have A, B, X and Y, retail's own button prompts name them, and the two sets
// line up. src/SB/Core/gc/iPad.cpp is the authority for the other half of the
// mapping -- which XPAD_BUTTON_* each console button becomes -- and the names
// in xPad.h are PlayStation's, so the correspondence is worth spelling out:
//
//     GameCube A -> XPAD_BUTTON_X          (jump, confirm)
//     GameCube B -> XPAD_BUTTON_TRIANGLE
//     GameCube X -> XPAD_BUTTON_O
//     GameCube Y -> XPAD_BUTTON_SQUARE
//
// The shoulders are where the two controllers genuinely differ. The GameCube
// has three -- L, R and Z -- and the game wants four, so gc/iPad.cpp uses Z as
// a modifier: L alone is L1, Z+L is L2. Holding Z also sets XPAD_BUTTON_Z in
// its own right, and that bit is not merely the modifier's echo -- zHud.cpp
// shows the HUD on it and zCamera.cpp toggles the near camera with it. A
// mapping that never sets it leaves both of those dead.
//
// So the triggers stand in for the GameCube's analog L and R, and RB is Z:
//
//     LT -> L1        RB + LT -> L2
//     RT -> R1        RB + RT -> R2
//     RB -> Z
//
// LB goes unused. The GameCube has no fourth shoulder and every bit the game
// reads is reachable without one -- which makes LB, and the two stick clicks,
// the obvious places for someone remapping to put something.
//
// All of the above is now the DEFAULT rather than the mapping: config.ini's
// [pad] section says which input presses which button, and iPadBind.cpp holds
// the defaults and the grammar. The exclusivity above is why that grammar has
// a '!' -- `l1 = lt+!rb` is what keeps Z+L from being L1 and L2 at once.

// The inputs an XInput pad has, numbered for the binder. Its ids are opaque to
// it, so this file is free to use a dense range and test them as a bitmask.
enum
{
    PADIN_A,
    PADIN_B,
    PADIN_X,
    PADIN_Y,
    PADIN_LB,
    PADIN_RB,
    PADIN_LT,
    PADIN_RT,
    PADIN_LS,
    PADIN_RS,
    PADIN_BACK,
    PADIN_START,
    PADIN_DPUP,
    PADIN_DPDOWN,
    PADIN_DPLEFT,
    PADIN_DPRIGHT,
    PADIN_COUNT
};

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

static const S32 kPadTokenCount = (S32)(sizeof(kPadTokens) / sizeof(kPadTokens[0]));

static iPadBind sPadBind[IPAD_BIND_MAX_BUTTONS];
static iPadBind sKeyBind[IPAD_BIND_MAX_BUTTONS];

// The pad currently being converted, as one bit per PADIN_*. A binding is
// evaluated through a callback that takes an id and nothing else, so the
// device state has to be somewhere it can reach; a mask built once per
// conversion is cheaper than handing the gamepad down and re-testing it.
static U32 sPadHeld;

static bool PadInputHeld(S16 id)
{
    return (sPadHeld & (1u << id)) != 0;
}

// Named rather than static, for the same reason as the stick conversion.
U32 iPadHostWin32ConvertButtons(const XINPUT_GAMEPAD& gp)
{
    sPadHeld = 0;

    if (gp.wButtons & XINPUT_GAMEPAD_A) sPadHeld |= 1u << PADIN_A;
    if (gp.wButtons & XINPUT_GAMEPAD_B) sPadHeld |= 1u << PADIN_B;
    if (gp.wButtons & XINPUT_GAMEPAD_X) sPadHeld |= 1u << PADIN_X;
    if (gp.wButtons & XINPUT_GAMEPAD_Y) sPadHeld |= 1u << PADIN_Y;

    if (gp.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) sPadHeld |= 1u << PADIN_LB;
    if (gp.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) sPadHeld |= 1u << PADIN_RB;
    if (gp.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) sPadHeld |= 1u << PADIN_LS;
    if (gp.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) sPadHeld |= 1u << PADIN_RS;

    if (gp.wButtons & XINPUT_GAMEPAD_BACK) sPadHeld |= 1u << PADIN_BACK;
    if (gp.wButtons & XINPUT_GAMEPAD_START) sPadHeld |= 1u << PADIN_START;

    if (gp.wButtons & XINPUT_GAMEPAD_DPAD_UP) sPadHeld |= 1u << PADIN_DPUP;
    if (gp.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) sPadHeld |= 1u << PADIN_DPDOWN;
    if (gp.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) sPadHeld |= 1u << PADIN_DPLEFT;
    if (gp.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) sPadHeld |= 1u << PADIN_DPRIGHT;

    // The triggers are analog and the game's buttons are not, so they click at
    // a threshold the way the GameCube's do -- gc/iPad.cpp uses 0x18 out of the
    // same 0..255 range for exactly this.
    if (gp.bLeftTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) sPadHeld |= 1u << PADIN_LT;
    if (gp.bRightTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) sPadHeld |= 1u << PADIN_RT;

    U32 on = 0;
    for (S32 i = 0; i < kPadBindButtonCount; i++)
    {
        if (iPadBindHeld(sPadBind[i], PadInputHeld))
        {
            on |= kPadBindButtons[i].mask;
        }
    }

    return on;
}

// ---------------------------------------------------------------------------
// Keyboard
//
// Only ever port 0, and only when no controller is on it. A keyboard is not a
// second player; it is what makes the port playable on a machine with no pad.
//
//     WASD          left stick        IJKL        c-stick
//
// The two sticks are the part config.ini cannot move. Everything else is in
// its [keyboard] section, defaulted in iPadBind.cpp to the layout this file
// used to hard-code.
//
// GetActiveWindow rather than a window handle: it reports the active window of
// the CALLING THREAD's queue, so it is non-null exactly when one of our own
// windows has focus and null the moment the user alt-tabs away. That is the
// gate this needs, and it does not require iWindow to publish its HWND -- which
// iWindow.h says only the RenderWare shim may interpret.

static bool KeyDown(S32 vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// Key names, for the right-hand side of a [keyboard] binding. Virtual-key
// codes are the ids, so the binder's lookup is the whole translation.
//
// Letters and digits are not listed: their VK codes ARE their ASCII values, so
// KeyTokenId answers a one-character name directly rather than carrying
// thirty-six rows that say so. What is listed is everything whose name is not
// its character.
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

// Letters and digits, folded to their virtual-key code, which is the upper-case
// character. Built once so the binder sees one flat table.
static iPadBindToken sKeyTokenTable[kKeyTokenCount + 36];
static S32 sKeyTokenTableCount;
static char sKeyTokenNames[36][2];

static void BuildKeyTokens()
{
    sKeyTokenTableCount = 0;

    for (S32 i = 0; i < kKeyTokenCount; i++)
    {
        sKeyTokenTable[sKeyTokenTableCount++] = kKeyTokens[i];
    }

    for (S32 i = 0; i < 36; i++)
    {
        char c = (i < 26) ? (char)('a' + i) : (char)('0' + (i - 26));
        sKeyTokenNames[i][0] = c;
        sKeyTokenNames[i][1] = '\0';

        sKeyTokenTable[sKeyTokenTableCount].name = sKeyTokenNames[i];
        sKeyTokenTable[sKeyTokenTableCount].id =
            (S16)((i < 26) ? ('A' + i) : ('0' + (i - 26)));
        sKeyTokenTableCount++;
    }
}

static bool KeyInputHeld(S16 vk)
{
    return KeyDown(vk);
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

static void PollKeyboard(iPadHostState* s)
{
    s->connected = true;

    if (
        GetActiveWindow() == NULL
#ifdef ENABLE_IMGUI
        || iImguiWantCapture()
#endif
    ) {
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

// ---------------------------------------------------------------------------
// config.ini

static void ClearState(iPadHostState* s)
{
    s->connected = false;
    s->buttons = 0;
    s->stick_x = 0.0f;
    s->stick_y = 0.0f;
    s->substick_x = 0.0f;
    s->substick_y = 0.0f;
}

// input.controller. "auto", or a slot counted from 1 the way a person counts
// controllers rather than the way XInput numbers them.
static void ChooseController()
{
    const char* v = iConfigGetString("input.controller", "auto");

    if (v == NULL || v[0] == '\0' || iHostStrCaseCmp(v, "auto") == 0)
    {
        sPinnedSlot = -1;
        return;
    }

    S32 n = atoi(v);
    if (n >= 1 && n <= IPAD_MAX_CONTROLLERS)
    {
        sPinnedSlot = n - 1;
        return;
    }

    printf("bfbb: input.controller is '%s', which is neither auto nor 1 to %d; using auto\n", v,
           (int)IPAD_MAX_CONTROLLERS);
    fflush(stdout);
    sPinnedSlot = -1;
}

static void LoadBindings()
{
    BuildKeyTokens();

    for (S32 i = 0; i < kPadBindButtonCount && i < IPAD_BIND_MAX_BUTTONS; i++)
    {
        const iPadBindButton* b = &kPadBindButtons[i];
        char key[64];

        snprintf(key, sizeof(key), "pad.%s", b->name);
        iPadBindParse(iConfigGetString(key, b->pad), kPadTokens, kPadTokenCount, key, &sPadBind[i]);

        snprintf(key, sizeof(key), "keyboard.%s", b->name);
        iPadBindParse(iConfigGetString(key, b->key), sKeyTokenTable, sKeyTokenTableCount, key,
                      &sKeyBind[i]);
    }

    // The array is sized by a macro and filled from a table whose length the
    // compiler will not hand over, so this is where the two are compared. A
    // button past the end would silently never be pressable.
    if (kPadBindButtonCount > IPAD_BIND_MAX_BUTTONS)
    {
        printf("bfbb: %d buttons to bind but room for %d; the last %d are unbound\n",
               (int)kPadBindButtonCount, (int)IPAD_BIND_MAX_BUTTONS,
               (int)(kPadBindButtonCount - IPAD_BIND_MAX_BUTTONS));
        fflush(stdout);
    }
}

// ---------------------------------------------------------------------------

void iPadHostInit()
{
    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        ClearState(&sState[i]);
        ClearState(&sSlot[i]);
        sPortSlot[i] = -1;

        // Every slot gets one immediate probe; the timer only starts once a
        // slot has actually answered that it is empty.
        sNextProbe[i] = 0;
    }

    sKeyboardOnPort0 = false;

    ChooseController();
    LoadBindings();

    LoadXInput();
}

void iPadHostExit()
{
    // Leave no motor running. The device outlives the process.
    if (sXInputSetState != NULL)
    {
        for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
        {
            XINPUT_VIBRATION off;
            off.wLeftMotorSpeed = 0;
            off.wRightMotorSpeed = 0;
            sXInputSetState((DWORD)i, &off);
        }
    }

    if (sXInput != NULL)
    {
        FreeLibrary(sXInput);
        sXInput = NULL;
        sXInputGetState = NULL;
        sXInputSetState = NULL;
    }
}

// Which XInput slot each game port reads, from what is plugged in and what
// input.controller asked for.
//
// The two settings want different rules, and each is right for its case:
//
//   auto     the connected slots are packed onto the ports in order, so the
//            only controller on the machine is port 0 whichever slot it landed
//            in. That is the case this setting exists for -- a wireless
//            receiver or a wheel can sit in slot 1 with nothing in it.
//   a number that slot IS port 0, plugged in or not. Falling through to
//            another controller when the chosen one is off would defeat the
//            point of choosing, and would do it silently.
static void MapPortsToSlots()
{
    S32 previous = sPortSlot[0];

    for (S32 p = 0; p < IPAD_MAX_CONTROLLERS; p++)
    {
        sPortSlot[p] = -1;
    }

    if (sPinnedSlot >= 0)
    {
        sPortSlot[0] = sPinnedSlot;

        S32 port = 1;
        for (S32 slot = 0; slot < IPAD_MAX_CONTROLLERS; slot++)
        {
            if (slot != sPinnedSlot)
            {
                sPortSlot[port++] = slot;
            }
        }
    }
    else
    {
        S32 port = 0;
        for (S32 slot = 0; slot < IPAD_MAX_CONTROLLERS; slot++)
        {
            if (sSlot[slot].connected)
            {
                sPortSlot[port++] = slot;
            }
        }
    }

    // Worth one line when it changes: which controller the game is listening to
    // is otherwise invisible, and with this setting it is now a question that
    // has more than one answer.
    if (sPortSlot[0] != previous && sPortSlot[0] >= 0)
    {
        printf("bfbb: playing on controller %d\n", (int)(sPortSlot[0] + 1));
        fflush(stdout);
    }
}

void iPadHostPoll()
{
    const ULONGLONG now = GetTickCount64();

    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        iPadHostState* s = &sSlot[i];

        // A slot that was empty last time is left alone until its deadline, so
        // that hot-plugging is still noticed within a couple of seconds without
        // paying the enumeration cost every frame.
        if (!s->connected && now < sNextProbe[i])
        {
            continue;
        }

        XINPUT_STATE xs;
        if (sXInputGetState == NULL || sXInputGetState((DWORD)i, &xs) != ERROR_SUCCESS)
        {
            ClearState(s);

            // Stagger the slots so their re-checks do not land on one frame and
            // reproduce the stall in a burst once every two seconds.
            sNextProbe[i] = now + IPAD_EMPTY_PORT_RECHECK_MS + (ULONGLONG)(i * 137);
            continue;
        }

        s->connected = true;
        s->buttons = iPadHostWin32ConvertButtons(xs.Gamepad);

        iPadHostWin32ConvertStick(xs.Gamepad.sThumbLX, xs.Gamepad.sThumbLY,
                                  XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, &s->stick_x, &s->stick_y);
        iPadHostWin32ConvertStick(xs.Gamepad.sThumbRX, xs.Gamepad.sThumbRY,
                                  XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE, &s->substick_x,
                                  &s->substick_y);
    }

    MapPortsToSlots();

    for (S32 p = 0; p < IPAD_MAX_CONTROLLERS; p++)
    {
        if (sPortSlot[p] >= 0)
        {
            sState[p] = sSlot[sPortSlot[p]];
        }
        else
        {
            ClearState(&sState[p]);
        }
    }

    // The keyboard only fills a gap. A controller appearing on port 0 mid-run
    // takes it back on the next frame, which is what a player who has just
    // plugged one in expects.
    sKeyboardOnPort0 = !sState[0].connected;
    if (sKeyboardOnPort0)
    {
        PollKeyboard(&sState[0]);
    }

    // BFBB_PAD: what port 0 is actually reporting, printed when it changes.
    //
    // The mapping in this file is the only thing between a device and the bits
    // the game tests, and every one of those tests is a bare mask against
    // pad->on or pad->pressed. When a button does nothing there are two
    // possibilities -- this file never produced the bit, or the game did not
    // act on it -- and they need different fixes. This says which.
    if (sReportPad)
    {
        static U32 lastButtons = 0;
        static bool lastConnected = false;

        if (sState[0].buttons != lastButtons || sState[0].connected != lastConnected)
        {
            printf("bfbb: pad0 %s buttons %08x%s%s%s%s%s%s%s%s\n",
                   sState[0].connected ? "connected" : "absent", (unsigned)sState[0].buttons,
                   (sState[0].buttons & XPAD_BUTTON_L1) ? " L1" : "",
                   (sState[0].buttons & XPAD_BUTTON_L2) ? " L2" : "",
                   (sState[0].buttons & XPAD_BUTTON_R1) ? " R1" : "",
                   (sState[0].buttons & XPAD_BUTTON_R2) ? " R2" : "",
                   (sState[0].buttons & XPAD_BUTTON_X) ? " X(gcA)" : "",
                   (sState[0].buttons & XPAD_BUTTON_TRIANGLE) ? " TRI(gcB)" : "",
                   (sState[0].buttons & XPAD_BUTTON_O) ? " O(gcX)" : "",
                   (sState[0].buttons & XPAD_BUTTON_START) ? " START" : "");
            fflush(stdout);

            lastButtons = sState[0].buttons;
            lastConnected = sState[0].connected;
        }
    }
}

const iPadHostState* iPadHostGet(S32 port)
{
    if (port < 0 || port >= IPAD_MAX_CONTROLLERS)
    {
        return NULL;
    }

    return &sState[port];
}

void iPadHostRumble(S32 port, S32 on)
{
    if (sXInputSetState == NULL || port < 0 || port >= IPAD_MAX_CONTROLLERS)
    {
        return;
    }

    // The keyboard has no motor, and asking port 0's absent controller to
    // vibrate would be one failed call per frame for as long as the game
    // rumbles.
    if (port == 0 && sKeyboardOnPort0)
    {
        return;
    }

    // The game rumbles a port; XInput vibrates a slot. Before input.controller
    // those were the same number.
    S32 slot = sPortSlot[port];
    if (slot < 0)
    {
        return;
    }

    // Both motors together. The GameCube's motor has one setting and
    // PADControlMotor is all retail calls, so there is no envelope to apply --
    // see iPadRumbleFx, which is empty on the console too.
    XINPUT_VIBRATION v;
    v.wLeftMotorSpeed = on ? 65535 : 0;
    v.wRightMotorSpeed = on ? 65535 : 0;

    sXInputSetState((DWORD)slot, &v);
}

const char* iPadHostName()
{
    if (sXInput != NULL)
    {
        return "win32 (XInput + keyboard)";
    }

    // Worth saying out loud: no XInput means no controller will ever be seen,
    // however many are plugged in, and the reason is this machine's DLLs.
    return "win32 (keyboard only -- no XInput DLL found)";
}
