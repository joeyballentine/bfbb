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

static iPadHostState sState[IPAD_MAX_CONTROLLERS];
static bool sKeyboardOnPort0;

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
// has three -- L, R and Z -- and the game wants four, so iPad.cpp there uses Z
// as a modifier: L alone is L1, Z+L is L2. An Xbox pad has four already, so it
// needs no modifier and nothing here synthesises XPAD_BUTTON_Z. Nothing in the
// game reads that bit; it exists only as the modifier's own echo.
//
//     LT -> L1    LB -> L2
//     RT -> R1    RB -> R2

// Named rather than static, for the same reason as the stick conversion.
U32 iPadHostWin32ConvertButtons(const XINPUT_GAMEPAD& gp)
{
    U32 on = 0;

    if (gp.wButtons & XINPUT_GAMEPAD_START) on |= XPAD_BUTTON_START;
    if (gp.wButtons & XINPUT_GAMEPAD_BACK) on |= XPAD_BUTTON_SELECT;

    if (gp.wButtons & XINPUT_GAMEPAD_DPAD_UP) on |= XPAD_BUTTON_UP;
    if (gp.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) on |= XPAD_BUTTON_DOWN;
    if (gp.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) on |= XPAD_BUTTON_LEFT;
    if (gp.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) on |= XPAD_BUTTON_RIGHT;

    if (gp.wButtons & XINPUT_GAMEPAD_A) on |= XPAD_BUTTON_X;
    if (gp.wButtons & XINPUT_GAMEPAD_B) on |= XPAD_BUTTON_TRIANGLE;
    if (gp.wButtons & XINPUT_GAMEPAD_X) on |= XPAD_BUTTON_O;
    if (gp.wButtons & XINPUT_GAMEPAD_Y) on |= XPAD_BUTTON_SQUARE;

    if (gp.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) on |= XPAD_BUTTON_L2;
    if (gp.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) on |= XPAD_BUTTON_R2;

    // The triggers are analog and the game's buttons are not, so they click at
    // a threshold the way the GameCube's do -- gc/iPad.cpp uses 0x18 out of the
    // same 0..255 range for exactly this.
    if (gp.bLeftTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) on |= XPAD_BUTTON_L1;
    if (gp.bRightTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) on |= XPAD_BUTTON_R1;

    return on;
}

// ---------------------------------------------------------------------------
// Keyboard
//
// Only ever port 0, and only when no controller is on it. A keyboard is not a
// second player; it is what makes the port playable on a machine with no pad.
//
//     WASD          left stick        arrows      d-pad
//     IJKL          c-stick           Enter       start
//     Space         GameCube A        LCtrl       GameCube B
//     E             GameCube X        Q           GameCube Y
//     Z / X         L1 / L2           C / V       R1 / R2
//     Backspace     select
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

    if (KeyDown(VK_RETURN)) on |= XPAD_BUTTON_START;
    if (KeyDown(VK_BACK)) on |= XPAD_BUTTON_SELECT;

    if (KeyDown(VK_UP)) on |= XPAD_BUTTON_UP;
    if (KeyDown(VK_DOWN)) on |= XPAD_BUTTON_DOWN;
    if (KeyDown(VK_LEFT)) on |= XPAD_BUTTON_LEFT;
    if (KeyDown(VK_RIGHT)) on |= XPAD_BUTTON_RIGHT;

    if (KeyDown(VK_SPACE)) on |= XPAD_BUTTON_X;
    if (KeyDown(VK_LCONTROL)) on |= XPAD_BUTTON_TRIANGLE;
    if (KeyDown('E')) on |= XPAD_BUTTON_O;
    if (KeyDown('Q')) on |= XPAD_BUTTON_SQUARE;

    if (KeyDown('Z')) on |= XPAD_BUTTON_L1;
    if (KeyDown('X')) on |= XPAD_BUTTON_L2;
    if (KeyDown('C')) on |= XPAD_BUTTON_R1;
    if (KeyDown('V')) on |= XPAD_BUTTON_R2;

    s->buttons = on;

    // Y is up-positive here, as iPadHost.h specifies and as the GameCube stick
    // reports it; iPadUpdate negates it on the way to the game.
    s->stick_x = KeyAxis('A', 'D');
    s->stick_y = KeyAxis('S', 'W');
    s->substick_x = KeyAxis('J', 'L');
    s->substick_y = KeyAxis('K', 'I');
}

// ---------------------------------------------------------------------------

void iPadHostInit()
{
    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        sState[i].connected = false;
        sState[i].buttons = 0;
        sState[i].stick_x = 0.0f;
        sState[i].stick_y = 0.0f;
        sState[i].substick_x = 0.0f;
        sState[i].substick_y = 0.0f;
    }

    sKeyboardOnPort0 = false;

    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        // Every port gets one immediate probe; the timer only starts once a
        // port has actually answered that it is empty.
        sNextProbe[i] = 0;
    }

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

void iPadHostPoll()
{
    bool pad0 = false;
    const ULONGLONG now = GetTickCount64();

    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        iPadHostState* s = &sState[i];

        // A port that was empty last time is left alone until its deadline, so
        // that hot-plugging is still noticed within a couple of seconds without
        // paying the enumeration cost every frame.
        if (!s->connected && now < sNextProbe[i])
        {
            continue;
        }

        XINPUT_STATE xs;
        if (sXInputGetState == NULL || sXInputGetState((DWORD)i, &xs) != ERROR_SUCCESS)
        {
            s->connected = false;
            s->buttons = 0;
            s->stick_x = 0.0f;
            s->stick_y = 0.0f;
            s->substick_x = 0.0f;
            s->substick_y = 0.0f;

            // Stagger the ports so their re-checks do not land on one frame and
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

        if (i == 0)
        {
            pad0 = true;
        }
    }

    // The keyboard only fills a gap. A controller appearing on port 0 mid-run
    // takes it back on the next frame, which is what a player who has just
    // plugged one in expects.
    sKeyboardOnPort0 = !pad0;
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

    // Both motors together. The GameCube's motor has one setting and
    // PADControlMotor is all retail calls, so there is no envelope to apply --
    // see iPadRumbleFx, which is empty on the console too.
    XINPUT_VIBRATION v;
    v.wLeftMotorSpeed = on ? 65535 : 0;
    v.wRightMotorSpeed = on ? 65535 : 0;

    sXInputSetState((DWORD)port, &v);
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
