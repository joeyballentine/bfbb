#include "iFile.h"
#include "iHost.h"

#include "xFile.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct file_queue_entry
{
    tag_xFile* file;
    void* buf;
    U32 size;
    U32 offset;
    IFILE_READSECTOR_STATUS stat;
    void (*callback)(tag_xFile* file);
    U32 asynckey;
};

static file_queue_entry file_queue[4];

// Where relative asset names are resolved from. Retail leaves this empty: the
// DVD root is the only place a name can mean, so iFileFullPath is a strcpy and
// iFileSetPath does nothing. A host has a working directory that is not the
// asset root, so both do their jobs here.
static char sBasePath[512];

// The asset root, from BFBB_ASSETS, kept separately from sBasePath.
//
// It exists because the GAME overwrites sBasePath: zMain.cpp:890 reads a PATH
// setting out of SB.INI and hands it to iFileSetPath. On the console that is
// harmless -- SB.INI's "PATH=." means the DVD root, which is the only place
// there is -- but on a host "." is the working directory, and the port lost
// every asset the moment it read its own INI. FONT.HIP and SB.INI loaded from
// the asset root, and BOOT.HIP was then looked for in the build directory.
//
// So a RELATIVE path from the INI is resolved against the asset root, which is
// what it means on the console, and an absolute one is honoured as given.
static char sAssetRoot[512];

static bool iPathIsAbsolute(const char* path)
{
    if (path == NULL || path[0] == 0)
    {
        return false;
    }

    // "/..." or "X:..." -- the second covers Windows drive letters, which the
    // console never had to think about.
    return path[0] == '/' || path[0] == '\\' ||
           (path[1] == ':' && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')));
}

void iFileInit()
{
    sBasePath[0] = '\0';

    // **Where the game's assets are.**
    //
    // On the console this question does not exist: iFileFullPath is a strcpy
    // because the DVD root is the only place a name can mean. A host has a
    // working directory that is almost never the asset root, and NOTHING in the
    // game ever calls iFileSetPath -- the console had no use for it, so the
    // port has to answer the question itself.
    //
    // Without this the game asks for "FONT.HIP" relative to the working
    // directory, does not find it, and retries FOREVER inside zMainLoadFontHIP:
    // no error, no message, just a process sitting there. That was the first
    // hang the port ever reached, and it took a watchdog to find.
    //
    // An environment variable rather than a command-line argument, because
    // main() is the game's own in zMain.cpp and its argv handling is retail's.
    // Same arrangement as BFBB_WATCHDOG.
    sAssetRoot[0] = 0;

    const char* assets = getenv("BFBB_ASSETS");
    if (assets != NULL && assets[0] != 0)
    {
        snprintf(sAssetRoot, sizeof(sAssetRoot), "%s", assets);

        size_t n = strlen(sAssetRoot);
        if (n > 0 && sAssetRoot[n - 1] != '/' && n + 1 < sizeof(sAssetRoot))
        {
            sAssetRoot[n] = '/';
            sAssetRoot[n + 1] = 0;
        }

        iFileSetPath((char*)assets);
    }

    for (S32 i = 0; i < 4; i++)
    {
        file_queue[i].stat = IFILE_RDSTAT_NOOP;
    }
}

void iFileExit()
{
}

void iFileSetPath(char* path)
{
    if (path == NULL || path[0] == '\0')
    {
        sBasePath[0] = '\0';
        return;
    }

    // A relative path means "relative to the asset root", because that is what
    // it means on the console. "." is the common case and means exactly the
    // root. See the note at sAssetRoot.
    if (!iPathIsAbsolute(path) && sAssetRoot[0] != 0)
    {
        if (path[0] == '.' && (path[1] == 0 || path[1] == '/'))
        {
            snprintf(sBasePath, sizeof(sBasePath), "%s%s", sAssetRoot,
                     path[1] == '/' ? path + 2 : "");
        }
        else
        {
            snprintf(sBasePath, sizeof(sBasePath), "%s%s", sAssetRoot, path);
        }
    }
    else
    {
        snprintf(sBasePath, sizeof(sBasePath), "%s", path);
    }

    size_t n = strlen(sBasePath);
    if (n > 0 && sBasePath[n - 1] != '/' && n + 1 < sizeof(sBasePath))
    {
        sBasePath[n] = '/';
        sBasePath[n + 1] = '\0';
    }
}

void iFileFullPath(const char* relname, char* fullname)
{
    snprintf(fullname, sizeof(((tag_iFile*)0)->path), "%s%s", sBasePath, relname);
}

// The disc filesystem is case-insensitive and the asset names in the game data
// do not agree with each other on case; a host filesystem usually does not
// forgive that. Only used when the exact name misses, so a correctly-cased
// tree costs nothing.
static bool iResolveCaseInsensitive(char* path, size_t pathsize)
{
    if (iHostPathExists(path))
    {
        return true;
    }

    char* slash = strrchr(path, '/');
    char* leaf = slash ? slash + 1 : path;

    char dirbuf[512];
    if (slash)
    {
        size_t n = (size_t)(slash - path);
        if (n >= sizeof(dirbuf))
        {
            return false;
        }
        memcpy(dirbuf, path, n);
        dirbuf[n] = '\0';
    }
    else
    {
        strcpy(dirbuf, ".");
    }

    iHostDir* d = iHostDirOpen(dirbuf);
    if (d == NULL)
    {
        return false;
    }

    // The space left for the leaf, so a longer replacement cannot run off the
    // end of the caller's buffer.
    size_t leafroom = pathsize - (size_t)(leaf - path);

    bool found = false;
    for (const char* name = iHostDirNext(d); name != NULL; name = iHostDirNext(d))
    {
        if (iHostStrCaseCmp(name, leaf) == 0)
        {
            snprintf(leaf, leafroom, "%s", name);
            found = true;
            break;
        }
    }

    iHostDirClose(d);
    return found;
}

U32 iFileOpen(const char* name, S32 flags, tag_xFile* file)
{
    tag_iFile* ps = &file->ps;

    if (flags & IFILE_OPEN_ABSPATH)
    {
        snprintf(ps->path, sizeof(ps->path), "%s", name);
    }
    else
    {
        iFileFullPath(name, ps->path);
    }

    const char* mode = (flags & IFILE_OPEN_WRITE) ? "wb" : "rb";

    if (!(flags & IFILE_OPEN_WRITE))
    {
        iResolveCaseInsensitive(ps->path, sizeof(ps->path));
    }


    FILE* fp = fopen(ps->path, mode);
    if (fp == NULL)
    {
        ps->handle = NULL;
        ps->flags = 0;
        ps->length = 0;
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    ps->handle = fp;
    ps->length = (S32)len;
    ps->offset = 0;
    ps->asynckey = -1;
    ps->flags = 0x1;

    return 0;
}

S32 iFileSeek(tag_xFile* file, S32 offset, S32 whence)
{
    tag_iFile* ps = &file->ps;
    S32 new_pos;

    switch (whence)
    {
    case IFILE_SEEK_SET:
        new_pos = offset;
        break;
    case IFILE_SEEK_CUR:
        new_pos = offset + ps->offset;
        break;
    case IFILE_SEEK_END:
        // Retail subtracts, rather than adding a negative offset the way the
        // C library does. Kept, because every caller was written against it.
        new_pos = ps->length - offset;
        if (new_pos < 0)
        {
            new_pos = 0;
        }
        break;
    default:
        new_pos = offset;
        break;
    }

    ps->offset = new_pos;

    return ps->offset;
}

U32 iFileRead(tag_xFile* file, void* buf, U32 size)
{
    tag_iFile* ps = &file->ps;
    FILE* fp = (FILE*)ps->handle;

    if (fp == NULL)
    {
        return 0;
    }

    fseek(fp, ps->offset, SEEK_SET);

    // Retail reads whole aligned sectors and lets the tail run past the end of
    // the file, because the DVD hardware simply returns whatever is on the
    // sector. fread stops at EOF instead, so the caller would see stale bytes
    // where the console gave it padding. Zero the shortfall.
    size_t got = fread(buf, 1, size, fp);
    if (got < size)
    {
        memset((U8*)buf + got, 0, size - got);
    }

    ps->offset += (S32)size;

    return size;
}

// Retail's asynchronous read is a DVD interrupt chain: iFileReadAsync starts
// it, the completion callback issues the next chunk, and iFileAsyncService --
// weakly defined as empty in xstransvc.cpp -- does nothing at all.
//
// A host read is fast enough that chunking buys nothing, but the callback must
// still not fire inside iFileReadAsync: game code written against an interrupt
// does not expect its completion handler to run before the call that started
// it has returned. So the request is queued here and completed from the
// service function, which the load loops already call every step. Polling
// drains too, so a caller that only checks status still makes progress.
S32 iFileReadAsync(tag_xFile* file, void* buf, U32 aSize, void (*callback)(tag_xFile*),
                   S32 priority)
{
    static S32 fopcount = 1;
    tag_iFile* ps = &file->ps;

    for (S32 i = 0; i < 4; i++)
    {
        if (file_queue[i].stat != IFILE_RDSTAT_QUEUED && file_queue[i].stat != IFILE_RDSTAT_INPROG)
        {
            S32 id = fopcount++ << 2;
            S32 asynckey = id + i;

            file_queue[i].file = file;
            file_queue[i].buf = buf;
            file_queue[i].size = aSize;
            file_queue[i].offset = 0;
            file_queue[i].stat = IFILE_RDSTAT_QUEUED;
            file_queue[i].callback = callback;
            file_queue[i].asynckey = asynckey;

            ps->asynckey = asynckey;

            return i + id;
        }
    }

    return -1;
}

static void iFileServiceEntry(file_queue_entry* entry)
{
    if (entry->stat != IFILE_RDSTAT_QUEUED && entry->stat != IFILE_RDSTAT_INPROG)
    {
        return;
    }

    tag_xFile* file = entry->file;

    // A POSITIONED read: it must not move the file's own offset.
    //
    // The console's asynchronous read is DVDReadAsync(&fileInfo, buf, size,
    // ps->offset, cb) -- it takes the offset as an argument and leaves
    // ps->offset alone, so advancing past what was read is the COMPLETION
    // CALLBACK's job. iCutscene.cpp:iCSAsyncReadCB does exactly that, seeking
    // forward by the amount iFileReadAsyncStatus reports.
    //
    // Servicing this through iFileRead, which advances ps->offset itself, made
    // the position run ahead by a whole chunk on every asynchronous read: the
    // read advanced it once and the callback advanced it again. The cutscene
    // loader read its time chunk from a chunk's distance past where the chunk
    // actually was, so csn->Play was filled with whatever happened to be there
    // -- or with the 0xDEADBEEF the loader poisons the buffer with, if the read
    // ran off the end of the HIP -- and iCSSoundSetup walked off it.
    S32 mark = file->ps.offset;
    U32 done = iFileRead(file, entry->buf, entry->size);
    file->ps.offset = mark;

    entry->offset = done;
    entry->stat = (done == entry->size) ? IFILE_RDSTAT_DONE : IFILE_RDSTAT_FAIL;

    if (entry->callback)
    {
        entry->callback(file);
    }

    file->ps.asynckey = -1;
}

void iFileAsyncService()
{
    for (S32 i = 0; i < 4; i++)
    {
        iFileServiceEntry(&file_queue[i]);
    }
}

IFILE_READSECTOR_STATUS iFileReadAsyncStatus(S32 key, S32* amtToFar)
{
    file_queue_entry* entry = &file_queue[key & 0x3];

    if (key != (S32)entry->asynckey)
    {
        return IFILE_RDSTAT_EXPIRED;
    }

    iFileServiceEntry(entry);

    if (amtToFar)
    {
        *amtToFar = entry->offset;
    }

    return entry->stat;
}

void iFileReadStop()
{
    for (S32 i = 0; i < 4; i++)
    {
        if (file_queue[i].stat == IFILE_RDSTAT_QUEUED || file_queue[i].stat == IFILE_RDSTAT_INPROG)
        {
            file_queue[i].stat = IFILE_RDSTAT_NOOP;
        }
    }
}

U32 iFileClose(tag_xFile* file)
{
    tag_iFile* ps = &file->ps;

    if (ps->handle == NULL)
    {
        return 1;
    }

    fclose((FILE*)ps->handle);
    ps->handle = NULL;
    ps->flags = 0;

    return 0;
}

U32 iFileGetSize(tag_xFile* file)
{
    return file->ps.length;
}

U32 iFileFind(const char* name, tag_xFile* file)
{
    return iFileOpen(name, 0, file);
}

// Retail reports the file's start sector on the disc, which xbinio uses only
// as a read-ordering hint (BFD_startSector). There is no such thing on a host
// filesystem, and a constant 0 makes every file compare equal, which is the
// correct answer when seeking is free.
void iFileGetInfo(tag_xFile* file, U32* addr, U32* length)
{
    if (addr)
    {
        *addr = 0;
    }

    if (length)
    {
        *length = (U32)file->ps.length;
    }
}

U32* iFileLoad(char* name, U32* buffer, U32* size)
{
    char path[512];
    tag_xFile file = {};

    iFileFullPath(name, path);

    // Retail passes `name` here, not `path`, so the full path it just built is
    // thrown away. That is invisible on the GameCube because iFileFullPath is
    // a strcpy, but it would defeat iFileSetPath entirely on a host.
    if (iFileOpen(path, IFILE_OPEN_ABSPATH, &file) != 0)
    {
        return NULL;
    }

    S32 fileSize = (S32)iFileGetSize(&file);
    S32 alignedSize = (fileSize + 31) & ~31;

    if (!buffer)
    {
        buffer = (U32*)malloc(alignedSize);
        if (buffer == NULL)
        {
            iFileClose(&file);
            return NULL;
        }
    }

    iFileRead(&file, buffer, alignedSize);

    if (size)
    {
        *size = alignedSize;
    }

    iFileClose(&file);

    return buffer;
}
