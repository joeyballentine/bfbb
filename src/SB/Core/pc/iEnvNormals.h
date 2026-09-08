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

// Scale the kept paint by a colour, through the material rather than the
// vertices.
//
// A piece whose prelight is artwork has rpGEOMETRYLIGHT cleared, so no light
// reaches it and it holds noon for ever while everything around it turns. World
// geometry carries rpGEOMETRYMODULATEMATERIALCOLOR, and RenderWare multiplies
// the material's colour into the finished vertex colour, so one RwRGBA per
// material moves the whole decal without rewriting a vertex.
void iEnvTintKeptPaint(iEnv* env, const F32 rgb[3]);

#endif