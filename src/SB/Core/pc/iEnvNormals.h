#ifndef IENVNORMALS_H
#define IENVNORMALS_H

#include <types.h>

struct iEnv;

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

#endif
