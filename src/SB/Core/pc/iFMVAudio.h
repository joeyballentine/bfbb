#ifndef IFMVAUDIO_H
#define IFMVAUDIO_H

#include <types.h>

// The audio device a movie plays through, which is deliberately NOT the game's.
//
// zFMV.cpp calls xSndSuspend before iFMVPlay and xSndResume after, so the
// mixer is stopped for the whole of a movie and the voice table behind
// iSndHost.h is not available to stream into. It is also the wrong shape: that
// interface hands a backend a finished sample and asks it to play it, where a
// movie produces audio a fragment at a time for as long as it runs.
//
// So a movie opens its own output, owns it for its duration, and closes it
// before the game's mixer comes back. Nothing is shared, and the two are never
// running at once.
//
// Queued() is what makes the picture line up with the sound. A movie is paced
// on its AUDIO clock -- the amount that has actually left the device -- because
// that clock is the one the player hears and cannot be adjusted without them
// noticing. The video is fitted to it. Pacing the other way round, on a frame
// timer, drifts against the device's real rate and ends with lips out of sync.

// FALSE if there is no device or the format is refused, which is not fatal:
// iFMV.cpp then plays the movie silently on a wall clock rather than not at all.
S32 iFMVAudioOpen(U32 sample_rate, U32 channels);
void iFMVAudioClose();

// Hands over interleaved 16-bit frames. Returns how many were accepted, which
// is fewer than asked for when the device's buffers are full -- the caller
// keeps the rest and offers them again.
U32 iFMVAudioWrite(const S16* frames, U32 num_frames);

// Sample frames submitted but not yet played. The audio clock is everything
// written minus this.
U32 iFMVAudioQueued();

// Names the backend, for the startup log.
const char* iFMVAudioName();

#endif
