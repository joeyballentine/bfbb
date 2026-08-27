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

static void test_snd()
{
    printf("iSnd\n");

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

    iSndSceneExit();

    // Timing. The null backend is silent but keeps the clock, because the game
    // waits on sounds; a voice must report playing for the sample's length.
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

    iSndExit();
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
    test_snd();
    test_hip();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "passed", failures,
           failures == 1 ? "" : "s");

    return failures ? 1 : 0;
}
