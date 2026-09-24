#ifndef ISAVESCREEN_H
#define ISAVESCREEN_H

// The PC save and load screens' widgets, as the menu data builds them and
// iSaveScreen.cpp drives them. One list of every save, newest first, shown a
// few rows at a time.

#define ISAVESCREEN_ROWS 5

// The rows' names, "%d" the row. The text is shared: the two screens are never
// up at once.
#define ISAVESCREEN_LOAD_ROW "PC LD ROW %d"
#define ISAVESCREEN_SAVE_ROW "PC SV ROW %d"
#define ISAVESCREEN_ROW_TEXT "PC SAVE ROW %d TXT"

// Room for a row's two lines.
#define ISAVESCREEN_ROW_TEXT_SIZE 160

// Where the rows sit, in the 640x480 the menus are laid out in. Left of the
// thumbnail, which is at (430, 180) on both screens.
#define ISAVESCREEN_ROW_X 60.0f
#define ISAVESCREEN_ROW_Y 118.0f
#define ISAVESCREEN_ROW_STEP 48.0f
#define ISAVESCREEN_ROW_W 360
#define ISAVESCREEN_ROW_H 46

#endif
