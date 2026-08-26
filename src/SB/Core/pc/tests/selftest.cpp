// Checks the parts of the PC platform layer that do not need a renderer.
//
// The GameCube side is scored by a byte-identical DOL. There is no equivalent
// for a port, so the substitute is this: each thing the layer claims to do is
// exercised and its answer checked, so that "implemented" is a measurement
// rather than an assertion.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <types.h>

#include "iFile.h"
#include "iMath.h"
#include "iMemMgr.h"
#include "iPadHost.h"
#include "iSystem.h"
#include "iTime.h"
#include "xFile.h"
#include "xMemMgr.h"

// iMemInit fills this in. It lives in xMemMgr.cpp, which needs the renderer to
// build, so the harness supplies the one object under test rather than linking
// the game.
xMemInfo_tag gMemInfo;

static int failures;

static void check(bool ok, const char* what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
    {
        failures++;
    }
}

static void test_time()
{
    printf("iTime\n");

    iTimeInit();

    // The whole point of GET_BUS_FREQUENCY on a host: five sites in src/SB
    // divide it by four and use the result as iTimeGet's tick rate. If these
    // two ever disagree, every one of them is wrong by the ratio and nothing
    // else in the build would say so.
    check(GET_BUS_FREQUENCY() / 4 == ITIME_TICKS_PER_SECOND,
          "GET_BUS_FREQUENCY()/4 == iTime tick rate");

    iTime t0 = iTimeGet();
    usleep(120000); // 0.12 s
    iTime t1 = iTimeGet();

    check(t1 > t0, "iTimeGet advances");

    F32 dt = iTimeDiffSec(t0, t1);
    check(dt > 0.10f && dt < 0.20f, "iTimeDiffSec reports ~0.12 s for a 0.12 s sleep");

    // The other spelling the game uses, on a raw tick count.
    check(fabsf(iTimeDiffSec((iTime)ITIME_TICKS_PER_SECOND) - 1.0f) < 0.0001f,
          "iTimeDiffSec(one second of ticks) == 1.0");

    char date[128];
    char clock[128];
    U32 dn = iGetCurrFormattedDate(date);
    U32 cn = iGetCurrFormattedTime(clock);

    check(dn == strlen(date) + 1, "iGetCurrFormattedDate returns length including NUL");
    check(cn == strlen(clock) + 1, "iGetCurrFormattedTime returns length including NUL");
    check(strstr(clock, ".M.") != NULL, "formatted time carries A.M./P.M.");

    S32 mon = iGetMonth();
    check(mon >= JANUARY && mon <= DECEMBER, "iGetMonth is 1-12");
    check(iGetHour() >= 0 && iGetHour() <= 23, "iGetHour is 0-23");

    printf("    date: %s\n    time: %s\n", date, clock);
}

static void test_math()
{
    printf("iMath\n");

    check(fabsf(isin(0.0f)) < 1e-6f, "isin(0) == 0");
    check(fabsf(icos(0.0f) - 1.0f) < 1e-6f, "icos(0) == 1");
    check(fabsf(isin(3.14159265f / 2.0f) - 1.0f) < 1e-5f, "isin(pi/2) == 1");
    check(fabsf(itan(0.0f)) < 1e-6f, "itan(0) == 0");

    // The MSL extension the game's sources use, supplied by compat/math.h.
    // The gc header defines __fabs as identity for non-CodeWarrior compilers,
    // which would make this -3.
    check(FABS(-3.0f) == 3.0f, "FABS is absolute value, not identity");
    check(iabs(-2.5f) == 2.5f, "iabs is absolute value");
}

static void test_mem()
{
    printf("iMemMgr\n");

    iMemInit();

    check(gMemInfo.DRAM.addr != 0, "iMemInit reserved a DRAM arena");
    check(gMemInfo.DRAM.size == 0x384000, "DRAM is retail's 0x384000 bytes");

    // xMemInitHeap does its arithmetic on gMemInfo.DRAM.addr as a U32, so the
    // arena has to be addressable in 32 bits and survive the round trip.
    U32 base = gMemInfo.DRAM.addr;
    void* p = (void*)(unsigned long)base;
    check((U32)(unsigned long)p == base, "the arena's address round-trips through U32");

    // xMemInit puts gxHeap[1] and gxHeap[2] at DRAM.addr + DRAM.size. Retail
    // leaves those past its own allocation; iMemInit reserves twice the size
    // so they are backed. Write to the far end to prove it.
    volatile U8* top = (volatile U8*)p + (2 * 0x384000) - 1;
    *top = 0xA5;
    check(*top == 0xA5, "the second heap's range is backed memory");

    check(mem_base_alloc == base, "mem_base_alloc agrees with DRAM.addr");

    iMemExit();
    check(gMemInfo.DRAM.addr == 0, "iMemExit released the arena");
}

static void test_file()
{
    printf("iFile\n");

    char dir[] = "/tmp/bfbb_pc_selftest_XXXXXX";
    if (mkdtemp(dir) == NULL)
    {
        check(false, "could not make a temp directory");
        return;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/HB01.HIP", dir);

    const char payload[] = "SPONGEBOB SQUAREPANTS";
    FILE* f = fopen(path, "wb");
    fwrite(payload, 1, sizeof(payload) - 1, f);
    fclose(f);

    iFileInit();
    iFileSetPath(dir);

    tag_xFile file = {};
    check(iFileOpen("HB01.HIP", 0, &file) == 0, "iFileOpen resolves a name under iFileSetPath");
    check(iFileGetSize(&file) == sizeof(payload) - 1, "iFileGetSize is the file's length");

    char buf[64];
    memset(buf, 0xCC, sizeof(buf));
    iFileRead(&file, buf, 32);

    check(memcmp(buf, payload, sizeof(payload) - 1) == 0, "iFileRead returns the bytes");

    // Retail reads whole sectors and the DVD pads the tail; fread would leave
    // the caller's buffer untouched past EOF, so iFileRead zeroes it.
    bool padded = true;
    for (size_t i = sizeof(payload) - 1; i < 32; i++)
    {
        padded = padded && buf[i] == 0;
    }
    check(padded, "the read past end-of-file is zeroed, not left stale");

    iFileSeek(&file, 5, IFILE_SEEK_SET);
    memset(buf, 0, sizeof(buf));
    iFileRead(&file, buf, 4);
    check(memcmp(buf, "EBOB", 4) == 0, "iFileSeek(SET) positions the next read");

    // Retail's SEEK_END subtracts rather than adding a negative offset.
    check(iFileSeek(&file, 4, IFILE_SEEK_END) == (S32)(sizeof(payload) - 1) - 4,
          "iFileSeek(END, n) lands n bytes before the end");

    check(iFileClose(&file) == 0, "iFileClose succeeds");

    // The disc filesystem was case-insensitive and the asset names disagree
    // with each other; a host filesystem usually is not.
    tag_xFile lower = {};
    check(iFileOpen("hb01.hip", 0, &lower) == 0, "a wrong-case name still opens");
    iFileClose(&lower);

    tag_xFile missing = {};
    check(iFileOpen("NOSUCH.HIP", 0, &missing) != 0, "a missing file reports failure");

    // The asynchronous path: queued by iFileReadAsync, completed by the
    // service function the load loops call.
    tag_xFile afile = {};
    iFileOpen("HB01.HIP", 0, &afile);
    memset(buf, 0, sizeof(buf));

    S32 key = iFileReadAsync(&afile, buf, 16, NULL, 0);
    check(key >= 0, "iFileReadAsync accepts the request");
    check(iFileReadAsyncStatus(key, NULL) != IFILE_RDSTAT_EXPIRED,
          "the key it returned is the key status expects");

    iFileAsyncService();
    check(iFileReadAsyncStatus(key, NULL) == IFILE_RDSTAT_DONE,
          "iFileAsyncService completes the read");
    check(memcmp(buf, payload, 16) == 0, "the asynchronous read returned the bytes");

    iFileClose(&afile);
    iFileExit();

    unlink(path);
    rmdir(dir);
}

static void test_pad()
{
    printf("iPad host backend\n");

    iPadHostInit();
    iPadHostPoll();

    const iPadHostState* s = iPadHostGet(0);
    check(s != NULL, "port 0 has a state");
    check(s != NULL && !s->connected, "the null backend reports no controller");
    check(iPadHostGet(99) == NULL, "an out-of-range port is rejected");

    iPadHostExit();
}

int main()
{
    printf("bfbb PC platform layer selftest\n\n");

    test_time();
    test_math();
    test_mem();
    test_file();
    test_pad();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "passed", failures,
           failures == 1 ? "" : "s");

    return failures ? 1 : 0;
}
