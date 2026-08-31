#include "iPadHost.h"

#include "xPad.h"

// The XInput input backend, and the one that needs nothing built alongside it.
// The SDL backend in iPadHostSDL.cpp is the default and reaches far more
// devices; this one is what a build with no submodules still has, and what
// answers if SDL ever turns out to be the problem.
//
// XInput is loaded at run time rather than linked. Three DLLs export the same
// two entry points this file needs, they ship with different Windows versions,
// and none of them is guaranteed to be present -- a Windows install stripped of
// the DirectX runtime has none. Linking against an import library would turn
// that into a process that refuses to start, which is a worse failure than the
// one the seam already handles: with no XInput, every port reports no
// controller and the keyboard covers port 0.
//
// Only the XInput half is here. The keyboard, the stick deadzone and the
// binding table are shared with the other backend -- which controller API this
// file talks to says nothing about any of them.

#include "iConfig.h"
#include "iHost.h"
#include "iPadBind.h"
#include "iPadKeyboard.h"
#include "iPadStick.h"

#include <windows.h>
#include <xinput.h>
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
    iPadBindLoad(IPAD_BIND_PAD, kPadTokens, kPadTokenCount, sPadBind);
    iPadKeyboardInit();

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

        iPadStickConvert(xs.Gamepad.sThumbLX, xs.Gamepad.sThumbLY, IPAD_STICK_DEADZONE_LEFT,
                         &s->stick_x, &s->stick_y);
        iPadStickConvert(xs.Gamepad.sThumbRX, xs.Gamepad.sThumbRY, IPAD_STICK_DEADZONE_RIGHT,
                         &s->substick_x, &s->substick_y);
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
        iPadKeyboardPoll(&sState[0]);
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
