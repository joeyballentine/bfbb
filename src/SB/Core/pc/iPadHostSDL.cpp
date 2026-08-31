// The SDL input backend, and the default one.
//
// It exists for the devices XInput cannot see. XInput speaks only to XUSB
// devices -- Xbox pads and the clones that pretend to be them -- so a DualSense,
// a Switch Pro controller, an arcade stick or any of the generic USB pads
// reports nothing at all through iPadHostWin32.cpp. SDL enumerates all of them,
// through DirectInput, raw input, HID and Windows.Gaming.Input at once.
//
// The reason it is SDL rather than DirectInput directly is the mapping. A
// DirectInput device hands you "button 0..127" with no meaning attached: button
// 2 is Circle on one pad and X on another, so nothing in config.ini could say
// `a = a` and be true on more than one device. SDL carries a controller
// database that maps every pad it knows to one standard layout, so the token
// names below mean the same physical position everywhere and a config.ini
// written for an Xbox pad works on a DualSense unchanged.
//
// SDL also does the awkward part of running both APIs at once: DirectInput
// enumerates XInput devices too, so an Xbox pad would otherwise appear twice.
//
// What this backend does NOT use SDL for is the keyboard, the window or
// anything else. SDL_GetKeyboardState wants the video subsystem and an event
// loop tied to an SDL window, and iWindow owns the only window this process
// has. Only SDL_INIT_GAMEPAD is ever initialised, no SDL window is created, and
// the keyboard stays on the shared async-key-state path in iPadKeyboard.h.

#include "iPadHost.h"

#include "iConfig.h"
#include "iHost.h"
#include "iPadBind.h"
#include "iPadKeyboard.h"
#include "iPadStick.h"
#include "xPad.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>

// What each slot is reporting, before any of it is assigned to a game port.
// "Slot" here means the same thing it does in the XInput backend -- the way the
// machine counts controllers, as opposed to the way the game counts ports --
// so that input.controller means one thing whichever backend is linked.
static iPadHostState sSlot[IPAD_MAX_CONTROLLERS];
static S32 sPortSlot[IPAD_MAX_CONTROLLERS];

static iPadHostState sState[IPAD_MAX_CONTROLLERS];
static bool sKeyboardOnPort0;

static S32 sPinnedSlot = -1;
static bool sReady;

// The open gamepad in each slot, and the SDL instance id that put it there. A
// gamepad takes the lowest free slot when it arrives and gives that slot back
// when it leaves, which is what makes a slot number stable enough for someone
// to name one in a settings file. SDL's own instance ids are not: they count up
// forever, so the second controller of a session is id 2 whether or not the
// first is still plugged in.
static SDL_Gamepad* sGamepad[IPAD_MAX_CONTROLLERS];
static SDL_JoystickID sInstance[IPAD_MAX_CONTROLLERS];

// Read once, so the per-frame cost is a load.
static const bool sReportPad = getenv("BFBB_PAD") != NULL;

// ---------------------------------------------------------------------------
// Buttons
//
// The same token names the XInput backend uses, so a [pad] section is worth the
// same on both. SDL3 names the face buttons by POSITION -- south, east, west,
// north -- rather than by the letter printed on them, which is what makes them
// portable: the button under your thumb is south on every pad, and only the
// label changes between an Xbox pad and a Switch one. `a` here is that
// position, as it is on the Xbox controller the defaults were written for.

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

static U32 sPadHeld;

static bool PadInputHeld(S16 id)
{
    return (sPadHeld & (1u << id)) != 0;
}

// The triggers are analog and the game's buttons are not, so they click at a
// threshold the way the GameCube's do. gc/iPad.cpp uses 0x18 out of 255, which
// is 9.4% of the travel; SDL reports a trigger from 0 to 32767, so the same
// fraction is 3084.
//
// Not 65535. SDL's STICK axes are signed and span 65535, and reusing that span
// here put the click at 23.5% -- far enough down a real analog trigger to feel
// like the button was missing its first half.
#define IPAD_SDL_TRIGGER_THRESHOLD 3084

static U32 ConvertButtons(SDL_Gamepad* pad)
{
    sPadHeld = 0;

    struct Map
    {
        SDL_GamepadButton button;
        S32 input;
    };

    static const Map kMap[] = {
        { SDL_GAMEPAD_BUTTON_SOUTH, PADIN_A },
        { SDL_GAMEPAD_BUTTON_EAST, PADIN_B },
        { SDL_GAMEPAD_BUTTON_WEST, PADIN_X },
        { SDL_GAMEPAD_BUTTON_NORTH, PADIN_Y },
        { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, PADIN_LB },
        { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PADIN_RB },
        { SDL_GAMEPAD_BUTTON_LEFT_STICK, PADIN_LS },
        { SDL_GAMEPAD_BUTTON_RIGHT_STICK, PADIN_RS },
        { SDL_GAMEPAD_BUTTON_BACK, PADIN_BACK },
        { SDL_GAMEPAD_BUTTON_START, PADIN_START },
        { SDL_GAMEPAD_BUTTON_DPAD_UP, PADIN_DPUP },
        { SDL_GAMEPAD_BUTTON_DPAD_DOWN, PADIN_DPDOWN },
        { SDL_GAMEPAD_BUTTON_DPAD_LEFT, PADIN_DPLEFT },
        { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PADIN_DPRIGHT },
    };

    for (S32 i = 0; i < (S32)(sizeof(kMap) / sizeof(kMap[0])); i++)
    {
        if (SDL_GetGamepadButton(pad, kMap[i].button))
        {
            sPadHeld |= 1u << kMap[i].input;
        }
    }

    if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) >= IPAD_SDL_TRIGGER_THRESHOLD)
    {
        sPadHeld |= 1u << PADIN_LT;
    }
    if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) >= IPAD_SDL_TRIGGER_THRESHOLD)
    {
        sPadHeld |= 1u << PADIN_RT;
    }

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

static void ClearState(iPadHostState* s)
{
    s->connected = false;
    s->buttons = 0;
    s->stick_x = 0.0f;
    s->stick_y = 0.0f;
    s->substick_x = 0.0f;
    s->substick_y = 0.0f;
}

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

static void OpenGamepad(SDL_JoystickID id)
{
    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        if (sInstance[i] == id)
        {
            return;
        }
    }

    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        if (sGamepad[i] != NULL)
        {
            continue;
        }

        SDL_Gamepad* pad = SDL_OpenGamepad(id);
        if (pad == NULL)
        {
            return;
        }

        sGamepad[i] = pad;
        sInstance[i] = id;

        const char* name = SDL_GetGamepadName(pad);
        printf("bfbb: controller %d is %s\n", (int)(i + 1), name != NULL ? name : "unnamed");
        fflush(stdout);
        return;
    }

    // More controllers than the game has ports. Nothing is wrong; the game has
    // four and the machine may have more.
}

static void CloseGamepad(SDL_JoystickID id)
{
    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        if (sInstance[i] != id)
        {
            continue;
        }

        if (sGamepad[i] != NULL)
        {
            SDL_CloseGamepad(sGamepad[i]);
        }

        sGamepad[i] = NULL;
        sInstance[i] = 0;
        ClearState(&sSlot[i]);
        return;
    }
}

// gamecontrollerdb.txt, if someone has put one beside the executable. SDL
// carries a layout for every controller it knows, and this is where a
// controller it does not know gets one -- the community file of that name, or
// a single line produced by SDL's own gamepad mapping tool.
//
// Optional, and silent when absent: not having one is the normal case.
static void LoadMappings()
{
    char dir[512];
    if (!iHostExeDir(dir, sizeof(dir)))
    {
        return;
    }

    char path[600];
    snprintf(path, sizeof(path), "%s/gamecontrollerdb.txt", dir);

    S32 added = SDL_AddGamepadMappingsFromFile(path);
    if (added > 0)
    {
        printf("bfbb: %d controller layouts from gamecontrollerdb.txt\n", (int)added);
        fflush(stdout);
    }
}

// A device SDL can see but has no layout for. It enumerates as a joystick --
// an ordered pile of axes and buttons with nothing saying which is which --
// and never becomes a gamepad, so nothing above opens it.
//
// Worth saying out loud. Silence reads as "the port cannot see my controller"
// when what happened is "SDL does not know this one", and the two have
// different answers.
static void ReportIfUnmapped(SDL_JoystickID id)
{
    if (SDL_IsGamepad(id))
    {
        return;
    }

    const char* name = SDL_GetJoystickNameForID(id);
    printf("bfbb: %s (%04x:%04x) is plugged in, but SDL has no button layout for it, so it "
           "cannot be played on. A line for it in gamecontrollerdb.txt beside the exe would "
           "give it one.\n",
           name != NULL ? name : "an unnamed device", (unsigned)SDL_GetJoystickVendorForID(id),
           (unsigned)SDL_GetJoystickProductForID(id));
    fflush(stdout);
}

void iPadHostInit()
{
    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        ClearState(&sState[i]);
        ClearState(&sSlot[i]);
        sPortSlot[i] = -1;
        sGamepad[i] = NULL;
        sInstance[i] = 0;
    }

    sKeyboardOnPort0 = false;

    ChooseController();
    iPadBindLoad(IPAD_BIND_PAD, kPadTokens, kPadTokenCount, sPadBind);
    iPadKeyboardInit();

    // XInput has no notion of focus and this backend should not grow one: the
    // keyboard already stops when the window loses focus, and a controller that
    // went dead on alt-tab would be a change in behaviour, not a fix.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    // GAMEPAD only. No video, no audio, no window -- see the note at the top.
    if (!SDL_Init(SDL_INIT_GAMEPAD))
    {
        printf("bfbb: SDL could not start its gamepad subsystem (%s); no controllers will be "
               "seen and the keyboard covers port 0\n",
               SDL_GetError());
        fflush(stdout);
        return;
    }

    sReady = true;

    // Before anything is enumerated, so a device the file covers is already a
    // gamepad by the time it is looked at.
    LoadMappings();

    // Whatever is already plugged in. Everything after this arrives as an event.
    S32 count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids != NULL)
    {
        for (S32 i = 0; i < count; i++)
        {
            OpenGamepad(ids[i]);
        }
        SDL_free(ids);
    }

}

void iPadHostExit()
{
    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        if (sGamepad[i] != NULL)
        {
            // Leave no motor running. The device outlives the process.
            SDL_RumbleGamepad(sGamepad[i], 0, 0, 0);
            SDL_CloseGamepad(sGamepad[i]);
            sGamepad[i] = NULL;
            sInstance[i] = 0;
        }
    }

    if (sReady)
    {
        SDL_Quit();
        sReady = false;
    }
}

// Which slot each game port reads. Identical in meaning to the XInput
// backend's, and deliberately so -- input.controller is documented once.
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

    if (sPortSlot[0] != previous && sPortSlot[0] >= 0)
    {
        printf("bfbb: playing on controller %d\n", (int)(sPortSlot[0] + 1));
        fflush(stdout);
    }
}

void iPadHostPoll()
{
    if (sReady)
    {
        // Draining the queue is what refreshes the gamepad state as well as
        // what reports a controller arriving or leaving, so hot-plugging needs
        // no timer here -- unlike the XInput backend, which has to ask.
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_EVENT_GAMEPAD_ADDED)
            {
                OpenGamepad(e.gdevice.which);
            }
            else if (e.type == SDL_EVENT_GAMEPAD_REMOVED)
            {
                CloseGamepad(e.gdevice.which);
            }
            else if (e.type == SDL_EVENT_JOYSTICK_ADDED)
            {
                // Every gamepad is a joystick, so this fires for those too and
                // ReportIfUnmapped drops them. What is left is a device that
                // arrived and will do nothing.
                //
                // SDL posts this for devices that were already plugged in when
                // it started, as well as for ones arriving later, so the first
                // poll covers both and iPadHostInit does not enumerate.
                ReportIfUnmapped(e.jdevice.which);
            }
        }

        for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
        {
            iPadHostState* s = &sSlot[i];

            if (sGamepad[i] == NULL)
            {
                ClearState(s);
                continue;
            }

            s->connected = true;
            s->buttons = ConvertButtons(sGamepad[i]);

            iPadStickConvert(SDL_GetGamepadAxis(sGamepad[i], SDL_GAMEPAD_AXIS_LEFTX),
                             SDL_GetGamepadAxis(sGamepad[i], SDL_GAMEPAD_AXIS_LEFTY),
                             IPAD_STICK_DEADZONE_LEFT, &s->stick_x, &s->stick_y);
            iPadStickConvert(SDL_GetGamepadAxis(sGamepad[i], SDL_GAMEPAD_AXIS_RIGHTX),
                             SDL_GetGamepadAxis(sGamepad[i], SDL_GAMEPAD_AXIS_RIGHTY),
                             IPAD_STICK_DEADZONE_RIGHT, &s->substick_x, &s->substick_y);

            // SDL reports Y down-positive, as a screen does. iPadHost.h asks for
            // up-positive, as the GameCube's stick reports it, and iPadUpdate
            // negates it again on the way to the game.
            s->stick_y = -s->stick_y;
            s->substick_y = -s->substick_y;
        }
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

    sKeyboardOnPort0 = !sState[0].connected;
    if (sKeyboardOnPort0)
    {
        iPadKeyboardPoll(&sState[0]);
    }

    // BFBB_PAD: what port 0 is actually reporting, printed when it changes. The
    // same probe the XInput backend carries, and for the same reason -- when a
    // button does nothing, this says whether the bit was ever produced.
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
    if (!sReady || port < 0 || port >= IPAD_MAX_CONTROLLERS)
    {
        return;
    }

    if (port == 0 && sKeyboardOnPort0)
    {
        return;
    }

    S32 slot = sPortSlot[port];
    if (slot < 0 || sGamepad[slot] == NULL)
    {
        return;
    }

    // Both motors together. The GameCube's motor has one setting and
    // PADControlMotor is all retail calls, so there is no envelope to apply --
    // see iPadRumbleFx, which is empty on the console too.
    //
    // SDL wants a duration and treats 0 as "stop now", so an effect that is
    // meant to run until the game says otherwise gets the longest one there is.
    // A real duration would end a rumble the game still believes is running.
    Uint16 strength = on ? 0xFFFF : 0;
    SDL_RumbleGamepad(sGamepad[slot], strength, strength, on ? SDL_MAX_UINT32 : 0);
}

const char* iPadHostName()
{
    return sReady ? "sdl (every controller SDL knows, plus keyboard)"
                  : "sdl (keyboard only -- SDL's gamepad subsystem did not start)";
}
