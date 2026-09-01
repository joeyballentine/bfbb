#ifndef IPADLAYOUT_H
#define IPADLAYOUT_H

#include <types.h>

// PC-only: which XPAD bit each of the player's moves reads, per console.
//
// **The one thing the three releases really disagree about.** Their pad layers
// are identical -- every disc's own menu prompts show A on XPAD_BUTTON_X, B on
// TRIANGLE, X on O and Y on SQUARE, which is gc/iPad.cpp's mapping -- and their
// UI link data is identical too, byte for byte: a menu cancels on TRIANGLE and
// opens options on O wherever you play it. What each release changed is which
// bit zEntPlayer reads for a MOVE:
//
//     move     GameCube    Xbox        PS2
//     jump     X           X           X
//     spin     TRIANGLE    O           SQUARE
//     bash     SQUARE      SQUARE      TRIANGLE
//     bounce   O           TRIANGLE    O          (also pick up and throw)
//
// So the Xbox is the GameCube with TRIANGLE and O transposed, and the PS2 is
// the GameCube with TRIANGLE and SQUARE transposed. Both are transpositions, so
// each is its own inverse.
//
// **Where this comes from.** Each disc's prompt for a move names a
// button_picture_NN, that picture names a pad_buttonN texture, and that texture
// is a picture of a physical button. Reading the chain end to end gives the
// physical button each release put each move on; the pad layer above turns that
// into a bit. Doing it for the GameCube reproduces zEntPlayer.cpp exactly, line
// for line, which is the check that the method is sound.
//
// **Why the menus are not in the table.** They do not move. All three discs
// cancel on the button printed B and open options on the one printed X, so the
// bits the code already reads are right on every preset and nothing has to be
// remapped for a menu. That is also why this is applied at the player's reads
// rather than to the pad: doing it to the pad would drag the menus along.
//
// A move and a menu action still share a bit -- Spin and cancel are both
// TRIANGLE on the GameCube -- and that is not a fault to fix. The GameCube
// release shipped that way and its own prompts say so.

// The bit to read in place of `mask`, under the preset in force. Only the four
// face bits move; everything else is returned unchanged.
//
// Cheap enough for a per-frame read: two comparisons and a table lookup no
// deeper than the preset list.
U32 iPadLayoutButton(U32 mask);

// Which glyph FILE a set draws for a face bit, named by the position the
// button is printed at -- "east" on the Xbox set for TRIANGLE, "west" on the
// GameCube's, "north" on the PS2's. NULL for a bit that is not one of the
// four, and for a set nobody has a table for: a folder someone drew
// themselves is named after positions already and is its own answer.
//
// What a prompt falls back on when there is no binding to draw from --
// keyboard play, a pad asleep, a face button the player has chorded. Without
// it every set would fall back to the Xbox's arrangement, and a GameCube set
// would draw its X beside "Return to Game".
const char* iPadLayoutFaceGlyph(const char* set, U32 mask);

#endif
