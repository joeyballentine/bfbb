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
#include <math.h>

#include <rwcore.h>
#include <rpcollis.h>
#include <rpmatfx.h>
#include <rpptank.h>
#include <rpskin.h>
#include <rpworld.h>
#include <rtintsec.h>
#include <rtquat.h>
#include <rtslerp.h>

// ../stream.h rather than "rw.h": it pulls in librw's header itself, and that
// header has no include guard, so including both redefines everything in it.
// It is also the only way to reach an RwStream's members, which are private to
// the shim by design.
#include "../stream.h"

#include "iWindow.h"

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

    // Strictly between init and open, which is not a style choice: each of
    // these registers plugins that grow the size of an atomic, a geometry or a
    // material, and Engine::open freezes those sizes. Attaching after open
    // would hand out plugin offsets past the end of every object allocated
    // afterwards. iSystem.cpp's RWAttachPlugins sequences the real game's
    // calls in exactly this window.
    check(RpWorldPluginAttach() != FALSE, "RpWorldPluginAttach");
    check(RpWorldPluginAttach() != FALSE, "and again -- attaching twice is idempotent");
    check(RpSkinPluginAttach() != FALSE, "RpSkinPluginAttach");
    check(RpMatFXPluginAttach() != FALSE, "RpMatFXPluginAttach");
    check(RpPTankPluginAttach() != FALSE, "RpPTankPluginAttach");
    check(_rpPTankAtomicDataOffset > 0, "RpPTank got a slot in the atomic's plugin block");

    // A real backend needs a window before the engine can open on it -- D3D9
    // creates its device against an HWND -- so this test opens one, the way
    // iSystem.cpp will. Under LIBRW_PLATFORM=NULL there is nothing to open and
    // the test stays headless, which is what lets it run on a build machine.
#ifndef RW_NULL
    iWindowParams windowParams;
    windowParams.title = "bfbb rw_selftest";
    windowParams.width = 640;
    windowParams.height = 480;
    windowParams.fullscreen = false;
    check(iWindowOpen(&windowParams) != FALSE, "iWindowOpen for the render backend");
    printf("  (window backend: %s)\n", iWindowBackendName());
#endif

    RwEngineOpenParams params;
    params.displayID = NULL;
    check(RwEngineOpen(&params) != FALSE, "RwEngineOpen");

    // Everything after this dereferences an open engine. Bailing out here turns
    // "the backend could not start" into one reported failure instead of a
    // segfault twenty checks later, which is how this first ran under D3D9.
    if (RwEngineInstance->engineStatus != rwENGINESTATUSOPENED)
    {
        check(false, "engineStatus is OPENED -- stopping, the rest needs an open engine");
        return;
    }
    check(true, "engineStatus is OPENED");

    check(RwEngineStart() != FALSE, "RwEngineStart");
    check(RwEngineInstance->engineStatus == rwENGINESTATUSSTARTED, "engineStatus is STARTED");

    check(sNumAlloc > 0, "librw allocated through the memory functions it was given");

    // xFX.cpp calls through this table by hand.
    check(RwEngineInstance->stringFuncs.vecStrcmp("spec3", "spec3") == 0 &&
              RwEngineInstance->stringFuncs.vecStrcmp("spec3", "spec4") != 0,
          "RwEngineInstance->stringFuncs.vecStrcmp");

    RwVideoMode videoMode;
#ifdef RW_NULL
    // No renderer is linked, so there is no video mode. The shim has to say so
    // instead of forwarding librw's null device, which reports success without
    // writing to the struct -- xScrFx would size a full-screen rectangle from
    // whatever was on its own stack.
    check(RwEngineGetCurrentVideoMode() == -1, "no current video mode without a backend");
    check(RwEngineGetVideoModeInfo(&videoMode, 0) == NULL, "no video mode info without a backend");
#else
    // With a backend the forwarding path is live, and this is the first thing
    // that proves it: a mode index that is not -1, and a mode whose width and
    // height were actually written rather than left as whatever was on the
    // stack. xScrFx sizes its full-screen rectangle from exactly this.
    check(RwEngineGetCurrentVideoMode() >= 0, "the backend reports a current video mode");
    memset(&videoMode, 0xCD, sizeof(videoMode));
    check(RwEngineGetVideoModeInfo(&videoMode, RwEngineGetCurrentVideoMode()) == &videoMode,
          "RwEngineGetVideoModeInfo");
    check(videoMode.width > 0 && videoMode.height > 0,
          "and it wrote a real width and height into the caller's struct");
#endif
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

static void test_cameras()
{
    printf("RwCamera\n");

    RwCamera* camera = RwCameraCreate();
    check(camera != NULL, "RwCameraCreate");
    if (camera == NULL)
    {
        return;
    }

    // Read out of the RenderWare struct, which is the mirroring doing its job:
    // librw's Camera::create wrote these and RwCamera names them elsewhere in
    // the struct than the console does.
    check(camera->nearPlane == 0.05f && camera->farPlane == 10.0f && camera->fogPlane == 5.0f,
          "librw's clip plane defaults land in RwCamera's own fields");
    check(camera->projectionType == rwPERSPECTIVE, "and so does the projection type");
    check(RwCameraGetViewWindow(camera)->x == 1.0f && RwCameraGetViewWindow(camera)->y == 1.0f,
          "RwCameraGetViewWindow reads the mirrored field");
    check(RwCameraGetRaster(camera) == NULL && RwCameraGetZRaster(camera) == NULL,
          "a new camera has no rasters");

    // With a real backend the camera needs somewhere to draw before anything
    // begins an update on it: librw hands camera->frameBuffer to the device as
    // its render target, and a NULL one faults inside the driver rather than
    // failing. iCamera.cpp:27-28 attaches exactly these two, and this is the
    // same pair -- so it is also the first exercise of RwRasterCreate's SUCCESS
    // path, which LIBRW_PLATFORM=NULL could never reach (its driver asserts in
    // rasterCreate). See rw/TODO.md, "Recorded but not rendered".
#ifndef RW_NULL
    RwRaster* cameraRaster = RwRasterCreate(640, 480, 0, rwRASTERTYPECAMERA);
    RwRaster* zRaster = RwRasterCreate(640, 480, 0, rwRASTERTYPEZBUFFER);
    check(cameraRaster != NULL, "RwRasterCreate(rwRASTERTYPECAMERA) with a backend linked");
    check(zRaster != NULL, "RwRasterCreate(rwRASTERTYPEZBUFFER) with a backend linked");
    check(cameraRaster != NULL && cameraRaster->width == 640 && cameraRaster->height == 480,
          "and the raster came back the size it was asked for");
    RwCameraSetRaster(camera, cameraRaster);
    RwCameraSetZRaster(camera, zRaster);
    check(RwCameraGetRaster(camera) == cameraRaster, "RwCameraSetRaster");
#endif
    check(RwCameraGetWorld(camera) == NULL, "and is in no world");

    RwCameraSetProjection(camera, rwPARALLEL);
    check(RwCameraGetProjection(camera) == rwPARALLEL, "RwCameraSetProjection");
    check(reinterpret_cast<rw::Camera*>(camera)->projection == rw::Camera::PARALLEL,
          "librw reads back the projection the RenderWare call wrote");
    RwCameraSetProjection(camera, rwPERSPECTIVE);

    RwV2d vw = { 0.5f, 0.375f };
    RwCameraSetViewWindow(camera, &vw);
    check(RwCameraGetViewWindow(camera)->x == 0.5f && RwCameraGetViewWindow(camera)->y == 0.375f,
          "RwCameraSetViewWindow");

    RwCameraSetNearClipPlane(camera, 0.1f);
    RwCameraSetFarClipPlane(camera, 400.0f);
    check(RwCameraGetNearClipPlane(camera) == 0.1f && RwCameraGetFarClipPlane(camera) == 400.0f,
          "RwCameraSetNearClipPlane / RwCameraSetFarClipPlane");

    // ---- the curCamera landmine ----
    //
    // This is the check the whole camera group exists for. RwEngineInstance is
    // a SECOND RwGlobals, not an alias onto rw::engine, so begin/end have to
    // write both copies -- xCutscene.cpp:716 and xFX.cpp:3044 read
    // RwEngineInstance->curCamera as a struct field and there is no call to
    // hook. Nothing else in the port would notice this being wrong until
    // someone played a cutscene.
    check(RwEngineInstance->curCamera == NULL, "curCamera is null before an update");

    // A camera needs a FRAME before a real device begins an update on it.
    // d3ddevice.cpp:1228 opens with
    //
    //     Matrix::invert(&inv, cam->getFrame()->getLTM());
    //
    // so a frameless camera dereferences NULL inside the driver -- no error, no
    // return value, just a fault. LIBRW_PLATFORM=NULL's beginUpdate does
    // nothing at all and never noticed, which is why this test did not attach
    // one until a backend was linked. zGame.cpp and iCamera.cpp:24-31 always
    // attach one, so the game was never going to hit this; the test was.
    RwFrame* cameraFrame = RwFrameCreate();
    check(cameraFrame != NULL, "a frame for the camera to sit on");
    RwCameraSetFrame(camera, cameraFrame);

    check(RwCameraBeginUpdate(camera) == camera, "RwCameraBeginUpdate");
    check(RwEngineInstance->curCamera == camera,
          "RwCameraBeginUpdate sets RwEngineInstance->curCamera");
    check(rw::engine->currentCamera == reinterpret_cast<rw::Camera*>(camera),
          "and rw::engine->currentCamera, which is the other copy");
    check(RwCameraGetCurrentCamera() == camera,
          "RwCameraGetCurrentCamera, the macro eleven more game files use");
    check(RwEngineInstance->curWorld == rw::engine->currentWorld,
          "curWorld agrees with librw's copy too");

    check(RwCameraEndUpdate(camera) == camera, "RwCameraEndUpdate");
    // zGame.cpp:910 and zNPCTypePrawn.cpp:1718 both use "curCamera is not null"
    // as "an update is in progress" and call EndUpdate on the strength of it,
    // so leaving it set would end an update that had already ended.
    check(RwEngineInstance->curCamera == NULL, "RwCameraEndUpdate nulls curCamera again");
    check(rw::engine->currentCamera == NULL, "in both copies");
    check(RwEngineInstance->curWorld == NULL && rw::engine->currentWorld == NULL,
          "and curWorld with it");

    // ---- the frustum ----
    //
    // The planes only exist once the camera has been synced through its frame,
    // which is librw's cameraSync running off the frame's update. The frame is
    // attached through librw because RwCameraSetFrame's helper is not written
    // yet -- legal because an RwCamera IS an rw::Camera.
    RwFrame* frame = RwFrameCreate();
    reinterpret_cast<rw::Camera*>(camera)->setFrame(reinterpret_cast<rw::Frame*>(frame));
    check(RwCameraGetFrame(camera) == frame, "RwCameraGetFrame after attaching one");

    RwV2d square = { 1.0f, 1.0f };
    RwCameraSetViewWindow(camera, &square);
    RwCameraSetNearClipPlane(camera, 1.0f);
    RwCameraSetFarClipPlane(camera, 100.0f);
    rw::Frame::syncDirty();

    // Default frame is the identity, so the camera sits at the origin looking
    // down +z with a 90-degree square view window.
    RwSphere infront = { { 0.0f, 0.0f, 10.0f }, 1.0f };
    RwSphere behind = { { 0.0f, 0.0f, -10.0f }, 1.0f };
    RwSphere beyondFar = { { 0.0f, 0.0f, 200.0f }, 1.0f };
    RwSphere onTheNearPlane = { { 0.0f, 0.0f, 1.0f }, 1.0f };

    check(RwCameraFrustumTestSphere(camera, &infront) == rwSPHEREINSIDE,
          "RwCameraFrustumTestSphere: a sphere down the view axis is inside");
    check(RwCameraFrustumTestSphere(camera, &behind) == rwSPHEREOUTSIDE,
          "a sphere behind the camera is outside");
    check(RwCameraFrustumTestSphere(camera, &beyondFar) == rwSPHEREOUTSIDE,
          "a sphere past the far plane is outside");
    check(RwCameraFrustumTestSphere(camera, &onTheNearPlane) == rwSPHEREBOUNDARY,
          "a sphere straddling the near plane is on the boundary");

    // The same planes the game reads by hand -- iCamera.cpp:131 pulls six of
    // them out by index into an xVec4 array and iModel.cpp:468 walks them --
    // read here through RenderWare's own struct rather than librw's.
    //
    // The INDEX ORDER is the part worth pinning down, because iCamera.cpp picks
    // planes 0 through 5 by number and gets a different plane if librw's order
    // is not RenderWare's. librw builds them far, near, right, top, left,
    // bottom, with each normal pointing out of the frustum: the far plane's is
    // the camera's own `at`, at the far distance, and the near plane's is its
    // negation.
    check(camera->frustumPlanes[0].plane.normal.z == 1.0f &&
              camera->frustumPlanes[0].plane.distance == 100.0f,
          "frustumPlanes[0] is the far plane, where SetFarClipPlane put it");
    check(camera->frustumPlanes[1].plane.normal.z == -1.0f &&
              camera->frustumPlanes[1].plane.distance == -1.0f,
          "frustumPlanes[1] is the near plane, facing the other way");
    check(camera->frustumBoundBox.sup.z == 100.0f && camera->frustumBoundBox.inf.z == 1.0f,
          "frustumBoundBox spans near to far");

    reinterpret_cast<rw::Camera*>(camera)->setFrame(NULL);
    RwFrameDestroy(frame);

    // Only the refusals are checkable for these two. RwCameraShowRaster reaches
    // Raster::show, which is the device's flip; RwCameraClear reaches the
    // device's clearCamera. LIBRW_PLATFORM=NULL has neither -- its clearCamera
    // is an empty function -- so a success here would prove nothing about what
    // ends up on screen. Whoever links GL3 or D3D9 finishes these.
#ifdef RW_NULL
    check(RwCameraShowRaster(camera, NULL, rwRASTERFLIPWAITVSYNC) == NULL,
          "RwCameraShowRaster refuses a camera with no frame buffer");
#else
    // With a backend linked this camera HAS a frame buffer -- attached above,
    // because beginUpdate needs one -- so there is nothing left to refuse and
    // the old check was only ever testing the absence of a renderer. What
    // showing it actually puts on screen still is not checked here: that needs
    // a frame drawn first, and this test draws nothing.
    check(RwCameraGetRaster(camera) != NULL,
          "the camera has a frame buffer, so RwCameraShowRaster has nothing to refuse");
#endif
    check(RwCameraShowRaster(NULL, NULL, 0) == NULL, "RwCameraShowRaster(NULL) is refused");
    check(RwCameraClear(NULL, NULL, rwCAMERACLEARZ) == NULL, "RwCameraClear(NULL) is refused");

    check(RwCameraDestroy(camera) != FALSE, "RwCameraDestroy");
    check(RwCameraDestroy(NULL) == FALSE, "RwCameraDestroy(NULL) is refused");
}

static void test_lights()
{
    printf("RpLight\n");

    RpLight* light = RpLightCreate(rpLIGHTPOINT);
    check(light != NULL, "RpLightCreate");
    if (light == NULL)
    {
        return;
    }

    check(RpLightGetType(light) == rpLIGHTPOINT, "RpLightGetType reads the mirrored object");
    check(RpLightGetFlags(light) == (rpLIGHTLIGHTATOMICS | rpLIGHTLIGHTWORLD),
          "a new light lights both atomics and the world");

    RwRGBAReal green = { 0.0f, 1.0f, 0.25f, 1.0f };
    check(RpLightSetColor(light, &green) == light, "RpLightSetColor");
    check(RpLightGetColor(light)->red == 0.0f && RpLightGetColor(light)->green == 1.0f &&
              RpLightGetColor(light)->blue == 0.25f,
          "the colour lands in RpLight's own field");
    check(reinterpret_cast<rw::Light*>(light)->color.green == 1.0f,
          "librw reads back the colour the RenderWare call wrote");
    check(light->object.object.privateFlags == 0, "a coloured light is not flagged as grey");

    RwRGBAReal grey = { 0.5f, 0.5f, 0.5f, 1.0f };
    RpLightSetColor(light, &grey);
    check(light->object.object.privateFlags != 0,
          "and a grey one is -- the flag both libraries use to take a cheaper path");

    check(RpLightSetRadius(light, 12.5f) == light, "RpLightSetRadius");
    check(RpLightGetRadius(light) == 12.5f, "the radius lands in RpLight's own field");
    check(reinterpret_cast<rw::Light*>(light)->radius == 12.5f, "and librw agrees");

    // Stored as -cos(angle), not as the angle, which is the thing a hand-written
    // assignment to the field would get wrong.
    check(RpLightSetConeAngle(light, 0.0f) == light, "RpLightSetConeAngle");
    check(light->minusCosAngle == -1.0f, "a zero cone angle stores -cos(0) = -1");
    RpLightSetConeAngle(light, rwPIOVER2);
    check(light->minusCosAngle > -0.0001f && light->minusCosAngle < 0.0001f,
          "and a right angle stores -cos(pi/2) = 0");

    check(RpLightDestroy(light) != FALSE, "RpLightDestroy");
    check(RpLightDestroy(NULL) == FALSE, "RpLightDestroy(NULL) is refused");
    check(RpLightSetColor(NULL, &grey) == NULL, "RpLightSetColor(NULL) is refused");
}

// Counts calls without ever being reached, which is the point: nothing may call
// back out of RpCollisionWorldForAllIntersections while it is unimplemented,
// least of all with a made-up triangle.
static int sWorldTrianglesSeen;

static RpCollisionTriangle* countWorldTriangleCB(RpIntersection*, RpWorldSector*,
                                                 RpCollisionTriangle* tri, RwReal, void*)
{
    sWorldTrianglesSeen++;
    return tri;
}

static void test_worlds()
{
    printf("RpWorld\n");

    // sup then inf, which is RwBBox's own order and the opposite of the one
    // most engines use.
    RwBBox bbox = { { 10.0f, 20.0f, 30.0f }, { -1.0f, -2.0f, -3.0f } };

    RpWorld* world = RpWorldCreate(&bbox);
    check(world != NULL, "RpWorldCreate");
    if (world == NULL)
    {
        return;
    }

    // The head of the struct is librw's, so this reads the type librw's own
    // World::create wrote.
    check(world->object.type == rpWORLD, "a new world is an rpWORLD object");
    check(reinterpret_cast<rw::World*>(world)->object.type == rpWORLD,
          "and an RpWorld* IS an rw::World*");

    // The plugin block. Everything checked here lives in memory librw allocated
    // for the port at the offset RpWorldPluginAttach asked for, so a wrong
    // offset shows up as one of these reading back something else.
    check(RpWorldGetBBox(world)->sup.x == 10.0f && RpWorldGetBBox(world)->sup.z == 30.0f &&
              RpWorldGetBBox(world)->inf.y == -2.0f,
          "the caller's bounding box lands in RpWorld::boundingBox");
    check(RpWorldGetNumMaterials(world) == 0, "a new world has no materials");
    check(world->matList.materials == NULL, "and no material array to free");
    check(world->rootSector == NULL, "and no root sector -- there is no world reader");
    check(RpWorldGetRenderOrder(world) == rpWORLDRENDERNARENDERORDER,
          "RpWorld::renderOrder starts at NA rather than at whatever rwMalloc returned");

    RpWorld* unbounded = RpWorldCreate(NULL);
    check(unbounded != NULL, "RpWorldCreate(NULL) is allowed -- an unbounded world");
    RpWorldDestroy(unbounded);

    // --- cameras -----------------------------------------------------------

    RwCamera* camera = RwCameraCreate();
    check(camera != NULL, "a camera to put in it");
    if (camera == NULL)
    {
        return;
    }

    check(RwCameraGetWorld(camera) == NULL, "which is in no world to start with");
    check(RpWorldAddCamera(world, camera) == world, "RpWorldAddCamera");
    check(RwCameraGetWorld(camera) == world, "and RwCameraGetWorld answers with the RpWorld");
    check(RpWorldAddCamera(world, camera) == NULL,
          "adding it twice is refused rather than asserted on");

    // curWorld is the reason RwCameraBeginUpdate mirrors librw's engine rather
    // than assigning its own: librw's beginUpdate chain sets currentWorld from
    // the camera's world, and until there was a world to put a camera in, that
    // could only be checked against NULL.
    //
    // The frame and the rasters are the same requirement as in test_cameras:
    // a real device's beginUpdate dereferences the camera's frame and renders
    // into its frame buffer. See the note there.
#ifndef RW_NULL
    RwCameraSetFrame(camera, RwFrameCreate());
    RwCameraSetRaster(camera, RwRasterCreate(640, 480, 0, rwRASTERTYPECAMERA));
    RwCameraSetZRaster(camera, RwRasterCreate(640, 480, 0, rwRASTERTYPEZBUFFER));
#endif

    RwCameraBeginUpdate(camera);
    check(RwEngineInstance->curWorld == world, "an update on it makes it RwEngineInstance->curWorld");
    RwCameraEndUpdate(camera);
    check(RwEngineInstance->curWorld == NULL, "and ending the update clears it");

    RpWorld* other = RpWorldCreate(&bbox);
    check(RpWorldRemoveCamera(other, camera) == NULL,
          "removing a camera from a world it is not in is refused");
    check(RpWorldRemoveCamera(world, camera) == world, "RpWorldRemoveCamera");
    check(RwCameraGetWorld(camera) == NULL, "and the camera is in no world again");
    RpWorldDestroy(other);

    // librw asserts a camera has left its world before it may be destroyed, so
    // this getting as far as returning TRUE is itself the check that the remove
    // above really unhooked it.
    check(RwCameraDestroy(camera) != FALSE, "the camera destroys cleanly afterwards");

    // --- lights ------------------------------------------------------------
    //
    // The two lists are the assertion that matters here. librw splits lights
    // into "local" (positioned) and "global" (directional and ambient) where
    // RenderWare splits them into lightList and directionalLightList, and the
    // mirror claims those are the same two lists under different names. Reading
    // them back through RenderWare's names is what checks that claim.

    RpLight* point = RpLightCreate(rpLIGHTPOINT);
    RpLight* directional = RpLightCreate(rpLIGHTDIRECTIONAL);
    check(point != NULL && directional != NULL, "a positioned light and a directional one");
    if (point == NULL || directional == NULL)
    {
        return;
    }

    check(rwLinkListEmpty(&world->lightList) && rwLinkListEmpty(&world->directionalLightList),
          "a new world's light lists are both empty");

    check(RpWorldAddLight(world, point) == world, "RpWorldAddLight, positioned");
    check(!rwLinkListEmpty(&world->lightList), "the point light lands in RpWorld::lightList");
    check(rwLinkListEmpty(&world->directionalLightList), "and not in the directional one");

    check(RpWorldAddLight(world, directional) == world, "RpWorldAddLight, directional");
    check(!rwLinkListEmpty(&world->directionalLightList),
          "the directional light lands in RpWorld::directionalLightList");

    check(RpWorldAddLight(world, point) == NULL, "adding a light twice is refused");
    check(RpWorldRemoveLight(NULL, point) == NULL, "RpWorldRemoveLight(NULL, ...) is refused");

    check(RpWorldRemoveLight(world, point) == world, "RpWorldRemoveLight");
    check(rwLinkListEmpty(&world->lightList), "and the list is empty again");
    check(RpWorldRemoveLight(world, point) == NULL,
          "removing it a second time is refused rather than corrupting the list");
    check(RpLightDestroy(point) != FALSE, "a removed light destroys cleanly");

    // --- refusals ----------------------------------------------------------

    check(RpWorldStreamRead(NULL) == NULL, "RpWorldStreamRead is not implemented and says so");

    RpIntersection isx;
    isx.type = rpINTERSECTSPHERE;
    isx.t.sphere.center.x = 0.0f;
    isx.t.sphere.center.y = 0.0f;
    isx.t.sphere.center.z = 0.0f;
    isx.t.sphere.radius = 1000.0f;
    sWorldTrianglesSeen = 0;
    check(RpCollisionWorldForAllIntersections(world, &isx, countWorldTriangleCB, NULL) == NULL,
          "RpCollisionWorldForAllIntersections is not implemented and says so");
    check(sWorldTrianglesSeen == 0, "and reports no triangles rather than invented ones");

    // --- destroy -----------------------------------------------------------
    //
    // The directional light and a clump are deliberately left in the world.
    // RenderWare would leave both holding a link into freed memory; the shim
    // detaches them, so that destroying either afterwards is safe instead of
    // tripping librw's assert at a call site with nothing to do with this one.
    //
    // The clump is made through librw rather than through RpClumpCreate,
    // because what is being checked is RpWorldDestroy's walk of the clump list,
    // not the clump shim.
    rw::Clump* clump = rw::Clump::create();
    reinterpret_cast<rw::World*>(world)->addClump(clump);
    check(clump->world == reinterpret_cast<rw::World*>(world), "a clump in the world too");

    check(RpWorldDestroy(world) != FALSE, "RpWorldDestroy with a light and a clump still in it");
    check(directional->world == NULL, "which took the light back out of it first");
    check(clump->world == NULL, "and the clump");
    check(RpLightDestroy(directional) != FALSE, "so the light still destroys cleanly");
    clump->destroy();

    check(RpWorldDestroy(NULL) == FALSE, "RpWorldDestroy(NULL) is refused");
    check(RpWorldAddLight(NULL, NULL) == NULL, "RpWorldAddLight(NULL, NULL) is refused");
    check(RpWorldAddCamera(NULL, NULL) == NULL, "RpWorldAddCamera(NULL, NULL) is refused");
}

// The null device swallows every render state, so what actually reaches librw
// cannot be read back out of it. These stand in for the device's own entry
// points for the length of one call, which is what makes the state-id mapping
// and the fog colour swizzle checkable without a backend.
static int sCapturedState;
static void* sCapturedValue;

static void captureSetRenderState(rw::int32 state, void* value)
{
    sCapturedState = state;
    sCapturedValue = value;
}

static void test_renderstate()
{
    printf("RwRenderState\n");

    // A Set has to be readable by the matching Get, because that is the whole
    // basis of xfont::set_render_state / restore_render_state and of every
    // other overlay in the game. librw cannot answer this: rw::GetRenderState
    // asks the device, and the null device answers 0 to everything.
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

    RwBlendFunction src = rwBLENDNABLEND;
    RwBlendFunction dst = rwBLENDNABLEND;
    check(RwRenderStateGet(rwRENDERSTATESRCBLEND, &src) != FALSE, "RwRenderStateGet(SRCBLEND)");
    check(RwRenderStateGet(rwRENDERSTATEDESTBLEND, &dst) != FALSE, "RwRenderStateGet(DESTBLEND)");
    check(src == rwBLENDSRCALPHA && dst == rwBLENDINVSRCALPHA,
          "a Get returns what the matching Set was given");

    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
    RwBool zwrite = 123;
    RwBool valpha = 123;
    RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &zwrite);
    RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &valpha);
    check(zwrite == TRUE && valpha == FALSE, "the boolean states round-trip");

    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
    RwTextureFilterMode filter = rwFILTERNAFILTERMODE;
    RwRenderStateGet(rwRENDERSTATETEXTUREFILTER, &filter);
    check(filter == rwFILTERLINEAR, "and so does the filter mode");

    // Recorded but not rendered: librw has no shade mode at all, so the Set
    // says FALSE. It still has to round-trip, because xFont.cpp:626 saves it
    // and xFont.cpp:649 puts it back.
    check(RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*)rwSHADEMODEFLAT) == FALSE,
          "SHADEMODE reports that librw did not take it");
    RwShadeMode shade = rwSHADEMODENASHADEMODE;
    check(RwRenderStateGet(rwRENDERSTATESHADEMODE, &shade) != FALSE && shade == rwSHADEMODEFLAT,
          "but it is still recorded, so xfont can restore it");

    check(RwRenderStateSet(rwRENDERSTATEFOGDENSITY, (void*)0) == FALSE,
          "FOGDENSITY is refused rather than recorded under a guessed encoding");
    RwUInt32 density = 0xCDCDCDCD;
    check(RwRenderStateGet(rwRENDERSTATEFOGDENSITY, &density) == FALSE && density == 0xCDCDCDCD,
          "and a Get for it leaves the caller's variable alone");

    // The combined address query answers only when the two axes agree, which is
    // what RenderWare does.
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSWRAP);
    RwTextureAddressMode addr = rwTEXTUREADDRESSNATEXTUREADDRESS;
    check(RwRenderStateGet(rwRENDERSTATETEXTUREADDRESS, &addr) != FALSE &&
              addr == rwTEXTUREADDRESSWRAP,
          "TEXTUREADDRESS sets and gets both axes");
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESSV, (void*)rwTEXTUREADDRESSCLAMP);
    check(RwRenderStateGet(rwRENDERSTATETEXTUREADDRESS, &addr) == FALSE,
          "and refuses the combined query once the axes differ");
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSWRAP);

    // What actually reaches librw. The device's setRenderState is replaced for
    // the length of these calls, because the null device keeps nothing.
    void (*saved)(rw::int32, void*) = rw::engine->device.setRenderState;
    rw::engine->device.setRenderState = captureSetRenderState;

    sCapturedState = -1;
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    check(sCapturedState == rw::VERTEXALPHA && sCapturedValue == (void*)1,
          "rwRENDERSTATEVERTEXALPHAENABLE reaches librw as VERTEXALPHA");

    sCapturedState = -1;
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
    check(sCapturedState == rw::CULLMODE && sCapturedValue == (void*)rw::CULLBACK,
          "and rwCULLMODECULLBACK reaches it as CULLBACK");

    // The one conversion in this file that would be invisible if it were wrong.
    // iCamera.cpp:373 packs the fog colour ARGB by hand; librw's backends read
    // red out of the LOW byte. Forwarding the word unchanged would swap red and
    // blue in every foggy level and nothing would fail.
    sCapturedState = -1;
    const RwUInt32 argb = 0xFF204080; // a=FF r=20 g=40 b=80
    RwRenderStateSet(rwRENDERSTATEFOGCOLOR, (void*)argb);
    check(sCapturedState == rw::FOGCOLOR, "rwRENDERSTATEFOGCOLOR reaches librw as FOGCOLOR");
    check(sCapturedValue == (void*)0xFF804020,
          "with red and blue swapped, which is the packing librw reads");

    rw::engine->device.setRenderState = saved;

    RwUInt32 fog = 0;
    check(RwRenderStateGet(rwRENDERSTATEFOGCOLOR, &fog) != FALSE && fog == argb,
          "and a Get hands back the same ARGB word the Set was given");

    check(RwRenderStateSet((RwRenderState)999, NULL) == FALSE, "an unknown state is refused");
    check(RwRenderStateGet(rwRENDERSTATESRCBLEND, NULL) == FALSE,
          "RwRenderStateGet(NULL) is refused");

    // RxRenderStateVectorLoadDriverState is the same copy, handed over whole.
    // xShadowSimple.cpp:687 unpacks bits 2 and 3 of Flags by hand, so those two
    // are what this checks.
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDDESTCOLOR);

    RxRenderStateVector rsv;
    memset(&rsv, 0xCD, sizeof(rsv));
    check(RxRenderStateVectorLoadDriverState(&rsv) == &rsv, "RxRenderStateVectorLoadDriverState");
    check(((rsv.Flags >> 2) & 1) == 1, "Flags bit 2 is z-write, as xShadowSimple reads it");
    check(((rsv.Flags >> 3) & 1) == 0, "Flags bit 3 is vertex alpha, as xShadowSimple reads it");
    check(rsv.SrcBlend == rwBLENDDESTCOLOR, "and the blend functions come across whole");
    check(RxRenderStateVectorLoadDriverState(NULL) == NULL,
          "RxRenderStateVectorLoadDriverState(NULL) is refused");
}

static int sIm2DPrim;
static void* sIm2DVerts;
static int sIm2DCount;
static int sIm3DPrim;
static int sIm3DEnds;

static int sIm2DIndexedPrim;
static int sIm2DIndexCount;

// The indexed form needed standing in for too, and that it did not is how this
// test faulted the first time a real backend was linked: the three stubs below
// covered im2DRenderPrimitive, im3DRenderPrimitive and im3DEnd, so the indexed
// call went to D3D9's real rasteriser -- outside any camera update, with no
// render target bound.
static void captureIm2DRenderIndexedPrimitive(rw::PrimitiveType type, void* verts,
                                              rw::int32 numVerts, void* indices,
                                              rw::int32 numIndices)
{
    (void)verts;
    (void)numVerts;
    (void)indices;
    sIm2DIndexedPrim = (int)type;
    sIm2DIndexCount = (int)numIndices;
}

static int sIm3DTransforms;

// The last of the four. im3DTransform builds the device's own transformed
// vertex buffer, so on D3D9 it is real work against real state -- the same
// reason the indexed 2D call needed standing in for.
static void captureIm3DTransform(void* verts, rw::int32 num, rw::Matrix* world, rw::uint32 flags)
{
    (void)verts;
    (void)num;
    (void)world;
    (void)flags;
    sIm3DTransforms++;
}

static void captureIm2DRenderPrimitive(rw::PrimitiveType type, void* verts, rw::int32 num)
{
    sIm2DPrim = type;
    sIm2DVerts = verts;
    sIm2DCount = num;
}

static void captureIm3DRenderPrimitive(rw::PrimitiveType type)
{
    sIm3DPrim = type;
}

static void captureIm3DEnd(void)
{
    sIm3DEnds++;
}

static void test_immediate()
{
    printf("RwIm2D / RwIm3D\n");

    // The null device's depth range. Real numbers, from the device rather than
    // from here -- but 0 and 1 only because there is no backend to have a real
    // depth buffer. xFont.cpp:425 and zGame.cpp:848 put their overlays at these.
    check(RwIm2DGetNearScreenZ() == 0.0f, "RwIm2DGetNearScreenZ comes from the device");
    check(RwIm2DGetFarScreenZ() == 1.0f, "RwIm2DGetFarScreenZ comes from the device");

    RwIm2DVertex quad[4];
    memset(quad, 0, sizeof(quad));

    void (*savedIm2D)(rw::PrimitiveType, void*, rw::int32) = rw::engine->device.im2DRenderPrimitive;
    void (*savedIm3D)(rw::PrimitiveType) = rw::engine->device.im3DRenderPrimitive;
    void (*savedEnd)(void) = rw::engine->device.im3DEnd;
    void (*savedIm2DIndexed)(rw::PrimitiveType, void*, rw::int32, void*, rw::int32) =
        rw::engine->device.im2DRenderIndexedPrimitive;
    rw::engine->device.im2DRenderIndexedPrimitive = captureIm2DRenderIndexedPrimitive;
    rw::engine->device.im2DRenderPrimitive = captureIm2DRenderPrimitive;
    void (*savedIm3DTransform)(void*, rw::int32, rw::Matrix*, rw::uint32) =
        rw::engine->device.im3DTransform;
    rw::engine->device.im3DTransform = captureIm3DTransform;
    rw::engine->device.im3DRenderPrimitive = captureIm3DRenderPrimitive;
    rw::engine->device.im3DEnd = captureIm3DEnd;

    sIm2DPrim = -1;
    check(RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, quad, 4) != FALSE, "RwIm2DRenderPrimitive");
    check(sIm2DPrim == rw::PRIMTYPETRISTRIP && sIm2DVerts == quad && sIm2DCount == 4,
          "the primitive type, the vertices and the count all arrive unchanged");

    check(RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, NULL, 4) == FALSE,
          "RwIm2DRenderPrimitive(NULL) is refused");
    check(RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, quad, 0) == FALSE,
          "and so is a zero-vertex primitive");

    RwImVertexIndex indices[6] = { 0, 1, 2, 0, 2, 3 };
    sIm2DIndexedPrim = -1;
    check(RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, quad, 4, indices, 6) != FALSE,
          "RwIm2DRenderIndexedPrimitive");
    check(sIm2DIndexedPrim == rw::PRIMTYPETRILIST && sIm2DIndexCount == 6,
          "the primitive type and index count arrive unchanged");
    check(RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, quad, 4, NULL, 6) == FALSE,
          "RwIm2DRenderIndexedPrimitive with no indices is refused");

    // Every one of the twenty call sites uses the result of RwIm3DTransform as
    // "may I render now", so a NULL here would silently stop every effect in
    // the game from drawing. Nothing dereferences it -- see the comment in
    // im.cpp.
    RwIm3DVertex verts[4];
    memset(verts, 0, sizeof(verts));
    sIm3DTransforms = 0;
    check(RwIm3DTransform(verts, 4, NULL, rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA) != NULL,
          "RwIm3DTransform reports success");
    check(sIm3DTransforms == 1, "and reaches the device once");
    check(RwIm3DTransform(NULL, 4, NULL, 0) == NULL, "RwIm3DTransform(NULL) is refused");
    check(RwIm3DTransform(verts, 0, NULL, 0) == NULL, "and so is a zero-vertex transform");

    sIm3DPrim = -1;
    check(RwIm3DRenderPrimitive(rwPRIMTYPETRILIST) != FALSE, "RwIm3DRenderPrimitive");
    check(sIm3DPrim == rw::PRIMTYPETRILIST, "the primitive type arrives unchanged");

    sIm3DEnds = 0;
    check(RwIm3DEnd() != FALSE, "RwIm3DEnd");
    check(sIm3DEnds == 1, "and reaches the device once");

    rw::engine->device.im2DRenderIndexedPrimitive = savedIm2DIndexed;
    rw::engine->device.im2DRenderPrimitive = savedIm2D;
    rw::engine->device.im3DTransform = savedIm3DTransform;
    rw::engine->device.im3DRenderPrimitive = savedIm3D;
    rw::engine->device.im3DEnd = savedEnd;
}

// A unit quad in the XZ plane, as two triangles, used by the geometry and
// intersection tests. Four vertices from (-1,0,-1) to (1,0,1); triangle 0 is
// the half with z < x, triangle 1 the half with z > x.
static RpGeometry* makeQuad()
{
    RpGeometry* geometry = RpGeometryCreate(4, 2, rpGEOMETRYPOSITIONS | rpGEOMETRYTEXTURED);
    if (geometry == NULL)
    {
        return NULL;
    }

    RwV3d* v = geometry->morphTarget[0].verts;
    v[0].x = -1.0f;
    v[0].y = 0.0f;
    v[0].z = -1.0f;
    v[1].x = 1.0f;
    v[1].y = 0.0f;
    v[1].z = -1.0f;
    v[2].x = 1.0f;
    v[2].y = 0.0f;
    v[2].z = 1.0f;
    v[3].x = -1.0f;
    v[3].y = 0.0f;
    v[3].z = 1.0f;

    RpGeometryTriangleSetVertexIndices(geometry, &geometry->triangles[0], 0, 1, 2);
    RpGeometryTriangleSetVertexIndices(geometry, &geometry->triangles[1], 0, 2, 3);
    return geometry;
}

static bool near(float a, float b)
{
    float d = a - b;
    return d > -0.0005f && d < 0.0005f;
}

static RpMaterial* countMaterialsCB(RpMaterial* material, void* data)
{
    (void)material;
    (*(int*)data)++;
    return material;
}

static RpMaterial* stopAfterFirstCB(RpMaterial* material, void* data)
{
    (void)material;
    (*(int*)data)++;
    return NULL;
}

static void test_geometry()
{
    printf("RpGeometry / RpMaterial / RpMorphTarget\n");

    RpGeometry* geometry = makeQuad();
    check(geometry != NULL, "RpGeometryCreate");
    if (geometry == NULL)
    {
        return;
    }

    // Read out of the RenderWare struct, which is the mirroring doing its job:
    // zFX.cpp and xJSP.cpp reach for exactly these fields.
    check(geometry->numVertices == 4 && geometry->numTriangles == 2,
          "a new geometry has the counts it was asked for");
    check(geometry->numMorphTargets == 1,
          "RpGeometryCreate makes the one morph target RenderWare's does");
    check(geometry->numTexCoordSets == 1, "rpGEOMETRYTEXTURED means one texture coordinate set");
    check(geometry->morphTarget != NULL && geometry->morphTarget[0].verts != NULL,
          "the morph target has vertices");
    check(geometry->morphTarget[0].parentGeom == geometry,
          "the morph target points back at its geometry");
    check(geometry->refCount == 1, "a new geometry starts with one reference");

    RwUInt16 a = 0;
    RwUInt16 b = 0;
    RwUInt16 c = 0;
    RpGeometryTriangleGetVertexIndices(geometry, &geometry->triangles[1], &a, &b, &c);
    check(a == 0 && b == 2 && c == 3, "RpGeometryTriangleGet/SetVertexIndices round-trip");

    // No material yet: librw writes 0xFFFF into a fresh triangle's index and
    // RenderWare reads that as -1, so this checks the two agree on "none".
    check(RpGeometryTriangleGetMaterial(geometry, &geometry->triangles[0]) == NULL,
          "a triangle with no material has no material");

    RpMaterial* material = reinterpret_cast<RpMaterial*>(rw::Material::create());
    check(material != NULL, "a material to put on it");
    if (material == NULL)
    {
        return;
    }

    RpGeometryTriangleSetMaterial(geometry, &geometry->triangles[0], material);
    check(geometry->matList.numMaterials == 1,
          "RpGeometryTriangleSetMaterial appends to the material list");
    check(RpGeometryTriangleGetMaterial(geometry, &geometry->triangles[0]) == material,
          "RpGeometryTriangleGetMaterial finds it again");
    check(material->refCount == 2, "the geometry took a reference on it");

    // The same material on a second triangle must not append it twice.
    RpGeometryTriangleSetMaterial(geometry, &geometry->triangles[1], material);
    check(geometry->matList.numMaterials == 1, "a material shared by two triangles appears once");

    int seen = 0;
    check(RpGeometryForAllMaterials(geometry, countMaterialsCB, &seen) == geometry,
          "RpGeometryForAllMaterials");
    check(seen == 1, "it visited the one material");

    RpMaterial* second = reinterpret_cast<RpMaterial*>(rw::Material::create());
    RpGeometryTriangleSetMaterial(geometry, &geometry->triangles[1], second);
    check(geometry->matList.numMaterials == 2, "a second material appends");

    seen = 0;
    RpGeometryForAllMaterials(geometry, stopAfterFirstCB, &seen);
    check(seen == 1, "a callback returning NULL stops the walk early");

    // RpMaterialSetTexture is reference counted, which is what lets
    // zParPTank.cpp hand over a texture it found in a dictionary.
    RwTexture* texture = RwTextureCreate(NULL);
    check(texture != NULL && texture->refCount == 1, "a texture for the material");
    RpMaterialSetTexture(material, texture);
    check(material->texture == texture, "RpMaterialSetTexture");
    check(texture->refCount == 2, "the material took a reference on the texture");
    RpMaterialSetTexture(material, NULL);
    check(material->texture == NULL && texture->refCount == 1,
          "setting it back to NULL gives the reference up again");
    RwTextureDestroy(texture);

    // Bounding sphere of the quad: centre at the origin, radius to a corner.
    // The morph target's own sphere is stamped first so that "it does not
    // store the result" is a real check and not a read of whatever
    // RpGeometryCreate left there.
    geometry->morphTarget[0].boundingSphere.radius = 99.0f;

    RwSphere sphere;
    sphere.center.x = 99.0f;
    sphere.radius = 99.0f;
    check(RpMorphTargetCalcBoundingSphere(&geometry->morphTarget[0], &sphere) ==
              &geometry->morphTarget[0],
          "RpMorphTargetCalcBoundingSphere");
    check(near(sphere.center.x, 0.0f) && near(sphere.center.y, 0.0f) && near(sphere.center.z, 0.0f),
          "the quad's bounding sphere is centred on the origin");
    check(near(sphere.radius, 1.41421f), "and reaches its corners");
    check(near(geometry->morphTarget[0].boundingSphere.radius, 99.0f),
          "it computes into the caller's sphere and does not store it");

    // Lock throws the mesh away and unlock rebuilds it, which is the half of
    // RenderWare's pair that matters here.
    check(RpGeometryUnlock(geometry) == geometry, "RpGeometryUnlock");
    check(geometry->mesh != NULL, "unlocking built the mesh");
    check(geometry->mesh->numMeshes == 2, "one mesh per material");

    check(RpGeometryLock(geometry, 1 /* rpGEOMETRYLOCKPOLYGONS */) == geometry, "RpGeometryLock");
    check(geometry->mesh == NULL, "locking the polygons threw the mesh away");
    check(geometry->lockedSinceLastInst & 1, "and recorded that it did");

    RpGeometryUnlock(geometry);
    check(geometry->mesh != NULL, "unlocking built it again");

    // A vertex-only lock leaves the mesh alone: the indices still describe the
    // triangles, only the positions moved. xCutscene.cpp locks this way every
    // frame it morphs a model.
    RpGeometryLock(geometry, 2 /* rpGEOMETRYLOCKVERTICES */);
    check(geometry->mesh != NULL, "locking only the vertices keeps the mesh");
    RpGeometryUnlock(geometry);

    reinterpret_cast<rw::Material*>(second)->destroy();
    reinterpret_cast<rw::Material*>(material)->destroy();
    reinterpret_cast<rw::Geometry*>(geometry)->destroy();
}

// What RpAtomicForAllIntersections handed back, for the checks below.
struct IsxLog
{
    int calls;
    RwInt32 lastIndex;
    RwReal lastDistance;
    RwV3d lastVertex0;
    RwV3d lastNormal;
};

static RpCollisionTriangle* logIsxCB(RpIntersection* intersection, RpCollisionTriangle* tri,
                                     RwReal distance, void* data)
{
    (void)intersection;
    IsxLog* log = (IsxLog*)data;
    log->calls++;
    log->lastIndex = tri->index;
    log->lastDistance = distance;
    log->lastVertex0 = *tri->vertices[0];
    log->lastNormal = tri->normal;
    return tri;
}

static RpCollisionTriangle* stopIsxCB(RpIntersection* intersection, RpCollisionTriangle* tri,
                                      RwReal distance, void* data)
{
    logIsxCB(intersection, tri, distance, data);
    return NULL;
}

static void test_atomics()
{
    printf("RpAtomic\n");

    RpAtomic* atomic = reinterpret_cast<RpAtomic*>(rw::Atomic::create());
    check(atomic != NULL, "an atomic to hang it all off");
    if (atomic == NULL)
    {
        return;
    }

    RwFrame* frame = RwFrameCreate();
    check(RpAtomicSetFrame(atomic, frame) == atomic, "RpAtomicSetFrame");
    check(RpAtomicGetFrame(atomic) == frame, "the atomic is on the frame");

    RpGeometry* geometry = makeQuad();
    RpMorphTargetCalcBoundingSphere(&geometry->morphTarget[0],
                                    &geometry->morphTarget[0].boundingSphere);

    check(RpAtomicSetGeometry(atomic, geometry, 0) == atomic, "RpAtomicSetGeometry");
    check(RpAtomicGetGeometry(atomic) == geometry, "the atomic has the geometry");
    check(geometry->refCount == 2, "and took a reference on it");
    check(near(RpAtomicGetBoundingSphere(atomic)->radius, 1.41421f),
          "it copied morph target 0's bounding sphere");

    // --- intersections ----------------------------------------------------
    //
    // The primitive goes in in WORLD space and the triangles come back in
    // OBJECT space. Moving the frame is what tells the two apart, and getting
    // it backwards is the failure that would make every collision in the game
    // happen at the origin.

    RwV3d ten = { 10.0f, 0.0f, 0.0f };
    RwFrameTranslate(frame, &ten, rwCOMBINEREPLACE);

    RpIntersection isx;
    IsxLog log;

    // A sphere half a unit above the quad, in world space, so over the moved
    // atomic rather than over the origin.
    memset(&log, 0, sizeof(log));
    isx.type = rpINTERSECTSPHERE;
    isx.t.sphere.center.x = 10.0f;
    isx.t.sphere.center.y = 0.5f;
    isx.t.sphere.center.z = 0.0f;
    isx.t.sphere.radius = 1.0f;
    check(RpAtomicForAllIntersections(atomic, &isx, logIsxCB, &log) == atomic,
          "RpAtomicForAllIntersections, sphere");
    check(log.calls == 2, "a sphere over the middle of the quad hits both triangles");
    check(near(log.lastDistance, 0.5f), "and reports the distance from its centre");
    check(near(log.lastVertex0.x, -1.0f), "the triangle comes back in object space");
    check(near(log.lastNormal.y, 1.0f) || near(log.lastNormal.y, -1.0f),
          "with a unit normal off the quad's plane");

    // Same sphere at the origin: the atomic is ten units away, so nothing.
    memset(&log, 0, sizeof(log));
    isx.t.sphere.center.x = 0.0f;
    RpAtomicForAllIntersections(atomic, &isx, logIsxCB, &log);
    check(log.calls == 0, "and the frame is honoured -- at the origin it misses entirely");

    // A line straight down through triangle 0's half of the quad. t comes back
    // normalised along the segment, which is what rayHitsEnvCB scales.
    memset(&log, 0, sizeof(log));
    isx.type = rpINTERSECTLINE;
    isx.t.line.start.x = 10.3f;
    isx.t.line.start.y = 1.0f;
    isx.t.line.start.z = -0.3f;
    isx.t.line.end.x = 10.3f;
    isx.t.line.end.y = -1.0f;
    isx.t.line.end.z = -0.3f;
    RpAtomicForAllIntersections(atomic, &isx, logIsxCB, &log);
    check(log.calls == 1, "a line through one half of the quad hits one triangle");
    check(log.lastIndex == 0, "and it is the triangle that half belongs to");
    check(near(log.lastDistance, 0.5f), "at the halfway point of the segment");

    // Off the edge of the quad entirely.
    memset(&log, 0, sizeof(log));
    isx.t.line.start.x = 20.0f;
    isx.t.line.end.x = 20.0f;
    RpAtomicForAllIntersections(atomic, &isx, logIsxCB, &log);
    check(log.calls == 0, "a line beside the quad hits nothing");

    // A box around the whole quad, again in world space.
    memset(&log, 0, sizeof(log));
    isx.type = rpINTERSECTBOX;
    isx.t.box.inf.x = 9.0f;
    isx.t.box.inf.y = -1.0f;
    isx.t.box.inf.z = -2.0f;
    isx.t.box.sup.x = 11.0f;
    isx.t.box.sup.y = 1.0f;
    isx.t.box.sup.z = 2.0f;
    RpAtomicForAllIntersections(atomic, &isx, logIsxCB, &log);
    check(log.calls == 2, "a box around the quad hits both triangles");

    // The early stop iCollide.cpp uses once its collision array is full.
    memset(&log, 0, sizeof(log));
    isx.type = rpINTERSECTSPHERE;
    isx.t.sphere.center.x = 10.0f;
    RpAtomicForAllIntersections(atomic, &isx, stopIsxCB, &log);
    check(log.calls == 1, "a callback returning NULL stops the walk early");

    // Neither of these is a triangle query on either side.
    memset(&log, 0, sizeof(log));
    isx.type = rpINTERSECTPOINT;
    RpAtomicForAllIntersections(atomic, &isx, logIsxCB, &log);
    check(log.calls == 0, "a point intersection against an atomic is refused");

    RpAtomicSetFrame(atomic, NULL);
    RwFrameDestroy(frame);
    reinterpret_cast<rw::Geometry*>(geometry)->destroy();
    reinterpret_cast<rw::Atomic*>(atomic)->destroy();
}

static void test_skin()
{
    printf("RpSkin\n");

    RpGeometry* geometry = makeQuad();
    if (geometry == NULL)
    {
        return;
    }

    check(RpSkinGeometryGetSkin(geometry) == NULL, "an unskinned geometry has no skin");
    check(RpSkinGetNumBones(NULL) == 0, "RpSkinGetNumBones(NULL) is refused");

    // There is no RpSkinGeometrySetSkin on the game's list and no skinned
    // model to stream in here, so the skin is built the way librw's own stream
    // reader builds one: allocate, init for the bone and vertex counts, attach.
    // What is being checked is the four accessors' strides, which is where a
    // wrong cast would silently mix up bones.
    rw::Skin* skin = rwNewT(rw::Skin, 1, rw::MEMDUR_EVENT | rw::ID_SKIN);
    memset(skin, 0, sizeof(*skin));
    skin->init(3, 3, geometry->numVertices);
    rw::Skin::set(reinterpret_cast<rw::Geometry*>(geometry), skin);

    check(RpSkinGeometryGetSkin(geometry) == reinterpret_cast<RpSkin*>(skin),
          "RpSkinGeometryGetSkin finds the skin in the geometry's plugin block");

    RpSkin* rpskin = RpSkinGeometryGetSkin(geometry);
    check(RpSkinGetNumBones(rpskin) == 3, "RpSkinGetNumBones");

    // Bone 1's matrix is the second sixteen floats.
    skin->inverseMatrices[16 + 12] = 7.0f; // bone 1, pos.x
    const RwMatrix* mats = RpSkinGetSkinToBoneMatrices(rpskin);
    check(mats != NULL && near(mats[1].pos.x, 7.0f),
          "RpSkinGetSkinToBoneMatrices strides one RwMatrix per bone");

    // Vertex 2's weights are the third group of four floats.
    skin->weights[2 * 4 + 1] = 0.25f;
    const RwMatrixWeights* weights = RpSkinGetVertexBoneWeights(rpskin);
    check(weights != NULL && near(weights[2].w1, 0.25f),
          "RpSkinGetVertexBoneWeights strides four floats per vertex");

    // Vertex 2's bone indices are the third group of four bytes, and the game
    // unpacks index j with (word >> 8*j) & 0xff.
    skin->indices[2 * 4 + 0] = 5;
    skin->indices[2 * 4 + 3] = 9;
    const RwUInt32* indices = RpSkinGetVertexBoneIndices(rpskin);
    check(indices != NULL && ((indices[2] >> 0) & 0xFF) == 5 && ((indices[2] >> 24) & 0xFF) == 9,
          "RpSkinGetVertexBoneIndices packs four of librw's index bytes per vertex");

    // RpSkinAtomicSetType picks a pipeline. There is only one on this side --
    // librw's Skin::setPipeline casts the type to void -- so what is checked is
    // that the atomic ends up on the skin pipeline at all.
    RpAtomic* atomic = reinterpret_cast<RpAtomic*>(rw::Atomic::create());
    check(RpSkinAtomicSetType(atomic, rpSKINTYPEMATFX) == atomic, "RpSkinAtomicSetType");
    check(atomic->pipeline != NULL &&
              reinterpret_cast<void*>(atomic->pipeline) ==
                  reinterpret_cast<void*>(rw::skinGlobals.pipelines[rw::platform]),
          "it put the atomic on librw's skin pipeline");

    reinterpret_cast<rw::Atomic*>(atomic)->destroy();
    reinterpret_cast<rw::Geometry*>(geometry)->destroy();
}

static void test_matfx()
{
    printf("RpMatFX\n");

    RpMaterial* material = reinterpret_cast<RpMaterial*>(rw::Material::create());
    check(material != NULL, "a material to put an effect on");
    if (material == NULL)
    {
        return;
    }

    check(RpMatFXMaterialGetEffects(material) == rpMATFXEFFECTNULL,
          "a material with no effect block has no effect");

    check(RpMatFXMaterialSetEffects(material, rpMATFXEFFECTENVMAP) == material,
          "RpMatFXMaterialSetEffects");
    check(RpMatFXMaterialGetEffects(material) == rpMATFXEFFECTENVMAP,
          "RpMatFXMaterialGetEffects reads it back");

    RwTexture* env = RwTextureCreate(NULL);
    RwFrame* frame = RwFrameCreate();
    check(RpMatFXMaterialSetupEnvMap(material, env, frame, FALSE, 0.75f) == material,
          "RpMatFXMaterialSetupEnvMap");

    rw::MatFX* fx = rw::MatFX::get(reinterpret_cast<rw::Material*>(material));
    check(fx != NULL, "the effect block exists");
    check(reinterpret_cast<RwTexture*>(fx->getEnvTexture()) == env, "it kept the texture");
    check(reinterpret_cast<RwFrame*>(fx->getEnvFrame()) == frame, "and the frame");
    check(near(fx->getEnvCoefficient(), 0.75f), "and the coefficient");
    check(env->refCount == 2, "setting an env map texture takes a reference on it");

    check(RpMatFXMaterialSetEnvMapCoefficient(material, 0.25f) == material,
          "RpMatFXMaterialSetEnvMapCoefficient");
    check(near(fx->getEnvCoefficient(), 0.25f), "which is what xFX.cpp calls every frame");

    // The bump map lives in the other slot, and only when the effect asks for
    // both. On an env-map-only material a bump setup is a no-op on both sides.
    check(RpMatFXMaterialSetupBumpMap(material, NULL, NULL, 0.5f) == material,
          "RpMatFXMaterialSetupBumpMap on a material with no bump effect is harmless");
    check(near(fx->getEnvCoefficient(), 0.25f), "and left the env map alone");

    RpMatFXMaterialSetEffects(material, rpMATFXEFFECTBUMPENVMAP);
    RwTexture* bump = RwTextureCreate(NULL);
    RpMatFXMaterialSetupBumpMap(material, bump, frame, 0.5f);
    check(near(fx->getBumpCoefficient(), 0.5f), "RpMatFXMaterialSetupBumpMap sets the coefficient");
    check(reinterpret_cast<RwTexture*>(fx->getBumpTexture()) == bump, "and the texture");

    check(RpMatFXMaterialSetBumpMapCoefficient(material, 0.125f) == material,
          "RpMatFXMaterialSetBumpMapCoefficient");
    check(near(fx->getBumpCoefficient(), 0.125f), "reads back");

    RpAtomic* atomic = reinterpret_cast<RpAtomic*>(rw::Atomic::create());
    check(RpMatFXAtomicEnableEffects(atomic) == atomic, "RpMatFXAtomicEnableEffects");
    check(reinterpret_cast<void*>(atomic->pipeline) ==
              reinterpret_cast<void*>(rw::matFXGlobals.pipelines[rw::platform]),
          "it put the atomic on librw's material-effects pipeline");

    reinterpret_cast<rw::Atomic*>(atomic)->destroy();
    RwFrameDestroy(frame);
    reinterpret_cast<rw::Material*>(material)->destroy();
    RwTextureDestroy(bump);
    RwTextureDestroy(env);
}

static void test_ptank()
{
    printf("RpPTank\n");

    // Structure of arrays, which is what xPtankPool.cpp asks for: each cluster
    // is its own contiguous block and strides by its own size alone.
    RwUInt32 flags = rpPTANKDFLAGPOSITION | rpPTANKDFLAGCOLOR | rpPTANKDFLAGVTX2TEXCOORDS |
                     rpPTANKDFLAGSTRUCTURE;
    RpAtomic* ptank = RpPTankAtomicCreate(64, flags, 0);
    check(ptank != NULL, "RpPTankAtomicCreate");
    if (ptank == NULL)
    {
        return;
    }

    RpPTankAtomicExtPrv* ext = RPATOMICPTANKPLUGINDATA(ptank);
    check(ext != NULL, "the tank is reachable through RPATOMICPTANKPLUGINDATA");
    check(ext->maxPCount == 64 && ext->actPCount == 0, "with the particle count it was given");
    check(ext->isAStructure != FALSE, "and in structure-of-arrays form");
    check(ext->publicData.format.numClusters == 3, "three clusters were asked for");

    // xPtankPool.cpp and zParPTank.cpp both read this, and a ptank with no
    // material would fault there rather than here.
    check(ptank->geometry != NULL, "a ptank has a geometry");
    check(ptank->geometry->numVertices == 64 * 4, "four billboard vertices per particle");
    check(ptank->geometry->numTriangles == 64 * 2, "two triangles per particle");
    check(ptank->geometry->matList.numMaterials == 1 &&
              ptank->geometry->matList.materials[0] != NULL,
          "and a material for the game to hang a texture on");

    RpPTankLockStruct pos;
    RpPTankLockStruct uv;
    memset(&pos, 0, sizeof(pos));
    memset(&uv, 0, sizeof(uv));

    check(RpPTankAtomicLock(ptank, &pos, rpPTANKDFLAGPOSITION, rpPTANKLOCKWRITE) != FALSE,
          "RpPTankAtomicLock, positions");
    check(pos.data != NULL && pos.stride == (RwInt32)sizeof(RwV3d),
          "a structure-of-arrays position cluster strides one RwV3d");

    check(RpPTankAtomicLock(ptank, &uv, rpPTANKDFLAGVTX2TEXCOORDS, rpPTANKLOCKWRITE) != FALSE,
          "RpPTankAtomicLock, texture coordinates");
    check(uv.data != NULL && uv.stride == (RwInt32)(2 * sizeof(RwTexCoords)),
          "and the UV cluster strides two RwTexCoords");
    check(uv.data != pos.data, "the two clusters are different blocks");

    // A cluster this ptank was not created with, and a multi-cluster lock,
    // are both refused rather than answered with something plausible.
    RpPTankLockStruct nope;
    check(RpPTankAtomicLock(ptank, &nope, rpPTANKDFLAGNORMAL, rpPTANKLOCKWRITE) == FALSE,
          "locking a cluster the format does not have is refused");
    check(RpPTankAtomicLock(ptank, &nope, rpPTANKDFLAGPOSITION | rpPTANKDFLAGCOLOR,
                            rpPTANKLOCKWRITE) == FALSE,
          "locking two clusters at once is refused");

    // The game writes through the pointers it was handed, so this is what a
    // particle update actually does.
    *(RwV3d*)(pos.data + 3 * pos.stride) = ptank->geometry->morphTarget[0].verts[0];
    ((RwTexCoords*)(uv.data + 3 * uv.stride))[1].u = 0.5f;

    check(RpPTankAtomicUnlock(ptank) == ptank, "RpPTankAtomicUnlock");
    check(ext->lockFlags == 0, "which clears the lock");
    check((ext->instFlags & rpPTANKIFLAGPOSITION) && (ext->instFlags & rpPTANKIFLAGVTX2TEXCOORDS),
          "and marks both written clusters for re-instancing");
    check(!(ext->instFlags & rpPTANKIFLAGCOLOR), "but not the one that was never locked");

    // Reading back through the cluster the game kept: the data survived.
    check(near(((RwTexCoords*)(ext->publicData.clusters[RPPTANKSIZEVTX2TEXCOORDS].data +
                               3 * uv.stride))[1]
                   .u,
               0.5f),
          "what was written through the lock is still there afterwards");

    RpPTankAtomicDestroy(ptank);

    // Array form -- zParPTank.cpp's -- interleaves every cluster into one
    // record per particle, so both clusters stride by the whole record.
    RpAtomic* aos = RpPTankAtomicCreate(
        16, rpPTANKDFLAGPOSITION | rpPTANKDFLAGVTX2TEXCOORDS | rpPTANKDFLAGARRAY, 0);
    check(aos != NULL, "RpPTankAtomicCreate, array form");
    if (aos == NULL)
    {
        return;
    }

    RpPTankLockStruct aosPos;
    RpPTankLockStruct aosUv;
    RpPTankAtomicLock(aos, &aosPos, rpPTANKDFLAGPOSITION, rpPTANKLOCKWRITE);
    RpPTankAtomicLock(aos, &aosUv, rpPTANKDFLAGVTX2TEXCOORDS, rpPTANKLOCKWRITE);
    check(aosPos.stride == aosUv.stride, "an array-form ptank strides both clusters the same");
    check(aosPos.stride >= (RwInt32)(sizeof(RwV3d) + 2 * sizeof(RwTexCoords)),
          "by at least one whole record");
    check(aosUv.data > aosPos.data && aosUv.data < aosPos.data + aosPos.stride,
          "and interleaves them inside it");
    RpPTankAtomicUnlock(aos);

    RpPTankAtomicDestroy(aos);

    check(RpPTankAtomicCreate(0, rpPTANKDFLAGPOSITION, 0) == NULL,
          "a ptank with no particles is refused");
    check(RpPTankAtomicLock(NULL, &aosPos, rpPTANKDFLAGPOSITION, rpPTANKLOCKWRITE) == FALSE,
          "RpPTankAtomicLock(NULL, ...) is refused");

    // Not exercised: instancing and rendering. Turning particle positions into
    // billboard vertices needs the camera's right and up vectors, and there is
    // no camera on this side yet. A ptank created here draws nothing rather
    // than drawing something wrong -- its vertices are zeroed at create.
}

static RpAtomic* countAtomicCB(RpAtomic* atomic, void* data)
{
    (void)atomic;
    (*(int*)data)++;
    return atomic;
}

static RpAtomic* stopAtomicCB(RpAtomic* atomic, void* data)
{
    (void)atomic;
    (*(int*)data)++;
    return NULL;
}

static RpAtomic* recordAtomicCB(RpAtomic* atomic, void* data)
{
    RpAtomic*** cursor = (RpAtomic***)data;
    **cursor = atomic;
    (*cursor)++;
    return atomic;
}

// An atomic on its own child frame under `root`, parked at x = `x` so that the
// stream round trip below can tell one from another: atomics carry no name, but
// their frames carry a matrix.
static RpAtomic* makeClumpAtomic(RwFrame* root, RpGeometry* geometry, float x)
{
    RpAtomic* atomic = reinterpret_cast<RpAtomic*>(rw::Atomic::create());
    if (atomic == NULL)
    {
        return NULL;
    }

    RwFrame* frame = RwFrameCreate();
    RwV3d t = { x, 0.0f, 0.0f };
    RwFrameTranslate(frame, &t, rwCOMBINEREPLACE);
    reinterpret_cast<rw::Frame*>(root)->addChild(reinterpret_cast<rw::Frame*>(frame));

    RpAtomicSetFrame(atomic, frame);
    RpAtomicSetGeometry(atomic, geometry, 0);
    return atomic;
}

static void test_clumps()
{
    printf("RpClump\n");

    RpClump* clump = reinterpret_cast<RpClump*>(rw::Clump::create());
    check(clump != NULL, "a clump to hang it all off");
    if (clump == NULL)
    {
        return;
    }

    // RpClumpCreate is not on the 112-function list -- nothing in the game
    // creates a clump except by reading one -- so the clump comes from librw
    // and the mirroring is what makes RpClumpSetFrame, a macro over
    // rwObjectSetParent, reach the right word.
    RwFrame* root = RwFrameCreate();
    RpClumpSetFrame(clump, root);
    check(RpClumpGetFrame(clump) == root, "RpClumpSetFrame lands where RpClumpGetFrame reads");
    check(RpClumpGetNumAtomics(clump) == 0, "a new clump has no atomics");

    // One geometry shared by all three, so that its reference count can say
    // afterwards whether RpClumpDestroy really took the atomics with it.
    RpGeometry* geometry = makeQuad();
    RpAtomic* a0 = makeClumpAtomic(root, geometry, 1.0f);
    RpAtomic* a1 = makeClumpAtomic(root, geometry, 2.0f);
    RpAtomic* a2 = makeClumpAtomic(root, geometry, 3.0f);
    check(a0 != NULL && a1 != NULL && a2 != NULL, "three atomics to put in it");
    if (a0 == NULL || a1 == NULL || a2 == NULL)
    {
        return;
    }
    check(geometry->refCount == 4, "each atomic took a reference on the shared geometry");

    check(RpClumpAddAtomic(clump, a0) == clump, "RpClumpAddAtomic");
    RpClumpAddAtomic(clump, a1);
    RpClumpAddAtomic(clump, a2);
    check(RpClumpGetNumAtomics(clump) == 3, "RpClumpGetNumAtomics counts them");
    check(RpAtomicGetClump(a1) == clump, "and each atomic points back at the clump");

    int calls = 0;
    check(RpClumpForAllAtomics(clump, countAtomicCB, &calls) == clump, "RpClumpForAllAtomics");
    check(calls == 3, "visits every atomic");

    calls = 0;
    RpClumpForAllAtomics(clump, stopAtomicCB, &calls);
    check(calls == 1, "and stops early when a callback returns NULL");

    // The deviation from librw, checked directly: RenderWare's RpClumpAddAtomic
    // inserts at the HEAD (rwLinkListAddLLLink in src/rwsdk/world/baclump.c),
    // where librw's Clump::addAtomic appends.
    RpAtomic* seen[3] = { NULL, NULL, NULL };
    RpAtomic** cursor = seen;
    RpClumpForAllAtomics(clump, recordAtomicCB, &cursor);
    check(seen[0] == a2 && seen[1] == a1 && seen[2] == a0,
          "RpClumpAddAtomic inserts at the head, as baclump.c does");

    // ...and the reason it has to. This is xJSP.cpp:171-177 exactly: every
    // atomic of one clump is moved into another, walking the array backwards,
    // and the merged clump has to end up with them in their original order
    // because xJSP indexes its baked strip vectors by position in that list.
    // With an appending add, this check reverses.
    RpClump* merged = reinterpret_cast<RpClump*>(rw::Clump::create());
    bool removeAnswered = true;
    for (int i = 2; i >= 0; i--)
    {
        removeAnswered = removeAnswered && RpClumpRemoveAtomic(clump, seen[i]) == clump;
        RpClumpAddAtomic(merged, seen[i]);
    }
    check(removeAnswered, "RpClumpRemoveAtomic answers with the clump");
    check(RpClumpGetNumAtomics(clump) == 0, "and the clump they came off ends up empty");
    check(RpAtomicGetClump(a1) == merged, "while the moved atomics point at the new one");

    RpAtomic* seenAgain[3] = { NULL, NULL, NULL };
    cursor = seenAgain;
    RpClumpForAllAtomics(merged, recordAtomicCB, &cursor);
    check(seenAgain[0] == seen[0] && seenAgain[1] == seen[1] && seenAgain[2] == seen[2],
          "moving every atomic into another clump preserves their order, as xJSP.cpp needs");

    // Move them back. The emptied clump is freed through librw rather than
    // RpClumpDestroy, which would want a frame hierarchy this one never had.
    for (int i = 2; i >= 0; i--)
    {
        RpClumpRemoveAtomic(merged, seen[i]);
        RpClumpAddAtomic(clump, seen[i]);
    }
    check(RpAtomicGetClump(a1) == clump, "and moving them back clears it again");
    reinterpret_cast<rw::Clump*>(merged)->destroy();

    RpClumpRemoveAtomic(clump, a1);
    check(RpClumpGetNumAtomics(clump) == 2, "RpClumpRemoveAtomic drops the count");
    check(RpAtomicGetClump(a1) == NULL, "and clears the atomic's clump pointer");
    RpClumpAddAtomic(clump, a1);

    check(RpClumpGetNumAtomics(NULL) == 0, "RpClumpGetNumAtomics(NULL) is refused");
    check(RpClumpForAllAtomics(NULL, countAtomicCB, &calls) == NULL,
          "RpClumpForAllAtomics(NULL, ...) is refused");
    check(RpClumpStreamRead(NULL) == NULL, "RpClumpStreamRead(NULL) is refused");
    check(RpClumpDestroy(NULL) == FALSE, "RpClumpDestroy(NULL) is refused");

    // --- the stream round trip --------------------------------------------
    //
    // The order question RpClumpStreamRead cannot answer from retail source,
    // asked of the shim instead: librw writes the atomics in list order and
    // reads them back in file order, so a clump that goes out and comes back
    // has the atomics in the same sequence. The atomics are told apart by where
    // their frames sit, which is the only thing about them that survives the
    // trip.
    RpAtomic* before[3] = { NULL, NULL, NULL };
    cursor = before;
    RpClumpForAllAtomics(clump, recordAtomicCB, &cursor);

    RwMemory mem = { NULL, 0 };
    RwStream* out = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMWRITE, &mem);
    check(out != NULL, "a memory stream to write the clump into");
    if (out == NULL)
    {
        return;
    }
    bool wrote = reinterpret_cast<rw::Clump*>(clump)->streamWrite(out) != 0;
    RwStreamClose(out, &mem);
    check(wrote && mem.start != NULL, "librw wrote the clump out");

    if (wrote)
    {
        // Exactly the shape of iModel.cpp:143 and xJSP.cpp:154: find the chunk
        // first, then read. RpClumpStreamRead picks up inside the header.
        RwStream* in = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &mem);
        check(RwStreamFindChunk(in, rwID_CLUMP, NULL, NULL) != FALSE,
              "RwStreamFindChunk finds the clump chunk");

        RpClump* readBack = RpClumpStreamRead(in);
        RwStreamClose(in, NULL);
        check(readBack != NULL, "RpClumpStreamRead");

        if (readBack != NULL)
        {
            check(RpClumpGetNumAtomics(readBack) == 3, "with all three atomics");
            check(RpClumpGetFrame(readBack) != NULL, "and a root frame");

            RpAtomic* got[3] = { NULL, NULL, NULL };
            cursor = got;
            RpClumpForAllAtomics(readBack, recordAtomicCB, &cursor);
            check(got[0] != NULL && got[1] != NULL && got[2] != NULL,
                  "the read-back atomics enumerate");
            if (got[2] != NULL)
            {
                // The atomics come back as new objects, so they are matched up
                // by where their frames sit -- which is why each one was parked
                // at a different x.
                check(near(RpAtomicGetFrame(got[0])->modelling.pos.x,
                           RpAtomicGetFrame(before[0])->modelling.pos.x) &&
                          near(RpAtomicGetFrame(got[1])->modelling.pos.x,
                               RpAtomicGetFrame(before[1])->modelling.pos.x) &&
                          near(RpAtomicGetFrame(got[2])->modelling.pos.x,
                               RpAtomicGetFrame(before[2])->modelling.pos.x),
                      "in the order they were written, not reversed");
                check(RpAtomicGetGeometry(got[0]) != geometry,
                      "each with a geometry of its own, read from the stream");
            }

            check(RpClumpDestroy(readBack) != FALSE, "RpClumpDestroy on the read-back clump");
        }
    }

    RwFree(mem.start);

    // RpClumpDestroy takes the atomics, and the atomics drop their references
    // on the shared geometry. Reading refCount afterwards is safe precisely
    // because the count is not expected to reach zero.
    check(RpClumpDestroy(clump) != FALSE, "RpClumpDestroy");
    check(geometry->refCount == 1, "it destroyed the atomics, which released the geometry");
    reinterpret_cast<rw::Geometry*>(geometry)->destroy();

    // The frames went with it: root was the clump's frame, and RpClumpDestroy
    // destroys the whole hierarchy. Nothing here can prove a freed frame is
    // gone without reading it, so what is checked is that librw's allocator
    // saw the frees -- four frames, three atomics and the clump.
    check(sNumFree > 0, "and the frame hierarchy under it");
}

// --- Rt --------------------------------------------------------------------
//
// The intersection pair is exercised directly here as well as through
// RpAtomicForAllIntersections in test_atomics, because xClumpColl.cpp calls
// them by name against a collision tree and never goes through an atomic.

static void test_intersections()
{
    printf("Rt intersection\n");

    // The same quad's first triangle, in the xz plane. cross(v1-v0, v2-v0)
    // points down, which is the winding xClumpColl.cpp would hand over for a
    // ceiling.
    RwV3d v0 = { -1.0f, 0.0f, -1.0f };
    RwV3d v1 = { 1.0f, 0.0f, -1.0f };
    RwV3d v2 = { 1.0f, 0.0f, 1.0f };

    RwSphere sphere = { { 0.0f, 0.5f, 0.0f }, 1.0f };
    RwV3d normal;
    RwReal distance = -1.0f;

    check(RtIntersectionSphereTriangle(&sphere, &v0, &v1, &v2, &normal, &distance) != FALSE,
          "RtIntersectionSphereTriangle, over the face");
    check(near(distance, 0.5f), "reports the distance from the centre to the triangle");
    check(near(normal.x, 0.0f) && near(normal.y, -1.0f) && near(normal.z, 0.0f),
          "and the unit normal of the winding it was given");

    // Just out of reach above the face: the radius is what decides, and the
    // distance still comes back.
    sphere.center.y = 2.0f;
    check(RtIntersectionSphereTriangle(&sphere, &v0, &v1, &v2, &normal, &distance) == FALSE,
          "a sphere above the face by more than its radius misses");
    check(near(distance, 2.0f), "and still reports how far away it was");

    // Beyond the edge, in the plane. This is the case a plane-distance test
    // would get wrong: the sphere is zero units from the triangle's PLANE and
    // four units from the triangle.
    sphere.center.x = 5.0f;
    sphere.center.y = 0.0f;
    sphere.center.z = 0.0f;
    check(RtIntersectionSphereTriangle(&sphere, &v0, &v1, &v2, &normal, &distance) == FALSE,
          "a sphere in the plane but off the edge misses");
    check(near(distance, 4.0f), "at the distance to the nearest edge, not to the plane");

    // Touching a vertex exactly, which is where the Voronoi-region form earns
    // its keep.
    sphere.center.x = -1.5f;
    sphere.center.y = 0.0f;
    sphere.center.z = -1.0f;
    sphere.radius = 0.5f;
    check(RtIntersectionSphereTriangle(&sphere, &v0, &v1, &v2, &normal, &distance) != FALSE,
          "a sphere just reaching a vertex hits");
    check(near(distance, 0.5f), "at exactly its radius");

    check(RtIntersectionSphereTriangle(NULL, &v0, &v1, &v2, &normal, &distance) == FALSE,
          "RtIntersectionSphereTriangle(NULL, ...) is refused");

    // --- the box ----------------------------------------------------------

    RwBBox bbox;
    bbox.inf.x = -2.0f;
    bbox.inf.y = -1.0f;
    bbox.inf.z = -2.0f;
    bbox.sup.x = 2.0f;
    bbox.sup.y = 1.0f;
    bbox.sup.z = 2.0f;
    check(RtIntersectionBBoxTriangle(&bbox, &v0, &v1, &v2) != FALSE,
          "RtIntersectionBBoxTriangle, box around the triangle");

    // Beside it: the box's own face normals separate.
    bbox.inf.x = 5.0f;
    bbox.sup.x = 6.0f;
    check(RtIntersectionBBoxTriangle(&bbox, &v0, &v1, &v2) == FALSE, "a box beside it misses");

    // Above it: separated on y alone.
    bbox.inf.x = -2.0f;
    bbox.sup.x = 2.0f;
    bbox.inf.y = 1.0f;
    bbox.sup.y = 2.0f;
    check(RtIntersectionBBoxTriangle(&bbox, &v0, &v1, &v2) == FALSE, "a box above it misses");

    // The case that separates a real triangle test from a bounding-box test:
    // this box is inside the triangle's AABB and outside the triangle, on the
    // diagonal edge. Only an edge-cross-edge axis rejects it.
    bbox.inf.x = -0.95f;
    bbox.inf.y = -0.1f;
    bbox.inf.z = 0.5f;
    bbox.sup.x = -0.55f;
    bbox.sup.y = 0.1f;
    bbox.sup.z = 0.9f;
    check(RtIntersectionBBoxTriangle(&bbox, &v0, &v1, &v2) == FALSE,
          "a box inside the triangle's bounds but past its diagonal misses");

    check(RtIntersectionBBoxTriangle(NULL, &v0, &v1, &v2) == FALSE,
          "RtIntersectionBBoxTriangle(NULL, ...) is refused");
}

static void test_slerp()
{
    printf("RtQuat\n");

    const float root2 = 0.70710678f;

    // Identity, and a quarter turn about y. A quaternion's angle is half the
    // rotation's, so these two are pi/4 apart.
    RtQuat from = { { 0.0f, 0.0f, 0.0f }, 1.0f };
    RtQuat to = { { 0.0f, root2, 0.0f }, root2 };

    RtQuatSlerpCache cache;
    memset(&cache, 0xCD, sizeof(cache));
    RtQuatSetupSlerpCache(&from, &to, &cache);
    check(cache.nearlyZeroOm == FALSE, "RtQuatSetupSlerpCache takes the slerp path");
    check(near(cache.omega, 0.78539816f), "and caches the half-angle between them");

    // The cached quaternions are the originals divided by sin(omega), which is
    // the division RtQuatSlerpMacro does not do.
    check(near(cache.raFrom.real, 1.0f / 0.70710678f),
          "raFrom is the initial quaternion scaled by 1/sin(omega)");

    RtQuat result;
    RtQuatSlerp(&result, &from, &to, 0.5f, &cache);
    check(near(result.imag.y, 0.38268343f) && near(result.real, 0.92387953f),
          "the halfway slerp is an eighth turn");
    check(near(result.imag.x, 0.0f) && near(result.imag.z, 0.0f), "about the axis it was given");
    check(near(result.imag.y * result.imag.y + result.real * result.real, 1.0f),
          "and comes out unit length");

    RtQuatSlerp(&result, &from, &to, 0.0f, &cache);
    check(result.real == from.real, "t <= 0 is the start quaternion exactly");
    RtQuatSlerp(&result, &from, &to, 1.0f, &cache);
    check(result.real == to.real, "t >= 1 is the end quaternion exactly");

    // The shortest arc. -q is the same rotation as q, so this pair describes
    // the same eighth turn -- but a slerp that did not flip the sign would take
    // the long way round and iAnimSKB.cpp would spin the bone.
    RtQuat negated = { { 0.0f, -root2, 0.0f }, -root2 };
    RtQuatSetupSlerpCache(&from, &negated, &cache);
    check(cache.nearlyZeroOm == FALSE, "a negated destination still slerps");
    check(near(cache.omega, 0.78539816f), "over the same angle, not the reflex one");
    RtQuatSlerp(&result, &from, &negated, 0.5f, &cache);
    check(near(result.imag.y, 0.38268343f) && near(result.real, 0.92387953f),
          "and lands on the short way round");

    // Nearly parallel: 1/sin(omega) is where a slerp blows up, so the cache
    // says lerp instead.
    RtQuat almost = { { 0.0f, 0.00001f, 0.0f }, 1.0f };
    RtQuatSetupSlerpCache(&from, &almost, &cache);
    check(cache.nearlyZeroOm != FALSE, "two nearly equal quaternions fall back to a lerp");
    check(cache.raFrom.real == 1.0f, "with the quaternions cached unscaled");
    RtQuatSlerp(&result, &from, &almost, 0.5f, &cache);
    check(near(result.imag.y, 0.000005f) && near(result.real, 1.0f), "which is the midpoint");

    // Identical quaternions: omega is exactly zero and 1/sin(0) would be an
    // infinity that reached every bone in the skeleton.
    RtQuatSetupSlerpCache(&from, &from, &cache);
    check(cache.nearlyZeroOm != FALSE, "and so do two identical ones");
    RtQuatSlerp(&result, &from, &from, 0.5f, &cache);
    check(near(result.real, 1.0f) && near(result.imag.y, 0.0f), "with no NaN in sight");
}

static void test_object_frames()
{
    printf("_rwObjectHasFrameSetFrame\n");

    // Not on the 112-function list, and the reason is a bug in how that list
    // was generated rather than anything about the function. RwCameraSetFrame
    // and RpLightSetFrame are macros for it.
    RwCamera* camera = RwCameraCreate();
    RwFrame* frame = RwFrameCreate();
    check(camera != NULL && frame != NULL, "a camera and a frame");
    if (camera == NULL || frame == NULL)
    {
        return;
    }

    check(RwCameraGetFrame(camera) == NULL, "a new camera is on no frame");
    RwCameraSetFrame(camera, frame);
    check(RwCameraGetFrame(camera) == frame, "RwCameraSetFrame attaches it");

    // The other half of the attach, and the half a naive implementation would
    // miss: the frame has to know about the object too, or moving the frame
    // never syncs the camera.
    check(frame->objectList.link.next != &frame->objectList.link,
          "and the frame's object list is no longer empty");

    RwV3d t = { 4.0f, 5.0f, 6.0f };
    RwFrameTranslate(frame, &t, rwCOMBINEREPLACE);
    check(near(RwFrameGetLTM(RwCameraGetFrame(camera))->pos.x, 4.0f),
          "so the camera reads its frame's LTM");

    // Reattaching has to unhook from the old frame first, or the old frame's
    // list keeps a link into a camera that is no longer in it.
    RwFrame* other = RwFrameCreate();
    RwCameraSetFrame(camera, other);
    check(RwCameraGetFrame(camera) == other, "reattaching moves it");
    check(frame->objectList.link.next == &frame->objectList.link,
          "and empties the frame it came off");

    // The detach. xShadow.cpp:693 and zNPCTypePrawn.cpp:622 both do this
    // immediately before destroying the frame underneath.
    _rwObjectHasFrameSetFrame(camera, NULL);
    check(RwCameraGetFrame(camera) == NULL, "_rwObjectHasFrameSetFrame(obj, NULL) detaches");
    check(other->objectList.link.next == &other->objectList.link, "leaving the frame empty");

    // A light, because the void* is meant to take any of them and the offset
    // it relies on is a different struct's.
    RpLight* light = RpLightCreate(rpLIGHTDIRECTIONAL);
    if (light != NULL)
    {
        RpLightSetFrame(light, frame);
        check(RpLightGetFrame(light) == frame, "RpLightSetFrame reaches the same code");
        _rwObjectHasFrameSetFrame(light, NULL);
        RpLightDestroy(light);
    }

    _rwObjectHasFrameSetFrame(NULL, frame); // must not fault

    RwFrameDestroy(other);
    RwFrameDestroy(frame);
    RwCameraDestroy(camera);
}

// Count the meshes a walk visits, and record where they were.
struct MeshWalkLog
{
    int calls;
    RwUInt32 totalIndices;
    RpMesh* first;
    RpMesh* last;
    RpMeshHeader* headerSeen;
};

static RpMesh* countMeshesCB(RpMesh* mesh, RpMeshHeader* meshHeader, void* pData)
{
    MeshWalkLog* log = (MeshWalkLog*)pData;
    if (log->calls == 0)
    {
        log->first = mesh;
    }
    log->last = mesh;
    log->headerSeen = meshHeader;
    log->calls++;
    log->totalIndices += mesh->numIndices;
    return mesh;
}

static RpMesh* stopAfterFirstMeshCB(RpMesh* mesh, RpMeshHeader* meshHeader, void* pData)
{
    countMeshesCB(mesh, meshHeader, pData);
    return NULL;
}

// The three functions the old regeneration command hid, and the reason they
// are tested together is that they have nothing else in common: a `sed
// 's/^_*//'` in front of a `^(Rw|Rp|Rt|Rx)` anchor dropped every RenderWare
// symbol spelled with a leading underscore, so these went missing as a group
// rather than for any reason to do with what they do. See TODO.md.
//
// _rwObjectHasFrameSetFrame was the fourth and has its own section above.
static void test_underscored()
{
    printf("_rwFrameSyncDirty / _rwInvSqrt / _rpMeshHeaderForAllMeshes\n");

    // --- _rwFrameSyncDirty ---------------------------------------------
    //
    // The whole point of the function is that moving a frame does NOT
    // recompute its LTM. If it did, all seven call sites would be dead code
    // and this test would pass while proving nothing -- so the staleness is
    // checked first, deliberately, by reading ->ltm out of the struct rather
    // than through RwFrameGetLTM (which syncs on the way past).
    RwFrame* frame = RwFrameCreate();
    check(frame != NULL, "a frame to move");
    if (frame == NULL)
    {
        return;
    }

    RwV3d t = { 7.0f, 8.0f, 9.0f };
    RwFrameTranslate(frame, &t, rwCOMBINEREPLACE);
    check(near(frame->modelling.pos.x, 7.0f), "the modelling matrix moved");
    check(near(frame->ltm.pos.x, 0.0f), "but the LTM is deferred, not recomputed");

    _rwFrameSyncDirty();
    check(near(frame->ltm.pos.x, 7.0f) && near(frame->ltm.pos.y, 8.0f) &&
              near(frame->ltm.pos.z, 9.0f),
          "_rwFrameSyncDirty brings it up to date");

    // The list has to be emptied too, or the next flush walks frames that may
    // since have been destroyed. Nothing can read the list from this side, so
    // this checks the consequence: a second flush with nothing dirty is a
    // no-op rather than a fault.
    _rwFrameSyncDirty();
    check(near(frame->ltm.pos.x, 7.0f), "and flushing again with nothing dirty is harmless");

    // A hierarchy, built through librw because the port has no RwFrameAddChild
    // -- nothing in src/SB calls one, so the C API does not carry it. What is
    // being checked is librw's subtree recursion, which is the part of
    // RenderWare's _rwFrameSyncDirty that a one-frame test cannot reach.
    RwFrame* child = RwFrameCreate();
    if (child != NULL)
    {
        reinterpret_cast<rw::Frame*>(frame)->addChild(reinterpret_cast<rw::Frame*>(child));

        RwV3d ct = { 1.0f, 0.0f, 0.0f };
        RwFrameTranslate(child, &ct, rwCOMBINEREPLACE);
        check(near(child->ltm.pos.x, 0.0f), "a child's LTM is deferred too");

        _rwFrameSyncDirty();
        check(near(child->ltm.pos.x, 8.0f), "and the flush walks the whole subtree");

        reinterpret_cast<rw::Frame*>(child)->removeChild();
        RwFrameDestroy(child);
    }

    RwFrameDestroy(frame);

    // --- _rwInvSqrt ----------------------------------------------------
    check(near(_rwInvSqrt(4.0f), 0.5f), "_rwInvSqrt(4) is 1/2");
    check(near(_rwInvSqrt(1.0f), 1.0f), "_rwInvSqrt(1) is 1");
    check(near(_rwInvSqrt(0.25f), 2.0f), "_rwInvSqrt(1/4) is 2");

    // The case the game reads, and the reason this function must not guard
    // its division. xCollide.cpp:1413 scales a degenerate triangle's zero
    // normal by this and rejects the triangle when the result is NaN; that
    // only happens if a zero input gives infinity. A "safe" version returning
    // 0 here would turn every degenerate triangle into a silent hit with no
    // surface direction.
    RwReal recip = _rwInvSqrt(0.0f);
    check(recip > 3.0e38f, "_rwInvSqrt(0) is +infinity, not a guarded zero");

    RwV3d degenerate = { 0.0f, 0.0f, 0.0f };
    RwV3dScaleMacro(&degenerate, &degenerate, recip);
    check(degenerate.x != degenerate.x, "so scaling a zero-area normal by it gives NaN");

    // --- _rpMeshHeaderForAllMeshes -------------------------------------
    RpGeometry* geometry = makeQuad();
    check(geometry != NULL, "a geometry to build meshes on");
    if (geometry == NULL)
    {
        return;
    }

    RpMaterial* m0 = reinterpret_cast<RpMaterial*>(rw::Material::create());
    RpMaterial* m1 = reinterpret_cast<RpMaterial*>(rw::Material::create());
    RpGeometryTriangleSetMaterial(geometry, &geometry->triangles[0], m0);
    RpGeometryTriangleSetMaterial(geometry, &geometry->triangles[1], m1);
    RpGeometryUnlock(geometry);

    RpMeshHeader* header = geometry->mesh;
    check(header != NULL && header->numMeshes == 2, "two materials, two meshes");
    if (header == NULL)
    {
        return;
    }

    // firstMeshOffset is stamped with a value that would walk off the end if
    // it were added. RenderWare's own implementation DOES add it -- its meshes
    // start that many bytes past the header -- and librw's start immediately
    // after the header with the field left as padding, so this is the one
    // place the two implementations must differ. Anything that "restores"
    // RenderWare's arithmetic here fails this check rather than corrupting a
    // level's vertices at xJSP.cpp:35.
    header->firstMeshOffset = 0x4000;

    MeshWalkLog log = { 0, 0, NULL, NULL, NULL };
    check(_rpMeshHeaderForAllMeshes(header, countMeshesCB, &log) == header,
          "_rpMeshHeaderForAllMeshes returns its header");
    check(log.calls == 2, "it visited both meshes");
    check(log.headerSeen == header, "and handed the callback the header it was given");
    check(log.first == (RpMesh*)(header + 1), "the first mesh follows the header immediately");
    check(log.last == (RpMesh*)(header + 1) + 1, "and they are contiguous");
    check(log.totalIndices == header->totalIndicesInMesh,
          "the indices it walked add up to totalIndicesInMesh");

    // xJSP.cpp reads mesh->indices[i] against morphTarget->verts, so an index
    // out of range would build the level out of whatever follows the vertex
    // array. Six indices over four vertices.
    check(log.totalIndices == 6, "six indices for two triangles");

    MeshWalkLog stopped = { 0, 0, NULL, NULL, NULL };
    _rpMeshHeaderForAllMeshes(header, stopAfterFirstMeshCB, &stopped);
    check(stopped.calls == 1, "a callback returning NULL stops the walk early");

    check(_rpMeshHeaderForAllMeshes(NULL, countMeshesCB, &log) == NULL,
          "a NULL header is refused rather than walked");

    reinterpret_cast<rw::Material*>(m1)->destroy();
    reinterpret_cast<rw::Material*>(m0)->destroy();
    reinterpret_cast<rw::Geometry*>(geometry)->destroy();
}

// RpAtomicDestroy and the standalone atomic stream pair, which were missing
// from the 112-function list rather than deliberately left out -- see the
// comment above them in atomic.cpp. All three exist for FullAtomicDupe
// (xModelBucket.cpp:125), and this test is that function's sequence.
static void test_atomic_stream()
{
    printf("RpAtomicDestroy / RpAtomicStreamWrite / RpAtomicStreamRead\n");

    RpAtomic* atomic = reinterpret_cast<RpAtomic*>(rw::Atomic::create());
    RpGeometry* geometry = makeQuad();
    check(atomic != NULL && geometry != NULL, "an atomic and a geometry to duplicate");
    if (atomic == NULL || geometry == NULL)
    {
        return;
    }

    RpMaterial* material = reinterpret_cast<RpMaterial*>(rw::Material::create());
    RpGeometryTriangleSetMaterial(geometry, &geometry->triangles[0], material);
    RpGeometryTriangleSetMaterial(geometry, &geometry->triangles[1], material);
    RpGeometryUnlock(geometry);

    RpAtomicSetGeometry(atomic, geometry, 0);
    RpAtomicSetFrame(atomic, RwFrameCreate());
    atomic->object.object.flags = rpATOMICCOLLISIONTEST | rpATOMICRENDER;

    // Write, exactly as FullAtomicDupe does: onto an empty RwMemory, so the
    // stream has to grow.
    RwMemory mem = { NULL, 0 };
    RwStream* stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMWRITE, &mem);
    check(stream != NULL, "a memory stream to write it to");
    if (stream == NULL)
    {
        return;
    }

    check(RpAtomicStreamWrite(atomic, stream) == atomic, "RpAtomicStreamWrite");
    RwStreamClose(stream, &mem);
    check(mem.start != NULL && mem.length > 12, "it wrote something");

    // The length in the chunk header has to be the length actually written.
    // Getting that wrong is invisible here -- the reader never consults it --
    // and fatal the moment an atomic is written into a stream that holds
    // anything after it, because RwStreamFindChunk skips by this number.
    stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &mem);
    RwUInt32 chunkLength = 0;
    check(RwStreamFindChunk(stream, rwID_ATOMIC, &chunkLength, NULL) != FALSE,
          "RwStreamFindChunk finds the rwID_ATOMIC chunk");
    check(chunkLength == mem.length - 12,
          "and the size it declared is the size it wrote");

    RpAtomic* dupe = RpAtomicStreamRead(stream);
    RwStreamClose(stream, NULL);
    check(dupe != NULL, "RpAtomicStreamRead");
    if (dupe == NULL)
    {
        return;
    }

    check(dupe != atomic, "the duplicate is a different atomic");

    // A COPY of the geometry, not another reference to the same one. This is
    // the whole point of the round trip: xModelBucket needs N atomics it can
    // instance separately, and sharing one geometry would defeat it.
    check(dupe->geometry != NULL && dupe->geometry != geometry,
          "with a geometry of its own");
    check(geometry->refCount == 2, "and the original's reference count is untouched");

    check(dupe->geometry->numVertices == 4 && dupe->geometry->numTriangles == 2,
          "the geometry round-tripped its counts");
    check(near(dupe->geometry->morphTarget[0].verts[2].x, 1.0f) &&
              near(dupe->geometry->morphTarget[0].verts[2].z, 1.0f),
          "and its vertices");
    check(dupe->geometry->matList.numMaterials == 1, "and its material list");

    check(dupe->object.object.flags == (rpATOMICCOLLISIONTEST | rpATOMICRENDER),
          "the atomic's flags round-tripped");

    // No frame: a standalone atomic chunk names its frame by an index into a
    // clump's frame list, and there is no clump here. FullAtomicDupe gives the
    // atomic a fresh frame on the next line, so this is the state it expects.
    check(RpAtomicGetFrame(dupe) == NULL, "and it comes back on no frame");

    // Reading twice out of one written block, which is the loop FullAtomicDupe
    // actually runs -- it reopens the same RwMemory once per duplicate.
    stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &mem);
    RwStreamFindChunk(stream, rwID_ATOMIC, NULL, NULL);
    RpAtomic* second = RpAtomicStreamRead(stream);
    RwStreamClose(stream, NULL);
    check(second != NULL && second != dupe, "the same block reads a second, distinct duplicate");

    // --- RpAtomicDestroy -----------------------------------------------
    check(RpAtomicDestroy(second) != FALSE, "RpAtomicDestroy");
    check(RpAtomicDestroy(dupe) != FALSE, "on both duplicates");
    check(RpAtomicDestroy(NULL) == FALSE, "RpAtomicDestroy(NULL) is refused");

    // The frame survives its atomic: RenderWare releases the frame rather than
    // destroying it, and FullAtomicDupe destroys it by hand one line earlier.
    // An implementation that took the frame with it would double-free there.
    RwFrame* frame = RpAtomicGetFrame(atomic);
    check(frame != NULL, "the original still has its frame");
    RpAtomicDestroy(atomic);
    check(frame->objectList.link.next == &frame->objectList.link,
          "the destroy detached the atomic from it");

    // Still live memory afterwards, not freed underneath the caller: moving it
    // and reading the value back is the cheapest way to say so.
    RwV3d after = { 3.0f, 0.0f, 0.0f };
    RwFrameTranslate(frame, &after, rwCOMBINEREPLACE);
    check(near(frame->modelling.pos.x, 3.0f), "and the frame outlives the atomic");
    RwFrameDestroy(frame);

    // Destroying the atomic gave up its reference on the geometry, so the one
    // taken by makeQuad's caller is all that is left.
    check(geometry->refCount == 1, "destroying the atomic released the geometry");

    reinterpret_cast<rw::Material*>(material)->destroy();
    reinterpret_cast<rw::Geometry*>(geometry)->destroy();

    RwFree(mem.start);
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

    // Term frees every plugin, which takes RpWorld's tail with it. A world made
    // now would have no memory behind ->matList or ->boundingBox, so the shim
    // refuses instead of handing one out. This is the same check that catches a
    // port whose startup forgot RpWorldPluginAttach, which is otherwise silent.
    RwBBox anyBox = { { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } };
    check(RpWorldCreate(&anyBox) == NULL, "RpWorldCreate without the world plugin is refused");

    check(sNumFree > 0, "librw freed through the memory functions it was given");

#ifndef RW_NULL
    iWindowClose();
#endif
}

int main()
{
    // Unbuffered, so that a crash leaves the last completed check on screen
    // instead of a half-flushed line. With a real render backend this test
    // reaches driver code that can fault, and "which check was it" is the whole
    // diagnosis.
    setvbuf(stdout, NULL, _IONBF, 0);

    test_engine_startup();
    test_frames();
    test_values();
    test_streams();
    test_textures();
    test_images();
    test_cameras();
    test_lights();
    test_worlds();
    test_renderstate();
    test_immediate();
    test_geometry();
    test_atomics();
    test_skin();
    test_matfx();
    test_ptank();
    test_clumps();
    test_intersections();
    test_slerp();
    test_object_frames();
    test_underscored();
    test_atomic_stream();
    test_engine_shutdown();

    printf("\n%d failure%s\n", failures, failures == 1 ? "" : "s");
    return failures != 0;
}
