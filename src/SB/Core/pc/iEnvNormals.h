#ifndef IENVNORMALS_H
#define IENVNORMALS_H

#include <types.h>

// iEnvBakedRig and iENV_BAKED_LIGHTS are part of two signatures below.
#include "iEnv.h"

// Give a level's world geometry the normals it was shipped without.
//
// **This is what stands between the world and being lit at run time.** The
// world is 100% baked vertex colour, which is easy to replace; what cannot be
// replaced is a normal that was never stored. Only 21 of the game's 55 levels
// ship them, and it is all-or-nothing per level -- a level either has them
// throughout or not at all -- because nothing at the time ever asked the world
// which way it faced.
//
// Call between reading the clump and instancing it: iEnvLoad's
// RpClumpForAllAtomics is where the vertex buffers are built, and a normal
// added afterwards is a normal the buffer has no room for.
//
// Does nothing to a world that already has normals, so it is safe to call
// unconditionally, and the 21 levels that do have them are the only way to
// check the generator against something other than an opinion --
// iEnvNormalsCompare is that check.
void iEnvGenerateNormals(iEnv* env);

// Generate into scratch memory and report how close the result comes to the
// normals the level actually shipped, instead of using it. For levels that have
// them; does nothing and reports nothing for levels that do not.
void iEnvNormalsCompare(iEnv* env);

// Take the baked colour off the world, so a run-time light replaces it rather
// than adding to it. Before instancing, like the generator.
void iEnvDropPrelight(iEnv* env);

// Release what iEnvGenerateNormals allocated. Called from iEnvFree.
void iEnvFreeNormals(iEnv* env);

// Give one model the normals it shipped without, and drop the paint that the
// run-time rig replaces. Between reading the clump and instancing it, like the
// world's; iEnvNormals.cpp says why a prop without normals cannot be lit at all.
//
// Only under experimental.world_lighting, which is the setting that says a rig
// stands in for the bake. Off, a prop keeps exactly what the console drew.
void iEnvPrepareModel(RpClump* clump);

// Free what iEnvPrepareModel allocated for this clump. From iModelUnload.
void iEnvForgetModel(RpClump* clump);

// The models that are really pieces of the world, listed by asset id, and which
// atomics came out of one. iEnvNormals.cpp says which models and why.
S32 iEnvGroundDecalAsset(U32 assetID);
void iEnvPendingGroundDecal(S32 on);
S32 iEnvTakePendingGroundDecal(void);
void iEnvMarkGroundDecal(RpClump* clump);
S32 iEnvIsGroundDecal(void* atomic);

// Fit the rig NOW, from the world's geometry as it shipped.
//
// experimental.hipoly_assets rebuilds that geometry before iEnvLoad ever sees
// it, and the fit comes out much worse on the result. The prelight is
// interpolated onto vertices no artist painted, and normals generated over the
// finer mesh agree with the shipped ones only 27% to 57% of the time against
// 62% to 78% on the shipped mesh. Both push the rig flat: bb01 fits an ambient
// of 0.37 against a key light of 0.49 after smoothing, where the shipped mesh
// gives 0.27 against 0.77.
//
// So the rig is taken here, where the paint and the topology are still the pair
// the artists authored together, and iEnvLoad applies it to whatever geometry it
// ends up with. Keyed on the clump, which hipoly rebuilds in place and so does
// not move; a clump that never reaches iEnvLoad is simply overwritten by the
// next one.
void iEnvFitShippedRig(RpClump* clump);

// The rig's ambient and per-light colours at a contrast setting.
//
// Shared so the two consumers cannot drift: zScene builds a light kit out of
// this, and iEnvBakeShadowedLight evaluates it per vertex. Lights past
// rig->count come back black.
void iEnvRigAtContrast(const iEnvBakedRig* rig, F32 contrast, F32 ambient[3],
                       F32 color[iENV_BAKED_LIGHTS][3]);

// Light the world once, at load, with the parts its own geometry hides from each
// light left dark, and write the answer into the prelight.
//
// The world keeps rpGEOMETRYPRELIT and loses rpGEOMETRYLIGHT, so it draws the
// colour computed here and nothing lights it again. iScreenWorldLightShadows
// says what that costs.
void iEnvBakeShadowedLight(iEnv* env);

// How bright the artists painted this level, 0 to 1, or 0 if it was never
// measured -- which is any level whose paint is left alone.
//
// The cel path needs it because the lights cannot answer it: the room colour
// there is every light summed, so a house indoors resolves as bright as open
// sunlight. iEnvNormals.cpp has the numbers for two levels.
F32 iEnvPaintLevel();

// The shade the level's placed models throw on the level, over a whole day.
//
// Hand over each placed model with the matrix it stands at, then bake. The trace
// walks the arc the sun actually takes and keeps one byte per world vertex per
// step, which the run time blends between as the clock moves; iEnvNormals.cpp
// says why it cannot be one trace or a trace per frame.
//
// **Said by the scene and not found here, because of when.** iEnvLoad runs as
// the JSP arrives and nothing is placed yet, so this waits for zSceneSetup.
void iEnvOccluderAdd(RpAtomic* model, const RwMatrix* mat);
void iEnvOccluderClear();
void iEnvSunShadeBake(iEnv* env);
void iEnvSunShadeClear();

// What it found, or NULL. Laid out step-minor: vertex v's step s is at
// v*steps + s, in the same vertex order iEnvGenerateNormals uses.
const U8* iEnvSunShade(S32* verts, S32* steps);

// Blend the traced steps into the world's prelight for the time of day given, so
// the cel shader reads the shade standing now. Cheap to call every frame: it
// returns without touching anything until the sun has moved far enough to be
// worth a write. `force` writes regardless, for the first one.
void iEnvSunShadeApply(iEnv* env, F32 phase, S32 force);

#endif