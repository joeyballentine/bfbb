#ifndef ZHUD_H
#define ZHUD_H

#include <types.h>
#include "xString.h"
#include "xHudMeter.h"
#include "xHudModel.h"
#include "xHud.h"

// screen_bounds is defined `static const` privately in xHud.cpp, zGame.cpp,
// zEntCruiseBubble.cpp and zEntPlayerBungeeState.cpp -- four separate copies,
// none of them this one. Nothing ever linked against an external definition.

struct special_data {
    char* hud_model;
    S32 max_value;
};

namespace zhud
{
    void render();
    void update(F32 dt);
    void show();
    void hide();
    void init();
    void destroy();
    void setup();
} // namespace zhud

#endif
