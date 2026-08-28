#include "iSnd.h"
#include "iSndData.h"
#include "iSndHost.h"

#include "iHost.h"

#include "xSnd.h"
#include "xMath.h"
#include "xstransvc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The host side of the sound interface.
//
// The split follows iPad's: everything with the game's semantics in it lives
// here -- the voice table, the handle scheme, the stream/sound division, the
// sound lookup and its id ranges -- and the part that would drive a device is
// behind iSndHost.h. Nothing in this file assumes an audio library exists.
//
// The GameCube original is 2051 lines, most of which is AX, MIX, ARAM and DVD:
// acquiring hardware voices, uploading ADPCM to ARAM, streaming from disc in
// interleaved blocks. None of that has a host counterpart, and reproducing its
// shape would be reproducing hardware that is not there.
//
// Asset data is read through the same direct casts the GameCube code uses.
// That is only correct because the port's assets are little-endian (see
// docs/PCPORT.md, "Asset caveats"); against GameCube-native assets every field read
// in this file would be byte-swapped.

// ---------------------------------------------------------------------------
// Asset layout
//
// Two different shapes meet in this file.
//
// `sndhdr` is what iSndLookup HANDS BACK: the GC DSPADPCM header with the asset
// ID appended, exactly as src/SB/Core/gc/iSnd.cpp describes it. Three fields are
// read outside this file, and xSnd.cpp reaches them through its own
// iSndLookupInfo declaration at fixed offsets -- num_samples at 0x00,
// sample_rate at 0x08 and the internal id at 0x64 -- so this layout is
// load-bearing and must not be rearranged.
//
// `xbox_sndhdr` is what the SNDI assets on disc ACTUALLY CONTAIN. The port
// reads the Xbox asset set, and the Xbox SNDI is not the GameCube one: it is a
// 12-byte count header followed by fixed 44-byte entries built around a
// WAVEFORMATEX, little-endian, with no DSP coefficients and no total_size field.
// Reading it as a GC table walks entry[] with the wrong stride off counts taken
// from the wrong words, which is a wild pointer within a few iterations.
//
// The layout below is confirmed against every HIP and HOP in the retail Xbox
// tree: each SNDI asset is exactly 12 + 44 * (num_sfx + num_streams +
// num_cutscene) bytes, and each entry's data_size matches the byte length of
// the SND asset its asset_id names.

struct sndhdr
{
    U32 num_samples; // 0x00
    U32 num_nibbles; // 0x04
    U32 sample_rate; // 0x08
    U16 loop_flag; // 0x0C
    U16 format; // 0x0E
    U32 loop_start; // 0x10
    U32 loop_end; // 0x14
    U32 cur_addr; // 0x18
    S16 coef[16]; // 0x1C
    U16 gain; // 0x3C
    U16 pred_scale; // 0x3E
    U16 yn1; // 0x40
    U16 yn2; // 0x42
    U16 loop_pred_scale; // 0x44
    U16 loop_yn1; // 0x46
    U16 loop_yn2; // 0x48
    U16 pad[11]; // 0x4A -- pad[0] is tagged 0x63 for memory streams
    U32 assetID; // 0x60
};

// One entry of an Xbox SNDI table, as it sits in the asset.
struct xbox_sndhdr
{
    // WAVEFORMATEX, verbatim.
    U16 format_tag; // 0x00 -- 1 = PCM
    U16 channels; // 0x02
    U32 samples_per_sec; // 0x04
    U32 avg_bytes_per_sec; // 0x08
    U16 block_align; // 0x0C
    U16 bits_per_sample; // 0x0E
    U16 cb_size; // 0x10
    U16 pad12; // 0x12 -- realigns the fields after the 18-byte WAVEFORMATEX
    U32 data_size; // 0x14 -- byte length of the SND asset
    U32 assetID; // 0x18
    U32 runtime[4]; // 0x1C -- zero on disc; the retail backend's own bookkeeping
};

// The header of a loaded Xbox SNDI asset.
struct sndinfo
{
    U32 num_sfx; // 0x00
    U32 num_streams; // 0x04
    U32 num_cutscene; // 0x08
    xbox_sndhdr entry[1]; // 0x0C
};

// What iSndLookup hands back. The id has to land at 0x64, immediately after the
// header, because that is where xSnd.cpp reads it.
struct sndlookup
{
    sndhdr hdr; // 0x00
    U32 id; // 0x64
};

// ---------------------------------------------------------------------------
// Loaded sound tables

// The on-disc stride. Every SNDI in the retail Xbox tree is exactly
// 12 + 44 * total_entries bytes, so getting this wrong is not a slow drift --
// the very first lookup walks off the asset.
static_assert(sizeof(xbox_sndhdr) == 44, "xbox_sndhdr must be the 44-byte on-disc entry");
static_assert(sizeof(sndinfo) == 12 + 44, "the Xbox SNDI count header must be 12 bytes");

#define ISND_MAX_TABLES 12

static sndinfo* sinfo_array[ISND_MAX_TABLES];
static S32 sinfo_array_max;

// iSndLookup returns a pointer to this, refilled on every call, as retail does.
// Callers read it immediately; nothing holds it across a second lookup.
static sndlookup snd;

// 0 = sound or memory stream, 1 = stream, 2 = cutscene. File-scope on the
// GameCube side too, set by iSndLookup and read by the play path.
static S32 sound_stream;

// ---------------------------------------------------------------------------
// Voices
//
// gSnd.voice[64] is the game's own table and lives in shared code. This is the
// platform's shadow of it: indices 0-5 are streams and 6-63 are sounds, the
// same division the GameCube path uses and the one iSndPlay dispatches on.

#define ISND_STREAM_VOICES 6
#define ISND_TOTAL_VOICES 64

struct pcvoice
{
    // The device voice from iSndHostAcquire, or -1 when this slot holds none.
    S32 host;

    U32 aid;
    bool in_use;
    bool paused;

    // The gain the last mix computed, for BFBB_SNDMIX.
    F32 last_left;
    F32 last_right;

    // Whether this slot is holding a reference on the sample cache. Tracked
    // rather than inferred from `aid`, because a voice can hold a slot for an
    // asset whose data could not be read, and releasing a reference that was
    // never taken would unpin somebody else's sample.
    bool holds_data;
};

static pcvoice sVoices[ISND_TOTAL_VOICES];

// How many more plays BFBB_SND has left to report. See iStartVoice.
static S32 sReportPlays;

// BFBB_SNDMIX. See iReportMix.
static bool sReportMix;
static U64 sLastMixReportNs;

// BFBB_SNDWHO=<asset id in hex>: the calling stack the first few times that
// sound is started. "Which code plays this" is otherwise unanswerable -- the
// retail Xbox assets carry no ADBG names, so an asset id is all there is, and
// the platform layer sees the call with no context at all.
static U32 sWhoAsset;
static S32 sWhoLeft;

static iSndExternalCallback sExternalCallback;
static bool sStereo = true;
static bool sSuspended;

// Defined further down with the rest of the mix, but called from the update
// sweep above it.
static void iApplyVoiceMix(S32 i);
static void iReportMix();

static void iReleaseVoice(S32 i)
{
    if (i < 0 || i >= ISND_TOTAL_VOICES)
    {
        return;
    }

    // BFBB_MUSIC: what kills the music. zMusic goes on believing its track is
    // playing long after the voice has gone, so the interesting event is not
    // where the music starts -- it starts fine -- but who ends it.
    if (sVoices[i].in_use && gSnd.voice[i].category == SND_CAT_MUSIC &&
        getenv("BFBB_MUSIC") != NULL)
    {
        char why[96];
        snprintf(why, sizeof(why), "music voice %d (%08x, handle %u) released", i,
                 gSnd.voice[i].assetID, gSnd.voice[i].sndID);
        iHostPrintCallers(why, 20);
    }

    if (sVoices[i].host >= 0)
    {
        // Stop before releasing the samples, in that order. iSndHostStop drops
        // the backend's pointer to them under its own lock, so once it returns
        // the mixer cannot reach the memory the next line unpins.
        iSndHostStop(sVoices[i].host);
        iSndHostRelease(sVoices[i].host);
    }

    if (sVoices[i].holds_data)
    {
        iSndDataRelease(sVoices[i].aid);
        sVoices[i].holds_data = false;
    }

    sVoices[i].host = -1;
    sVoices[i].aid = 0;
    sVoices[i].in_use = false;
    sVoices[i].paused = false;
}

void iSndInit()
{
    for (S32 i = 0; i < ISND_TOTAL_VOICES; i++)
    {
        sVoices[i].host = -1;
        sVoices[i].aid = 0;
        sVoices[i].in_use = false;
        sVoices[i].paused = false;
        sVoices[i].holds_data = false;
    }

    for (S32 i = 0; i < ISND_MAX_TABLES; i++)
    {
        sinfo_array[i] = NULL;
    }

    sinfo_array_max = 0;
    sExternalCallback = NULL;
    sStereo = true;
    sSuspended = false;

    memset(&snd, 0, sizeof(snd));

    iSndDataReset();
    iSndHostInit();

    sReportPlays = 0;
    {
        const char* env = getenv("BFBB_SND");
        if (env != NULL)
        {
            sReportPlays = atoi(env);
            if (sReportPlays <= 0)
            {
                sReportPlays = 32;
            }
        }
    }

    sReportMix = getenv("BFBB_SNDMIX") != NULL;
    sLastMixReportNs = 0;

    sWhoAsset = 0;
    sWhoLeft = 0;
    {
        const char* env = getenv("BFBB_SNDWHO");
        if (env != NULL)
        {
            sWhoAsset = (U32)strtoul(env, NULL, 16);
            sWhoLeft = 3;
        }
    }

    printf("iSnd: audio backend is %s\n", iSndHostName());
    fflush(stdout);
}

void iSndExit()
{
    for (S32 i = 0; i < ISND_TOTAL_VOICES; i++)
    {
        iReleaseVoice(i);
    }

    iSndHostExit();
    iSndDataReset();
    sinfo_array_max = 0;
}

void iSndSetEnvironmentalEffect(isound_effect)
{
    // The GameCube applies this as a DSP reverb preset through AX. Nothing to
    // apply without a device, and the seam deliberately does not carry it: a
    // backend that has reverb should be given the game's parameters, not a
    // GameCube preset index. Left for whoever adds one.
}

void iSndInitSceneLoaded()
{
}

// ---------------------------------------------------------------------------
// Queries

bool iSndIsPlaying(U32 assetID)
{
    if (assetID == 0)
    {
        return false;
    }

    for (S32 i = 0; i < ISND_TOTAL_VOICES; i++)
    {
        if (sVoices[i].in_use && sVoices[i].aid == assetID && iSndHostIsPlaying(sVoices[i].host))
        {
            return true;
        }
    }

    return false;
}

bool iSndIsPlaying(U32 assetID, U32 parid)
{
    if (assetID == 0)
    {
        return false;
    }

    for (S32 i = 0; i < ISND_TOTAL_VOICES; i++)
    {
        if (!sVoices[i].in_use || sVoices[i].aid != assetID)
        {
            continue;
        }

        if (gSnd.voice[i].parentID != parid)
        {
            continue;
        }

        if (iSndHostIsPlaying(sVoices[i].host))
        {
            return true;
        }
    }

    return false;
}

bool iSndIsPlayingByHandle(U32 handle)
{
    if (handle == 0)
    {
        return false;
    }

    for (S32 i = 0; i < ISND_TOTAL_VOICES; i++)
    {
        if (gSnd.voice[i].sndID == handle)
        {
            return sVoices[i].in_use && iSndHostIsPlaying(sVoices[i].host);
        }
    }

    return false;
}

// Translate one Xbox table entry into the DSP-shaped record iSndLookup hands
// back. Only three of those fields are read anywhere: sample_rate and the id by
// xSnd.cpp, num_samples by iStartVoice to time the voice out. The rest are
// zeroed rather than invented -- there are no DSP coefficients on this platform
// and nothing reads them.
// How many sample frames an entry's asset holds.
//
// For PCM that is bytes over the frame size, which is what block_align means
// there. For ADPCM block_align is the size of a compressed BLOCK instead, each
// holding one predictor sample and two more per payload byte -- so the same
// division gives a block count, about 65 times too small. Nothing in the game
// reads this, but the play path uses it to time a voice whose samples could not
// be read, and being 65 times short would cut the menu music off instantly.
static U32 entry_frames(const xbox_sndhdr& e)
{
    if (e.block_align == 0)
    {
        return 0;
    }

    if (e.format_tag == 0x69 && e.block_align >= 5)
    {
        U32 blocks = e.data_size / e.block_align;
        return blocks * ((e.block_align - 4) * 2 + 1);
    }

    return e.data_size / e.block_align;
}

static void fill_lookup(const xbox_sndhdr& e)
{
    memset(&snd.hdr, 0, sizeof(snd.hdr));

    snd.hdr.sample_rate = e.samples_per_sec;
    snd.hdr.format = e.format_tag;
    snd.hdr.assetID = e.assetID;
    snd.hdr.num_samples = entry_frames(e);
}

// The table entry for one asset, without the side effects of iSndLookup.
//
// iSndLookup hands out a fresh internal id on every call and rewrites the
// static it returns, so the play path cannot use it to answer "what shape is
// this sound?" -- it would burn an id and disturb the record the caller is
// still holding. This walks the same tables and reads nothing but the entry,
// so iStartVoice can ask about the voice it is starting rather than trusting
// that a lookup happened immediately before.
static const xbox_sndhdr* find_entry(U32 assetID)
{
    if (assetID == 0)
    {
        return NULL;
    }

    for (S32 i = sinfo_array_max - 1; i >= 0; i--)
    {
        sndinfo* info = sinfo_array[i];
        if (info == NULL)
        {
            continue;
        }

        U32 n = info->num_sfx + info->num_streams + info->num_cutscene;
        for (U32 j = 0; j < n; j++)
        {
            if (info->entry[j].assetID == assetID)
            {
                return &info->entry[j];
            }
        }
    }

    return NULL;
}

iSndFileInfo* iSndLookup(U32 id)
{
    // Retail hands out ids from two ranges and wraps each one: streams below
    // 0x1000, sounds at or above it. xSnd.cpp tells the two apart by exactly
    // that test -- ip->ID >= 0x1000 selects the sound path -- so the ranges are
    // contract, not bookkeeping.
    static S32 strm_id = 1;
    static S32 snd_id = 0x1000;

    sound_stream = 0;

    if (id == 0)
    {
        return NULL;
    }

    for (S32 i = sinfo_array_max - 1; i >= 0; i--)
    {
        sndinfo* info = sinfo_array[i];
        if (info == NULL)
        {
            continue;
        }

        xbox_sndhdr* entry = info->entry;
        U32 n = info->num_sfx;
        U32 j = 0;

        for (; j < n; j++)
        {
            if (id == entry[j].assetID)
            {
                fill_lookup(entry[j]);
                snd.id = snd_id++;
                if (snd_id >= 0x7ffa)
                {
                    snd_id = 0x1000;
                }
                return (iSndFileInfo*)&snd;
            }
        }

        n = info->num_streams + n;
        for (; j < n; j++)
        {
            if (id == entry[j].assetID)
            {
                fill_lookup(entry[j]);
                snd.id = strm_id++;
                if (strm_id >= 0xffe)
                {
                    strm_id = 1;
                }

                // The GameCube table tags a memory-resident stream with
                // pad[0] == 0x63 and hands it a sound-range id so the play path
                // treats it as one. The Xbox entry has no such field -- every
                // entry is the same 44 bytes with nothing spare -- so a stream
                // here is always a stream.
                sound_stream = 1;
                return (iSndFileInfo*)&snd;
            }
        }

        n = info->num_cutscene + n;
        for (; j < n; j++)
        {
            if (id == entry[j].assetID)
            {
                fill_lookup(entry[j]);
                snd.id = strm_id++;
                if (strm_id >= 0xffe)
                {
                    strm_id = 1;
                }
                sound_stream = 2;
                return (iSndFileInfo*)&snd;
            }
        }
    }

    return NULL;
}

// ---------------------------------------------------------------------------
// Transport

void iSndPause(U32 handle, U32 pause)
{
    if (handle == 0)
    {
        return;
    }

    for (S32 i = 0; i < ISND_TOTAL_VOICES; i++)
    {
        if (gSnd.voice[i].sndID == handle && sVoices[i].in_use)
        {
            sVoices[i].paused = (pause != 0);
            iSndHostPause(sVoices[i].host, pause != 0);
            return;
        }
    }
}

void iSndStop(U32 handle)
{
    if (handle == 0)
    {
        return;
    }

    for (S32 i = 0; i < ISND_TOTAL_VOICES; i++)
    {
        if (gSnd.voice[i].sndID == handle)
        {
            // Retail clears the game-side slot here as well as the platform
            // one, and callers rely on it -- xSndStop does not clear sndID.
            gSnd.voice[i].sndID = 0;
            gSnd.voice[i].flags = 0;
            iReleaseVoice(i);
            return;
        }
    }
}

void iSndUpdate()
{
    iSndHostUpdate();

    // Retail's iSndVolUpdate: follow each live emitter and recompute its mix.
    // This has to run before the retirement sweep below, so that a voice which
    // ends this frame is not re-mixed after being released.
    for (S32 i = 0; i < ISND_TOTAL_VOICES; i++)
    {
        if (!sVoices[i].in_use)
        {
            continue;
        }

        xSndInternalUpdateVoicePos(&gSnd.voice[i]);
        iApplyVoiceMix(i);
    }

    iReportMix();

    // Retire voices the device has finished, so the game's table does not hold
    // handles for sounds that have stopped. Retail does this from its AX
    // callback; here the frame is the only clock there is.
    for (S32 i = 0; i < ISND_TOTAL_VOICES; i++)
    {
        if (!sVoices[i].in_use || sVoices[i].paused)
        {
            continue;
        }

        if (!iSndHostIsPlaying(sVoices[i].host))
        {
            U32 handle = gSnd.voice[i].sndID;

            gSnd.voice[i].sndID = 0;
            gSnd.voice[i].flags = 0;
            iReleaseVoice(i);

            if (sExternalCallback != NULL)
            {
                sExternalCallback(handle);
            }
        }
    }
}

S32 iSndFindFreeVoice(U32 priority, U32 flags, U32 owner)
{
    if (priority > 0xff)
    {
        priority = 0xff;
    }

    // flags & 4 is a stream, and streams live in the first six slots. xSnd sets
    // that bit from the lookup id being below 0x1000.
    bool stream = (flags & 0x4) != 0;
    S32 begin = stream ? 0 : ISND_STREAM_VOICES;
    S32 end = stream ? ISND_STREAM_VOICES : ISND_TOTAL_VOICES;

    // A caller that already owns a stream slot reclaims its own before taking a
    // new one, so one emitter cannot occupy every stream voice.
    if (stream && owner != 0)
    {
        for (S32 i = begin; i < end; i++)
        {
            if (gSnd.voice[i].lock_owner != owner)
            {
                continue;
            }

            if (gSnd.voice[i].sndID != 0)
            {
                iSndStop(gSnd.voice[i].sndID);
            }

            S32 host = iSndHostAcquire(priority >> 3);
            if (host < 0)
            {
                return -1;
            }

            if (sVoices[i].holds_data)
            {
                iSndDataRelease(sVoices[i].aid);
                sVoices[i].holds_data = false;
            }

            sVoices[i].host = host;
            sVoices[i].in_use = true;
            sVoices[i].paused = false;
            sVoices[i].aid = 0;
            return i;
        }
    }

    for (S32 i = begin; i < end; i++)
    {
        // **A stream slot someone has locked is not free**, even when nothing is
        // playing on it. Retail tests this (src/SB/Core/gc/iSnd.cpp:1241) and
        // the port did not, which is why the level music stopped at the first
        // line of dialogue and never came back.
        //
        // zTalkBox reserves up to two of the six stream slots for a
        // conversation with xSndStreamLock (zTalkBox.cpp:1022), which marks a
        // slot without playing anything on it. Handing that slot to the music
        // instead put the music somewhere a talkbox already believed it owned;
        // its next line came through the reclaim branch above, found its own
        // lock, and stopped what was there. zMusic never noticed -- it keeps
        // its track's handle and does not poll it -- so the music was gone for
        // the rest of the scene.
        //
        // Only streams have owners. Retail's arm for the other 58 voices does
        // not test this and neither does the loop bound here, because `begin`
        // and `end` already restrict a stream to the first six.
        if (stream && gSnd.voice[i].lock_owner != 0)
        {
            continue;
        }

        if (sVoices[i].in_use)
        {
            continue;
        }

        S32 host = iSndHostAcquire(priority >> 3);
        if (host < 0)
        {
            return -1;
        }

        if (sVoices[i].holds_data)
        {
            iSndDataRelease(sVoices[i].aid);
            sVoices[i].holds_data = false;
        }

        sVoices[i].host = host;
        sVoices[i].in_use = true;
        sVoices[i].paused = false;
        sVoices[i].aid = 0;
        return i;
    }

    return -1;
}

// ---------------------------------------------------------------------------
// Playback
//
// The GameCube separates these four because the hardware path differs: a stream
// is read from disc in interleaved blocks and fed through ARQ, a memory stream
// is already resident, a sound is uploaded to ARAM once. A host backend is told
// the same thing in every case -- this many samples at this rate -- so they
// converge here. They stay four entry points because iSndPlay dispatches on
// them and iSnd.h declares them.

// ---------------------------------------------------------------------------
// Pitch
//
// **The game's pitch is in SEMITONES, not a playback ratio.** Zero is the
// sample's own rate, +12 an octave up, -12 an octave down, and retail converts
// with powf(2, pitch/12) at all four places it reaches AX
// (src/SB/Core/gc/iSnd.cpp:1346, 1417, 1500 and 1626).
//
// The seam takes a ratio, because that is what a mixer steps its read position
// by, so the conversion belongs here. Passing the semitones straight through is
// wrong in a way that is easy to miss and loud when it happens: the commonest
// value is 0, which a ratio reads as "stopped" and so gets specially guarded
// to 1.0, which is right by accident -- and then the HUD's counter ping, which
// rises to 6.5 semitones as it counts up, plays at six and a half times its
// rate instead of one and a half.
static F32 iPitchRatio(F32 semitones)
{
    if (semitones == 0.0f)
    {
        return 1.0f;
    }

    F32 ratio = powf(2.0f, semitones / 12.0f);

    // A backend steps a read position by this, so it has to stay sane even if a
    // caller passes something absurd. Retail's range is -12 to +6.5.
    if (ratio < 0.01f)
    {
        ratio = 0.01f;
    }
    else if (ratio > 16.0f)
    {
        ratio = 16.0f;
    }

    return ratio;
}

// ---------------------------------------------------------------------------
// The mix
//
// This is iSndCalcVol and iSndCalcVol3d from src/SB/Core/gc/iSnd.cpp, which
// the port had been missing entirely. The GameCube runs both from
// iSndVolUpdate once a frame per voice, so distance attenuation and panning are
// not properties of a sound -- they are recomputed as the emitter and the
// listener move. Without them every sound in the level plays at full volume in
// both ears, which was invisible only because the null backend threw the
// volume away.
//
// The one thing that cannot be reproduced is retail's units. AX takes a
// logarithmic fader and a 0..127 pan index and MIX interpolates between them;
// the seam here takes a linear gain per side, because that is what a host
// mixer wants and converting to decibels and back would only lose precision.
// So the curve is the same and the last step differs: an equal-power pan, which
// keeps a sound's loudness constant as it crosses in front of the listener.

static void iApplyVoiceMix(S32 i)
{
    if (i < 0 || i >= ISND_TOTAL_VOICES || !sVoices[i].in_use)
    {
        return;
    }

    xSndVoiceInfo* vp = &gSnd.voice[i];

    F32 vol = vp->vol * gSnd.categoryVolFader[vp->category];

    // Retail's pan index, 0 hard left through 0x40 centre to 0x7f hard right.
    // A voice with no position stays centred, which is what iSndCalcVol does.
    F32 pan01 = 0.5f;

    if (vp->flags & 0x8)
    {
        xVec3 to;
        xVec3Sub(&to, &vp->playPos, &gSnd.pos);
        F32 dist2 = xVec3Length2(&to);

        F32 scale;
        if (dist2 > vp->outerRadius2)
        {
            scale = 0.0f;
        }
        else if (dist2 <= vp->innerRadius2)
        {
            scale = 1.0f;
        }
        else
        {
            F32 fadeRange = vp->outerRadius2 - vp->innerRadius2;
            scale = sqrtf((fadeRange - (dist2 - vp->innerRadius2)) / fadeRange);
        }

        vol *= scale;

        // Retail normalises unconditionally. A listener standing exactly on an
        // emitter would divide by zero there; on AX that produced a harmless
        // garbage pan index, but here it would put a NaN into the mix and the
        // whole output with it, so the degenerate case is centred instead.
        if (dist2 > 1e-8f)
        {
            xVec3Normalize(&to, &to);
            F32 pan = xVec3Dot(&to, &gSnd.right);

            S32 ipan = (S32)(64.0f * pan) + 0x40;
            if (ipan < 0)
            {
                ipan = 0;
            }
            else if (ipan > 0x7f)
            {
                ipan = 0x7f;
            }

            pan01 = (F32)ipan / 127.0f;
        }
    }

    if (vol < 0.0f)
    {
        vol = 0.0f;
    }
    else if (vol > 1.0f)
    {
        vol = 1.0f;
    }

    if (!sStereo)
    {
        pan01 = 0.5f;
    }

    F32 theta = pan01 * 1.5707963f;
    F32 left = cosf(theta) * vol;
    F32 right = sinf(theta) * vol;

    sVoices[i].last_left = left;
    sVoices[i].last_right = right;

    iSndHostSetVol(sVoices[i].host, left, right);
}

// BFBB_SNDMIX: every second, every live voice and the gain it is being mixed
// at. A sound that is playing and inaudible and a sound that never started look
// identical from outside, and they have nothing in common.
static void iReportMix()
{
    if (!sReportMix)
    {
        return;
    }

    U64 now = iHostMonotonicNs();
    if (now - sLastMixReportNs < 1000000000ULL)
    {
        return;
    }

    sLastMixReportNs = now;

    static const char* kCategory[] = { "game", "dialog", "music", "cutscene", "ui" };

    U32 cached = 0;
    U32 cachedBytes = 0;
    U32 pinned = 0;
    iSndDataStats(&cached, &cachedBytes, &pinned);

    printf("iSnd: --- voices  (cache %u entries, %u KB, %u pinned)  faders", cached,
           cachedBytes / 1024, pinned);
    for (S32 c = 0; c < 5; c++)
    {
        printf(" %s=%.2f", kCategory[c], gSnd.categoryVolFader[c]);
    }
    printf("\n");

    // The sound tables, and whether the package each came from is still open.
    // A table whose package has been unloaded still answers a lookup -- nothing
    // clears sinfo_array -- but its samples cannot be read any more, so a sound
    // in it starts and plays silently. That is a different failure from a sound
    // that was never in a table at all, and only this tells them apart.
    printf("iSnd:   tables %d:", sinfo_array_max);
    for (S32 t = 0; t < sinfo_array_max; t++)
    {
        if (sinfo_array[t] == NULL)
        {
            printf(" [%d empty]", t);
            continue;
        }

        U32 n = sinfo_array[t]->num_sfx + sinfo_array[t]->num_streams +
                sinfo_array[t]->num_cutscene;
        const char* pkg = n != 0 ? xST_xAssetID_HIPFullPath(sinfo_array[t]->entry[0].assetID) : NULL;
        printf(" [%d %u in %s]", t, n, pkg != NULL ? pkg : "NO PACKAGE");
    }
    printf("\n");

    for (S32 i = 0; i < ISND_TOTAL_VOICES; i++)
    {
        if (!sVoices[i].in_use)
        {
            continue;
        }

        xSndVoiceInfo* vp = &gSnd.voice[i];
        const char* cat = (U32)vp->category < 5 ? kCategory[vp->category] : "?";

        printf("iSnd:   [%2d] %08x %-8s vol=%.3f -> L=%.3f R=%.3f  flags=%08x parent=%08x %s%s%s\n",
               i, vp->assetID, cat, vp->vol, sVoices[i].last_left, sVoices[i].last_right,
               vp->flags, vp->parentID,
               iSndHostIsPlaying(sVoices[i].host) ? "playing" : "SILENT",
               (vp->flags & 0x8) ? " 3d" : "", sVoices[i].holds_data ? "" : " nodata");
    }

    fflush(stdout);
}

static S32 iStartVoice(xSndVoiceInfo* vp)
{
    S32 i = (S32)(vp - gSnd.voice);
    if (i < 0 || i >= ISND_TOTAL_VOICES || !sVoices[i].in_use)
    {
        return 0;
    }

    // Whatever this slot was holding, it is not holding it any more.
    if (sVoices[i].holds_data)
    {
        iSndDataRelease(sVoices[i].aid);
        sVoices[i].holds_data = false;
    }

    sVoices[i].aid = vp->assetID;

    const xbox_sndhdr* e = find_entry(vp->assetID);

    iSndHostSample sample;
    memset(&sample, 0, sizeof(sample));

    // The samples themselves, pinned for as long as this voice plays them. A
    // sound whose asset will not read still starts, with no data: it is
    // inaudible and finishes when it would have, which is what everything in
    // the game that waits on a sound needs it to do.
    iSndDataFormat fmt;
    fmt.format_tag = e != NULL ? e->format_tag : 1;
    fmt.channels = (e != NULL && e->channels != 0) ? e->channels : 1;
    fmt.block_align = e != NULL ? e->block_align : 2;

    U32 bytes = 0;
    sample.data = iSndDataAcquire(vp->assetID, &fmt, &bytes);
    sample.bytes = bytes;
    sVoices[i].holds_data = (sample.data != NULL);

    sample.channels = fmt.channels;

    // Always 16, whatever the table says: iSndDataAcquire decodes on the way
    // in, so a 4-bit ADPCM asset reaches the backend as PCM. Passing the
    // table's own bits_per_sample here is what a reader expects to see and is
    // exactly wrong -- it would make the mixer read 16-bit samples as bytes.
    sample.bits = 16;

    // vp->sample_rate is the rate xSnd read out of the same table on its way
    // here, so the two agree; it is preferred only because a caller is free to
    // have changed it.
    sample.sample_rate = vp->sample_rate != 0
                             ? vp->sample_rate
                             : (e != NULL ? e->samples_per_sec : snd.hdr.sample_rate);

    // The Xbox table carries no loop points -- its entries are a WAVEFORMATEX
    // and a size, and nothing else. The GameCube reads loop_flag out of the
    // DSPADPCM header OR takes the caller's 0x8000 (src/SB/Core/gc/iSnd.cpp:
    // 1304); with no header to read, only the flag is left. zMusic sets it from
    // a track's `loop` and xSFX sets it for a looping emitter, which is every
    // looping sound the game has, so nothing is lost -- and a loop restarts at
    // the beginning, because there is no other point to restart at.
    sample.looping = (vp->flags & 0x8000) != 0;
    sample.loop_start = 0;

    U32 frame_bytes = sample.channels * (sample.bits / 8);
    sample.num_samples = frame_bytes != 0 ? bytes / frame_bytes : 0;

    if (sample.num_samples == 0)
    {
        // Nothing was read. Fall back to the length the table implies so the
        // voice still times out correctly rather than ending instantly.
        sample.num_samples = e != NULL ? entry_frames(*e) : snd.hdr.num_samples;
    }

    // BFBB_SND reports what actually reaches the mixer. A sound that is
    // inaudible has failed at one of four places -- the asset was not in the
    // table, its bytes did not read, the mix came out at zero, or the device is
    // not running -- and only the first two are visible from anywhere else.
    if (sReportPlays > 0)
    {
        sReportPlays--;
        printf("iSnd: play %08x  %u Hz  %u ch  %u-bit  %u frames  %u bytes%s%s\n", vp->assetID,
               sample.sample_rate, sample.channels, sample.bits, sample.num_samples, bytes,
               sample.looping ? "  looping" : "", sample.data != NULL ? "" : "  NO DATA");
        if (sReportPlays == 0)
        {
            printf("iSnd:   (that is the last one BFBB_SND will report)\n");
        }
        fflush(stdout);
    }

    if (sWhoLeft > 0 && vp->assetID == sWhoAsset)
    {
        sWhoLeft--;
        char why[96];
        snprintf(why, sizeof(why), "sound %08x started on voice %d", vp->assetID, i);
        iHostPrintCallers(why, 24);
    }

    // Pitch multiplies the sample's own rate and the backend applies it to the
    // step, so it has to be set before the voice starts.
    iSndHostSetPitch(sVoices[i].host, iPitchRatio(vp->pitch));

    // And the mix before it too: iSndHostStart takes the current gain as the
    // voice's starting gain rather than ramping up to it, so a voice started
    // before its volume is known would begin at silence and fade in.
    iApplyVoiceMix(i);
    iSndHostStart(sVoices[i].host, &sample);

    return vp->sndID;
}

S32 iSndPrepStream(xSndVoiceInfo* vp)
{
    // Retail queues the first disc read here and returns a value the caller
    // range-checks against 0x3a before starting playback. With nothing to
    // pre-buffer there is nothing to prepare, and the voice index is what that
    // check expects to see.
    return (S32)(vp - gSnd.voice);
}

S32 iSndPlayStream(xSndVoiceInfo* vp)
{
    return iStartVoice(vp);
}

S32 iSndPlayMemStream(xSndVoiceInfo* vp)
{
    return iStartVoice(vp);
}

S32 iSndPlaySound(xSndVoiceInfo* vp)
{
    return iStartVoice(vp);
}

S32 iSndPlay(xSndVoiceInfo* vp)
{
    // Retail computes this as a byte offset divided by 100, which is
    // sizeof(xSndVoiceInfo) on a 32-bit build. Pointer arithmetic says the same
    // thing without depending on the size.
    S32 i = (S32)(vp - gSnd.voice);

    if (i < 0 || i >= ISND_TOTAL_VOICES)
    {
        return 0;
    }

    if (i < ISND_STREAM_VOICES)
    {
        S32 ret = iSndPrepStream(vp);
        if (ret < 0x3a)
        {
            if (vp->flags & 0x200)
            {
                return iSndPlayMemStream(vp);
            }
            return iSndPlayStream(vp);
        }
        return ret;
    }

    return iSndPlaySound(vp);
}

// ---------------------------------------------------------------------------
// Mixing

void iSndSetVol(U32 handle, F32 vol)
{
    if (handle == 0)
    {
        return;
    }

    for (S32 i = 0; i < ISND_TOTAL_VOICES; i++)
    {
        if (gSnd.voice[i].sndID != handle || !sVoices[i].in_use)
        {
            continue;
        }

        gSnd.voice[i].vol = vol;

        // Through iApplyVoiceMix rather than straight to the backend, so that
        // setting a volume does not throw away the pan and the distance
        // attenuation the last frame computed.
        iApplyVoiceMix(i);
        return;
    }
}

F32 iSndGetVol(U32 handle)
{
    if (handle == 0)
    {
        return 0.0f;
    }

    for (S32 i = 0; i < ISND_TOTAL_VOICES; i++)
    {
        if (gSnd.voice[i].sndID == handle)
        {
            return gSnd.voice[i].vol;
        }
    }

    return 0.0f;
}

void iSndSetPitch(U32 handle, F32 pitch)
{
    if (handle == 0)
    {
        return;
    }

    for (S32 i = 0; i < ISND_TOTAL_VOICES; i++)
    {
        if (gSnd.voice[i].sndID == handle && sVoices[i].in_use)
        {
            gSnd.voice[i].pitch = pitch;
            iSndHostSetPitch(sVoices[i].host, iPitchRatio(pitch));
            return;
        }
    }
}

void iSndStartStereo(U32 id1, U32 id2, F32 pitch)
{
    // Two voices started together as a stereo pair. Without a device there is
    // no pair to keep in sync, but the pitch still has to reach both or their
    // durations diverge and one finishes early.
    iSndSetPitch(id1, pitch);
    iSndSetPitch(id2, pitch);
}

void iSndStereo(U32 stereo)
{
    sStereo = (stereo != 0);
}

// ---------------------------------------------------------------------------
// Lifecycle

void iSndWaitForDeadSounds()
{
    // Retail blocks until the DSP has drained every voice it was asked to stop.
    // Nothing is in flight here, so there is nothing to wait for -- but the
    // voices still have to be retired, which is what the caller is waiting on.
    iSndUpdate();
}

void iSndSuspendCD(U32)
{
    // Streams come off the disc on the GameCube, so this stops the drive.
    // There is no drive.
}

static void iStopAllVoices()
{
    for (S32 i = 0; i < ISND_TOTAL_VOICES; i++)
    {
        if (sVoices[i].in_use)
        {
            gSnd.voice[i].sndID = 0;
            gSnd.voice[i].flags = 0;
            iReleaseVoice(i);
        }
    }
}

void iSndSceneExit()
{
    iStopAllVoices();

    // **And pop the sound table**, which retail does here and this had been
    // missing (src/SB/Core/gc/iSnd.cpp:1720). It is not bookkeeping. Every call
    // site pairs this with an xSTUnLoadScene of the package a table came from --
    // zScene.cpp:1229 and the three in zEntPlayer.cpp -- so one push in
    // iSndLoadSounds is matched by one pop here, and without the pop the array
    // only grows.
    //
    // Two things go wrong when it does. The array is twelve long and refuses
    // anything past that, so after a few level changes a scene loads with no
    // sounds at all. And worse, a table left behind points into the unloaded
    // package's memory: iSndLookup walks the array from the top down, so it
    // reads every stale table BEFORE reaching the live one. Whatever now
    // occupies that memory is read as a sound entry, and an id that matches by
    // accident plays at whatever sample rate and format the garbage says.
    //
    // Retail also RwFrees the table. The port must not: these point into the
    // packer's own layer memory, which the packer owns and frees itself.
    if (sinfo_array_max > 0)
    {
        sinfo_array_max--;
        sinfo_array[sinfo_array_max] = NULL;
    }
}

S32 iSndLoadSounds(void* data)
{
    sndinfo* p = (sndinfo*)data;

    if (p == NULL)
    {
        return 0;
    }

    if (sinfo_array_max >= ISND_MAX_TABLES)
    {
        // Retail calls exit(-1) here. Refusing the table costs that scene its
        // sound rather than the process, and says so.
        printf("iSndLoadSounds: more than %d sound tables loaded; ignoring\n", ISND_MAX_TABLES);
        return 0;
    }

    // An empty table still takes a slot: iSndLookup walks the array backwards
    // by index, and retail records the gap rather than closing it.
    if (p->num_sfx == 0 && p->num_streams == 0 && p->num_cutscene == 0)
    {
        sinfo_array[sinfo_array_max++] = NULL;
        return 0;
    }

    sinfo_array[sinfo_array_max++] = p;

    // What is NOT done here, and what a real backend has to add: retail also
    // opens the HIP holding the sample data, converts each entry's loop points
    // to nibble addresses relative to where the asset landed, and uploads the
    // ADPCM to ARAM. Lookup and timing need none of that -- num_samples and
    // sample_rate are in the table already -- but playing an actual sample
    // does. The offsets it would need are reachable through
    // xSTGetAssetInfoInHxP, as src/SB/Core/gc/iSnd.cpp shows.

    return 1;
}

void iSndDIEDIEDIE()
{
    // Voices only. Retail's version silences the hardware and clears
    // soundInited; what it does NOT do is pop a sound table, and it must not --
    // it is the reset-button and disc-error kill switch (iSystem.cpp:152),
    // which is not a scene ending. Calling iSndSceneExit here would drop a
    // table the loaded scene still needs.
    iStopAllVoices();
}

void iSndSetExternalCallback(iSndExternalCallback callback)
{
    sExternalCallback = callback;
}

void iSndSuspend()
{
    if (sSuspended)
    {
        return;
    }

    sSuspended = true;

    for (S32 i = 0; i < ISND_TOTAL_VOICES; i++)
    {
        // A voice the game paused itself is left alone, so that resuming does
        // not un-pause something suspend did not pause.
        if (sVoices[i].in_use && !sVoices[i].paused)
        {
            iSndHostPause(sVoices[i].host, true);
        }
    }
}

void iSndResume()
{
    if (!sSuspended)
    {
        return;
    }

    sSuspended = false;

    for (S32 i = 0; i < ISND_TOTAL_VOICES; i++)
    {
        if (sVoices[i].in_use && !sVoices[i].paused)
        {
            iSndHostPause(sVoices[i].host, false);
        }
    }
}
