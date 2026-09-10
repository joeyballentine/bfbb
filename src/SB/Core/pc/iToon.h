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
#define ITOON_RAMP_PROP 3
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
//
// They take the matrix the model is about to be drawn with because the hull is
// pushed in the model's own units: a HUD model is sheared down to about a tenth
// and needs ten times the width to draw the same line. iToon.cpp says more.
void iToonOutlineMinWidth(const RwMatrix* mat);
void iToonOutlineMaxWidth(const RwMatrix* mat);

// **The cel ramp is global state, so "plain" has to be said out loud.**
//
// video.toon shades every model the renderer draws; the registry only ever
// decided who also gets ink. A floating sign -- the number over a clam, the
// icon over a task gate -- is art with no shape to describe, and a model drawn
// with no light kit at all lands in the ramp's darkest band, which is why one
// comes out as a dark shape inside its own bright outline.
//
// NONE means nobody said anything, so experimental.toon_all still reaches it.
// PLAINDRAW means somebody did: iModelRender turns the shading off around that
// model and back on afterwards. It is negative so that the NONE test that
// gates the ink still reads the way it did.
//
// SHADEONLY is the other half of that: a character the game names, shaded like
// one, with the hull left off. A model drawn as a transparent shell cannot be
// inked by an inverted hull, because the hull is a solid copy sitting behind it
// and the model never paints over the middle of it -- the whole silhouette
// fills with ink and the scene stops showing through. Bubble Buddy is the one
// that matters.
#define ITOON_OUTLINE_SHADEONLY (-2)
#define ITOON_OUTLINE_PLAINDRAW (-1)
#define ITOON_OUTLINE_NONE 0
#define ITOON_OUTLINE_PLAIN 1
#define ITOON_OUTLINE_TWOTONE 2

struct xModelInstance;

void iToonOutlineClear();
void iToonOutlineRegister(xModelInstance* model, S32 mode);

// What a model nobody registered is drawn as.
//
// ITOON_OUTLINE_NONE is the show's answer: a crate, a platform and a spatula
// are things in the world rather than people in a cartoon, and inking them
// makes the picture read as a diagram. ITOON_OUTLINE_PLAIN inks everything the
// game draws as a model instead, which is what experimental.toon_all asks for.
//
// Either way the level itself is untouched, and not by choice of this file:
// iEnv.cpp draws the world through RpAtomicRender and never reaches the
// registry at all.
void iToonSetOutlineDefault(S32 mode);

// Stop and restart the whole look, for a pass that draws see-through art
// rather than solid surfaces.
//
// The alpha half of a frame -- floor decals, plant cards, particles, the
// numbers that float off a clam -- is flat art laid on the picture. Banding its
// light only darkens it, and a hull round an alpha card traces the rectangle
// and not the shape. iToon.cpp says the whole of it.
void iToonPause(S32 on);

// Draw this model with no cel ramp, whenever it is finally drawn. For art that
// goes through the model path but is not a surface; see ITOON_OUTLINE_PLAINDRAW.
void iToonPlainRegister(xModelInstance* model);

// How bright the room this draw is in, as a scale on the colour the lights
// resolve to. The world's draw sets it and clears it again; iScreen.h says why
// the lights cannot answer it alone.
void iToonSetRoomLevel(F32 scale);

// How much of the shade the scene traced from its placed models this draw takes.
// The world's draw sets it and clears it again; see iScreenWorldModelShade.
void iToonSetModelShade(F32 amount);

// Switch the ramp off and on around one draw. iModelRender's, not a caller's.
void iToonSuppress(S32 on);

// Hold this model's ink under a third of its own thinnest side, so a distant
// model does not end up with a band as wide as itself. After
// iToonOutlineMaxWidth, whose ceiling it lowers, and per draw because it
// depends on where the model is. iToon.cpp says why a pixel floor needs this.
void iToonOutlineThinCap(void* atomic, const RwMatrix* mat);

// Whether an atomic was named rather than reaching the look through
// experimental.toon_all, and which atomic the next iToonSetOutline is about.
S32 iToonOutlineNamed(void* atomic);

// Give this model's hull a normal of its own, once, by rebuilding its geometry
// with the welded normal in two texture coordinate sets beside the ones the
// artists drew with. Everything downstream reads the new geometry, so this runs
// before the rest. iToon.cpp says why the hull cannot share the surface's.
void iToonHullNormals(void* atomic);

// Lift one text colour into the cel look, in place.
//
// The game's text is painted for a photographic scene and the cel look is not
// one: every surface around it has had its colour pushed away from grey and its
// light cut into bands, and the text keeps the muted colour it was authored
// with. It reads as dull rather than as restrained. Brightness scales it and
// saturation pushes it off its own grey; both leave white alone, so a white
// caption is untouched and a coloured one comes up to meet the scene.
//
// Does nothing with the cel look off, or with both left at 1.
void iToonTextColor(U8* r, U8* g, U8* b);
void iToonOutlineAtomic(void* atomic);

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

// Bracket the goo's own surface draw: the cel look, the world's strip and the
// wet glint on, then back to what the pass had.
//
// Called from zFXGooRenderAtomic, which is the only place that knows a draw is
// goo. See the definition, which says why each of the three has to be asked for.
void iToonGooDraw(S32 on);

// Forget everything measured about a model's geometries, because the model is
// being unloaded and the next one loaded will sit where it did.
void iToonForgetModel(void* clump);

// Average an atomic's normals across vertices that share a position, once.
//
// A hard edge is stored as a duplicated vertex, one per face, each with its own
// normal -- and inflating along those sends the copies apart and opens the
// outline at every corner. Averaging closes it, and smooths the shading, which
// a cel ramp wants anyway.
// Returns the object-space height where the model's two inks meet, measured
// from its bind pose the first time it is seen and remembered after.
F32 iToonWeld(void* atomic);

// Whether an atomic's geometry is wound inside out: closed, with every face
// pointing into its own volume. Four of the seven gate digits are. Measured
// once, on the same walk as the weld; iToon.cpp says what the hull does about
// it.
S32 iToonInsideOut(void* atomic);

// Give this model real thickness if it is a flat open sheet, so a hull round it
// has something to be a band past. Once per geometry; the solid replaces the
// sheet on the atomic. iToon.cpp says why a sheet cannot be inked at all.
void iToonSolidify(void* atomic);

// Tell the renderer which way round the next hull goes, and put a mesh right
// if it is wound inside out by mistake rather than by intent. NULL clears it.
void iToonOutlineOrient(void* atomic);

// This model is meant to face inward: it is seen from inside, so leave its
// winding alone. Said by xSkyDome, which is the only thing that knows.
void iToonSeenFromInside(void* atomic);

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
