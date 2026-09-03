#ifndef IBOOT_H
#define IBOOT_H

#include <types.h>

// PC-only: config.ini's [game] section -- which scene the game starts in, and
// whether the logo movies play on the way to the title screen.
//
// Retail already has the first switch. `SB.INI` holds `BOOT=` and
// `ShowMenuOnBoot=`, `zMainReadINI` reads them, and together they start the
// game in a named scene with no input. The port keeps that and does not change
// it. What it cannot do is answer per instance: SB.INI lives in the assets
// folder, and every copy of the game running on the machine reads the same one,
// so two instances cannot boot into two different levels and setting the switch
// at all means editing the player's own game files and remembering to put them
// back. config.ini is chosen per instance -- BFBB_CONFIG names the file -- so
// the same switch belongs here as well.
//
// SB.INI is read first and a [game] boot= that is set wins over it. An empty
// one leaves SB.INI to decide, so a config.ini that does not mention the
// section behaves exactly as the port did before this existed.

// The scene to start in: four characters, "soak", or "" for whatever SB.INI
// says. Already validated -- a value that is neither four characters nor
// "soak" was reported when it was set and reads back as "".
const char* iBootScene();

// Whether the logos play. TRUE unless config.ini says otherwise. They are
// skippable on the console with a button, but only once each has started, so a
// port launched a hundred times an evening spends most of that on three logos.
S32 iBootIntroMovies();

// What to multiply the camera's two stick scales by, config.ini's
// input.camera_sensitivity. 1.0 unless something says otherwise.
//
// Here rather than in an input module because the thing it scales is game code:
// zcam_pad_pyaw_scale and zcam_pad_pitch_scale are zCamera's, SB.INI already
// sets them, and zMainReadINI is where both files meet. The yaw scale is a turn
// rate; the pitch one is a blend weight that clamps at 1, so raising it reaches
// the same extremes with less stick rather than pitching further.
F32 iBootCameraSensitivity();

// Set by iSystem from config.ini, before zMainReadINI runs.
void iBootSetScene(const char* scene);
void iBootSetIntroMovies(S32 play);
void iBootSetCameraSensitivity(F32 scale);

#endif
