#ifndef ISNDDATA_H
#define ISNDDATA_H

#include <types.h>

// Where the samples come from.
//
// PC-only, and separate from iSnd.cpp because it answers a different question.
// iSnd.cpp owns the game's model of sound -- voices, handles, priorities,
// the id ranges. This owns one fact the GameCube never had to state: on a host
// there is no ARAM, so the bytes a voice plays have to be somewhere in main
// memory, and getting them there is nobody else's job.
//
// See iSndData.cpp for why they are not already there.

// How the asset is encoded, straight out of its sound-table entry.
struct iSndDataFormat
{
    U32 format_tag; // 1 = PCM, 0x69 = Xbox ADPCM
    U32 channels;
    U32 block_align;
};

// What actually came back, which is not always what the table asked for. The
// caller passes the sound table's idea of the asset in `fmt` and reads the
// truth out of this: a soundtrack override (iSoundtrack.h) is a different
// recording of the same music and is free to be stereo, or 48 kHz, or both.
//
// `channels` and `sample_rate` describe the block returned. `overridden` says
// the bytes did not come from the game's own archives, which is the one thing
// a caller cannot work out for itself and which decides where the loop goes.
struct iSndDataPcm
{
    U32 channels;
    U32 sample_rate;
    bool overridden;
};

// Hand back the PCM for one SND or SNDS asset, reading it if this is the first
// time. Returns NULL if the asset cannot be found or read, which a caller is
// expected to treat as "play this voice silently" rather than as a failure --
// see iSndHost.h.
//
// **What comes back is always 16-bit PCM**, whatever the asset was. An ADPCM
// asset is decoded once, here, and the cache holds the decoded form -- so
// `bytes` is the decoded length, not the asset's, and the caller should read
// the sample count from it rather than from the table's data_size.
//
// `pcm` may be NULL for a caller that only wants the bytes; one that is going
// to play them should pass it and believe it over `fmt`.
//
// The returned memory stays valid until the matching iSndDataRelease. Every
// successful Acquire must be paired with exactly one Release, including when
// the voice is stopped early.
const void* iSndDataAcquire(U32 assetID, const iSndDataFormat* fmt, U32* bytes,
                            iSndDataPcm* pcm);
void iSndDataRelease(U32 assetID);

// Drop everything, including entries still held. Only for iSndInit/iSndExit,
// where no voice can be playing.
void iSndDataReset();

// For the startup log and the self-test: how much is cached and how much of it
// is spoken for.
void iSndDataStats(U32* entries, U32* bytes, U32* pinned);

#endif
