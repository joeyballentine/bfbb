#ifndef IPADGLYPH_H
#define IPADGLYPH_H

#include <types.h>

// PC-only: drawing button prompts with the player's controller's buttons
// rather than the disc's.
//
// There is no GameCube counterpart. A console ships one controller, so the
// glyph for "the button that jumps" is a texture in font.HIP and nothing ever
// has to choose. A host has whatever is plugged in, and config.ini can rebind
// any of it, so the glyph has to be chosen at draw time.
//
// **What retail does.** Every button prompt in the game is xtextbox markup.
// Sixteen TEXT assets named button_picture_01..16 each hold one line --
// `{tex:pad_button1;scale=size;dst=...}` -- and everything that wants a prompt
// includes one of those by name: button_jump_text is `{i:button_picture_01}`.
// Twelve 32x32 textures in font.HIP are the glyphs. Nothing draws a button any
// other way: no models, no HUD sprites, no font pages.
//
// So xFont.cpp's `{tex:...}` handler is the only place a prompt resolves, and
// one lookup in front of it retargets every prompt in the game.
//
// **The glyph follows the BINDING, not the console.** A set could be twelve
// files named after the retail textures, swapped as a unit. That is wrong the
// moment anyone edits [pad] in config.ini: the game would keep drawing the
// console's button while a different one presses it. Instead a set is named
// for the host's inputs -- a.png, lt.png -- and the chain is
//
//     {tex:pad_button4}  ->  XPAD_BUTTON_TRIANGLE   (the slot table below)
//                        ->  "x"                    (whatever [pad] binds it to)
//                        ->  buttons/<set>/x.png
//
// **The slot table is the Xbox's, because the port runs the Xbox assets.** The
// three discs number the slots differently -- Xbox's button_picture_02 is the
// GameCube's and the PS2's 03 -- so a slot means nothing until you say which
// disc it came off. tools/padglyphs.py resolves that when it extracts a set,
// which is why every set here is already in the port's terms and this file
// needs only one table.
//
// **A chord has no glyph.** `l2 = lt+rb` is two inputs and one picture, so a
// binding that is not a single input falls back to the set's own art for the
// slot -- what that console drew for its own L2. Retail did the same thing:
// the GameCube has no L2 and its font.HIP holds a made-up "L2" pill.

// Off draws exactly what the disc holds. Pushed from iSystem.cpp's ApplyConfig
// the way the render features are.
void iPadGlyphSetEnabled(S32 on);
S32 iPadGlyphEnabled();

// Which set to draw, by folder name under `buttons/` -- "xbox", "gamecube",
// "ps2", or anything else someone drops in beside them. "auto" asks the input
// backend what is plugged in and picks from that, rechecking when the pad
// changes. An unreadable set reports itself once and leaves the disc's own
// glyphs alone, which is also what a set with a missing file does for that one
// glyph.
void iPadGlyphSetChoice(const char* name);

// The directory holding the `buttons/` folder -- beside the executable, the
// way config.ini's fallback works. Set before the first lookup.
void iPadGlyphSetRoot(const char* dir);

// The replacement for one retail texture name, or NULL to leave it alone.
// `name` need not be terminated; `nameLen` is its length. Called from
// xFont.cpp for every `{tex:...}` in the game, so a name that is not one of
// the twelve costs one comparison against a short table and nothing else.
//
// The returned texture belongs to this module and outlives the caller's use of
// it. Text is re-parsed every frame it is drawn, so a controller swapped
// mid-game changes the prompts on the next frame.
struct RwTexture* iPadGlyphFor(const char* name, U32 nameLen);

// Drops every loaded glyph. For the shutdown path, and for the tests.
void iPadGlyphExit();

// The set that is actually being drawn, for the startup log, or NULL when none
// is. Not always the name passed to iPadGlyphSetChoice: "auto" resolves here.
const char* iPadGlyphActiveSet();

#endif
