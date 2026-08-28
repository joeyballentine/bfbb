// The movie audio device on a host that has none, and the one Win32 falls back
// to if waveOut is not selected at configure time.
//
// Refusing to open is the supported answer, not a failure: iFMV.cpp plays the
// movie silently on a wall clock when there is no device. See iFMVAudio.h.

#include "iFMVAudio.h"

S32 iFMVAudioOpen(U32 sample_rate, U32 channels)
{
    (void)sample_rate;
    (void)channels;
    return FALSE;
}

void iFMVAudioClose()
{
}

U32 iFMVAudioWrite(const S16* frames, U32 num_frames)
{
    (void)frames;
    (void)num_frames;
    return 0;
}

U32 iFMVAudioQueued()
{
    return 0;
}

const char* iFMVAudioName()
{
    return "none";
}
