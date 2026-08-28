#ifndef IDRAWDIST_H
#define IDRAWDIST_H

#include <types.h>

// PC-only: whether the game stops drawing things that are far away. There is no
// GameCube counterpart -- the console has one budget and the artists spent it --
// so shared code reaches this through src/SB/Core/x/xDrawDist.h, which
// preprocesses away entirely on the console.
//
// **Three separate systems make distance hurt**, and the setting has to reach
// all three or it does nothing anyone would notice:
//
//   1. `zLOD.cpp` owns the models the LODT assets name. Beyond
//      `adjustNoRenderDist` it sets flag 0x400 and the model is not drawn at
//      all; for the last four units before that it fades the model out through
//      `FadeStart`/`FadeEnd`, which `xModelBucket_Add` reads. Under
//      `lodDist[0..2]` it swaps the model's bucket for a lower-detail one.
//   2. `zEntSimpleObj.cpp` does the same job again, from its own copy of the
//      same table, for the static props that make up most of a level. It is
//      easy to miss and it is the system most of the popping comes from.
//   3. The camera's far clip plane, 400 units, which frustum-culls everything
//      past it -- the world geometry included. Lifting 1 and 2 without lifting
//      this would just move the wall.
//
// **Two things that read the same numbers are deliberately left alone.**
//
//   `zSceneSetup` derives the UPDATE cull distance from `zLODTable::noRenderDist`
//   -- the same field, the same `SQR(10 + sqrt(d))` formula -- so an entity
//   stops thinking at the distance it stops drawing. That is a simulation
//   budget, not a picture: lifting it would leave every NPC in the level
//   running, which costs far more than drawing them and changes when things
//   happen. So the switch is applied at each RENDER-side use of the distance
//   rather than by rewriting the table, which is why xDrawDist.h wraps values
//   instead of exposing a flag.
//
//   Fog. When a level has fog, `iCameraSetFogRenderStates` puts the far clip at
//   `fogStop`, because RenderWare's linear fog runs from the fog plane to the
//   far plane and the two have to agree. At that distance the picture is 100%
//   fog colour, so there is nothing behind it to reveal -- pushing the plane out
//   would only stretch the gradient and thin the fog the level was authored
//   with. Fogged levels keep their fog; the setting reaches the unfogged path.

// The switch. FALSE until iSystem says otherwise, so a target that never calls
// the setter -- rw_selftest does not -- behaves exactly as the port did before
// this existed.
S32 iDrawDistUnlimited();

// The camera far clip to build cameras with, and to fall back to when
// `iCameraSetNearFarClip` is handed a zero. 400.0f when the switch is off,
// which is the number retail has always used.
F32 iDrawDistFarClip();

// Set by iSystem from config.ini, before the first camera is created.
void iDrawDistSetUnlimited(S32 unlimited);

#endif
