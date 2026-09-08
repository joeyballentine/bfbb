#ifndef IENV_H
#define IENV_H

#include "xJSP.h"
#include "xMath3.h"

#include <rwcore.h>
#include <rpworld.h>

// How many directionals the bake fit keeps. Four is where the gain stops --
// see RecoverBakedLight in iEnvNormals.cpp -- and it is what the artists' own
// world kits use.
#define iENV_BAKED_LIGHTS 4

// The rig recovered from a level's baked vertex colour: an ambient and up to
// iENV_BAKED_LIGHTS directionals, fitted per channel by iEnvNormals.
//
// Directions are where the light TRAVELS, like RpLight's, not where it comes
// from. Lights are ordered brightest first, so dir[0] is the one a
// single-direction consumer -- a shadow -- should follow.
struct iEnvBakedRig
{
    S32 valid;
    S32 count;
    xVec3 dir[iENV_BAKED_LIGHTS];
    F32 color[iENV_BAKED_LIGHTS][3];
    F32 ambient[3];
    // What the directionals contribute to the AVERAGE vertex: the sum over
    // lights of the light's colour times the mean of max(0, n.s) over the
    // world. The contrast setting holds ambient + this constant while it scales
    // the directionals, so a level keeps its brightness as it gains contrast.
    // See iScreenWorldLightContrast.
    F32 dirMean[3];
};

struct iEnv
{
    RpWorld* world;
    RpWorld* collision;
    RpWorld* fx;
    RpWorld* camera;
    xJSPHeader* jsp;
    RpLight* light[2];
    RwFrame* light_frame[2];
    S32 memlvl;
    // Normals made at load time for a world that shipped without them, as one
    // block sliced across the clump's geometries. NULL when the level brought
    // its own, which 21 of the 55 did. See iEnvNormals.h.
    void* genNormals;
    iEnvBakedRig baked;
    // Whether iEnvLoad took the baked colour off this world's lit geometry.
    //
    // The render side must not ask the question a second time: if load drops
    // the prelight and render then decides not to light, the level is black,
    // and if load keeps it and render lights anyway the two are added. One
    // decision, made at load, read at render.
    S32 prelightDropped;
    // The materials of the pieces whose paint was kept, so something can still
    // reach them. They draw from the prelight with rpGEOMETRYLIGHT clear, which
    // is what protects them from being lit and also what stops a day/night cycle
    // ever touching them. See iEnvTintKeptPaint.
    RpMaterial** keptMaterial;
    S32 keptMaterialCount;
};

struct xEnvAsset;

void iEnvLoad(iEnv* env, const void* data, U32 datasize, S32 dataType);
void iEnvFree(iEnv* env);
void iEnvDefaultLighting(iEnv*);
void iEnvLightingBasics(iEnv*, xEnvAsset*);
void iEnvRender(iEnv* env);
void iEnvEndRenderFX(iEnv*);

inline RwBBox* iEnvGetBBox(iEnv* r3)
{
    return &r3->world->boundingBox;
}

#endif
