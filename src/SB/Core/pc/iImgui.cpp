#include "iImgui.h"

#include <types.h>

#include "iWindow.h"
#ifdef ENABLE_IMGUI
#include "imgui.h"
#include "imgui_impl_rw.h"
#include "imgui_impl_win32.h"
#endif

static bool sShowDemo = true;

void iImguiInit()
{
#ifdef ENABLE_IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(iWindowNativeHandle());
    ImGui_ImplRW_Init();
#endif
}

void iImguiNewFrame()
{
#ifdef ENABLE_IMGUI
    ImGui_ImplWin32_NewFrame();
    ImGui_ImplRW_NewFrame();
    ImGui::NewFrame();

    if (sShowDemo)
    {
        ImGui::ShowDemoWindow(&sShowDemo);
    }
#endif
}

void iImguiRender()
{
#ifdef ENABLE_IMGUI
    ImGui::Render();
    ImGui_ImplRW_RenderDrawLists(ImGui::GetDrawData());
#endif
}

bool iImguiWantCapture()
{
#ifdef ENABLE_IMGUI
    ImGuiIO& io = ImGui::GetIO();
    // Ignore io.WantCaptureMouse because we don't want to prevent controlling the game unless the user has actually
    // clicked into an imgui window.
    return io.WantCaptureKeyboard;
#else
    return false;
#endif
}

void iImguiShutdown()
{
#ifdef ENABLE_IMGUI
    ImGui_ImplWin32_Shutdown();
    ImGui_ImplRW_Shutdown();
    ImGui::DestroyContext();
#endif
}
