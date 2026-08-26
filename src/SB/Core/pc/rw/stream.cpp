// RenderWare C API: RwStream.
//
// Unlike frame.cpp, this is not a cast and a call. librw's memory stream is
// fixed-capacity: StreamMemory::write8 truncates at the buffer it was opened
// on and never grows. RenderWare's grows, and the game depends on it --
// FullAtomicDupe in xModelBucket.cpp opens a write stream on an EMPTY RwMemory,
// streams an atomic into it, and expects RwStreamClose to hand back a block it
// can read the atomic out of again. Forwarding to librw would write nothing and
// silently duplicate no geometry, so the memory stream is written out here.
//
// The block is allocated with RwMalloc rather than librw's rwMalloc because
// the game frees it itself: xModelBucket.cpp calls RwFree on what
// RwStreamClose gave it, so it has to come from the same allocator.

#include <rwcore.h>

#include "stream.h" // brings in librw's rw.h, which must not be included twice

#include <new>
#include <string.h>

namespace
{

// A memory stream, read or write. RenderWare has one type for both and so does
// this: what changes is `grows`, which is what a write stream is allowed to do
// to its buffer and a read stream is not.
struct MemoryStream : RwStream
{
    RwUInt8* data;
    rw::uint32 length; // bytes of meaningful data
    rw::uint32 capacity; // bytes actually allocated
    rw::uint32 position;
    bool grows; // write/append: may realloc `data` to fit
    bool owned; // we allocated it, so we free it unless it is handed over
    bool ateof;

    MemoryStream(RwUInt8* block, rw::uint32 len, bool writable)
        : data(block), length(len), capacity(len), position(0), grows(writable),
          owned(writable), ateof(false)
    {
    }

    bool reserve(rw::uint32 need)
    {
        if (need <= this->capacity)
        {
            return true;
        }
        if (!this->grows)
        {
            return false;
        }

        // Doubling rather than growing to fit: streaming an atomic out is
        // thousands of small writes, and reallocating on every one of them
        // would make FullAtomicDupe quadratic.
        rw::uint32 want = this->capacity ? this->capacity * 2 : 1024;
        if (want < need)
        {
            want = need;
        }

        RwUInt8* grown = (RwUInt8*)(this->data ? RwRealloc(this->data, want) : RwMalloc(want));
        if (grown == NULL)
        {
            return false;
        }

        this->data = grown;
        this->capacity = want;
        this->owned = true;
        return true;
    }

    rw::uint32 write8(const void* buffer, rw::uint32 len)
    {
        if (!this->grows || !this->reserve(this->position + len))
        {
            this->ateof = true;
            return 0;
        }

        memcpy(this->data + this->position, buffer, len);
        this->position += len;
        if (this->position > this->length)
        {
            this->length = this->position;
        }
        return len;
    }

    rw::uint32 read8(void* buffer, rw::uint32 len)
    {
        rw::uint32 avail = this->position < this->length ? this->length - this->position : 0;
        rw::uint32 got = len < avail ? len : avail;

        if (got != 0)
        {
            memcpy(buffer, this->data + this->position, got);
            this->position += got;
        }

        // A short read is how librw's findChunk learns it has run out of
        // chunks -- readChunkHeaderInfo asks for 12 bytes and then tests eof --
        // so eof has to become true here and not one call later.
        if (got != len)
        {
            this->ateof = true;
        }
        return got;
    }

    void seek(rw::int32 offset, rw::int32 whence)
    {
        rw::int32 to;
        if (whence == 0)
        {
            to = offset;
        }
        else if (whence == 1)
        {
            to = (rw::int32)this->position + offset;
        }
        else
        {
            to = (rw::int32)this->length - offset;
        }

        if (to < 0)
        {
            this->ateof = true;
            return;
        }

        if ((rw::uint32)to > this->length)
        {
            // Seeking past the end of a write stream extends it, which is how
            // RenderWare reserves space for a size field it fills in later.
            if (!this->reserve((rw::uint32)to))
            {
                this->ateof = true;
                return;
            }
            memset(this->data + this->length, 0, (rw::uint32)to - this->length);
            this->length = (rw::uint32)to;
        }

        // Seeking somewhere valid clears the end-of-stream mark, as librw's
        // does by recomputing position outright. Read failures leave it set,
        // which is what makes findChunk terminate.
        this->position = (rw::uint32)to;
        this->ateof = false;
    }

    rw::uint32 tell(void)
    {
        return this->position;
    }

    bool eof(void)
    {
        return this->ateof;
    }

    void close_(void* pData)
    {
        // RenderWare reports the block and how much of it holds data. For a
        // write stream that is the whole point of the call; for a read stream
        // it writes back what the caller passed in, which is why it is done
        // unconditionally rather than only for writes.
        if (pData != NULL)
        {
            RwMemory* mem = (RwMemory*)pData;
            mem->start = this->data;
            mem->length = this->length;

            // Ownership goes with it -- xModelBucket.cpp RwFree's this block.
            this->owned = false;
        }

        if (this->owned)
        {
            RwFree(this->data);
        }
        this->data = NULL;
    }
};

// A file opened by name. Nothing in the game asks for one -- assets arrive as
// memory blocks out of the HIP reader -- but librw serves this type as-is, so
// forwarding is a handful of lines and beats a type that quietly fails.
//
// Unlike the memory stream, this one does need librw's engine running: every
// call below lands in engine->filefuncs.
struct FileStream : RwStream
{
    rw::StreamFile file;

    rw::uint32 write8(const void* buffer, rw::uint32 len)
    {
        return this->file.write8(buffer, len);
    }

    rw::uint32 read8(void* buffer, rw::uint32 len)
    {
        return this->file.read8(buffer, len);
    }

    void seek(rw::int32 offset, rw::int32 whence)
    {
        this->file.seek(offset, whence);
    }

    rw::uint32 tell(void)
    {
        return this->file.tell();
    }

    bool eof(void)
    {
        return this->file.eof();
    }

    // The handle is closed by rw::StreamFile's own destructor, which
    // RwStreamClose runs. Closing it here as well would trip librw's assert.
    void close_(void* pData)
    {
    }
};

// Streams come out of the game's allocator, not librw's and not global new,
// for the same reason their buffers do: the port has one memory manager and
// RwStreamClose has to give the block back to it.
template <class T> static T* streamAlloc(void)
{
    return (T*)RwMalloc(sizeof(T));
}

} // namespace

RwStream* RwStreamOpen(RwStreamType type, RwStreamAccessType accessType, const void* pData)
{
    if (pData == NULL)
    {
        return NULL;
    }

    switch (type)
    {
    case rwSTREAMMEMORY:
    {
        const RwMemory* mem = (const RwMemory*)pData;
        bool writable = accessType == rwSTREAMWRITE || accessType == rwSTREAMAPPEND;

        MemoryStream* stream = streamAlloc<MemoryStream>();
        if (stream == NULL)
        {
            return NULL;
        }

        new (stream) MemoryStream(mem->start, mem->length, writable);

        // Appending starts at the end of what is already there; writing
        // replaces it from the top.
        if (accessType == rwSTREAMAPPEND)
        {
            stream->seek(0, 2);
        }
        return stream;
    }

    case rwSTREAMFILENAME:
    {
        const char* mode = accessType == rwSTREAMWRITE ?
            "wb" :
            (accessType == rwSTREAMAPPEND ? "ab" : "rb");

        FileStream* stream = streamAlloc<FileStream>();
        if (stream == NULL)
        {
            return NULL;
        }

        new (stream) FileStream();
        if (stream->file.open((const RwChar*)pData, mode) == NULL)
        {
            // Deliberately not running the destructor: librw's StreamFile
            // closes in ~Stream and asserts on a handle that was never opened.
            // There is nothing to release, so the memory just goes back.
            RwFree(stream);
            return NULL;
        }
        return stream;
    }

    default:
        // rwSTREAMFILE hands over a FILE* the caller still owns, and
        // rwSTREAMCUSTOM supplies a skip callback that only moves forward --
        // neither can be built out of librw's streams without lying about who
        // closes the handle or about whether seeking works. Nothing in the game
        // opens either, so they are left unimplemented rather than faked.
        return NULL;
    }
}

RwBool RwStreamClose(RwStream* stream, void* pData)
{
    if (stream == NULL)
    {
        return FALSE;
    }

    stream->close_(pData);

    // Virtual, so this runs the concrete stream's destructor -- which for a
    // file stream is what closes the handle.
    stream->~RwStream();
    RwFree(stream);
    return TRUE;
}

RwBool RwStreamFindChunk(RwStream* stream, RwUInt32 type, RwUInt32* lengthOut,
                         RwUInt32* versionOut)
{
    if (stream == NULL)
    {
        return FALSE;
    }

    // Both sides report the version unpacked from the chunk's library ID, so
    // versionOut needs no translation. The chunk IDs agree too, for the core
    // ones the game uses: librw's MAKEPLUGINID puts the vendor in the high
    // bits and VEND_CORE is zero, so rwID_CLUMP and rw::ID_CLUMP are both 0x10.
    return rw::findChunk(stream, type, lengthOut, versionOut) ? TRUE : FALSE;
}

RwStream* RwStreamReadChunkHeaderInfo(RwStream* stream, RwChunkHeaderInfo* chunkHeaderInfo)
{
    if (stream == NULL || chunkHeaderInfo == NULL)
    {
        return NULL;
    }

    // The 12 bytes are read here rather than through rw::readChunkHeaderInfo
    // because RenderWare reports one thing librw discards: isComplex, which
    // says the library ID is the packed version+build form 3.2 and later write
    // rather than the plain version-only word of 3.1 and earlier. That is a
    // property of the raw word, and librw unpacks it away.
    struct
    {
        rw::int32 type;
        rw::int32 size;
        rw::uint32 libid;
    } header;

    if (stream->read32(&header, 12) != 12 || stream->eof())
    {
        return NULL;
    }

    chunkHeaderInfo->type = (RwUInt32)header.type;
    chunkHeaderInfo->length = (RwUInt32)header.size;
    chunkHeaderInfo->version = (RwUInt32)rw::libraryIDUnpackVersion(header.libid);
    chunkHeaderInfo->buildNum = (RwUInt32)rw::libraryIDUnpackBuild(header.libid);
    chunkHeaderInfo->isComplex = (header.libid & 0xFFFF0000) != 0 ? TRUE : FALSE;

    return stream;
}
