#ifndef XENTBOULDER_H
#define XENTBOULDER_H

#include "xEnt.h"
#include "xDynAsset.h"
#include "xShadowSimple.h"

struct xEntBoulderAsset
{
    F32 gravity;
    F32 mass;
    F32 bounce;
    F32 friction;
    F32 statFric;
    F32 maxVel;
    F32 maxAngVel;
    F32 stickiness;
    F32 bounceDamp;
    U32 flags;
    F32 killtimer;
    U32 hitpoints;
    U32 soundID;
    F32 volume;
    F32 minSoundVel;
    F32 maxSoundVel;
    F32 innerRadius;
    F32 outerRadius;
};

struct xEntBoulder : xEnt
{
    xEntBoulderAsset* basset;
    xShadowSimpleCache simpShadow_embedded;
    xEntShadow entShadow_embedded;
    xVec3 localCenter;
    xVec3 vel;
    xVec3 rotVec;
    xVec3 force;
    xVec3 instForce;
    F32 angVel;
    F32 timeToLive;
    S32 hitpoints;
    F32 lastRolling;
    U32 rollingID;
    U8 collis_chk;
    U8 collis_pen;
    U8 pad1[2];
#ifdef PLATFORM_PC
    // Boulder physics runs at a fixed 1/60, not at the frame rate. See the
    // comment on xEntBoulder_FixedStep in xEntBoulder.cpp for why. These carry
    // the accumulator and the two states the rendered transform interpolates
    // between, and they sit at the end of the struct behind the guard so
    // sizeof(xEntBoulder) -- which zScene.cpp puts in a table the GameCube
    // objects contain -- does not move.
    F32 fixedAcc;
    xMat4x3 fixedMat;        // the simulated transform, after the last whole step
    xMat4x3 fixedRenderMat;  // what was last written to model->Mat to be drawn
    xVec3 fixedPrevPos;      // the position the last step started from
    U8 fixedInit;
    U8 pad2[3];
#endif
};

struct xBoulderGeneratorAsset : xDynAsset
{
    U32 object;
    xVec3 offset;
    F32 offsetRand;
    xVec3 initvel;
    F32 velAngleRand;
    F32 velMagRand;
    xVec3 initaxis;
    F32 angvel;
};

struct xBoulderGenerator : xBase
{
    xBoulderGeneratorAsset* bgasset;
    S32 numBoulders;
    S32 nextBoulder;
    xEntBoulder** boulderList;
    S32* boulderAges;
    U32 isMarker;
    void* objectPtr;
    F32 lengthOfInitVel;
    xVec3 perp1;
    xVec3 perp2;
};

void xEntBoulder_Init(void* ent, void* asset);
void xEntBoulder_Init(xEntBoulder* ent, xEntAsset* asset);
void xEntBoulder_BubbleBowl(F32 multiplier);
void xEntBoulder_Setup(xEntBoulder* ent);
void xEntBoulder_Reset(xEntBoulder* ent, xScene* scene);
void xEntBoulder_RealBUpdate(xEnt* ent, xVec3* pos);
void xEntBoulder_Kill(xEntBoulder* ent);
void xBoulderGenerator_Init(xBase& data, xDynAsset& asset, size_t);
void xBoulderGenerator_Init(xBoulderGenerator* bg, xBoulderGeneratorAsset* asset);
void xBoulderGenerator_Init(xBase& data, xDynAsset& asset);
S32 xEntBoulderEventCB(xBase*, xBase*, U32, const F32*, xBase*);
void xEntBoulder_ApplyForces(xEntCollis* collis);
void xEntBoulder_Update(xEntBoulder* ent, xScene* sc, F32 dt);

#endif
