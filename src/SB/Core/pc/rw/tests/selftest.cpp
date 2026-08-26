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

#include <rwcore.h>

#include "rw.h"

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
    test_engine_shutdown();

    printf("\n%d failure%s\n", failures, failures == 1 ? "" : "s");
    return failures != 0;
}
