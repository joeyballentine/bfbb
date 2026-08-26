#ifndef ITRC_H
#define ITRC_H

#include <types.h>

#include "xPad.h"

// The GameCube header also declares ROMFont, which draws the disc-error screen
// with the console's built-in font through GX, and the six iTRCDisk hook
// setters that route sound and movie suspension into that screen. Nothing
// outside src/SB/Core/gc references any of it -- game code reaches TRC only
// through xTRC.cpp and the three entry points below -- so the host build does
// not carry the GX font renderer.

struct _tagiTRCPadInfo
{
    _tagPadInit pad_init;
};

// Nintendo's Technical Requirements Checklist: the certification rules for
// what a GameCube game must do when the disc is removed, the controller is
// unplugged, or Reset is pressed. A PC port has no such obligations, but the
// game calls into these from its load loops and its pad handling, so they stay
// -- doing the part that is still true on a host.
namespace iTRCDisk
{
    bool CheckDVDAndResetState();
    void Init();
} // namespace iTRCDisk

namespace ResetButton
{
    void EnableReset();
    void DisableReset();
} // namespace ResetButton

#endif
