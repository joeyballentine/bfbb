#include "iHipMerge.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The container: chunks of a four-character id, a size and that many payload
// bytes, padded to an even length. Ids and sizes are big-endian on every
// platform. Asset payloads are in the console's byte order: big-endian on the
// GameCube, little-endian on the Xbox.
//
//   HIPA (empty)
//   PACK   PVER, PFLG, PCNT (asset count, layer count, 3 unused), PCRT, PMOD, PLAT
//   DICT
//     ATOC AINF, then AHDR per asset: id, type, offset, size, pad, flags; ADBG
//     LTOC LINF, then LHDR per layer: type, count, asset ids; LDBG
//   STRM   DHDR, DPAK (the asset payloads)
//
// AHDR offsets are absolute. The packer reads a layer as one block from its
// first asset's offset (PKR_LoadStep_Async), so a layer's assets sit together
// in LHDR order, and each pad is the gap to the next asset.

namespace
{
    const U32 kHIPA = 0x48495041;
    const U32 kPACK = 0x5041434B;
    const U32 kPCNT = 0x50434E54;
    const U32 kPLAT = 0x504C4154;
    const U32 kDICT = 0x44494354;
    const U32 kATOC = 0x41544F43;
    const U32 kAINF = 0x41494E46;
    const U32 kAHDR = 0x41484452;
    const U32 kADBG = 0x41444247;
    const U32 kLTOC = 0x4C544F43;
    const U32 kLINF = 0x4C494E46;
    const U32 kLHDR = 0x4C484452;
    const U32 kLDBG = 0x4C444247;
    const U32 kSTRM = 0x5354524D;
    const U32 kDHDR = 0x44484452;
    const U32 kDPAK = 0x4450414B;

    const U32 kJAW = 0x4A415720;
    const U32 kSND = 0x534E4420;
    const U32 kSNDS = 0x534E4453;
    const U32 kCSN = 0x43534E20;

    // Every layer starts on this boundary, and the packer rounds a layer's size up
    // to it.
    const U32 kLayerAlign = 0x800;

    // Retail's filler between assets.
    const U8 kPadByte = 0x33;

    U32 readBE(const U8* p)
    {
        return ((U32)p[0] << 24) | ((U32)p[1] << 16) | ((U32)p[2] << 8) | p[3];
    }

    U32 readLE(const U8* p)
    {
        return ((U32)p[3] << 24) | ((U32)p[2] << 16) | ((U32)p[1] << 8) | p[0];
    }

    void writeBE(U8* p, U32 v)
    {
        p[0] = (U8)(v >> 24);
        p[1] = (U8)(v >> 16);
        p[2] = (U8)(v >> 8);
        p[3] = (U8)v;
    }

    void writeLE(U8* p, U32 v)
    {
        p[0] = (U8)v;
        p[1] = (U8)(v >> 8);
        p[2] = (U8)(v >> 16);
        p[3] = (U8)(v >> 24);
    }

    U32 alignUp(U32 v, U32 a)
    {
        return (v + a - 1) / a * a;
    }

    void fail(char* why, size_t whySize, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vsnprintf(why, whySize, format, args);
        va_end(args);
    }

    struct ChunkReader
    {
        const U8* at;
        const U8* end;
        bool bad;

        ChunkReader(const U8* body, size_t size) : at(body), end(body + size), bad(false)
        {
        }

        bool next(U32* id, const U8** body, U32* size)
        {
            if (end - at < 8)
            {
                return false;
            }

            *id = readBE(at);
            *size = readBE(at + 4);
            *body = at + 8;
            if (*size > (size_t)(end - *body))
            {
                bad = true;
                return false;
            }

            at = *body + *size;
            if ((*size & 1) && at < end)
            {
                at++;
            }
            return true;
        }
    };
} // namespace

// ---- sources ----------------------------------------------------------------

namespace
{
    enum SourceKind
    {
        kSourceFile,
        kSourceMemory,
        kSourceMerge
    };

    // A span of a merged package: bytes of its own, filler, or a span of one of
    // the two packages it was merged from.
    enum PieceKind
    {
        kPieceBytes,
        kPieceFill,
        kPieceBase,
        kPieceMod
    };

    struct Piece
    {
        U32 at;
        U32 size;
        PieceKind kind;
        U32 from;
        const U8* bytes;
    };
} // namespace

struct iHipSource
{
    SourceKind kind;
    U32 size;

    // kSourceFile
    FILE* fp;

    // kSourceMemory
    U8* data;

    // kSourceMerge. `blocks` are the merge's own bytes, which pieces point
    // into: its header and its merged tables.
    iHipSource* base;
    iHipSource* mod;
    Piece* pieces;
    S32 pieceCount;
    U8** blocks;
    S32 blockCount;
};

iHipSource* iHipSourceFile(const char* path)
{
    FILE* fp = fopen(path, "rb");
    if (fp == NULL)
    {
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long length = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    iHipSource* s = (iHipSource*)calloc(1, sizeof(iHipSource));
    if (s == NULL || length < 0)
    {
        free(s);
        fclose(fp);
        return NULL;
    }

    s->kind = kSourceFile;
    s->fp = fp;
    s->size = (U32)length;
    return s;
}

iHipSource* iHipSourceMemory(U8* data, U32 size)
{
    iHipSource* s = (iHipSource*)calloc(1, sizeof(iHipSource));
    if (s == NULL)
    {
        free(data);
        return NULL;
    }

    s->kind = kSourceMemory;
    s->data = data;
    s->size = size;
    return s;
}

U32 iHipSourceSize(const iHipSource* source)
{
    return source->size;
}

bool iHipSourceRead(iHipSource* s, U32 offset, void* buf, U32 size)
{
    U8* out = (U8*)buf;
    U32 got = 0;
    if (offset < s->size)
    {
        got = s->size - offset < size ? s->size - offset : size;
    }

    bool ok = true;
    switch (s->kind)
    {
    case kSourceFile:
        if (got > 0)
        {
            ok = fseek(s->fp, (long)offset, SEEK_SET) == 0 && fread(out, 1, got, s->fp) == got;
        }
        break;

    case kSourceMemory:
        memcpy(out, s->data + offset, got);
        break;

    case kSourceMerge:
    {
        // The first piece that ends past `offset`.
        S32 lo = 0;
        S32 hi = s->pieceCount;
        while (lo < hi)
        {
            S32 mid = (lo + hi) / 2;
            if (s->pieces[mid].at + s->pieces[mid].size <= offset)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }

        U32 done = 0;
        for (S32 i = lo; i < s->pieceCount && done < got && ok; i++)
        {
            const Piece& p = s->pieces[i];
            U32 into = offset + done - p.at;
            U32 n = p.size - into < got - done ? p.size - into : got - done;
            switch (p.kind)
            {
            case kPieceBytes:
                memcpy(out + done, p.bytes + into, n);
                break;
            case kPieceFill:
                memset(out + done, kPadByte, n);
                break;
            case kPieceBase:
                ok = iHipSourceRead(s->base, p.from + into, out + done, n);
                break;
            case kPieceMod:
                ok = iHipSourceRead(s->mod, p.from + into, out + done, n);
                break;
            }
            done += n;
        }
        break;
    }
    }

    memset(out + got, 0, size - got);
    return ok;
}

void iHipSourceClose(iHipSource* s)
{
    if (s == NULL)
    {
        return;
    }

    switch (s->kind)
    {
    case kSourceFile:
        fclose(s->fp);
        break;
    case kSourceMemory:
        free(s->data);
        break;
    case kSourceMerge:
        iHipSourceClose(s->base);
        iHipSourceClose(s->mod);
        free(s->pieces);
        for (S32 i = 0; i < s->blockCount; i++)
        {
            free(s->blocks[i]);
        }
        free(s->blocks);
        break;
    }
    free(s);
}

// ---- the merge ----------------------------------------------------------------

namespace
{
    // Where an asset's data is: in the base package, the mod's, or a buffer of
    // the merge's own.
    enum DataSource
    {
        kFromBase,
        kFromMod,
        kFromOwned
    };

    struct Asset
    {
        U32 id;
        U32 type;
        U32 flags;
        DataSource source;
        U32 from;
        U32 size;
        U8* owned;
        const U8* adbg;
        U32 adbgSize;
        bool placed;
        U32 offset;
        U32 pad;
    };

    struct Layer
    {
        U32 type;
        U32* ids;
        S32 count;
        const U8* ldbg;
        U32 ldbgSize;
    };

    struct Package
    {
        iHipSource* file;
        DataSource source;

        // The PACK and DICT payloads, read whole. ADBG and LDBG blocks point
        // into `dict`.
        U8* pack;
        U32 packSize;
        U8* dict;
        U32 dictSize;

        U32 ainf;
        U32 linf;
        U32 dhdr;
        char plat[3];
        bool bigEndian;

        Asset* assets;
        S32 assetCount;
        S32 assetRoom;

        Layer* layers;
        S32 layerCount;

        U32 read(const U8* p) const
        {
            return bigEndian ? readBE(p) : readLE(p);
        }

        void write(U8* p, U32 v) const
        {
            if (bigEndian)
            {
                writeBE(p, v);
            }
            else
            {
                writeLE(p, v);
            }
        }

        Asset* find(U32 id)
        {
            for (S32 i = 0; i < assetCount; i++)
            {
                if (assets[i].id == id)
                {
                    return &assets[i];
                }
            }
            return NULL;
        }

        Asset* firstOfType(U32 type)
        {
            for (S32 i = 0; i < assetCount; i++)
            {
                if (assets[i].type == type)
                {
                    return &assets[i];
                }
            }
            return NULL;
        }

        bool add(const Asset& a)
        {
            if (assetCount == assetRoom)
            {
                S32 room = assetRoom == 0 ? 64 : assetRoom * 2;
                void* grown = realloc(assets, (size_t)room * sizeof(Asset));
                if (grown == NULL)
                {
                    return false;
                }
                assets = (Asset*)grown;
                assetRoom = room;
            }
            assets[assetCount++] = a;
            return true;
        }
    };

    void freePackage(Package* pkg)
    {
        for (S32 i = 0; i < pkg->assetCount; i++)
        {
            free(pkg->assets[i].owned);
        }
        for (S32 i = 0; i < pkg->layerCount; i++)
        {
            free(pkg->layers[i].ids);
        }
        free(pkg->assets);
        free(pkg->layers);
        free(pkg->pack);
        free(pkg->dict);
        memset(pkg, 0, sizeof(*pkg));
    }

    // `size` bytes of `file` at `offset`, in a new buffer.
    U8* readSpan(iHipSource* file, U32 offset, U32 size)
    {
        U8* data = (U8*)malloc(size > 0 ? size : 1);
        if (data != NULL && !iHipSourceRead(file, offset, data, size))
        {
            free(data);
            data = NULL;
        }
        return data;
    }

    // An asset's data, read into a buffer of its own if it is not in one yet.
    const U8* assetData(Package* base, Package* mod, Asset* a)
    {
        if (a->owned == NULL)
        {
            iHipSource* file = a->source == kFromMod ? mod->file : base->file;
            a->owned = readSpan(file, a->from, a->size);
        }
        return a->owned;
    }

    bool parseAsset(Package* pkg, const U8* body, U32 size, char* why, size_t whySize)
    {
        if (size < 24)
        {
            fail(why, whySize, "an asset header is %u bytes", (unsigned)size);
            return false;
        }

        Asset a = {};
        a.id = readBE(body);
        a.type = readBE(body + 4);
        a.source = pkg->source;
        a.from = readBE(body + 8);
        a.size = readBE(body + 12);
        a.flags = readBE(body + 20);

        U32 fileSize = iHipSourceSize(pkg->file);
        if (a.from > fileSize || a.size > fileSize - a.from)
        {
            fail(why, whySize, "asset %08X lies outside the file", (unsigned)a.id);
            return false;
        }

        ChunkReader sub(body + 24, size - 24);
        U32 id;
        const U8* subBody;
        U32 subSize;
        while (sub.next(&id, &subBody, &subSize))
        {
            if (id == kADBG)
            {
                a.adbg = subBody;
                a.adbgSize = subSize;
            }
        }

        if (sub.bad)
        {
            fail(why, whySize, "asset %08X has a malformed debug block", (unsigned)a.id);
            return false;
        }

        if (!pkg->add(a))
        {
            fail(why, whySize, "out of memory");
            return false;
        }
        return true;
    }

    bool parseLayer(Package* pkg, Layer* layer, const U8* body, U32 size, char* why, size_t whySize)
    {
        if (size < 8)
        {
            fail(why, whySize, "a layer header is %u bytes", (unsigned)size);
            return false;
        }

        layer->type = readBE(body);
        U32 count = readBE(body + 4);
        if (count > (size - 8) / 4)
        {
            fail(why, whySize, "a layer lists more assets than its header holds");
            return false;
        }

        layer->count = (S32)count;
        layer->ids = (U32*)malloc((count > 0 ? count : 1) * sizeof(U32));
        if (layer->ids == NULL)
        {
            fail(why, whySize, "out of memory");
            return false;
        }

        for (U32 i = 0; i < count; i++)
        {
            layer->ids[i] = readBE(body + 8 + 4 * i);
            if (pkg->find(layer->ids[i]) == NULL)
            {
                fail(why, whySize, "a layer lists asset %08X, which is not in the package",
                     (unsigned)layer->ids[i]);
                return false;
            }
        }

        ChunkReader sub(body + 8 + 4 * count, size - 8 - 4 * count);
        U32 id;
        const U8* subBody;
        U32 subSize;
        while (sub.next(&id, &subBody, &subSize))
        {
            if (id == kLDBG)
            {
                layer->ldbg = subBody;
                layer->ldbgSize = subSize;
            }
        }

        if (sub.bad)
        {
            fail(why, whySize, "a layer has a malformed debug block");
            return false;
        }
        return true;
    }

    bool parseDict(Package* pkg, char* why, size_t whySize)
    {
        ChunkReader dict(pkg->dict, pkg->dictSize);
        U32 id;
        const U8* sectionBody;
        U32 sectionSize;
        bool sawAtoc = false;

        while (dict.next(&id, &sectionBody, &sectionSize))
        {
            if (id == kATOC)
            {
                sawAtoc = true;
                ChunkReader atoc(sectionBody, sectionSize);
                U32 cid;
                const U8* cbody;
                U32 csize;
                while (atoc.next(&cid, &cbody, &csize))
                {
                    if (cid == kAINF && csize >= 4)
                    {
                        pkg->ainf = readBE(cbody);
                    }
                    else if (cid == kAHDR && !parseAsset(pkg, cbody, csize, why, whySize))
                    {
                        return false;
                    }
                }
                if (atoc.bad)
                {
                    fail(why, whySize, "the asset table is malformed");
                    return false;
                }
            }
            else if (id == kLTOC)
            {
                // The asset table comes first in every package; the layers name
                // assets in it.
                if (!sawAtoc)
                {
                    fail(why, whySize, "the layer table comes before the asset table");
                    return false;
                }

                ChunkReader ltoc(sectionBody, sectionSize);
                U32 cid;
                const U8* cbody;
                U32 csize;
                S32 room = 0;
                while (ltoc.next(&cid, &cbody, &csize))
                {
                    if (cid == kLINF && csize >= 4)
                    {
                        pkg->linf = readBE(cbody);
                        continue;
                    }
                    if (cid != kLHDR)
                    {
                        continue;
                    }

                    if (pkg->layerCount == room)
                    {
                        room = room == 0 ? 16 : room * 2;
                        void* grown = realloc(pkg->layers, (size_t)room * sizeof(Layer));
                        if (grown == NULL)
                        {
                            fail(why, whySize, "out of memory");
                            return false;
                        }
                        pkg->layers = (Layer*)grown;
                    }

                    Layer* layer = &pkg->layers[pkg->layerCount++];
                    memset(layer, 0, sizeof(*layer));
                    if (!parseLayer(pkg, layer, cbody, csize, why, whySize))
                    {
                        return false;
                    }
                }
                if (ltoc.bad)
                {
                    fail(why, whySize, "the layer table is malformed");
                    return false;
                }
            }
        }

        if (dict.bad)
        {
            fail(why, whySize, "the dictionary is malformed");
            return false;
        }
        return true;
    }

    // Reads the package's header chunks. Asset data stays in the file.
    bool parsePackage(iHipSource* file, DataSource source, Package* pkg, char* why, size_t whySize)
    {
        memset(pkg, 0, sizeof(*pkg));
        pkg->file = file;
        pkg->source = source;

        const U32 fileSize = iHipSourceSize(file);
        U8 head[12];
        U32 pos = 0;
        bool sawPack = false;
        bool sawDict = false;

        while (pos + 8 <= fileSize)
        {
            if (!iHipSourceRead(file, pos, head, 8))
            {
                fail(why, whySize, "the file could not be read");
                return false;
            }

            const U32 id = readBE(head);
            const U32 size = readBE(head + 4);
            const U32 body = pos + 8;
            if (size > fileSize - body)
            {
                fail(why, whySize, "the package is malformed");
                return false;
            }

            if (pos == 0 && id != kHIPA)
            {
                fail(why, whySize, "not a package");
                return false;
            }

            if (id == kPACK || id == kDICT)
            {
                U8* payload = readSpan(file, body, size);
                if (payload == NULL)
                {
                    fail(why, whySize, "the file could not be read");
                    return false;
                }

                if (id == kPACK)
                {
                    sawPack = true;
                    free(pkg->pack);
                    pkg->pack = payload;
                    pkg->packSize = size;
                }
                else
                {
                    sawDict = true;
                    free(pkg->dict);
                    pkg->dict = payload;
                    pkg->dictSize = size;
                    if (!parseDict(pkg, why, whySize))
                    {
                        return false;
                    }
                }
            }
            else if (id == kSTRM && size >= 12)
            {
                if (!iHipSourceRead(file, body, head, 12))
                {
                    fail(why, whySize, "the file could not be read");
                    return false;
                }
                if (readBE(head) == kDHDR)
                {
                    pkg->dhdr = readBE(head + 8);
                }
            }

            pos = body + size + (size & 1);
        }

        if (!sawPack || !sawDict)
        {
            fail(why, whySize, "the package is malformed");
            return false;
        }

        ChunkReader pack(pkg->pack, pkg->packSize);
        U32 cid;
        const U8* cbody;
        U32 csize;
        while (pack.next(&cid, &cbody, &csize))
        {
            if (cid == kPLAT && csize >= 2)
            {
                pkg->plat[0] = (char)cbody[0];
                pkg->plat[1] = (char)cbody[1];
            }
            else if (cid == kPCNT && csize < 8)
            {
                fail(why, whySize, "the package counts are %u bytes", (unsigned)csize);
                return false;
            }
        }
        if (pack.bad)
        {
            fail(why, whySize, "the package header is malformed");
            return false;
        }

        pkg->bigEndian = pkg->plat[0] == 'G' && pkg->plat[1] == 'C';
        return true;
    }

    // A table asset: a header of 32-bit words, some of which count the entries of
    // the lists that follow it, back to back. Entries are fixed-size, and an
    // entry's key is one or two 32-bit words in it.
    struct TableFormat
    {
        U32 type;
        const char* plat;
        S32 headerWords;
        S32 lists;
        S32 countWord[3];
        U32 entrySize;
        S32 keys;
        U32 keyOffset[2];
    };

    // COLL: model, collision model, camera collision model (zCollGeom.cpp).
    // LODT: zLODTable, keyed by its base model. PIPT: xModelPipeInfo, keyed by
    // model and subobject bits. SHDW: model, shadow model, unknown.
    // SNDI holds the SND, SNDS and cutscene sound lists. On the Xbox the header is
    // the three counts and an entry is a WAVEFORMAT-style header with the sound's
    // ID at 24. On the GameCube the second header word is filler and an entry is
    // a 0x60-byte DSP header followed by the sound's ID.
    const TableFormat kTables[] = {
        { 0x434F4C4C, NULL, 1, 1, { 0 }, 12, 1, { 0 } },
        { 0x4C4F4454, NULL, 1, 1, { 0 }, 32, 1, { 0 } },
        { 0x50495054, NULL, 1, 1, { 0 }, 12, 2, { 0, 4 } },
        { 0x53484457, NULL, 1, 1, { 0 }, 12, 1, { 0 } },
        { 0x534E4449, "XB", 3, 3, { 0, 1, 2 }, 44, 1, { 24 } },
        { 0x534E4449, "GC", 4, 3, { 0, 2, 3 }, 100, 1, { 96 } },
    };

    bool isTableType(U32 type)
    {
        if (type == kJAW)
        {
            return true;
        }
        for (size_t i = 0; i < sizeof(kTables) / sizeof(kTables[0]); i++)
        {
            if (kTables[i].type == type)
            {
                return true;
            }
        }
        return false;
    }

    const TableFormat* tableFormat(U32 type, const char* plat)
    {
        for (size_t i = 0; i < sizeof(kTables) / sizeof(kTables[0]); i++)
        {
            const TableFormat* f = &kTables[i];
            if (f->type == type && (f->plat == NULL || strncmp(f->plat, plat, 2) == 0))
            {
                return f;
            }
        }
        return NULL;
    }

    bool sameKey(const TableFormat* f, const U8* a, const U8* b)
    {
        for (S32 k = 0; k < f->keys; k++)
        {
            if (memcmp(a + f->keyOffset[k], b + f->keyOffset[k], 4) != 0)
            {
                return false;
            }
        }
        return true;
    }

    // The entries of each list in a table, as pointers into its data.
    bool tableLists(const Package& pkg, const TableFormat* f, const U8* data, U32 size,
                    const U8** lists[3], U32 counts[3])
    {
        U32 header = (U32)f->headerWords * 4;
        if (size < header)
        {
            return false;
        }

        const U8* at = data + header;
        U32 left = size - header;
        for (S32 l = 0; l < f->lists; l++)
        {
            counts[l] = pkg.read(data + 4 * f->countWord[l]);
            if (counts[l] > left / f->entrySize)
            {
                return false;
            }

            lists[l] = (const U8**)malloc((counts[l] > 0 ? counts[l] : 1) * sizeof(const U8*));
            if (lists[l] == NULL)
            {
                return false;
            }
            for (U32 i = 0; i < counts[l]; i++)
            {
                lists[l][i] = at;
                at += f->entrySize;
            }
            left -= counts[l] * f->entrySize;
        }
        return true;
    }

    // Merges table `modData` into `base`'s data, which becomes a buffer of its own.
    bool mergeTable(const Package& pkg, const TableFormat* f, Asset* base, const U8* baseData,
                    const U8* modData, U32 modSize, U32 modId, char* why, size_t whySize)
    {
        const U8** baseLists[3] = {};
        const U8** modLists[3] = {};
        U32 baseCounts[3] = {};
        U32 modCounts[3] = {};
        const U8** merged[3] = {};
        U32 mergedCounts[3] = {};
        U8* out = NULL;
        U32 size = (U32)f->headerWords * 4;
        bool ok = false;

        if (!tableLists(pkg, f, baseData, base->size, baseLists, baseCounts) ||
            !tableLists(pkg, f, modData, modSize, modLists, modCounts))
        {
            fail(why, whySize, "table %08X or %08X is shorter than its counts say",
                 (unsigned)base->id, (unsigned)modId);
            goto done;
        }

        for (S32 l = 0; l < f->lists; l++)
        {
            merged[l] = (const U8**)malloc((baseCounts[l] + modCounts[l] + 1) * sizeof(const U8*));
            if (merged[l] == NULL)
            {
                fail(why, whySize, "out of memory");
                goto done;
            }
            memcpy(merged[l], baseLists[l], baseCounts[l] * sizeof(const U8*));
            mergedCounts[l] = baseCounts[l];

            for (U32 m = 0; m < modCounts[l]; m++)
            {
                U32 kept = 0;
                for (U32 i = 0; i < mergedCounts[l]; i++)
                {
                    if (!sameKey(f, merged[l][i], modLists[l][m]))
                    {
                        merged[l][kept++] = merged[l][i];
                    }
                }
                merged[l][kept++] = modLists[l][m];
                mergedCounts[l] = kept;
            }
            size += mergedCounts[l] * f->entrySize;
        }

        out = (U8*)malloc(size);
        if (out == NULL)
        {
            fail(why, whySize, "out of memory");
            goto done;
        }

        // The header's other words, like the GameCube SNDI's filler, are the
        // game's.
        memcpy(out, baseData, (size_t)f->headerWords * 4);
        {
            U8* at = out + f->headerWords * 4;
            for (S32 l = 0; l < f->lists; l++)
            {
                pkg.write(out + 4 * f->countWord[l], mergedCounts[l]);
                for (U32 i = 0; i < mergedCounts[l]; i++)
                {
                    memcpy(at, merged[l][i], f->entrySize);
                    at += f->entrySize;
                }
            }
        }

        free(base->owned);
        base->owned = out;
        base->source = kFromOwned;
        base->size = size;
        ok = true;

    done:
        for (S32 l = 0; l < 3; l++)
        {
            free(baseLists[l]);
            free(modLists[l]);
            free(merged[l]);
        }
        return ok;
    }

    // JAW: a count, then per entry the sound's ID and the offset and length of its
    // mouth data in the block after the table (xJaw.cpp).
    struct JawEntry
    {
        U32 sound;
        const U8* data;
        U32 length;
    };

    bool jawEntries(const Package& pkg, const U8* data, U32 size, JawEntry** out, U32* count)
    {
        if (size < 4)
        {
            return false;
        }

        U32 n = pkg.read(data);
        if (n > (size - 4) / 12)
        {
            return false;
        }

        const U8* raw = data + 4 + 12 * n;
        U32 rawSize = size - 4 - 12 * n;
        *out = (JawEntry*)malloc((n > 0 ? n : 1) * sizeof(JawEntry));
        if (*out == NULL)
        {
            return false;
        }
        for (U32 i = 0; i < n; i++)
        {
            const U8* e = data + 4 + 12 * i;
            U32 start = pkg.read(e + 4);
            U32 length = pkg.read(e + 8);
            if (start > rawSize || length > rawSize - start)
            {
                free(*out);
                *out = NULL;
                return false;
            }
            (*out)[i].sound = pkg.read(e);
            (*out)[i].data = raw + start;
            (*out)[i].length = length;
        }
        *count = n;
        return true;
    }

    bool mergeJaw(const Package& pkg, Asset* base, const U8* baseData, const U8* modData,
                  U32 modSize, U32 modId, char* why, size_t whySize)
    {
        JawEntry* baseEntries = NULL;
        JawEntry* modEntries = NULL;
        U32 baseCount = 0;
        U32 modCount = 0;

        if (!jawEntries(pkg, baseData, base->size, &baseEntries, &baseCount) ||
            !jawEntries(pkg, modData, modSize, &modEntries, &modCount))
        {
            free(baseEntries);
            fail(why, whySize, "jaw table %08X or %08X is malformed", (unsigned)base->id,
                 (unsigned)modId);
            return false;
        }

        JawEntry* merged = (JawEntry*)malloc((baseCount + modCount + 1) * sizeof(JawEntry));
        if (merged == NULL)
        {
            free(baseEntries);
            free(modEntries);
            fail(why, whySize, "out of memory");
            return false;
        }
        memcpy(merged, baseEntries, baseCount * sizeof(JawEntry));
        U32 count = baseCount;
        for (U32 m = 0; m < modCount; m++)
        {
            U32 kept = 0;
            for (U32 i = 0; i < count; i++)
            {
                if (merged[i].sound != modEntries[m].sound)
                {
                    merged[kept++] = merged[i];
                }
            }
            merged[kept++] = modEntries[m];
            count = kept;
        }

        U32 size = 4 + 12 * count;
        for (U32 i = 0; i < count; i++)
        {
            size += alignUp(merged[i].length, 4);
        }

        U8* out = (U8*)calloc(1, size);
        bool ok = out != NULL;
        if (ok)
        {
            pkg.write(out, count);
            U8* raw = out + 4 + 12 * count;
            U32 start = 0;
            for (U32 i = 0; i < count; i++)
            {
                U8* e = out + 4 + 12 * i;
                pkg.write(e, merged[i].sound);
                pkg.write(e + 4, start);
                pkg.write(e + 8, merged[i].length);
                memcpy(raw + start, merged[i].data, merged[i].length);
                start += alignUp(merged[i].length, 4);
            }

            free(base->owned);
            base->owned = out;
            base->source = kFromOwned;
            base->size = size;
        }
        else
        {
            fail(why, whySize, "out of memory");
        }

        free(merged);
        free(baseEntries);
        free(modEntries);
        return ok;
    }

    void dropFromLayers(Package* pkg, U32 id)
    {
        for (S32 l = 0; l < pkg->layerCount; l++)
        {
            Layer* layer = &pkg->layers[l];
            S32 kept = 0;
            for (S32 i = 0; i < layer->count; i++)
            {
                if (layer->ids[i] != id)
                {
                    layer->ids[kept++] = layer->ids[i];
                }
            }
            layer->count = kept;
        }
    }

    // Retail's layer order, by type: TEXTURE, BSP, JSPINFO, MODEL, ANIMATION,
    // DEFAULT, CUTSCENE, SRAM, SNDTOC. A type not in it goes last.
    S32 layerRank(U32 type)
    {
        static const U32 kOrder[] = { 1, 2, 10, 3, 4, 0, 8, 6, 7 };
        const S32 n = (S32)(sizeof(kOrder) / sizeof(kOrder[0]));
        for (S32 i = 0; i < n; i++)
        {
            if (kOrder[i] == type)
            {
                return i;
            }
        }
        return n;
    }

    bool mergeTables(Package* base, Package* mod, Asset* table, Asset* a, char* why, size_t whySize)
    {
        const U8* baseData = assetData(base, mod, table);
        U8* modData = readSpan(mod->file, a->from, a->size);
        if (baseData == NULL || modData == NULL)
        {
            free(modData);
            fail(why, whySize, "the file could not be read");
            return false;
        }

        // `baseData` is the table's own buffer, which the merge replaces.
        U8* oldOwned = table->owned;
        table->owned = NULL;

        bool ok;
        if (a->type == kJAW)
        {
            ok = mergeJaw(*base, table, baseData, modData, a->size, a->id, why, whySize);
        }
        else
        {
            const TableFormat* f = tableFormat(a->type, base->plat);
            if (f == NULL)
            {
                fail(why, whySize, "SNDI tables from the %.2s release cannot be merged",
                     base->plat);
                ok = false;
            }
            else
            {
                ok = mergeTable(*base, f, table, baseData, modData, a->size, a->id, why, whySize);
            }
        }

        if (ok)
        {
            free(oldOwned);
        }
        else
        {
            table->owned = oldOwned;
        }
        free(modData);
        return ok;
    }

    bool merge(Package* base, Package* mod, char* why, size_t whySize)
    {
        if (base->plat[0] != mod->plat[0] || base->plat[1] != mod->plat[1])
        {
            fail(why, whySize,
                 "the mod's package is from the %.2s release and the game's from the %.2s",
                 mod->plat, base->plat);
            return false;
        }

        for (S32 i = 0; i < mod->assetCount; i++)
        {
            Asset* a = &mod->assets[i];

            if (isTableType(a->type))
            {
                Asset* table = base->firstOfType(a->type);
                if (table != NULL)
                {
                    if (!mergeTables(base, mod, table, a, why, whySize))
                    {
                        return false;
                    }
                    dropFromLayers(mod, a->id);
                    continue;
                }
            }

            Asset* same = base->find(a->id);
            if (same != NULL)
            {
                free(same->owned);
                same->owned = NULL;
                same->type = a->type;
                same->flags = a->flags;
                same->adbg = a->adbg;
                same->adbgSize = a->adbgSize;
                same->source = kFromMod;
                same->from = a->from;
                same->size = a->size;
                dropFromLayers(mod, a->id);
                continue;
            }

            if (!base->add(*a))
            {
                fail(why, whySize, "out of memory");
                return false;
            }
        }

        S32 kept = 0;
        for (S32 l = 0; l < mod->layerCount; l++)
        {
            if (mod->layers[l].count > 0)
            {
                kept++;
            }
        }

        Layer* layers =
            (Layer*)realloc(base->layers, (size_t)(base->layerCount + kept + 1) * sizeof(Layer));
        if (layers == NULL)
        {
            fail(why, whySize, "out of memory");
            return false;
        }
        base->layers = layers;

        for (S32 l = 0; l < mod->layerCount; l++)
        {
            if (mod->layers[l].count > 0)
            {
                base->layers[base->layerCount++] = mod->layers[l];
                mod->layers[l].ids = NULL;
            }
        }

        // A stable insertion sort: layers of one type keep their order, the game's
        // ahead of the mod's.
        for (S32 i = 1; i < base->layerCount; i++)
        {
            Layer moving = base->layers[i];
            S32 rank = layerRank(moving.type);
            S32 j = i;
            while (j > 0 && layerRank(base->layers[j - 1].type) > rank)
            {
                base->layers[j] = base->layers[j - 1];
                j--;
            }
            base->layers[j] = moving;
        }

        return true;
    }

    struct Writer
    {
        U8* data;
        size_t size;
        size_t room;
        bool failed;

        void reserve(size_t more)
        {
            if (failed || size + more <= room)
            {
                return;
            }

            size_t grown = room == 0 ? 1 << 16 : room;
            while (grown < size + more)
            {
                grown *= 2;
            }

            void* p = realloc(data, grown);
            if (p == NULL)
            {
                failed = true;
                return;
            }
            data = (U8*)p;
            room = grown;
        }

        void bytes(const void* p, size_t n)
        {
            reserve(n);
            if (!failed && n > 0)
            {
                memcpy(data + size, p, n);
                size += n;
            }
        }

        void u32(U32 v)
        {
            U8 b[4];
            writeBE(b, v);
            bytes(b, 4);
        }

        // Starts a chunk; end() fills in its size.
        size_t begin(U32 id)
        {
            size_t at = size;
            u32(id);
            u32(0);
            return at;
        }

        void end(size_t at)
        {
            if (failed)
            {
                return;
            }
            size_t payload = size - at - 8;
            writeBE(data + at + 4, (U32)payload);
            if (payload & 1)
            {
                U8 zero = 0;
                bytes(&zero, 1);
            }
        }

        void chunk(U32 id, const U8* payload, U32 n)
        {
            size_t at = begin(id);
            bytes(payload, n);
            end(at);
        }
    };

    struct PieceList
    {
        Piece* pieces;
        S32 count;
        S32 room;
        U32 end;
        bool failed;

        void add(PieceKind kind, U32 size, U32 from, const U8* bytes)
        {
            if (size == 0 || failed)
            {
                return;
            }

            if (count == room)
            {
                S32 grown = room == 0 ? 256 : room * 2;
                void* p = realloc(pieces, (size_t)grown * sizeof(Piece));
                if (p == NULL)
                {
                    failed = true;
                    return;
                }
                pieces = (Piece*)p;
                room = grown;
            }

            Piece& piece = pieces[count++];
            piece.at = end;
            piece.size = size;
            piece.kind = kind;
            piece.from = from;
            piece.bytes = bytes;
            end += size;
        }

        void fillTo(U32 align)
        {
            add(kPieceFill, alignUp(end, align) - end, 0, NULL);
        }
    };

    // Where an asset's data has to start. Sounds and cutscenes keep the alignment
    // the retail packer gave them; everything else is 16-byte aligned.
    U32 assetAlign(const Package& pkg, U32 type)
    {
        if (type == kSND || type == kSNDS || type == kCSN)
        {
            return pkg.bigEndian ? 0x20 : kLayerAlign;
        }
        return 16;
    }

    void placeAsset(PieceList* list, Asset* a)
    {
        a->placed = true;
        a->offset = list->end;
        if (a->source == kFromOwned)
        {
            list->add(kPieceBytes, a->size, 0, a->owned);
        }
        else
        {
            list->add(a->source == kFromMod ? kPieceMod : kPieceBase, a->size, a->from, NULL);
        }
    }

    // Lays the merged package out: its header in `header`, and every span of it
    // in `list`, the header first.
    bool layOut(Package* pkg, Writer* header, PieceList* list, char* why, size_t whySize)
    {
        Writer& w = *header;

        w.chunk(kHIPA, NULL, 0);

        size_t pack = w.begin(kPACK);
        {
            ChunkReader r(pkg->pack, pkg->packSize);
            U32 id;
            const U8* body;
            U32 size;
            while (r.next(&id, &body, &size))
            {
                if (id == kPCNT)
                {
                    size_t at = w.begin(kPCNT);
                    w.u32((U32)pkg->assetCount);
                    w.u32((U32)pkg->layerCount);
                    w.bytes(body + 8, size - 8);
                    w.end(at);
                }
                else
                {
                    w.chunk(id, body, size);
                }
            }
        }
        w.end(pack);

        // The asset table in ID order, as the retail packer writes it.
        S32* order = (S32*)malloc((size_t)(pkg->assetCount + 1) * sizeof(S32));
        size_t* offsetAt = (size_t*)malloc((size_t)(pkg->assetCount + 1) * sizeof(size_t));
        if (order == NULL || offsetAt == NULL)
        {
            free(order);
            free(offsetAt);
            fail(why, whySize, "out of memory");
            return false;
        }

        for (S32 i = 0; i < pkg->assetCount; i++)
        {
            order[i] = i;
        }
        for (S32 i = 1; i < pkg->assetCount; i++)
        {
            S32 moving = order[i];
            S32 j = i;
            while (j > 0 && pkg->assets[order[j - 1]].id > pkg->assets[moving].id)
            {
                order[j] = order[j - 1];
                j--;
            }
            order[j] = moving;
        }

        size_t dict = w.begin(kDICT);
        size_t atoc = w.begin(kATOC);
        {
            U8 b[4];
            writeBE(b, pkg->ainf);
            w.chunk(kAINF, b, 4);
        }
        for (S32 k = 0; k < pkg->assetCount; k++)
        {
            S32 i = order[k];
            const Asset& a = pkg->assets[i];
            size_t ahdr = w.begin(kAHDR);
            w.u32(a.id);
            w.u32(a.type);
            offsetAt[i] = w.size;
            w.u32(0);
            w.u32(a.size);
            w.u32(0);
            w.u32(a.flags);
            if (a.adbg != NULL)
            {
                w.chunk(kADBG, a.adbg, a.adbgSize);
            }
            w.end(ahdr);
        }
        w.end(atoc);

        size_t ltoc = w.begin(kLTOC);
        {
            U8 b[4];
            writeBE(b, pkg->linf);
            w.chunk(kLINF, b, 4);
        }
        for (S32 l = 0; l < pkg->layerCount; l++)
        {
            const Layer& layer = pkg->layers[l];
            size_t lhdr = w.begin(kLHDR);
            w.u32(layer.type);
            w.u32((U32)layer.count);
            for (S32 i = 0; i < layer.count; i++)
            {
                w.u32(layer.ids[i]);
            }
            if (layer.ldbg != NULL)
            {
                w.chunk(kLDBG, layer.ldbg, layer.ldbgSize);
            }
            w.end(lhdr);
        }
        w.end(ltoc);
        w.end(dict);

        const size_t strm = w.begin(kSTRM);
        {
            U8 b[4];
            writeBE(b, pkg->dhdr);
            w.chunk(kDHDR, b, 4);
        }
        const size_t dpak = w.begin(kDPAK);

        if (w.failed)
        {
            free(order);
            free(offsetAt);
            fail(why, whySize, "out of memory");
            return false;
        }

        // The header is the first piece; the asset data follows it, in DPAK.
        list->add(kPieceBytes, (U32)w.size, 0, NULL);

        // Each layer from a 2K boundary, its assets in order. An asset with no
        // data takes the position of the next one, so a layer still begins with
        // the data the packer reads first.
        bool twice = false;
        U32 twiceId = 0;
        for (S32 l = 0; l < pkg->layerCount && !twice; l++)
        {
            const Layer& layer = pkg->layers[l];
            if (layer.count == 0)
            {
                continue;
            }

            list->fillTo(kLayerAlign);
            Asset* prev = NULL;
            for (S32 i = 0; i < layer.count; i++)
            {
                Asset* a = pkg->find(layer.ids[i]);
                if (a->placed)
                {
                    twice = true;
                    twiceId = a->id;
                    break;
                }

                if (a->size > 0)
                {
                    list->fillTo(assetAlign(*pkg, a->type));
                }
                if (prev != NULL)
                {
                    prev->pad = list->end - (prev->offset + prev->size);
                }
                placeAsset(list, a);
                prev = a;
            }
        }

        // Assets no layer lists are never loaded; their data goes after the layers.
        for (S32 i = 0; i < pkg->assetCount && !twice; i++)
        {
            Asset* a = &pkg->assets[i];
            if (!a->placed)
            {
                list->fillTo(16);
                placeAsset(list, a);
            }
        }

        // The packer reads a layer's size rounded up to 2K.
        list->fillTo(kLayerAlign);

        if (!twice)
        {
            for (S32 i = 0; i < pkg->assetCount; i++)
            {
                writeBE(w.data + offsetAt[i], pkg->assets[i].offset);
                writeBE(w.data + offsetAt[i] + 8, pkg->assets[i].pad);
            }

            // DPAK and STRM end where the pieces do. The layout ends on a 2K
            // boundary, so neither needs a pad byte.
            writeBE(w.data + dpak + 4, list->end - (U32)(dpak + 8));
            writeBE(w.data + strm + 4, list->end - (U32)(strm + 8));
        }

        free(order);
        free(offsetAt);

        if (twice)
        {
            fail(why, whySize, "asset %08X is in two layers", (unsigned)twiceId);
            return false;
        }
        if (list->failed)
        {
            fail(why, whySize, "out of memory");
            return false;
        }

        list->pieces[0].bytes = w.data;
        return true;
    }
} // namespace

iHipSource* iHipSourceMerge(iHipSource* base, iHipSource* mod, char* why, size_t whySize)
{
    Package basePkg;
    Package modPkg;
    char inner[256];
    Writer header = {};
    PieceList list = {};
    iHipSource* merged = NULL;

    bool ok = parsePackage(base, kFromBase, &basePkg, inner, sizeof(inner));
    if (!ok)
    {
        fail(why, whySize, "the game's package: %s", inner);
        memset(&modPkg, 0, sizeof(modPkg));
    }
    else
    {
        ok = parsePackage(mod, kFromMod, &modPkg, inner, sizeof(inner));
        if (!ok)
        {
            fail(why, whySize, "the mod's package: %s", inner);
        }
    }

    ok = ok && merge(&basePkg, &modPkg, why, whySize) &&
         layOut(&basePkg, &header, &list, why, whySize);

    // The merge keeps the header and the merged tables; the pieces point into
    // them.
    U8** blocks = NULL;
    S32 blockCount = 0;
    if (ok)
    {
        blocks = (U8**)malloc((size_t)(basePkg.assetCount + 1) * sizeof(U8*));
        merged = (iHipSource*)calloc(1, sizeof(iHipSource));
        ok = blocks != NULL && merged != NULL;
        if (!ok)
        {
            fail(why, whySize, "out of memory");
        }
    }

    if (ok)
    {
        blocks[blockCount++] = header.data;
        header.data = NULL;
        for (S32 i = 0; i < basePkg.assetCount; i++)
        {
            Asset* a = &basePkg.assets[i];
            if (a->source == kFromOwned)
            {
                blocks[blockCount++] = a->owned;
                a->owned = NULL;
            }
        }

        merged->kind = kSourceMerge;
        merged->size = list.end;
        merged->base = base;
        merged->mod = mod;
        merged->pieces = list.pieces;
        merged->pieceCount = list.count;
        merged->blocks = blocks;
        merged->blockCount = blockCount;
        list.pieces = NULL;
    }
    else
    {
        free(blocks);
        free(merged);
        merged = NULL;
        iHipSourceClose(base);
        iHipSourceClose(mod);
    }

    free(header.data);
    free(list.pieces);
    freePackage(&basePkg);
    freePackage(&modPkg);
    return merged;
}
