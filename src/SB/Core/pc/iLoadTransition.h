#ifndef ILOADTRANSITION_H
#define ILOADTRANSITION_H

#include <types.h>

// PC-only: config.ini's video.load_transition, and what `fancy` does to the
// end of a load.
//
// The console's loading screen cuts. The bubbles are up over a still of the
// level you left, the load finishes, and the next frame is the new level.
// `fancy` replaces the cut with a wipe: the still slides off bottom to top
// over the level that just loaded, with the bubble wall rising through it, so
// the animation the loading screen was already playing is what hands over.
//
// `normal` is the cut, and what the port did before this existed.
//
// The wipe is drawn by the game loop rather than by the loading screen,
// because the thing it reveals is the level, and the level is only renderable
// once zGameSetup has placed the camera and the player -- which is after
// zGameScreenTransitionEnd has torn the loading screen's own camera down. So
// the loading screen hands the still over instead of releasing it, and the
// first seconds of the game loop finish the job.
void iLoadTransitionSetFancy(S32 fancy);

// Whether it is on. The loading screen asks because `fancy` takes the bubbles
// off it: they are the wipe's, and a wall that plays through the load and then
// stops dead when the still starts moving is two animations, not one.
S32 iLoadTransitionFancy();

// A load has begun; zGameScreenTransitionBegin. Only job is to abandon a wipe
// that a scene change cut short.
void iLoadTransitionBegin();

// The loading screen is coming down; zGameScreenTransitionEnd. TRUE means the
// wipe has taken the still and the caller must NOT release the snapshot --
// iLoadTransitionWipeFrame releases it when it is done with it. FALSE means
// there is no wipe to run and the caller owns the still as it always did.
S32 iLoadTransitionStartWipe();

// Whether a wipe is up. Asked before the scene renders, because the bubbles
// have to come out of the scene's own pass while it is: they belong OVER the
// still, the way the loading screen draws them over its background, and the
// scene draws them long before the still goes down.
S32 iLoadTransitionWiping();

// Where the bottom of the still is this frame, as a fraction of the way up the
// screen: 0 at the bottom edge, 1 at the top, negative while it is still below
// the screen and the still is whole.
F32 iLoadTransitionWipeEdge();

// And where to lay this frame's bubbles, in the same terms. It is not the same
// number: the wall rises the whole height of the screen BEFORE the still starts
// to leave, so that the level is uncovered behind bubbles instead of in plain
// view. Once it is covered this follows the still's edge.
F32 iLoadTransitionBubbleUp();

// One frame of the wipe, drawn over the scene the game loop has just rendered.
// Returns TRUE when the caller should spawn a bubble wall this frame: the wipe
// keeps the bubbles going at the rate the loading screen spawned them, and
// zFX_SpawnBubbleWall is game code that the platform layer does not call.
S32 iLoadTransitionWipeFrame(F32 dt);

#endif
