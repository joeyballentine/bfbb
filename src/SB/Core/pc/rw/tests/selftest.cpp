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
#include <rpcollis.h>
#include <rpmatfx.h>
#include <rpptank.h>
#include <rpskin.h>
#include <rpworld.h>

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

    // Strictly between init and open, which is not a style choice: each of
    // these registers plugins that grow the size of an atomic, a geometry or a
    // material, and Engine::open freezes those sizes. Attaching after open
    // would hand out plugin offsets past the end of every object allocated
    // afterwards. iSystem.cpp's RWAttachPlugins sequences the real game's
    // calls in exactly this window.
    check(RpSkinPluginAttach() != FALSE, "RpSkinPluginAttach");
    check(RpMatFXPluginAttach() != FALSE, "RpMatFXPluginAttach");
    check(RpPTankPluginAttach() != FALSE, "RpPTankPluginAttach");
    check(_rpPTankAtomicDataOffset > 0, "RpPTank got a slot in the atomic's plugin block");

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
    check(RwCameraShowRaster(camera, NULL, rwRASTERFLIPWAITVSYNC) == NULL,
          "RwCameraShowRaster refuses a camera with no frame buffer");
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
    rw::engine->device.im2DRenderPrimitive = captureIm2DRenderPrimitive;
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
    check(RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, quad, 4, indices, 6) != FALSE,
          "RwIm2DRenderIndexedPrimitive");
    check(RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, quad, 4, NULL, 6) == FALSE,
          "RwIm2DRenderIndexedPrimitive with no indices is refused");

    // Every one of the twenty call sites uses the result of RwIm3DTransform as
    // "may I render now", so a NULL here would silently stop every effect in
    // the game from drawing. Nothing dereferences it -- see the comment in
    // im.cpp.
    RwIm3DVertex verts[4];
    memset(verts, 0, sizeof(verts));
    check(RwIm3DTransform(verts, 4, NULL, rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA) != NULL,
          "RwIm3DTransform reports success");
    check(RwIm3DTransform(NULL, 4, NULL, 0) == NULL, "RwIm3DTransform(NULL) is refused");
    check(RwIm3DTransform(verts, 0, NULL, 0) == NULL, "and so is a zero-vertex transform");

    sIm3DPrim = -1;
    check(RwIm3DRenderPrimitive(rwPRIMTYPETRILIST) != FALSE, "RwIm3DRenderPrimitive");
    check(sIm3DPrim == rw::PRIMTYPETRILIST, "the primitive type arrives unchanged");

    sIm3DEnds = 0;
    check(RwIm3DEnd() != FALSE, "RwIm3DEnd");
    check(sIm3DEnds == 1, "and reaches the device once");

    rw::engine->device.im2DRenderPrimitive = savedIm2D;
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
    test_cameras();
    test_lights();
    test_renderstate();
    test_immediate();
    test_geometry();
    test_atomics();
    test_skin();
    test_matfx();
    test_ptank();
    test_engine_shutdown();

    printf("\n%d failure%s\n", failures, failures == 1 ? "" : "s");
    return failures != 0;
}
