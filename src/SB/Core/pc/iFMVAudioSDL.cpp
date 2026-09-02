// The movie audio device, through SDL.
//
// A separate device from the game's mixer, for the reason iFMVAudio.h gives:
// zFMV.cpp suspends the mixer around a movie, and the voice table behind
// iSndHost.h is the wrong shape for something that produces audio a fragment at
// a time. The two are never open at once.
//
// Push mode: a stream bound to the device, filled with SDL_PutAudioStreamData
// and pulled out by SDL on its own thread as the device asks. That is exactly
// the shape of this seam, so there is no ring of blocks here and no device
// thread of ours.
//
// SDL converts the movie's rate and channel count to whatever the hardware
// wants, so a 22 kHz mono cutscene needs no resampling on this side.

#include "iFMVAudio.h"

#include <SDL3/SDL.h>

#include <stdio.h>

namespace
{
    SDL_AudioStream* sStream;
    SDL_AudioDeviceID sDevice;
    bool sSubsystem;
    U32 sChannels;
    U32 sRate;

    // How far ahead of the device the writer is allowed to get. The old waveOut
    // backend held eight 20 ms blocks; the same 160 ms here keeps a 25 fps
    // movie's 40 ms frames from ever finding the queue full for long, without
    // letting a fast decoder run so far ahead that closing has to discard much.
    const U32 kQueueMs = 160;

    U32 QueuedFrames()
    {
        if (sStream == NULL)
        {
            return 0;
        }

        // **Zero once the device has taken everything it can.**
        //
        // A stream whose rate differs from the device's keeps a few input
        // samples as resampler history, and SDL goes on counting them as
        // queued: a 44.1 kHz movie on a 48 kHz device settles at seven frames
        // and never reaches zero. iFMV.cpp hands its clock over to the wall
        // only when the track has drained AND the queue is empty, so those
        // seven frames stop the clock on the last video frame and the movie
        // hangs there forever. Nothing is left to play when there is no
        // converted output, so that is what "empty" means here.
        if (SDL_GetAudioStreamAvailable(sStream) <= 0)
        {
            return 0;
        }

        // Bytes of INPUT, so this is in the movie's own format rather than the
        // device's. What has already been handed down to the device is no
        // longer counted, so this runs up to one device period short -- ten or
        // twenty milliseconds of picture ahead of sound, constant rather than
        // drifting, and below what anyone sees.
        int queued = SDL_GetAudioStreamQueued(sStream);

        if (queued <= 0)
        {
            return 0;
        }

        return (U32)queued / (sChannels * (U32)sizeof(S16));
    }
}

S32 iFMVAudioOpen(U32 sample_rate, U32 channels)
{
    if (sStream != NULL || sample_rate == 0 || channels == 0 || channels > 2)
    {
        return FALSE;
    }

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        return FALSE;
    }

    sSubsystem = true;

    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = (int)channels;
    spec.freq = (int)sample_rate;

    sDevice = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);

    if (sDevice == 0)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        sSubsystem = false;
        return FALSE;
    }

    // Both ends of the stream are the movie's format; binding it to the device
    // is what sets the output end to the hardware's.
    sStream = SDL_CreateAudioStream(&spec, &spec);

    if (sStream == NULL || !SDL_BindAudioStream(sDevice, sStream))
    {
        iFMVAudioClose();
        return FALSE;
    }

    sChannels = channels;
    sRate = sample_rate;

    if (!SDL_ResumeAudioDevice(sDevice))
    {
        iFMVAudioClose();
        return FALSE;
    }

    return TRUE;
}

void iFMVAudioClose()
{
    if (sStream != NULL)
    {
        SDL_DestroyAudioStream(sStream);
        sStream = NULL;
    }

    // After the stream, so nothing is left playing the tail of a movie the game
    // has moved on from.
    if (sDevice != 0)
    {
        SDL_CloseAudioDevice(sDevice);
        sDevice = 0;
    }

    if (sSubsystem)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        sSubsystem = false;
    }

    sChannels = 0;
    sRate = 0;
}

U32 iFMVAudioWrite(const S16* frames, U32 num_frames)
{
    if (sStream == NULL || frames == NULL || num_frames == 0)
    {
        return 0;
    }

    // SDL would take everything offered and queue it without limit, which would
    // put the whole movie in ahead of the picture and leave iFMVAudioQueued
    // measuring the decoder rather than the device. Cap it here so the caller
    // keeps the rest, which is the contract in iFMVAudio.h.
    U32 cap = (sRate * kQueueMs) / 1000;
    U32 have = QueuedFrames();

    if (have >= cap)
    {
        return 0;
    }

    U32 take = cap - have;
    if (take > num_frames)
    {
        take = num_frames;
    }

    int bytes = (int)(take * sChannels * (U32)sizeof(S16));

    if (!SDL_PutAudioStreamData(sStream, frames, bytes))
    {
        return 0;
    }

    return take;
}

U32 iFMVAudioQueued()
{
    return QueuedFrames();
}

const char* iFMVAudioName()
{
    // The startup log asks for this before any movie has opened a device, and
    // SDL names no driver until one is up. Say which library it is either way,
    // and which driver once there is one to name.
    const char* driver = SDL_GetCurrentAudioDriver();

    if (driver == NULL)
    {
        return "SDL3";
    }

    static char name[64];
    snprintf(name, sizeof(name), "SDL3 %s", driver);

    return name;
}
