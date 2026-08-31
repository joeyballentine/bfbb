#include "iWindow.h"

// The Win32 window, which is what a D3D9 librw wants: its EngineOpenParams is
// { HWND window; } and nothing else.
//
// Deliberately plain. There is no SDL here because D3D9 does not need one, and
// pulling in a windowing library to hand D3D a handle it could get from
// CreateWindowEx would be a dependency the port does not have to have. The GL3
// build has one -- iWindowSDL.cpp, selected by BFBB_RENDER_BACKEND rather than
// by the host OS, because which window a renderer wants is the renderer's
// question.

#include <windows.h>

static HWND sWindow;
static HINSTANCE sInstance;
static S32 sShouldClose;
static S32 sWidth;
static S32 sHeight;
static iWindowMode sMode = iWINDOW_WINDOWED;

static const char* const kClassName = "BFBBWindow";

// **Tell Windows this process means physical pixels.**
//
// Without it the process is DPI UNAWARE, and on a machine whose monitors are at
// different scalings that is not a cosmetic setting. Windows virtualises the
// window: it is created at one size, and the moment it moves to a monitor at a
// different scaling the OS resizes it behind the game's back. That resize is
// indistinguishable from the user resizing it, so the client rect stops matching
// the back buffer and librw resets the D3D device -- repeatedly, growing the
// window by the ratio of the two scalings each time. On this machine, one
// monitor at 100% and one 4K panel at 125%, dragging the window across grew it
// by a fifth per reset and marched it off the screen.
//
// Per-monitor v2 is the one to ask for: it also makes Windows scale the
// non-client area and send WM_DPICHANGED with the rectangle it wants, which is
// what the handler below honours.
//
// Resolved at run time rather than linked. SetProcessDpiAwarenessContext is
// Windows 10 1703 and later, SetProcessDpiAwareness is 8.1, and
// SetProcessDPIAware is Vista -- so the best one available is taken and an older
// Windows still gets something. Importing the newest by name would make the
// executable refuse to start on anything older, which is a poor trade for a
// setting.
//
// A manifest would be the conventional way and is deliberately not used: the
// port already builds with and without a resource compiler (see the icon in
// CMakeLists), and a setting this load bearing should not be the thing that
// silently differs between those two builds.
static void MakeProcessDpiAware()
{
    typedef BOOL(WINAPI * PFN_SetProcessDpiAwarenessContext)(HANDLE);
    typedef HRESULT(WINAPI * PFN_SetProcessDpiAwareness)(int);

    // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2, which is a sentinel handle
    // rather than an enum and so is spelled out here.
    HANDLE perMonitorV2 = (HANDLE)-4;

    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (user32 != NULL)
    {
        PFN_SetProcessDpiAwarenessContext setContext =
            (PFN_SetProcessDpiAwarenessContext)GetProcAddress(user32,
                                                              "SetProcessDpiAwarenessContext");
        if (setContext != NULL && setContext(perMonitorV2))
        {
            return;
        }
    }

    HMODULE shcore = LoadLibraryA("shcore.dll");
    if (shcore != NULL)
    {
        PFN_SetProcessDpiAwareness setAwareness =
            (PFN_SetProcessDpiAwareness)GetProcAddress(shcore, "SetProcessDpiAwareness");
        // 2 is PROCESS_PER_MONITOR_DPI_AWARE.
        if (setAwareness != NULL && SUCCEEDED(setAwareness(2)))
        {
            return;
        }
    }

    SetProcessDPIAware();
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_CLOSE:
        // NOT DestroyWindow. The game owns its own shutdown -- it saves, tears
        // RenderWare down and frees its heap -- and destroying the window here
        // would pull the D3D device out from under all of that. iSystem reads
        // iWindowShouldClose and runs the game's exit path instead.
        sShouldClose = 1;
        return 0;

    case WM_DESTROY:
        sWindow = NULL;
        return 0;

    case WM_SIZE:
        sWidth = LOWORD(lparam);
        sHeight = HIWORD(lparam);
        return 0;

    // The window has moved to a monitor at a different scaling.
    //
    // Under per-monitor v2 awareness Windows does not resize the window itself;
    // it asks, and lparam is the rectangle it suggests -- already scaled, and
    // already including the non-client area. Honouring it is what keeps the
    // window the same PHYSICAL size across a move, which is what someone
    // dragging it between two monitors means to happen.
    //
    // Ignoring the message is the thing not to do: the window then keeps its
    // pixel size while the title bar and borders around it are rescaled, so the
    // client area changes anyway and the device is reset for a move that should
    // not have touched it.
    case WM_DPICHANGED:
    {
        const RECT* suggested = (const RECT*)lparam;
        if (suggested != NULL)
        {
            SetWindowPos(hwnd, NULL, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }

    // The screensaver and monitor-power messages, refused while the game runs.
    // Retail did not need this because a GameCube has no screensaver; a host
    // blanking the screen mid-cutscene is a real thing that happens.
    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0) == SC_SCREENSAVE || (wparam & 0xFFF0) == SC_MONITORPOWER)
        {
            return 0;
        }
        break;
    }

    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

S32 iWindowOpen(const iWindowParams* params)
{
    if (params == NULL || sWindow != NULL)
    {
        return FALSE;
    }

    // Keep the display awake for as long as the game is up.
    //
    // The WM_SYSCOMMAND refusal below is not enough on its own: it only reaches
    // the foreground window and only once there IS one being pumped, so it does
    // nothing about a screensaver that is already running when the process
    // starts. This is the API that actually says "do not blank the screen", and
    // it applies from here rather than from the first frame.
    //
    // It matters more than a blanked screen would suggest. When the display
    // sleeps, this machine's adapters stop reporting D3DDEVTYPE_HAL, so
    // RenderWare startup fails outright -- and before the probe in
    // rw/engine_start.cpp existed, it faulted inside D3D9 instead of saying so.
    // A screensaver could take the game down.
    SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);

    // Before the class is registered and long before the window exists: the
    // awareness of a process is fixed by its first window, and AdjustWindowRect
    // below already depends on which one is in force.
    MakeProcessDpiAware();

    sInstance = GetModuleHandleA(NULL);

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = sInstance;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = kClassName;

    // The window's own icons, which are NOT the same thing as the executable's.
    //
    // The shell finds the taskbar and alt-tab icon by reading the lowest-numbered
    // icon resource out of the .exe, so that one works whether or not anything
    // here is set. The title bar does not: it draws the small icon of the WINDOW
    // CLASS, and a class that leaves hIcon and hIconSm null gets the system
    // default. That is the whole difference between "the taskbar icon is right
    // and the title bar's is generic" and both being right.
    //
    // Resource 1 is the icon in res/bfbb.rc. LoadImage is asked for the two
    // system metric sizes rather than being left to guess, because the .ico
    // carries 16, 32, 48 and 64 pixel images and this is what picks the right
    // one for each slot instead of scaling a large one down.
    wc.hIcon = (HICON)LoadImageA(sInstance, MAKEINTRESOURCEA(1), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                                 LR_DEFAULTCOLOR);
    wc.hIconSm = (HICON)LoadImageA(sInstance, MAKEINTRESOURCEA(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                   LR_DEFAULTCOLOR);

    if (RegisterClassExA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return FALSE;
    }

    sMode = params->mode;

    // Windowed opens at the render size, wherever Windows puts it. The other
    // two cover a monitor, so they need that monitor's rectangle -- and the
    // primary one is the only sensible choice with nothing else to go on, since
    // there is no window yet to ask which display it is nearest.
    //
    // GetMonitorInfo rather than SM_CXSCREEN: the metric answers for the
    // primary monitor only, and the rectangle is wanted as much as the size --
    // a monitor left of the primary has a negative origin, which is exactly the
    // machine this was written on.
    DWORD style = (sMode == iWINDOW_WINDOWED) ? WS_OVERLAPPEDWINDOW : WS_POPUP;

    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    int w;
    int h;

    if (sMode == iWINDOW_WINDOWED)
    {
        // The size asked for is the size of the DRAWABLE area, not of the
        // window. Getting this wrong gives a back buffer a few pixels larger
        // than the client rect and a picture that is subtly stretched -- the
        // kind of thing that reads as a bad aspect ratio rather than as a bug
        // here.
        RECT rect = { 0, 0, params->width, params->height };
        AdjustWindowRect(&rect, style, FALSE);
        w = rect.right - rect.left;
        h = rect.bottom - rect.top;
    }
    else
    {
        POINT origin = { 0, 0 };
        HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);

        MONITORINFO info;
        info.cbSize = sizeof(info);

        if (monitor != NULL && GetMonitorInfo(monitor, &info))
        {
            x = info.rcMonitor.left;
            y = info.rcMonitor.top;
            w = info.rcMonitor.right - info.rcMonitor.left;
            h = info.rcMonitor.bottom - info.rcMonitor.top;
        }
        else
        {
            x = 0;
            y = 0;
            w = GetSystemMetrics(SM_CXSCREEN);
            h = GetSystemMetrics(SM_CYSCREEN);
        }

        // A popup has no border, so the window rectangle IS the client
        // rectangle and there is nothing to adjust for.
    }

    sWindow = CreateWindowExA(0, kClassName, params->title ? params->title : "BFBB", style, x, y,
                              w, h, NULL, NULL, sInstance, NULL);
    if (sWindow == NULL)
    {
        return FALSE;
    }

    // What the window actually got, which for a popup covering a monitor is the
    // monitor and for a windowed one is what AdjustWindowRect worked back to.
    // Read rather than assumed: WM_SIZE has already been through the proc by
    // now, and this is the number every other part of the port pairs with.
    RECT client;
    if (GetClientRect(sWindow, &client) && client.right > 0 && client.bottom > 0)
    {
        sWidth = client.right;
        sHeight = client.bottom;
    }
    else
    {
        sWidth = params->width;
        sHeight = params->height;
    }

    sShouldClose = 0;

    ShowWindow(sWindow, SW_SHOW);
    UpdateWindow(sWindow);
    return TRUE;
}

void iWindowClose()
{
    if (sWindow != NULL)
    {
        DestroyWindow(sWindow);
        sWindow = NULL;
    }

    UnregisterClassA(kClassName, sInstance);

    // Hand the display back. ES_CONTINUOUS on its own clears the request
    // without asserting anything in its place.
    SetThreadExecutionState(ES_CONTINUOUS);
}

void iWindowPump()
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

// 60 Hz, which is what the GameCube's video interface gave the game, and what
// the port paces to until config.ini says otherwise.
static S32 sFrameRate = 60;
static S32 sVSync = TRUE;

// Held at file scope rather than inside iWindowPaceFrame so that a rate change
// can invalidate the deadline. See iWindowSetFrameRate.
static bool sPacerStarted = false;
static ULONGLONG sNextFrame = 0;

void iWindowSetFrameRate(S32 fps)
{
    sFrameRate = fps > 0 ? fps : 0;

    // The deadline the pacer is holding was measured against the old period.
    // Leaving it be would make the first frame after a change sleep out the
    // remains of a frame that is no longer the right length.
    sPacerStarted = false;
}

S32 iWindowGetFrameRate()
{
    return sFrameRate;
}

void iWindowSetVSync(S32 on)
{
    sVSync = on ? TRUE : FALSE;
}

S32 iWindowGetVSync()
{
    return sVSync;
}

S32 iWindowGetDisplayRefreshRate()
{
    // The monitor the window is on, not the primary one -- on a two-monitor
    // machine those differ, and the one that paces the game is the one it is
    // being displayed on. Before the window exists there is no such monitor and
    // the primary display is the only honest answer.
    DEVMODEA mode;
    ZeroMemory(&mode, sizeof(mode));
    mode.dmSize = sizeof(mode);

    const char* deviceName = NULL;
    MONITORINFOEXA info;

    if (sWindow != NULL)
    {
        HMONITOR monitor = MonitorFromWindow(sWindow, MONITOR_DEFAULTTOPRIMARY);
        ZeroMemory(&info, sizeof(info));
        info.cbSize = sizeof(info);
        if (GetMonitorInfoA(monitor, &info))
        {
            deviceName = info.szDevice;
        }
    }

    if (!EnumDisplaySettingsA(deviceName, ENUM_CURRENT_SETTINGS, &mode))
    {
        return 0;
    }

    // 0 and 1 both mean "the hardware's default rate" in a DEVMODE, which is
    // not a number of hertz and must not be used as one -- a 1 fps cap is the
    // game stopped.
    if (mode.dmDisplayFrequency <= 1)
    {
        return 0;
    }

    return (S32)mode.dmDisplayFrequency;
}

void iWindowPaceFrame()
{
    static LARGE_INTEGER frequency = { 0 };

    // No cap. The frame rate is then whatever the machine produces, bounded by
    // the display if vsync is on and by nothing at all if it is not.
    if (sFrameRate <= 0)
    {
        sPacerStarted = false;
        return;
    }

    const ULONGLONG framePeriodNs = 1000000000ULL / (ULONGLONG)sFrameRate;

    if (frequency.QuadPart == 0 && !QueryPerformanceFrequency(&frequency))
    {
        return;
    }

    LARGE_INTEGER counter;
    if (!QueryPerformanceCounter(&counter))
    {
        return;
    }

    ULONGLONG now =
        (ULONGLONG)((double)counter.QuadPart * 1e9 / (double)frequency.QuadPart);

    if (!sPacerStarted)
    {
        sNextFrame = now;
        sPacerStarted = true;
    }

    sNextFrame += framePeriodNs;

    // A frame that overran its budget must not be paid for by not sleeping for
    // the next several -- that turns one slow frame into a burst of fast ones.
    // Drop the missed deadlines and pace from now instead. Same arrangement as
    // iVSync in iSystem.cpp, which does this for the loops that have no
    // renderer to present from.
    if (sNextFrame <= now)
    {
        sNextFrame = now;
        return;
    }

    ULONGLONG remaining = sNextFrame - now;

    // Sleep for all but the last millisecond, then spin. Sleep's resolution is
    // the scheduler's tick and it routinely overshoots by more than a frame at
    // this rate, which would cost a frame every time rather than pace one.
    if (remaining > 1500000ULL)
    {
        Sleep((DWORD)((remaining - 1000000ULL) / 1000000ULL));
    }

    for (;;)
    {
        if (!QueryPerformanceCounter(&counter))
        {
            return;
        }

        now = (ULONGLONG)((double)counter.QuadPart * 1e9 / (double)frequency.QuadPart);
        if (now >= sNextFrame)
        {
            return;
        }
    }
}

S32 iWindowShouldClose()
{
    return sShouldClose;
}

iWindowMode iWindowGetMode()
{
    return sMode;
}

void iWindowGetSize(S32* width, S32* height)
{
    if (width != NULL)
    {
        *width = sWidth;
    }

    if (height != NULL)
    {
        *height = sHeight;
    }
}

void* iWindowNativeHandle()
{
    return (void*)sWindow;
}

// Nothing to defer. D3D9 is handed a window that already exists, so by the time
// RwEngineOpen runs there is an HWND and iWindowNativeHandle answers with it.
// Both are here rather than left undefined so that every implementation of this
// header implements all of it -- a linker error is a poor way to learn that a
// window backend and a render backend were paired that do not go together.
const iWindowDeferred* iWindowDeferredParams()
{
    return NULL;
}

void iWindowDeferredCreated()
{
}

const char* iWindowBackendName()
{
    return "win32";
}
