// Dear ImGui subsystem
//
// Only available on PC, these interfaces can only be called by other i-code

#ifndef IIMGUI_H
#define IIMGUI_H

#include <types.h>

void iImguiInit();
void iImguiNewFrame();
void iImguiRender();
void iImguiShutdown();

// True while an ImGui window is focused
bool iImguiWantCapture();

#endif
