#include "iGCSwap.h"
#include "iSndDsp.h"

#include <string.h>

// Only <types.h>, the C library and iSndDsp.cpp, so tools/gcswap/check.py can
// compile this file on its own and run it over a whole disc.

static inline void gcsSwap16(U8* p)
{
    U8 t = p[0];
    p[0] = p[1];
    p[1] = t;
}

static inline void gcsSwap32(U8* p)
{
    U8 t = p[0];
    p[0] = p[3];
    p[3] = t;
    t = p[1];
    p[1] = p[2];
    p[2] = t;
}

static inline void gcsSwap64(U8* p)
{
    for (S32 i = 0; i < 4; i++)
    {
        U8 t = p[i];
        p[i] = p[7 - i];
        p[7 - i] = t;
    }
}

// Little-endian reads, for count fields a step has already swapped.
static inline U32 gcsRead8(const U8* p)
{
    return p[0];
}

static inline U32 gcsRead16(const U8* p)
{
    return (U32)p[0] | ((U32)p[1] << 8);
}

static inline U32 gcsRead32(const U8* p)
{
    return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24);
}

// A big-endian read, for a field not swapped yet.
static inline U32 gcsReadBE32(const U8* p)
{
    return ((U32)p[0] << 24) | ((U32)p[1] << 16) | ((U32)p[2] << 8) | (U32)p[3];
}

// Fields the GameCube packer swapped in some assets and not in others; see
// EITHER in tools/gcswap/spec.py. Each keeps whichever byte order reads as a
// plausible value, and swaps when both do. Plausible for a float is zero or a
// magnitude between 2^-20 and 2^20: the fields are timer ranges and heights.
static bool gcsPlausibleFloat(U32 bits)
{
    U32 exponent = (bits >> 23) & 0xFF;
    return (bits & 0x7FFFFFFF) == 0 || (exponent >= 127 - 20 && exponent <= 127 + 20);
}

static inline void gcsSwap32EitherFloat(U8* p)
{
    U32 asIs = gcsRead32(p);
    if (!gcsPlausibleFloat(gcsReadBE32(p)) && gcsPlausibleFloat(asIs))
    {
        return;
    }
    gcsSwap32(p);
}

static inline void gcsSwap32EitherInt(U8* p)
{
    if (gcsReadBE32(p) > 0xFFFF && gcsRead32(p) <= 0xFFFF)
    {
        return;
    }
    gcsSwap32(p);
}

// An xBaseAsset's links: `linkCount` xLinkAssets at the very end of the
// asset. linkCount is a byte, so it reads the same either way round. Sets *end
// to where the links begin -- the end of the type's own data.
static void gcs_xLinkAsset(U8* p);

// iGCSwapAsset's onLink, for the asset being swapped.
static void (*sOnLink)(void* link);

static void gcsSwapLink(U8* p)
{
    gcs_xLinkAsset(p);
    if (sOnLink != NULL)
    {
        sOnLink(p);
    }
}

static bool gcsSwapLinks(U8* p, U32 size, U32* end)
{
    const U32 kLinkSize = 0x20;

    if (size < 8)
    {
        return false;
    }

    U32 links = p[5] * kLinkSize;
    if (links > size)
    {
        return false;
    }

    *end = size - links;
    for (U32 at = *end; at < size; at += kLinkSize)
    {
        gcsSwapLink(p + at);
    }
    return true;
}

// The hand-written rules the spec names; defined below the generated table,
// whose struct swaps they use.
static S32 iGCSwapAnim(U8* p, U32 size);
static S32 iGCSwapDisco(U8* p, U32 size);
static S32 iGCSwapModelInfo(U8* p, U32 size);
static S32 iGCSwapCutsceneToc(U8* p, U32 size);
static S32 iGCSwapJaw(U8* p, U32 size);
static S32 iGCSwapSoundTable(U8* p, U32 size);
static S32 iGCSwapJspInfo(U8* p, U32 size);
static S32 iGCSwapCredits(U8* p, U32 size);
static S32 iGCSwapShrapnel(U8* p, U32 size);
static S32 iGCSwapPlayer(U8* p, U32 size);

#include "iGCSwapTable.inc"

// ANIM, an SKB1 skeletal animation, laid out as iAnimEvalSKB reads it: the
// iAnimSKBHeader -- Magic, Flags, BoneCount and TimeCount (16-bit), KeyCount,
// Scale[3] -- then KeyCount iAnimSKBKeys of eight 16-bit fields each, then
// TimeCount float times, then 16-bit key offsets to the end.
static S32 iGCSwapAnim(U8* p, U32 size)
{
    const U32 kHeader = 0x1C;
    const U32 kKey = 0x10;

    if (size < kHeader)
    {
        return 0;
    }
    gcsSwap32(p + 0x0);
    gcsSwap32(p + 0x4);
    gcsSwap16(p + 0x8);
    gcsSwap16(p + 0xA);
    gcsSwap32(p + 0xC);
    gcsSwap32(p + 0x10);
    gcsSwap32(p + 0x14);
    gcsSwap32(p + 0x18);

    U32 keys = gcsRead32(p + 0xC);
    U32 times = gcsRead16(p + 0xA);
    if (keys > (size - kHeader) / kKey || times > (size - kHeader - keys * kKey) / 4)
    {
        return 0;
    }

    U32 c = kHeader;
    for (U32 end = c + keys * kKey; c < end; c += 2)
    {
        gcsSwap16(p + c);
    }
    for (U32 end = c + times * 4; c < end; c += 4)
    {
        gcsSwap32(p + c);
    }
    for (; c + 2 <= size; c += 2)
    {
        gcsSwap16(p + c);
    }
    return 1;
}

// DSCO, z_disco_floor. Unlike every other base asset its links come straight
// after the header -- zDiscoFloor.cpp takes `&asset + 1` -- and the data after
// them is addressed by offsets from `asset.flags`, byte 8: the three prefix
// strings, a table of `states_size` u32 offsets at `states_offset`, and the
// state masks, which are bytes.
static S32 iGCSwapDisco(U8* p, U32 size)
{
    const U32 kHeader = 0x2C;
    const U32 kDataBase = 0x8;

    if (size < kHeader)
    {
        return 0;
    }
    gcs_z_disco_floor_asset(p);

    U32 links = p[5] * 0x20;
    if (kHeader + links > size)
    {
        return 0;
    }
    for (U32 at = kHeader; at < kHeader + links; at += 0x20)
    {
        gcsSwapLink(p + at);
    }

    U32 table = kDataBase + gcsRead32(p + 0x24);
    U32 count = gcsRead32(p + 0x28);
    if (table > size || count > (size - table) / 4)
    {
        return 0;
    }
    for (U32 i = 0; i < count; i++)
    {
        gcsSwap32(p + table + i * 4);
    }
    return 1;
}

// MINF, xModelAssetInfo: the header, NumModelInst xModelAssetInsts, then the
// parameters zParamGetString walks -- each a u32 hash, a WordLength byte, and
// text filling WordLength * 4 + 4 more bytes.
static S32 iGCSwapModelInfo(U8* p, U32 size)
{
    const U32 kInfo = 0x14;
    const U32 kInst = 0x38;

    if (size < kInfo)
    {
        return 0;
    }
    gcs_xModelAssetInfo(p);

    U32 n = gcsRead32(p + 0x4);
    if (n > (size - kInfo) / kInst)
    {
        return 0;
    }
    U32 c = kInfo;
    for (U32 i = 0; i < n; i++, c += kInst)
    {
        gcs_xModelAssetInst(p + c);
    }

    while (c + 8 <= size)
    {
        gcsSwap32(p + c);
        c += p[c + 4] * 4 + 8;
    }
    return 1;
}

// Every aligned u32 in [from, to).
static void gcsSwapWords(U8* p, U32 from, U32 to)
{
    for (U32 c = from; c + 4 <= to; c += 4)
    {
        gcsSwap32(p + c);
    }
}

// PLYR: an xEntAsset, its links, then the light kit id. The links are not at
// the end as in other xBase assets: zEntPlayerInit reads the light kit id
// after them. Retail's PLYRs have no links; mods' do.
static S32 iGCSwapPlayer(U8* p, U32 size)
{
    const U32 kEnt = 0x54;

    U32 links = kEnt + p[5] * 0x20;
    if (size < links)
    {
        return 0;
    }
    gcs_xEntAsset(p);
    for (U32 at = kEnt; at < links; at += 0x20)
    {
        gcsSwapLink(p + at);
    }
    gcsSwapWords(p, links, size);
    return 1;
}

// CTOC, the cutscene table xCutscene_Init takes: a count, then that many
// cutscene headers, each HeaderSize bytes. A header is an xCutsceneInfo --
// twelve u32s and the two 16-character sound names -- then the xCutsceneData
// list, the time chunk offsets, the visibility words and the xCutsceneBreaks,
// all 32-bit.
static S32 iGCSwapCutsceneToc(U8* p, U32 size)
{
    const U32 kInfoWords = 0x30;
    const U32 kInfo = 0x50;

    if (size < 4)
    {
        return 0;
    }
    gcsSwap32(p);

    U32 n = gcsRead32(p);
    U32 c = 4;
    for (U32 i = 0; i < n; i++)
    {
        if (c > size || size - c < kInfo)
        {
            return 0;
        }
        gcsSwapWords(p, c, c + kInfoWords);

        U32 header = gcsRead32(p + c + 0x1C);
        if (header < kInfo || header > size - c)
        {
            return 0;
        }
        gcsSwapWords(p, c + kInfo, c + header);
        c += header;
    }
    return 1;
}

// JAW, the lip-sync table xJaw_FindData walks: a count, then that many
// xJawDataTables of three u32s. The curves after them are left alone: each
// starts with a count the GameCube disc already ships little-endian, and
// xJaw_EvalData turns one around itself when it reads as over 0xFFFF.
static S32 iGCSwapJaw(U8* p, U32 size)
{
    if (size < 4)
    {
        return 0;
    }
    gcsSwap32(p);

    U32 n = gcsRead32(p);
    if (n > (size - 4) / 12)
    {
        return 0;
    }
    gcsSwapWords(p, 4, 4 + n * 12);
    return 1;
}

// A cutscene's time chunk, laid out as xCutscene.cpp reads it: an
// xCutsceneTime, then NumData records of a 16-byte xCutsceneData header and a
// payload padded to 16. The payloads:
//
//   ANIMATION    an xCutsceneAnimHdr, then an SKB1 animation, as ANIM
//   CAMERA       a key count, then zFlyKeys: all 32-bit
//   JDELTAANIM   the four characters JDTA, then counts and floats
//   SOUND        DSP ADPCM, which the sound layer decodes; left alone
bool iGCSwapCutsceneTime(void* chunk, U32 size)
{
    const U32 kTime = 0x10;
    const U32 kRecord = 0x10;
    const U32 kAnimHdr = 0x10;
    U8* p = (U8*)chunk;

    if (size < kTime)
    {
        return false;
    }
    gcsSwapWords(p, 0, kTime);

    U32 n = gcsRead32(p + 0x8);
    U32 c = kTime;
    for (U32 i = 0; i < n; i++)
    {
        if (c > size || size - c < kRecord)
        {
            return false;
        }
        gcsSwapWords(p, c, c + kRecord);

        U32 type = gcsRead32(p + c);
        U32 len = gcsRead32(p + c + 0x8);
        U32 body = c + kRecord;
        if (len > size - body)
        {
            return false;
        }

        switch (type)
        {
        case 2:
            if (len >= kAnimHdr)
            {
                gcsSwapWords(p, body, body + kAnimHdr);
                iGCSwapAnim(p + body + kAnimHdr, len - kAnimHdr);
            }
            break;
        case 3:
            gcsSwapWords(p, body, body + len);
            break;
        case 7:
            gcsSwapWords(p, body + 4, body + len);
            break;
        default:
            break;
        }

        c = body + ((len + 15) & ~15u);
    }
    return true;
}

// A cutscene's JDELTAMODEL, as JDeltaEval reads it: the four characters JDTM
// and a count, then JDeltaTargets, each skipSize bytes long -- skipSize,
// numRuns, numVerts and scale, numRuns JDeltaRuns, and at the next 16 bytes
// numVerts positions: three S16s each when scale is nonzero, three floats
// otherwise.
bool iGCSwapCutsceneDeltaModel(void* data, U32 size)
{
    const U32 kHeader = 0x8;
    const U32 kTarget = 0x10;
    U8* p = (U8*)data;

    if (size < kHeader)
    {
        return false;
    }
    gcsSwap32(p + 0x4);

    U32 c = kHeader;
    while (c + kTarget <= size)
    {
        gcsSwapWords(p, c, c + kTarget);

        U32 skip = gcsRead32(p + c);
        U32 runs = gcsRead32(p + c + 0x4);
        U32 verts = gcsRead32(p + c + 0x8);
        bool packed = gcsRead32(p + c + 0xC) != 0;

        U32 r = c + kTarget;
        if (runs > (size - r) / 8)
        {
            return false;
        }
        gcsSwapWords(p, r, r + runs * 8);

        U32 v = (r + runs * 8 + 15) & ~15u;
        U32 vsize = packed ? 6 : 12;
        if (v > size || verts > (size - v) / vsize)
        {
            return false;
        }
        if (packed)
        {
            for (U32 k = 0; k < verts * 3; k++)
            {
                gcsSwap16(p + v + k * 2);
            }
        }
        else
        {
            gcsSwapWords(p, v, v + verts * 12);
        }

        if (skip == 0)
        {
            break;
        }
        c += skip;
    }
    return true;
}

// SNDI, the GameCube sound table, as src/SB/Core/gc/iSnd.cpp reads it: num_sfx,
// total_size, num_streams and num_cutscene, then one entry per sound -- its
// 0x60-byte DSPADPCM header and its asset id. total_size is scratch the
// GameCube fills in at load; on the disc it is 0xCDCDCDCD. It is stamped with
// IGCSWAP_SNDI_GAMECUBE, which no Xbox table can hold in that word.
static S32 iGCSwapSoundTable(U8* p, U32 size)
{
    const U32 kHeader = 0x10;
    const U32 kEntry = 0x64;

    if (size < kHeader)
    {
        return 0;
    }
    for (U32 c = 0; c < kHeader; c += 4)
    {
        gcsSwap32(p + c);
    }

    U32 n = gcsRead32(p + 0x0) + gcsRead32(p + 0x8) + gcsRead32(p + 0xC);
    if (n > (size - kHeader) / kEntry)
    {
        return 0;
    }

    U32 tag = IGCSWAP_SNDI_GAMECUBE;
    memcpy(p + 0x4, &tag, 4);

    for (U32 i = 0; i < n; i++)
    {
        U8* e = p + kHeader + i * kEntry;
        iSndDspSwapHeader(e);
        gcsSwap32(e + ISNDDSP_HEADER_SIZE);
    }
    return 1;
}

// JSP. Most are RenderWare clump streams, little-endian everywhere, and are
// left alone. The JSPINFO one is a run of blocks xJSP_MultiStreamRead walks,
// each behind a 12-byte little-endian mark -- type, length, library version --
// whose bodies the GameCube ships big-endian:
//
//   0xBEEF01  the collision tree: magic, branch count, triangle count, the
//             branch nodes (four 32-bit fields), then 8-byte triangles of a
//             U16 atomIndex, U16 meshVertIndex, two bytes and a U16 matIndex
//   0xBEEF02  an xJSPHeader -- four characters, then 32-bit fields -- and its
//             xJSPNodeInfos
//   0xBEEF03  a count and that many strip vertices (GameCube only)
static S32 iGCSwapJspInfo(U8* p, U32 size)
{
    const U32 kMark = 12;
    const U32 kTree = 12;
    const U32 kBranch = 16;
    const U32 kTriangle = 8;

    if (size < kMark || gcsRead32(p) != 0xBEEF01)
    {
        return 1;
    }

    U32 c = 0;
    while (c + kMark <= size)
    {
        U32 type = gcsRead32(p + c);
        U32 len = gcsRead32(p + c + 4);
        U32 body = c + kMark;
        if (len > size - body)
        {
            return 0;
        }

        switch (type)
        {
        case 0xBEEF01:
        {
            if (len < kTree)
            {
                return 0;
            }
            gcsSwapWords(p, body, body + kTree);
            U32 branches = gcsRead32(p + body + 4);
            U32 tris = gcsRead32(p + body + 8);
            if (branches > (len - kTree) / kBranch ||
                tris > (len - kTree - branches * kBranch) / kTriangle)
            {
                return 0;
            }
            U32 t = body + kTree;
            gcsSwapWords(p, t, t + branches * kBranch);
            for (t += branches * kBranch; tris > 0; tris--, t += kTriangle)
            {
                gcsSwap16(p + t + 0);
                gcsSwap16(p + t + 2);
                gcsSwap16(p + t + 6);
            }
            break;
        }
        case 0xBEEF02:
            gcsSwapWords(p, body + 4, body + len);
            break;
        case 0xBEEF03:
            gcsSwapWords(p, body, body + len);
            break;
        default:
            break;
        }

        c = body + len;
    }
    return 1;
}

// CRDT, the credits, as zAssetTypes.cpp's Credits_Read walks them: an
// xCMheader, then blocks to its total_size -- an xCMcredits, num_presets
// xCMpresets, and hunks to the block's credits_size. A preset is two U16s and
// then 32-bit fields; the packer wrote each textbox's iColor_tag as one u32,
// so it turns around with them. A hunk is six 32-bit fields and then its text.
static S32 iGCSwapCredits(U8* p, U32 size)
{
    const U32 kHeader = 0x18;
    const U32 kCredits = 0x38;
    const U32 kPreset = 0x4C;
    const U32 kHunk = 0x18;

    if (size < kHeader)
    {
        return 0;
    }
    gcsSwapWords(p, 0, kHeader);

    U32 total = gcsRead32(p + 0x14);
    if (total > size)
    {
        return 0;
    }

    U32 c = kHeader;
    while (c < total)
    {
        if (total - c < kCredits)
        {
            return 0;
        }
        gcsSwapWords(p, c, c + kCredits);

        U32 block = c;
        U32 blockSize = gcsRead32(p + c);
        U32 presets = gcsRead32(p + c + 0x34);
        if (blockSize > total - block || presets > (blockSize - kCredits) / kPreset)
        {
            return 0;
        }

        c += kCredits;
        for (U32 i = 0; i < presets; i++, c += kPreset)
        {
            gcsSwap16(p + c);
            gcsSwap16(p + c + 2);
            gcsSwapWords(p, c + 4, c + kPreset);
        }

        while (c - block < blockSize)
        {
            if (block + blockSize - c < kHunk)
            {
                return 0;
            }
            gcsSwapWords(p, c, c + kHunk);

            U32 hunkSize = gcsRead32(p + c);
            if (hunkSize < kHunk || hunkSize > block + blockSize - c)
            {
                return 0;
            }
            c += hunkSize;
        }
    }
    return 1;
}

// SHRP, a zShrapnelAsset and fassetCount frag assets, each sized by its
// zFragType, all 32-bit fields. A projectile's gravity, its last field, is one
// the GameCube packer left little-endian in some assets (see EITHER in
// tools/gcswap/spec.py). A frag type with no known size ends the walk, and the
// rest is swapped as words.
static S32 iGCSwapShrapnel(U8* p, U32 size)
{
    const U32 kHeader = 0xC;
    const U32 kProjectile = 4;
    const U32 kGravity = 0x8C;
    // zFragParticleAsset, zFragProjectileAsset, zFragLightningAsset,
    // zFragSoundAsset and zFragShockwaveAsset, by zFragType from 3.
    static const U32 kSizes[] = { 0x1D4, 0x90, 0x68, 0x4C, 0x54 };

    if (size < kHeader)
    {
        return 0;
    }
    gcsSwapWords(p, 0, kHeader);

    U32 n = gcsRead32(p);
    U32 c = kHeader;
    for (U32 i = 0; i < n && c + 4 <= size; i++)
    {
        U32 type = gcsReadBE32(p + c);
        if (type < 3 || type - 3 >= sizeof(kSizes) / sizeof(kSizes[0]) ||
            kSizes[type - 3] > size - c)
        {
            break;
        }

        U32 end = c + kSizes[type - 3];
        if (type == kProjectile)
        {
            gcsSwapWords(p, c, c + kGravity);
            gcsSwap32EitherFloat(p + c + kGravity);
        }
        else
        {
            gcsSwapWords(p, c, end);
        }
        c = end;
    }

    gcsSwapWords(p, c, size);
    return 1;
}

iGCSwapResult iGCSwapAsset(U32 type, void* data, U32 size, void (*onLink)(void* link))
{
    sOnLink = onLink;
    S32 r = iGCSwapGenerated(type, (U8*)data, size);
    sOnLink = NULL;
    if (r < 0)
    {
        return IGCSWAP_UNKNOWN;
    }
    return r ? IGCSWAP_DONE : IGCSWAP_MALFORMED;
}

bool iGCSwapKnowsType(U32 type)
{
    return iGCSwapGeneratedKnows(type);
}
