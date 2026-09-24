#ifndef ISETTINGSSCREEN_H
#define ISETTINGSSCREEN_H

#include <types.h>

// The in-game settings screen: config.ini's settings a player would change,
// changed from the pause menu or the title and applied as they change. The
// widgets are the menu data's (mnu4, which is loaded everywhere); the rest is
// iSettingsScreen.cpp.

#define ISETTINGS_ROWS 7

// Each row is a label and a value, "%d" the row.
#define ISETTINGS_LABEL "PC SET LABEL %d"
#define ISETTINGS_VALUE "PC SET VALUE %d"
#define ISETTINGS_LABEL_TEXT "PC SET LABEL %d TXT"
#define ISETTINGS_VALUE_TEXT "PC SET VALUE %d TXT"
#define ISETTINGS_TITLE "PC SET TITLE UIF"
#define ISETTINGS_TITLE_TEXT "PC SET TITLE TXT"
#define ISETTINGS_HELP "PC SET HELP UIF"
#define ISETTINGS_HELP_TEXT "PC SET HELP TXT"
#define ISETTINGS_GROUP "PC SETTINGS GROUP"

// The menu entries that open it.
#define ISETTINGS_TITLE_ENTRY "MNU3 START SETTINGS UIF"
#define ISETTINGS_PAUSE_ENTRY "PAUSE OPTION SETTINGS UIF"

#define ISETTINGS_ROW_Y 105.0f
#define ISETTINGS_ROW_STEP 34.0f

// Whether the entry that opens the screen was just chosen. Asked by the save
// and load loops, which the entries reach through the game's own save and
// load modes: the title's goes through Load, the pause menu's through Save.
S32 iSettingsRequested(S32 fromPause);

// Run the screen until the player backs out, then put back the menu it came
// from.
void iSettingsRun(S32 fromPause);

#endif
