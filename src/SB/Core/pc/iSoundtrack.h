#ifndef ISOUNDTRACK_H
#define ISOUNDTRACK_H

#include <types.h>

// PC-only: playing your own recordings of the music in place of the game's.
//
// The disc's music is mono -- every one of the 3537 sound assets in the retail
// tree is -- and the music is the only one where that is a loss rather than a
// detail, because it is the only one heard at full length with nothing else
// going on. Nothing about the mixer stopped a stereo master being played:
// iSampleAt has taken a channel index and strided by the source's channel count
// since it was written, and a 2D voice like music sits centred. What was
// missing was any way to get one in.
//
// The asset is replaced, not the mixing. An override goes in at
// iSndDataAcquire, the one place a sound's bytes are fetched, so the voice is
// acquired, panned, faded, paused, stopped and timed out exactly as the disc's
// own asset would have been, and zMusic never learns anything happened. That is
// also why this is not restricted to music: any asset id can be overridden, and
// the music is simply the only one worth it.
//
// Files are found by two rules, tried in order, because two kinds of folder
// exist. A folder assembled for this port has files named after the assets, and
// the name is the key -- the packer's asset id is xStrHash of the asset's name,
// so music_00_hb_44.flac needs no mapping. A soundtrack rip has files named
// after the music, which cannot be matched by name, so a soundtrack.txt in the
// folder may say what is what:
//
//     ; asset name = file, relative to this folder
//     music_00_hb_44 = 01. Bikini Bottom.flac
//     music_10_gy_44 = 12. Flying Dutchman's Graveyard.flac
//
// An entry there wins over a name that happens to hash, so a mapping can
// correct a coincidence rather than being fought by one.
//
// The file's own rate and channel count are used, not the sound table's: the
// table describes the asset on the disc and an override is a different
// recording. iSndDataAcquire reports what it decoded and iStartVoice believes
// it, so a 48 kHz stereo FLAC standing in for a 44.1 kHz mono asset plays as a
// 48 kHz stereo voice.
//
// Looping is at the disc's length, not the file's. A soundtrack release is
// usually the same performance with a proper ending on it: measured against the
// game's own tracks, every one starts at the same instant and runs 0.9 to 7.2
// seconds longer. The game loops its music forever, so looping the file whole
// would drag that ending round every time. The retail asset's length is carried
// through as the loop end, which puts the seam where the console put it and
// leaves the tail for a track that is allowed to finish.

// The folder to look in, or NULL/"" for none, which is the default and is
// exactly retail. Pushed from iSystem.cpp's ApplyConfig, the way the text
// patch and the render features are, so that nothing below here has to know
// what config.ini is. Setting it discards whatever the last one found.
void iSoundtrackSetFolder(const char* path);
const char* iSoundtrackFolder();

// The file to play instead of `assetID`, or NULL if there is none -- which is
// the answer for every asset when no folder is set, and for all but a handful
// when one is. The folder is scanned once, on the first call.
const char* iSoundtrackFind(U32 assetID);

// Decode one file to interleaved 16-bit PCM, which is what the rest of the
// port's audio path deals in. The caller owns the block and frees it.
//
// NULL if the file will not open, will not decode, or decodes to nothing.
// A caller is expected to fall back to the disc's own asset rather than
// treating that as fatal: a soundtrack folder with one bad file in it should
// cost that one track, not the game's audio.
//
// `channels` comes back as 1 or 2 -- anything wider is downmixed, because the
// mixer reads two and carrying six would be memory spent on silence.
void* iSoundtrackDecode(const char* path, U32* channels, U32* rate, U32* bytes);

// How many overrides the folder yielded, for the startup log and the selftest.
// Forces the scan, the same as iSoundtrackFind.
U32 iSoundtrackCount();

// Names the decoder that was linked in, for the startup log.
const char* iSoundtrackDecoderName();

// Drop the scan, so the next lookup redoes it. For iSndExit and the selftest.
void iSoundtrackReset();

#endif
