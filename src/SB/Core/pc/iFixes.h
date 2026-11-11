#ifndef IFIXES_H
#define IFIXES_H

#include <rwplcore.h>
#include <types.h>

// PC-only: the switches for the bugs in the original game that the port fixes
// rather than copies. There is no GameCube counterpart -- the console has the
// bugs -- so shared code reaches these through src/SB/Core/x/xFixes.h, which
// preprocesses away entirely there.
//
// Every one of them defaults ON. They are fixes; a player who wants the console
// picture exactly as it shipped turns them off one at a time under `[fixes]`.
// The switch exists because "what the game did" is a legitimate thing to want
// to see, not because the fix is in doubt.
//
// Not every fixed bug is in here. The GameCube's flipped 3D sound panning is
// corrected in the sound layer and has no switch: it is a defect with no look
// to preserve, and nobody wants to hear a level backwards on purpose.

// **The pause menu's bamboo frame has no rope at its corners.**
//
// The rope lashing is painted on the ends of the horizontal rails. The vertical
// stiles sit on the nearer of the frame's two depth planes AND are drawn
// second, so at each corner they cover the rail's end cap -- and the end cap is
// the rope. It is not missing art, and it has nothing to do with the render
// size: the rope was invisible at 640x480 as well.
//
// iMenuFrame.cpp rebuilds the mesh anyway, to widen the frame for a screen that
// is not 4:3, so the fix costs nothing there: it hands each group the other's
// depth plane and lays the stiles down first. Off restores the original order
// and depths, and on a 4:3 or pillarboxed screen -- where there is no widening
// to do either -- leaves the mesh alone entirely.
S32 iFixMenuRope();
void iFixSetMenuRope(S32 on);

// **GL03's sky is clipped away whole by the camera's far plane.**
//
// A level with fog has its far clip plane at the fog stop, because RenderWare's
// linear fog runs from the fog plane to the far plane and the two have to agree
// -- `iCameraSetFogRenderStates` does it, and it is retail code. Goo Lagoon's
// pier sets a fog stop of 285 and scales its skydome to 2.30, which puts every
// one of the dome's 76 vertices between 380 and 406 units from the camera. Not
// one of them is inside the frustum, so the entire sky is thrown away and what
// is left is the clear colour -- which comes from the same fog asset, and is
// the flat teal the level shows instead of a sky.
//
// GL03 is the only level in the game where this happens. The other 51 sky
// entities all sit inside their own level's fog stop, most of them by a wide
// margin.
//
// **The fix is for GL03 alone.** Mods use a fog stop short of the dome on
// purpose: BFBB Deluxe's GY07 stops its fog at 100 so that the clear colour,
// orange, is the sky. Refitting that dome would draw the Graveyard's sky in
// its place, which the GameCube never shows.
//
// **The fix shrinks the dome, not the frustum.** Pushing the far plane out
// would stretch the fog gradient with it and thin the fog the level was
// authored with -- the same argument iDrawDist.h makes for leaving fogged
// levels alone -- and under librw the projection is built once per
// `RwCameraBeginUpdate`, so a mid-frame change would not reach the current
// frame anyway.
//
// Scaling the dome radially about the CAMERA is exact. Every vertex keeps its
// direction from the eye, so it lands on the same pixel; view-space z scales
// uniformly, so perspective-correct interpolation gives the same texture
// coordinates. The sky pass draws with depth test, depth write and fog all off,
// so the only thing the new distance changes is that the geometry now survives
// the clip. The dome's normals come out scaled by the reciprocal, which is
// harmless: `zSceneRenderPreFX` calls `xLightKit_Enable(NULL, ...)` before the
// sky, so no directional, point or spot light is in the world while it draws,
// and the ambient term the shader is left with does not read a normal.
S32 iFixSkyClip();
void iFixSetSkyClip(S32 on);

// Shrink `mat` about `eye` until the model it places is inside the current
// camera's far clip plane, and answer TRUE if it did, having put the matrix it
// replaced in `saved` for the caller to hand back after the draw. FALSE means
// `mat` was not touched and `saved` holds nothing -- the fix is off, `sceneID`
// is not GL03, there is no camera, or the model already fits.
//
// `world` is the model's world bounding sphere, which `iModelCull` fills in on
// the way past, so the caller has one already.
S32 iFixSkyDomeToFarPlane(U32 sceneID, RwMatrix* mat, const RwV3d* eye, const RwSphere* world,
                          RwMatrix* saved);

// **A melee hit reaches its target once per frame of contact, not once.**
//
// CheckObjectAgainstMeleeBound (zEntPlayer.cpp) sends eEventHit_Melee on every
// update in which the attack's bound overlaps the target. SpongeBob's bubble
// spin, Patrick's and Sandy's melee and the slide all run it once a frame
// while they are live, and none of them remembers what it already hit. The
// function is 100% matched, and the Xbox's xSFXEventCB, xSndPlayInternal and
// voice allocator have no duplicate check either: the console does this too.
//
// 27 entities in the retail data link the event, and the Goo Lagoon beach
// umbrellas are 18 of them. Each one's hit plays SFX 3f533410 or 3f533411,
// Umbrella_hit1, a 2.1 s sample, and every update of contact starts another
// copy of it one frame behind the last: 3 from a spin beside the umbrella, 9
// from a spin at its pole, up to 16 from Patrick's swing. The copies
// comb-filter one another into a lower, buzzing pitch. Above 60 fps there are
// more of them, closer together. The other nine are 8 HB02 platforms that
// send Run, and one HB03 object that turns its collision off and on and plays
// a sound.
//
// The fix sends the event on the first update of a contact only. A contact
// ends when the target goes kMeleeContactGap without a hit. Off: one event per
// update of contact, as the game shipped.
S32 iFixMeleeOnce();
void iFixSetMeleeOnce(S32 on);

// Whether a melee hit on `ent` at game time `now` starts a new contact, and so
// should send eEventHit_Melee. Always TRUE with the fix off.
S32 iFixMeleeNewContact(const void* ent, F64 now);

// **The pause menu's task spatulas spin out of step with one another.**
//
// The level browser's task row is eight gold and eight silver spatula models,
// shared by every world's page. zUI_ScenePortalUpdate hides all sixteen and
// shows the ones the current world has earned, every frame. zUI_Update, which
// is 100% matched, advances a UI model's animation only while it is visible,
// so each spatula stops turning on any page that does not show it. Back on a
// page that does, it resumes from where it stopped. A few page turns between
// worlds with different tasks done leave every spatula at its own angle. The
// gold and silver models share one animation (ATBL 85b5055b and 8b3dab0f both
// play raw anim cfcf8553), so they drift against each other too.
//
// The fix gives all sixteen the time of a visible one every frame, in
// iMenuSpin.cpp. Off: each spatula keeps its own time, as the game shipped.
S32 iFixSpatulaSpin();
void iFixSetSpatulaSpin(S32 on);

#endif
