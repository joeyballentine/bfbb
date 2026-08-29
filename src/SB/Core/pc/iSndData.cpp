#include "iSndData.h"

#include "iFile.h"

#include "xFile.h"
#include "xString.h"
#include "xstransvc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Getting the samples into main memory.
//
// **The packer never loads them.** A scene's sound assets are all in one layer,
// PKR_LTYPE_SRAM, and PKR_layerLoadDest maps that layer to PKR_LDDEST_SKIP
// (xpkrsvc.cpp:498). It is skipped because on the GameCube it is not headed for
// main memory at all: iSndLoadSounds streams it from the disc straight into
// ARAM, the audio memory the DSP reads and the CPU cannot address. Nothing on a
// host corresponds to that, so the layer has to be loaded here or not at all.
// xSTFindAsset on a SND returns NULL and always will -- reaching for it is the
// first thing anyone tries, so it is worth saying plainly.
//
// What the packer does keep is the table of contents, which is the other half
// of what is needed: xST_xAssetID_HIPFullPath names the package an asset lives
// in, and xSTGetAssetInfoInHxP gives its sector and offset within that package.
// Those two are exactly what src/SB/Core/gc/iSnd.cpp uses to find the same
// bytes on the disc, so this is retail's route to the data, ending in main
// memory rather than in ARAM.
//
// Reading is LAZY rather than eager. A scene's sound layer is not small -- 22 MB
// for hb02, 52 MB for hb01, and 170 MB for the menu, whose layer is mostly
// music and cutscene dialogue -- and loading all of it at scene load would cost
// a long stall for audio most of which never plays. So a sound is read the
// first time it is asked for and kept, which puts a single file read on the
// first play of each distinct sound: a few hundred KB from a warm page cache,
// and thereafter nothing.
//
// The cache is capped and evicts by least-recently-used, so a long session that
// wanders through the whole game does not accumulate all 859 MB of it. An entry
// with a voice playing from it is pinned and cannot be evicted; that is what
// the refcount is for, and it is why iSndHost.h can promise a backend that the
// pointer it was handed stays alive.

#define ISNDDATA_MAX_ENTRIES 1024
#define ISNDDATA_DEFAULT_BUDGET_MB 192

struct snd_entry
{
    U32 aid;
    U32 bytes;
    void* data;
    S32 refs;
    U64 stamp;
};

static snd_entry sEntries[ISNDDATA_MAX_ENTRIES];
static U32 sEntryCount;
static U64 sTotalBytes;
static U64 sStampCounter;
static U64 sBudgetBytes;
static bool sBudgetRead;

// Reported once, so a build that cannot find its sounds says so and a build
// that can does not chatter.
static S32 sReadFailures;
static bool sSaidUnreadable;

static U64 iBudget()
{
    if (!sBudgetRead)
    {
        sBudgetRead = true;
        sBudgetBytes = (U64)ISNDDATA_DEFAULT_BUDGET_MB * 1024 * 1024;

        const char* env = getenv("BFBB_SNDCACHE");
        if (env != NULL)
        {
            int mb = atoi(env);
            if (mb > 0)
            {
                sBudgetBytes = (U64)mb * 1024 * 1024;
            }
        }
    }

    return sBudgetBytes;
}

static snd_entry* iFindEntry(U32 aid)
{
    for (U32 i = 0; i < sEntryCount; i++)
    {
        if (sEntries[i].aid == aid)
        {
            return &sEntries[i];
        }
    }

    return NULL;
}

static void iDropEntry(U32 index)
{
    free(sEntries[index].data);
    sTotalBytes -= sEntries[index].bytes;

    // Order does not matter; the array is searched linearly.
    sEntries[index] = sEntries[sEntryCount - 1];
    sEntryCount--;
}

// Make room for `wanted` more bytes by dropping the least recently used entries
// that no voice is playing from. Returns false if it could not -- which means
// everything in the cache is pinned, and the caller adds anyway rather than
// refusing to play a sound over a memory target.
static bool iMakeRoom(U64 wanted)
{
    U64 budget = iBudget();

    while (sEntryCount > 0 && sTotalBytes + wanted > budget)
    {
        U32 victim = ISNDDATA_MAX_ENTRIES;
        U64 oldest = 0;

        for (U32 i = 0; i < sEntryCount; i++)
        {
            if (sEntries[i].refs > 0)
            {
                continue;
            }

            if (victim == ISNDDATA_MAX_ENTRIES || sEntries[i].stamp < oldest)
            {
                victim = i;
                oldest = sEntries[i].stamp;
            }
        }

        if (victim == ISNDDATA_MAX_ENTRIES)
        {
            return false;
        }

        iDropEntry(victim);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Xbox ADPCM
//
// Most of the game's audio is 16-bit PCM, but 21 assets are not: the menu's
// music tracks are WAVE_FORMAT_XBOX_ADPCM, format tag 0x69, in fixed 36-byte
// mono blocks. That format is IMA ADPCM with the block size pinned, so this is
// the ordinary IMA decoder -- the step table, the four-bit index table, and a
// per-block reseed from a stored predictor and step index.
//
// **Sixty-four samples per block.** This is where Xbox ADPCM parts company
// with IMA ADPCM in a WAV, which would give sixty-five: the block's four-byte
// header is decoder state only -- a predictor and a step index to start from --
// and is not itself an output sample. Only the 32 payload bytes produce audio,
// two samples per byte. Each entry's own avgBytesPerSec agrees and is the
// easiest place to read it off: 44100 * 36 / 24806 is 64 exactly.
//
// Emitting the header's predictor as well stretches every track by 65/64. At
// 44.1 kHz that is playback at an effective 43421 Hz -- 1.6% slow, 27 cents
// flat -- with a spurious sample wedged in 689 times a second.
//
// Decoding happens once, on the way into the cache, so the mixer only ever
// sees 16-bit PCM. It costs about four times the asset's size in memory, which
// the cache's budget accounts for like any other entry.

static const S32 kImaStep[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,   23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,   80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,  279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,  963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024, 3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static const S32 kImaIndex[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

static S16 iClamp16(S32 v)
{
    if (v < -32768)
    {
        return -32768;
    }

    if (v > 32767)
    {
        return 32767;
    }

    return (S16)v;
}

// Decode `srcBytes` of mono IMA ADPCM into a fresh 16-bit PCM block. NULL if
// the block size makes no sense or the allocation fails.
static void* iDecodeAdpcm(const U8* src, U32 srcBytes, U32 blockAlign, U32* outBytes)
{
    if (blockAlign < 5 || srcBytes < 5)
    {
        return NULL;
    }

    U32 blocks = (srcBytes + blockAlign - 1) / blockAlign;
    U32 maxSamples = blocks * ((blockAlign - 4) * 2);

    S16* out = (S16*)malloc((size_t)maxSamples * sizeof(S16));
    if (out == NULL)
    {
        return NULL;
    }

    U32 written = 0;

    for (U32 b = 0; b < blocks; b++)
    {
        U32 off = b * blockAlign;
        U32 avail = srcBytes - off;
        if (avail > blockAlign)
        {
            avail = blockAlign;
        }

        // A trailing fragment too short to hold a header is not a block.
        if (avail < 5)
        {
            break;
        }

        const U8* blk = src + off;

        S32 pred = (S16)((U16)blk[0] | ((U16)blk[1] << 8));
        S32 index = blk[2];
        if (index > 88)
        {
            index = 88;
        }

        for (U32 i = 4; i < avail; i++)
        {
            U8 byte = blk[i];

            // Low nibble first, as IMA ADPCM in a WAV always is.
            for (S32 half = 0; half < 2; half++)
            {
                S32 nibble = half == 0 ? (byte & 0xf) : (byte >> 4);
                S32 step = kImaStep[index];

                S32 diff = step >> 3;
                if (nibble & 1)
                {
                    diff += step >> 2;
                }
                if (nibble & 2)
                {
                    diff += step >> 1;
                }
                if (nibble & 4)
                {
                    diff += step;
                }

                pred = (nibble & 8) ? pred - diff : pred + diff;
                out[written++] = iClamp16(pred);
                pred = out[written - 1];

                index += kImaIndex[nibble];
                if (index < 0)
                {
                    index = 0;
                }
                else if (index > 88)
                {
                    index = 88;
                }
            }
        }
    }

    *outBytes = written * sizeof(S16);
    return out;
}

// Read one asset's bytes out of the package that holds it. NULL if the asset is
// not in any loaded package, or the package will not open, or the read is
// short.
static void* iReadAsset(U32 aid, U32* out_bytes)
{
    char* path = xST_xAssetID_HIPFullPath(aid);
    if (path == NULL)
    {
        return NULL;
    }

    st_PKR_ASSET_TOCINFO info;
    if (!xSTGetAssetInfoInHxP(aid, &info, xStrHash(path)))
    {
        return NULL;
    }

    if (info.size == 0)
    {
        return NULL;
    }

    tag_xFile file = {};
    if (iFileOpen(path, 0, &file) != 0)
    {
        return NULL;
    }

    // The TOC stores a byte offset, which PKR_GetAssetInfo splits into a sector
    // and a remainder against the package's base sector (xpkrsvc.cpp:891).
    // Putting it back together is the same arithmetic src/SB/Core/gc/iSnd.cpp
    // does, with the same 32-byte sector PKRStartup sets. iFileGetInfo reports
    // a base of 0 on a host -- there are no disc sectors to be relative to --
    // but it is subtracted rather than assumed, so this stays correct if that
    // ever changes.
    U32 base = 0;
    iFileGetInfo(&file, &base, NULL);

    U32 offset = info.plus_offset + ((info.sector - base) << 5);

    void* mem = NULL;

    if (iFileSeek(&file, (S32)offset, IFILE_SEEK_SET) >= 0)
    {
        mem = malloc(info.size);
        if (mem != NULL)
        {
            U32 got = iFileRead(&file, mem, info.size);
            if (got != info.size)
            {
                free(mem);
                mem = NULL;
            }
        }
    }

    iFileClose(&file);

    if (mem != NULL)
    {
        *out_bytes = info.size;
    }

    return mem;
}

#define ISND_FORMAT_PCM 1
#define ISND_FORMAT_XBOX_ADPCM 0x69

// Turn what was read off disc into the 16-bit PCM the cache promises. Takes
// ownership of `raw` either way: it is returned unchanged for an asset that was
// already PCM, and freed once decoded for one that was not.
static void* iDecodeToPcm(void* raw, U32 rawBytes, const iSndDataFormat* fmt, U32* outBytes)
{
    *outBytes = rawBytes;

    if (fmt == NULL || fmt->format_tag == ISND_FORMAT_PCM || fmt->format_tag == 0)
    {
        return raw;
    }

    if (fmt->format_tag == ISND_FORMAT_XBOX_ADPCM && fmt->channels <= 1)
    {
        U32 got = 0;
        void* pcm = iDecodeAdpcm((const U8*)raw, rawBytes, fmt->block_align, &got);
        free(raw);

        if (pcm == NULL)
        {
            return NULL;
        }

        *outBytes = got;
        return pcm;
    }

    // Stereo ADPCM interleaves a block per channel and nothing in the retail
    // asset set is encoded that way, so the arm for it is absent rather than
    // guessed at. Anything else is a format this port has never seen. Either
    // way, refusing is right: playing it as PCM would be loud noise.
    static bool said = false;
    if (!said)
    {
        said = true;
        printf("iSndData: sound format tag %u (%u channels) is not decodable here; those sounds "
               "are silent\n",
               fmt->format_tag, fmt->channels);
        fflush(stdout);
    }

    free(raw);
    return NULL;
}

const void* iSndDataAcquire(U32 assetID, const iSndDataFormat* fmt, U32* bytes)
{
    if (bytes != NULL)
    {
        *bytes = 0;
    }

    if (assetID == 0)
    {
        return NULL;
    }

    snd_entry* e = iFindEntry(assetID);

    if (e == NULL)
    {
        if (sEntryCount >= ISNDDATA_MAX_ENTRIES)
        {
            // The table is full rather than the budget being spent, so force a
            // drop regardless of how little is cached.
            if (!iMakeRoom((U64)-1))
            {
                return NULL;
            }
        }

        U32 got = 0;
        void* mem = iReadAsset(assetID, &got);

        if (mem != NULL)
        {
            mem = iDecodeToPcm(mem, got, fmt, &got);
        }

        if (mem == NULL)
        {
            sReadFailures++;
            if (!sSaidUnreadable)
            {
                sSaidUnreadable = true;
                printf("iSndData: could not read sound asset %08x; it will play silently\n",
                       assetID);
                printf("iSndData:   (further unreadable sounds are silent about it too)\n");
                fflush(stdout);
            }
            return NULL;
        }

        iMakeRoom(got);

        e = &sEntries[sEntryCount++];
        e->aid = assetID;
        e->bytes = got;
        e->data = mem;
        e->refs = 0;
        sTotalBytes += got;
    }

    e->refs++;
    e->stamp = ++sStampCounter;

    if (bytes != NULL)
    {
        *bytes = e->bytes;
    }

    return e->data;
}

void iSndDataRelease(U32 assetID)
{
    snd_entry* e = iFindEntry(assetID);

    if (e != NULL && e->refs > 0)
    {
        e->refs--;
    }
}

void iSndDataReset()
{
    for (U32 i = 0; i < sEntryCount; i++)
    {
        free(sEntries[i].data);
    }

    memset(sEntries, 0, sizeof(sEntries));
    sEntryCount = 0;
    sTotalBytes = 0;
    sStampCounter = 0;
    sReadFailures = 0;
    sSaidUnreadable = false;
}

void iSndDataStats(U32* entries, U32* bytes, U32* pinned)
{
    if (entries != NULL)
    {
        *entries = sEntryCount;
    }

    if (bytes != NULL)
    {
        *bytes = (U32)sTotalBytes;
    }

    if (pinned != NULL)
    {
        U32 n = 0;
        for (U32 i = 0; i < sEntryCount; i++)
        {
            if (sEntries[i].refs > 0)
            {
                n++;
            }
        }
        *pinned = n;
    }
}
