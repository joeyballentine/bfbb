#include "iSndHost.h"
#include "iHost.h"
#include "iSndReverb.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The audio backend: a software mixer feeding one SDL audio stream.
//
// The seam in iSndHost.h describes a device with 64 independent voices, each
// with its own rate, volume per side and pitch. No host audio API offers that
// directly -- SDL renders one stream, and the APIs that do offer voices
// (XAudio2, DirectSound) bring a submix graph, their own threading model and
// their own idea of what a voice is. Mixing here instead is both less code and
// a closer fit: the game's model becomes 64 float accumulations per output
// frame, and the only thing SDL is asked for is somewhere to put the result.
//
// It also makes the pitch and rate handling exact rather than approximate.
// Every voice is resampled to the device rate by stepping a fractional read
// position, so a sound authored at 22050 Hz played at pitch 1.3 on a 48 kHz
// device is one multiply, and the point where it runs out is known to the
// sample rather than to the frame.
//
// The stream is opened at the device's own rate in float stereo, so SDL has no
// conversion left to do on the way out. Which driver, what the hardware format
// is, and how stereo reaches a mono endpoint are all below the stream and all
// SDL's.
//
// **Failing to open a device is not an error.** A machine with no output, or
// with the endpoint in use exclusively, still has to run the game correctly,
// and a great deal of BFBB waits on sounds -- dialogue lines, cutscene beats,
// NPC barks. So the mixer runs either way: with a device the voices advance
// because the mixer consumed them, and without one they advance on the
// monotonic clock in iSndHostUpdate. The game cannot tell the difference, which
// is the whole point of iSndHostNull.cpp and is preserved here rather than
// replaced.

// ---------------------------------------------------------------------------
// Voices

struct hvoice
{
    bool acquired;
    bool playing;
    bool paused;
    bool looping;

    // Borrowed from iSndData's cache, which pins it for as long as the voice
    // holds it. NULL is a legal voice: it plays silently and still ends at the
    // right moment, which is what a missing or unreadable asset sounds like.
    const void* data;
    U32 frames;
    U32 channels;
    U32 bits;
    U32 rate;
    U32 loop_start;
    U32 loop_end;

    // Read position in source frames. Fractional because the source rate and
    // the device rate are unrelated.
    double pos;

    // Where the mix is heading, and where it is now. Ramped across a block
    // rather than applied at its start: the game moves volumes every frame as
    // an emitter approaches, and stepping the gain discontinuously at a block
    // boundary is audible as a click on exactly the sounds that move most.
    float volL;
    float volR;
    float curL;
    float curR;

    float pitch;
};

static hvoice sVoices[ISNDHOST_MAX_VOICES];
static SDL_Mutex* sLock;

// ---------------------------------------------------------------------------
// Device

static SDL_AudioStream* sStream;
static bool sSubsystem;

// The rate the stream runs at, which is the device's own. Also the rate the
// silent path steps voices at when there is no device at all.
static U32 sOutRate;

// What SDL says the device wants per callback. Reported in the backend name and
// used to size the mix buffer. The callback is free to ask for more than this,
// which the mix loop handles by running more than once.
static U32 sDeviceFrames;

static bool sDeviceUp;
static char sName[128];

// The silent path's clock. Only read and written by iSndHostUpdate, on the game
// thread, and only when there is no device.
static U64 sLastSilentNs;

// Scratch for one mixed block, in float. Sized on the endpoint buffer, which
// shared-mode WASAPI reports once at initialisation.
static float* sMixBuffer;
static U32 sMixBufferFrames;

// The environmental reverb the game last asked for. Kept here as well as in
// iSndReverb because the reverb's delay lengths depend on the output rate,
// which is not known until a device opens and changes if one is swapped: the
// game sets this once when a scene loads and must not have to set it again.
static iSndHostReverb sReverb;
static bool sReverbWanted;

// ---------------------------------------------------------------------------
// Mixing

static bool iVoiceValid(S32 voice)
{
    return voice >= 0 && voice < ISNDHOST_MAX_VOICES && sVoices[voice].acquired;
}

// The seam does not promise that iSndHostInit runs first -- the null backend
// tolerates being called cold, so this one has to as well. Only the game thread
// reaches these entry points, so no lock is needed to make the lock. If SDL
// will not give us a mutex, every lock below becomes a no-op, which is the
// right answer while there is no audio thread to race against.
static void iEnsureLock()
{
    if (sLock == NULL)
    {
        sLock = SDL_CreateMutex();
    }
}

// One source frame, as a float in -1..1, with linear interpolation between the
// two frames it falls between. `ch` is 0 for left and 1 for right; a mono
// source answers the same value to both.
static inline float iSampleAt(const hvoice* v, double pos, U32 ch)
{
    if (v->data == NULL)
    {
        return 0.0f;
    }

    U32 i0 = (U32)pos;
    if (i0 >= v->frames)
    {
        return 0.0f;
    }

    U32 i1 = i0 + 1;
    if (i1 >= v->frames)
    {
        // Past the end, hold the last frame rather than wrapping into whatever
        // follows in the cache.
        i1 = i0;
    }

    float frac = (float)(pos - (double)i0);
    U32 c = (v->channels > 1 && ch < v->channels) ? ch : 0;
    U32 stride = v->channels;

    if (v->bits == 16)
    {
        const S16* p = (const S16*)v->data;
        float a = (float)p[i0 * stride + c] * (1.0f / 32768.0f);
        float b = (float)p[i1 * stride + c] * (1.0f / 32768.0f);
        return a + (b - a) * frac;
    }

    const U8* p = (const U8*)v->data;
    float a = ((float)p[i0 * stride + c] - 128.0f) * (1.0f / 128.0f);
    float b = ((float)p[i1 * stride + c] - 128.0f) * (1.0f / 128.0f);
    return a + (b - a) * frac;
}

// Advance one voice past the end of its data, looping if it should. Returns
// false when the voice has finished.
static bool iWrap(hvoice* v)
{
    // A looping voice may turn round before the data runs out, which is what a
    // soundtrack override does. Everything else ends where the samples do.
    U32 end = v->frames;
    if (v->looping && v->loop_end != 0 && v->loop_end <= v->frames)
    {
        end = v->loop_end;
    }

    if (v->pos < (double)end)
    {
        return true;
    }

    if (!v->looping || end == 0)
    {
        return false;
    }

    U32 start = v->loop_start < end ? v->loop_start : 0;
    double span = (double)(end - start);

    if (span <= 0.0)
    {
        v->pos = (double)start;
        return true;
    }

    // A loop shorter than one output step would spin here, so this subtracts
    // repeatedly rather than once.
    while (v->pos >= (double)end)
    {
        v->pos -= span;
    }

    return true;
}

// Mix `frames` output frames into sMixBuffer as interleaved stereo. Caller
// holds the lock.
static void iMixLocked(U32 frames)
{
    memset(sMixBuffer, 0, sizeof(float) * 2 * frames);

    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        hvoice* v = &sVoices[i];

        if (!v->acquired || !v->playing || v->paused)
        {
            continue;
        }

        // A voice with no data still has to advance, so that it finishes when
        // its sound would have. Skipping the mix but not the clock is the
        // difference between a silent sound and a stuck one.
        double step = ((double)v->rate / (double)sOutRate) * (double)v->pitch;
        if (step <= 0.0)
        {
            step = 0.0;
        }

        if (v->data == NULL)
        {
            v->pos += step * (double)frames;
            if (!iWrap(v))
            {
                v->playing = false;
            }
            continue;
        }

        float gL = v->curL;
        float gR = v->curR;
        float dL = (v->volL - gL) / (float)frames;
        float dR = (v->volR - gR) / (float)frames;

        U32 n = 0;
        for (; n < frames; n++)
        {
            if (!iWrap(v))
            {
                v->playing = false;
                break;
            }

            sMixBuffer[n * 2 + 0] += iSampleAt(v, v->pos, 0) * gL;
            sMixBuffer[n * 2 + 1] += iSampleAt(v, v->pos, 1) * gR;

            v->pos += step;
            gL += dL;
            gR += dR;
        }

        // Only claim to have reached the target gain if the whole block ran;
        // a voice that ended early keeps where its ramp got to, which matters
        // if the same slot is restarted before the next block.
        if (n == frames)
        {
            v->curL = v->volL;
            v->curR = v->volR;

            // The loop tests for the end BEFORE reading, which it has to. A
            // voice whose last frame is the block's last frame would therefore
            // stay "playing" until the next block noticed -- a whole endpoint
            // period of slack in every timing the game derives from a sound.
            // Settle it here instead.
            if (!iWrap(v))
            {
                v->playing = false;
            }
        }
        else
        {
            v->curL = gL;
            v->curR = gR;
        }
    }

    // The environmental reverb, over the finished mix. Here rather than in the
    // render thread so that it is inside the same lock the parameters are set
    // under, and so that the test hook exercises it too.
    //
    // Post-mix is also the only place it can go. It is a send off the whole
    // mix, and the voices above have already been panned and summed; per-voice
    // sends would need a wet level per emitter, which nothing in the game
    // computes and nothing in this seam carries.
    iSndReverbProcess(sMixBuffer, frames);
}

// Keep the finished block inside the representable range. Sixty-four voices
// summing at once can leave it, and float output means nothing below here will
// notice: the wrap happens in the driver or the hardware, as a click.
static void iClamp(U32 frames)
{
    for (U32 n = 0; n < frames * 2; n++)
    {
        if (sMixBuffer[n] > 1.0f)
        {
            sMixBuffer[n] = 1.0f;
        }
        else if (sMixBuffer[n] < -1.0f)
        {
            sMixBuffer[n] = -1.0f;
        }
    }
}

// ---------------------------------------------------------------------------
// The stream callback
//
// SDL calls this on its own audio thread whenever the stream is running short,
// asking for `additional` bytes. It may ask for more than one mix buffer's
// worth -- after a stall, or on a driver with a period longer than the one SDL
// reported -- so this fills as many blocks as it takes rather than assuming the
// request fits.

static void SDLCALL iStreamCallback(void* userdata, SDL_AudioStream* stream, int additional,
                                    int total)
{
    (void)userdata;
    (void)total;

    const int frame_bytes = (int)(sizeof(float) * 2);

    while (additional >= frame_bytes)
    {
        U32 frames = (U32)(additional / frame_bytes);
        if (frames > sMixBufferFrames)
        {
            frames = sMixBufferFrames;
        }

        SDL_LockMutex(sLock);
        iMixLocked(frames);
        SDL_UnlockMutex(sLock);

        iClamp(frames);

        int bytes = (int)frames * frame_bytes;
        if (!SDL_PutAudioStreamData(stream, sMixBuffer, bytes))
        {
            // Nothing useful to do about it here. The next callback will try
            // again, and the voices have already been advanced, so the game
            // stays in step with what it thinks it is hearing.
            return;
        }

        additional -= bytes;
    }
}

// ---------------------------------------------------------------------------
// Device setup

static void iTeardownDevice()
{
    // Destroying the stream unbinds it from the logical device SDL opened for
    // it and closes that device, which also stops the callback. Nothing below
    // may run while it could still be in flight.
    if (sStream != NULL)
    {
        SDL_DestroyAudioStream(sStream);
        sStream = NULL;
    }

    free(sMixBuffer);
    sMixBuffer = NULL;
    sMixBufferFrames = 0;

    // After the callback has gone, which is the only other user of it.
    // sReverbWanted deliberately survives: a device coming back should come
    // back with the effect the current scene asked for.
    iSndReverbExit();

    if (sSubsystem)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        sSubsystem = false;
    }

    sDeviceUp = false;
}

// Everything here reports and returns false rather than failing hard: see the
// note at the top about a missing device being a configuration, not an error.
static bool iBringUpDevice()
{
    // Refcounted, and the window and pad backends init their own subsystems the
    // same way, so this neither depends on nor disturbs them.
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        printf("iSndHost: SDL audio would not start (%s); no audio\n", SDL_GetError());
        return false;
    }

    sSubsystem = true;

    // Ask the device what it is already running at and open the stream there,
    // so SDL has no resampling to do. A failure here means no output device at
    // all, which is the silent path rather than an error.
    SDL_AudioSpec have;
    int device_frames = 0;

    if (!SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &have, &device_frames))
    {
        printf("iSndHost: no default output device (%s); no audio\n", SDL_GetError());
        return false;
    }

    sOutRate = have.freq > 0 ? (U32)have.freq : 48000;
    sDeviceFrames = device_frames > 0 ? (U32)device_frames : 1024;

    // Four periods of headroom. The callback asks for what the stream is short
    // of rather than for one period, and a block bigger than this only costs an
    // extra pass through the mix loop, so this is a size and not a limit.
    sMixBufferFrames = sDeviceFrames * 4;
    sMixBuffer = (float*)malloc(sizeof(float) * 2 * sMixBufferFrames);

    if (sMixBuffer == NULL)
    {
        printf("iSndHost: could not allocate the mix buffer; no audio\n");
        sMixBufferFrames = 0;
        return false;
    }

    // Stereo float at the device's rate. SDL converts from this to whatever the
    // hardware wants, including down to a mono endpoint, which is the one piece
    // of format handling this backend does not have to write.
    SDL_AudioSpec want;
    want.format = SDL_AUDIO_F32;
    want.channels = 2;
    want.freq = (int)sOutRate;

    // Opened paused, so the mix buffer above is guaranteed to exist before the
    // first callback and the reverb below is built before it is heard.
    sStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, iStreamCallback,
                                        NULL);

    if (sStream == NULL)
    {
        printf("iSndHost: could not open an audio stream (%s); no audio\n", SDL_GetError());
        return false;
    }

    // The reverb's delay lengths are in samples, so it can only be built once
    // the device has said what rate it runs at. If the game already asked for
    // an effect -- it sets one when a scene loads, which can be before a device
    // is available -- it is applied now rather than lost.
    iSndReverbInit(sOutRate);
    if (sReverbWanted)
    {
        iSndReverbSet(&sReverb);
    }

    if (!SDL_ResumeAudioStreamDevice(sStream))
    {
        printf("iSndHost: the stream would not start (%s); no audio\n", SDL_GetError());
        return false;
    }

    const char* driver = SDL_GetCurrentAudioDriver();

    snprintf(sName, sizeof(sName), "SDL3 %s (%u Hz, float32 stereo, %u-frame buffer)",
             driver != NULL ? driver : "?", sOutRate, sDeviceFrames);

    return true;
}

// ---------------------------------------------------------------------------
// The seam

void iSndHostInit()
{
    iEnsureLock();

    SDL_LockMutex(sLock);
    memset(sVoices, 0, sizeof(sVoices));
    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        sVoices[i].pitch = 1.0f;
    }
    SDL_UnlockMutex(sLock);

    if (sDeviceUp)
    {
        return;
    }

    // BFBB_AUDIO=0 forces the silent path, which is the quickest way to tell a
    // bug in the mixer apart from a bug in the game logic that waits on it.
    const char* off = getenv("BFBB_AUDIO");
    if (off != NULL && off[0] == '0')
    {
        snprintf(sName, sizeof(sName), "SDL3 (silenced by BFBB_AUDIO=0, but keeps time)");
        sOutRate = 48000;
        sLastSilentNs = iHostMonotonicNs();
        return;
    }

    sDeviceUp = iBringUpDevice();

    if (!sDeviceUp)
    {
        iTeardownDevice();
        snprintf(sName, sizeof(sName), "SDL3 (no device; silent, but keeps time)");

        // The fallback clock needs a rate to convert its elapsed time into
        // frames. Any value works as long as it matches what the voices are
        // stepped against; 48 kHz is what a device would most likely have been.
        sOutRate = 48000;
        sLastSilentNs = iHostMonotonicNs();
    }
}

void iSndHostExit()
{
    iTeardownDevice();

    SDL_LockMutex(sLock);
    memset(sVoices, 0, sizeof(sVoices));
    SDL_UnlockMutex(sLock);
}

S32 iSndHostAcquire(U32 priority)
{
    iEnsureLock();

    SDL_LockMutex(sLock);

    S32 got = -1;
    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        if (!sVoices[i].acquired)
        {
            memset(&sVoices[i], 0, sizeof(sVoices[i]));
            sVoices[i].acquired = true;
            sVoices[i].pitch = 1.0f;
            got = i;
            break;
        }
    }

    SDL_UnlockMutex(sLock);

    // Every voice busy. Retail's AXAcquireVoice evicts a lower-priority one
    // here; the mixer has exactly as many voices as the game's own table, so
    // running out means the game already decided all 64 were worth keeping, and
    // there is nothing better to evict than what it chose. Failing is a state
    // the game handles.
    return got;
}

void iSndHostRelease(S32 voice)
{
    iEnsureLock();

    SDL_LockMutex(sLock);

    if (iVoiceValid(voice))
    {
        memset(&sVoices[voice], 0, sizeof(sVoices[voice]));
    }

    SDL_UnlockMutex(sLock);
}

void iSndHostStart(S32 voice, const iSndHostSample* sample)
{
    if (sample == NULL)
    {
        return;
    }

    iEnsureLock();

    SDL_LockMutex(sLock);

    if (iVoiceValid(voice))
    {
        hvoice* v = &sVoices[voice];

        U32 channels = sample->channels != 0 ? sample->channels : 1;
        U32 bits = sample->bits != 0 ? sample->bits : 16;

        // What the caller says the sample is, against what it actually handed
        // over. A short block would be read past the end at the last frame, so
        // the byte count wins.
        U32 frame_bytes = channels * (bits / 8);
        U32 frames = sample->num_samples;

        if (sample->data != NULL && frame_bytes != 0)
        {
            U32 have = sample->bytes / frame_bytes;
            if (frames == 0 || frames > have)
            {
                frames = have;
            }
        }

        v->data = sample->data;
        v->frames = frames;
        v->channels = channels;
        v->bits = bits;
        v->rate = sample->sample_rate != 0 ? sample->sample_rate : 22050;
        v->looping = sample->looping;
        v->loop_start = sample->loop_start;
        v->loop_end = sample->loop_end;
        v->pos = 0.0;
        v->playing = frames > 0;
        v->paused = false;

        // Start at the target gain rather than ramping up from silence: the
        // first block of a sound is its attack, and fading it in blunts every
        // percussive effect in the game.
        v->curL = v->volL;
        v->curR = v->volR;
    }

    SDL_UnlockMutex(sLock);
}

void iSndHostStop(S32 voice)
{
    iEnsureLock();

    SDL_LockMutex(sLock);

    if (iVoiceValid(voice))
    {
        sVoices[voice].playing = false;
        sVoices[voice].paused = false;

        // Drop the borrowed pointer here, not just the playing flag. The caller
        // is free to release the sample as soon as this returns, and the mixer
        // must have no way to reach it afterwards.
        sVoices[voice].data = NULL;
        sVoices[voice].frames = 0;
    }

    SDL_UnlockMutex(sLock);
}

void iSndHostPause(S32 voice, bool paused)
{
    iEnsureLock();

    SDL_LockMutex(sLock);

    if (iVoiceValid(voice) && sVoices[voice].playing)
    {
        sVoices[voice].paused = paused;
    }

    SDL_UnlockMutex(sLock);
}

void iSndHostSetVol(S32 voice, F32 left, F32 right)
{
    if (left < 0.0f) left = 0.0f;
    else if (left > 1.0f) left = 1.0f;
    if (right < 0.0f) right = 0.0f;
    else if (right > 1.0f) right = 1.0f;

    iEnsureLock();

    SDL_LockMutex(sLock);

    if (iVoiceValid(voice))
    {
        sVoices[voice].volL = left;
        sVoices[voice].volR = right;
    }

    SDL_UnlockMutex(sLock);
}

void iSndHostSetPitch(S32 voice, F32 pitch)
{
    if (pitch <= 0.0f)
    {
        return;
    }

    iEnsureLock();

    SDL_LockMutex(sLock);

    if (iVoiceValid(voice))
    {
        sVoices[voice].pitch = pitch;
    }

    SDL_UnlockMutex(sLock);
}

bool iSndHostIsPlaying(S32 voice)
{
    iEnsureLock();

    SDL_LockMutex(sLock);

    bool playing = iVoiceValid(voice) && sVoices[voice].playing;

    SDL_UnlockMutex(sLock);

    return playing;
}

void iSndHostUpdate()
{
    if (sDeviceUp)
    {
        // The render thread advanced every voice as it consumed it, and retires
        // them itself. Nothing to do.
        return;
    }

    // No device: advance the voices on the wall clock instead, so that
    // everything the game derives from a sound's length still happens on time.
    U64 now = iHostMonotonicNs();
    U64 elapsed = now - sLastSilentNs;
    sLastSilentNs = now;

    // A long stall -- a breakpoint, a scene load -- would otherwise skip a
    // sound entirely. Cap the step at a quarter second, which is what a device
    // would effectively have done by dropping the frames it could not fill.
    if (elapsed > 250000000ULL)
    {
        elapsed = 250000000ULL;
    }

    double frames = ((double)elapsed / 1000000000.0) * (double)sOutRate;

    iEnsureLock();

    SDL_LockMutex(sLock);

    for (S32 i = 0; i < ISNDHOST_MAX_VOICES; i++)
    {
        hvoice* v = &sVoices[i];

        if (!v->acquired || !v->playing || v->paused)
        {
            continue;
        }

        double step = ((double)v->rate / (double)sOutRate) * (double)v->pitch;
        v->pos += step * frames;

        if (!iWrap(v))
        {
            v->playing = false;
        }
    }

    SDL_UnlockMutex(sLock);
}

void iSndHostSetReverb(const iSndHostReverb* params)
{
    iEnsureLock();

    SDL_LockMutex(sLock);

    if (params != NULL)
    {
        sReverb = *params;
        sReverbWanted = true;
        iSndReverbSet(&sReverb);
    }
    else
    {
        sReverbWanted = false;
        iSndReverbSet(NULL);
    }

    SDL_UnlockMutex(sLock);
}

// ---------------------------------------------------------------------------
// Test hook
//
// Deliberately not in iSndHost.h: nothing in the game may call this, and its
// only caller is src/SB/Core/pc/tests/selftest.cpp. It exists because the
// interesting half of this file -- the resampling, the gain ramp, the loop
// wrap -- is otherwise only reachable through a real audio endpoint, which a
// self-test must not require and could not make deterministic if it did.
//
// Mixes one block at `rate` into `out` as interleaved stereo, exactly as the
// render thread would, and advances the voices by it.
void iSndHostTestMix(U32 rate, float* out, U32 frames)
{
    if (out == NULL || frames == 0 || rate == 0)
    {
        return;
    }

    iEnsureLock();

    if (sMixBuffer == NULL || frames > sMixBufferFrames)
    {
        free(sMixBuffer);
        sMixBuffer = (float*)malloc(sizeof(float) * 2 * frames);
        sMixBufferFrames = sMixBuffer != NULL ? frames : 0;
    }

    if (sMixBuffer == NULL)
    {
        return;
    }

    U32 saved = sOutRate;
    sOutRate = rate;

    SDL_LockMutex(sLock);
    iMixLocked(frames);
    SDL_UnlockMutex(sLock);

    sOutRate = saved;

    memcpy(out, sMixBuffer, sizeof(float) * 2 * frames);
}

const char* iSndHostName()
{
    return sName[0] != '\0' ? sName : "SDL3 (not initialised)";
}
