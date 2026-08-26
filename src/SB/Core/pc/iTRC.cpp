#include "iTRC.h"

#include <types.h>

// Nintendo's TRC rules govern what a GameCube game must do when the disc is
// unreadable, the tray is opened, or Reset is pressed: put up a specific
// message, suspend sound and video, and refuse to continue until the fault
// clears. iTRC.cpp on the GameCube is 534 lines because it has to draw that
// screen itself, with the console's ROM font, after the renderer is gone.
//
// A PC port is under none of those obligations and has none of those faults --
// there is no disc, no tray, and no reset button. What remains is the shape of
// the interface, because game code calls into it from its load loops.

static bool sResetEnabled;

void iTRCDisk::Init()
{
    sResetEnabled = true;
}

// Retail polls the drive, shows the error screen if the disc is gone, and
// returns true when it showed one and the game is resuming -- which is why
// zGame.cpp calls zMusicNotify(7) on a true return, to restart the music the
// error screen silenced. Assets on a host filesystem do not go away between
// one frame and the next, so this reports "nothing happened" and the music is
// never interrupted in the first place.
bool iTRCDisk::CheckDVDAndResetState()
{
    return false;
}

// The console's Reset button, which the TRC rules say must be ignored during a
// save. The flag is kept because the game sets and clears it around its writes
// and a later backend may want to honour it -- a window close request is the
// nearest host equivalent, and it deserves the same protection.
void ResetButton::EnableReset()
{
    sResetEnabled = true;
}

void ResetButton::DisableReset()
{
    sResetEnabled = false;
}
