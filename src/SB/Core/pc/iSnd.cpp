#include "iSnd.h"
#include "iSndHost.h"

#include "xSnd.h"

#include <stdio.h>
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
// PCPORT.md, "Asset caveats"); against GameCube-native assets every field read
// in this file would be byte-swapped.

// ---------------------------------------------------------------------------
// Asset layout
//
// The GC DSPADPCM header with the asset ID appended, exactly as
// src/SB/Core/gc/iSnd.cpp describes it. Three fields are read outside this
// file, and xSnd.cpp reaches them through its own iSndLookupInfo declaration at
// fixed offsets -- num_samples at 0x00, sample_rate at 0x08 and the internal id
// at 0x64 -- so this layout is load-bearing and must not be rearranged.

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

// The header of a loaded SNDI asset.
struct sndinfo
{
    U32 num_sfx; // 0x00
    U32 total_size; // 0x04
    U32 num_streams; // 0x08
    U32 num_cutscene; // 0x0C
    sndhdr entry[1]; // 0x10
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
};

static pcvoice sVoices[ISND_TOTAL_VOICES];
static iSndExternalCallback sExternalCallback;
static bool sStereo = true;
static bool sSuspended;

static void iReleaseVoice(S32 i)
{
    if (i < 0 || i >= ISND_TOTAL_VOICES)
    {
        return;
    }

    if (sVoices[i].host >= 0)
    {
        iSndHostStop(sVoices[i].host);
        iSndHostRelease(sVoices[i].host);
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

    iSndHostInit();
}

void iSndExit()
{
    for (S32 i = 0; i < ISND_TOTAL_VOICES; i++)
    {
        iReleaseVoice(i);
    }

    iSndHostExit();
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

        sndhdr* entry = info->entry;
        U32 n = info->num_sfx;
        U32 j = 0;

        for (; j < n; j++)
        {
            if (id == entry[j].assetID)
            {
                memcpy(&snd.hdr, &entry[j], sizeof(sndhdr));
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
                memcpy(&snd.hdr, &entry[j], sizeof(sndhdr));
                snd.id = strm_id++;
                if (strm_id >= 0xffe)
                {
                    strm_id = 1;
                }

                // pad[0] == 0x63 marks a stream held in memory rather than read
                // from disc; retail gives it a sound-range id so the play path
                // treats it as one.
                if (entry[j].pad[0] == 0x63)
                {
                    snd.id = 0x1000;
                    sound_stream = 0;
                }
                else
                {
                    sound_stream = 1;
                }
                return (iSndFileInfo*)&snd;
            }
        }

        n = info->num_cutscene + n;
        for (; j < n; j++)
        {
            if (id == entry[j].assetID)
            {
                memcpy(&snd.hdr, &entry[j], sizeof(sndhdr));
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

            sVoices[i].host = host;
            sVoices[i].in_use = true;
            sVoices[i].paused = false;
            sVoices[i].aid = 0;
            return i;
        }
    }

    for (S32 i = begin; i < end; i++)
    {
        if (sVoices[i].in_use)
        {
            continue;
        }

        S32 host = iSndHostAcquire(priority >> 3);
        if (host < 0)
        {
            return -1;
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

static S32 iStartVoice(xSndVoiceInfo* vp)
{
    S32 i = (S32)(vp - gSnd.voice);
    if (i < 0 || i >= ISND_TOTAL_VOICES || !sVoices[i].in_use)
    {
        return 0;
    }

    sVoices[i].aid = vp->assetID;

    // snd still holds the entry the caller looked up on its way here: xSnd
    // calls iSndLookup and then iSndPlay without another lookup in between.
    // That is retail's arrangement too, and it is why iSndLookup can hand back
    // a pointer to one static.
    iSndHostSample sample;
    sample.sample_rate = vp->sample_rate != 0 ? vp->sample_rate : snd.hdr.sample_rate;
    sample.num_samples = snd.hdr.num_samples;
    sample.looping = (snd.hdr.loop_flag != 0);
    sample.loop_start = snd.hdr.loop_start;

    // Pitch multiplies the sample's own rate and the backend applies it to the
    // duration, so it has to be set before the voice starts.
    iSndHostSetPitch(sVoices[i].host, vp->pitch > 0.0f ? vp->pitch : 1.0f);
    iSndHostStart(sVoices[i].host, &sample);
    iSndSetVol(vp->sndID, vp->vol);

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

        F32 scaled = vol * gSnd.categoryVolFader[gSnd.voice[i].category];
        if (scaled < 0.0f)
        {
            scaled = 0.0f;
        }
        else if (scaled > 1.0f)
        {
            scaled = 1.0f;
        }

        // Retail converts to AX's log scale here (iVolFromX, 43.43 * log). The
        // seam takes linear because that is what host audio libraries want, and
        // converting to decibels and back would only lose precision.
        iSndHostSetVol(sVoices[i].host, scaled, scaled);
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
            iSndHostSetPitch(sVoices[i].host, pitch);
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

void iSndSceneExit()
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
    iSndSceneExit();
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
