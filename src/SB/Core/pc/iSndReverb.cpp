#include "iSndReverb.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// See iSndReverb.h for what this is and which half of it is faithful.

#define ISNDREVERB_COMBS 8
#define ISNDREVERB_ALLPASSES 4
#define ISNDREVERB_TAPS 6

// Schroeder-Moorer's delay lengths, at the 44100 Hz they were chosen for, and
// scaled to the output rate below. Mutually prime-ish, which is the point: a
// set of combs whose lengths share factors rings at the common factor rather
// than filling in between.
static const U32 kCombTune[ISNDREVERB_COMBS] = { 1116, 1188, 1277, 1356,
                                                 1422, 1491, 1557, 1617 };
static const U32 kAllpassTune[ISNDREVERB_ALLPASSES] = { 556, 441, 341, 225 };

// Added to the right channel's lengths, so the two sides decorrelate.
static const U32 kSpreadTune = 23;

// The early reflection pattern. I3DL2 fixes how loud the early reflections are
// and when the first one arrives; it says nothing about the ones after it, and
// the Xbox's own pattern is inside DSP microcode. So this part is chosen. Both
// channels start at zero -- that tap is reflections_delay exactly, which is the
// part the parameters do specify -- and differ after it.
//
// Alternating signs, because a set of same-signed taps a few milliseconds apart
// is a comb filter and sounds like one.
static const F32 kTapMsL[ISNDREVERB_TAPS] = { 0.0f, 5.3f, 11.7f, 17.1f, 24.3f, 31.9f };
static const F32 kTapMsR[ISNDREVERB_TAPS] = { 0.0f, 6.9f, 10.4f, 19.3f, 22.7f, 29.1f };
static const F32 kTapGain[ISNDREVERB_TAPS] = { 1.0f, -0.72f, 0.58f, -0.41f, 0.32f, -0.23f };

// Long enough for I3DL2's own limits: reflections_delay tops out at 300 ms and
// reverb_delay at 100 ms, and the last tap adds another 32.
static const F32 kLineSeconds = 0.45f;

// How long the effect takes to fade in or out. The game only changes this at a
// scene load, where nothing much is audible, but a step in a gain is a click
// and a click costs nothing to avoid.
static const F32 kRampSeconds = 0.05f;

// A loop gain of exactly one is an oscillator.
static const F32 kMaxLoopGain = 0.9995f;

struct rcomb
{
    F32* buf;
    U32 cap;
    U32 len;
    U32 idx;

    // The one-pole state of the shelf in the feedback path.
    F32 lp;

    // Loop gain at DC and at high frequency. decay_hf_ratio above one puts
    // g_hf above g_lf, which is what "high frequencies ring longer" means.
    F32 g_lf;
    F32 g_hf;
};

struct rallpass
{
    F32* buf;
    U32 cap;
    U32 len;
    U32 idx;
    F32 g;
};

struct rchan
{
    rcomb comb[ISNDREVERB_COMBS];
    rallpass ap[ISNDREVERB_ALLPASSES];

    // One line serves both the early taps and the late network's pre-delay,
    // because they are the same signal read at different distances.
    F32* line;
    U32 line_cap;
    U32 line_idx;

    U32 tap[ISNDREVERB_TAPS];
    U32 late_tap;

    F32 hf_lp;
};

static bool sReady;
static U32 sRate;
static F32* sArena;
static rchan sChan[2];

static F32 sTapGain[ISNDREVERB_TAPS];

// Shared by every comb: the corner is hf_reference, which is one number for the
// whole reverb.
static F32 sDamp;

static F32 sLateNorm;
static F32 sEarlyGain;
static F32 sLateGain;

static F32 sHfGain;
static F32 sHfDamp;

// 0 or 1, and where the ramp has got to between them.
static F32 sMix;
static F32 sMixTarget;
static F32 sRampStep;

static bool sSilenced;

// ---------------------------------------------------------------------------
// Setup

static U32 iScaled(U32 tune)
{
    return (U32)((F32)tune * (F32)sRate / 44100.0f + 0.5f);
}

// Millibels -- hundredths of a decibel -- to a linear amplitude.
static F32 iFromMillibels(S32 mb)
{
    return powf(10.0f, (F32)mb / 2000.0f);
}

// The coefficient of a one-pole lowpass whose corner sits at `hz`.
static F32 iOnePole(F32 hz)
{
    F32 nyquist = (F32)sRate * 0.49f;

    if (hz < 20.0f)
    {
        hz = 20.0f;
    }
    if (hz > nyquist)
    {
        hz = nyquist;
    }

    return expf(-2.0f * 3.14159265358979323846f * hz / (F32)sRate);
}

static void iClearState()
{
    for (U32 c = 0; c < 2; c++)
    {
        rchan* ch = &sChan[c];

        memset(ch->line, 0, sizeof(F32) * ch->line_cap);
        ch->line_idx = 0;
        ch->hf_lp = 0.0f;

        for (U32 i = 0; i < ISNDREVERB_COMBS; i++)
        {
            memset(ch->comb[i].buf, 0, sizeof(F32) * ch->comb[i].cap);
            ch->comb[i].idx = 0;
            ch->comb[i].lp = 0.0f;
        }

        for (U32 i = 0; i < ISNDREVERB_ALLPASSES; i++)
        {
            memset(ch->ap[i].buf, 0, sizeof(F32) * ch->ap[i].cap);
            ch->ap[i].idx = 0;
        }
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

    U32 spread = iScaled(kSpreadTune);
    U32 line_cap = (U32)(kLineSeconds * (F32)rate) + 1;

    // Sized for the longest each delay can be asked to be: density shortens the
    // combs from here, and the two delay parameters are clamped to I3DL2's own
    // limits, which is what kLineSeconds is derived from. So a parameter change
    // never needs to allocate.
    U32 total = 0;
    for (U32 c = 0; c < 2; c++)
    {
        U32 off = (c == 1) ? spread : 0;

        total += line_cap;
        for (U32 i = 0; i < ISNDREVERB_COMBS; i++)
        {
            total += iScaled(kCombTune[i]) + off + 1;
        }
        for (U32 i = 0; i < ISNDREVERB_ALLPASSES; i++)
        {
            total += iScaled(kAllpassTune[i]) + off + 1;
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
        U32 off = (c == 1) ? spread : 0;

        memset(ch, 0, sizeof(*ch));

        ch->line = p;
        ch->line_cap = line_cap;
        p += line_cap;

        for (U32 i = 0; i < ISNDREVERB_COMBS; i++)
        {
            ch->comb[i].cap = iScaled(kCombTune[i]) + off + 1;
            ch->comb[i].len = ch->comb[i].cap;
            ch->comb[i].buf = p;
            p += ch->comb[i].cap;
        }

        for (U32 i = 0; i < ISNDREVERB_ALLPASSES; i++)
        {
            ch->ap[i].cap = iScaled(kAllpassTune[i]) + off + 1;
            ch->ap[i].len = ch->ap[i].cap;
            ch->ap[i].buf = p;
            p += ch->ap[i].cap;
        }
    }

    // Normalise the early taps to unit power, so that the level the parameters
    // ask for is the level they get rather than the level times however many
    // taps this pattern happens to have.
    F32 power = 0.0f;
    for (U32 t = 0; t < ISNDREVERB_TAPS; t++)
    {
        power += kTapGain[t] * kTapGain[t];
    }

    F32 norm = (power > 0.0f) ? (1.0f / sqrtf(power)) : 1.0f;
    for (U32 t = 0; t < ISNDREVERB_TAPS; t++)
    {
        sTapGain[t] = kTapGain[t] * norm;
    }

    sRampStep = 1.0f / (kRampSeconds * (F32)rate);
    sDamp = 0.0f;
    sLateNorm = 0.0f;
    sEarlyGain = 0.0f;
    sLateGain = 0.0f;
    sHfGain = 1.0f;
    sHfDamp = 0.0f;
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
    F32 density = params->density * 0.01f;
    if (diffusion < 0.0f) diffusion = 0.0f;
    if (diffusion > 1.0f) diffusion = 1.0f;
    if (density < 0.0f) density = 0.0f;
    if (density > 1.0f) density = 1.0f;

    sDamp = iOnePole(params->hf_reference);

    // I3DL2's own limits on the two delays. kLineSeconds is sized from these,
    // so clamping here is what keeps every tap inside the line.
    F32 refl_delay = params->reflections_delay;
    if (refl_delay < 0.0f) refl_delay = 0.0f;
    if (refl_delay > 0.3f) refl_delay = 0.3f;

    F32 rev_delay = params->reverb_delay;
    if (rev_delay < 0.0f) rev_delay = 0.0f;
    if (rev_delay > 0.1f) rev_delay = 0.1f;

    // Density scales the delay lengths, which is what changes how closely
    // spaced the modes are. At the maximum the network runs at its full
    // lengths, which is the case the game asks for.
    F32 density_scale = 0.65f + 0.35f * density;

    // Diffusion sets how hard the allpasses smear. At the maximum they are as
    // strong as they can be without the tail turning metallic.
    F32 ap_g = 0.2f + 0.5f * diffusion;

    F32 power_sum = 0.0f;

    for (U32 c = 0; c < 2; c++)
    {
        rchan* ch = &sChan[c];
        const F32* tap_ms = (c == 0) ? kTapMsL : kTapMsR;

        U32 refl_samples = (U32)(refl_delay * (F32)sRate);

        for (U32 t = 0; t < ISNDREVERB_TAPS; t++)
        {
            U32 v = refl_samples + (U32)(tap_ms[t] * 0.001f * (F32)sRate);
            if (v >= ch->line_cap)
            {
                v = ch->line_cap - 1;
            }
            ch->tap[t] = v;
        }

        // The late network starts one reverb_delay after the first reflection,
        // which is how I3DL2 defines that field -- not after the direct sound.
        U32 late = (U32)((refl_delay + rev_delay) * (F32)sRate);
        if (late >= ch->line_cap)
        {
            late = ch->line_cap - 1;
        }
        ch->late_tap = late;

        for (U32 i = 0; i < ISNDREVERB_COMBS; i++)
        {
            rcomb* cb = &ch->comb[i];

            U32 len = (U32)((F32)cb->cap * density_scale);
            if (len < 8)
            {
                len = 8;
            }
            if (len > cb->cap)
            {
                len = cb->cap;
            }
            cb->len = len;

            // Never clears: a parameter change while something is audible would
            // click, and clamping is all that safety needs.
            if (cb->idx >= cb->len)
            {
                cb->idx = 0;
            }

            // RT60 is the time to decay by 60 dB, so a loop of period T needs
            // a gain of 10^(-3T/RT60) per pass. Doing it separately at the two
            // ends of the spectrum is what decay_hf_ratio asks for.
            F32 seconds = (F32)len / (F32)sRate;

            F32 g_lf = powf(10.0f, -3.0f * seconds / decay);
            F32 g_hf = powf(10.0f, -3.0f * seconds / (decay * hf_ratio));

            if (g_lf > kMaxLoopGain) g_lf = kMaxLoopGain;
            if (g_hf > kMaxLoopGain) g_hf = kMaxLoopGain;
            if (g_lf < 0.0f) g_lf = 0.0f;
            if (g_hf < 0.0f) g_hf = 0.0f;

            cb->g_lf = g_lf;
            cb->g_hf = g_hf;

            if (c == 0)
            {
                // A comb of loop gain g multiplies the power of a broadband
                // input by 1/(1-g*g). Averaging the two ends estimates what the
                // eight of them together will do, which is what the late level
                // has to be divided by if it is to mean what it says.
                power_sum += 0.5f * (1.0f / (1.0f - g_lf * g_lf));
                power_sum += 0.5f * (1.0f / (1.0f - g_hf * g_hf));
            }
        }

        for (U32 i = 0; i < ISNDREVERB_ALLPASSES; i++)
        {
            ch->ap[i].g = ap_g;
            if (ch->ap[i].idx >= ch->ap[i].len)
            {
                ch->ap[i].idx = 0;
            }
        }
    }

    sLateNorm = (power_sum > 0.0f) ? (1.0f / sqrtf(power_sum)) : 1.0f;

    // Both levels are relative to room, which is I3DL2's definition, so the
    // absolute level of each path is the sum of the two in millibels.
    sEarlyGain = iFromMillibels(params->room + params->reflections);
    sLateGain = iFromMillibels(params->room + params->reverb);

    // Unity at DC, room_hf above hf_reference. At the zero the game asks for
    // this is exactly a no-op, which is why it costs nothing to honour.
    sHfGain = iFromMillibels(params->room_hf);
    sHfDamp = sDamp;

    // room_rolloff_factor is not represented, and at the zero the game asks for
    // there is nothing to represent: it scales the room effect with distance
    // per source, and zero means do not. Anything else would need per-voice
    // sends, which this seam is downstream of -- it sees one mixed, panned
    // stereo pair and no distances at all.

    sMixTarget = 1.0f;
    sSilenced = false;
}

bool iSndReverbIdle()
{
    return !sReady || (sMixTarget == 0.0f && sMix == 0.0f);
}

// ---------------------------------------------------------------------------
// The process step

static F32 iChannel(rchan* ch, F32 in)
{
    ch->line[ch->line_idx] = in;

    F32 early = 0.0f;
    for (U32 t = 0; t < ISNDREVERB_TAPS; t++)
    {
        U32 k = ch->line_idx + ch->line_cap - ch->tap[t];
        if (k >= ch->line_cap)
        {
            k -= ch->line_cap;
        }
        early += sTapGain[t] * ch->line[k];
    }

    U32 lk = ch->line_idx + ch->line_cap - ch->late_tap;
    if (lk >= ch->line_cap)
    {
        lk -= ch->line_cap;
    }
    F32 late_in = ch->line[lk];

    ch->line_idx++;
    if (ch->line_idx >= ch->line_cap)
    {
        ch->line_idx = 0;
    }

    F32 acc = 0.0f;
    for (U32 i = 0; i < ISNDREVERB_COMBS; i++)
    {
        rcomb* cb = &ch->comb[i];

        F32 out = cb->buf[cb->idx];

        // A one-pole shelf: unity-gain lowpass, then blended so the loop gain
        // is g_lf at DC and g_hf at the top. The plain lowpass every textbook
        // reverb uses here can only make high frequencies decay faster, and
        // this room wants the opposite.
        cb->lp = out * (1.0f - sDamp) + cb->lp * sDamp;
        cb->buf[cb->idx] = late_in + (cb->g_hf * out + (cb->g_lf - cb->g_hf) * cb->lp);

        cb->idx++;
        if (cb->idx >= cb->len)
        {
            cb->idx = 0;
        }

        acc += out;
    }

    acc *= sLateNorm;

    for (U32 i = 0; i < ISNDREVERB_ALLPASSES; i++)
    {
        rallpass* ap = &ch->ap[i];

        // A true allpass, not freeverb's approximation to one: it has to leave
        // the level alone, or it would undo the normalisation above.
        F32 delayed = ap->buf[ap->idx];
        F32 v = acc + ap->g * delayed;
        ap->buf[ap->idx] = v;
        acc = delayed - ap->g * v;

        ap->idx++;
        if (ap->idx >= ap->len)
        {
            ap->idx = 0;
        }
    }

    F32 wet = early * sEarlyGain + acc * sLateGain;

    ch->hf_lp = wet * (1.0f - sHfDamp) + ch->hf_lp * sHfDamp;
    wet = sHfGain * wet + (1.0f - sHfGain) * ch->hf_lp;

    return wet;
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

        F32 l = stereo[n * 2 + 0];
        F32 r = stereo[n * 2 + 1];

        // The reverb is fed the mix rather than either side of it. A room does
        // not reverberate the left and right halves of itself separately, and
        // the two sides differ here because the networks do, not because the
        // input did.
        F32 in = (l + r) * 0.5f;

        F32 wl = iChannel(&sChan[0], in);
        F32 wr = iChannel(&sChan[1], in);

        // Added, never replacing: the dry path is the game's own mix and pan,
        // and this is a send.
        stereo[n * 2 + 0] = l + wl * sMix;
        stereo[n * 2 + 1] = r + wr * sMix;
    }
}
