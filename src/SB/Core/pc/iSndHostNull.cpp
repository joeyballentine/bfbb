#include "iSndHost.h"
#include "iHost.h"

#include <string.h>

// The backend that is always available: silent, but it keeps time.
//
// The obvious null backend reports nothing ever playing. That is wrong here in
// a way that would be blamed on the game rather than on the audio: a great deal
// of BFBB's logic waits on a sound. zTalkBox holds a line on screen until its
// voice clip finishes, cutscenes gate on iSndIsPlayingByHandle, and NPCs stagger
// barks by asking whether the previous one is done. A backend that finishes
// every sound instantly makes dialogue flash past and cutscenes desynchronise --
// bugs that look like the port getting the game code wrong.
//
// So this one plays nothing and still finishes at the right moment: it records
// the sample count and rate, and reports the voice as playing until that much
// time has passed on the monotonic clock. Pitch scales the remaining duration,
// because that is what changing the playback rate does. Looping voices never
// finish on their own, as on hardware, and are stopped explicitly.
//
// It is not a placeholder for a missing implementation. It is what the port
// does when it is built without an audio library, and it is the correct answer
// in that configuration: no sound, and every timing the game derives from sound
// still correct. A real backend replaces this file in the link, not around it.

struct null_voice
{
    bool acquired;
    bool playing;
    bool paused;
    bool looping;

    U64 start_ns;
    U64 duration_ns;

    // Time already served when a pause began, so a resumed voice finishes the
    // remainder rather than restarting its deadline.
    U64 elapsed_at_pause_ns;

    F32 pitch;
};

static null_voice sVoices[ISNDHOST_MAX_VOICES];

static U64 iDurationNs(const iSndHostSample* s, F32 pitch)
{
    if (s->sample_rate == 0 || pitch <= 0.0f)
    {
        return 0;
    }

    double seconds = (double)s->num_samples / ((double)s->sample_rate * (double)pitch);
    return (U64)(seconds * 1000000000.0);
}

void iSndHostInit()
{
    memset(sVoices, 0, sizeof(sVoices));
    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        sVoices[i].pitch = 1.0f;
    }
}

void iSndHostExit()
{
    memset(sVoices, 0, sizeof(sVoices));
}

S32 iSndHostAcquire(U32 priority)
{
    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        if (!sVoices[i].acquired)
        {
            memset(&sVoices[i], 0, sizeof(sVoices[i]));
            sVoices[i].acquired = true;
            sVoices[i].pitch = 1.0f;
            return i;
        }
    }

    // Every voice busy. Retail's AXAcquireVoice would evict a lower-priority
    // one here; with nothing audible there is nothing to gain by evicting, and
    // failing is a state the game already handles.
    return -1;
}

static bool iValid(S32 voice)
{
    return voice >= 0 && voice < ISNDHOST_MAX_VOICES && sVoices[voice].acquired;
}

void iSndHostRelease(S32 voice)
{
    if (iValid(voice))
    {
        memset(&sVoices[voice], 0, sizeof(sVoices[voice]));
    }
}

void iSndHostStart(S32 voice, const iSndHostSample* sample)
{
    if (!iValid(voice) || sample == NULL)
    {
        return;
    }

    null_voice* v = &sVoices[voice];
    v->playing = true;
    v->paused = false;
    v->looping = sample->looping;
    v->start_ns = iHostMonotonicNs();
    v->duration_ns = iDurationNs(sample, v->pitch);
    v->elapsed_at_pause_ns = 0;
}

void iSndHostStop(S32 voice)
{
    if (iValid(voice))
    {
        sVoices[voice].playing = false;
        sVoices[voice].paused = false;
    }
}

void iSndHostPause(S32 voice, bool paused)
{
    if (!iValid(voice) || !sVoices[voice].playing)
    {
        return;
    }

    null_voice* v = &sVoices[voice];
    if (paused == v->paused)
    {
        return;
    }

    if (paused)
    {
        v->elapsed_at_pause_ns = iHostMonotonicNs() - v->start_ns;
        v->paused = true;
    }
    else
    {
        // Restart the deadline from now, minus what had already elapsed.
        v->start_ns = iHostMonotonicNs() - v->elapsed_at_pause_ns;
        v->paused = false;
    }
}

void iSndHostSetVol(S32 voice, F32 left, F32 right)
{
    // Nothing is audible, so there is nothing to apply. Deliberately not
    // recorded: no caller can observe it, and storing it would suggest the
    // value means something here.
}

void iSndHostSetPitch(S32 voice, F32 pitch)
{
    if (!iValid(voice) || pitch <= 0.0f)
    {
        return;
    }

    null_voice* v = &sVoices[voice];

    // Pitch changes the remaining playback time, so rescale what is left rather
    // than the whole duration -- a pitch bend halfway through a clip must not
    // move the part already played.
    if (v->playing && !v->paused && v->pitch > 0.0f)
    {
        U64 now = iHostMonotonicNs();
        U64 elapsed = now - v->start_ns;
        if (elapsed < v->duration_ns)
        {
            U64 remaining = v->duration_ns - elapsed;
            double scale = (double)v->pitch / (double)pitch;
            v->duration_ns = elapsed + (U64)((double)remaining * scale);
        }
    }

    v->pitch = pitch;
}

bool iSndHostIsPlaying(S32 voice)
{
    if (!iValid(voice) || !sVoices[voice].playing)
    {
        return false;
    }

    null_voice* v = &sVoices[voice];

    if (v->paused)
    {
        return true;
    }

    // A looping voice runs until something stops it, as on hardware.
    if (v->looping)
    {
        return true;
    }

    return (iHostMonotonicNs() - v->start_ns) < v->duration_ns;
}

void iSndHostUpdate()
{
    // Retire voices whose time is up, so a caller polling iSndHostIsPlaying and
    // a caller watching the voice table agree.
    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        if (sVoices[i].acquired && sVoices[i].playing && !iSndHostIsPlaying(i))
        {
            sVoices[i].playing = false;
        }
    }
}

const char* iSndHostName()
{
    return "null (silent, but keeps time)";
}
