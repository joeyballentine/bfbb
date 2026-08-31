#ifndef XDEBUG_H
#define XDEBUG_H

#include "xFont.h"

#include <types.h>

extern U32 gFrameCount;

#ifdef PLATFORM_PC
// Seconds of game time, incremented alongside gFrameCount by the frame's dt.
//
// gFrameCount is a frame COUNTER, and retail has consumers that read it as a
// clock -- zSurface.cpp's mode 1 UV animation is `gFrameCount * (1/60)`, which
// is a time in seconds only while a frame is a sixtieth of a second. This is
// that time, measured rather than counted, so those consumers keep meaning what
// they meant at any frame rate.
//
// Not a substitute for gFrameCount everywhere: the consumers that compare it
// for EQUALITY -- xFXAura stamps ap->frame and the render draws only the auras
// stamped this frame -- want the counter and are correct with it, because the
// port runs one update per presented frame.
// F64: at a few thousand frames a second the per-frame addend is ~3e-4, and an
// F32 accumulator stops advancing entirely once it passes 8192 because the
// addend falls below half an ulp. That is a couple of hours of uncapped play,
// after which every consumer of this clock freezes.
extern F64 gGameSeconds;
#endif

struct uint_data
{
    U32 value_def;
    U32 value_min;
    U32 value_max;
};

struct float_data
{
    F32 value_def;
    F32 value_min;
    F32 value_max;
};

struct bool_data
{
    U8 value_def;
};

struct select_data
{
    U32 value_def;
    U32 labels_size;
    char** labels;
    void* values;
};

struct flag_data
{
    U32 value_def;
    U32 mask;
};

struct raw_data
{
    U8 pad[16];
};

struct int_data
{
    S32 value_def;
    S32 value_min;
    S32 value_max;
};

struct tweak_callback;
struct tweak_info
{
    substr name;
    void* value;
    tweak_callback* cb;
    void* context;
    U8 type;
    U8 value_size;
    U16 flags;
    union
    {
        int_data int_context;
        uint_data uint_context;
        float_data float_context;
        bool_data bool_context;
        select_data select_context;
        flag_data flag_context;
        raw_data all_context;
    };
};

struct tweak_callback
{
    void (*on_change)(tweak_info&);
    void (*on_select)(tweak_info&);
    void (*on_unselect)(tweak_info&);
    void (*on_start_edit)(tweak_info&);
    void (*on_stop_edit)(tweak_info&);
    void (*on_expand)(tweak_info&);
    void (*on_collapse)(tweak_info&);
    void (*on_update)(tweak_info&);
    void (*convert_mem_to_tweak)(tweak_info&, void*);
    void (*convert_tweak_to_mem)(tweak_info&, void*);

    static tweak_callback create_change(void (*)(const tweak_info&));
};

void xprintf(const char* msg, ...);
S32 xDebugModeAdd(const char* mode, void(*debugFunc)());
void xDebugInit();
void xDebugUpdate();
void xDebugExit();
void xDebugTimestampScreen();

inline void xDebugRemoveTweak(const char*)
{
}

void xDebugUpdate();

inline void xDebugAddTweak(const char*, F32*, F32, F32, const tweak_callback*, void*, U32)
{
}

inline void xDebugAddTweak(const char*, S16*, S16, S16, const tweak_callback*, void*, U32)
{
}

inline void xDebugAddTweak(const char*, U8*, U8, U8, const tweak_callback*, void*, U32)
{
}

inline void xDebugAddTweak(const char*, const char*, const tweak_callback*, void*, U32)
{
}


inline void xDebugAddFlagTweak(const char*, U32*, U32, const tweak_callback*, void*, U32)
{
}

inline void xDebugAddSelectTweak(const char*, U32*, const char**, const U32*, U32, const tweak_callback*,
                          void*, U32)
{
}

#endif
