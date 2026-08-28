# Running uncapped: what breaks, and what it would take

## Where things stand

The port simulates at a fixed 1/60 s step (`zGame.cpp`, the accumulator around
the update block) and presents whenever the display is ready. That is not an end
state, it is a way of staying correct while the list below is open. This
document is that list.

Uncapping was tried and rolled back. With `dt` set to the real frame time,
gameplay ran at 1700-3200 fps (median 1732, over 64 seconds), putting `dt` near
0.3 ms. Anything assuming a sixtieth of a second is then wrong by a factor of
fifty, not the factor of four a 240 Hz display suggests. Three systems were
visibly broken: the camera, pickups, and particles. All three are explained
below.

The frame rate is not bounded by the display unless vsync is on. The apparent
240 fps ceiling seen in early testing was the intro movies, which ask for vsync
at `iFMV.cpp:433`. Gameplay does not.

## The three confirmed systems

### Camera

`zCamera.cpp:927` and `:952` build the per-frame yaw delta as

    dp = 0.016666668f * (dp * zcam_pad_pyaw_scale);

then add it into `pgoal`, `cam->pcur` and `cam->pgoal`. It is a rate times a
hardcoded timestep, so the stick turns the camera at a speed proportional to the
frame rate. Four times too fast at 240 fps, fifty times at 3000.

Fix: `dp = dt * (dp * zcam_pad_pyaw_scale);`

`zCamera.cpp:985` and `:997` look identical and are NOT the same thing. `pitch_s`
is reset to zero every frame at line 992 and consumed in
`zCameraFreeLookSetGoals` (lines 669-679) as a blend weight,
`dgoal = pitch_s * (zcam_below_d - d) + d`. The constant converts stick units
into a 0..1 fraction. It is not a timestep, nothing accumulates it, and
multiplying by `dt` makes the pitch target shrink as the frame rate rises. Leave
both alone.

That pair is why this is a document and not a patch. The two cases are textually
identical and behave oppositely.

### Pickups

`zEntPickup.cpp:2141` spins one shared global:

    xMat3x3RMulRotY((xMat3x3*)&sPickupOrientation, (xMat3x3*)&sPickupOrientation, PI * dt);

The rotation is correct, `PI * dt` is a proper rate. The problem is that the
matrix is multiplied into itself every frame and `xMat3x3RMulRotY`
(`xMath3.cpp:386`) does not renormalise. Error accumulates per multiplication,
not per second, so fifty times the frame rate is fifty times the drift. Line
2143 then sets `rwMATRIXTYPEORTHONORMAL`, asserting a property the matrix no
longer has, and every pickup copies it (lines 802, 1785, 1830, 1884).

Fix: renormalise after the multiply, or hold the angle as a scalar and rebuild
the matrix each frame. The second cannot drift at all.

### Particles

`xParEmitterEmitCustom(xParEmitter*, F32 dt, ...)` takes a time window as its
second argument and emits that much worth of particles. A constant there means
emitting a fixed amount per FRAME rather than per second.

`xFX.cpp:1624` and `:1652` pass `dt` and are correct. These do not:

    zEntPickup.cpp:1736, 2002, 2029      gEmitShinySparkles
    zEntPlayer.cpp:7143                  gEmitBFX
    zEntPlayer.cpp:7184                  sEmitStankBreath
    zNPCTypeDutchman.cpp:1877, 3340      flame, light
    zNPCTypeKingJelly.cpp:3025, 3057     shock ring, thump ring
    zNPCTypeTiki.cpp:1641                cloud
    xLaserBolt.cpp:61, 123               laser fx
    xParEmitter.cpp:230                  hardcodes 1/30, not 1/60

Fix: pass the frame's `dt`. The emitter already does the right thing with it.

## The rest of the audit

### Hardcoded 1/60 used as a timestep

Beyond the camera and the emitters above:

    zNPCHazard.cpp:659, 678, 799, 901    ang_spin.x = 0.016666668f * (...)
    zNPCHazard.cpp:940                   ang_spin *= 0.016666668f
    zNPCHazard.cpp:1820                  xVec3SMulBy(&ang_spin, 0.016666668f)
    zNPCHazard.cpp:2001                  tym_end += 0.016666668f
    zEntPlayer.cpp:200, 201              update_dt, last_update_dt initialisers
    zNPCSupplement.cpp:538               info->freq = 1.0f / 60.0f
    zNPCSupplement.cpp:1018              useFixedTimestepForSpiral
    zNPCMgr.cpp:509                      BackdoorUpdateAllNPCsOnce(scene, 1/60)

The `zNPCHazard` group is spin rates per frame and needs `dt`. The two
`zEntPlayer` statics are initialisers, overwritten before use, listed here so
nobody has to re-check them. `zNPCMgr.cpp:509` is a one-shot catch-up call, not a
per-frame path. `useFixedTimestepForSpiral` is named for what it is and needs its
own look.

### Per-frame multiplicative damping

`x *= k` once per frame settles at a rate set by the frame rate. Verified as
genuine per-frame decay on persistent state:

    zEntPlayer.cpp:10734, 10735          vel.x, vel.z *= 0.96f   (jump)
    zEntPlayer.cpp:10988, 10989          v->x, v->z *= 0.97f     (slick surface)
    zNPCGoalRobo.cpp:3718                drot.angle *= 0.97f
    zNPCGoalRobo.cpp:3818                drot.angle *= 0.8f
    zNPCGoalRobo.cpp:5767                ang_spinrate *= 0.8f
    zNPCGoalRobo.cpp:8112                ang_spinrate *= 0.99f
    zNPCGoalVillager.cpp:1062            ang_spinrate *= 0.985f
    zNPCSupplement.cpp:828               npdata->vel *= 0.9f
    zNPCTypeBossSandy.cpp:2029, 2031     jawLevel, jawThreshold
    zEntPlayerBungeeState.cpp:1108       rot_vel *= fixed.turn.decay
    zEntPlayerBungeeState.cpp:2717       v *= fixed.horizontal.decay

Fix: `x *= xpow(k, 60.0f * dt)`. This is the codebase's own idiom, not an
invention. Retail already uses it at `zFX.cpp:427`, `zFX.cpp:449` and
`zParPTank.cpp:319`, and `zEntCruiseBubble.cpp:3392` uses the related
`xpow(1.0f - decay, dt)`.

Sites that look like this and are not: `zCamera.cpp:931, 956, 1076`
(`dp *= 0.2f` scales an already-per-frame delta, so it rides on whatever `dp`
is), and every `*= -1.0f`, every `*= 1.0f / dst` normalise, and every one-shot
magnitude tweak.

### Additive accumulation without dt

This category is NOT resolved. The numbers here are leads, not findings.

A scan of `+=` and `-=` inside functions taking `F32 dt`, where the line does not
mention `dt`, yields 223 hits: 92 writing through a pointer or object, 131
writing to locals. Spot-checking the persistent group found a high false positive
rate:

    zSurface.cpp:355, 357, 362, 364      rot -= 360.0f, trans.x += 1.0f
                                         wrapping into range, not accumulating
    xParEmitterType.cpp:150, 152, 221    per-emission random spread, runs once
                                         per particle, not per frame
    zEntPickup.cpp:1628                  += 10000.0f, moves a thing offscreen
    zEntPickup.cpp:2239                  += 1.0f spawn offset
    zEntPickup.cpp:1354                  ent->vel.y += 0.08f * ydiff
                                         this one does look real

So roughly one in five of what the scan flags is worth changing, and which one
cannot be told without reading the surrounding function. Treating these as a
to-do list would overstate the work about fivefold.

Files with the most persistent-group hits, as a place to start reading:
`zSurface.cpp` (15), `zEntPlayer.cpp` (14), `zNPCSupplement.cpp` (12),
`xParCmd.cpp` (11), `xParEmitterType.cpp` (9), `zNPCTypeBossPatrick.cpp` (7),
`zNPCTypeTiki.cpp` (7).

### Not yet looked at

Animation and skinning rates, `xScrFx`, water and UV scrolling in `zSurface`
proper (as opposed to the wrap sites above), and anything counting frames rather
than seconds. `gFrameCount` and `ostrich_delay` in `zGame.cpp` are frame counters
by construction.

## Telling a real site from a false one

Four things wear the same clothes, in rough order of how often they turn up:

1. A RATE. Multiplied by a timestep, accumulates into persistent state. Needs
   `dt`. Example: camera yaw.
2. A UNIT CONVERSION. A constant that happens to be 1/60 but converts units, with
   nothing accumulating. Leave alone. Example: `pitch_s`.
3. A WRAP. `-= 360.0f` or `+= 1.0f` to bring a value back into range, inside an
   `if`. Leave alone. Example: the `zSurface` UV wrap.
4. A ONE-SHOT. Runs on an event, not every frame, and happens to sit in a
   function that takes `dt`. Example: particle emission spread.

What separates 1 from 2 is whether the value survives the frame. If it is reset
at the top of the function, or consumed and discarded, it is integrating nothing
and `dt` does not belong in it.

## One thing the fixed step is not buying

`zGame.cpp:579` substitutes 1/60 for any frame measured under ten microseconds.
That is above 100,000 fps and was never reached in testing, so it is not the
cause of anything observed. Earlier notes in this repo blamed the spinning
pickups on it. That was wrong. The real cause is in the pickups section above.

## Reproducing the candidate lists

The scan walks `src/SB/Game` and `src/SB/Core/x`, tracks brace depth to find
functions taking `F32 dt`, and reports lines inside them using `+=`, `-=` or `*=`
against a float literal without mentioning `dt`. It is a lead generator. Do not
treat its output as a defect list, for the reasons in the additive section.
