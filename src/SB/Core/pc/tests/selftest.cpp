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

#include <types.h>

#include "iFile.h"
#include "iHost.h"
#include "iSnd.h"
#include "iSndData.h"
#include "xpkrsvc.h"
#include "xhipio.h"
#include "iSndHost.h"
#include "xSnd.h"
#include "iMath.h"
#include "iMemMgr.h"
#include "iPadHost.h"
#include "iSystem.h"
#include "isavegame.h"
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
    iHostSleepUntilNs(iHostMonotonicNs() + 120000000ULL); // 0.12 s
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

// mkdtemp is POSIX-only. The monotonic clock is already a seam function and
// its value differs between runs, which is all the uniqueness a selftest needs.
static bool scratch_dir(const char* tag, char* out, size_t outsize)
{
    char tmp[512];
    if (!iHostTempDir(tmp, sizeof(tmp)))
    {
        return false;
    }

    snprintf(out, outsize, "%s/bfbb_pc_%s_%llu", tmp, tag,
             (unsigned long long)(iHostMonotonicNs() % 1000000000ULL));
    return iHostMakeDir(out);
}

static void test_file()
{
    printf("iFile\n");

    char dir[512];
    if (!scratch_dir("selftest", dir, sizeof(dir)))
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

    // And it must NOT have moved the file's own offset.
    //
    // The console's DVDReadAsync takes the offset as an argument and leaves
    // ps->offset alone, so advancing past what was read belongs to the
    // completion callback -- iCutscene.cpp:iCSAsyncReadCB seeks forward by the
    // amount status reports, and would advance twice if the read did it too.
    // That cost the cutscene loader a whole chunk per read.
    check(afile.ps.offset == 0, "an asynchronous read leaves the file offset alone");

    // Which means two reads with no seek between them return the same bytes,
    // exactly as they would on the console.
    U8 again[16];
    memset(again, 0, sizeof(again));
    S32 key2 = iFileReadAsync(&afile, again, 16, NULL, 0);
    iFileAsyncService();
    check(iFileReadAsyncStatus(key2, NULL) == IFILE_RDSTAT_DONE, "a second read completes");
    check(memcmp(again, payload, 16) == 0, "and returns the same bytes, the offset not having moved");

    // The callback's seek is what advances it.
    iFileSeek(&afile, 16, IFILE_SEEK_CUR);
    check(afile.ps.offset == 16, "seeking forward is what advances the position");

    iFileClose(&afile);
    iFileExit();

    iHostRemoveFile(path);
    iHostRemoveDir(dir);
}

#ifdef BFBB_INPUT_BACKEND_WIN32
// The two pure conversions inside iPadHostWin32.cpp, which are named rather
// than static so this file can reach them. Declared here rather than pulled in
// from a header, so that a change to either signature is caught at link time
// instead of the test silently retargeting itself at something else.
#include <windows.h>
#include <xinput.h>
void iPadHostWin32ConvertStick(S16 rawX, S16 rawY, S32 deadzone, F32* outX, F32* outY);
U32 iPadHostWin32ConvertButtons(const XINPUT_GAMEPAD& gp);

// The button bits, restated rather than included from xPad.h -- the same
// reasoning as the sound table above. These are the values the GAME reads, so
// a test that took them from the same header as the code under test could not
// catch either side changing.
#define TEST_PAD_START 0x1
#define TEST_PAD_SELECT 0x2
#define TEST_PAD_UP 0x10
#define TEST_PAD_RIGHT 0x20
#define TEST_PAD_DOWN 0x40
#define TEST_PAD_LEFT 0x80
#define TEST_PAD_L1 0x100
#define TEST_PAD_L2 0x200
#define TEST_PAD_R1 0x1000
#define TEST_PAD_R2 0x2000
#define TEST_PAD_X 0x10000 // A on the GameCube
#define TEST_PAD_O 0x20000 // X on the GameCube
#define TEST_PAD_SQUARE 0x40000 // Y on the GameCube
#define TEST_PAD_TRIANGLE 0x80000 // B on the GameCube
#define TEST_PAD_Z 0x100000

static void test_pad_win32_sticks()
{
    const S32 dz = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    F32 x, y;

    iPadHostWin32ConvertStick(0, 0, dz, &x, &y);
    check(x == 0.0f && y == 0.0f, "a centred stick reads zero");

    iPadHostWin32ConvertStick((S16)(dz - 1), 0, dz, &x, &y);
    check(x == 0.0f && y == 0.0f, "inside the deadzone reads zero");

    // The deadzone is radial, so a diagonal push whose components are each
    // inside it but whose magnitude is not must still register. A per-axis
    // deadzone reports zero here, and that is the bug this guards.
    S16 diag = (S16)(dz * 0.8f);
    iPadHostWin32ConvertStick(diag, diag, dz, &x, &y);
    check(x > 0.0f && y > 0.0f, "a diagonal past the radial deadzone registers");

    iPadHostWin32ConvertStick(32767, 0, dz, &x, &y);
    check(fabsf(x - 1.0f) < 0.0001f, "full right deflection reads 1");
    check(fabsf(y) < 0.0001f, "and nothing on the other axis");

    iPadHostWin32ConvertStick(-32768, 0, dz, &x, &y);
    check(fabsf(x + 1.0f) < 0.0001f, "full left deflection reads -1, not past it");

    // Just past the deadzone edge the output must start near zero. Rescaling
    // from that edge is what makes it true; without it the first movement jumps
    // straight to the deadzone's fraction of full scale.
    iPadHostWin32ConvertStick((S16)(dz + 40), 0, dz, &x, &y);
    check(x > 0.0f && x < 0.01f, "just past the deadzone the stick barely moves");

    // A full diagonal is clamped to the unit circle, so its magnitude cannot
    // exceed what one axis alone reports.
    iPadHostWin32ConvertStick(32767, 32767, dz, &x, &y);
    check(sqrtf(x * x + y * y) <= 1.0001f, "a full diagonal does not exceed full scale");

    iPadHostWin32ConvertStick(0, 32767, dz, &x, &y);
    check(y > 0.99f, "up is positive, as iPadHost.h specifies");
}

static void test_pad_win32_buttons()
{
    XINPUT_GAMEPAD gp;
    memset(&gp, 0, sizeof(gp));

    check(iPadHostWin32ConvertButtons(gp) == 0, "an idle pad reports no buttons");

    // Face buttons map by name onto the GameCube's, and gc/iPad.cpp is what
    // says which XPAD_BUTTON_* each of those becomes.
    gp.wButtons = XINPUT_GAMEPAD_A;
    check(iPadHostWin32ConvertButtons(gp) == TEST_PAD_X, "A is the GameCube's A");
    gp.wButtons = XINPUT_GAMEPAD_B;
    check(iPadHostWin32ConvertButtons(gp) == TEST_PAD_TRIANGLE, "B is the GameCube's B");
    gp.wButtons = XINPUT_GAMEPAD_X;
    check(iPadHostWin32ConvertButtons(gp) == TEST_PAD_O, "X is the GameCube's X");
    gp.wButtons = XINPUT_GAMEPAD_Y;
    check(iPadHostWin32ConvertButtons(gp) == TEST_PAD_SQUARE, "Y is the GameCube's Y");

    gp.wButtons = XINPUT_GAMEPAD_START;
    check(iPadHostWin32ConvertButtons(gp) == TEST_PAD_START, "start is start");

    gp.wButtons = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT;
    check(iPadHostWin32ConvertButtons(gp) == (TEST_PAD_UP | TEST_PAD_LEFT),
          "two d-pad directions come through together");

    // Shoulders: the bumpers are the second pair, the triggers the first, and
    // the triggers only click past the threshold.
    gp.wButtons = XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER;
    check(iPadHostWin32ConvertButtons(gp) == (TEST_PAD_L2 | TEST_PAD_R2),
          "the bumpers are L2 and R2");

    gp.wButtons = 0;
    gp.bLeftTrigger = XINPUT_GAMEPAD_TRIGGER_THRESHOLD - 1;
    gp.bRightTrigger = XINPUT_GAMEPAD_TRIGGER_THRESHOLD - 1;
    check(iPadHostWin32ConvertButtons(gp) == 0, "a trigger short of the threshold does nothing");

    gp.bLeftTrigger = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    gp.bRightTrigger = 255;
    check(iPadHostWin32ConvertButtons(gp) == (TEST_PAD_L1 | TEST_PAD_R1),
          "the triggers are L1 and R1 once past it");

    // Nothing synthesises the GameCube's Z. It exists on that console only to
    // reach L2 and R2, and an Xbox pad reaches them directly.
    gp.wButtons = 0xFFFF;
    gp.bLeftTrigger = 255;
    gp.bRightTrigger = 255;
    check((iPadHostWin32ConvertButtons(gp) & TEST_PAD_Z) == 0,
          "no combination produces the GameCube's Z modifier");
}
#endif

// xutil.cpp's only unresolved symbol here. xUtil_yesno and xUtil_wtadjust use
// it and nothing below calls either; the real one is in xMath.cpp, which cannot
// be linked into this target for the reason given beside iMath3 in CMakeLists.
F32 xurand()
{
    return 0.0f;
}

char* xUtil_idtag2string(U32 srctag, S32 bufidx);

static void test_idtag()
{
    printf("xUtil_idtag2string\n");

    // The ordinary case. bufidx 0 takes the default branch, which is the one
    // zMain.cpp:823 uses and the one that reads the way the tag is written.
    check(strcmp(xUtil_idtag2string(0x48423030, 0), "HB00") == 0,
          "a printable tag round-trips");

    // bufidx 4 and 5 take the other branch and come out reversed. That is
    // retail's behaviour on both platforms, not an endian bug in the port: the
    // swap above turns the console's bytes into the host's order first, so both
    // reach this switch with the same array.
    check(strcmp(xUtil_idtag2string(0x48423030, 4), "00BH") == 0,
          "buffers 4 and 5 come out reversed, on the console too");

    // And the case that aborts the process rather than misprinting.
    //
    // uc walks the tag as a SIGNED char, so a byte at or above 0x80 reaches
    // isprint negative. That is out of range for it, and the host CRT range-
    // checks and aborts where the console's table lookup shrugs. Asset ids are
    // hashes, so roughly half of every tag the game prints has such a byte --
    // zSceneLoad printed one and the port died there.
    char* s = xUtil_idtag2string(0xDEADBEEF, 4);
    check(s != NULL, "a tag with the high bit set returns rather than aborting");
    check(s != NULL && strlen(s) == 4, "and is still four characters");

    // 0xFF is not printable, so it must come back as the placeholder rather
    // than as itself or as whatever a negative index found.
    s = xUtil_idtag2string(0xFFFFFFFF, 4);
    check(s != NULL && strcmp(s, "????") == 0, "an unprintable tag is all placeholders");

    // Every byte value, which is the real assertion: none of them may abort.
    for (S32 i = 0; i < 256; i++)
    {
        U32 tag = (U32)((i << 24) | (i << 16) | (i << 8) | i);
        char* p = xUtil_idtag2string(tag, 4);
        if (p == NULL || strlen(p) != 4)
        {
            check(false, "every byte value survives idtag2string");
            break;
        }
    }
    check(true, "every byte value survives idtag2string");
}

static void test_pad()
{
    printf("iPad host backend\n");

    iPadHostInit();
    iPadHostPoll();

    // The contract every backend owes, whichever one is linked.
    const iPadHostState* s = iPadHostGet(0);
    check(s != NULL, "port 0 has a state");
    check(iPadHostGet(99) == NULL, "an out-of-range port is rejected");
    check(iPadHostGet(-1) == NULL, "so is a negative one");

    for (S32 i = 0; i < IPAD_MAX_CONTROLLERS; i++)
    {
        const iPadHostState* p = iPadHostGet(i);
        check(p != NULL, "every port in range has a state");
        if (p != NULL && !p->connected)
        {
            check(p->buttons == 0 && p->stick_x == 0.0f && p->stick_y == 0.0f &&
                      p->substick_x == 0.0f && p->substick_y == 0.0f,
                  "a disconnected port reads as fully neutral");
        }
    }

    // Rumbling a port that is out of range, or that has no controller, must be
    // a no-op rather than a fault -- xPad.cpp rumbles whatever pad the game
    // thinks is active without checking first.
    iPadHostRumble(0, 1);
    iPadHostRumble(0, 0);
    iPadHostRumble(99, 1);
    iPadHostRumble(-1, 1);
    check(true, "rumbling an absent or out-of-range port is harmless");

#ifdef BFBB_INPUT_BACKEND_WIN32
    // This process has no window, so GetActiveWindow reports none and the
    // keyboard path takes its unfocused branch: present, but holding nothing.
    check(s != NULL && s->connected,
          "win32: the keyboard stands in for port 0 with no controller plugged in");
    check(s != NULL && s->buttons == 0, "win32: an unfocused keyboard holds nothing");

    test_pad_win32_sticks();
    test_pad_win32_buttons();
#else
    check(s != NULL && !s->connected, "the null backend reports no controller");
#endif

    iPadHostExit();
}

static void test_savegame()
{
    printf("isavegame\n");

    char dir[512];
    if (!scratch_dir("saves", dir, sizeof(dir)))
    {
        check(false, "could not make a temp directory");
        return;
    }

    // iSGStartup resolves the save root once, so this has to be set first.
    iHostSetEnv("BFBB_SAVE_DIR", dir);

    check(iSGStartup() == 1, "iSGStartup succeeds on first call");

    // The names are retail's, so a save file is called the same thing on both
    // platforms.
    check(strcmp(iSGMakeName(ISG_NGTYP_GAMEFILE, NULL, 1), "SpongeBob01") == 0,
          "iSGMakeName(GAMEFILE, 1) is SpongeBob01");
    check(iSGMakeName(ISG_NGTYP_GAMEDIR, NULL, 0)[0] == '\0',
          "iSGMakeName(GAMEDIR) is empty, as on a card with no directories");

    // Eight rotating buffers, because callers hold several at once.
    char* a = iSGMakeName(ISG_NGTYP_GAMEFILE, NULL, 0);
    char* b = iSGMakeName(ISG_NGTYP_GAMEFILE, NULL, 1);
    check(strcmp(a, "SpongeBob00") == 0 && strcmp(b, "SpongeBob01") == 0,
          "two names in flight do not alias");

    st_ISGSESSION* isg = iSGSessionBegin(NULL, NULL, 0);
    check(isg != NULL, "iSGSessionBegin returns a session");
    check(isg->slot == -1, "a fresh session has no target selected");

    S32 max = 0;
    check(iSGTgtCount(isg, &max) >= 1, "at least one target is present");
    check(max == ISG_NUM_SLOTS, "the maximum is the console's slot count");

    check(iSGTgtState(isg, 0, NULL) == 0xF, "target 0 is present and formatted");
    check(iSGTgtState(isg, 99, NULL) == 0x1000000, "an out-of-range target reports no card");
    check(iSGCheckMemoryCard(isg, 0) == 1, "iSGCheckMemoryCard sees target 0");
    check(iSGTgtPhysSlotIdx(isg, 0) == 0, "target 0 maps to physical slot 0");
    check(iSGCheckForWrongDevice() == -1, "no wrong-region device on a host");

    check(iSGTgtSetActive(isg, 0) == 1, "iSGTgtSetActive selects target 0");
    check(isg->slot == 0, "the session records the active slot");

    // Nothing saved yet.
    check(iSGSelectGameDir(isg, "") == 0, "no game files yet");
    check(iSGFileSize(isg, "SpongeBob00") == -1, "a missing file has size -1");
    check(iSGCheckForCorruptFiles(isg, (char(*)[64])calloc(ISG_NUM_FILES, 64)) == 0,
          "nothing is corrupt when nothing exists");

    // xSGGameIsEmpty is `size <= 0`, so -1 has to mean empty rather than error.
    check(iSGFileSize(isg, "SpongeBob00") <= 0, "a missing file reads as an empty slot");

    S32 needed = -1;
    S32 avail = -1;
    S32 needFile = -1;
    check(iSGTgtHaveRoom(isg, 0, 4096, NULL, "SpongeBob00", &needed, &avail, &needFile) == 1,
          "there is room for a 4 KB save");
    check(needed == 4096, "bytesNeeded is the whole file when none exists");
    check(needFile == 1, "a new file needs a new directory entry");
    check(avail > 0, "availOnDisk is reported");

    const char payload[] = "BIKINI BOTTOM SAVE DATA, ONE EACH";
    check(iSGSaveFile(isg, "SpongeBob00", (char*)payload, sizeof(payload) - 1, 0, NULL) == 1,
          "iSGSaveFile writes a slot");
    check(iSGPollStatus(isg, NULL, 0) == ISG_OPSTAT_SUCCESS, "the status reports success");
    check(iSGOpError(isg, NULL) == ISG_OPERR_NONE, "no error is left behind");

    check(iSGFileSize(isg, "SpongeBob00") == (S32)sizeof(payload) - 1,
          "iSGFileSize is the payload, with no icon header to subtract");
    check(iSGSelectGameDir(isg, "") == 1, "the target now has a game on it");

    char readbuf[64];
    memset(readbuf, 0, sizeof(readbuf));
    check(iSGLoadFile(isg, "SpongeBob00", readbuf, 0) == 1, "iSGLoadFile reads it back");
    check(memcmp(readbuf, payload, sizeof(payload) - 1) == 0, "the bytes survived the round trip");

    // The save UI reads only the header of each slot to list them.
    memset(readbuf, 0, sizeof(readbuf));
    check(iSGReadLeader(isg, "SpongeBob00", readbuf, 6, 0) == 1, "iSGReadLeader reads a prefix");
    check(memcmp(readbuf, "BIKINI", 6) == 0, "the prefix is the start of the file");

    // Asking for more than the file holds is what a truncated save looks like.
    // The buffer really is this big: iSGReadLeader's contract is that databuf
    // holds numbytes, and it zero-fills the shortfall rather than leaving the
    // tail stale.
    static char bigbuf[4096];
    check(iSGReadLeader(isg, "SpongeBob00", bigbuf, sizeof(bigbuf), 0) == 0,
          "a short read is reported as failure, not a partly-filled buffer");
    check(bigbuf[sizeof(bigbuf) - 1] == 0, "and the shortfall is zeroed");
    check(iSGOpError(isg, NULL) == ISG_OPERR_CORRUPT, "and the error says corrupt");

    char errmsg[256];
    iSGOpError(isg, errmsg);
    check(strlen(errmsg) > 0, "iSGOpError fills in a message");

    // Overwriting costs only the difference and needs no new entry.
    needed = -1;
    needFile = -1;
    iSGTgtHaveRoom(isg, 0, 4096, NULL, "SpongeBob00", &needed, &avail, &needFile);
    check(needed == 4096 - (S32)(sizeof(payload) - 1), "overwriting only needs the difference");
    check(needFile == 0, "overwriting needs no new directory entry");

    char date[64];
    strcpy(date, iSGFileModDate(isg, "SpongeBob00"));
    check(strlen(date) == 19, "iSGFileModDate is MM/DD/YYYY HH:MM:SS");
    check(strcmp(iSGFileModDate(isg, "NOPE00"), "<Unknown Modification>") == 0,
          "a missing file reports an unknown date");

    char stamp[64];
    iSGMakeTimeStamp(stamp);
    check(strlen(stamp) == 19, "iSGMakeTimeStamp uses the same format");

    // An empty file is the one kind of damage visible at this level.
    char corrupt[512];
    snprintf(corrupt, sizeof(corrupt), "%s/SpongeBob01", dir);
    fclose(fopen(corrupt, "wb"));
    char bad[ISG_NUM_FILES][64];
    check(iSGCheckForCorruptFiles(isg, bad) == 1, "an empty save file is reported as corrupt");
    check(strcmp(bad[0], "SpongeBob01") == 0, "and it is named");

    // The autosave session watches a target rather than saving through it.
    st_ISGSESSION* mon = iSGAutoSave_Connect(0, NULL, NULL);
    check(mon != NULL, "iSGAutoSave_Connect attaches to target 0");
    check(iSGAutoSave_Monitor(mon, 0) == 1, "the target is still there");
    check(iSGAutoSave_Monitor(NULL, 0) == 0, "a null session monitors nothing");
    iSGAutoSave_Disconnect(mon);

    iSGSessionEnd(isg);
    check(iSGShutdown() == 1, "iSGShutdown succeeds");

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    if (system(cmd) != 0)
    {
        printf("    (could not clean up %s)\n", dir);
    }
    iHostSetEnv("BFBB_SAVE_DIR", NULL);
}

// The game owns gSnd; the platform layer only reads and writes it. Defining it
// here is what makes this a unit test of the seam rather than of the game.
xSndGlobals gSnd;

// ---------------------------------------------------------------------------
// Stand-ins for the rest of the game
//
// iSnd.cpp reaches four vector helpers and one xSnd function to compute a
// voice's mix. Their real definitions are in xVec3.cpp and xSnd.cpp, neither of
// which this target can link -- see the note beside iMath3 in CMakeLists.txt
// for why pulling in the vector helpers alone is not possible. The four vector
// ones are reimplemented rather than stubbed, because the pan and attenuation
// tests below depend on them being right; the xSnd one is a genuine no-op here,
// since these tests place a voice's playPos themselves rather than deriving it
// from an entity.

void xVec3Sub(xVec3* o, const xVec3* a, const xVec3* b)
{
    o->x = a->x - b->x;
    o->y = a->y - b->y;
    o->z = a->z - b->z;
}

F32 xVec3Length2(const xVec3* v)
{
    return v->x * v->x + v->y * v->y + v->z * v->z;
}

F32 xVec3Dot(const xVec3* a, const xVec3* b)
{
    return a->x * b->x + a->y * b->y + a->z * b->z;
}

F32 xVec3Normalize(xVec3* o, const xVec3* v)
{
    F32 len = sqrtf(xVec3Length2(v));
    if (len > 0.0f)
    {
        o->x = v->x / len;
        o->y = v->y / len;
        o->z = v->z / len;
    }
    else
    {
        o->x = o->y = o->z = 0.0f;
    }
    return len;
}

void xSndInternalUpdateVoicePos(xSndVoiceInfo*)
{
}

// ---------------------------------------------------------------------------
// A fake package, for the sample cache
//
// iSndData.cpp finds a sound's bytes through three asset-system calls: which
// package holds the asset, where in that package it starts, and the string hash
// that ties the two together. The real ones live in xstransvc.cpp, on top of the
// whole packer; these three replace them with one file in the scratch
// directory, which is enough to exercise everything the cache actually does --
// the sector arithmetic, the read, the refcount and the eviction.

static char sFakePkgName[64];
static U32 sFakePkgAsset;
static U32 sFakePkgOffset;
static U32 sFakePkgSize;

U32 xStrHash(const char* s)
{
    // Any hash works: the only requirement is that the two calls agree, and
    // xSTGetAssetInfoInHxP below ignores the value entirely.
    U32 h = 0;
    while (*s != '\0')
    {
        h = h * 31 + (U8)*s++;
    }
    return h;
}

char* xST_xAssetID_HIPFullPath(U32 aid)
{
    return aid == sFakePkgAsset ? sFakePkgName : NULL;
}

S32 xSTGetAssetInfoInHxP(U32 aid, st_PKR_ASSET_TOCINFO* info, U32)
{
    if (aid != sFakePkgAsset)
    {
        return 0;
    }

    memset(info, 0, sizeof(*info));
    info->aid = aid;

    // The split PKR_GetAssetInfo performs, against a base sector of 0 -- which
    // is what iFileGetInfo reports on a host. iSndData has to put it back
    // together to reach the bytes.
    info->sector = sFakePkgOffset / 32;
    info->plus_offset = sFakePkgOffset % 32;
    info->size = sFakePkgSize;

    return 1;
}

// The Xbox sound table layout iSnd.cpp parses -- a 12-byte count header and
// fixed 44-byte entries built around a WAVEFORMATEX, which is what the retail
// Xbox assets this port reads actually contain. Declared independently on
// purpose: if the two ever disagree, this test fails rather than the port
// silently walking a real asset with the wrong stride.
struct test_sndhdr
{
    U16 format_tag; // 0x00
    U16 channels; // 0x02
    U32 samples_per_sec; // 0x04
    U32 avg_bytes_per_sec; // 0x08
    U16 block_align; // 0x0C
    U16 bits_per_sample; // 0x0E
    U16 cb_size; // 0x10
    U16 pad12; // 0x12
    U32 data_size; // 0x14
    U32 assetID; // 0x18
    U32 runtime[4]; // 0x1C
};

struct test_sndinfo
{
    U32 num_sfx;
    U32 num_streams;
    U32 num_cutscene;
    test_sndhdr entry[2];
};

// What xSnd.cpp casts iSndLookup's result to. Reproduced here so the offsets
// this file checks are the ones the game actually reads.
struct test_lookup
{
    U32 num_samples; // 0x00
    U32 num_nibbles; // 0x04
    U32 sample_rate; // 0x08
    U8 pad0C[0x58]; // 0x0C
    S32 ID; // 0x64
};

#define SND_ASSET 0x11111111
#define STRM_ASSET 0x22222222

// ---------------------------------------------------------------------------
// The sample cache

static void test_snd_data()
{
    printf("iSndData\n");

    char dir[512];
    if (!scratch_dir("snddata", dir, sizeof(dir)))
    {
        printf("  (no scratch directory; skipped)\n");
        return;
    }

    // A package with a recognisable payload at an offset that is NOT a multiple
    // of the 32-byte sector, so a sector calculation that drops the remainder
    // fails here rather than silently reading the wrong bytes in the game.
    const U32 kOffset = 32 * 5 + 13;
    const U32 kSize = 256;

    char path[600];
    snprintf(path, sizeof(path), "%s/fake.HOP", dir);

    FILE* f = fopen(path, "wb");
    check(f != NULL, "the fake package could be created");
    if (f == NULL)
    {
        return;
    }

    for (U32 i = 0; i < kOffset; i++)
    {
        fputc(0xAA, f);
    }
    for (U32 i = 0; i < kSize; i++)
    {
        fputc((int)(i & 0xff), f);
    }
    fclose(f);

    iFileInit();
    iFileSetPath(dir);

    snprintf(sFakePkgName, sizeof(sFakePkgName), "fake.HOP");
    sFakePkgAsset = 0xABCD1234;
    sFakePkgOffset = kOffset;
    sFakePkgSize = kSize;

    iSndDataReset();

    // 16-bit mono PCM: the format almost every asset in the game uses, and the
    // one iSndDataAcquire passes through untouched.
    iSndDataFormat pcm;
    pcm.format_tag = 1;
    pcm.channels = 1;
    pcm.block_align = 2;

    U32 bytes = 0;
    const U8* p = (const U8*)iSndDataAcquire(sFakePkgAsset, &pcm, &bytes);

    check(p != NULL, "a known asset reads");
    check(bytes == kSize, "with the size the table gave");

    if (p != NULL)
    {
        // The whole payload, not just its first byte: an off-by-one in the
        // sector arithmetic would still get byte 0 right about one time in 32.
        bool exact = true;
        for (U32 i = 0; i < kSize; i++)
        {
            if (p[i] != (U8)(i & 0xff))
            {
                exact = false;
                break;
            }
        }
        check(exact, "and the bytes at the sector-plus-offset the TOC describes");
    }

    check(iSndDataAcquire(0x99999999, &pcm, &bytes) == NULL, "an unknown asset reads nothing");
    check(bytes == 0, "and reports no size");

    // Cached: the same pointer, without going back to the file.
    U32 again = 0;
    const void* second = iSndDataAcquire(sFakePkgAsset, &pcm, &again);
    check(second == p, "a second acquire returns the cached block");
    check(again == kSize, "with the same size");

    U32 entries = 0;
    U32 total = 0;
    U32 pinned = 0;
    iSndDataStats(&entries, &total, &pinned);
    check(entries == 1, "one entry is cached");
    check(total == kSize, "holding the asset's bytes");
    check(pinned == 1, "and it is pinned while two references are out");

    iSndDataRelease(sFakePkgAsset);
    iSndDataStats(NULL, NULL, &pinned);
    check(pinned == 1, "still pinned after one of the two is released");

    iSndDataRelease(sFakePkgAsset);
    iSndDataStats(NULL, NULL, &pinned);
    check(pinned == 0, "and unpinned once both are");

    // A release without a matching acquire must not underflow into a pin that
    // can never be lifted.
    iSndDataRelease(sFakePkgAsset);
    iSndDataStats(NULL, NULL, &pinned);
    check(pinned == 0, "an unmatched release does not corrupt the count");

    iSndDataReset();
    iSndDataStats(&entries, &total, NULL);
    check(entries == 0 && total == 0, "reset empties the cache");

    // --- Xbox ADPCM ---------------------------------------------------------
    // The 21 menu music tracks are format tag 0x69 in 36-byte mono blocks, and
    // they reach the mixer decoded. Two things have to be right: the sample
    // count, which is 65 per block and not the 64 the assets' own header field
    // implies; and the decode itself, checked here against values worked out by
    // hand from the IMA step table.
    const U32 kBlock = 36;

    snprintf(path, sizeof(path), "%s/adpcm.HOP", dir);
    f = fopen(path, "wb");
    check(f != NULL, "the ADPCM package could be created");
    if (f == NULL)
    {
        return;
    }

    // One block: predictor 1000, step index 0 (step 7), then 32 payload bytes.
    // The first two nibbles are 0 and 8 -- +step/8 and -step/8 -- so the first
    // three samples are 1000, 1000, 1000. Every later byte is 0x00, which is
    // nibble 0 twice: a rise of step>>3 each, with the index walking down and
    // staying at 0.
    fputc(1000 & 0xff, f);
    fputc((1000 >> 8) & 0xff, f);
    fputc(0, f); // step index
    fputc(0, f); // reserved
    fputc(0x80, f); // low nibble 0, high nibble 8
    for (U32 i = 1; i < kBlock - 4; i++)
    {
        fputc(0x00, f);
    }
    fclose(f);

    snprintf(sFakePkgName, sizeof(sFakePkgName), "adpcm.HOP");
    sFakePkgAsset = 0x0AD9C000;
    sFakePkgOffset = 0;
    sFakePkgSize = kBlock;

    iSndDataFormat adpcm;
    adpcm.format_tag = 0x69;
    adpcm.channels = 1;
    adpcm.block_align = kBlock;

    U32 abytes = 0;
    const S16* a = (const S16*)iSndDataAcquire(sFakePkgAsset, &adpcm, &abytes);

    check(a != NULL, "an ADPCM asset decodes");
    check(abytes == 65 * sizeof(S16),
          "a 36-byte block is 65 samples, not the 64 its own header implies");

    if (a != NULL)
    {
        check(a[0] == 1000, "the block's stored predictor is its first sample");
        check(a[1] == 1000, "nibble 0 raises it by step/8, which is 0 at step 7");
        check(a[2] == 1000, "and nibble 8 lowers it by the same");

        // Index walks 0 -> 0 (kImaIndex[0] is -1, clamped at 0), so step stays
        // 7 and every remaining nibble adds 7>>3 == 0. A decoder that got the
        // table or the clamp wrong drifts away from this immediately.
        bool flat = true;
        for (U32 i = 3; i < 65; i++)
        {
            if (a[i] != 1000)
            {
                flat = false;
                break;
            }
        }
        check(flat, "and the rest of the block holds, with the index clamped at zero");
    }

    iSndDataRelease(sFakePkgAsset);
    iSndDataReset();
}

// ---------------------------------------------------------------------------
// The mixer
//
// Only built against the win32 backend, which is the only one that has a mixer.
// The null backend's contract -- silent, but keeps time -- is checked by
// test_snd below and holds for both.

#ifdef BFBB_AUDIO_BACKEND_WIN32
void iSndHostWin32TestMix(U32 rate, float* out, U32 frames);

static void test_snd_mixer()
{
    printf("iSndHost (mixer)\n");

    iSndHostInit();

    // A ramp, so that any read position can be checked against the value it
    // should have produced. 16-bit mono at 1000 Hz: 100 frames, one tenth of a
    // second, and sample n is n/32768.
    static S16 ramp[100];
    for (S32 i = 0; i < 100; i++)
    {
        ramp[i] = (S16)(i * 100);
    }

    iSndHostSample s;
    memset(&s, 0, sizeof(s));
    s.data = ramp;
    s.bytes = sizeof(ramp);
    s.channels = 1;
    s.bits = 16;
    s.sample_rate = 1000;
    s.num_samples = 100;

    static float out[512];

    // --- rate conversion -----------------------------------------------------
    // Mixed at twice the source rate, so each output frame advances half a
    // source frame and the block covers 50 source frames.
    S32 v = iSndHostAcquire(0);
    check(v >= 0, "a voice for the mixer test");

    iSndHostSetVol(v, 1.0f, 1.0f);
    iSndHostStart(v, &s);

    iSndHostWin32TestMix(2000, out, 100);

    check(fabsf(out[0] - 0.0f) < 0.001f, "the first output frame is the first sample");
    check(fabsf(out[2 * 2] - (100.0f / 32768.0f)) < 0.001f,
          "output frame 2 is source frame 1 at double the rate");
    check(fabsf(out[2 * 99] - (4950.0f / 32768.0f)) < 0.001f,
          "and output frame 99 is halfway through the sample");
    check(iSndHostIsPlaying(v), "which is not the end of it");

    // Interpolated, not held: an odd output frame falls between two samples.
    // Frame 3 is source position 1.5, so halfway between ramp[1] and ramp[2].
    check(fabsf(out[2 * 3] - (150.0f / 32768.0f)) < 0.001f,
          "a fractional position interpolates between neighbours");

    // Both sides carry a mono source.
    check(fabsf(out[2 * 2] - out[2 * 2 + 1]) < 0.0001f, "a mono source reaches both sides");

    // --- running out ---------------------------------------------------------
    iSndHostWin32TestMix(2000, out, 100);
    check(!iSndHostIsPlaying(v), "the voice finishes when the samples run out");

    // --- volume per side -----------------------------------------------------
    iSndHostSetVol(v, 0.5f, 0.0f);
    iSndHostStart(v, &s);
    iSndHostWin32TestMix(1000, out, 16);

    check(fabsf(out[2 * 8] - (800.0f / 32768.0f) * 0.5f) < 0.001f, "the left gain applies");
    check(fabsf(out[2 * 8 + 1]) < 0.0001f, "and a zero right gain silences that side");

    // --- looping -------------------------------------------------------------
    s.looping = true;
    iSndHostSetVol(v, 1.0f, 1.0f);
    iSndHostStart(v, &s);

    // 150 output frames at the source rate: past the 100-frame sample, so the
    // last 50 are the loop's second pass.
    iSndHostWin32TestMix(1000, out, 150);
    check(iSndHostIsPlaying(v), "a looping voice does not finish on its own");
    check(fabsf(out[2 * 120] - (2000.0f / 32768.0f)) < 0.001f,
          "and wraps to the start rather than running off the end");

    iSndHostStop(v);
    check(!iSndHostIsPlaying(v), "iSndHostStop ends it");

    // --- a voice with no samples --------------------------------------------
    // The case a missing or unreadable asset produces. It must still finish on
    // time, because that is what everything in the game that waits on a sound
    // depends on.
    iSndHostSample q = s;
    q.data = NULL;
    q.bytes = 0;
    q.looping = false;
    q.num_samples = 100;

    iSndHostStart(v, &q);
    check(iSndHostIsPlaying(v), "a voice with no samples still starts");

    iSndHostWin32TestMix(1000, out, 50);
    check(iSndHostIsPlaying(v), "and is still going halfway through its length");
    check(fabsf(out[0]) < 0.0001f, "while contributing nothing to the mix");

    iSndHostWin32TestMix(1000, out, 60);
    check(!iSndHostIsPlaying(v), "and finishes at the end of it");

    iSndHostRelease(v);
    iSndHostExit();
}
#endif

static void test_snd()
{
    printf("iSnd\n");

    // Force the silent path. The win32 backend opens a real endpoint otherwise,
    // which would make the timing below depend on an audio device being present
    // and on its render thread's schedule. What is under test here is the
    // timing contract, which both paths must keep identically -- the mixing
    // itself is tested above.
    iHostSetEnv("BFBB_AUDIO", "0");

    check(sizeof(xSndVoiceInfo) == 100,
          "xSndVoiceInfo is 100 bytes, as iSndPlay's offset/100 assumes");
    check(offsetof(test_lookup, ID) == 0x64, "the lookup id sits at 0x64, where xSnd.cpp reads it");
    check(sizeof(test_sndhdr) == 44, "an Xbox sound table entry is 44 bytes");
    check(offsetof(test_sndinfo, entry) == 12, "the Xbox sound table header is 12 bytes");

    memset(&gSnd, 0, sizeof(gSnd));
    for (S32 i = 0; i < 8; i++)
    {
        gSnd.categoryVolFader[i] = 1.0f;
    }

    iSndInit();

    // A table with one sound and one stream, in the order iSndLookup walks:
    // sfx first, then streams, then cutscene.
    static test_sndinfo table;
    memset(&table, 0, sizeof(table));
    table.num_sfx = 1;
    table.num_streams = 1;
    table.num_cutscene = 0;

    // 16-bit mono, so block_align is 2 and the sample count is data_size / 2.
    table.entry[0].assetID = SND_ASSET;
    table.entry[0].format_tag = 1;
    table.entry[0].channels = 1;
    table.entry[0].samples_per_sec = 32000;
    table.entry[0].block_align = 2;
    table.entry[0].bits_per_sample = 16;
    table.entry[0].data_size = 3200 * 2; // 0.1 s at 32 kHz

    table.entry[1].assetID = STRM_ASSET;
    table.entry[1].format_tag = 1;
    table.entry[1].channels = 1;
    table.entry[1].samples_per_sec = 32000;
    table.entry[1].block_align = 2;
    table.entry[1].bits_per_sample = 16;
    table.entry[1].data_size = 32000 * 2; // 1.0 s

    check(iSndLoadSounds(&table) == 1, "iSndLoadSounds accepts a table");

    check(iSndLookup(0) == NULL, "asset id 0 looks up to nothing");
    check(iSndLookup(0x99999999) == NULL, "an unknown asset looks up to nothing");

    test_lookup* lk = (test_lookup*)iSndLookup(SND_ASSET);
    check(lk != NULL, "a sound is found");
    if (lk != NULL)
    {
        check(lk->sample_rate == 32000, "the lookup reports the sample rate");
        check(lk->num_samples == 3200, "the lookup reports the sample count");
        check(lk->ID >= 0x1000, "a sound gets an id at or above 0x1000");
    }

    test_lookup* sk = (test_lookup*)iSndLookup(STRM_ASSET);
    check(sk != NULL, "a stream is found");
    if (sk != NULL)
    {
        check(sk->ID > 0 && sk->ID < 0x1000, "a stream gets an id below 0x1000");
    }

    // Voice allocation: flag 4 means stream, and streams live in the first six
    // slots. Everything else lands at 6 or above.
    S32 sv = iSndFindFreeVoice(128, 0x4, 0);
    check(sv >= 0 && sv < 6, "a stream voice comes from the first six slots");

    S32 nv = iSndFindFreeVoice(128, 0x2, 0);
    check(nv >= 6 && nv < 64, "a sound voice comes from slot 6 or above");

    // Exhaust the stream slots; the sixth request must fail rather than spill
    // into the sound range.
    S32 taken = 1;
    while (taken < 6 && iSndFindFreeVoice(128, 0x4, 0) >= 0)
    {
        taken++;
    }
    check(taken == 6, "all six stream slots can be taken");
    check(iSndFindFreeVoice(128, 0x4, 0) == -1,
          "a seventh stream request fails rather than using a sound slot");

    // A scene ending frees the voices AND pops the sound table, because every
    // caller pairs it with unloading the package that table came from. Without
    // the pop the array only grows -- twelve deep, so a few level changes leave
    // a scene with no sound at all -- and worse, each table left behind points
    // into an unloaded package that iSndLookup goes on reading.
    iSndSceneExit();

    check(iSndLookup(SND_ASSET) == NULL, "a scene ending pops its sound table");

    // So the scene that follows brings its own, exactly as a level load does.
    check(iSndLoadSounds(&table) == 1, "and the next scene's table loads in its place");

    // Timing. Silent but on the clock, because the game waits on sounds; a
    // voice must report playing for the sample's length.
    lk = (test_lookup*)iSndLookup(SND_ASSET);
    S32 v = iSndFindFreeVoice(128, 0x2, 0);
    check(v >= 6, "a voice for the timing test");

    xSndVoiceInfo* vp = &gSnd.voice[v];
    vp->assetID = SND_ASSET;
    vp->sndID = 0x4242;
    vp->sample_rate = 32000;
    vp->vol = 1.0f;
    vp->pitch = 1.0f;
    vp->flags = 0x2 | 1;
    vp->category = (sound_category)0;

    check(iSndPlay(vp) == 0x4242, "iSndPlay returns the handle");
    check(iSndIsPlaying(SND_ASSET), "the sound is playing straight after starting");
    check(iSndIsPlayingByHandle(0x4242), "and by handle");
    check(!iSndIsPlayingByHandle(0), "handle 0 is never playing");

    check(iSndGetVol(0x4242) == 1.0f, "iSndGetVol reports what was set");
    iSndSetVol(0x4242, 0.25f);
    check(iSndGetVol(0x4242) == 0.25f, "iSndSetVol updates it");

    // 0.1 s of samples: still playing well before, finished well after.
    iHostSleepUntilNs(iHostMonotonicNs() + 30000000ULL); // 0.03 s
    check(iSndIsPlayingByHandle(0x4242), "still playing a third of the way in");

    iHostSleepUntilNs(iHostMonotonicNs() + 120000000ULL); // past the end

    // Through iSndHostUpdate, because that is where iSndHost.h says a backend
    // retires voices -- "a backend that finishes voices on its own clock
    // retires them here". The null backend also happens to answer correctly
    // between updates, since it recomputes a deadline on every call; a mixer
    // cannot, because a voice ends when its samples have been consumed and
    // nothing consumes them until the block is mixed. Only the weaker promise
    // is common to both, and it is the one the game relies on: iSndUpdate calls
    // iSndHostUpdate before its own retirement sweep, every frame.
    iSndHostUpdate();
    check(!iSndHostIsPlaying(0), "the device retires the voice when its time is up");

    iSndUpdate();
    check(!iSndIsPlayingByHandle(0x4242), "iSndUpdate clears the finished handle");
    check(gSnd.voice[v].sndID == 0, "and the game-side slot with it");

    // Pause holds a voice past its natural end.
    lk = (test_lookup*)iSndLookup(SND_ASSET);
    v = iSndFindFreeVoice(128, 0x2, 0);
    vp = &gSnd.voice[v];
    vp->assetID = SND_ASSET;
    vp->sndID = 0x4343;
    vp->sample_rate = 32000;
    vp->vol = 1.0f;
    vp->pitch = 1.0f;
    iSndPlay(vp);

    iSndPause(0x4343, 1);
    iHostSleepUntilNs(iHostMonotonicNs() + 150000000ULL); // past its length
    iSndUpdate();
    check(iSndIsPlayingByHandle(0x4343), "a paused voice does not finish on its own");

    iSndStop(0x4343);
    check(!iSndIsPlayingByHandle(0x4343), "iSndStop ends it");
    check(gSnd.voice[v].sndID == 0, "and clears the game-side handle");

    // Pitch is in SEMITONES, not a playback ratio: retail converts with
    // powf(2, pitch/12) everywhere it reaches AX. Checked through the clock
    // rather than by reading a number back, because the number the backend
    // holds is the ratio and the thing worth pinning down is that the
    // conversion happened at all. +12 is an octave, so a 0.1 s sample must be
    // finished by 0.08 s -- and the same voice at pitch 0 must not be.
    //
    // The commonest value is 0, which converts to 1.0 either way, so a port
    // that forgot the conversion looks correct until the HUD counter reaches
    // 6.5 semitones and plays six and a half times too fast.
    for (S32 pass = 0; pass < 2; pass++)
    {
        F32 semitones = (pass == 0) ? 12.0f : 0.0f;

        lk = (test_lookup*)iSndLookup(SND_ASSET);
        v = iSndFindFreeVoice(128, 0x2, 0);
        vp = &gSnd.voice[v];
        vp->assetID = SND_ASSET;
        vp->sndID = 0x4400 + pass;
        vp->sample_rate = 32000;
        vp->vol = 1.0f;
        vp->pitch = semitones;
        vp->flags = 0x2 | 1;
        vp->category = (sound_category)0;
        iSndPlay(vp);

        iHostSleepUntilNs(iHostMonotonicNs() + 80000000ULL); // 0.08 s
        iSndHostUpdate();

        if (pass == 0)
        {
            check(!iSndIsPlayingByHandle(vp->sndID),
                  "+12 semitones is an octave up, so the sample ends in half the time");
        }
        else
        {
            check(iSndIsPlayingByHandle(vp->sndID),
                  "and the same sample at pitch 0 is still playing then");
        }

        iSndStop(0x4400 + pass);
    }

    iSndExit();
    iHostSetEnv("BFBB_AUDIO", NULL);
}

// ---------------------------------------------------------------------------
// The HIP container reader.
//
// The container is big-endian on every platform, including Xbox -- it is the
// packer's format, not the console's -- so a little-endian host has to swap
// every chunk id and size on the way in.
//
// It already does, and that is the point of this test. xbinio.cpp has carried
// ReadMShorts/ReadMLongs/ReadMFloats behind `#if ENDIAN == LITTLE_ENDIAN` since
// retail: Heavy Iron shipped this engine on Xbox and PS2 as well as the
// GameCube, and the decomp preserved the mechanism. ENDIAN is selected by
// GAMECUBE, so the port gets the swapping for free.
//
// Worth pinning down with a test precisely because it looks like something the
// port would have to add. It does not, and adding it double-swaps -- which is
// what happened before this test existed to say so.
//
// Synthesised rather than pointed at a retail file on purpose: the check has to
// run for anyone, and a hand-built container is what pins the byte order down.
// Set BFBB_HIP to a real .HIP as well and the last part of this reads that too.

static void put32(FILE* f, U32 v)
{
    fputc((v >> 24) & 0xff, f);
    fputc((v >> 16) & 0xff, f);
    fputc((v >> 8) & 0xff, f);
    fputc(v & 0xff, f);
}

static void put_tag(FILE* f, const char* tag)
{
    fwrite(tag, 1, 4, f);
}

#define TAG4(a, b, c, d) (((U32)(a) << 24) | ((U32)(b) << 16) | ((U32)(c) << 8) | (U32)(d))

static void test_hip()
{
    printf("HIP container reader\n");

    char dir[512];
    if (!scratch_dir("hip", dir, sizeof(dir)))
    {
        check(false, "could not make a temp directory");
        return;
    }

    char path[600];
    snprintf(path, sizeof(path), "%s/synth.HIP", dir);

    // HIPA(0) PACK{ PVER=<3 longs> PFLG=<1 long> } -- the shape of a real file's
    // head, with values chosen so a byte-order slip cannot look like a pass.
    FILE* f = fopen(path, "wb");
    check(f != NULL, "the synthetic HIP could be created");
    if (f == NULL)
    {
        return;
    }

    put_tag(f, "HIPA");
    put32(f, 0);
    put_tag(f, "PACK");
    put32(f, 8 + 12 + 8 + 4);
    put_tag(f, "PVER");
    put32(f, 12);
    put32(f, 2);
    put32(f, 0x000a000f);
    put32(f, 1);
    put_tag(f, "PFLG");
    put32(f, 4);
    put32(f, 0x022a002e);
    fclose(f);

    // iFile resolves names against a base path, so point it at the scratch
    // directory and open the file by name, exactly as the game does.
    iFileInit();
    iFileSetPath(dir);

    st_HIPLOADFUNCS* hf = get_HIPLFuncs();
    check(hf != NULL, "get_HIPLFuncs returns the function map");

    st_HIPLOADDATA* ld = hf->create("synth.HIP", NULL, 0);
    check(ld != NULL, "the reader opens it");
    if (ld == NULL)
    {
        return;
    }

    check(hf->basesector(ld) == 0,
          "base sector is 0 on a host, where there is no disc");

    U32 cid = hf->enter(ld);
    check(cid == TAG4('H', 'I', 'P', 'A'), "first chunk is HIPA");
    hf->exit(ld);

    cid = hf->enter(ld);
    check(cid == TAG4('P', 'A', 'C', 'K'), "second chunk is PACK");

    cid = hf->enter(ld);
    check(cid == TAG4('P', 'V', 'E', 'R'), "PACK's first child is PVER");

    // The values, which is where a byte-order slip actually shows: a swapped
    // 2 reads as 0x02000000, and a swapped 0x000a000f as 0x0f000a00.
    S32 v[3] = { 0, 0, 0 };
    S32 got = hf->readLongs(ld, v, 3);
    check(got == 3, "three longs read out of PVER");
    check(v[0] == 2, "PVER's first long is 2, swapped back from big-endian");
    check(v[1] == 0x000a000f, "PVER's second long survives the round trip");
    check(v[2] == 1, "PVER's third long is 1");
    hf->exit(ld);

    cid = hf->enter(ld);
    check(cid == TAG4('P', 'F', 'L', 'G'), "PACK's second child is PFLG");
    S32 flg = 0;
    hf->readLongs(ld, &flg, 1);
    check(flg == 0x022a002e, "PFLG's value survives the round trip");
    hf->exit(ld);

    hf->exit(ld);
    hf->destroy(ld);

    iHostRemoveFile(path);
    iHostRemoveDir(dir);

    // If a real game HIP is to hand, walk its head too. Retail files are the
    // only thing that proves the synthetic one has the shape right.
    const char* real = getenv("BFBB_HIP");
    if (real == NULL || real[0] == '\0')
    {
        printf("    (set BFBB_HIP to a retail .HIP to check one of those too)\n");
        return;
    }

    // Split the retail path into a directory to point iFile at and a name.
    char rdir[512];
    snprintf(rdir, sizeof(rdir), "%s", real);
    char* slash = strrchr(rdir, '/');
    char* back = strrchr(rdir, '\\');
    if (back > slash)
    {
        slash = back;
    }
    const char* rname = real;
    if (slash != NULL)
    {
        *slash = '\0';
        rname = real + (slash - rdir) + 1;
        iFileSetPath(rdir);
    }

    st_HIPLOADDATA* rl = hf->create(rname, NULL, 0);
    check(rl != NULL, "a retail HIP opens");
    if (rl == NULL)
    {
        return;
    }

    check(hf->enter(rl) == TAG4('H', 'I', 'P', 'A'), "retail: first chunk is HIPA");
    hf->exit(rl);
    check(hf->enter(rl) == TAG4('P', 'A', 'C', 'K'), "retail: second chunk is PACK");
    check(hf->enter(rl) == TAG4('P', 'V', 'E', 'R'), "retail: PACK begins with PVER");
    hf->exit(rl);
    hf->exit(rl);
    check(hf->enter(rl) == TAG4('D', 'I', 'C', 'T'), "retail: then DICT");
    check(hf->enter(rl) == TAG4('A', 'T', 'O', 'C'), "retail: DICT begins with ATOC");
    hf->exit(rl);
    hf->exit(rl);
    hf->destroy(rl);
    iFileExit();
}

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("bfbb PC platform layer selftest\n\n");

    test_time();
    test_math();
    test_mem();
    test_file();
    test_idtag();
    test_pad();
    test_savegame();
    test_snd_data();
#ifdef BFBB_AUDIO_BACKEND_WIN32
    test_snd_mixer();
#endif
    test_snd();
    test_hip();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "passed", failures,
           failures == 1 ? "" : "s");

    return failures ? 1 : 0;
}
