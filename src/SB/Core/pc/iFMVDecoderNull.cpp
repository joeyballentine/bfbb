// The decoder that decodes nothing, built when CMake does not find FFmpeg.
//
// This is what keeps "no movie decoder" a supported configuration rather than a
// broken build. iFMV.cpp asks for a decoder, gets NULL, and reports the movie
// as having run to the end -- which is exactly what the port did before there
// was a decoder at all, and which every caller already handles because a movie
// that is skipped and a movie that finished are the same thing to the game.
//
// Kept as a real translation unit rather than #ifdefs inside iFMV.cpp so that
// the playback path has one shape. The loop, the pacing and the skip button are
// not conditional on whether a decoder exists; only what they are fed is.

#include "iFMVDecoder.h"

#include <string.h>

iFMVDecoder* iFMVDecoderOpen(const char* path, iFMVDecoderInfo* info)
{
    (void)path;
    if (info != NULL)
    {
        memset(info, 0, sizeof(*info));
    }
    return NULL;
}

void iFMVDecoderClose(iFMVDecoder* dec)
{
    (void)dec;
}

S32 iFMVDecoderNextFrame(iFMVDecoder* dec, const void** pixels, U32* pitch, F32* pts)
{
    (void)dec;
    (void)pixels;
    (void)pitch;
    (void)pts;
    return FALSE;
}

U32 iFMVDecoderReadAudio(iFMVDecoder* dec, S16* out, U32 frames)
{
    (void)dec;
    (void)out;
    (void)frames;
    return 0;
}

const char* iFMVDecoderName()
{
    return "none";
}
