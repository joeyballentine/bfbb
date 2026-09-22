# Frame rates other than 60

Retail ran at a fixed 60 fps, and much of its code does something once per
frame that should happen a fixed number of times per second. The PC port runs
at any frame rate. The fixes below make those sites per-second on the PC build.

## Frame pacing

### Settings

`config.ini`'s `video.framerate` takes a number of frames a second, `display`
for the monitor's refresh rate, or `0`/`off` for no cap. `video.vsync` decides
separately whether the present waits for the display. The defaults are 60 and
vsync on, which is what the GameCube's video interface gave the game.
`ApplyDisplayRateConfig` in `iSystem.cpp` reads both after the window opens.
If `display` cannot read the refresh rate, it leaves the rate uncapped.

The two settings do separate jobs:

- `iWindowPaceFrame` (`iWindowSDL.cpp`) holds the cap. It advances a deadline
  by one frame period and sleeps to it with `SDL_DelayPrecise`. A frame that
  overran its deadline drops it and returns without sleeping, so one slow frame
  does not become a burst of fast ones. `iWindowSetFrameRate` resets the
  deadline.
- `RwCameraShowRaster` (`rw/camera.cpp`) decides the flip flag. Retail passes
  `rwRASTERFLIPDONTWAIT`. The port sets or clears `rwRASTERFLIPWAITVSYNC` from
  `iWindowGetVSync()`, then calls `iWindowPaceFrame`.

Vsync alone runs at the monitor's rate. A cap alone still tears.

Loops that draw nothing call `iVSync` (`iSystem.cpp`), which paces to a fixed
60 Hz with the same deadline logic.

### dt

`dt` is the real frame time. Uncapped, it is well under a millisecond, not
the sixtieth of a second the code was written against. The main loop in
`zGame.cpp` builds it:

- `iTimeDiffSec` between this frame and the last. `iTimeSuspend` and
  `iTimeResume` stop the clock while an Android app is in the background, so
  the first frame back does not carry the whole absence.
- With `sHackSmoothedUpdate`, a moving average. Retail averages the last two
  frames. The PC arm averages the newest samples that cover a thirtieth of a
  second, from a 128-slot ring. At 60 fps that is the same two samples in the
  same order.
- Clamped. A frame under 1e-5 s becomes 1/60, and a frame over 0.1 s becomes
  0.1. The lower clamp is 100,000 fps and is not reached in practice.
- Written to `globals.update_dt` and passed down the update tree.

### gFrameCount and gGameSeconds

`gFrameCount` counts rendered frames. `gGameSeconds` (`xDebug.h`, PC only) is
the same tick in seconds: `zGame.cpp` adds `sTimeElapsed` to it beside
`gFrameCount++`. It is `F64`. An `F32` accumulator stops advancing past 8192 s
when the addend is 3e-4.

Consumers that read `gFrameCount` as a clock use `gGameSeconds`. Consumers
that compare it for equality keep the counter. `xFX.cpp`'s aura stamps
`ap->frame = gFrameCount` in the update and draws only what was stamped this
frame. That is correct because the port runs one update per presented frame.
It breaks if simulation and rendering are decoupled.

### The GameCube build

Every change is inside `#ifdef PLATFORM_PC`. The GameCube arm of every touched
function is retail's code, and the GameCube build stays byte identical. Where a
split would cost a match, keep retail's exact spelling in the `#else`. For
example, `--x < 0` becomes a decrement and a compare only on the PC arm
(`zNPCSlick::SlipSlidenAway`, `zNPCGoalAlertTubelet::EmitSteam`).

New fields on a game struct go at the end, behind `PLATFORM_PC`, when the
struct's size appears in a table. `xEntBoulder` is one: `zScene.cpp` uses
`sizeof(xEntBoulder)`.

## Rate helpers

Each helper returns retail's constant at a sixtieth of a second, so a 60 fps
build behaves like the console. `fps_selftest` checks that property for each
one.

### `xpow(k, 60.0f * dt)`

Rebases a per-frame multiplier. Retail uses it itself at `zFX.cpp:460` and
`:482`. Use it for `x *= k` where `k` is a known constant in [0, 1].

### `xFrameApproach(F32 k, F32 dt)` (`xMath.h`)

Returns `1 - xpow(1 - k, 60 * dt)`: the fraction of the remaining distance a
frame of `dt` closes, given that a console frame closed `k`. Use it for
`x += k * (target - x)` and its spellings. It clamps `k` to [0, 1] before the
power. `xpow` of a negative base with a fractional exponent is a NaN, and
several callers read `k` from level data with no bound.

### `xFrameEmitCount(F32 count, F32 dt)` (`xMath.h`)

Converts a count per console frame into a count for a frame of `dt`. It
returns the whole part of `count * 60 * dt` and adds one more with probability
equal to the fraction. The caller keeps no state. Use it where retail spawns N
things every frame.

### `xFrameEmitChance(F32 chance, F32 dt)` (`xMath.h`)

Converts a probability per console frame into one for a frame of `dt`:
`1 - xpow(1 - chance, 60 * dt)`, clamped to [0, 1]. Use it for a random draw
against a constant on a per-frame path. Compute it once outside a loop over
particles.

### A fixed step with a carried remainder

For code whose behaviour is a sequence of frames rather than a rate, run it at
60 Hz:

    tmr += dt;
    S32 steps = (S32)(60.0f * tmr);
    if (steps > 4) steps = 4;         // or clamp tmr to 0.1
    tmr -= steps * (1.0f / 60.0f);
    for (S32 i = 0; i < steps; i++) { ...retail body... }

Carry the remainder. Zeroing it drops a tick whenever a frame is slightly
short. Cap the steps or clamp the accumulator so a hitch does not spend a
backlog in one frame. Where a counter is also reset by other code (`Enter`,
`Resume`), keep the counter and subtract `steps` from it rather than replacing
it with a timer.

Use this for frame-counted animation (HUD shake), FIR filters and fixed-slot
histories, collision that depends on step size (boulders), and rounding that
fails at small `dt` (King Jelly).

### `xParEmitterEmitCustom(..., F32 par_frames)` (`xParEmitter.h`, PC only)

See *Particle system* below. `par_frames` is how many console frames the
caller's window stands for. It defaults to 1. `xParFrameStep` in
`xParEmitter.cpp` turns it into the birth-velocity step.

## Defect classes

### Emission counted in frames

A spawn that runs every frame, every N frames, or on a per-frame coin flip is a
rate per frame. Fix with `xFrameEmitCount`, `xFrameEmitChance`, or a seconds
timer at the same period.

Converted:

    xEntBoulder.cpp         the bubble bowl trail, a tenth of the speed a frame
    xClimate.cpp            the PTank snow path. zParPTankSpawnSnow adds flakes
                            outright, so a count a frame set the population
    zThrown.cpp             the thrown-object trail
    zEntPlayer.cpp          the springboard, stun, goo, Patrick melee, tongue
                            and slide bubble trails
    zEntPlayer.cpp          zEntPlayer_SpawnWandBubbles, the bubble wand trail.
                            Three a frame for the bubble spin, one for the Bbash
                            and Bbounce windups. Scaled inside the function,
                            since all three callers run once a frame and none
                            passes a window. A frame that wins no bubble stamps
                            last_frame and last_time without moving last_center,
                            so the next spawn spreads over the whole path.
                            Passing zero would hit the `count != 0` default and
                            emit three
    zEntPlayer.cpp          MeleeAttackBoundCollide's zFX_SpawnBubbleHit, ten a
                            frame while a target is inside the bound. All three
                            callers (slide, bubble spin wand, Sandy's melee)
                            repeat every frame. The one-shot users of
                            zFX_SpawnBubbleHit are separate call sites, so the
                            scale goes at this call, not inside the function
    zEntPlayer.cpp          the bubble bash and bounce bone contrails, one a
                            frame at each of four bones. Each bone rolls its own
                            count so the trail keeps its spread
    zEntPlayer.cpp          Patrick's StunLand slam, a 24-bubble ring every
                            frame for the first quarter second. The number of
                            RINGS scales, not the bubbles in one, because the
                            ring spreads its angles over its own count
    zScene.cpp              the menu bubbles, a 1.5% chance a frame
    zEntSimpleObj.cpp       the blob burst
    zShrapnel.cpp           the projectile trail
    zNPCHazard.cpp          seven `moreorless` frame dividers, now seconds
                            timers at the same period
    zNPCHazard.cpp          `cnt_nextemit`, four sites, now `tmr_nextemit`
    zNPCHazard.cpp          DeathStar, twenty a frame for the first sixth of
                            the hazard's life
    zNPCTypeRobot.cpp       DoFX_Motorboat, a bubble on six of every sixteen
                            frames. From cnt_nextemit = 15, 14 down to 5 are
                            silent and 4 to -1 emit
    zNPCTypeRobot.cpp       the chomper's breath wisps (BreathTrail) and the
                            slick's oil vapours (`moreorless`)
    zNPCGoalRobo.cpp        the AlertGlove whirlwind (`tmr_nextemit`), the
                            death-ray tip, both tube-dying propel trails
    zNPCGoalRobo.cpp        the tubelet's steam jet, a particle per call with
                            no gate
    zNPCGoalRobo.cpp        FurryFlurry's cone. `moreorless` is reset to -1, so
                            the cone went out every frame. Scales the number of
                            cones, like the StunLand ring
    zNPCGoalAmbient.cpp     the bumped-jellyfish trail
    zNPCTypeBossPatrick.cpp the glob trail
    zNPCTypeBossSandy.cpp   the Poseidome laser show, two ribbon joints a frame
    zNPCFXCinematic.cpp     see below

`zNPCFXCinematic.cpp`'s callbacks take no `dt`. They read `globals.update_dt`,
which `zGame.cpp` writes once a frame. `NCIN_BubbleTrail_AR` already builds its
accumulator from it. The per-frame sites scaled from it are `NCIN_BubWipe`
(fifty a frame; its buffer is sized from the scaled count), `NCIN_BubHit`'s
three-a-frame tail, `NCIN_BubTrailBone_AR`, `NCIN_SleepyDRay_AR`,
`NCIN_MidFish_AR`, `NCIN_BombTrail_AR`, `NCIN_BoneTrail_AR` and
`NCIN_HookRecoil_AR`.

`flg_stat & 2` is the effect's first-frame flag. `zNPCFXCutscene` clears it
after the first `cb_fxupd` call. Anything behind it is a one-shot and keeps a
fixed count: `NCIN_BubSlam`, `NCIN_SleepyDRay_Upd`, `NCIN_ShieldPop`, and the
sixteen bubbles at the top of `NCIN_BubHit`.

Left alone:

    zNPCSupplement.cpp      a burst inside a nested loop, run once on an event
    zNPCTypeAmbient.cpp     zNPCJelly::ActLikeOctopus, run once from a goal Exit
    zNPCHazard.cpp          ReconTarTar and the other Recon/Kick bursts
    zEntCruiseBubble.cpp    `shared.trail.bubbles += dt * bubble_rate` already
    zFX.cpp                 update_popper and entrail_data::update already

#### The bubble pool

`zParPTankBubbleUpdate` ages `life` by `dt`, moves by `vel * dt`, adds
buoyancy as `3.0f * dt` and damps with `xpow(0.95f, 60 * dt)`. A bubble between
1.2 and 0.5 seconds of life left has a 4% chance of popping each frame. That is
now `xFrameEmitChance(0.04f, dt)`. Over the 0.7 s window the console makes 42
rolls and 18% survive (0.96^42). At 240 fps retail's per-frame roll makes 168
rolls and 0.1% survive.

The pool holds 0x300 bubbles (0x10 for the menu tank). `zParPTankSpawnBubbles`
truncates a request that would overflow it. The live count is spawn rate times
the 1.75 s life, so a per-second rate saturates it as often as the console did.
A per-frame rate does not: StunLand alone asks for 1440 bubbles in a quarter
second at 240 fps.

#### An emitter offset walked per frame

`zNPCTypeTiki.cpp`'s thunder cloud steps `t2` a quarter unit per frame across a
grid so consecutive emissions do not stack. With a `dt` window a particle comes
out every few frames, and a per-frame walk aliases against that onto a
sub-grid. `t2` now advances a quarter per sixtieth of a second (`15 * dt`).
`t3` is the carry digit and advances a quarter per wrap of `t2`. `loveyFloat`
walks `t2` and `t3` the same way. Nothing reads either, so it is unchanged.

### Particle system

#### The emit window

`xParEmitterEmitCustom(xParEmitter*, F32 dt, ...)` and
`zParPTankConvertEmitRate(xParEmitter*, F32 dt)` take a time window and emit
that much worth of particles. A constant there emits a fixed amount per frame
when the call runs every frame.

Passed `dt`:

    zEntPickup.cpp          the three shiny-sparkle sites
    zEntPlayer.cpp          gEmitBFX
    zEntHangable.cpp        the chandelier candle flame and smoke, as 2 * dt
    zNPCTypeDutchman.cpp    the beam light
    zNPCTypeTiki.cpp        the thunder cloud

The candle window is `1/30` every frame, which is two console frames' worth:
retail asks for twice the emitter's authored rate. It passes `2.0f * dt`.
Check how often a call runs before reading a constant window as one frame.

`zEntPlayer.cpp`'s stank breath is behind a `sLastInvulnEmit > 0.02f` time
gate, so it fires at about 50 Hz, not per frame. It passes half the elapsed
time the gate measured, which is `1/60` at 60 fps and a constant count per
second above it.

#### The emitter core

`xParEmitterEmit` turns a rate and a window into a count, and
`zParPTankConvertEmitRate` copies the first half of it. Both are correct:

    pe->rate_fraction += rate * emit_dt;
    count = floorf(pe->rate_fraction);
    pe->rate_fraction -= count;

The count floors, there is no per-call minimum, and the remainder carries in
`rate_fraction`, which is reset only in `xParEmitterInit`. Halving the window
doubles the calls and halves each one's share. Shared emitters do not bias the
count either: twenty pickups calling `gEmitShinySparkles` add
`sum(rate_i) * dt` a frame however the frame is divided. Which caller's
position gets the particle when the accumulator crosses an integer depends on
call order, at any frame rate.

`rate_time` is a phase in seconds, wrapped modulo `prop->rate.freq`.
`xParGroupAnimate` ages life, colour and size by `dt`.

#### Birth velocity

`xParEmitterEmit(pe, emit_dt, par_dt)` takes two windows. `emit_dt` buys
particles. `par_dt` scales the birth velocity, because `xPar::m_vel` is a
displacement per frame: `xParCmdVelocityApply_Update` adds it to the position
with no `dt`. Retail passes the same number for both, which is right only
while the window is the frame.

On the PC arm `xParEmitterEmitCustom` keeps the caller's window for the count
and sizes the step from `globals.update_dt` times `par_frames`. A one-shot
burst sized at 1/60 therefore leaves at its console speed at any frame rate.
Four callers pass a thirtieth and `par_frames = 2.0f`: `zEntHangable.cpp`
(candles and `zEntHangableMountFX`), `zGust.cpp`'s debris, and the
`eEventEmit` handler in `xParEmitter.cpp`.

The Dutchman's beam subdivides its frame and passes the sub-step. On console
its plasma and sparks slowed as the beam swept faster. On PC they do not. The
difference shows only while the beam covers more than one and a half segment
widths in a frame.

#### Particle commands

`m_vel` is a displacement per frame, and the emitter types set it as
`asset_vel * par_dt` at birth to match. The acceleration commands add
`acc * dt` to that per-frame displacement, which is an acceleration of
`acc / dt` a second. They carry an extra `60 * dt`:

    xParCmdFollow_Update, xParCmdOrbitPoint_Update, xParCmdOrbitLine_Update,
    xParCmdAccelerate_Update, xParCmdApplyWind_Update, xParCmd_Shaper_Update

`xParCmdKillSlow_Update` compares `m_vel` squared against a limit scaled by one
`dt`, so the sides scale apart. It takes the same factor.

Correct as they stand: `xParCmd_DampenSpeed_Update` and the `damp` term in
`xParCmd_Shaper_Update` scale `m_vel` by a fraction of itself.
`xParCmdRandomVelocityPar_Update` rotates `m_vel` by `cmd->x * dt` radians.

### Hardcoded 1/60 used as a timestep

A `1/60` that advances persistent state is a timestep. Replace it with `dt`.

    zNPCHazard.cpp          the spin group. The four ConfigHelper sites, the
                            ROBOBITS `ang_spin *= 1/60` and the in-function
                            xVec3SMulBy fed one matrix that Timestep applied
                            once a frame. The rate is now `ang_spinRate` in
                            radians a second and the delta is rebuilt from it
                            and dt
    zNPCSupplement.cpp      useFixedTimestepForSpiral. It ages tmr_remain and
                            integrates `pos += vel * ts`
    zNPCTypeRobot.cpp       `NPCC_TmrCycle(&tmr_cycle, 1/60, 2.63f)`, the
                            sleepy night light. Every other caller passes dt

Unit conversions and one-shots that stay:

    zNPCHazard.cpp          `tym_end += 1/60`, padding past the lifespan clamp
                            so a swept sphere is not degenerate. dt would
                            shrink it and break the sweep
    zNPCSupplement.cpp      `info->freq = 1/60`, a period in seconds consumed
                            by xFXStreak as `elapsed >= frequency`
    zEntPlayer.cpp          the `update_dt` / `last_update_dt` initialisers,
                            written before any reader
    zNPCMgr.cpp             BackdoorUpdateAllNPCsOnce, a one-shot catch-up
    zCamera.cpp, zMain.cpp  `zcam_flytime` and the 1/30 conversions: animation
                            frame numbers to seconds
    zEntPlayerOOBState.cpp  `xModelUpdate(model, 1/1000)` inside
                            grab_state_type::start(), a one-shot

### Per-frame multiplicative damping

`x *= k` once a frame settles at a rate set by the frame rate. The fix is
`x *= xpow(k, 60.0f * dt)`.

    zEntPlayer.cpp              vel.x, vel.z *= 0.96f   (jump)
    zEntPlayer.cpp              v->x, v->z *= 0.97f     (slick surface)
    zEntHangable.cpp            xVec3SMul(&ent->vel, &ent->vel, 0.97f)
    zNPCGoalRobo.cpp            drot.angle *= 0.97f, *= 0.8f
    zNPCGoalRobo.cpp            ang_spinrate *= 0.8f, *= 0.99f
    zNPCGoalVillager.cpp        ang_spinrate *= 0.985f
    zNPCSupplement.cpp          npdata->vel *= 0.9f, and seven spellings of
                                npdata->vel *= fac_keep
    zNPCTypeKingJelly.cpp       vel *= tweak.vel_decay
    zEntPlayerBungeeState.cpp   rot_vel *= fixed.turn.decay
    zEntPlayerBungeeState.cpp   v *= fixed.horizontal.decay
    zEntPlayerBungeeState.cpp   roll_offset *= eh.camera.roll_decay

The multiplier is not always a literal and the multiply is not always `*=`.
`fpsdep.py`'s damping pattern matches all three spellings above.

Check what the value is before rebasing. `zNPCGoalRobo.cpp`'s `drot.angle`
decay is right to rebase because the value is built as `dt * -bonkSpinRate`,
a rate times the frame.

### Exponential approach and facing filters

`x += k * (target - x)` once a frame is the same defect written as a lerp. The
fraction that survives the frame is what compounds. Fix with
`xFrameApproach(k, dt)`.

    zEntPlayerBungeeState.cpp   cam_dir turn lerp (turn_speed), roll offset
    zEntCruiseBubble.cpp        player aim turn_speed, missile engine pitch,
                                camera aim turn_speed
    zEntPlayer.cpp              SlideTrackLean, 4% a frame
    zEntPlayer.cpp              the lasso swing radius, `0.95f * hangDist +
                                0.2f`. Unfixed, the rope becomes a rigid rod
    zNPCTypeBossSandy.cpp       facing, in all eight goals
    xEntBoulder.cpp             the spin axis and rate close on the contact
                                values by `stickiness`

The facing filter is often two calls:

    xVec3SMul(&frame->mat.at, &model->Mat->at, 0.9f);
    xVec3AddScaled(&frame->mat.at, &newAt, 0.1f);

`xEntBeginUpdate` copies `model->Mat` into `frame->mat` and `xEntEndUpdate`
copies it back, so the value read is last frame's output. Unfixed, Sandy turns
four times as fast at 240 fps and the player loses the lead-in before a
charge.

Four sites read `turn_speed` or `roll_speed` from an asset with no bound. They
go through `xFrameApproach` for its clamp. The two bungee `decay` tweaks are
clamped to [0, 1] at load.

### One-pole filters

    jawLevel *= 0.9f;
    jawLevel = 0.1f * amp + jawLevel;

Rebasing only the `*=` leaves the input weight at 0.1 while the decay
approaches 1, and the value runs away as the frame rate rises. Move both
coefficients together:

    F32 decay = xpow(0.9f, 60.0f * dt);
    jawLevel = (1.0f - decay) * amp + decay * jawLevel;

Any `x *= k;` followed by `x += (1 - k) * input;` is this shape.

    zNPCTypeBossSandy.cpp   jawLevel
    zEntPlayer.cpp          PredictCurrVel and the turn-rate filter beside it,
                            read by every NPC that leads its aim through
                            zEntPlayer_PredictPos
    zEntPlayer.cpp          the lasso camera factor, 0.8/0.2

### Frame counters read as time

A counter ticked once a frame measures frames. Fix with `gGameSeconds`, a
seconds timer at the same period, or a fixed step that subtracts whole
sixtieths from the counter.

    zSurface.cpp            mode 1 UV animation, `isin(2 * gFrameCount / 60)`.
                            Now gGameSeconds, wrapped with fmod before the cast
    zGame.cpp               ostrich_delay, ten frames of grace before the
                            pad-removed dialog may appear. Now a sixth of a
                            second
    zEntPlayer.cpp          the wand bubbles' five-frame restart gap. Now
                            5/60 s of gGameSeconds
    zNPCGoalDuplotron.cpp   `cnt_destruct = 120; // 2 seconds`. The body runs
                            at 60 Hz with the remainder carried in
                            tmr_destruct, which also fixes the light strobe and
                            the overheat smoke throttle inside it
    zNPCHazard.cpp          cnt_skipcol staggers the collision test five or six
                            frames apart. Counted in frames a hazard gets four
                            times the chances to connect at 240 fps. Now
                            tmr_skipcol
    zNPCGoalRobo.cpp        cnt_nextlos, the Fodder death ray's line-of-sight
                            recheck. cnt_inContact steps inside the block it
                            gates, so the ray damaged the player about 2.4x
                            faster at 144 fps. Enter, Resume and the warm-up
                            path zero the counter to force a check, so it stays
                            a counter and tmr_nextlos spends whole sixtieths
                            against it
    zMain.cpp               the copyright screen, 180 fields. Now three seconds
                            by iTimeDiffSec on PC

Left alone:

    zNPCGoalRobo.cpp        cnt_nextfunfrag counts robot deaths
    xScrFx.cpp              `gFrameCount % 2` calls xScrFxDistortionAdd, whose
                            body is empty

### Probability gates

A random draw against a constant on a per-frame path is a rate per frame. Fix
with `xFrameEmitChance`.

    zParPTank.cpp           the bubble early pop
    zScene.cpp              the menu bubbles
    zEntSimpleObj.cpp       the blob burst
    zNPCTypeAmbient.cpp     jellyfish lightning, a twentieth chance a frame
                            through xUtil_yesno. Two bolts a flash from a
                            48-bolt pool, so a shoal could starve every other
                            lightning effect in the scene

Look for draws through wrappers like `xUtil_yesno` as well as bare `xurand`.

### Bounded histories pushed once per frame

A fixed number of slots with one sample pushed per frame spans a window
measured in frames. No coefficient can be rebased to fix it. The samples have
to arrive at a fixed rate.

    xFX.cpp                 xFXStreakUpdate advances the head when
                            `elapsed > frequency`. Most streaks start with a
                            frequency of 0 or -1, so the head moved every frame
                            and the fifty elements spanned fifty frames. A
                            frequency <= 0 now means 1/60, with the remainder
                            carried. The player's melee and spin trails and the
                            bubble wand are in this group
    zLasso.cpp              fizzicalCenter, fizzicalNormal and fizzicalHonda
                            are FIR filters over a five-slot ring. zLasso_Update
                            runs at 60 Hz
    zEntCruiseBubble.cpp    missle_record, a fixed_queue of 127 flight samples:
                            2.1 s on console. The explosion camera drifts 6 to
                            8 units back along the path and eval_missle_path
                            does not clamp its lerp, so a short record
                            extrapolates the camera off the oldest pair. Now
                            sampled at 60 Hz; the impact sample is never skipped
    zGame.cpp               the dt boxcar. See *dt* above

Every ribbon draws joints from one pool:
`xFXRibbon::joint_alloc.init(sizeof(joint_data), 32, 128)`, 4096 joints for
the game. `xFXRibbon::insert` evicts a ribbon's own tail when the pool is full.
A ribbon that lays a joint per frame shortens every other ribbon on screen.

    zEntCruiseBubble.cpp    the wake's `samples <= 0` floor threw the carry
                            away and sampled anyway. It carries unspent time
                            now, so the frame that does sample spans the path
    zNPCTypeDutchman.cpp    the eye scorch subdivides by distance and adds one
                            joint for the leftover. The leftover is gated
    zNPCTypeBossSandy.cpp   the Poseidome laser show

The other bounded containers in the tree are event-driven, seconds-gated or
distance-gated. `containers.h` holds the ring primitives. Frame-amortised work
queues (`zLOD`'s round-robin, the shadow caches) are the inverse shape: a
higher frame rate refreshes them sooner.

Gating an update does not gate what writes into it. `fizzicalSlack` runs from
the 60 Hz `zLasso_Update` but consumes a rope-length delta that `zLasso_Render`
stamps every rendered frame. Reading only the latest delta drops the ones
overwritten between updates. It measures against `sSlackDist`, the rope length
it last saw, instead.

### A quantity whose unit is a frame

A value that is "per frame", so every use of it is wrong away from 60 fps.
Two spellings: a delta between this frame and the last, used as a velocity or
compared against a threshold; and a per-frame increment written in one
function and added in another. The write and the read are usually in
different functions.

    zEntPickup.cpp          the grabbed golden spatula. xMat3x3MulRotC takes an
                            angle per frame. PI * dt is right. The
                            sSpatulaGrabbedSpinMult term beside it ramps per
                            second and was added raw
    zNPCTypeBossSandy.cpp   the limb springs' node velocity, +-0.05 of a limb
                            per frame, added on the render path
    zEntHangable.cpp        the candle test compares a per-frame displacement
                            against a fixed band. Above 60 fps the band never
                            opened and the candles stayed lit
    zEntPlayer.cpp          the downhill stick-down takes the square root of a
                            per-frame distance. Now xsqrt(ndotm * 60 * dt)
    xEnt.cpp                step-up in xEntCollideFloor is gated on a per-frame
                            displacement against 0.001
    zEntPlayer.cpp          the wand bubbles inherit the wand's displacement
                            since the last spawn as a velocity. Rebased by the
                            time since last_center_time
    zNPCGoalRobo.cpp        the tubelet spin-down compares drot.angle, one
                            frame's rotation, against two absolute thresholds.
                            The branch works in radians a second now

Reading one operand of an expression is not reading the expression. The
spatula's `PI * dt` is correct, and the term beside it is not.

### Rotation composed into itself

A matrix multiplied into itself once a frame accumulates error per multiply,
not per second. `xMat3x3RMulRotY` and `xMat3x3Mul` do not renormalise. Hold the
angle as a scalar advanced by `rate * dt` and rebuild the basis.

    zEntPickup.cpp          zEntPickup_SceneUpdate rotated one shared matrix,
                            sPickupOrientation, which every pickup copies. The
                            next line set rwMATRIXTYPEORTHONORMAL on it. Now
                            sPickupAngle += PI * dt and xMat3x3RotY
    zNPCHazard.cpp          TypData_RotMatApply does xMat3x3Mul(frame, mat,
                            frame). The basis is rebuilt from `at` after the
                            multiply. The frame carries no scale: the hazard's
                            scale is mdl_hazard->Scale, applied at render, and
                            every writer of the frame stores an orthonormal
                            basis
    zNPCGlyph.cpp           NPCGlyph::RotAddDelta composed rot_glyph, a
                            per-frame delta, into the frame. See below

The talk glyph, the task glyph and the stunned robot's stars spun a constant
angle per frame: `DEG2RAD(3)` for `NPC_GLYPH_TALK`, `DEG2RAD(-3)` for
`NPC_GLYPH_TALKOTHER`, `DEG2RAD(2.1)` for `NPC_GLYPH_DAZED`. The euler
`RotSet` now also records the angles as a rate a second (`angrate_glyph`). A
PC-only `RotAddDelta(xMat3x3*, F32 dt)` steps `angspin_glyph` by that rate and
rebuilds the basis with `xMat3x3Euler`. Callers: `zNPCGlyph.cpp`'s autospin
branch and shiny `Timestep`, `zNPCGoalVillager.cpp`, and
`zNPCRobot::SyncStunGlyph`. With no rate recorded the new overload calls the
old one.

Only those three glyph types are acquired. The five shiny types and
`NPC_GLYPH_FRIEND` have no `GLYF_Acquire` caller, and `NPCGlyph::Reset` does
not initialise `rot_glyph`. `NPCGlyph::Timestep`'s billboard branch for the
talk glyphs needs bit 2 or 3 of `flg_glyph`. Bit 2 is set only by `VelSet` and
bit 3 by nothing, so the talk glyph free-spins at any frame rate.

### Rounding that reaches zero

`zNPCTypeKingJelly.cpp` sizes a burst as
`S32 total = amount * dt * xurand() + 0.5f`. Below a sixtieth-second frame
`amount * dt` never reaches one half, the count is zero every frame, and the
effect vanishes. A fractional accumulator feeding `total` does not help: the
distribution loop spends `j * total / (ring_size - 1)`, which never reaches
`total`, so a total of one emits nothing.

The wave ring in `update_rings` and the tentacle zaps in
`update_tentacle_lightning` run on a fixed 60 Hz step with the remainder in
`tmr_ringemit` and `tmr_zapemit`. `RyzMemData::operator new` clears only the
first four bytes and the constructor does not zero them, so both are clamped
to 0.1 s before the loop. A `while (t >= 1/60)` loop over garbage would hang.

### Epsilon guards

`if (dt < small) return;` written when the shortest frame was a sixtieth. At a
high frame rate the guard fires every frame and the system stops advancing.
`xFXRingUpdate` in `xFX.cpp` returned for any `dt` under 1e-3. Above 1000 fps
rings never expired and held all eight pool slots. The PC arm returns only for
`dt <= 0`. The rest of the codebase's guards are at 1e-5.

### Accumulators never taken back

`x += dt; if (x < period) return;` with no `x -= period`. It limits the rate
once, then the gate stays open and the body runs every frame.
`sSteamAnimTime` in `zParPTank.cpp` made steam die in eight host frames rather
than eight sixtieths of a second. It now steps at 60 Hz, subtracting 1/60 per
step and clamping the carry.

### Frame-counted animation

`xhud::shake_motive_update` keeps a frame counter in `motive::context`, flips
the sign of the displacement every frame, decays the amplitude every fourth and
ends after fifty. `zHud.cpp`'s `ping_widget` uses it to jog a HUD widget when
its count changes. The four-frame pattern is the effect, so rebasing the decay
alone does not help. It runs on a 60 Hz step with the remainder in the PC-only
`motive::step_time`.

### Two per-frame factors in series

`xBinaryCamera::update` in `xCamera.cpp` runs the Robo-Sandy and Robo-Patrick
fights. `zCameraDisableTracking(CO_BOSS)` hands it the camera for the battle.
Each filter in it is written against a variable frame time. Three lines
together are not:

    F32 max_yaw_diff = cfg.max_yaw_vel * dt;              // the bound
    F32 sloc = 1.0f - xexp(-cfg.move_speed * dt);         // the lag
    F32 yaw_start = xatan2(B.x - A.x, B.z - A.z);         // re-derived from A

The bound puts the goal at most `max_yaw_vel * dt` ahead. The camera closes
`sloc` of that, which is also proportional to `dt`. `yaw_start` is re-derived
from the camera's position next frame, so the part not closed is discarded.
The angle per frame goes as `dt * dt` and the rate per second as `dt`.
Degrees a second from `fps_selftest`'s model of the loop with Sandy's
`bossCam` config:

    retail    60: 88.5   144: 38.5   240: 23.4   1000: 5.7   3000: 1.9
    rebased   60: 88.5   144: 88.5   240: 88.5   1000: 88.5  3000: 88.6

The fix scales the bound by `sloc(1/60) / sloc(dt)`, which is 1 at a console
frame. `stick_offset.x` is rebased with it. Its target is
`stick_yaw_vel * stick.offset.x * dt`, and `stick_yaw_vel` and `max_yaw_vel`
are both 10, so full deflection lands on the bound at any frame rate. Rebasing
the bound alone stops the stick reaching it above 60 fps. Rebasing the stick
alone changes nothing, because the bound is the binding constraint.

Two per-frame quantities in series are one defect. Neither line is wrong on
its own, and `fpsdep.py` has no shape for it.

### Collision that depends on step size

`xEntBoulder_Update` recomputes velocity from the frame's position change, so
depenetration the motion did not cause enters as `overlap / dt`. JF01's cannon
spawns its boulder 0.59 units inside the cannon mesh. One 1/60 step tunnels it
out straight. A shorter step leaves it scraping the barrel and it exits 57
degrees off at 240 fps.

`xEntBoulder_FixedStep` runs the boulder body at a fixed 1/60 with the
remainder carried, and the rendered transform interpolates between the last
two steps. At 60 fps it is one step per frame with a zero remainder. A boulder
whose caller supplies its own `xEntCollis` is left at the frame's `dt`: the
spongeball drives one directly from `zEntPlayer.cpp` and reads the contacts
back out of that buffer.

### Loading paced by the frame rate

`xSTLoadStep` in `xstransvc.cpp` advances the package loader one state
transition per call. The scene loops in `zScene.cpp` draw a frame between
calls:

    do {
        zGameScreenTransitionUpdate(pdone, "... scene loading ...\n", rgba_bkgrd);
    } while (xSTLoadStep(theSceneID) < 1.0f);

`zGameScreenTransitionUpdate` ends in `RwCameraShowRaster`, which waits for the
display and paces to the cap. On the GameCube each step queues a drive read and
the frame is time the drive needed anyway. On PC, `iFileAsyncService`, which
`xSTLoadStep` calls, completes the read in one call, so load time was the step
count divided by the frame rate.

The PC arm of `xSTLoadStep` keeps stepping until `kLoadStepBudget`, a
thirtieth of a second, has passed. Batching changes no call and no order:
`PKR_LoadStep_Async` holds its layer in a static and drives one at a time, and
the memory mark `PKR_LayerMemReserve` pushes for a RenderWare handoff is popped
by the step that hands that layer over.

The budget has to be at least one refresh period for both pacers to stop
waiting:

- `iWindowPaceFrame` drops a missed deadline, and a 33 ms budget misses every
  period `video.framerate` allows.
- librw's D3D9 device uses `BackBufferCount = 1` and `D3DSWAPEFFECT_DISCARD`
  (`d3ddevice.cpp`). `Present` queues a flip and blocks only while an earlier
  flip is pending. With 33 ms between presents the earlier flip has happened.

30 Hz is the slowest display designed for. A budget at the floor keeps the
loading-screen bubbles moving. The cost is the loading screen's draw, a
constant few ms out of every 33. Going below that means loading off the main
thread. `xMemAlloc` is not thread safe and the RenderWare handoff at the end of
`PKR_LoadStep_Async` has to run where the device is.

`video.load_time` (`iLoadScreen.cpp`) is a separate minimum on how long the
loading screen stays up.

The startup loops in `zMain.cpp` (`BOOT`, `PLAT`, `MNU4`, `MNU5`) and
`zEntPlayer_LoadHOP` have empty bodies, draw no frame, and already ran
unpaced.

## Shapes that are not defects

### The camera

`zCamera.cpp`'s yaw is `dp` built from the stick times a constant `1/60`. It
looks like a rate times a hardcoded timestep. It is not. Changing it to `dt`
makes the turn rate proportional to `dt`: about half speed at 128 fps.

`dp` does not accumulate. It is added to `cam->pcur` and to a local `pgoal`,
and `xCameraMove` writes that local into `cam->pgoal`, so the goal is always
`dp` ahead of the camera. `xCameraUpdate` re-derives `pcur` from `mat.pos`
through `xCam_worldtocyl` at the top of every frame, discarding the `pcur`
write. `xCam_CorrectP` springs the position toward the goal by a fraction
proportional to `dt`. The angle per frame is `(k * dt) * dp`, so the rate per
second is `k * dp`. A constant `dp` makes the turn rate frame-rate independent.
The `1/60` converts stick units into radians of gap.

- `pitch_s` is reset to zero every frame and consumed in
  `zCameraFreeLookSetGoals` as a 0..1 blend weight.
- The overrotation site builds its `dp` from `zcam_overrot_rate` with no `dt`
  and feeds the same mechanism.
- `xCamera.cpp` keeps a `static F32 last_dt` and uses it to turn a per-frame
  position delta into a velocity. This camera was written for a variable frame
  time.

The boss fights use `xBinaryCamera`, which has a real defect. See *Two
per-frame factors in series*.

### The pickups' spin rate

`zEntPickup_SceneUpdate` takes `dt` from `zSceneUpdate` and rotates by
`PI * dt`. The rate was always right. The fix there removes drift. Golden
spatulas are ordinary `zEntPickup`s: `zEntPickup_RenderOne` (reached from the
aura pass in `xFX.cpp`) and `zEntPickup_RenderList` both copy
`sPickupOrientation` into the model matrix.

### An emitter rate that spells a count

An emitter whose `rate` is set to a multiple of 60 right before a `1/60` call
is spelling a particle count per call: `rate.set(59.999996f)` is one particle,
`rate.set(119.99999f * n)` is `2n`. Whether it needs `dt` depends on how often
the call runs. The Dutchman's beam light runs every frame while the beam
travels, so it takes `dt`. These run on an event or on distance and stay:

    zNPCTypeDutchman.cpp    the flame. `emit = (S32)(dist * emit_rate) + 1`
                            against wave.emitted[i]: runs on distance
    zNPCTypeKingJelly.cpp   the shock ring (rate 59.999996f; `total` already
                            carries dt) and the thump ring (rate 59.999996f *
                            tweak.thump.particles; its caller sets delay = 1e9
                            right after)
    xParEmitter.cpp         eEventEmit
    zGust.cpp               behind debris_timer, reset to 0.15-0.3 seconds
    zPlatform.cpp           zPlatform_Tremble on an event, and
                            zPlatform_BreakawayFallFX, called only from the
                            state 2 -> 3 transition
    zEntHangable.cpp        zEntHangableMountFX, on eEventMount

`xLaserBolt.cpp` passes `dt` to both emits. `xClimate.cpp` and `zLightning.cpp`
pass `seconds`, the real frame time.

### Systems checked and clean

- `xAnim.cpp` is time-based: `xModelUpdate(inst, dt)` advances time by
  `timeDelta * CurrentSpeed`, and blend progress uses `BlendRecip`, one over a
  number of seconds. Keyframes sample by absolute time in `iAnimSKB.cpp` and
  `xMorph.cpp`. The 30 fps authoring rate appears only as load-time unit
  conversions in `zMain.cpp`.
- `xScrFx.cpp` ages fade, letterbox, glare and the distortion pool by `dt`.
- `zSurface.cpp` uses `dt` throughout, apart from the UV clock above.
- `zGust.cpp` uses `dt` and a seconds timer.
- `zShrapnel.cpp` ages by `lifetime -= dt` and integrates the parabola
  analytically.
- `xFFXShakeUpdateEnt` derives its magnitude from an absolute timer and
  applies the delta.

Rotation sites advanced by `dt` or not per frame:

    xEntMotion.cpp          the mech rotation is speed * dt. The PEN
                            xMat3x3Mul(modlrot, modlrot, &pshrot) is in the
                            reset path. ORB evaluates position from motion->t
    zPlatform.cpp           FM platforms step by ds, solved from dt and the
                            asset's accel/decel times. Teeter and paddle derive
                            angles from dt and a timer
    zNPCTypeBossSB2.cpp     xAccelMove(..., dt, ...)
    zShrapnel.cpp           xMat3x3Rot(&spin, axis, dt * angVel)
    xEntBoulder.cpp         xMat3x3Rot(&rotM, rotVec, angVel * dt)
    zEntPlayer.cpp          sReticleRot += 8.0f * dt, sHitchAngle += 3.14f * dt
    xFX.cpp                 the aura pulse and spin, both * dt
    xHud.cpp                rc.rot from the asset and the motives, which use dt
    xFont.cpp               a parsed markup argument
    zEntTrigger.cpp         one-time setup from asset->ang
    zEntTeleportBox.cpp     eulers built from a launch angle on an event
    zNPCTypeVillager.cpp    screenRot, set once to 1.0f and changed only by a
                            debug tweak
    zEntHangable.cpp        ent->spin, set to zero and never advanced
    zNPCGoalVillager.cpp    ang_spinrate += 16.0f, an impulse on side damage

### Telling a real site from a false one

A hit from a scan is a lead. Six shapes look like a defect and are not:

1. A unit conversion. A constant that equals 1/60 or 1/30 but converts units,
   with nothing accumulating. Examples: `pitch_s`, `zcam_flytime`, an emitter
   rate set to a multiple of 60.
2. A wrap. `-= 360.0f` or `+= 1.0f` inside an `if` that brings a value back
   into range. Example: the `zSurface.cpp` UV wrap.
3. A one-shot. Runs on an event and happens to sit in a function that takes
   `dt`. Examples: per-particle spread in `xParEmitterType.cpp`,
   `zEntPickup.cpp`'s offscreen `+= 10000.0f`.
4. A value that does not survive the frame. Reset at the top of the function,
   or recomputed from elsewhere before anything reads the accumulated part.
   Follow the value to its next read, not to its declaration: `cam->pcur` is a
   member and is still discarded every frame by `xCam_worldtocyl`.
   `zEntPickup.cpp`'s `vel.y += 0.08f * ydiff` is another: `ent->vel` is
   overwritten from a normalised direction three lines above.
5. A counter of events. Hit points, bounces, misses, deaths.
   `cnt_nextfunfrag` counts robot deaths.
6. A value already in seconds. A seconds accumulator
   (`shared.trail.bubbles += dt * bubble_rate`), a seconds timer gating the
   path (`debris_timer`), or `dt` already in the expression.

A per-frame counter that only throttles work is still a defect when something
else steps inside the throttled block. `cnt_nextlos` looked like a pure
raycast throttle, and `cnt_inContact` steps inside it.

## What holds this in place

### fps_selftest

`src/SB/Core/pc/tests/fpstest.cpp`, built as `fps_selftest`. It checks the
helpers as properties:

- At a sixtieth of a second each helper returns the constant it replaced.
- Out-of-range coefficients clamp and never produce a NaN.
- A second of damping, approach, emission and pop chance gives the same total
  at 60, 120, 144, 240, 1000 and 3000 fps.
- The bubble pop loop, driven through the real `xurand`, keeps 18% survival.
- The boss camera model turns at one rate at every frame rate, and the
  unrebased loop does not.

36 checks. It is its own target rather than a case in `pc_selftest` because
linking `xMath.cpp` needs `range_limit<F32>`, which CodeWarrior placed in
`xCamera.cpp`.

### tools/fpsdep.py and tools/fpsdep.json

The rebased game sites need a scene, a model and a player, so a unit test
cannot reach them. `fpsdep.py` scans `src/SB/Game` and `src/SB/Core/x` for the
mechanically recognisable shapes:

    timestep  a hardcoded console frame: 1/60, 1/30, 59.999996, 119.99999
    damping   `*= 0.97f`, `*= <name>decay` and friends, xVec3SMul by a literal
    counter   ++ or -- on a member or file static in a function taking dt
    gate      a random draw against a constant on a per-frame path
    history   one sample pushed per frame into a fixed-slot container

It scans the PC arm only, so a fixed site reports its guarded line. Every
judged hit is recorded in `fpsdep.json`, and any hit not in it fails. The
baseline records what has been read, not what is correct.

    fpsdep.py                  report hits not in the baseline; exit 1 if any
    fpsdep.py --all            list every hit
    fpsdep.py --shape counter  one shape only
    fpsdep.py --update         rewrite the baseline from the current tree

It cannot see two correct lines that compose badly (the boss camera), a
one-pole spelled across functions, or a quantity whose unit is a frame.

### ctest

Both are `ctest` cases in `build-pc`: `fps_selftest`, and `fpsdep` when
Python 3 is found.

## Open items

### Known and left

- `tools/fpsdep.py` reports six hits not in the baseline, so the `fpsdep`
  ctest fails. All six are the fixed-step fixes: `tmr_nextlos` (two sites) and
  `tmr_emit` in `zNPCGoalRobo.cpp`, `tmr_breath` and `tmr_moreorless` in
  `zNPCTypeRobot.cpp`, and `kLoadStepBudget` in `xstransvc.cpp`. They need
  `fpsdep.py --update`.
- `zNPCGoalVillager.cpp`'s `cnt_nextMedic` decrements per frame to grant a
  health point. It is a cheat.
- `zMain.cpp`'s memory-card screen calls
  `xPadUpdate(globals.currentActivePad, 1.0f / 60)`, so rumble timers there age
  one console frame per loop iteration. Nothing rumbles on that screen.
- `zEntHangable.cpp`'s `enabled = -2` counts up to zero, a two-frame re-enable
  delay. The countdown is its only reader, and `grabTimer` beside it is
  decremented but never tested. It gates nothing.
- `iFMV.cpp`'s movie loop presents every iteration and decodes on a timestamp.
  With no cap and vsync off it re-presents the same frame thousands of times a
  second. Playback speed is correct. Either setting bounds it.
- `zUI.cpp`'s `ushift += 0.05f` in `zUIRenderAll` is a per-rendered-frame
  counter. Nothing reads `ushift`.
- `zNPCTypeTiki.cpp`'s `loveyFloat` walks `t2` and `t3` per frame. Nothing
  reads them.

### Not swept

Additive accumulation without `dt`: `+=` or `-=` against a float literal
inside a function that takes `F32 dt`, on a line that does not mention `dt`.
`fpsdep.py` has no shape for it. A scan of this shape finds a couple of
hundred hits, mostly false positives of kinds 2 to 4 above: `zSurface.cpp`'s wraps,
`xParEmitterType.cpp`'s per-particle spread, `zEntPickup.cpp`'s offsets. About
one in five is worth changing, and which one takes reading the function.
`zSurface.cpp`, `zEntPlayer.cpp`, `zNPCSupplement.cpp`, `xParCmd.cpp`,
`xParEmitterType.cpp`, `zNPCTypeBossPatrick.cpp` and `zNPCTypeTiki.cpp` have
the most hits.
