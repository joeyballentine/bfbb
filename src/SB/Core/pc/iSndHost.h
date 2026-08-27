#ifndef ISNDHOST_H
#define ISNDHOST_H

#include <types.h>

// PC-only, and the same shape as iPadHost.h for the same reason: the GameCube
// has one audio API that is always present, so its iSnd.cpp talks to AX and
// MIX directly. A host has several possible audio libraries and none of them is
// guaranteed to exist at build time, so the part of iSnd that would touch a
// device is behind this seam and the part with the game's semantics in it --
// the voice table, the handles, the priority rules, the sound lookup -- is not.
//
// This is deliberately NOT where the game's model lives. A backend is told
// "start this many samples at this rate on this voice" and nothing about
// streams, categories, owners or asset IDs.

#define ISNDHOST_MAX_VOICES 64

struct iSndHostSample
{
    // The samples themselves, interleaved, or NULL when the caller could not
    // find them. A backend that can play is expected to fall back to timing the
    // voice out silently rather than refusing it: the game waits on sounds it
    // cannot hear just as hard as on ones it can, and one missing asset must
    // not stall a cutscene.
    //
    // The memory belongs to the caller and stays alive until the voice is
    // stopped or released, which is what iSnd's sample cache guarantees. A
    // backend must not free it, and must not read it after iSndHostStop.
    const void* data;
    U32 bytes;

    // 1 or 2, and 8 or 16. Every asset the port has met is 16-bit mono, but the
    // table says so per entry rather than by convention, so the backend is told
    // rather than assuming.
    U32 channels;
    U32 bits;

    U32 sample_rate;
    U32 num_samples;

    // Loops restart at loop_start rather than ending. num_samples still gives
    // the length of one pass, which is what a silent backend paces on.
    bool looping;
    U32 loop_start;
};

void iSndHostInit();
void iSndHostExit();

// Acquires a device voice, or -1 if none is free. Lower `priority` is more
// important, matching AXAcquireVoice, which the GameCube path feeds from the
// game's 0-255 priority shifted right by three.
S32 iSndHostAcquire(U32 priority);
void iSndHostRelease(S32 voice);

void iSndHostStart(S32 voice, const iSndHostSample* sample);
void iSndHostStop(S32 voice);
void iSndHostPause(S32 voice, bool paused);

// Linear 0..1 per side. The GameCube path converts to AX's log scale inside
// iSnd; a backend gets the linear value because that is what audio libraries
// take, and converting twice would be lossy.
void iSndHostSetVol(S32 voice, F32 left, F32 right);

// 1.0 is the sample's own rate.
void iSndHostSetPitch(S32 voice, F32 pitch);

bool iSndHostIsPlaying(S32 voice);

// Once per frame, from iSndUpdate. A backend that finishes voices on its own
// clock retires them here.
void iSndHostUpdate();

// Names the backend that was linked in, for the startup log.
const char* iSndHostName();

#endif
