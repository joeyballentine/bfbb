#ifndef ITOON_H
#define ITOON_H

#include <types.h>

#include <rwcore.h>

// The colour strip a cel-shaded surface looks its lighting up in.
//
// **Band colours, not just band count.** Arithmetic banding gives every step
// the same hue and varies only how much of it there is, so a character's shadow
// side is the lit side turned down. A drawing does not do that: its shadows
// shift towards blue while the lit side stays warm, and that shift is most of
// what makes it read as ink rather than as a dimmer.
//
// The strip carries the count, the widths and the colours together, so retuning
// the look is a matter of what is written into it rather than of the shader
// that reads it. This one is generated rather than authored -- there is nowhere
// in the game's assets to put a new texture -- but it is generated once, at
// startup, and everything downstream treats it as art.
//
// Safe to call when the renderer has no toon support: it does nothing.
void iToonInit(S32 bands);
void iToonShutdown();

// Which models are outlined, and with how many inks.
//
// **The game has to say; the renderer cannot work it out.** Nothing about an
// atomic says whether it is a character, and nothing at all says which
// character -- only one of them wears trousers.
//
// **And it has to be said about the MODEL, not about a moment.** Setting a mode
// around `ent->render(ent)` looks right and works for exactly one character:
// the player, who happens to be drawn on his own. Everybody else renders inside
// xModelBucket's window, where `render` only queues the model and the draw
// happens later in xModelBucket_RenderOpaque -- by which time the mode has been
// cleared and every NPC comes out with no line around it.
//
// So a mode is attached to the model itself and read back when it is finally
// drawn, which is what the GameCube version's PipeFlags bit did.
//
// Registered per frame: Clear at the top of the scene, Register each character
// as it is walked, and the lookup holds until the next Clear -- comfortably
// past the bucket flush.
// Which of the stacked ramps a draw is shaded with.
//
// **A ramp is a material, not a brightness.** Skin bands softly and warm, sheet
// metal bands hard and cold, and a painted background wants neither -- these are
// different looks and not one look at different strengths, which is why they are
// separate strips of colour rather than a parameter on one.
//
// iToon.cpp holds the tuning of each. header.frag hardcodes the count.
#define ITOON_RAMP_CHARACTER 0
#define ITOON_RAMP_METAL 1
#define ITOON_RAMP_WORLD 2
#define ITOON_RAMP_ROWS 4

void iToonSetRampRow(S32 row);

// Which row this atomic should be drawn with, from what its materials are.
//
// A reflective material is the only thing in these models that says "metal"
// without the game having to be told, and it is a reliable one: the robots
// reflect and the characters do not. Worked out once per geometry and
// remembered, like the weld.
S32 iToonRampRowFor(void* atomic);

// Push the floor under the outline's width and the ceiling over it, from the
// camera about to draw and the size of the picture. Both change, so these are
// per draw rather than per level.
//
// Both are in pixels while the width itself is in world units, which is what
// keeps a drawn line reading as a drawn line: it holds its weight on screen
// instead of swelling as the camera closes and vanishing as it pulls back.
void iToonOutlineMinWidth();
void iToonOutlineMaxWidth();

#define ITOON_OUTLINE_NONE 0
#define ITOON_OUTLINE_PLAIN 1
#define ITOON_OUTLINE_TWOTONE 2

struct xModelInstance;

void iToonOutlineClear();
void iToonOutlineRegister(xModelInstance* model, S32 mode);

// What was registered for this atomic, or ITOON_OUTLINE_NONE. Called by
// iModelRender around the draw.
S32 iToonOutlineFind(void* atomic);
void iToonSetOutline(S32 mode);

// Light a character from his own front rather than from the room.
//
// **This is the shading the show uses, and it is not lighting.** SpongeBob's
// front is flat yellow and his side a solid darker green, and that holds
// whichever way he turns and wherever the sun is -- the two tones describe his
// SHAPE. A world light cannot do it: turn him around and the dark side swings
// with the room. So a character is lit down his own forward axis, which puts
// his face in the top band of the ramp and his sides in the bottom one, always.
//
// `forward` is the model's own facing. Cleared after the draw.
// Average an atomic's normals across vertices that share a position, once.
//
// A hard edge is stored as a duplicated vertex, one per face, each with its own
// normal -- and inflating along those sends the copies apart and opens the
// outline at every corner. Averaging closes it, and smooths the shading, which
// a cel ramp wants anyway.
// Returns the object-space height where the model's two inks meet, measured
// from its bind pose the first time it is seen and remembered after.
F32 iToonWeld(void* atomic);

// The colour the level's own lighting paints the room, so a character standing
// in it is painted the same.
//
// A level lights its world and its objects with separate rigs, and Rock Bottom
// shows why that matters here: the room gets a blue kit and the characters a
// grey one. That is right for a renderer drawing characters as objects and
// wrong for a cartoon, which paints everything in a scene from one palette.
//
// Only the hue survives -- the value is normalized to its brightest channel --
// because what is wanted is the room's colour and not its brightness, which the
// ramp is already deciding.
void iToonSetRoomTint(const F32* rgb);
void iToonRoomTintApply();
void iToonRoomTintClear();

void iToonOutlineSplit(F32 y, S32 mode);
void iToonFaceLight(const RwMatrixTag* mat);
void iToonFaceLightClear();

#endif
