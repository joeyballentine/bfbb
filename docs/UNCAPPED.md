# Running uncapped: what was fixed, and what is left

## Where things stand

The frame rate is a setting. `config.ini`'s `video.framerate` takes a number of
frames a second, `display` for the monitor's refresh rate, or `0` for no cap;
`video.vsync` decides separately whether the present waits for the display. The
default is 60 and vsync on, which is what the GameCube's video interface gave
the game.

The two settings answer different questions and are wired separately.
`iWindowPaceFrame` in `iWindowWin32.cpp` holds the cap and sleeps to a deadline;
`RwCameraShowRaster` in `rw/camera.cpp` decides the flip flag. Vsync alone still
runs at whatever the monitor gives, and a cap alone still tears.

`dt` is the real frame time. Uncapped, gameplay runs at 1700-3200 fps (median
1732, over 64 seconds), putting `dt` near 0.3 ms — a factor of fifty off the
sixtieth of a second the original code was written against, not the factor of
four a 240 Hz display suggests.

Every change below is inside `#ifdef PLATFORM_PC`. The GameCube arm of every
file this touched is byte identical to what it was.

## The three systems the first audit named

Two were real. The camera was not, and finding that out cost a revert.

### Camera — NOT a bug, and not fixed

The original version of this document called `zCamera.cpp`'s yaw constant a rate
times a hardcoded timestep and said the camera turned four times too fast at 240
fps. That is wrong, it was changed to use `dt`, and the change was reverted after
a player reported the camera turning at half speed at 128 fps.

`dp` does not accumulate. It is added to `cam->pcur` and to a LOCAL `pgoal`, and
`xCameraMove` then writes that local into `cam->pgoal` — so the goal is always
exactly `dp` ahead of where the camera is, never further. `xCameraUpdate`
re-derives `pcur` from `mat.pos` through `xCam_worldtocyl` at the top of every
frame, which throws the `pcur` write away, and `xCam_CorrectP` springs the
POSITION toward the goal by a fraction proportional to `dt`.

So the angle moved per frame is `(k * dt) * dp` and the rate per second is that
over `dt` — `k * dp`, with the `dt` cancelled. A CONSTANT `dp` is what makes the
turn rate frame-rate independent. The `1/60` converts stick units into radians of
gap. Multiplying by `dt` makes the rate proportional to `dt`, which is the
half-speed-at-double-the-frame-rate the player saw.

The pitch pair at `pitch_s` is the same shape for a different reason: it is reset
to zero every frame and consumed in `zCameraFreeLookSetGoals` as a 0..1 blend
weight. Also not a timestep.

The overrotation site further down builds its `dp` from `zcam_overrot_rate` with
no `dt` at all and feeds the same `pcur`/`pgoal` mechanism, so it is frame-rate
independent for the same reason. Leave it.

`xCamera.cpp` keeps a `static F32 last_dt` and uses it to turn a per-frame
position delta into a velocity. Retail wrote this camera for a variable frame
time. It is one of the few systems in the game that was already right, which is
why the naive reading fails here three times over.

The boss fights do not use this camera. `xBinaryCamera` does, and that one has a
real defect - see *A bound and a lag that multiply* below.

### Pickups — fixed

`zEntPickup_SceneUpdate` multiplied one shared global matrix into itself every
frame with `xMat3x3RMulRotY`, which does not renormalise. Error accumulated per
MULTIPLICATION rather than per second, so fifty times the frame rate was fifty
times the drift — and the next line then set `rwMATRIXTYPEORTHONORMAL`,
asserting a property the matrix no longer had. Every pickup copies that matrix.

Now the angle is held as a scalar and the basis is rebuilt with `xMat3x3RotY`
each frame. That cannot drift at all.

### Particles — fixed

`xParEmitterEmitCustom(xParEmitter*, F32 dt, ...)` and
`zParPTankConvertEmitRate(xParEmitter*, F32 dt)` take a time WINDOW as their
second argument and emit that much worth of particles. A constant there emits a
fixed amount per FRAME.

Passed `dt`:

    zEntPickup.cpp      the three shiny-sparkle sites
    zEntPlayer.cpp      gEmitBFX
    zEntHangable.cpp    the chandelier candle flame and smoke
    zNPCTypeDutchman.cpp  the beam light
    zNPCTypeTiki.cpp    the thunder cloud

`zEntPlayer.cpp`'s stank breath is behind a `sLastInvulnEmit > 0.02f` time gate,
so `dt` is the wrong window there — it fires at 50 Hz, not per frame. It passes
half the elapsed time the gate measured, which is exactly `1/60` at 60 fps and
constant particles per second above it.

### The emitter core itself — audited, one defect

`xParEmitterEmit` at `xParEmitter.cpp:432` turns a rate and a window into a
count, and `zParPTankConvertEmitRate` at `zParPTank.cpp:814` is a copy of the
first half of it. Both are correct.

    pe->rate_fraction += rate * emit_dt;
    count = std::floorf(pe->rate_fraction);
    if (count > 0) pe->rate_fraction -= count;
    if (count == 0) return NULL;

The count FLOORS, there is no per-call minimum, and the remainder stays in
`rate_fraction` until it makes a whole particle. Halving the window doubles the
number of calls and halves each one's contribution, so the particles a second
do not change. `rate_fraction` is reset only in `xParEmitterInit`.

The accumulator is per-emitter and several emitters are shared globals, but that
does not bias the count either: twenty pickups calling `gEmitShinySparkles` with
their own rates add `sum(rate_i) * dt` a frame however the frame is diced. What
the sharing does change is WHICH caller's position gets the particle when the
accumulator crosses an integer, and that is retail behaviour at any frame rate.

`rate_time` is a wall-clock phase, wrapped modulo `prop->rate.freq` and fed to
`xParInterpCompute`. Every interpolation mode reads it as seconds.

The per-particle update in `xParGroupAnimate` ages life, colour and size against
`dt`. All correct.

The one defect is the velocity convention. `xParCmdVelocityApply_Update` does

    xVec3Add(&p->m_pos, &p->m_pos, &p->m_vel);

with no `dt`, so `xPar::m_vel` is a displacement per FRAME, and the emitter
types set it as `asset_vel * par_dt` at birth to match. That pair is
self-consistent. The acceleration commands are not: they add `acc * dt` to a
per-frame displacement, which is an acceleration of `acc / dt` a second — 2.1x
too strong at 128 fps. Six sites in `xParCmd.cpp` now carry an extra `60 * dt`,
which is one at a sixtieth of a second:

    xParCmdFollow_Update, xParCmdOrbitPoint_Update, xParCmdOrbitLine_Update,
    xParCmdAccelerate_Update, xParCmdApplyWind_Update, xParCmd_Shaper_Update

`xParCmdKillSlow_Update` compares `m_vel` squared against a limit scaled by one
`dt`, so the two sides scale apart; it takes the same factor.

`xParCmd_DampenSpeed_Update` and the `damp` term in `xParCmd_Shaper_Update` are
NOT this. They scale `m_vel` by a fraction of itself, which compounds to the
same factor a second whatever the frame rate. `xParCmdRandomVelocityPar_Update`
rotates `m_vel` by `cmd->x * dt` radians, an angular rate, and is also correct.

## Emission counted in frames

A spawn that runs every frame, every N frames, or on a per-frame coin flip is an
emission rate per frame whatever the emitter behind it does. Two helpers in
`xMath.h` convert one:

    U32 xFrameEmitCount(F32 count, F32 dt)    // count per frame -> per second
    F32 xFrameEmitChance(F32 chance, F32 dt)  // chance per frame -> per second

`xFrameEmitCount` resolves the leftover fraction with a coin flip rather than an
accumulator, so no site needs new state. `xFrameEmitChance` is
`1 - xpow(1 - chance, 60 * dt)`, the same shape as the bungee turn lerp.

Converted:

    xEntBoulder.cpp         the bubble bowl trail, a tenth of the speed a frame
    zThrown.cpp             the thrown-object trail
    zEntPlayer.cpp          the springboard, stun, goo, Patrick melee, tongue
                            and slide bubble trails
    zEntPlayer.cpp          `zEntPlayer_SpawnWandBubbles`, the bubble wand
                            trail. SpongeBob's melee. Three bubbles a frame for
                            the whole bubble spin, one a frame for the Bbash and
                            Bbounce windups. Scaled inside the function, since
                            all three callers run once a frame while their
                            effect is live and none of them pass a window.
                            A frame that wins no bubble stamps `last_frame`
                            without touching `last_center`, so the five-frame
                            restart gap still measures a break in the effect and
                            the next spawn spreads its bubbles over the whole
                            path the wand travelled. Passing zero would have hit
                            the `count != 0` default and emitted three.
    zScene.cpp              the menu bubbles, a 1.5% chance a frame
    zEntSimpleObj.cpp       the blob burst
    zShrapnel.cpp           the projectile trail
    zNPCHazard.cpp          seven `moreorless` frame dividers, now seconds
                            timers at the same period
    zNPCTypeRobot.cpp       DoFX_Motorboat, a bubble on five of every sixteen
                            frames
    zNPCGoalRobo.cpp        the AlertGlove whirlwind counter (now
                            `tmr_nextemit`), the death-ray tip, both tube-dying
                            propel trails
    zNPCGoalAmbient.cpp     the bumped-jellyfish trail
    zNPCTypeBossPatrick.cpp the glob trail
    zEntPlayer.cpp          the bubble bash and bounce bone contrails, one
                            bubble a frame at each of four bones. Each bone
                            rolls its own count, so the trail keeps its spread
                            instead of collapsing onto the first bones
    zEntPlayer.cpp          Patrick's StunLand slam, a 24-bubble ring every
                            frame for the first quarter second. The number of
                            RINGS scales, not the bubbles in one: the ring
                            spreads its angles over its own count, so a thinner
                            ring is a few fixed spokes rather than a circle
    zNPCHazard.cpp          DeathStar, twenty bubbles a frame for the first
                            sixth of the hazard's life
    zNPCGoalRobo.cpp        FurryFlurry's cone. `moreorless` is reset to -1, so
                            it is negative on every later call and the cone goes
                            out every frame
    zNPCFXCinematic.cpp     the `_AR` callbacks and NCIN_BubWipe/NCIN_BubHit --
                            see below

Left alone:

    zNPCSupplement.cpp:702  a burst inside a nested loop, run once on an event
    zNPCTypeAmbient.cpp:455 ActLikeOctopus, run once from a goal Exit
    zNPCHazard.cpp:2233     ReconTarTar and the other Recon/Kick bursts
    zEntCruiseBubble.cpp    `shared.trail.bubbles += dt * bubble_rate` already
    zFX.cpp                 `update_popper` and `entrail_data::update` already
    zNPCGoalRobo.cpp:1413   `cnt_nextlos` throttles a line-of-sight raycast, not
                            a spawn. Cheaper at a high frame rate, not wrong.

`zEntPlayer.cpp:4406` was on this list and should not have been. It reads as an
impact burst — ten bubbles when the melee bound hits something — and the worry
was that scaling it would cut a genuine one-frame hit to four bubbles. But all
THREE callers of `MeleeAttackBoundCollide` repeat once a frame while their move
is live: the slide track at `:7260`, the bubble spin's wand at `:8481` and
Sandy's melee tag at `:8975`. None is one-shot, so ten a frame is a rate for as
long as a target stays inside the bound, and during a bubble spin that is the
whole window. The one-shot users of `zFX_SpawnBubbleHit` — the boulder, the
teleport box, the shrapnel — are separate call sites, which is why the scale
goes at this call and not inside the function. `num == 0` returns early there,
so a frame that wins no bubble costs nothing.

Together with the wand trail above it, the bubble spin was emitting three
bubbles a frame along the wand path plus ten a frame per object in the bound.

`zNPCFXCinematic.cpp` has no `dt` in any of its callback signatures, but it does
not need one: `NCIN_BubbleTrail_AR` builds its own accumulator out of
`globals.update_dt`, which is retail's own global and is written once a frame in
`zGame.cpp`. The seven other per-frame sites now take their window from the same
place — `NCIN_BubWipe` (fifty a frame, and its buffer is sized from the scaled
count), `NCIN_BubHit`'s three-a-frame tail, `NCIN_BubTrailBone_AR`,
`NCIN_SleepyDRay_AR`, `NCIN_MidFish_AR`, `NCIN_BombTrail_AR`,
`NCIN_BoneTrail_AR` and `NCIN_HookRecoil_AR`.

`flg_stat & 2` is the effect's first-frame flag: `zNPCFXCutscene` clears it after
the first `cb_fxupd` call. Anything behind it is a one-shot and stays a fixed
count — `NCIN_BubSlam`, `NCIN_SleepyDRay_Upd`, `NCIN_ShieldPop`, and the sixteen
bubbles at the top of `NCIN_BubHit`.

## The bubble pool

`zParPTankBubbleUpdate` ages `life` by `dt`, moves by `vel * dt`, adds buoyancy
as `3.0f * dt` and damps with `xpow(0.95f, 60 * dt)` — retail's own rebase, the
`zFX.cpp:445` idiom. All correct.

The one defect was the early pop. A bubble between 1.2 and 0.5 seconds of life
left had a 4% chance of popping EVERY FRAME, which is a rate per frame. Over the
0.7 seconds the window is open that is 42 rolls on the console and 0.96^42 = 18%
survival; at 240 fps it is 168 rolls and 0.1%, so nothing reached the fade-out at
all. It is now `xFrameEmitChance(0.04f, dt)`, computed once outside the particle
loop.

The pool caps at 0x300 bubbles (0x10 for the menu tank), and `zParPTankSpawnBubbles`
silently truncates a request that would overflow it. Live count is spawn rate
times the 1.75-second life, so a correct rate saturates it exactly as often as
the console did. Every uncorrected site above was over that budget on its own:
the StunLand slam alone asked for 1440 bubbles in a quarter second at 240 fps
against a 768 pool.

## The emit window is two things at once

`xParEmitterEmit(pe, emit_dt, par_dt)` takes them separately and every caller
passes the same number for both. They are not the same job:

- `emit_dt` buys particles. The count is `rate * emit_dt`, remainder carried.
- `par_dt` scales the BIRTH VELOCITY, because `xPar::m_vel` is a displacement
  per FRAME — `xParCmdVelocityApply_Update` adds it to the position with no `dt`.

That is right only while the window IS the frame. A one-shot burst asking for a
console frame's worth of particles also asks for a console frame's worth of
step, so its debris leaves at `(1/60)/dt` times the intended speed — four times
too fast at 240 fps. A caller that subdivides its frame, like the Dutchman's
beam, has the opposite problem.

Fixed in `xParEmitterEmitCustom`: the count keeps the caller's window, the step
takes the frame's own share of it. This is what makes every "left alone as a
count" entry below correct rather than merely correct in count, and it is why
the firework burst can pass `1/60` safely.

The first version of this fix put the override in `xParEmitterEmit` itself and
set `par_dt = globals.update_dt` for everyone, on the reasoning that at a
sixtieth of a second the two are the same number. They are not. Four callers
pass a THIRTIETH -- `zEntHangable.cpp:154` and `:253`, `zGust.cpp:331`, and the
`eEventEmit` handler in `xParEmitter.cpp` -- and their particles came out at
half their console speed at 60 fps as well as above it. Only the caller knows
how many console frames its window stands for, so `xParEmitterEmitCustom` takes
that count as `par_frames` and the four pass `2.0f`. Everything else means the
one console frame that is the default.

Still divergent, and left: the Dutchman's beam subdivides its frame and passes
`ddt`, so on console its plasma and sparks got SLOWER the faster the beam swept.
They no longer do. That only shows while the beam covers more than one and a
half segment widths in a frame.

## A rate constant that is not a rate

An emitter whose `rate` is set to a multiple of 60 immediately before the call
is spelling an exact particle COUNT, not a rate. `rate.set(59.999996f)` against
a `1/60` window is one particle; `rate.set(119.99999f * n)` is `2n`. Passing
`dt` at one of those makes the effect thin out as the frame rate rises.

Left alone for that reason. Each was read a second time against the source and
each held:

    zNPCTypeDutchman.cpp:1877  the flame. `emit = (S32)(dist * emit_rate) + 1`
                            against a running `wave.emitted[i]`, so the loop
                            runs on distance travelled, not on frames
    zNPCTypeKingJelly.cpp:3025  the shock ring. `shock_ring_emitter_settings
                            .rate.val[0] = 59.999996f` at :862, and `total =
                            amount * dt * xurand() + 0.5f` already carries dt
    zNPCTypeKingJelly.cpp:3057  the thump ring. `rate.val[0] = 59.999996f *
                            tweak.thump.particles` at :3046, and the only caller
                            sets `delay = 1e9` right after it
    xLaserBolt.cpp:475,482  both take `dt`. The `reset()`/`emit()` in this file
                            are xDecalEmitter, not a particle window
    xParEmitter.cpp:230     eEventEmit, and no dt is in scope
    zGust.cpp:331           behind `debris_timer`, reset to 0.15-0.3 SECONDS at
                            :290 by the same block
    zPlatform.cpp:1093      `zPlatform_Tremble` runs on an event
    zPlatform.cpp:1108      `zPlatform_BreakawayFallFX` takes dt but its only
                            caller is the state 2 -> 3 transition at :835
    zEntHangable.cpp:248    `zEntHangableMountFX` runs on eEventMount

`xClimate.cpp` and `zLightning.cpp` pass a variable named `seconds`, which is
the real frame time, not a constant. Both correct.

## Hardcoded 1/60 used as a timestep

Fixed:

    zNPCHazard.cpp      the whole spin group. The four ConfigHelper sites, the
                        ROBOBITS `ang_spin *= 1/60` and the in-function
                        `xVec3SMulBy` all funnel through TypData_RotMatStore into
                        one matrix that Timestep applied once per frame. The
                        rate is now kept in radians per second on the hazard
                        (`ang_spinRate`) and the delta is rebuilt from it and dt.
    zNPCSupplement.cpp  `useFixedTimestepForSpiral` — a real timestep. It ages
                        `tmr_remain` and integrates `pos += vel * ts`.
    zNPCTypeRobot.cpp   `NPCC_TmrCycle(&tmr_cycle, 1/60, 2.63f)`, the sleepy
                        night light. Every other caller of NPCC_TmrCycle passes
                        its own dt. Not in the original audit.

Left alone, with the reason:

    zNPCHazard.cpp      `tym_end += 1/60` — a fixed padding past the lifespan
                        clamp so a swept sphere is not degenerate at end of life.
                        dt would shrink it to 0.3 ms and break the sweep.
    zNPCSupplement.cpp  `info->freq = 1/60` — a PERIOD in seconds, consumed by
                        xFXStreak as `elapsed >= frequency`. Already correct.
    zEntPlayer.cpp      `update_dt` / `last_update_dt` initialisers, written at
                        the top of zEntPlayer_Update before any reader.
    zNPCMgr.cpp:509     a one-shot catch-up call, not a per-frame path.
    zCamera.cpp         `zcam_flytime` and the 1/30 in zMain.cpp — animation
                        frame numbers converted to seconds. Unit conversions.

## Per-frame multiplicative damping

`x *= k` once per frame settles at a rate set by the frame rate. The fix is
`x *= xpow(k, 60.0f * dt)`, which is the codebase's own idiom — retail uses it
at `zFX.cpp:445` and `:467`.

Converted:

    zEntPlayer.cpp              vel.x, vel.z *= 0.96f   (jump)
    zEntPlayer.cpp              v->x, v->z *= 0.97f     (slick surface)
    zNPCGoalRobo.cpp            drot.angle *= 0.97f, *= 0.8f
    zNPCGoalRobo.cpp            ang_spinrate *= 0.8f, *= 0.99f
    zNPCGoalVillager.cpp        ang_spinrate *= 0.985f
    zNPCSupplement.cpp          npdata->vel *= 0.9f
    zEntPlayerBungeeState.cpp   rot_vel *= fixed.turn.decay
    zEntPlayerBungeeState.cpp   v *= fixed.horizontal.decay
    zEntPlayerBungeeState.cpp   roll_offset *= eh.camera.roll_decay

`zEntPlayerBungeeState.cpp`'s `cam_dir = start + (dir - start) * turn_speed` is
the same shape written as a lerp. The fraction that survives the frame is what
compounds, so it becomes `1 - xpow(1 - turn_speed, 60 * dt)`.

### A fifth shape: the one-pole filter

`zNPCTypeBossSandy.cpp` has

    jawLevel *= 0.9f;
    jawLevel = 0.1f * amp + jawLevel;

Rebasing only the `*=` leaves the input weight at 0.1 while the decay approaches
1, and the value runs away as the frame rate rises. Both coefficients have to
move together:

    F32 decay = xpow(0.9f, 60.0f * dt);
    jawLevel = (1.0f - decay) * amp + decay * jawLevel;

Any `x *= k;` immediately followed by `x += (1 - k) * input;` is this, not plain
damping.

## Frame counters read as time

`gFrameCount` is a frame counter, and some consumers read it as a clock.
`gGameSeconds` (`xDebug.h`, PC only) is the same tick measured in seconds,
incremented beside `gFrameCount` in `zGame.cpp`.

    zSurface.cpp        mode 1 UV animation was `isin(2 * gFrameCount * (1/60))`,
                        i.e. gFrameCount/60 as a time in seconds. Now gGameSeconds.
    zGame.cpp           `ostrich_delay`, ten frames of grace before the scene
                        counts as entered and the pad-removed dialog may appear.
                        Now a sixth of a second.
    zNPCHazard.cpp      `cnt_nextemit`, four sites. A trail particle every N
                        frames is an emission rate per frame. Now a `tmr_nextemit`
                        in seconds, at the same period. The seven `moreorless`
                        dividers in the same file are the same shape and are in
                        the emission-counted-in-frames section above.

`gFrameCount` itself is NOT replaced. The consumers that compare it for
equality — `xFXAura` stamps `ap->frame` in the simulation and the render draws
only what was stamped this frame — want the counter, and are correct with it
because the port runs one update per presented frame. That equality breaks the
moment simulation and rendering are decoupled, which is what happened the last
time the cap came off; it is not what happens here.

Checked and left alone:

    zEntPlayer.cpp:339      `gFrameCount - last_frame > 5`, a wand-bubble
                            debounce. All callers call it on consecutive frames
                            while the effect is live, so the delta is 1 at any
                            frame rate; between two separate uses of the wand the
                            gap exceeds five frames at any frame rate too.
    zNPCGoalRobo.cpp:7052   `cnt_nextfunfrag` counts robot DEATHS, not frames.
    xScrFx.cpp:421          `gFrameCount % 2` feeds gNumDistortionParticles,
                            which is dead code on the GameCube.

## A particle count rounded to nearest

`zNPCTypeKingJelly.cpp` sizes a burst as `S32 total = amount * dt * xurand() +
0.5f`. Below a sixtieth-second frame `amount * dt` never reaches the half that
rounds to one, so the count is zero every frame and the effect vanishes rather
than floods. A fractional accumulator feeding `total` does not fix it either:
the distribution loop underneath spends `j * total / (ring_size - 1)`, which
never reaches `total`, so a `total` of one emits nothing.

Both sites — the wave ring in `update_rings` and the tentacle zaps in
`update_tentacle_lightning` — now run on a fixed sixtieth-second step with the
remainder carried in `tmr_ringemit` and `tmr_zapemit`, PC-only members reset in
`zNPCKingJelly::Reset`. `dt` is clamped to 0.1 s in `zGame.cpp`, so the catch-up
loop runs at most six times.

## Matrix drift, again

`NPCHazard::TypData_RotMatApply` does `xMat3x3Mul(frame, mat, frame)` once per
frame — the pickups' shape, error accruing per multiply rather than per second.
The basis is now rebuilt from `at` after the multiply.

The reason it was not simply renormalised is that the frame might have carried a
scale. It does not. A hazard's scale lives in `mdl_hazard->Scale` and is applied
at render, and every writer of the frame stores an orthonormal basis:
`GrabModel` writes an identity euler, `TypData_RotMatSet` takes
`xMat3x3LookVec`/`xMat3x3Rot` output, and `ReconTarTar`, `ReconChuck` and the
third site at `zNPCHazard.cpp:3416` build theirs from a unit normal, a
normalised `NPCC_MakePerp` and a cross of the two.

## An emitter offset walked per frame

`zNPCTypeTiki.cpp`'s thunder cloud steps `t2` a quarter of a unit per frame
across a grid so consecutive emissions do not stack on one point.
`xParEmitterEmit` keeps its own fractional particle count, so with a `dt` window
a particle comes out every few frames rather than every frame, and a per-frame
walk aliases against that: at a steady frame rate the emissions land on a
sub-grid. `t2` now advances a quarter per sixtieth of a second. `t3` is the
carry digit and still advances a quarter per wrap of `t2`.

`loveyFloat` in the same file steps `t2` and `t3` the same way. Nothing reads
either, so it is left alone.

## Spinning glyphs

The icon that floats over a talkable NPC, the one over an NPC with a task, and
the stars over a stunned robot are all `NPCGlyph`s, and all three spun at a
constant angle per frame.

`NPCGlyph::RotSet(xVec3* ang, ...)` builds `rot_glyph` from an euler triple and
`NPCGlyph::RotAddDelta` does `xMat3x3Mul(frame, rot_glyph, frame)` on the model
frame. The three callers pass a per-FRAME delta: `DEG2RAD(3)` for
`NPC_GLYPH_TALK`, `DEG2RAD(-3)` for `NPC_GLYPH_TALKOTHER`, `DEG2RAD(2.1)` for
`NPC_GLYPH_DAZED`. Three degrees a frame is half a turn a second at 60 fps and
384 degrees a second at 128.

The euler `RotSet` now also records the angles as a rate a second
(`angrate_glyph`), and a PC-only `RotAddDelta(xMat3x3*, F32 dt)` steps
`angspin_glyph` by that rate and rebuilds the basis with `xMat3x3Euler`.
Rebuilding also removes the drift of composing a matrix into itself. The four
call sites — `zNPCGlyph.cpp`'s autospin branch and shiny `Timestep`,
`zNPCGoalVillager.cpp:285`, `zNPCTypeRobot.cpp`'s `SyncStunGlyph` — all have
`dt` in scope already.

When no rate was recorded the new overload calls the old one, so the matrix
`RotSet` overload and the shiny glyphs behave exactly as before. Only three
glyph types are ever acquired — `NPC_GLYPH_TALK`, `NPC_GLYPH_TALKOTHER` and
`NPC_GLYPH_DAZED`. The five shiny types and `NPC_GLYPH_FRIEND` have no
`GLYF_Acquire` caller, and `NPCGlyph::Reset` never initialises `rot_glyph`, so
a shiny glyph would multiply its frame by the zero matrix.

`NPCGlyph::Timestep`'s billboard branch for the talk glyphs is unreachable: it
needs bit 2 or bit 3 of `flg_glyph`, bit 2 is set only by `VelSet` and bit 3 by
nothing. The talk glyph free-spins rather than facing the camera, at any frame
rate.

## The pickups were already right

`zEntPickup_SceneUpdate` takes `elapsedSec` from `zSceneUpdate` and rotated by
`PI * dt`, so the rate was never wrong — the fix in the pickups section above
removes drift, not speed. Golden spatulas are ordinary `zEntPickup`s: both
`zEntPickup_RenderOne` (reached from the aura pass at `xFX.cpp:3270`) and
`zEntPickup_RenderList` (`zScene.cpp:3114`) copy `sPickupOrientation` into the
model matrix, skipping it only when the pickup has an anim or is already
collected. A reward spatula freezes its matrix in `zEntPickup_DoPickup` and does
not spin at all.

## A HUD shake counted in frames

`xhud::shake_motive_update` stashes a frame counter in `motive::context`, flips
the sign of the displacement every frame, decays the amplitude every fourth and
ends after fifty. `zHud.cpp`'s `ping_widget` uses it to jog a HUD widget when its
count changes.

It now runs on a fixed sixtieth-second step with the remainder carried in a
PC-only `motive::step_time`. Rebasing the amplitude decay alone would not help:
the four-frame sign pattern is the effect.

## Not resolved

### Additive accumulation without dt

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

`zEntPickup.cpp:1354`, which an earlier version of this document called the one
that looked real, is not. `ent->vel` is overwritten by `xVec3Copy` from a freshly
normalised direction three lines above, so `vel.y += 0.08f * ydiff` accumulates
nothing. The `vel2 > 2.0f` collect test in the same block is a "would step past
the player this frame" guard and correctly fires less often as frames get
shorter.

So roughly one in five of what the scan flags is worth changing, and which one
cannot be told without reading the surrounding function. Treating these as a
to-do list would overstate the work about fivefold.

Files with the most persistent-group hits, as a place to start reading:
`zSurface.cpp` (15), `zEntPlayer.cpp` (14), `zNPCSupplement.cpp` (12),
`xParCmd.cpp` (11), `xParEmitterType.cpp` (9), `zNPCTypeBossPatrick.cpp` (7),
`zNPCTypeTiki.cpp` (7).

### Known and left

    zEntHangable.cpp    `enabled = -2` counted back to zero is a two-frame
                        re-enable delay. Nothing reads `enabled`: the countdown
                        in zEntHangable_Update is its only reader, and `grabTimer`
                        beside it is written on dismount and decremented but never
                        tested either. The delay gates nothing at any frame rate.
    iFMV.cpp            the movie loop presents every iteration and decodes on a
                        timestamp, so with no cap AND vsync off it re-presents
                        the same frame thousands of times a second. The video
                        still plays at the right speed; it is wasted work, and
                        either setting on its own bounds it.

### Swept and clean

Recorded so the next sweep does not re-open them.

`xAnim.cpp` is time-based end to end: `Time += timeDelta * CurrentSpeed` from
`xModelUpdate(inst, dt)`, and blend progress is `BlendFactor * BlendRecip` where
`BlendRecip` is one over a number of SECONDS. Keyframes sample by absolute time
in `iAnimSKB.cpp` and `xMorph.cpp`; there is no frame index anywhere. The 30 fps
authoring rate appears only as load-time unit conversions in `zMain.cpp`.

`xScrFx.cpp` ages everything by `dt` — fade, letterbox, glare, the distortion
pool. `zSurface.cpp` proper is `dt` throughout. `zGust.cpp` is `dt` plus a
seconds timer. `zShrapnel.cpp`'s managers age by `lifetime -= dt` and integrate
the parabola analytically.

`zEntPlayerOOBState.cpp:1102` calls `xModelUpdate(model, 1.0f/1000.0f)` and is
NOT a defect — it is inside `grab_state_type::start()`, a one-shot.
`xFFXShakeUpdateEnt` derives its magnitude from an absolute timer and applies
only the delta.

A sweep for rotation advanced once per frame — `xMat3x3RMulRot*`, `xMat3x3Mul`
composing a matrix into itself, and `+=` on anything named angle, ang, rot, yaw,
spin, theta or phase — turned up the glyphs above and nothing else. Rejected,
with the reason:

    xEntMotion.cpp:808      the mech rotation is `speed * dt`, and the PEN
                            `xMat3x3Mul(modlrot, modlrot, &pshrot)` at :164 is
                            in the reset path, not per frame. The ORB mode
                            evaluates a position from `motion->t`
    zPlatform.cpp:669-689   the FM platform steps by `ds`, solved from `dt` and
                            the asset's accel/decel times. Teeter and paddle
                            derive their angles from `dt` and a timer too
    zNPCTypeBossSB2.cpp     the spinning platforms run `xAccelMove(ang, vel,
                            accel, dt, ...)`
    zShrapnel.cpp:1089      `xMat3x3Rot(&spin, axis, dt * angVel)`
    xEntBoulder.cpp:637     `xMat3x3Rot(&rotM, rotVec, angVel * dt)`
    zEntPlayer.cpp:6908     `sReticleRot += 8.0f * dt`
    zEntPlayer.cpp:8803     `sHitchAngle += 3.14f * dt`
    xFX.cpp:3011,3027       the aura pulse and spin, both `* dt`
    xHud.cpp:663            `rc.rot` comes from the asset and the motives, and
                            the linear, accelerate and delay motives all use dt
    xFont.cpp:3246          the rotation is a parsed markup argument
    zEntTrigger.cpp:24      one-time setup from `asset->ang`
    zEntTeleportBox.cpp     both eulers are built from a launch angle on an event
    zNPCTypeVillager.cpp:1651  `screenRot` is written once, to 1.0f
    zEntHangable.cpp        `ent->spin` is set to zero and never advanced
    zNPCGoalVillager.cpp:1089  `ang_spinrate += 16.0f` is an impulse in
                            NPCMessage, on taking side damage

`zUI.cpp:740`'s `ushift += 0.05f` in `zUIRenderAll` is a per-rendered-frame
counter with no `dt` anywhere in the function. Nothing in the tree reads
`ushift`, so it is left alone.

### Still open

`zNPCGoalVillager.cpp:346`'s `cnt_nextMedic` decrements per frame to grant a
health point — frame-rate dependent, but it is a cheat.

`zMain.cpp:1152`, the memory-card screen, calls `xPadUpdate(pad, 1.0f/60)`, so
rumble timers there age one console frame per iteration of an uncapped loop.
Nothing rumbles on that screen.

## Corrections found by re-auditing this branch

The golden spatula was cleared twice as "rate-correct" and was not. Both looks
checked `PI * dt` in

    xMat3x3MulRotC(Mat, Mat, 0, 1, 0, PI * dt + sSpatulaGrabbedSpinMult);

found it to be a proper rate, and stopped. The term beside it ramps at a tenth
per SECOND and is added raw to an angle whose unit is radians per FRAME. Reading
one operand of an expression is not reading the expression.

The work above was reviewed adversarially after the first play test. What that
turned up, recorded because each one was a plausible-looking mistake:

`zEntHangable.cpp` — the candle window is `1/30` applied EVERY frame, which is
retail asking for twice the emitter's authored rate. Converting it to `dt`
halved the flame and smoke at every frame rate including 60. It is `2.0f * dt`.
A constant window is not automatically one frame's worth; check how often the
call runs.

`zNPCTypeRobot.cpp` — the motorboat emits on SIX frames of sixteen, not five.
From `cnt_nextemit = 15` the decrement gives 14 down to 5 silent, because `< 5`
is false at 5, then 4, 3, 2, 1, 0 and -1 emitting. Count the reset frame.

`xpow(1 - k, 60 * dt)` is a NaN when `k` comes from level data above 1, and a
NaN never washes out of a position or a quaternion. Four sites read `turn_speed`
or `roll_speed` from an asset with no bound of their own. `xFrameApproach` in
`xMath.h` clamps before the power; the two bungee `decay` tweaks were already
clamped to [0,1] at load and did not need it.

`gGameSeconds` was `F32`. At a few thousand frames a second the addend is 3e-4
and an F32 accumulator stops advancing entirely past 8192 — a couple of hours of
play, after which the clock silently freezes. It is `F64`, and `zSurface` wraps
the angle before casting because a sine's whole turns carry no information.

`zNPCKingJelly`'s catch-up accumulators are zeroed by `Reset()` but not by the
constructor, and `RyzMemData::operator new` clears only the first four bytes. A
`while (t >= 1/60)` loop on a garbage accumulator is a hang, where the retail
line it replaced merely emitted a wrong count once. Both are clamped to 0.1 s,
which `dt` can never exceed anyway.

`zNPCGoalRobo.cpp`'s tubelet spin-down compares `drot.angle` against two
absolute thresholds. `drot.angle` is one FRAME's rotation, so the band traps it
at 5.24-5.76 radians a second only while a frame is a sixtieth of one — settled,
the tubelet spun four times as fast at 240 fps. The branch works in radians a
second on both sides now.

The claim that the `drot.angle` DECAY rebase was wrong was itself wrong:
`zNPCGoalRobo.cpp:3729` builds the value as `dt * -bonkSpinRate`, so it is a
rate times the frame and the `xpow` rebase is right.

## The second sweep

Everything above came out of the first pass and the play test that followed it.
A second pass went after three shapes the first had no scan for.

### Ribbons and streaks: joints laid per frame

Every ribbon in the game draws joints from ONE pool -- `joint_alloc.init(...,
32, 128)` at `xFX.cpp:2906`, so 4096 joints for the whole game -- and
`xFXRibbon::insert` evicts a ribbon's own tail when that pool is full. So a
ribbon that lays a joint per frame does not just get denser at 240 fps, it
shortens every other ribbon on screen with it.

    zEntCruiseBubble.cpp:1087   the wake's `samples <= 0` floor threw the carry
                                away and took a sample anyway. Carries the
                                unspent time now, so the frame that does sample
                                still spans the whole path
    zNPCTypeDutchman.cpp:3329   the eye scorch subdivides by distance and then
                                adds one more joint for the leftover. The
                                subdivided joints stay; the leftover is gated
    zNPCTypeBossSandy.cpp:2105  the Poseidome laser show, fixed in the first
                                pass, is the same shape -- two joints a frame,
                                each pair a separate beam

`xFXStreakUpdate` is the same thing one level up. It advances the head when
`elapsed > frequency`, and almost every streak starts with a frequency of `0.0f`
or `-1.0f`, both of which are always true. The head moves once a frame and the
fifty elements span fifty frames. The player's melee and spin trails and the
bubble wand are all in that group.

`zLasso.cpp`'s `fizzicalCenter`, `fizzicalNormal` and `fizzicalHonda` are FIR
filters over a five-slot ring pushed once per frame. Rebasing a coefficient
cannot fix a FIR window, so `zLasso_Update` runs at 60 Hz instead.

### Probability gates

A random draw against a constant, on a path that runs once per frame, is a rate
per frame. `xFrameEmitChance` rebases it. The first pass found the bubble pop;
the second found one more:

    zNPCTypeAmbient.cpp:494     jellyfish lightning, a twentieth chance a frame
                                through `xUtil_yesno` -- the indirection is why
                                a grep for `xurand` missed it. Two bolts a
                                flash out of a 48-bolt pool, so a shoal can
                                starve every other lightning effect in the scene

### Facing filters written as two calls

`x = 0.9*x + 0.1*target` does not look like `x *= 0.9f` when it is spelled

    xVec3SMul(&frame->mat.at, &model->Mat->at, 0.9f);
    xVec3AddScaled(&frame->mat.at, &newAt, 0.1f);

`xEntBeginUpdate` copies `model->Mat` into `frame->mat` and `xEntEndUpdate`
copies it back, so the value read is last frame's own output. Sandy does this
in all eight of her goals and turns four times as fast at 240 fps, which costs
the player the lead-in before a charge. `xEntBoulder.cpp:611` does it to the
boulder's spin axis and rate, which costs the skid after a deflection, and
`zEntPlayer.cpp:14359`'s `0.95f * hangDist + 0.2f` does it to the lasso swing
radius, which turns the rope into a rigid rod.

### Frame counters, again

    zNPCGoalDuplotron.cpp:213   `cnt_destruct = 120; // 2 seconds`. The whole
                                self-destruct body now runs at 60 Hz, which also
                                fixes the light strobe and the overheat smoke
                                throttle inside it
    zNPCHazard.cpp:1985         `cnt_skipcol` staggers a hazard's collision test
                                five or six frames apart to spread the load.
                                Counted in frames it is four times as many
                                chances to connect at 240 fps

## What holds this in place

Two things, because the work splits cleanly into what a test can reach and what
it cannot.

`fps_selftest` checks the rate helpers as PROPERTIES rather than values. The
interesting claim is not that the arithmetic is right, it is that at a sixtieth
of a second every helper gives back the constant it replaced -- so the default
build is the console's -- and that a second of game time costs the same at 60
fps as at 3000. It runs a second of damping, approach, emission and pop-chance
at 60, 120, 144, 240, 1000 and 3000 fps and compares the totals, plus the
bubble-pop loop driven through the real `xurand` rather than the closed form.
31 checks, a sixth of a second. Breaking `60.0f * dt` in `xFrameApproach` trips
seven of them.

It is its own target rather than a case in `pc_selftest` because linking
`xMath.cpp` needs `range_limit<F32>`, which CodeWarrior placed in
`xCamera.cpp` -- the weak-inline problem that is also why `pc_selftest` links no
game math at all.

`tools/fpsdep.py` covers the rest. Sixty-odd rebased sites are in game code that
wants a scene, a model and a player before it will run, so none of them are
unit-testable. What is testable is that no NEW one appears: the four shapes are
mechanically recognisable, every known site is recorded in `tools/fpsdep.json`,
and anything not in that file fails. It scans the PC arm only, so a fixed site
shows its guarded line and not the retail line beside it -- which is why the
nine `damping` hits it reports are all known false positives and every real one
is invisible.

The baseline is a record of what has been READ and judged, not of what is
correct. Roughly one in five of what the scan finds is worth changing, and the
list below is how to tell which.

Both are `ctest` cases in `build-pc`.

## The third sweep

After the second, two shapes were still unswept, and both turned out to be
populated.

### A bounded history pushed once per frame

`missle_record` is a `fixed_queue<missle_record_data, 127>` holding the cruise
bubble's flight path, one sample a frame. 127 slots is 2.1 seconds on console
and a quarter of that at 240 fps. The consumer is the explosion cinematic: when
a missile detonates more than ten units away the camera drifts from six to
eight world units BACK ALONG the missile's own path, and `eval_missle_path`
does not clamp its lerp. Past about 190 fps the record is shorter than eight
units and the camera is extrapolated off the oldest pair of samples, through
whatever the missile flew past. The roll is worse: `t` reaches a thousand
uncapped, so one frame's roll becomes a couple of radians.

`zGame`'s frame-time boxcar averages the last two FRAMES. That is a thirtieth
of a second on console and an eighth of that at 240 fps -- the one filter
between an uneven frame and every system's `dt`, and it stops filtering exactly
where jitter starts to matter most, since a millisecond hitch is a sixteenth of
a console frame and a quarter of a 240 fps one.

Every other bounded container in the tree is event-driven, seconds-gated or
distance-gated. `containers.h` holds the only ring primitives, and all nine
instantiations were enumerated. The frame-amortised work queues -- `zLOD`'s
round-robin, the shadow caches -- are the INVERSE of this shape: a higher frame
rate refreshes them sooner, which is the safe direction.

### A quantity whose unit is a frame

Shape 9, above. What it caught:

    zEntPickup.cpp:1266     the grabbed golden spatula. See the correction below
    zNPCTypeBossSandy.cpp   the limb springs' node velocity, +-0.05 of a limb
                            per frame, added on the RENDER path
    zEntHangable.cpp:341    the candle test compares a per-frame displacement
                            against a fixed band, so above 60 fps the band never
                            opens and the candles stay lit
    zEntPlayer.cpp:7889     the downhill stick-down takes the square root of a
                            per-frame distance, so the pull per second grows
                            with the square root of the frame rate
    xEnt.cpp:1695           step-up is gated on a per-frame displacement against
                            0.001, which is 3.2 units a second at 3200 fps
    zEntPlayer.cpp:359      the wand bubbles inherit the wand's displacement
                            since the last spawn as a velocity in units a second

### A regression this branch introduced

`fizzicalSlack` consumes a rope-length delta that `zLasso_Render` stamps every
RENDERED frame, but the update it runs from is now gated to 60 Hz for the FIR
filters beside it. At 240 fps three of every four deltas were overwritten before
it saw them, so the sum telescoped to a quarter of the real length change while
the drain still took the whole window, and the rope read taut. Gating an update
does not gate what writes into it.

## A bound and a lag that multiply

Reported from play: the Poseidome camera is really slow, and only above 60 fps.

`xBinaryCamera::update` runs the Robo-Sandy and Robo-Patrick fights.
`zCameraDisableTracking(CO_BOSS)` hands it the camera for the whole battle, so
none of the `pcur`/`pgoal` machinery above is live there. Every filter in it is
written against a variable frame time and every one of them is right on its own.
Three lines together are not:

    F32 max_yaw_diff = cfg.max_yaw_vel * dt;              // the bound
    F32 sloc = 1.0f - xexp(-cfg.move_speed * dt);         // the lag
    F32 yaw_start = xatan2(B.x - A.x, B.z - A.z);         // re-derived from A

The bound puts the goal at most `max_yaw_vel * dt` ahead of where the camera is
now. The camera then closes `sloc` of that goal, which is itself proportional to
`dt`. And `yaw_start` comes back from the camera's own position next frame, so
the part it did not close is not carried over - it is simply gone. The angle
turned per frame goes as `dt * dt` and the rate per second as `dt`:

    retail    60: 88.5   144: 38.5   240: 23.4   1000: 5.7   3000: 1.9  deg/s
    rebased   60: 88.5   144: 88.5   240: 88.5   1000: 88.5  3000: 88.6

The fix scales the bound by `sloc(1/60) / sloc(dt)`, which is exactly 1 at a
console frame. `stick_offset.x` is rebased with it: its target is written
`stick_yaw_vel * stick.offset.x * dt`, and since `stick_yaw_vel` and
`max_yaw_vel` are both 10 that lands full deflection exactly on the bound at any
frame rate. Rebase the bound alone and the stick stops reaching it above 60 fps.

Rebasing the stick ALONE does nothing at all, which is worth knowing before
reading a site like this: the bound was the binding constraint, so the first
attempt shipped, changed no behaviour anybody could feel, and had to come back
out. Two per-frame quantities in series are one defect, not two, and neither
line is wrong where it stands.

`fpsdep.py` has no shape for this and cannot get one. Both lines already carry a
`dt`, correctly; what is wrong is that they compose. The scan looks for a missing
`dt`, and the fifth shape - the one-pole - looks at a single statement. `fps_selftest`
covers the arithmetic instead, running a second of the loop at six frame rates
against Sandy's own config, and the baseline covers the two rebased lines.

## Telling a real site from a false one

Nine things wear the same clothes, in rough order of how often they turn up:

1. A RATE. Multiplied by a timestep, ACCUMULATES into persistent state. Needs
   `dt`. Example: the hazard spin rates.
2. A UNIT CONVERSION. A constant that happens to be 1/60 but converts units, with
   nothing accumulating. Leave alone. Example: `pitch_s`, and every emitter rate
   set to a multiple of 60 next to its call.
3. A WRAP. `-= 360.0f` or `+= 1.0f` to bring a value back into range, inside an
   `if`. Leave alone. Example: the `zSurface` UV wrap.
4. A ONE-SHOT. Runs on an event, not every frame, and happens to sit in a
   function that takes `dt`. Example: particle emission spread.
5. A ONE-POLE FILTER. A decay and an input weight that sum to one, on two
   adjacent lines. Both coefficients move together or the value runs away.
   Example: `jawLevel` in `zNPCTypeBossSandy.cpp`.
6. AN EPSILON GUARD. `if (dt < someSmallNumber) return;`, written when the
   shortest possible frame was a sixtieth of a second. Uncapped the guard fires
   every frame and the system it protects stops advancing. Example:
   `xFX.cpp:364`, which stalled the whole ring pool at anything over 1000 fps.
   The rest of the codebase's guards are at 1e-5, which is 100,000 fps and out
   of reach; that one was at 1e-3.
7. A BOUNDED HISTORY. A fixed number of slots -- a ribbon's joint queue, a
   streak's fifty elements, a FIR filter's five-sample ring -- pushed once per
   frame. The window is then measured in frames, so the trail gets shorter and
   the filter less smooth as the frame rate rises. No coefficient can be
   rebased to fix this; the samples have to arrive at a fixed rate. Examples:
   `xFXStreakUpdate`, `zLasso_Update`, the cruise bubble wake.
9. A QUANTITY WHOSE UNIT IS A FRAME. Not a rate against a wrong constant --
   a value that IS "per frame", so every use of it is wrong anywhere but 60
   fps. Two spellings: a delta between this frame and last used as a velocity
   or compared against a threshold (`d = pos - old_pos`, then `if (d > 20)`),
   and a per-frame increment written in one function and added in another
   (`vel1 = 0.05f` at goal entry, `node1 += vel1` in the renderer). The write
   and the read are usually in different functions, which is what hides it.
   Examples: the chandelier's candle test, Sandy's limb springs.
8. AN ACCUMULATOR NEVER TAKEN BACK. `x += dt; if (x < period) return;` with no
   `x -= period` anywhere. It reads as a rate limiter and is one exactly once,
   after which the gate stays open and the body runs every frame. Example:
   `sSteamAnimTime` in `zParPTank.cpp`, which made steam die in eight host
   frames rather than eight sixtieths of a second.

What separates 1 from 2 is whether the value survives the frame. If it is reset
at the top of the function, or consumed and discarded, it is integrating nothing
and `dt` does not belong in it.

"Survives the frame" is not the same as "is a member". The camera yaw writes
`cam->pcur`, which is a member and looks persistent, and the write is thrown
away at the top of the next frame by `xCam_worldtocyl` re-deriving it from the
position. Follow the value to its next READ, not to its declaration. If
something recomputes it from elsewhere before anything reads the accumulated
part, it is not accumulating.

## One thing the cap is not for

`zGame.cpp` substitutes 1/60 for any frame measured under ten microseconds.
That is above 100,000 fps and was never reached in testing, so it is not the
cause of anything observed. Earlier notes in this repo blamed the spinning
pickups on it. That was wrong. The real cause is in the pickups section above.

## Reproducing the candidate lists

Two scans, both lead generators. Neither output is a defect list.

The additive scan walks `src/SB/Game` and `src/SB/Core/x`, tracks brace depth to
find functions taking `F32 dt`, and reports lines inside them using `+=`, `-=` or
`*=` against a float literal without mentioning `dt`.

The counter scan does the same walk and reports `++` or `--` on a member or a
file static — a counter ticked once per frame measures frames, not seconds. It
yields 41 leads, of which the `cnt_nextemit` group and `ostrich_delay` were real
and the rest count events (hit points, bounces, misses, deaths).
