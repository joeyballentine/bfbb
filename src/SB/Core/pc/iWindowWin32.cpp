#include "iWindow.h"

// The Win32 window, which is what a D3D9 librw wants: its EngineOpenParams is
// { HWND window; } and nothing else.
//
// Deliberately plain. There is no GLFW or SDL here because D3D9 does not need
// one, and pulling in a windowing library to hand D3D a handle it could get
// from CreateWindowEx would be a dependency the port does not have to have.
// The GL3 build will want one -- iWindowGlfw.cpp, selected the way
// BFBB_INPUT_BACKEND and BFBB_AUDIO_BACKEND select theirs.

#include <windows.h>

static HWND sWindow;
static HINSTANCE sInstance;
static S32 sShouldClose;
static S32 sWidth;
static S32 sHeight;

static const char* const kClassName = "BFBBWindow";

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

    sInstance = GetModuleHandleA(NULL);

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = sInstance;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = kClassName;

    if (RegisterClassExA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return FALSE;
    }

    DWORD style = params->fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW;

    // The size asked for is the size of the DRAWABLE area, not of the window.
    // Getting this wrong gives a back buffer a few pixels larger than the
    // client rect and a picture that is subtly stretched -- the kind of thing
    // that reads as a bad aspect ratio rather than as a bug here.
    RECT rect = { 0, 0, params->width, params->height };
    if (!params->fullscreen)
    {
        AdjustWindowRect(&rect, style, FALSE);
    }

    sWindow = CreateWindowExA(0, kClassName, params->title ? params->title : "BFBB", style,
                              CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                              rect.bottom - rect.top, NULL, NULL, sInstance, NULL);
    if (sWindow == NULL)
    {
        return FALSE;
    }

    sWidth = params->width;
    sHeight = params->height;
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

S32 iWindowShouldClose()
{
    return sShouldClose;
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

const char* iWindowBackendName()
{
    return "win32";
}
