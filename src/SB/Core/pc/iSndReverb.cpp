#include "iSndReverb.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// See iSndReverb.h for what this is, where the parameters came from, and how
// far the resemblance to the Xbox goes.

#define ISNDREVERB_STAGES 6
#define ISNDREVERB_ERTAPS 5

// ---------------------------------------------------------------------------
// The constants of the design
//
// The late delays are a geometric series: 67 ms on the left and 75 on the
// right, each multiplied by 0.93 raised to 0, 1, 3, 6, 10 and 15 -- the running
// sum of the stage index, so the stages crowd together as they shorten. The
// chain runs from the shortest to the longest.
static const F32 kLateHeadMs[2] = { 67.0f, 75.0f };
static const F32 kLateRatio = 0.93f;

// The early reflections are placed across the window between the first
// reflection and the onset of the late reverberation, as fractions of it, and
// summed with these weights. The first left tap sits at zero, so it lands on
// reflections_delay exactly.
static const F32 kErFrac[2][ISNDREVERB_ERTAPS] = {
    { 0.0000f, 0.1768f, 0.3953f, 0.6899f, 0.9400f },
    { 0.1078f, 0.2727f, 0.5386f, 0.8306f, 0.9800f },
};

static const F32 kErWeight[2][ISNDREVERB_ERTAPS] = {
    { 0.68f, -0.5f, -0.62f, -0.5f, -0.62f },
    { 0.707f, -0.6f, -0.5f, -0.6f, -0.5f },
};

// A sixth tap of the same line feeds the late chain rather than the early
// output. On the left it is placed 7 ms beyond the late onset; on the right, at
// the onset itself.
static const F32 kLateFeedExtraMs = 7.0f;

// What the late chain is tapped for its output, stage by stage. Stage 1 is the
// exception: its share crosses to the other channel instead.
static const F32 kOutWeight[ISNDREVERB_STAGES] = {
    -0.38f, 0.0f, -0.38f, 0.35f, -0.2f, -0.15f
};
static const F32 kCrossWeight = 0.38f;

// The energy-preserving 2x2 matrix the two chains are fed through, which is
// what couples them.
static const F32 kMatrix = 0.707f;

// Reciprocal of the golden ratio, and the allpass coefficient at full
// diffusion. Also the fixed coefficient of the early reflection diffuser.
static const F32 kGolden = 0.618034f;

// The diffuser after the early taps, and the extra delay in the middle of the
// late chain.
static const F32 kErDiffuserMs[2] = { 3.25f, 3.53f };
static const F32 kMidMs = 10.0f;

// Applied to the early level on top of what the parameters ask for.
static const F32 kErLevelScale = 0.761f;

// The input lines have to reach the furthest tap: reflections_delay tops out at
// 300 ms and reverb_delay at 100, and the left line carries 7 ms more.
static const F32 kInMs[2] = { 407.0f, 400.0f };

// How long the effect takes to fade in or out. The game only changes this at a
// scene load, where nothing much is audible, but a step in a gain is a click
// and a click costs nothing to avoid.
static const F32 kRampSeconds = 0.05f;

// ---------------------------------------------------------------------------
// State

// Write position counts down, so a tap at distance n reads what was written n
// frames ago at (pos + n) modulo the length.
struct rline
{
    F32* buf;
    U32 len;
    U32 pos;
    U32 tap;
};

struct rchan
{
    rline in;                       // holds the room-filtered input
    rline ap[ISNDREVERB_STAGES];    // the absorbent allpass chain
    rline er_ap;                    // the early reflection diffuser
    rline mid;                      // the extra delay inside the chain

    U32 er_tap[ISNDREVERB_ERTAPS];
    U32 late_feed_tap;

    // Per stage: the decay gain, and the pole of the damping filter in front
    // of it.
    F32 c1[ISNDREVERB_STAGES];
    F32 c2[ISNDREVERB_STAGES];

    F32 hist[ISNDREVERB_STAGES];
    F32 hist_mid;
    F32 hist_room;

    // The last stage's output, which is what the input tap is summed into to
    // close the loop.
    F32 hist_loop;

    F32 level;
};

static bool sReady;
static U32 sRate;
static F32* sArena;
static rchan sChan[2];

static F32 sDiffusion;
static F32 sErLevel;
static F32 sRoomFilter;

static F32 sMidC1;
static F32 sMidC2;

static F32 sMix;
static F32 sMixTarget;
static F32 sRampStep;

static bool sSilenced;

// ---------------------------------------------------------------------------
// Delay lines

static inline F32 iLineGet(const rline* l, U32 tap)
{
    U32 i = l->pos + tap;
    if (i >= l->len)
    {
        i -= l->len;
    }
    return l->buf[i];
}

static inline F32 iLineTap(const rline* l)
{
    return iLineGet(l, l->tap);
}

static inline void iLineSet(rline* l, F32 v)
{
    l->buf[l->pos] = v;
}

static inline void iLineAdvance(rline* l)
{
    l->pos = (l->pos == 0) ? (l->len - 1) : (l->pos - 1);
}

static U32 iSamples(F32 ms)
{
    F32 n = ms * 0.001f * (F32)sRate;
    return (n < 0.0f) ? 0 : (U32)n;
}

// ---------------------------------------------------------------------------
// Setup

static F32 iFromMillibels(S32 mb)
{
    return powf(10.0f, (F32)mb / 2000.0f);
}

// The stage index's running sum: 0, 1, 3, 6, 10, 15.
static U32 iRatioPower(U32 stage)
{
    return stage * (stage + 1) / 2;
}

static F32 iLateMs(U32 channel, U32 stage)
{
    return kLateHeadMs[channel] * powf(kLateRatio, (F32)iRatioPower(stage));
}

static void iClearState()
{
    for (U32 c = 0; c < 2; c++)
    {
        rchan* ch = &sChan[c];

        memset(ch->in.buf, 0, sizeof(F32) * ch->in.len);
        memset(ch->er_ap.buf, 0, sizeof(F32) * ch->er_ap.len);
        memset(ch->mid.buf, 0, sizeof(F32) * ch->mid.len);

        for (U32 s = 0; s < ISNDREVERB_STAGES; s++)
        {
            memset(ch->ap[s].buf, 0, sizeof(F32) * ch->ap[s].len);
            ch->hist[s] = 0.0f;
        }

        ch->hist_mid = 0.0f;
        ch->hist_room = 0.0f;
        ch->hist_loop = 0.0f;
    }
}

void iSndReverbInit(U32 rate)
{
    iSndReverbExit();

    if (rate < 8000)
    {
        return;
    }

    sRate = rate;

    // Sized for the longest each line can be asked for. Density only shortens
    // the late taps and the two delay parameters are clamped to I3DL2's own
    // limits, so a parameter change never needs to allocate.
    //
    // Sized from the tap rather than from a rounded-down millisecond figure.
    // The reference this is taken from lists the late lines as 67, 62, 53, 43,
    // 32 and 22 ms, which are the tap times truncated, and pads them by five
    // samples -- at 48 kHz that is not enough for five of the six, and the taps
    // wrap to a few samples each. What the constants describe is the geometric
    // series, so that is what is built.
    U32 total = 0;
    for (U32 c = 0; c < 2; c++)
    {
        total += iSamples(kInMs[c]) + 8;
        total += iSamples(kErDiffuserMs[c]) + 8;
        total += iSamples(kMidMs) + 8;

        for (U32 s = 0; s < ISNDREVERB_STAGES; s++)
        {
            total += iSamples(iLateMs(c, s)) + 8;
        }
    }

    sArena = (F32*)calloc(total, sizeof(F32));
    if (sArena == NULL)
    {
        sRate = 0;
        return;
    }

    F32* p = sArena;
    for (U32 c = 0; c < 2; c++)
    {
        rchan* ch = &sChan[c];
        memset(ch, 0, sizeof(*ch));

        ch->in.len = iSamples(kInMs[c]) + 8;
        ch->in.buf = p;
        p += ch->in.len;

        ch->er_ap.len = iSamples(kErDiffuserMs[c]) + 8;
        ch->er_ap.buf = p;
        ch->er_ap.tap = ch->er_ap.len - 8;
        p += ch->er_ap.len;

        ch->mid.len = iSamples(kMidMs) + 8;
        ch->mid.buf = p;
        ch->mid.tap = ch->mid.len - 8;
        p += ch->mid.len;

        for (U32 s = 0; s < ISNDREVERB_STAGES; s++)
        {
            ch->ap[s].len = iSamples(iLateMs(c, s)) + 8;
            ch->ap[s].buf = p;
            ch->ap[s].tap = ch->ap[s].len - 8;
            p += ch->ap[s].len;
        }
    }

    sRampStep = 1.0f / (kRampSeconds * (F32)rate);
    sDiffusion = 0.0f;
    sErLevel = 0.0f;
    sRoomFilter = 0.0f;
    sMidC1 = 0.0f;
    sMidC2 = 0.0f;
    sMix = 0.0f;
    sMixTarget = 0.0f;
    sSilenced = true;
    sReady = true;
}

void iSndReverbExit()
{
    free(sArena);
    sArena = NULL;
    sReady = false;
    sRate = 0;
    memset(sChan, 0, sizeof(sChan));
}

// The decay gain and damping pole for one delay of `tap` samples, and the
// energy that one such stage passes for a broadband input -- which is what the
// output level has to be normalised by.
static F32 iDecayCoeffs(U32 tap, F32 decay, F32 hf_ratio, F32 hf_ref_rad, F32* c1_out,
                        F32* c2_out)
{
    F32 seconds = (F32)tap / (F32)sRate;

    // RT60: sixty decibels over decay_time, so a loop of period T needs this
    // much gain per pass.
    F32 c1 = powf(10.0f, (seconds * -60.0f / decay) / 20.0f);
    F32 c2 = 0.0f;

    // The damping filter is (1 - c2) / (1 - c2 z^-1): unity at DC, so the low
    // end decays at exactly decay_time, and (1 - c2) / (1 + c2) at the top, set
    // so the high end decays at decay_time * decay_hf_ratio instead. A ratio
    // above one puts c2 below zero, which is the case a plain lowpass cannot
    // reach and the reason this is not one.
    F32 denom = 1.0f - cosf(hf_ref_rad);
    if (denom > 1e-9f)
    {
        F32 c21 = (powf(c1, 2.0f - 2.0f / hf_ratio) - 1.0f) / denom;

        if (c21 != 0.0f && c21 == c21)
        {
            F32 c22 = -2.0f * c21 - 2.0f;
            F32 c23sq = c22 * c22 - c21 * c21 * 4.0f;
            F32 c23 = (c23sq > 0.0f) ? sqrtf(c23sq) : 0.0f;

            c2 = (c23 - c22) / (c21 + c21);
            if (c2 > 1.0f || c2 < -1.0f)
            {
                c2 = (-c22 - c23) / (c21 + c21);
            }
            if (c2 != c2)
            {
                c2 = 0.0f;
            }
        }
    }

    *c1_out = c1;
    *c2_out = c2;

    F32 c1sq = c1 * c1;
    F32 diff2 = sDiffusion * sDiffusion;
    F32 open = 1.0f - diff2 * c1sq;
    if (open < 1e-6f)
    {
        open = 1e-6f;
    }

    return diff2 + c1sq / open * (1.0f - diff2) * (1.0f - diff2);
}

void iSndReverbSet(const iSndHostReverb* params)
{
    if (!sReady)
    {
        return;
    }

    if (params == NULL)
    {
        // Only the target moves. The tail keeps running while the ramp takes it
        // down, and the process step clears the state once it is silent.
        sMixTarget = 0.0f;
        return;
    }

    F32 decay = params->decay_time;
    if (decay < 0.1f)
    {
        decay = 0.1f;
    }

    F32 hf_ratio = params->decay_hf_ratio;
    if (hf_ratio < 0.1f)
    {
        hf_ratio = 0.1f;
    }

    F32 diffusion = params->diffusion * 0.01f;
    if (diffusion < 0.0f) diffusion = 0.0f;
    if (diffusion > 1.0f) diffusion = 1.0f;

    F32 density = params->density * 0.01f;
    if (density < 0.0f) density = 0.0f;
    if (density > 1.0f) density = 1.0f;

    // Diffusion is the echo density of the tail, and it arrives as the
    // coefficient of every allpass in the chain. At the maximum it is the
    // reciprocal of the golden ratio.
    sDiffusion = diffusion * kGolden;

    // Density is the modal density -- how closely spaced the resonances are --
    // and longer delays put them closer together, so it scales the late taps.
    // At the maximum this works out to exactly one.
    F32 density_scale = (density + 0.1f) * 0.9091f;
    if (density_scale > 1.0f)
    {
        density_scale = 1.0f;
    }

    // I3DL2's own limits on the two delays. The input lines are sized from
    // these, so clamping here is what keeps every tap inside them.
    F32 refl_delay = params->reflections_delay;
    if (refl_delay < 0.0f) refl_delay = 0.0f;
    if (refl_delay > 0.3f) refl_delay = 0.3f;

    F32 rev_delay = params->reverb_delay;
    if (rev_delay < 0.005f) rev_delay = 0.005f;
    if (rev_delay > 0.1f) rev_delay = 0.1f;

    // Above the reference frequency when the high end decays faster, and at
    // the top of the spectrum when it decays slower -- there is no shelf to
    // place in that direction, only a tilt.
    F32 hf_ref_rad = (2.0f * 3.14159265358979323846f) * params->hf_reference / (F32)sRate;
    if (hf_ratio > 1.0f)
    {
        hf_ref_rad = 3.14159265358979323846f;
    }

    // The room filter: unity at DC, room_hf at the reference. At the zero the
    // game asks for it is exactly a pass-through.
    F32 room_hf = powf(10.0f, (F32)params->room_hf / 1000.0f);
    if (room_hf == 1.0f)
    {
        sRoomFilter = 0.0f;
    }
    else
    {
        F32 freq = cosf(params->hf_reference * (2.0f * 3.14159265358979323846f) / (F32)sRate);
        if (freq > 0.9999f)
        {
            freq = 0.9999f;
        }

        F32 disc = freq * (room_hf * room_hf * freq * 4.0f) + room_hf * 8.0f -
                   room_hf * room_hf * 4.0f - room_hf * freq * 8.0f;
        F32 root = (disc > 0.0f) ? sqrtf(disc) : 0.0f;

        F32 rf = (freq * (room_hf + room_hf) - 2.0f + root) / (room_hf + room_hf - 2.0f);
        if (rf < 0.0f) rf = 0.0f;
        if (rf > 1.0f) rf = 1.0f;
        sRoomFilter = rf;
    }

    sErLevel = iFromMillibels(params->room + params->reflections);
    if (sErLevel > 1.0f)
    {
        sErLevel = 1.0f;
    }
    sErLevel *= kErLevelScale;

    // The taps, and the coefficients that follow from them.
    F32 energy[2][ISNDREVERB_STAGES];

    for (U32 c = 0; c < 2; c++)
    {
        rchan* ch = &sChan[c];

        U32 refl_samples = (U32)(refl_delay * (F32)sRate);
        F32 window = rev_delay * (F32)sRate;

        for (U32 t = 0; t < ISNDREVERB_ERTAPS; t++)
        {
            U32 v = refl_samples + (U32)(window * kErFrac[c][t]);
            if (v >= ch->in.len)
            {
                v = ch->in.len - 1;
            }
            ch->er_tap[t] = v;
        }

        // The late chain is fed from the same line, at the onset of the late
        // reverberation -- one reverb_delay after the first reflection, which
        // is how I3DL2 defines that field.
        F32 feed = (F32)refl_samples + window;
        if (c == 0)
        {
            feed += kLateFeedExtraMs * 0.001f * (F32)sRate;
        }

        U32 fv = (U32)feed;
        if (fv >= ch->in.len)
        {
            fv = ch->in.len - 1;
        }
        ch->late_feed_tap = fv;

        for (U32 s = 0; s < ISNDREVERB_STAGES; s++)
        {
            U32 tap = (U32)(iSamples(iLateMs(c, s)) * density_scale);
            if (tap < 1)
            {
                tap = 1;
            }
            if (tap >= ch->ap[s].len)
            {
                tap = ch->ap[s].len - 1;
            }
            ch->ap[s].tap = tap;

            energy[c][s] = iDecayCoeffs(tap, decay, hf_ratio, hf_ref_rad, &ch->c1[s], &ch->c2[s]);
        }
    }

    iDecayCoeffs(sChan[0].mid.tap, decay, hf_ratio, hf_ref_rad, &sMidC1, &sMidC2);

    // Normalising the late level.
    //
    // The chain's own gain depends on every stage, so the level the parameters
    // ask for is only meaningful once it has been divided out. The running
    // products below follow the signal along the chain, and each stage's share
    // is weighted by the square of the weight its output is tapped with.
    F32 lt = 1.0f;
    F32 rt = 1.0f;
    F32 lsum = 0.0f;
    F32 rsum = 0.0f;

    static const U32 kOrder[4] = { 5, 4, 3, 2 };
    static const F32 kShare[4] = { 0.0225f, 0.04f, 0.1225f, 0.1444f };

    for (U32 i = 0; i < 4; i++)
    {
        lt *= energy[0][kOrder[i]];
        rt *= energy[1][kOrder[i]];
        lsum += lt * kShare[i];
        rsum += rt * kShare[i];
    }

    lt *= sMidC1 * sMidC1;
    rt *= sMidC1 * sMidC1;

    // Stage one's output crosses to the other channel, so its share does too.
    lt *= energy[0][1];
    rt *= energy[1][1];
    lsum += rt * 0.1444f;
    rsum += lt * 0.1444f;

    lt *= energy[0][0];
    rt *= energy[1][0];
    lsum += lt * 0.1444f;
    rsum += rt * 0.1444f;

    F32 level = iFromMillibels(params->room + params->reverb);
    if (level > 1.0f)
    {
        level = 1.0f;
    }

    F32 mono_inv = 1.0f - ((lt + rt) * 0.5f);
    if (mono_inv < 1e-6f)
    {
        mono_inv = 1e-6f;
    }
    if (lsum < 1e-9f) lsum = 1e-9f;
    if (rsum < 1e-9f) rsum = 1e-9f;

    sChan[0].level = level * sqrtf(mono_inv / lsum);
    sChan[1].level = level * sqrtf(mono_inv / rsum);

    // room_rolloff_factor is not represented, and at the zero the game asks for
    // there is nothing to represent: it scales the room effect per source with
    // distance, and this seam is downstream of the pan, holding one stereo pair
    // and no distances at all.

    sMixTarget = 1.0f;
    sSilenced = false;
}

bool iSndReverbIdle()
{
    return !sReady || (sMixTarget == 0.0f && sMix == 0.0f);
}

// ---------------------------------------------------------------------------
// The process step

// One absorbent allpass: the delayed signal is damped and decayed on its way
// round, and the allpass itself is what spreads a single echo into many.
static inline F32 iStage(rline* line, F32* hist, F32 c1, F32 c2, F32 in)
{
    F32 delayed = iLineTap(line);
    *hist = (*hist - delayed) * c2 + delayed;

    F32 out = *hist * c1 + in * sDiffusion;
    iLineSet(line, in - out * sDiffusion);

    return out;
}

static F32 iEarly(rchan* ch, U32 c)
{
    F32 early = 0.0f;
    for (U32 t = 0; t < ISNDREVERB_ERTAPS; t++)
    {
        early += kErWeight[c][t] * iLineGet(&ch->in, ch->er_tap[t]);
    }

    // One allpass over the taps, so the reflections read as a surface rather
    // than as a handful of discrete echoes.
    F32 prev = early;
    early = iLineTap(&ch->er_ap) + early * kGolden;
    iLineSet(&ch->er_ap, prev - early * kGolden);

    return early * sErLevel;
}

void iSndReverbProcess(F32* stereo, U32 frames)
{
    if (!sReady || stereo == NULL || frames == 0)
    {
        return;
    }

    if (iSndReverbIdle())
    {
        // Only once. Clearing every silent block would be a memset of a few
        // hundred kilobytes per block for as long as the game is above ground.
        if (!sSilenced)
        {
            iClearState();
            sSilenced = true;
        }
        return;
    }

    rchan* L = &sChan[0];
    rchan* R = &sChan[1];

    for (U32 n = 0; n < frames; n++)
    {
        if (sMix < sMixTarget)
        {
            sMix += sRampStep;
            if (sMix > sMixTarget)
            {
                sMix = sMixTarget;
            }
        }
        else if (sMix > sMixTarget)
        {
            sMix -= sRampStep;
            if (sMix < sMixTarget)
            {
                sMix = sMixTarget;
            }
        }

        F32 in_l = stereo[n * 2 + 0];
        F32 in_r = stereo[n * 2 + 1];

        // The room filter, then into the line the early taps read from.
        F32 room_l = (L->hist_room - in_l) * sRoomFilter + in_l;
        L->hist_room = room_l;
        iLineSet(&L->in, room_l);

        F32 room_r = (R->hist_room - in_r) * sRoomFilter + in_r;
        R->hist_room = room_r;
        iLineSet(&R->in, room_r);

        F32 early_l = iEarly(L, 0);
        F32 early_r = iEarly(R, 1);

        // Close the loop: what came out of the chain last frame, plus what the
        // input line has reached the late onset with.
        L->hist_loop += iLineGet(&L->in, L->late_feed_tap);
        R->hist_loop += iLineGet(&R->in, R->late_feed_tap);

        // The energy-preserving matrix. Each chain is fed a mix of both loops,
        // which is what stops the two sides drifting into separate rooms.
        F32 half = -L->hist_loop * kMatrix;
        F32 sig_l = R->hist_loop * kMatrix + half;
        F32 sig_r = half - R->hist_loop * kMatrix;

        F32 out_l = 0.0f;
        F32 out_r = 0.0f;
        F32 cross_l = 0.0f;
        F32 cross_r = 0.0f;

        // Stages five down to two, shortest delay first.
        for (U32 i = 0; i < 4; i++)
        {
            U32 s = 5 - i;

            sig_l = iStage(&L->ap[s], &L->hist[s], L->c1[s], L->c2[s], sig_l);
            sig_r = iStage(&R->ap[s], &R->hist[s], R->c1[s], R->c2[s], sig_r);

            out_l += sig_l * kOutWeight[s];
            out_r += sig_r * kOutWeight[s];
        }

        // The extra delay in the middle of the chain, damped like the rest.
        iLineSet(&L->mid, sig_l);
        F32 mid_l = iLineTap(&L->mid) * sMidC1;
        L->hist_mid = (L->hist_mid - mid_l) * sMidC2 + mid_l;

        iLineSet(&R->mid, sig_r);
        F32 mid_r = iLineTap(&R->mid) * sMidC1;
        R->hist_mid = (R->hist_mid - mid_r) * sMidC2 + mid_r;

        // Stage one, fed from that delay rather than from the stage above it.
        sig_l = iStage(&L->ap[1], &L->hist[1], L->c1[1], L->c2[1], L->hist_mid);
        sig_r = iStage(&R->ap[1], &R->hist[1], R->c1[1], R->c2[1], R->hist_mid);

        // Its share is the one that crosses.
        cross_l = sig_r * kCrossWeight;
        cross_r = sig_l * kCrossWeight;

        // Stage zero, the longest, whose output is also what closes the loop.
        sig_l = iStage(&L->ap[0], &L->hist[0], L->c1[0], L->c2[0], sig_l);
        sig_r = iStage(&R->ap[0], &R->hist[0], R->c1[0], R->c2[0], sig_r);

        out_l += sig_l * kOutWeight[0];
        out_r += sig_r * kOutWeight[0];

        L->hist_loop = sig_l;
        R->hist_loop = sig_r;

        F32 late_l = (out_l + cross_l) * L->level;
        F32 late_r = (out_r + cross_r) * R->level;

        for (U32 c = 0; c < 2; c++)
        {
            rchan* ch = &sChan[c];

            iLineAdvance(&ch->in);
            iLineAdvance(&ch->er_ap);
            iLineAdvance(&ch->mid);

            for (U32 s = 0; s < ISNDREVERB_STAGES; s++)
            {
                iLineAdvance(&ch->ap[s]);
            }
        }

        // Added, never replacing: the dry path is the game's own mix and pan,
        // and this is a send.
        stereo[n * 2 + 0] = in_l + (early_l + late_l) * sMix;
        stereo[n * 2 + 1] = in_r + (early_r + late_r) * sMix;
    }
}
