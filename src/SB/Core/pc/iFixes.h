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
// margin, so nothing else changes when this is on.
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
// `mat` was not touched and `saved` holds nothing -- the fix is off, there is
// no camera, or the model already fits, which is every level but GL03.
//
// `world` is the model's world bounding sphere, which `iModelCull` fills in on
// the way past, so the caller has one already.
S32 iFixSkyDomeToFarPlane(RwMatrix* mat, const RwV3d* eye, const RwSphere* world,
                          RwMatrix* saved);

#endif
