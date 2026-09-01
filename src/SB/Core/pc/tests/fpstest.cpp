// Checks that the rate helpers behave the same however long a frame is.
//
// The port's whole frame-rate-independence story rests on four claims, and the
// interesting one is not that the arithmetic is right. It is that at a
// sixtieth of a second every helper collapses back to the constant it replaced,
// so the default 60 fps build behaves exactly as the console did, and that
// above 60 the same amount happens per SECOND rather than per frame.
//
// Those are properties, not values, so they are checked by running a second of
// game time at several frame rates and comparing the totals.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <types.h>

#include "xMath.h"

// xMath.cpp's leaf math, which the game gets from iMath.cpp and from the weak
// inlines CodeWarrior scattered across xCamera.cpp and xHudMeter.cpp. Only
// xpow is reached by anything under test; the rest are here because they are
// referenced by the other half of xMath.cpp's object file.
F32 xpow(F32 x, F32 y)
{
    return powf(x, y);
}

F32 xsqrt(F32 x)
{
    return sqrtf(x);
}

F32 xatan2(F32 y, F32 x)
{
    return atan2f(y, x);
}

F32 isin(F32 x)
{
    return sinf(x);
}

F32 icos(F32 x)
{
    return cosf(x);
}

F32 xfmod(F32 x, F32 y)
{
    return fmodf(x, y);
}

template <> F32 range_limit<F32>(F32 v, F32 minv, F32 maxv)
{
    return v < minv ? minv : (v > maxv ? maxv : v);
}

static int failures;

static void check(bool ok, const char* what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
    {
        failures++;
    }
}

// The rates worth checking: the console's, two common displays, and two well
// past anything a display does, because uncapped measured 1700-3200 fps.
static const S32 kRates[] = { 60, 120, 144, 240, 1000, 3000 };
static const S32 kRateCount = sizeof(kRates) / sizeof(kRates[0]);

static const F32 kConsoleFrame = 1.0f / 60.0f;

static bool near_rel(F32 got, F32 want, F32 tol)
{
    F32 scale = fabsf(want) > 1e-6f ? fabsf(want) : 1.0f;

    return fabsf(got - want) / scale <= tol;
}

// --- at a console frame, every helper is the identity ------------------------
//
// This is the claim that the default build is unchanged. Nothing else in this
// file matters if it does not hold.

static void test_console_frame_is_identity()
{
    printf("\nat a sixtieth of a second the helpers return what they replaced\n");

    static const F32 ks[] = { 0.02f, 0.1f, 0.2f, 0.5f, 0.8f, 0.9f, 0.95f, 0.96f,
                              0.97f, 0.98f, 0.985f, 0.99f };
    bool approach_ok = true;
    bool chance_ok = true;

    for (S32 i = 0; i < (S32)(sizeof(ks) / sizeof(ks[0])); i++)
    {
        if (!near_rel(xFrameApproach(ks[i], kConsoleFrame), ks[i], 1e-4f))
        {
            approach_ok = false;
        }

        if (!near_rel(xFrameEmitChance(ks[i], kConsoleFrame), ks[i], 1e-4f))
        {
            chance_ok = false;
        }
    }

    check(approach_ok, "xFrameApproach gives back every coefficient in use");
    check(chance_ok, "xFrameEmitChance gives back every probability in use");

    // The count is the one helper with a random part: 60 * dt lands a hair off
    // 1.0 in float, so the fraction is not quite zero and the coin flip can add
    // one. What has to hold is that the guaranteed part is the whole count.
    bool count_ok = true;

    for (U32 n = 1; n <= 16; n++)
    {
        U32 got = xFrameEmitCount((F32)n, kConsoleFrame);

        if (got != n && got != n + 1)
        {
            count_ok = false;
        }
    }

    check(count_ok, "xFrameEmitCount asks for the whole count, never less");

    // The damping idiom retail already used, and the one 15-odd sites call
    // directly rather than through a helper.
    check(near_rel(xpow(0.96f, 60.0f * kConsoleFrame), 0.96f, 1e-4f),
          "xpow(k, 60*dt) gives back k");
}

// --- clamps ------------------------------------------------------------------
//
// Both helpers take a coefficient straight out of a game asset in at least one
// place -- xEntBoulder's stickiness -- so a value outside 0..1 is reachable and
// xpow of a negative base is a NaN that would spread into the entity.

static void test_clamps()
{
    printf("\ncoefficients from assets are clamped before xpow sees them\n");

    check(xFrameApproach(-0.5f, kConsoleFrame) == 0.0f, "a negative approach is zero");
    check(xFrameApproach(1.5f, kConsoleFrame) == 1.0f, "an approach past one is one");
    check(xFrameEmitChance(-0.5f, kConsoleFrame) == 0.0f, "a negative chance is zero");
    check(xFrameEmitChance(1.5f, kConsoleFrame) == 1.0f, "a chance past one is one");

    bool finite = true;

    for (S32 i = 0; i < kRateCount; i++)
    {
        F32 dt = 1.0f / kRates[i];

        if (!isfinite(xFrameApproach(-1.0f, dt)) || !isfinite(xFrameApproach(2.0f, dt)))
        {
            finite = false;
        }
    }

    check(finite, "and no rate turns an out-of-range coefficient into a NaN");
}

// --- damping settles at the same place, whatever the frame rate --------------

static void test_damping_is_flat()
{
    printf("\na second of damping ends at the same value at every frame rate\n");

    static const F32 ks[] = { 0.8f, 0.9f, 0.95f, 0.96f, 0.97f, 0.98f, 0.985f, 0.99f };

    for (S32 j = 0; j < (S32)(sizeof(ks) / sizeof(ks[0])); j++)
    {
        F32 k = ks[j];
        F32 want = powf(k, 60.0f);
        bool ok = true;

        for (S32 i = 0; i < kRateCount; i++)
        {
            F32 dt = 1.0f / kRates[i];
            F32 x = 1.0f;

            for (S32 n = 0; n < kRates[i]; n++)
            {
                x *= xpow(k, 60.0f * dt);
            }

            if (!near_rel(x, want, 0.02f))
            {
                ok = false;
            }
        }

        char what[80];
        snprintf(what, sizeof(what), "x *= %g settles to %.4f from 60 to 3000 fps", k, want);
        check(ok, what);
    }
}

// --- an approach closes the same distance, whatever the frame rate -----------

static void test_approach_is_flat()
{
    printf("\na second of approach closes the same distance at every frame rate\n");

    static const F32 ks[] = { 0.02f, 0.05f, 0.1f, 0.2f, 0.5f };

    for (S32 j = 0; j < (S32)(sizeof(ks) / sizeof(ks[0])); j++)
    {
        F32 k = ks[j];
        F32 want = 1.0f - powf(1.0f - k, 60.0f);
        bool ok = true;

        for (S32 i = 0; i < kRateCount; i++)
        {
            F32 dt = 1.0f / kRates[i];
            F32 x = 0.0f;

            for (S32 n = 0; n < kRates[i]; n++)
            {
                x += xFrameApproach(k, dt) * (1.0f - x);
            }

            if (!near_rel(x, want, 0.02f))
            {
                ok = false;
            }
        }

        char what[80];
        snprintf(what, sizeof(what), "x += %g * (target - x) closes %.4f a second", k, want);
        check(ok, what);
    }
}

// --- emission counts the same particles a second -----------------------------
//
// xFrameEmitCount is unbiased by construction -- the whole part plus the
// probability of the coin flip is exactly count * 60 * dt -- so what is under
// test is that the frame rate does not enter that product. It is still a random
// variable, and the smallest case here buys under half a particle a frame, so
// the run has to be long enough that the standard deviation sits well inside
// the tolerance rather than on it. Enough game time for twenty thousand
// expected emissions puts it near half a percent against a five percent bar.

static void test_emission_is_flat()
{
    printf("\nemission buys the same particles a second at every frame rate\n");

    static const F32 counts[] = { 0.375f, 1.0f, 3.0f, 10.0f };
    static const F32 kWantedEmissions = 20000.0f;

    for (S32 j = 0; j < (S32)(sizeof(counts) / sizeof(counts[0])); j++)
    {
        F32 per_frame = counts[j];
        S32 seconds = (S32)(kWantedEmissions / (per_frame * 60.0f)) + 1;
        F32 want = per_frame * 60.0f * seconds;
        bool ok = true;

        for (S32 i = 0; i < kRateCount; i++)
        {
            F32 dt = 1.0f / kRates[i];

            xsrand(12345);

            F32 total = 0.0f;

            for (S32 n = 0; n < kRates[i] * seconds; n++)
            {
                total += xFrameEmitCount(per_frame, dt);
            }

            if (!near_rel(total, want, 0.05f))
            {
                ok = false;
            }
        }

        char what[80];
        snprintf(what, sizeof(what), "%g a console frame stays %g a second", per_frame,
                 per_frame * 60.0f);
        check(ok, what);
    }
}

// --- a per-frame chance survives the same fraction of a second ---------------
//
// This is the shape that made every bubble in the game die young: a 4% chance
// of popping per FRAME is 42 rolls over a 0.7 s window on console and 168 at
// 240 fps, which is 18% survival against 0.1%.

static void test_chance_is_flat()
{
    printf("\na per-frame chance leaves the same survivors after a second\n");

    static const F32 chances[] = { 0.04f, 0.05f, 0.25f };

    for (S32 j = 0; j < (S32)(sizeof(chances) / sizeof(chances[0])); j++)
    {
        F32 chance = chances[j];
        F32 want = powf(1.0f - chance, 60.0f);
        bool ok = true;

        for (S32 i = 0; i < kRateCount; i++)
        {
            F32 dt = 1.0f / kRates[i];
            F32 survival = powf(1.0f - xFrameEmitChance(chance, dt), (F32)kRates[i]);

            if (!near_rel(survival, want, 0.02f))
            {
                ok = false;
            }
        }

        char what[80];
        snprintf(what, sizeof(what), "%g a frame leaves %.4f alive after a second", chance, want);
        check(ok, what);
    }
}

// --- the bubble that started it ----------------------------------------------
//
// zParPTank rolls xurand() against the rebased chance once per frame over the
// 0.7 s window between a bubble's life dropping below 1.2 and 0.5. Driven
// through the real xurand rather than the closed form, because the closed form
// is what the helper computes and would prove nothing about the loop that uses
// it.

static F32 bubble_survival(S32 rate, S32 trials)
{
    F32 dt = 1.0f / rate;
    F32 keep = 1.0f - xFrameEmitChance(0.04f, dt);
    S32 frames = (S32)(0.7f * rate);
    S32 alive = 0;

    for (S32 t = 0; t < trials; t++)
    {
        bool popped = false;

        for (S32 n = 0; n < frames && !popped; n++)
        {
            if (xurand() > keep)
            {
                popped = true;
            }
        }

        if (!popped)
        {
            alive++;
        }
    }

    return (F32)alive / trials;
}

static void test_bubble_pop()
{
    printf("\nbubbles reach their fade-out as often as they do on console\n");

    xsrand(1);

    F32 console = bubble_survival(60, 20000);
    bool ok = true;

    for (S32 i = 0; i < kRateCount; i++)
    {
        if (!near_rel(bubble_survival(kRates[i], 20000), console, 0.1f))
        {
            ok = false;
        }
    }

    char what[80];
    snprintf(what, sizeof(what), "%.0f%% of bubbles survive their window at every rate",
             console * 100.0f);
    check(ok, what);

    // What the fix was for. Retail's flat 4% a frame at 240 fps.
    F32 unfixed = powf(0.96f, 0.7f * 240.0f);
    check(unfixed < 0.01f && console > 0.1f,
          "and the unrebased 4% a frame would leave under 1% of them");
}

// --- the boss camera's yaw ---------------------------------------------------
//
// xBinaryCamera::update runs the Robo-Sandy and Robo-Patrick fights, and three
// of its lines decide how fast the camera can come round:
//
//     max_yaw_diff = max_yaw_vel * dt            the goal is at most this far
//                                                ahead of the camera
//     sloc         = 1 - exp(-move_speed * dt)   the camera closes this much of
//                                                that goal this frame
//     yaw_start    = atan2(B - A)                re-derived from the camera's
//                                                own position next frame
//
// Both factors are per-frame and they multiply, and because yaw_start comes
// back from the camera itself nothing carries over, so retail's rate falls off
// with dt: 88 degrees a second at 60 fps, 38 at 144, 2 at 3000. The stick
// offset rides on the same bound -- stick_yaw_vel and max_yaw_vel are both 10,
// so full deflection sits exactly on it -- which is why rebasing the stick
// alone changes nothing and both lines move together.
//
// This mirrors the source rather than calling it. Linking xCamera.cpp wants a
// scene, a pad and an xCamera; the arithmetic under test is these three lines.

static F32 wrap_pi(F32 a)
{
    a = fmodf(a + PI, 2.0f * PI);

    if (a < 0.0f)
    {
        a += 2.0f * PI;
    }

    return a - PI;
}

// Degrees the camera turns in one second, from zNPCBSandy's own bossCam config.
// `stick` picks what asks for the turn: the framing, which wants a half turn
// and leaves the bound to decide the rest, or the player holding the stick.
static F32 boss_cam_turn(S32 fps, bool rebased, bool stick)
{
    const F32 d = 6.0f;             // cfg.zone_rest.distance
    const F32 move_speed = 10.0f;   // cfg.move_speed
    const F32 max_yaw_vel = 10.0f;  // cfg.max_yaw_vel
    const F32 stick_yaw_vel = 10.0f;
    const F32 stick_speed = 10.0f;

    F32 dt = 1.0f / (F32)fps;
    F32 sloc = 1.0f - expf(-move_speed * dt);
    F32 sloc60 = 1.0f - expf(-move_speed * kConsoleFrame);
    F32 sstick = 1.0f - expf(-stick_speed * dt);

    // The player sits at the origin; the camera starts d behind at yaw 0.
    F32 ax = 0.0f;
    F32 az = -d;
    F32 stick_offset = 0.0f;

    for (S32 i = 0; i < fps; i++)
    {
        F32 yaw_start = atan2f(-ax, -az);
        F32 yaw_end;

        if (stick)
        {
            F32 target = stick_yaw_vel * 1.0f * (rebased ? kConsoleFrame : dt);

            stick_offset += (target - stick_offset) * sstick;
            yaw_end = yaw_start + stick_offset;
        }
        else
        {
            yaw_end = PI;
        }

        F32 yaw_diff = wrap_pi(yaw_end - yaw_start);
        F32 max_yaw_diff = max_yaw_vel * dt;

        if (rebased)
        {
            max_yaw_diff *= sloc60 / sloc;
        }

        if (fabsf(yaw_diff) > max_yaw_diff)
        {
            yaw_end = yaw_start + (yaw_diff < 0.0f ? -max_yaw_diff : max_yaw_diff);
        }

        ax += (-d * sinf(yaw_end) - ax) * sloc;
        az += (-d * cosf(yaw_end) - az) * sloc;
    }

    return fabsf(wrap_pi(atan2f(-ax, -az))) * (ONEEIGHTY / PI);
}

static void test_boss_camera_is_flat()
{
    printf("\nthe boss camera comes round at one rate however long a frame is\n");

    for (S32 stick = 0; stick < 2; stick++)
    {
        F32 console = boss_cam_turn(60, false, stick != 0);
        bool identity = near_rel(boss_cam_turn(60, true, stick != 0), console, 1e-3f);
        bool flat = true;

        for (S32 i = 0; i < kRateCount; i++)
        {
            if (!near_rel(boss_cam_turn(kRates[i], true, stick != 0), console, 0.05f))
            {
                flat = false;
            }
        }

        char what[80];

        snprintf(what, sizeof(what), "a console frame turns %.0f degrees a second on the %s",
                 console, stick ? "stick" : "framing");
        check(identity, what);

        snprintf(what, sizeof(what), "and every rate up to 3000 turns the same %s",
                 stick ? "on the stick" : "on the framing");
        check(flat, what);
    }

    // What the fix is for: two per-frame factors multiplied, so the rate falls
    // with dt rather than holding.
    check(boss_cam_turn(144, false, false) < 0.5f * boss_cam_turn(60, false, false) &&
              boss_cam_turn(3000, false, false) < 0.05f * boss_cam_turn(60, false, false),
          "unrebased, 144 fps turns under half as far and 3000 under a twentieth");
}

int main()
{
    printf("bfbb frame-rate independence self-test\n");

    test_console_frame_is_identity();
    test_clamps();
    test_damping_is_flat();
    test_approach_is_flat();
    test_emission_is_flat();
    test_chance_is_flat();
    test_bubble_pop();
    test_boss_camera_is_flat();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "passed", failures,
           failures == 1 ? "" : "s");

    return failures ? 1 : 0;
}
