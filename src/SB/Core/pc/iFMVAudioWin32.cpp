// The movie audio device on Win32, through waveOut.
//
// waveOut rather than WASAPI, which is what iSndHostWin32.cpp uses for the
// game. That is a deliberate difference and worth the two sentences:
//
// WASAPI is the right choice for the game's mixer, which is long-lived, wants
// low latency and mixes many voices. A movie is the opposite of all three --
// one stereo stream, opened and closed around a single cutscene, where latency
// does not matter because nothing is reacting to it. waveOut does exactly that
// in a fraction of the code, needs no COM apartment, and cannot disturb the
// mixer that is suspended alongside it.
//
// The header queue is the whole design. Blocks are submitted with waveOutWrite
// and the device retires them as it plays; a block whose WHDR_DONE is set has
// been heard, so counting the ones that are not gives the caller its audio
// clock without asking the device for a position.

#include "iFMVAudio.h"

#include <windows.h>
#include <mmsystem.h>

#include <string.h>

// Enough blocks that the device never runs dry between frames of a 25 fps movie
// (40 ms), small enough that closing does not stall. Eight 20 ms blocks is
// 160 ms of slack.
#define FMVAUDIO_BLOCKS 8
#define FMVAUDIO_BLOCK_MS 20

namespace
{
    HWAVEOUT sDevice;
    WAVEHDR sHeaders[FMVAUDIO_BLOCKS];
    S16* sBuffers[FMVAUDIO_BLOCKS];
    U32 sBlockFrames;
    U32 sChannels;
    U32 sNext;
    bool sOpen;

    // Frames handed to the device since it was opened. The clock is this minus
    // whatever is still queued.
    U32 sWritten;
}

S32 iFMVAudioOpen(U32 sample_rate, U32 channels)
{
    if (sOpen || sample_rate == 0 || channels == 0 || channels > 2)
    {
        return FALSE;
    }

    WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = (WORD)channels;
    fmt.nSamplesPerSec = sample_rate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = (WORD)(channels * 2);
    fmt.nAvgBytesPerSec = sample_rate * fmt.nBlockAlign;

    if (waveOutOpen(&sDevice, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
    {
        sDevice = NULL;
        return FALSE;
    }

    sChannels = channels;
    sBlockFrames = (sample_rate * FMVAUDIO_BLOCK_MS) / 1000;
    sNext = 0;
    sWritten = 0;

    for (int i = 0; i < FMVAUDIO_BLOCKS; i++)
    {
        sBuffers[i] = new S16[sBlockFrames * channels];
        memset(&sHeaders[i], 0, sizeof(WAVEHDR));
        sHeaders[i].lpData = (LPSTR)sBuffers[i];
        sHeaders[i].dwBufferLength = sBlockFrames * channels * sizeof(S16);
        if (waveOutPrepareHeader(sDevice, &sHeaders[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
        {
            // Unwind what was prepared, then refuse. A half-open device is
            // worse than none: the caller has a silent fallback and no way to
            // recover one of these.
            for (int j = 0; j < i; j++)
            {
                waveOutUnprepareHeader(sDevice, &sHeaders[j], sizeof(WAVEHDR));
                delete[] sBuffers[j];
                sBuffers[j] = NULL;
            }
            delete[] sBuffers[i];
            sBuffers[i] = NULL;
            waveOutClose(sDevice);
            sDevice = NULL;
            return FALSE;
        }

        // Marked done so the first pass round the ring treats every block as
        // free, rather than waiting for a device that has been given nothing.
        sHeaders[i].dwFlags |= WHDR_DONE;
    }

    sOpen = true;
    return TRUE;
}

void iFMVAudioClose()
{
    if (!sOpen)
    {
        return;
    }

    // Reset before unpreparing: waveOutUnprepareHeader refuses a header that is
    // still queued, and reset is what takes them all back at once.
    waveOutReset(sDevice);

    for (int i = 0; i < FMVAUDIO_BLOCKS; i++)
    {
        waveOutUnprepareHeader(sDevice, &sHeaders[i], sizeof(WAVEHDR));
        delete[] sBuffers[i];
        sBuffers[i] = NULL;
    }

    waveOutClose(sDevice);
    sDevice = NULL;
    sOpen = false;
    sWritten = 0;
}

U32 iFMVAudioWrite(const S16* frames, U32 num_frames)
{
    if (!sOpen || frames == NULL)
    {
        return 0;
    }

    U32 done = 0;
    while (done < num_frames)
    {
        WAVEHDR* h = &sHeaders[sNext];
        if ((h->dwFlags & WHDR_DONE) == 0)
        {
            // The ring has caught up with the device. Whatever is left belongs
            // to the caller until there is somewhere to put it.
            break;
        }

        U32 take = num_frames - done;
        if (take > sBlockFrames)
        {
            take = sBlockFrames;
        }

        memcpy(sBuffers[sNext], frames + (size_t)done * sChannels,
               (size_t)take * sChannels * sizeof(S16));
        h->dwBufferLength = take * sChannels * sizeof(S16);
        h->dwFlags &= ~WHDR_DONE;

        if (waveOutWrite(sDevice, h, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
        {
            h->dwFlags |= WHDR_DONE;
            break;
        }

        sWritten += take;
        done += take;
        sNext = (sNext + 1) % FMVAUDIO_BLOCKS;
    }

    return done;
}

U32 iFMVAudioQueued()
{
    if (!sOpen)
    {
        return 0;
    }

    U32 queued = 0;
    for (int i = 0; i < FMVAUDIO_BLOCKS; i++)
    {
        if ((sHeaders[i].dwFlags & WHDR_DONE) == 0)
        {
            queued += sHeaders[i].dwBufferLength / (sChannels * sizeof(S16));
        }
    }
    return queued;
}

const char* iFMVAudioName()
{
    return "waveOut";
}
