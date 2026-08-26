// Checks the RenderWare shim by RUNNING it.
//
// This is the same bargain as tests/selftest.cpp: the GameCube side is scored
// by a byte-identical DOL and a port has no equivalent, so every claim the shim
// makes gets exercised and its answer checked. It matters more here than
// anywhere else in the layer, because the shim compiled and linked cleanly for
// a whole commit before anyone discovered that it could not create a frame.
//
// Not in CMakeLists.txt, because librw is not vendored yet. Build it by hand
// with the command in README.md.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rwcore.h>

// ../stream.h rather than "rw.h": it pulls in librw's header itself, and that
// header has no include guard, so including both redefines everything in it.
// It is also the only way to reach an RwStream's members, which are private to
// the shim by design.
#include "../stream.h"

static int failures;

static void check(bool ok, const char* what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
    {
        failures++;
    }
}

// The game's allocator, stood in for. RwEngineInit's whole reason to take a
// RwMemoryFunctions is that the game allocates RenderWare's objects out of its
// own heap; if librw were still calling malloc directly, nothing else in the
// port would notice, so it is counted here.
static int sNumAlloc;
static int sNumFree;

static void* testMalloc(size_t size)
{
    sNumAlloc++;
    return malloc(size);
}

static void testFree(void* mem)
{
    if (mem != NULL)
    {
        sNumFree++;
    }
    free(mem);
}

static void* testRealloc(void* mem, size_t size)
{
    return realloc(mem, size);
}

static void* testCalloc(size_t numObj, size_t sizeObj)
{
    return calloc(numObj, sizeObj);
}

static void test_engine_startup()
{
    printf("RwEngine\n");

    RwMemoryFunctions memoryFns = { testMalloc, testFree, testRealloc, testCalloc };

    check(RwEngineInit(&memoryFns, 0, 0x60000) != FALSE, "RwEngineInit");
    check(RwEngineInstance != NULL, "RwEngineInstance is live after init");
    check(RwEngineInstance->engineStatus == rwENGINESTATUSINITED, "engineStatus is INITED");

    // Refused rather than accepted twice: a second init would strand librw's
    // first plugin list and every object allocated against its sizes.
    check(RwEngineInit(&memoryFns, 0, 0x60000) == FALSE, "a second RwEngineInit is refused");

    RwEngineOpenParams params;
    params.displayID = NULL;
    check(RwEngineOpen(&params) != FALSE, "RwEngineOpen");
    check(RwEngineInstance->engineStatus == rwENGINESTATUSOPENED, "engineStatus is OPENED");

    check(RwEngineStart() != FALSE, "RwEngineStart");
    check(RwEngineInstance->engineStatus == rwENGINESTATUSSTARTED, "engineStatus is STARTED");

    check(sNumAlloc > 0, "librw allocated through the memory functions it was given");

    // xFX.cpp calls through this table by hand.
    check(RwEngineInstance->stringFuncs.vecStrcmp("spec3", "spec3") == 0 &&
              RwEngineInstance->stringFuncs.vecStrcmp("spec3", "spec4") != 0,
          "RwEngineInstance->stringFuncs.vecStrcmp");

    // No renderer is linked, so there is no video mode. The shim has to say so
    // instead of forwarding librw's null device, which reports success without
    // writing to the struct -- xScrFx would size a full-screen rectangle from
    // whatever was on its own stack.
    RwVideoMode videoMode;
    check(RwEngineGetCurrentVideoMode() == -1, "no current video mode without a backend");
    check(RwEngineGetVideoModeInfo(&videoMode, 0) == NULL, "no video mode info without a backend");
}

static void test_frames()
{
    printf("RwFrame\n");

    RwFrame* frame = RwFrameCreate();
    check(frame != NULL, "RwFrameCreate");
    if (frame == NULL)
    {
        return;
    }

    RwV3d t = { 1.0f, 2.0f, 3.0f };
    RwFrameTranslate(frame, &t, rwCOMBINEREPLACE);

    // Read out of the RenderWare struct, not through an accessor: this is the
    // whole point of mirroring librw's layout, and about 120 sites in the game
    // do exactly this.
    check(frame->modelling.pos.x == 1.0f && frame->modelling.pos.y == 2.0f &&
              frame->modelling.pos.z == 3.0f,
          "RwFrameTranslate lands in ->modelling.pos");

    RwMatrix* ltm = RwFrameGetLTM(frame);
    check(ltm->pos.x == 1.0f && ltm->pos.y == 2.0f && ltm->pos.z == 3.0f,
          "LTM of a root frame is its modelling matrix");

    // A parented frame is what actually walks engine->frameDirtyList, which is
    // the structure that does not exist until RwEngineStart has run.
    RwFrame* child = RwFrameCreate();
    check(child != NULL, "RwFrameCreate for a child");
    if (child == NULL)
    {
        RwFrameDestroy(frame);
        return;
    }

    // RwFrameAddChild is not written yet, so the hierarchy is built through
    // librw. Legal precisely because RwFrame IS rw::Frame here.
    reinterpret_cast<rw::Frame*>(frame)->addChild(reinterpret_cast<rw::Frame*>(child));

    RwV3d ct = { 0.0f, 0.0f, 5.0f };
    RwFrameTranslate(child, &ct, rwCOMBINEREPLACE);

    RwMatrix* childLtm = RwFrameGetLTM(child);
    check(childLtm->pos.x == 1.0f && childLtm->pos.y == 2.0f && childLtm->pos.z == 8.0f,
          "child LTM composes with its parent");

    // Moving the parent has to invalidate the child, which is the dirty list
    // doing its job rather than getLTM recomputing unconditionally.
    RwV3d pt = { 10.0f, 0.0f, 0.0f };
    RwFrameTranslate(frame, &pt, rwCOMBINEPOSTCONCAT);
    childLtm = RwFrameGetLTM(child);
    check(childLtm->pos.x == 11.0f && childLtm->pos.y == 2.0f && childLtm->pos.z == 8.0f,
          "moving a parent updates the child LTM");

    RwFrameDestroy(child);
    RwFrameDestroy(frame);
    check(RwFrameDestroy(NULL) == FALSE, "RwFrameDestroy(NULL) is refused");
}

static void test_values()
{
    printf("RwV3d / RwMatrix\n");

    RwV3d v = { 3.0f, 4.0f, 0.0f };
    check(RwV3dLength(&v) == 5.0f, "RwV3dLength");
}

static void test_streams()
{
    printf("RwStream\n");

    // A write stream on an EMPTY RwMemory, which is how FullAtomicDupe in
    // xModelBucket.cpp opens one. librw's own memory stream cannot do this --
    // it truncates at the buffer it was handed -- so this is the check that
    // says the hand-written one in stream.cpp earns its place.
    RwMemory mem = { NULL, 0 };
    RwStream* stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMWRITE, &mem);
    check(stream != NULL, "RwStreamOpen(rwSTREAMMEMORY, rwSTREAMWRITE) on an empty block");
    if (stream == NULL)
    {
        return;
    }

    // Past any plausible initial capacity, so the block has to be grown.
    const RwUInt32 payload = 9000;
    rw::writeChunkHeader(stream, rw::ID_TEXDICTIONARY, (rw::int32)payload);

    RwUInt32 written = 0;
    for (RwUInt32 i = 0; i < payload; i++)
    {
        RwUInt8 byte = (RwUInt8)(i & 0xFF);
        written += stream->write8(&byte, 1);
    }
    check(written == payload, "a memory write stream grows instead of truncating");

    check(RwStreamClose(stream, &mem) != FALSE, "RwStreamClose");
    check(mem.start != NULL && mem.length == 12 + payload,
          "RwStreamClose hands the block back through its RwMemory");

    // Read it back the way zAssetTypes.cpp reads a TXD out of a HIP block.
    stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &mem);
    check(stream != NULL, "RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD)");

    RwUInt32 length = 0;
    RwUInt32 version = 0;
    check(RwStreamFindChunk(stream, rwID_TEXDICTIONARY, &length, &version) != FALSE,
          "RwStreamFindChunk finds the chunk that was written");
    check(length == payload, "RwStreamFindChunk reports the chunk length");

    RwUInt8 buf[16];
    memset(buf, 0, sizeof(buf));
    check(stream->read8(buf, 16) == 16 && buf[0] == 0 && buf[15] == 15,
          "the bytes read back are the bytes written");
    RwStreamClose(stream, NULL);

    // A chunk that is not there has to end the search rather than run off the
    // end of the block.
    stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &mem);
    check(RwStreamFindChunk(stream, rwID_CLUMP, NULL, NULL) == FALSE,
          "RwStreamFindChunk stops at the end of the block");
    RwStreamClose(stream, NULL);

    stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &mem);
    RwChunkHeaderInfo info;
    memset(&info, 0xCD, sizeof(info));
    check(RwStreamReadChunkHeaderInfo(stream, &info) == stream, "RwStreamReadChunkHeaderInfo");
    check(info.type == (RwUInt32)rwID_TEXDICTIONARY && info.length == payload,
          "the chunk header info matches what was written");
    check(info.version == (RwUInt32)rw::version && info.buildNum == (RwUInt32)rw::build,
          "the library version and build come back unpacked");
    check(info.isComplex != FALSE, "isComplex: 3.2 and later pack version and build together");

    // Past the end: eof, not a read off the end of the block.
    stream->seek((rw::int32)mem.length - 4, 0);
    check(stream->read8(buf, 16) == 4 && stream->eof(), "a short read at the end reports eof");
    check(RwStreamReadChunkHeaderInfo(stream, &info) == NULL,
          "RwStreamReadChunkHeaderInfo fails at eof rather than inventing a chunk");
    RwStreamClose(stream, NULL);

    RwFree(mem.start);

    // Deliberately unimplemented, and they say so rather than half-working.
    RwMemory unused = { NULL, 0 };
    check(RwStreamOpen(rwSTREAMFILE, rwSTREAMREAD, &unused) == NULL,
          "rwSTREAMFILE is refused rather than faked");
    check(RwStreamOpen(rwSTREAMCUSTOM, rwSTREAMREAD, &unused) == NULL,
          "rwSTREAMCUSTOM is refused rather than faked");
    check(RwStreamClose(NULL, NULL) == FALSE, "RwStreamClose(NULL) is refused");
}

static RwTexture* countTextures(RwTexture* texture, void* data)
{
    (*(int*)data)++;
    return texture;
}

static RwTexture* stopAfterFirst(RwTexture* texture, void* data)
{
    (*(int*)data)++;
    return NULL;
}

static void test_textures()
{
    printf("RwTexture / RwTexDictionary\n");

    // The rasters are NULL on purpose. RwRasterCreate cannot run here: librw's
    // null driver asserts in rasterCreate, because allocating pixels is the one
    // thing that genuinely needs a backend. Everything the dictionary does is
    // independent of it.
    RwTexture* a = RwTextureCreate(NULL);
    RwTexture* b = RwTextureCreate(NULL);
    check(a != NULL && b != NULL, "RwTextureCreate");
    if (a == NULL || b == NULL)
    {
        return;
    }

    check(a->refCount == 1, "a new texture starts with one reference");
    // RwTextureSetName is not written yet, so the name goes in through librw
    // and comes back out through RenderWare's macro. Which is the point: it is
    // the same 32 bytes.
    strcpy(reinterpret_cast<rw::Texture*>(a)->name, "sand_bottom");
    check(strcmp(RwTextureGetName(a), "sand_bottom") == 0,
          "librw's name and RwTextureGetName are the same 32 bytes");

    // The filter/addressing word is packed by macros reading the mirrored
    // field, so this checks the layout and the bit positions at once.
    RwTextureSetFilterMode(a, rwFILTERLINEARMIPLINEAR);
    RwTextureSetAddressing(a, rwTEXTUREADDRESSCLAMP);
    check(RwTextureGetFilterMode(a) == rwFILTERLINEARMIPLINEAR, "RwTextureGetFilterMode");
    check(RwTextureGetAddressing(a) == rwTEXTUREADDRESSCLAMP, "RwTextureGetAddressing");
    check(reinterpret_cast<rw::Texture*>(a)->getFilter() == rw::Texture::LINEARMIPLINEAR,
          "librw reads back the filter the RenderWare macro wrote");

    // RwTexDictionaryCreate is not on the game's list, so the dictionary comes
    // from librw -- legal because an RwTexDictionary IS an rw::TexDictionary.
    rw::TexDictionary* raw = rw::TexDictionary::create();
    RwTexDictionary* dict = reinterpret_cast<RwTexDictionary*>(raw);
    check(dict != NULL, "a texture dictionary to put them in");
    if (dict == NULL)
    {
        return;
    }

    raw->add(reinterpret_cast<rw::Texture*>(a));
    raw->add(reinterpret_cast<rw::Texture*>(b));
    check(a->dict == dict, "RwTexture::dict points at the dictionary that holds it");

    int seen = 0;
    check(RwTexDictionaryForAllTextures(dict, countTextures, &seen) == dict,
          "RwTexDictionaryForAllTextures returns its dictionary");
    check(seen == 2, "RwTexDictionaryForAllTextures visits every texture");

    seen = 0;
    RwTexDictionaryForAllTextures(dict, stopAfterFirst, &seen);
    check(seen == 1, "a callback returning NULL stops the walk");

    check(RwTexDictionaryRemoveTexture(a) == a, "RwTexDictionaryRemoveTexture");
    check(a->dict == NULL, "a removed texture no longer names a dictionary");
    check(RwTexDictionaryRemoveTexture(a) == NULL,
          "removing a texture that is in no dictionary is refused");

    seen = 0;
    RwTexDictionaryForAllTextures(dict, countTextures, &seen);
    check(seen == 1, "the removed texture is gone from the walk");

    // This is exactly what RWTX_Read in zAssetTypes.cpp does: pull the one
    // texture it wants out, then destroy the dictionary and everything left in
    // it. `b` goes with the dictionary; `a` is ours to destroy.
    check(RwTexDictionaryDestroy(dict) != FALSE, "RwTexDictionaryDestroy");

    check(RwTextureDestroy(a) != FALSE, "RwTextureDestroy");
    check(RwTextureDestroy(NULL) == FALSE, "RwTextureDestroy(NULL) is refused");
    check(RwTexDictionaryDestroy(NULL) == FALSE, "RwTexDictionaryDestroy(NULL) is refused");

    // A TXD stream read cannot be exercised without a backend -- the textures
    // inside one are native rasters -- but the failure path checks the wiring:
    // an RwStream* has to arrive at librw as an rw::Stream*.
    RwUInt8 notATxd[16];
    memset(notATxd, 0, sizeof(notATxd));
    RwMemory mem = { notATxd, sizeof(notATxd) };
    RwStream* stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &mem);
    check(RwTexDictionaryStreamRead(stream) == NULL,
          "RwTexDictionaryStreamRead rejects a stream with no dictionary in it");
    RwStreamClose(stream, NULL);
    check(RwTexDictionaryStreamRead(NULL) == NULL, "RwTexDictionaryStreamRead(NULL) is refused");
}

static void test_images()
{
    printf("RwImage\n");

    RwImage* image = RwImageCreate(64, 32, 32);
    check(image != NULL, "RwImageCreate");
    if (image == NULL)
    {
        return;
    }

    check(image->width == 64 && image->height == 32 && image->depth == 32,
          "the size lands in RwImage's own fields");
    check(RwImageGetPixels(image) == NULL, "a new image has no pixels yet");

    check(RwImageAllocatePixels(image) == image, "RwImageAllocatePixels");
    check(RwImageGetPixels(image) != NULL, "RwImageAllocatePixels allocated them");
    check(RwImageGetStride(image) == 64 * 4, "a 32-bit image strides four bytes per pixel");

    // Written through the RenderWare accessor and read back through librw's
    // field, which is the mirroring doing its job.
    RwImageGetPixels(image)[7] = 0xAB;
    check(reinterpret_cast<rw::Image*>(image)->pixels[7] == 0xAB,
          "librw sees the pixels the RenderWare macro handed out");

    // A palettised image gets a palette too, and only then.
    RwImage* paletted = RwImageCreate(16, 16, 8);
    RwImageAllocatePixels(paletted);
    check(RwImageGetPalette(paletted) != NULL, "an 8-bit image is given a palette");
    check(RwImageGetPalette(image) == NULL, "a 32-bit image is not");
    RwImageDestroy(paletted);

    check(RwImageDestroy(image) != FALSE, "RwImageDestroy");
    check(RwImageDestroy(NULL) == FALSE, "RwImageDestroy(NULL) is refused");

    // RwImageSetFromRaster is NOT exercised: it goes through the driver's
    // rasterToImage, and there is no driver here. It needs a GL3 or D3D9 librw.
    check(RwImageSetFromRaster(NULL, NULL) == NULL, "RwImageSetFromRaster(NULL, NULL) is refused");
}

static void test_engine_shutdown()
{
    printf("RwEngine teardown\n");

    check(RwEngineStop() != FALSE, "RwEngineStop");
    check(RwEngineInstance->engineStatus == rwENGINESTATUSOPENED, "engineStatus is OPENED again");

    check(RwEngineClose() != FALSE, "RwEngineClose");
    check(RwEngineInstance->engineStatus == rwENGINESTATUSINITED, "engineStatus is INITED again");

    check(RwEngineTerm() != FALSE, "RwEngineTerm");

    // Nulled rather than left pointing at a plausible-looking struct, so that
    // anything still reading it faults where the bug is.
    check(RwEngineInstance == NULL, "RwEngineInstance is null after term");
    check(RwEngineTerm() == FALSE, "RwEngineTerm on a dead engine is refused");

    check(sNumFree > 0, "librw freed through the memory functions it was given");
}

int main()
{
    test_engine_startup();
    test_frames();
    test_values();
    test_streams();
    test_textures();
    test_images();
    test_engine_shutdown();

    printf("\n%d failure%s\n", failures, failures == 1 ? "" : "s");
    return failures != 0;
}
