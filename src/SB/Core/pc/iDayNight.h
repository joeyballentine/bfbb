#ifndef IDAYNIGHT_H
#define IDAYNIGHT_H

#include <types.h>

#include "iEnv.h"

// PC-only: move the sun. There is no GameCube counterpart, and there is nothing
// in the shipped game this reproduces -- every level's lighting is one fixed
// moment that the artists painted.
//
// **It needs experimental.world_lighting on, and world_light_shadows off.** With
// world lighting off the level's colour IS the prelight, a byte per vertex that
// nothing at run time can change, so the world would hold noon while only the
// characters moved. With the shadow trace on, iEnvBakeShadowedLight has already
// written the finished lighting into that same prelight and cleared
// rpGEOMETRYLIGHT, which is the same problem by a different route.
//
// What the cycle drives, and what it does not:
//
//   The world's light kit, direction and colour. zScene rebuilds it as the sun
//   moves. This is the whole illusion.
//   Every other light kit's colour, so a character standing in the level does
//   not stay lit at noon. Directions are left alone -- an object kit is three or
//   four lights the artists aimed at a character from the camera's side, and
//   swinging those does not read as a sun.
//   Not the skydome, and not fog. Both would help and neither is here.
//
// The level's own fitted rig is taken as NOON. Its key light gives the sun's
// azimuth and its peak height, its colour gives the sun's colour, and the cycle
// swings a full circle through that point. So each level keeps the direction and
// the palette the artists gave it, and what moves is the time.

// Seconds for one full turn, sunrise to sunrise. 0 is off.
F32 iDayNightLength();
void iDayNightSetLength(F32 seconds);

S32 iDayNightActive();

// Where in the day it is: 0 sunrise, 0.25 noon, 0.5 sunset, 0.75 midnight.
F32 iDayNightPhase();

// Move the clock on. Called once per scene update, and not while paused.
void iDayNightAdvance(F32 seconds);

// Where the sun stands at a given phase, as the direction TOWARD it.
//
// The same arc iDayNightRig swings the key light along, asked at a phase that
// need not be now. The shadow trace wants it that way: it traces the day at load
// and blends between what it found as the clock runs, and it has to trace the
// path the light will actually take.
void iDayNightSunToward(const iEnvBakedRig* noon, F32 phase, xVec3* toward);

// The level's rig with its lights swung to the current time of day.
//
// Colours come out at their fitted values, NOT scaled for the time: the scaling
// is the tint below, which every kit gets on its way to being enabled. Doing it
// in one place is what keeps a character and the ground it stands on the same
// brightness.
void iDayNightRig(const iEnvBakedRig* noon, iEnvBakedRig* out);

// What to multiply a light kit's authored colour by, now.
//
// **A hue rotation and nothing else.** Every multiplier holds its luminance, so
// a lit surface is as bright at midnight as at noon, and only its colour and the
// direction it is lit from change. iDayNightScreen below is what makes the night
// dark. Scaling the lights as well would darken lit surfaces twice over, leaving
// the world and its characters black against the props no light reaches.
void iDayNightTint(F32 ambient[3], F32 directional[3]);

// The grade the finished frame gets, as a multiply and then an add. Drawn over
// the scene and under the interface.
//
// **This is what makes the night a night, and the lights only say which way the
// sun is.** Chasing the level down through the lighting cannot work, because
// most of what is on screen is not lit: a model with no light kit draws the
// colour baked into it, the world keeps its painted decals for the same reason,
// and the skydome is a texture. Each of those is a separate path and there is no
// end to them.
//
// Two quads over the lot reach everything, including the things nobody thought
// of. The multiply turns the picture cyan and the add keeps it off black, so the
// result is a saturated night rather than a dim day. What neither can do is tell
// a surface facing the moon from one facing away, which is exactly what the
// light kits are still there for.
void iDayNightScreen(F32 mul[3], F32 add[3]);

#endif
