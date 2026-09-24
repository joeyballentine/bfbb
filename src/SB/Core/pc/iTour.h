#ifndef ITOUR_H
#define ITOUR_H

#include <types.h>

// PC-only. Runs the game from a script, in a hidden window, and writes PNGs of
// its own frames: a way to look at the menus without a person at the machine.
//
//   BFBB_TOUR=path/to/script.txt  the script; tour mode is on when this is set
//   BFBB_TOUR_OUT=dir             where the PNGs go; the script's folder if unset
//
// Port 0 reads the script and nothing else; real controllers and the keyboard
// are ignored. The window is created hidden and stays windowed, so the render
// size is video.width x video.height. The frame rate is held at 60 so that
// counts below are seconds / 60.
//
// One command a line; `#` starts a comment. Counts are in presented frames.
//
//   wait N              do nothing for N frames
//   press B [B...]      hold the buttons 4 frames, then release for 4
//   hold N B [B...]     hold the buttons N frames, then release for 4
//   shot NAME           write NAME.png, at most 1280 wide
//   quit                exit the process
//
// Buttons are the [pad] names from config.ini: a b x y z hud l1 r1 l2 r2 start
// select up down left right. The process exits when the script ends.
void iTourInit();

// Whether BFBB_TOUR is set. iWindowSDL.cpp reads the variable itself, since
// the shim does not link the platform layer.
S32 iTourActive();

#endif
