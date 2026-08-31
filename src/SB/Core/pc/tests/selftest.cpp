// Checks the parts of the PC platform layer that do not need a renderer.
//
// The GameCube side is scored by a byte-identical DOL. There is no equivalent
// for a port, so the substitute is this: each thing the layer claims to do is
// exercised and its answer checked, so that "implemented" is a measurement
// rather than an assertion.

#include <math.h>
#include "xordarray.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <types.h>

#include "iConfig.h"
#include "iDrawDist.h"
#include "iFile.h"
#include "iHost.h"
#include "iSnd.h"
#include "iSndData.h"
#include "iSoundtrack.h"
#include "xpkrsvc.h"
#include "xhipio.h"
#include "iSndHost.h"
#include "iSndReverb.h"
#include "xSnd.h"
#include "iMath.h"
#include "iMemMgr.h"
#include "iPadBind.h"
#include "iPadHost.h"
#include "iPadStick.h"
#include "iScreen.h"
#include "iSystem.h"
#include "iTextPatch.h"
#include "isavegame.h"
#include "iTime.h"
#include "xFile.h"
#include "xMemMgr.h"

// Only for zFlyKey, whose layout test_flykey checks against the shipping asset.
#include "zCamera.h"

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

// iConfig parses ONCE per process, so this is the only place in the harness
// that may touch it and it has to run first -- hence its position in main().
// BFBB_CONFIG is set before any query, so the load reads this file rather than
// a config.ini someone happens to have beside the test binary, and so that the
// write-when-missing path cannot leave one there either.
// Lines in `text` that begin with `prefix`, ignoring leading spaces. Counting
// LINES rather than occurrences because the settings' own documentation
// mentions the names of settings, and a substring search cannot tell a comment
// about a key from the key itself.
static int countLinesStartingWith(const char* text, const char* prefix)
{
    size_t n = strlen(prefix);
    int count = 0;
    const char* p = text;

    while (*p != '\0')
    {
        const char* line = p;
        while (*line == ' ' || *line == '\t')
        {
            line++;
        }
        if (strncmp(line, prefix, n) == 0)
        {
            count++;
        }
        const char* nl = strchr(p, '\n');
        if (nl == NULL)
        {
            break;
        }
        p = nl + 1;
    }
    return count;
}

static void test_config()
{
    printf("iConfig\n");

    char dir[512];
    if (!scratch_dir("config", dir, sizeof(dir)))
    {
        check(false, "could not make a temp directory");
        return;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/config.ini", dir);

    // Deliberately awkward: mixed case in the section and the key, both comment
    // spellings, ragged whitespace, a blank line, three spellings of true, and
    // one key that does not exist.
    static const char kFile[] = "; the port's settings\n"
                                "[assets]\n"
                                "path = D:\\my games\\bfbb xbox\n"
                                "[Video]\n"
                                "mode = Borderless\n"
                                "width = 1280\n"
                                "height  =  960\n"
                                "Draw_Distance = no\n"
                                "[XBOX]\n"
                                "  Glow   =   OFF   \n"
                                "distortion=yes\n"
                                "\n"
                                "snapshot = 1   # trailing comment\n"
                                "reverb=TRUE\n"
                                "glwo = off\n"
                                "[input]\n"
                                "controller = 3\n"
                                "[pad]\n"
                                "select = ls\n"
                                "nonsense = a\n"
                                "[keyboard]\n"
                                "start = f1\n";

    FILE* f = fopen(path, "wb");
    if (f == NULL)
    {
        check(false, "could not write a config.ini");
        return;
    }
    fwrite(kFile, 1, sizeof(kFile) - 1, f);
    fclose(f);

    check(iHostSetEnv("BFBB_CONFIG", path), "BFBB_CONFIG can be set");

    iConfigLoad();

    check(iConfigPath() != NULL && strcmp(iConfigPath(), path) == 0,
          "iConfigPath is the file BFBB_CONFIG named");

    // Each read against a default that is the OPPOSITE of the file's value, so
    // a key that failed to parse cannot pass by agreeing with its fallback.
    check(iConfigGetBool("xbox.glow", TRUE) == FALSE, "'OFF' is false, and the key is folded");
    check(iConfigGetBool("xbox.distortion", FALSE) == TRUE, "'yes' is true");
    check(iConfigGetBool("xbox.snapshot", FALSE) == TRUE, "'1' is true, past a # comment");
    check(iConfigGetBool("xbox.reverb", FALSE) == TRUE, "'TRUE' is true");

    // The render size. Read as ints against a fallback that is neither the
    // file's value nor the table's default, so neither can pass by accident.
    check(iConfigGetInt("video.width", 1) == 1280, "the render width comes off the file");
    check(iConfigGetInt("video.height", 1) == 960, "and the height, past ragged spacing");

    // A string setting, and the one whose value is neither a number nor a
    // boolean. iSystem maps it onto iWindowMode case-insensitively, so the
    // file's mixed case has to survive the parse rather than be folded the way
    // the key is.
    check(strcmp(iConfigGetString("video.mode", "fullscreen"), "Borderless") == 0,
          "the window mode comes off the file with its case intact");
    check(iConfigGetBool("video.draw_distance", TRUE) == FALSE,
          "'no' is false, in a key that has an underscore in it");

    // The asset root. A Windows path with spaces and backslashes in it, which
    // is what someone actually pastes in here: nothing may be quoted, escaped
    // or split at the space, and the value has to be longer than a word --
    // this is the setting that decides how much room an entry gets.
    check(strcmp(iConfigGetString("assets.path", ""), "D:\\my games\\bfbb xbox") == 0,
          "the asset path survives spaces and backslashes");

    // An unknown key is rejected at load, so it cannot be read back even under
    // its own name. That is what makes the report at load a guarantee rather
    // than a courtesy.
    check(iConfigGetBool("xbox.glwo", TRUE) == TRUE, "an unknown key is dropped, not stored");

    // A key the table has and the file does not: answered from the table, not
    // from the caller's fallback. Passing FALSE here would pass either way if
    // the table were consulted wrongly, so the fallback is the wrong answer on
    // purpose.
    check(iConfigGetBool("xbox.glow", TRUE) == FALSE, "a second read gives the same answer");

    // A key in neither: the caller's fallback is all there is.
    check(iConfigGetBool("xbox.nothing", TRUE) == TRUE, "an unknown key falls back");
    check(iConfigGetInt("xbox.nothing", 7) == 7, "an unknown int falls back");
    check(iConfigGetFloat("xbox.nothing", 0.5f) == 0.5f, "an unknown float falls back");
    check(strcmp(iConfigGetString("xbox.nothing", "d"), "d") == 0,
          "an unknown string falls back");

    // The two binding sections are not in the settings table -- their contents
    // are one row per game button, and that list lives in iPadBind.cpp. They
    // still have to behave like every other key: known, defaulted from the same
    // place the generated file is written from, and reported when misspelled.
    check(strcmp(iConfigGetString("pad.select", "!"), "ls") == 0,
          "a [pad] binding comes off the file");
    check(strcmp(iConfigGetString("keyboard.start", "!"), "f1") == 0, "and a [keyboard] one");

    const iPadBindButton* b = iPadBindFind("b");
    check(b != NULL && strcmp(iConfigGetString("pad.b", "!"), b->pad) == 0,
          "a binding the file omits is answered from the button table, not the fallback");
    check(b != NULL && strcmp(iConfigGetString("keyboard.b", "!"), b->key) == 0,
          "on both devices");

    check(strcmp(iConfigGetString("pad.nonsense", "!"), "!") == 0,
          "a binding for a button that does not exist is dropped, like any unknown key");

    check(iConfigGetInt("input.controller", 0) == 3, "input.controller comes off the file");

    // The frame rate. The file does not mention it, so this is the table's
    // default reaching a caller whose fallback is the wrong answer -- and it is
    // the one setting where the wrong answer is not visible on screen but in
    // how fast the game runs.
    check(iConfigGetInt("video.framerate", 1) == 60, "video.framerate defaults to the console's rate");
    check(iConfigGetBool("video.vsync", FALSE) == TRUE, "and vsync defaults on");

    // `display` and `off` are words iConfigGetInt cannot parse; iSystem reads
    // the key as a string first and only falls through to the int for a number.
    // So the string accessor has to hand back exactly what the file says.
    check(strcmp(iConfigGetString("video.framerate", "!"), "60") == 0,
          "and reads back as a string, which is how `display` and `off` get through");

    // The load appends the settings this build has and the file did not.
    //
    // A config.ini written by an older build is missing every setting added
    // since, and iConfigWriteDefaults refuses to touch a file that exists -- so
    // without this those settings are invisible, sitting at their defaults with
    // nothing anywhere naming them. The file above omits most of the table, so
    // the load that has already run should have grown it.
    {
        FILE* a = fopen(path, "rb");
        check(a != NULL, "the config file can be read back after the load");
        if (a != NULL)
        {
            char grown[32768];
            size_t gn = fread(grown, 1, sizeof(grown) - 1, a);
            fclose(a);
            grown[gn] = '\0';

            check(strstr(grown, "; Settings added by a newer build") != NULL,
                  "the appended block says where it came from");
            check(strstr(grown, "msaa = 4") != NULL,
                  "a setting the file never had is appended at its default");
            check(strstr(grown, "shadow_resolution = auto") != NULL, "and so is another");

            // The bindings are not in the settings table -- they come from
            // iPadBind.cpp -- and are appended the same way. The file names two
            // of them, so the rest have to arrive.
            check(countLinesStartingWith(grown, "y ") == 2,
                  "a binding the file omits arrives for both devices");

            // What was already there is untouched. The append never rewrites,
            // so a value the file set keeps its own line and gains no second
            // one at the default.
            check(countLinesStartingWith(grown, "width") == 1,
                  "a setting the file already had is not appended again");
            check(strstr(grown, "width = 1280") != NULL, "and keeps the value it had");
            // Spelling and spacing included: the file is grown, never reread
            // and rewritten, so a line someone typed by hand comes back byte
            // for byte and gains no tidied-up twin at the default.
            check(strstr(grown, "  Glow   =   OFF   ") != NULL,
                  "a hand-written line keeps its case and its spacing");
            check(strstr(grown, "glow = on") == NULL,
                  "and the table's default is not appended beside it");

            // The section headers repeat on purpose: the settings are new, the
            // sections they belong to are not.
            check(countLinesStartingWith(grown, "[video]") == 1,
                  "the appended settings sit under a section header of their own");
        }
    }

    iHostRemoveFile(path);

    // The writer, which the load reaches only when there is no file -- and
    // there has already been a load this process, so it is called directly.
    char written[512];
    snprintf(written, sizeof(written), "%s/written.ini", dir);

    check(iConfigWriteDefaults(written), "iConfigWriteDefaults writes a file");
    check(iHostPathExists(written), "and the file is there afterwards");

    // Exclusive. A second call must not overwrite what the first produced --
    // this is the property that stops a launch from erasing someone's edits.
    check(!iConfigWriteDefaults(written), "a second write refuses rather than overwriting");

    // What it wrote has to be a file this parser accepts and has to carry the
    // defaults it claims. Checking the text is what makes the round trip real:
    // a writer that emitted a key the parser then rejects would otherwise pass
    // every check above.
    FILE* r = fopen(written, "rb");
    check(r != NULL, "the written file can be read back");
    if (r != NULL)
    {
        // Big enough for the whole file, which is mostly the settings' own
        // documentation and grows every time one is added. A buffer that only
        // fits most of it fails the LAST checks below and looks like the writer
        // dropping settings rather than the reader stopping early.
        char buf[16384];
        size_t n = fread(buf, 1, sizeof(buf) - 1, r);
        fclose(r);
        buf[n] = '\0';

        check(strstr(buf, "[assets]") != NULL, "it has the [assets] section header");
        check(strstr(buf, "path =") != NULL, "and the asset path, empty as it has to be");
        check(strstr(buf, "[video]") != NULL, "it has the [video] section header");
        check(strstr(buf, "mode = fullscreen") != NULL, "and the window mode at its default");
        check(strstr(buf, "width = 640") != NULL, "and the render width at its default");
        check(strstr(buf, "height = 480") != NULL, "and the height");
        check(strstr(buf, "framerate = 60") != NULL, "and the frame rate at the console's");
        check(strstr(buf, "vsync = on") != NULL, "and vsync");
        check(strstr(buf, "draw_distance = on") != NULL, "and the draw distance");
        check(strstr(buf, "msaa = 4") != NULL, "and the sample count");
        // Written as its own setting, not folded into the one above. Coverage
        // needs samples and does nothing without them, but that is a fact about
        // what the two mean together, not a reason for one to imply the other.
        check(strstr(buf, "alpha_to_coverage = on") != NULL, "and alpha to coverage separately");
        check(strstr(buf, "per_pixel_lighting = on") != NULL, "and per-pixel lighting");
        check(strstr(buf, "[xbox]") != NULL, "it has the [xbox] section header");
        check(strstr(buf, "glow = on") != NULL, "and glow at its default");
        check(strstr(buf, "distortion = on") != NULL, "and distortion");
        check(strstr(buf, "snapshot = on") != NULL, "and snapshot");
        check(strstr(buf, "reverb = on") != NULL, "and reverb");
        check(strstr(buf, "[text]") != NULL, "it has the [text] section header");
        check(strstr(buf, "platform_wording = on") != NULL, "and the text rewrite at its default");
    }

    iHostRemoveFile(written);
    iHostRemoveDir(dir);
}

static void test_screen()
{
    printf("iScreen\n");

    // The console's framebuffer, and what the port renders at until config.ini
    // says otherwise. Checked first because everything below moves it.
    check(iScreenWidth() == 640 && iScreenHeight() == 480, "the default is 640x480");
    check(iScreenWidthF() == 640.0f && iScreenHeightF() == 480.0f,
          "and the float pair agrees with the int pair");

    iScreenSetSize(1280, 960);
    check(iScreenWidth() == 1280 && iScreenHeight() == 960, "a size is taken");
    check(iScreenWidthF() == 1280.0f && iScreenHeightF() == 960.0f, "by both pairs");

    // A refusal must LEAVE the size alone rather than fall back to 640x480:
    // the window has already been opened at whatever is in force by the time
    // anything could pass a bad one, and every camera raster in the game is
    // about to be built at it.
    iScreenSetSize(0, 480);
    check(iScreenWidth() == 1280 && iScreenHeight() == 960, "a zero width is refused");
    iScreenSetSize(640, -1);
    check(iScreenWidth() == 1280 && iScreenHeight() == 960, "a negative height is refused");
    iScreenSetSize(100000, 100000);
    check(iScreenWidth() == 1280 && iScreenHeight() == 960, "and a size no surface could be");

    // --- the UI box -------------------------------------------------------
    //
    // A 4:3 render size is the whole screen, and the numbers a caller that has
    // never heard of widescreen would produce. This is the case that has to
    // stay exact: every 2D site in the game multiplies by these.
    iScreenSetSize(1280, 960);
    check(iScreenAspectF() == 0.75f, "4:3 gives retail's own 0.75 aspect");
    check(iScreenUIWidthF() == 1280.0f && iScreenUIHeightF() == 960.0f,
          "and a UI box that is the whole screen");
    check(iScreenUIOriginXF() == 0.0f && iScreenUIOriginYF() == 0.0f, "at the origin");
    check(iScreenUIFracXF() == 1.0f && iScreenUIFracYF() == 1.0f, "covering all of both axes");

    // Wider than 4:3: the box is height-limited and pillarboxed. 1080 * 4/3 is
    // 1440, so 240 either side of a 1920-wide screen.
    iScreenSetSize(1920, 1080);
    check(iScreenAspectF() == 1080.0f / 1920.0f, "16:9 reports its real aspect");
    check(iScreenUIHeightF() == 1080.0f, "the UI box is as tall as the screen");
    check(iScreenUIWidthF() == 1440.0f, "and 4:3 of that wide");
    check(iScreenUIOriginXF() == 240.0f, "centred, so 240 either side");
    check(iScreenUIOriginYF() == 0.0f, "and flush top and bottom");
    check(iScreenUIFracYF() == 1.0f, "all of the vertical axis");
    check(iScreenUIFracXF() == 1440.0f / 1920.0f, "and three quarters of the horizontal");

    // Narrower than 4:3, which is the same fit the other way up: 1280 * 3/4 is
    // 960, so 32 above and below a 1024-tall screen.
    iScreenSetSize(1280, 1024);
    check(iScreenUIWidthF() == 1280.0f, "5:4 gives a UI box as wide as the screen");
    check(iScreenUIHeightF() == 960.0f, "4:3 of that tall");
    check(iScreenUIOriginXF() == 0.0f && iScreenUIOriginYF() == 32.0f, "centred vertically");
    check(iScreenUIFracXF() == 1.0f, "all of the horizontal axis");

    // --- the HUD anchor ---------------------------------------------------
    //
    // At 4:3 it has to be the identity, exactly. This is the case that runs for
    // anyone playing at the default, and every HUD position goes through it.
    iScreenSetSize(640, 480);
    iScreenSetUIMode(iSCREENUI_NATIVE);
    iScreenSetAnchorRect(0.08f, 0.83f, 0.09f, 0.09f);
    check(iScreenAnchorX(0.0f) == 0.0f && iScreenAnchorX(0.25f) == 0.25f &&
              iScreenAnchorX(1.0f) == 1.0f,
          "at 4:3 the anchor is the identity");
    check(iScreenUIMarginXF() == 0.0f && iScreenUIMarginYF() == 0.0f, "and there is no margin");

    // 16:9. The box is three quarters of the width, so the screen reaches
    // (1 - 0.75) / (2 * 0.75) = 1/6 of a box-width past each side.
    iScreenSetSize(1920, 1080);
    const F32 margin = 1.0f / 6.0f;
    check(fabsf(iScreenUIMarginXF() - margin) < 0.0001f, "16:9 puts the screen 1/6 past the box");
    check(iScreenUIMarginYF() == 0.0f, "and nothing past it vertically");

    // A widget authored at the left goes left by the whole margin, so the
    // position the box called 0 is the screen's own edge.
    iScreenSetAnchorRect(0.0f, 0.0f, 0.0f, 0.0f);
    check(fabsf(iScreenAnchorX(0.0f) - -margin) < 0.0001f, "the left edge reaches the screen");

    // And one authored at the right goes right by it.
    iScreenSetAnchorRect(1.0f, 0.0f, 0.0f, 0.0f);
    check(fabsf(iScreenAnchorX(1.0f) - (1.0f + margin)) < 0.0001f, "and so does the right");

    // What was authored in the middle stays in the middle rather than being
    // pushed to one side of it.
    iScreenSetAnchorRect(0.44f, 0.115f, 0.07f, 0.07f);
    check(iScreenAnchorX(0.44f) == 0.44f, "the middle is left alone");
    check(iScreenAnchorY(0.115f) == 0.115f, "the vertical axis is untouched on a wide screen");

    // The property the whole thing exists for, on the real HUD: a counter and
    // its icon are authored a fixed distance apart, and they have to still be
    // that distance apart afterwards. Retail's four counters, each measured as
    // the icon's rect and then the number's.
    struct
    {
        F32 ix, iy, iw, ih;
        F32 nx, ny, nw, nh;
    } counters[] = {
        { 0.44f, 0.115f, 0.07f, 0.07f, 0.51f, 0.100f, 0.05f, 0.07f }, // spatulas
        { 0.63f, 0.125f, 0.06f, 0.06f, 0.71f, 0.100f, 0.05f, 0.07f }, // shiny objects
        { 0.08f, 0.830f, 0.09f, 0.09f, 0.18f, 0.793f, 0.05f, 0.07f }, // the pickup counter
        { 0.73f, 0.815f, 0.09f, 0.09f, 0.83f, 0.793f, 0.05f, 0.07f }, // socks
    };

    for (U32 i = 0; i < sizeof(counters) / sizeof(counters[0]); i++)
    {
        iScreenSetAnchorRect(counters[i].ix, counters[i].iy, counters[i].iw, counters[i].ih);
        F32 icon = iScreenAnchorX(counters[i].ix);

        iScreenSetAnchorRect(counters[i].nx, counters[i].ny, counters[i].nw, counters[i].nh);
        F32 number = iScreenAnchorX(counters[i].nx);

        check(fabsf((number - icon) - (counters[i].nx - counters[i].ix)) < 0.0001f,
              "the number stays the distance from its icon that it was drawn at");
    }

    // The reach: what the culling and the clipping are allowed to let through.
    // It is the margin while the anchor is using it...
    check(fabsf(iScreenAnchorMarginXF() - margin) < 0.0001f, "native reaches the margin");

    // Pillarbox is the identity whatever the shape, which is what makes it the
    // way back to exactly what the console drew.
    iScreenSetUIMode(iSCREENUI_PILLARBOX);
    iScreenSetAnchorRect(0.08f, 0.83f, 0.09f, 0.09f);
    check(iScreenAnchorX(0.0f) == 0.0f && iScreenAnchorX(1.0f) == 1.0f,
          "pillarbox leaves every position alone");

    // ...and zero when it is not, so that a widget sliding in from off the box
    // is culled at the box edge exactly as the console culled it, rather than
    // being revealed sliding through the pillar bars.
    check(iScreenAnchorMarginXF() == 0.0f && iScreenAnchorMarginYF() == 0.0f,
          "and pillarbox reaches nothing, so the box culls as the console did");
    check(iScreenUIMarginXF() > 0.0f, "though the box is still inset from the screen");
    iScreenSetUIMode(iSCREENUI_NATIVE);
    iScreenSetAnchorRect(0.5f, 0.5f, 0.0f, 0.0f);

    // --- full-bleed menu art ----------------------------------------------
    //
    // Identity at 4:3, for the same reason the anchor is: it is what everyone
    // playing at the default gets.
    iScreenSetSize(640, 480);
    iScreenSetUIMode(iSCREENUI_NATIVE);
    check(iScreenStretchX(0.0f) == 0.0f && iScreenStretchX(1.0f) == 640.0f,
          "at 4:3 the stretch is the box");
    check(iScreenStretchY(0.0f) == 0.0f && iScreenStretchY(1.0f) == 480.0f, "on both axes");

    // 16:9: the art reaches all four edges, which is the whole point of it.
    iScreenSetSize(1920, 1080);
    check(iScreenStretchX(0.0f) == 0.0f && iScreenStretchX(1.0f) == 1920.0f,
          "16:9 fills the width");
    check(iScreenStretchY(0.0f) == 0.0f && iScreenStretchY(1.0f) == 1080.0f,
          "and the height, with nothing cropped");

    // It is a stretch and not a uniform scale, and that is the trade being
    // made: the caustics get a third wider rather than losing their corners.
    F32 artW = iScreenStretchX(1.0f) - iScreenStretchX(0.0f);
    F32 artH = iScreenStretchY(1.0f) - iScreenStretchY(0.0f);
    check(fabsf(artW / artH - 16.0f / 9.0f) < 0.0001f, "the art takes the screen's shape");

    // Nothing is left over on either axis at any shape, including narrower
    // than 4:3, where the box is short of the screen the other way about.
    iScreenSetSize(1280, 1024);
    check(iScreenStretchX(1.0f) == 1280.0f && iScreenStretchY(1.0f) == 1024.0f,
          "5:4 fills it too");

    // Pillarbox does not fill anything -- the art goes back in the box, black
    // bars and all, because that is the mode that draws what the console drew.
    iScreenSetSize(1920, 1080);
    iScreenSetUIMode(iSCREENUI_PILLARBOX);
    check(iScreenStretchX(0.0f) == iScreenUIOriginXF() &&
              fabsf(iScreenStretchX(1.0f) - (iScreenUIOriginXF() + iScreenUIWidthF())) < 0.01f,
          "pillarbox leaves full-bleed art in the box");
    iScreenSetUIMode(iSCREENUI_NATIVE);

    // Nothing else in this process renders, but the value is global, so it goes
    // back to the default rather than being left where a later test would find
    // it surprising.
    iScreenSetSize(640, 480);
    check(iScreenWidth() == 640 && iScreenHeight() == 480, "and it can be set back");
    check(iScreenUIWidthF() == 640.0f && iScreenUIHeightF() == 480.0f,
          "with the UI box back at 640x480");
}

static void test_drawdist()
{
    printf("iDrawDist\n");

    // Off until something says otherwise, and off means retail exactly: 400 is
    // the far clip iCamera.cpp has always initialised itself to.
    check(iDrawDistUnlimited() == FALSE, "the switch is off by default");
    check(iDrawDistFarClip() == 400.0f, "and the far clip is the console's 400");

    iDrawDistSetUnlimited(TRUE);
    check(iDrawDistUnlimited() == TRUE, "the switch is taken");
    check(iDrawDistFarClip() > 400.0f, "and the far clip moves out with it");

    // The value xDrawDistCull hands the renderer when the switch is on. Every
    // distance in these systems is already squared, so 1e38f is compared
    // against a squared camera distance directly and nothing squares it again
    // -- except zLOD, which takes its square root first and squares that back
    // to reach adjustNoRenderDist. That round trip is the one that has to stay
    // a number: an infinity would make the comparison false by accident rather
    // than because the distance is unreachable. Checked here rather than in
    // xDrawDist.h because the macro is a macro and this target does not link
    // the renderer.
    const F32 never = 1.0e38f;
    const F32 adjusted = (10.0f + sqrtf(never)) * (10.0f + sqrtf(never));
    check(adjusted > 0.0f && adjusted < 3.4e38f, "the 'never' distance survives zLOD's round trip");
    check(adjusted > 1.0e30f, "and comes back far enough out that nothing reaches it");

    // Global, and nothing else in this process draws, so it goes back rather
    // than being left where a later test would find it surprising.
    iDrawDistSetUnlimited(FALSE);
    check(iDrawDistFarClip() == 400.0f, "and it can be set back");
}

// A TEXT asset, as the packer hands one to zAssetTypes.cpp: the string, in a
// buffer the size of the asset's own bytes less its xTextAsset header. The
// slack matters -- retail rounds every asset up, and that padding is what a
// replacement is allowed to spill into.
struct patched
{
    char buf[512];
    U32 capacity;
};

static void patch_setup(patched* p, const char* text, U32 capacity)
{
    memset(p->buf, 0x7f, sizeof(p->buf));
    strcpy(p->buf, text);
    p->capacity = capacity;
}

static bool patch_run(patched* p, const char* name)
{
    return iTextPatchAsset(iTextPatchAssetID(name), p->buf, p->capacity) != FALSE;
}

static void test_textpatch()
{
    printf("iTextPatch\n");

    // The hash is xStrHash copied into the platform layer, which sits below the
    // game code in the link. These four ids were read out of the retail Xbox
    // archives with the asset names beside them, so they pin the copy to the
    // original: if the two ever disagree, every override silently stops
    // matching and nothing else would say so.
    check(iTextPatchAssetID("PS2_MEMCARD") == 0x7a338125u, "the id hash agrees with the archives");
    check(iTextPatchAssetID("LD MC1 TXT") == 0x06dece17u, "for a name with spaces");
    check(iTextPatchAssetID("text_menu_reboot") == 0xc54086e3u, "and a lower-case one");
    check(iTextPatchAssetID("ps2_memcard") == iTextPatchAssetID("PS2_MEMCARD"),
          "and it folds case, as the asset system does");

    // Retail truncates a name past 31 characters in the archive's debug chunk
    // but hashes it in full, so the table's long names have to be the full
    // ones. This is the id of an asset whose listing shows only
    // "MNU4 AUTO SAVE FAILED NOSPACE T".
    check(iTextPatchAssetID("MNU4 AUTO SAVE FAILED NOSPACE TXT") == 0x3edb7748u,
          "a name longer than a listing shows still hashes right");

    check(iTextPatchRulesFit() != FALSE, "no substitution rule is longer than what it replaces");

    patched p;

    // Off is retail, exactly. Checked before anything is turned on, because the
    // default this process starts with is what the game gets if ApplyConfig
    // never runs.
    iTextPatchSetEnabled(FALSE);
    patch_setup(&p, "Please don't turn off your Xbox console.", 64);
    check(!patch_run(&p, "MNU4 AUTO SAVE TXT"), "off, nothing is rewritten");
    check(strcmp(p.buf, "Please don't turn off your Xbox console.") == 0, "and the text is retail's");

    iTextPatchSetEnabled(TRUE);
    check(iTextPatchEnabled() != FALSE, "the switch reads back");

    // The word swap, on the string a player actually meets.
    patch_setup(&p, "Please don't turn off your Xbox console.", 64);
    check(patch_run(&p, "MNU4 AUTO SAVE TXT"), "on, the console's name goes");
    check(strcmp(p.buf, "Please don't turn off your computer.") == 0, "'your Xbox console' becomes 'your computer'");

    // The same rule at the start of a sentence. Matching folds case, so this
    // hits the rule spelled in lower case, and the capital has to survive.
    patch_setup(&p, "Your Xbox console doesn't have enough free blocks.", 64);
    patch_run(&p, "SV NOSPACE TXT");
    check(strcmp(p.buf, "Your computer doesn't have enough free blocks.") == 0,
          "a capital at the start of a sentence is kept");

    // ...but a shouted name is not a sentence, and must not hand its capital on.
    patch_setup(&p, "Please insert a DUALSHOCK.", 64);
    patch_run(&p, "some other asset");
    check(strcmp(p.buf, "Please insert a gamepad.") == 0, "and an all-caps name does not");

    // The bare name, and the PS2 text the Xbox release never finished
    // stripping.
    patch_setup(&p, "Your Xbox doesn't have enough free blocks.", 64);
    patch_run(&p, "SV BADSAVE TXT");
    check(strcmp(p.buf, "Your PC doesn't have enough free blocks.") == 0, "'Xbox' alone becomes 'PC'");

    patch_setup(&p, "Memory card (8MB) (for PlayStation\xae\x32) is not inserted.", 96);
    patch_run(&p, "some other asset");
    check(strstr(p.buf, "PlayStation") == NULL, "and PlayStation goes with it");

    // The whole point of stepping over markup: what is inside the braces is a
    // lookup key. An asset called "xbox_button" that became "PC_button" would
    // resolve to nothing, and the failure would show up as a missing glyph
    // three menus away from this file.
    patch_setup(&p, "{tex:xbox_button}Press it{i:PS2_MEMCARD}", 96);
    patch_run(&p, "some other asset");
    check(strcmp(p.buf, "{tex:xbox_button}Press it{i:PS2_MEMCARD}") == 0,
          "a name inside {markup} is left alone");

    // ...while text on both sides of a markup span still gets rewritten.
    patch_setup(&p, "Xbox{n}Xbox", 32);
    patch_run(&p, "some other asset");
    check(strcmp(p.buf, "PC{n}PC") == 0, "text either side of it does not");

    // An override replaces the whole string, by name.
    patch_setup(&p, "{i:button_picture_03} Reboot to Xbox Dashboard", 48);
    check(patch_run(&p, "text_menu_reboot"), "an override matches by name");
    check(strcmp(p.buf, "{i:button_picture_03} Quit to Desktop") == 0, "and replaces the whole string");

    // The save location under "Load saved game". Retail ships two different
    // strings under this one name -- "MEMORY CARD slot 1" in the shared menu
    // archive, "Xbox Hard Disk" in the level archives -- and the shorter of the
    // two is what the replacement has to fit.
    patch_setup(&p, "Xbox Hard Disk", 16);
    check(patch_run(&p, "LD MC1 TXT"), "the save location is replaced");
    check(strcmp(p.buf, "save folder") == 0, "in the smaller of the two assets retail ships");

    // Capacity is the asset's, and an override that does not fit is refused
    // rather than truncated. The word swap still runs, so the string is left
    // better than retail rather than untouched.
    patch_setup(&p, "Reboot to Xbox Dashboard", 25);
    patch_run(&p, "text_menu_reboot");
    check(strcmp(p.buf, "Reboot to Desktop") == 0, "an override too big for its asset is refused");

    // Nothing is written past the capacity, whatever happens above.
    patch_setup(&p, "Your Xbox console", 18);
    patch_run(&p, "some other asset");
    check(p.buf[18] == 0x7f, "and nothing is written past the asset's own bytes");

    // A string with no terminator inside the asset is not a string. Walking it
    // would read into whatever the packer put next.
    memset(p.buf, 'X', sizeof(p.buf));
    check(!iTextPatchAsset(iTextPatchAssetID("LD MC1 TXT"), p.buf, 16),
          "an unterminated asset is left alone");
    check(p.buf[0] == 'X', "and not written to");

    // Global, and later tests do not expect it on.
    iTextPatchSetEnabled(FALSE);
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

// The stick shaping in iPad.cpp, which sits above the backend and so is worth
// checking whichever one is linked. Its two link-time dependencies are stubbed
// here; see the note beside iPad.cpp in CMakeLists.txt.
#include "xTRC.h"
#include "iPad.h"

_tagTRCPadInfo gTrcPad[4];
_tagTRCState gTrcDisk[2];
void xTRCPad(S32, _tagTRCState)
{
}

// AnalogMin and AnalogMax as zMain.cpp defaults them. The game reads the stick
// through these, so they are what the shaping has to land inside.
#define TEST_ANALOG_MIN 0x20
#define TEST_ANALOG_MAX 0x6e

// DampenControls in zEntPlayer.cpp, restated. This is the consumer the shape
// exists for: it measures the stick with max(|x|,|y|), not with its length, so
// a stick that reports a circle reads short everywhere off-axis.
static F32 test_dampen_mag(S32 padX, S32 padY)
{
    F32 x = (F32)padX;
    F32 y = (F32)padY;
    F32 mag;

    if (x > -(F32)TEST_ANALOG_MIN && x < (F32)TEST_ANALOG_MIN)
    {
        x = 0.0f;
    }
    if (y > -(F32)TEST_ANALOG_MIN && y < (F32)TEST_ANALOG_MIN)
    {
        y = 0.0f;
    }
    if (x == 0.0f && y == 0.0f)
    {
        return 0.0f;
    }

    mag = (fabsf(x) > fabsf(y)) ? fabsf(x) : fabsf(y);
    mag = (mag - TEST_ANALOG_MIN) / (F32)(TEST_ANALOG_MAX - TEST_ANALOG_MIN);
    if (mag < 0.0f)
    {
        return 0.0f;
    }
    return (mag > 1.0f) ? 1.0f : mag;
}

// One host stick reading all the way through to what the game measures.
static F32 test_stick_mag(F32 inX, F32 inY)
{
    F32 x, y;
    iPadShapeStick(inX, inY, 72.0f, 40.0f, &x, &y);
    return test_dampen_mag(iPadConvStick(x), iPadConvStick(y));
}

// ---------------------------------------------------------------------------
// The binding grammar, checked against a made-up device so that what is being
// tested is the parser and not one backend's token names.

enum
{
    FAKE_ONE,
    FAKE_TWO,
    FAKE_THREE,
    FAKE_COUNT
};

static const iPadBindToken kFakeTokens[] = {
    { "one", FAKE_ONE },
    { "two", FAKE_TWO },
    { "three", FAKE_THREE },
};

static const S32 kFakeTokenCount = 3;

static U32 sFakeHeld;

static bool fake_held(S16 id)
{
    return (sFakeHeld & (1u << id)) != 0;
}

static bool bind_says(const char* text, U32 held)
{
    iPadBind b;
    iPadBindParse(text, kFakeTokens, kFakeTokenCount, "test", &b);
    sFakeHeld = held;
    return iPadBindHeld(b, fake_held);
}

#define FAKE(x) (1u << FAKE_##x)

static void test_pad_bindings()
{
    iPadBind b;

    check(iPadBindParse("one", kFakeTokens, kFakeTokenCount, "test", &b), "one input parses");
    check(b.alts == 1 && b.length[0] == 1, "as one alternative of one input");

    check(bind_says("one", FAKE(ONE)), "and is on when that input is held");
    check(!bind_says("one", FAKE(TWO)), "and off when a different one is");
    check(!bind_says("one", 0), "and off when nothing is");

    // ','
    check(bind_says("one, two", FAKE(ONE)), "either alternative on its own is enough");
    check(bind_says("one, two", FAKE(TWO)), "including the second");
    check(bind_says("one, two", FAKE(ONE) | FAKE(TWO)), "and both at once is still on");
    check(!bind_says("one, two", FAKE(THREE)), "and neither is off");

    // '+'
    check(bind_says("one+two", FAKE(ONE) | FAKE(TWO)), "a chord needs both");
    check(!bind_says("one+two", FAKE(ONE)), "half a chord is not enough");
    check(!bind_says("one+two", FAKE(TWO)), "in either direction");

    // '!' -- the reason the grammar has one. gc/iPad.cpp reads Z+L as L2 and
    // NOT also as L1, and the port's default bindings have to be able to say
    // so; without this, holding the modifier would press both.
    check(bind_says("one+!two", FAKE(ONE)), "a negated term is satisfied while it is idle");
    check(!bind_says("one+!two", FAKE(ONE) | FAKE(TWO)), "and blocks the binding once held");
    check(!bind_says("one+!two", FAKE(TWO)), "and does not stand in for the positive term");

    // Exactly the l1/l2 pair, which is what the default pad mapping relies on.
    check(bind_says("one+!two", FAKE(ONE)) && !bind_says("one+two", FAKE(ONE)),
          "l1 alone: the trigger without the modifier");
    check(!bind_says("one+!two", FAKE(ONE) | FAKE(TWO)) &&
              bind_says("one+two", FAKE(ONE) | FAKE(TWO)),
          "l2 alone: the trigger with it -- never both at once");

    // Empty is a button turned off, not an error.
    check(iPadBindParse("", kFakeTokens, kFakeTokenCount, "test", &b), "an empty binding parses");
    check(b.alts == 0, "as nothing");
    sFakeHeld = FAKE(ONE) | FAKE(TWO) | FAKE(THREE);
    check(!iPadBindHeld(b, fake_held), "and nothing can press it");

    // Whitespace is not significant anywhere.
    check(bind_says("  one  +  ! two  ,  three  ", FAKE(THREE)), "spaces around the operators");

    // Bad input costs the whole binding, not the readable part of it. A typo
    // that left a button half-bound would be harder to spot than one that left
    // it dead.
    check(!iPadBindParse("one, nonsense", kFakeTokens, kFakeTokenCount, "test", &b),
          "an unknown input is rejected");
    check(b.alts == 0, "and takes the rest of the line with it");

    check(!iPadBindParse("!one", kFakeTokens, kFakeTokenCount, "test", &b),
          "an all-negated alternative is rejected");
    check(b.alts == 0, "because nothing would ever press it");

    check(!iPadBindParse("one+", kFakeTokens, kFakeTokenCount, "test", &b),
          "a trailing '+' is rejected");
    check(!iPadBindParse("one,", kFakeTokens, kFakeTokenCount, "test", &b),
          "and so is a trailing ','");
    check(!iPadBindParse("+one", kFakeTokens, kFakeTokenCount, "test", &b),
          "and a leading one");

    check(!iPadBindParse("one, two, three, one, two", kFakeTokens, kFakeTokenCount, "test", &b),
          "more alternatives than there is room for is rejected");

    // The table every backend and the config writer share.
    check(kPadBindButtonCount <= IPAD_BIND_MAX_BUTTONS,
          "the button table fits the array the backends size from it");
    check(iPadBindFind("a") != NULL && iPadBindFind("START") != NULL,
          "a button is found by name, case-insensitively");
    check(iPadBindFind("nonsense") == NULL, "and an invented one is not");

    for (S32 i = 0; i < kPadBindButtonCount; i++)
    {
        if (kPadBindButtons[i].mask == 0 || kPadBindButtons[i].pad == NULL ||
            kPadBindButtons[i].key == NULL)
        {
            check(false, "every button has a mask and a default for each device");
            return;
        }
    }
    check(true, "every button has a mask and a default for each device");
}

static void test_pad_stick_shape()
{
    F32 x, y;

    iPadShapeStick(0.0f, 0.0f, 72.0f, 40.0f, &x, &y);
    check(x == 0.0f && y == 0.0f, "a centred stick shapes to zero");

    iPadShapeStick(1.0f, 0.0f, 72.0f, 40.0f, &x, &y);
    check(fabsf(x - 72.0f) < 0.001f && fabsf(y) < 0.001f,
          "full deflection along an axis reaches the octagon's 72");
    check(iPadConvStick(x) == 127, "which iPadConvStick saturates at 127");

    // The octagon's diagonal vertex. 40 is exactly where iPadConvStick clamps,
    // so both axes saturate here as well -- that equality is the whole point of
    // the shape, and it is what a circular stick does not have.
    F32 h = 0.70710678f;
    iPadShapeStick(h, h, 72.0f, 40.0f, &x, &y);
    check(fabsf(x - 40.0f) < 0.01f && fabsf(y - 40.0f) < 0.01f,
          "a full diagonal reaches the octagon's 40 on both axes");
    check(iPadConvStick(x) == 127 && iPadConvStick(y) == 127, "so a diagonal saturates too");

    // The regression this file exists for. Sweeping a stick held at full
    // deflection right round must give the game full speed at every angle;
    // scaling the host's circle instead dips to 0.71 of full near 45 degrees,
    // which is the character dropping to a walk four times per revolution.
    F32 worst = 1.0f;
    for (S32 i = 0; i < 720; i++)
    {
        F32 a = (F32)i * (6.28318531f / 720.0f);
        F32 mag = test_stick_mag(cosf(a), sinf(a));
        if (mag < worst)
        {
            worst = mag;
        }
    }
    check(worst > 0.999f, "full deflection reads as full speed at every angle");

    // And the stick still has a usable middle: a half push is not full, and is
    // not zero either.
    F32 half = test_stick_mag(0.5f * h, 0.5f * h);
    check(half > 0.0f && half < 1.0f, "a half diagonal is neither idle nor full");
    check(test_stick_mag(0.0f, 0.0f) == 0.0f, "a centred stick reads as no speed");

    // The C-stick's octagon is the smaller one, and its diagonal stops at 31
    // rather than 40 -- so the camera really does turn slower on a diagonal.
    // That is the console's, not an artefact of this mapping, and pinning it
    // keeps the two sticks from being collapsed into one set of constants.
    iPadShapeStick(h, h, 59.0f, 31.0f, &x, &y);
    check(fabsf(x - 31.0f) < 0.01f && iPadConvStick(x) < 127,
          "the C-stick's diagonal stops short of saturating, as on the console");
    iPadShapeStick(1.0f, 0.0f, 59.0f, 31.0f, &x, &y);
    check(iPadConvStick(x) == 127, "but its axis push still saturates");

    // Sign is carried through untouched; the octagon is symmetric.
    iPadShapeStick(-h, -h, 72.0f, 40.0f, &x, &y);
    check(fabsf(x + 40.0f) < 0.01f && fabsf(y + 40.0f) < 0.01f,
          "the octagon is symmetric about both axes");

    // The keyboard reports 1 on each axis at once, which is off the circle
    // entirely. The clamp has to bring it back to the boundary rather than let
    // it through at sqrt(2).
    iPadShapeStick(1.0f, 1.0f, 72.0f, 40.0f, &x, &y);
    check(fabsf(x - 40.0f) < 0.01f && fabsf(y - 40.0f) < 0.01f,
          "a keyboard's square diagonal lands on the boundary, not past it");
}

// The stick deadzone, which every backend shares -- both of them read a
// device that reports two signed 16-bit axes, and none of the arithmetic is
// specific to one API. Outside the backend guard below for that reason.
static void test_pad_stick_deadzone()
{
    const S32 dz = IPAD_STICK_DEADZONE_LEFT;
    F32 x, y;

    iPadStickConvert(0, 0, dz, &x, &y);
    check(x == 0.0f && y == 0.0f, "a centred stick reads zero");

    iPadStickConvert((S16)(dz - 1), 0, dz, &x, &y);
    check(x == 0.0f && y == 0.0f, "inside the deadzone reads zero");

    // The deadzone is radial, so a diagonal push whose components are each
    // inside it but whose magnitude is not must still register. A per-axis
    // deadzone reports zero here, and that is the bug this guards.
    S16 diag = (S16)(dz * 0.8f);
    iPadStickConvert(diag, diag, dz, &x, &y);
    check(x > 0.0f && y > 0.0f, "a diagonal past the radial deadzone registers");

    iPadStickConvert(32767, 0, dz, &x, &y);
    check(fabsf(x - 1.0f) < 0.0001f, "full right deflection reads 1");
    check(fabsf(y) < 0.0001f, "and nothing on the other axis");

    iPadStickConvert(-32768, 0, dz, &x, &y);
    check(fabsf(x + 1.0f) < 0.0001f, "full left deflection reads -1, not past it");

    // Just past the deadzone edge the output must start near zero. Rescaling
    // from that edge is what makes it true; without it the first movement jumps
    // straight to the deadzone's fraction of full scale.
    iPadStickConvert((S16)(dz + 40), 0, dz, &x, &y);
    check(x > 0.0f && x < 0.01f, "just past the deadzone the stick barely moves");

    // A full diagonal is clamped to the unit circle, so its magnitude cannot
    // exceed what one axis alone reports.
    iPadStickConvert(32767, 32767, dz, &x, &y);
    check(sqrtf(x * x + y * y) <= 1.0001f, "a full diagonal does not exceed full scale");

    iPadStickConvert(0, 32767, dz, &x, &y);
    check(y > 0.99f, "up is positive, as iPadHost.h specifies");
}

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

#ifdef BFBB_INPUT_BACKEND_WIN32
// The button conversion inside iPadHostWin32.cpp, which is named rather than
// static so this file can reach it. Declared here rather than pulled in from a
// header, so that a change to its signature is caught at link time instead of
// the test silently retargeting itself at something else.
#include <windows.h>
#include <xinput.h>
U32 iPadHostWin32ConvertButtons(const XINPUT_GAMEPAD& gp);

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

    // Shoulders: the triggers stand in for the GameCube's analog L and R, and
    // click at the same threshold those do.
    gp.wButtons = 0;
    gp.bLeftTrigger = XINPUT_GAMEPAD_TRIGGER_THRESHOLD - 1;
    gp.bRightTrigger = XINPUT_GAMEPAD_TRIGGER_THRESHOLD - 1;
    check(iPadHostWin32ConvertButtons(gp) == 0, "a trigger short of the threshold does nothing");

    gp.bLeftTrigger = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    gp.bRightTrigger = 255;
    check(iPadHostWin32ConvertButtons(gp) == (TEST_PAD_L1 | TEST_PAD_R1),
          "the triggers are L1 and R1 once past it");

    // RB is the GameCube's Z. zHud.cpp shows the HUD on that bit and zCamera.cpp
    // toggles the near camera with it, so pressing it alone has to produce it
    // alone -- anything else riding along would fire an action with the HUD.
    gp.wButtons = XINPUT_GAMEPAD_RIGHT_SHOULDER;
    gp.bLeftTrigger = 0;
    gp.bRightTrigger = 0;
    check(iPadHostWin32ConvertButtons(gp) == TEST_PAD_Z, "RB alone is the GameCube's Z");

    // Held, it promotes the triggers the way Z does on the GameCube, and goes on
    // reporting itself while it does.
    gp.bLeftTrigger = 255;
    gp.bRightTrigger = 255;
    U32 modified = iPadHostWin32ConvertButtons(gp);
    check(modified == (TEST_PAD_Z | TEST_PAD_L2 | TEST_PAD_R2),
          "Z held turns the triggers into L2 and R2");

    // The promotion has to replace L1 and R1, not add to them: on the GameCube
    // one button reads as one or the other, and menu code in zUI.cpp tests for
    // each separately, so reporting both would give it a button nobody pressed.
    check((modified & (TEST_PAD_L1 | TEST_PAD_R1)) == 0, "and stops reporting L1 and R1");

    // LB is unbound by default. The GameCube has no fourth shoulder.
    gp.wButtons = XINPUT_GAMEPAD_LEFT_SHOULDER;
    gp.bLeftTrigger = 0;
    gp.bRightTrigger = 0;
    check(iPadHostWin32ConvertButtons(gp) == 0, "LB maps to nothing");

    // Everything above is the DEFAULT mapping. The harness config.ini rebinds
    // one button -- `select = ls` -- and this is the whole path from that line
    // to the bits the game reads: parsed at iPadHostInit, evaluated here.
    // Without it the binding layer could be inert and every check above would
    // still pass on the defaults compiled into iPadBind.cpp.
    gp.wButtons = XINPUT_GAMEPAD_LEFT_THUMB;
    check(iPadHostWin32ConvertButtons(gp) == TEST_PAD_SELECT,
          "a rebound button answers to what config.ini gave it");

    gp.wButtons = XINPUT_GAMEPAD_BACK;
    check(iPadHostWin32ConvertButtons(gp) == 0, "and stops answering to what it had");
}
#endif

#ifdef BFBB_INPUT_BACKEND_SDL
#include <SDL3/SDL.h>

// A controller that is not there.
//
// SDL's virtual joystick driver presents one to the rest of SDL exactly as a
// real device: same enumeration, same generated gamepad mapping, same arriving
// and leaving events. That makes it the only way to test the half of
// iPadHostSDL.cpp a fake input struct cannot reach -- which SDL button becomes
// which of the game's, that a device arriving mid-session is picked up, and
// that one leaving is dropped.
//
// What it does NOT cover is a real driver: DirectInput, HIDAPI and raw input
// are the reason this backend exists and none of them is exercised here. This
// checks the code above them.

// The virtual driver hands out button and axis indices by walking the
// SDL_GamepadButton and SDL_GamepadAxis enums and packing the bits that are
// set in the masks below. With every bit from SOUTH to DPAD_RIGHT set, and
// every axis, index N is enum value N -- which is what lets the checks below
// pass an SDL_GamepadButton straight through as a joystick button number.
#define TEST_SDL_BUTTON_MASK ((1u << (SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1)) - 1u)
#define TEST_SDL_AXIS_MASK ((1u << (SDL_GAMEPAD_AXIS_RIGHT_TRIGGER + 1)) - 1u)

// Which port the virtual pad ended up on, and the handle that drives it.
static S32 sVirtualPort = -1;
static SDL_Joystick* sVirtualJoystick;

static U32 virtual_buttons()
{
    const iPadHostState* s = (sVirtualPort >= 0) ? iPadHostGet(sVirtualPort) : NULL;
    return (s != NULL) ? s->buttons : 0;
}

// Everything up, every stick centred, both triggers at rest. Queued only --
// the caller polls.
static void virtual_neutral()
{
    for (S32 i = 0; i <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT; i++)
    {
        SDL_SetJoystickVirtualButton(sVirtualJoystick, i, false);
    }
    for (S32 i = 0; i <= SDL_GAMEPAD_AXIS_RIGHTY; i++)
    {
        SDL_SetJoystickVirtualAxis(sVirtualJoystick, i, 0);
    }

    // A trigger rests at the BOTTOM of its axis rather than the centre, which
    // is how the gamepad layer gets 0..32767 out of a signed axis.
    SDL_SetJoystickVirtualAxis(sVirtualJoystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_MIN_SINT16);
    SDL_SetJoystickVirtualAxis(sVirtualJoystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_MIN_SINT16);
}

static void virtual_release_all()
{
    virtual_neutral();
    iPadHostPoll();
}

// Hold one button and nothing else, let the backend see it, and report what the
// game would read. Everything else is put back to neutral first: a check that
// inherited a held trigger from the check before it would be reading two
// buttons and blaming one.
//
// The virtual driver applies queued state at the next joystick update, which is
// what iPadHostPoll's own SDL_PumpEvents triggers -- so one poll is enough, and
// a second would only paper over it not being.
static U32 virtual_press(SDL_GamepadButton button)
{
    virtual_neutral();
    SDL_SetJoystickVirtualButton(sVirtualJoystick, (S32)button, true);
    iPadHostPoll();
    return virtual_buttons();
}

// Where the trigger has to click, worked out from the CONSOLE and from the
// clamp it goes through: PADClamp's ClampTrigger takes 30 off and saturates at
// 180, so gc/iPad.cpp's `>= 0x18` is raw 54 of a 180 pull -- 30% of the travel.
//
// Both halves matter. Reading 0x18 as a fraction of 255, or of the clamped 150,
// gives a threshold less than half of that, and a bracket built on either
// number passes against a backend that clicks far too early.
//
// A virtual trigger's raw axis runs the full signed range and the gamepad layer
// rescales it to 0..32767, so gamepad = (raw + 32768) / 2. These two are 27%
// and 33% of travel: close enough to 30% either side that a threshold which is
// merely in the right neighbourhood still fails, which a wider bracket does not
// do -- 20% and 40% would have let the old 7710 through.
#define TEST_SDL_TRIGGER_OFF ((Sint16)(-15074)) // 27% of the travel
#define TEST_SDL_TRIGGER_ON ((Sint16)(-11142)) // 33%

static void test_pad_sdl_virtual()
{
    if (!SDL_WasInit(SDL_INIT_GAMEPAD))
    {
        printf("    (SDL's gamepad subsystem did not start; the virtual pad is skipped)\n");
        return;
    }

    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER + 1;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;
    desc.button_mask = TEST_SDL_BUTTON_MASK;
    desc.axis_mask = TEST_SDL_AXIS_MASK;
    desc.name = "bfbb self-test pad";

    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    check(id != 0, "a virtual gamepad attaches");
    if (id == 0)
    {
        printf("    (%s)\n", SDL_GetError());
        return;
    }

    check(SDL_IsGamepad(id), "and SDL gives it a layout, so it is a gamepad and not just a stick");

    sVirtualJoystick = SDL_OpenJoystick(id);
    check(sVirtualJoystick != NULL, "and a handle to drive it with");
    if (sVirtualJoystick == NULL)
    {
        SDL_DetachVirtualJoystick(id);
        return;
    }

    // The arriving device, through the same event the game gets. Which port it
    // lands on is not fixed -- the harness config pins input.controller, and a
    // controller on the developer's desk takes a slot first -- so the port is
    // found by pressing a button and seeing which one moves rather than
    // assumed.
    iPadHostPoll();

    sVirtualPort = -1;
    for (S32 i = 0; i <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT; i++)
    {
        SDL_SetJoystickVirtualButton(sVirtualJoystick, i, i == SDL_GAMEPAD_BUTTON_SOUTH);
    }
    iPadHostPoll();

    for (S32 p = 0; p < IPAD_MAX_CONTROLLERS; p++)
    {
        const iPadHostState* s = iPadHostGet(p);
        if (s != NULL && s->connected && s->buttons == TEST_PAD_X)
        {
            sVirtualPort = p;
            break;
        }
    }

    check(sVirtualPort >= 0, "a controller that arrives mid-session reaches a port");
    if (sVirtualPort < 0)
    {
        SDL_CloseJoystick(sVirtualJoystick);
        SDL_DetachVirtualJoystick(id);
        sVirtualJoystick = NULL;
        return;
    }

    // SDL names the face buttons by POSITION, and that is the whole reason a
    // config.ini written for an Xbox pad works on a Switch one: south is the
    // button under your thumb on every controller ever made, and only the
    // letter printed on it changes. These four are that claim.
    check(virtual_press(SDL_GAMEPAD_BUTTON_SOUTH) == TEST_PAD_X,
          "the south face button is the GameCube's A");
    check(virtual_press(SDL_GAMEPAD_BUTTON_EAST) == TEST_PAD_TRIANGLE,
          "east is the GameCube's B");
    check(virtual_press(SDL_GAMEPAD_BUTTON_WEST) == TEST_PAD_O, "west is the GameCube's X");
    check(virtual_press(SDL_GAMEPAD_BUTTON_NORTH) == TEST_PAD_SQUARE,
          "north is the GameCube's Y");

    check(virtual_press(SDL_GAMEPAD_BUTTON_START) == TEST_PAD_START, "start is start");
    check(virtual_press(SDL_GAMEPAD_BUTTON_DPAD_UP) == TEST_PAD_UP, "the d-pad comes through");
    check(virtual_press(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) == 0,
          "LB maps to nothing, as on the other backend");

    // RB is the GameCube's Z, and held it promotes the triggers to L2 and R2
    // INSTEAD of L1 and R1 -- the exclusivity iPadBind.h's '!' exists for.
    check(virtual_press(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) == TEST_PAD_Z,
          "RB alone is the GameCube's Z");

    virtual_release_all();
    SDL_SetJoystickVirtualAxis(sVirtualJoystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
                               TEST_SDL_TRIGGER_OFF);
    SDL_SetJoystickVirtualAxis(sVirtualJoystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
                               TEST_SDL_TRIGGER_OFF);
    iPadHostPoll();
    check(virtual_buttons() == 0, "a trigger short of the threshold does nothing");

    SDL_SetJoystickVirtualAxis(sVirtualJoystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
                               TEST_SDL_TRIGGER_ON);
    SDL_SetJoystickVirtualAxis(sVirtualJoystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
                               TEST_SDL_TRIGGER_ON);
    iPadHostPoll();
    check(virtual_buttons() == (TEST_PAD_L1 | TEST_PAD_R1),
          "the triggers are L1 and R1 once past it");

    SDL_SetJoystickVirtualButton(sVirtualJoystick, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    iPadHostPoll();
    U32 modified = virtual_buttons();
    check(modified == (TEST_PAD_Z | TEST_PAD_L2 | TEST_PAD_R2),
          "Z held turns the triggers into L2 and R2");
    check((modified & (TEST_PAD_L1 | TEST_PAD_R1)) == 0, "and stops reporting L1 and R1");

    // The harness config.ini rebinds one button -- `select = ls` -- so this is
    // the whole path from that line to the bits the game reads, on this backend
    // too. Without it the binding layer could be inert on the SDL side and
    // every check above would still pass on the defaults.
    check(virtual_press(SDL_GAMEPAD_BUTTON_LEFT_STICK) == TEST_PAD_SELECT,
          "a rebound button answers to what config.ini gave it");
    check(virtual_press(SDL_GAMEPAD_BUTTON_BACK) == 0, "and stops answering to what it had");

    // The sticks, and the one thing about them this backend has to get right
    // that the XInput one does not: SDL reports Y the way a screen does, down
    // positive, and iPadHost.h asks for it the way the GameCube's stick does.
    virtual_release_all();
    SDL_SetJoystickVirtualAxis(sVirtualJoystick, SDL_GAMEPAD_AXIS_LEFTY, SDL_MIN_SINT16);
    iPadHostPoll();

    const iPadHostState* s = iPadHostGet(sVirtualPort);
    check(s != NULL && s->stick_y > 0.99f, "a stick pushed up reads positive, not negative");
    check(s != NULL && s->stick_x > -0.01f && s->stick_x < 0.01f,
          "and the axis it was not pushed along stays at zero");

    virtual_release_all();
    s = iPadHostGet(sVirtualPort);
    check(s != NULL && s->buttons == 0 && s->stick_y == 0.0f, "released, it reads neutral again");

    // And leaving. The XInput backend has to poll for this; here it is an
    // event, so it is worth checking the event is actually acted on.
    SDL_CloseJoystick(sVirtualJoystick);
    sVirtualJoystick = NULL;
    check(SDL_DetachVirtualJoystick(id), "the virtual gamepad detaches");

    iPadHostPoll();
    s = iPadHostGet(sVirtualPort);
    check(s != NULL && !s->connected, "and the port it was on reports no controller");

    sVirtualPort = -1;
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

#ifdef BFBB_INPUT_HAVE_KEYBOARD
    // The keyboard is shared by both Windows backends, so this holds under
    // either. This process has no window, so GetActiveWindow reports none and
    // the keyboard path takes its unfocused branch: present, holding nothing.
    //
    // The harness config.ini pins input.controller to 3, so port 0 reads slot
    // 2 and a pad on the developer's desk lands in slot 0 without disturbing
    // this. It takes THREE controllers to push one into slot 2 and fail the
    // check -- which is the trade: asserting nothing here would leave the one
    // path every keyboard-only player takes untested.
    check(s != NULL && s->connected,
          "the keyboard stands in for port 0 with no controller plugged in");
    check(s != NULL && s->buttons == 0, "and an unfocused keyboard holds nothing");
#else
    check(s != NULL && !s->connected, "the null backend reports no controller");
#endif

#ifdef BFBB_INPUT_BACKEND_WIN32
    test_pad_win32_buttons();
#endif

#ifdef BFBB_INPUT_BACKEND_SDL
    test_pad_sdl_virtual();
#endif

    test_pad_bindings();
    test_pad_stick_deadzone();
    test_pad_stick_shape();

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
    // Two, and it has to be two: the save and load screens are UI assets with
    // two buttons baked into them, and reporting one target did not hide the
    // second -- it made choosing it an error. Both are real folders now.
    check(iSGTgtCount(isg, &max) == 2, "both save targets are present");
    check(max == ISG_NUM_SLOTS, "the maximum is the console's slot count");

    check(iSGTgtState(isg, 0, NULL) == 0xF, "target 0 is present and formatted");
    check(iSGTgtState(isg, 1, NULL) == 0xF, "and so is target 1");
    check(iSGTgtState(isg, 99, NULL) == 0x1000000, "an out-of-range target reports no card");
    check(iSGCheckMemoryCard(isg, 0) == 1, "iSGCheckMemoryCard sees target 0");
    check(iSGCheckMemoryCard(isg, 1) == 1, "and target 1");
    check(iSGCheckMemoryCard(isg, 2) == 0, "and nothing past the last one");
    check(iSGTgtPhysSlotIdx(isg, 0) == 0, "target 0 maps to physical slot 0");
    check(iSGTgtPhysSlotIdx(isg, 1) == 1, "and target 1 to slot 1");
    check(iSGCheckForWrongDevice() == -1, "no wrong-region device on a host");

    check(iSGTgtSetActive(isg, 0) == 1, "iSGTgtSetActive selects target 0");
    check(isg->slot == 0, "the session records the active slot");

    // Target 0 is the save directory ITSELF, not a subdirectory of it. That is
    // what keeps saves written while this build exposed a single target where
    // the game can still find them; only the second needed somewhere new.
    check(strcmp(isg->mcdata[0].root, dir) == 0, "target 0 is the save directory itself");

    check(iSGTgtSetActive(isg, 1) == 1, "iSGTgtSetActive selects target 1");
    char second[600];
    snprintf(second, sizeof(second), "%s/second", dir);
    check(strcmp(isg->mcdata[1].root, second) == 0, "target 1 is a subdirectory of it");
    check(iHostPathExists(second), "which iSGStartup created");

    // The two must not share a file namespace, or the second folder's three
    // game slots would be the first folder's three under another name.
    check(strcmp(isg->mcdata[0].root, isg->mcdata[1].root) != 0, "and the two are different places");

    check(iSGTgtSetActive(isg, 0) == 1, "back to target 0 for the rest of this");

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

    // The unit the save UI displays in. The figures reaching it are bytes, and
    // it was written to word them as a memory card's 8 KB blocks.
    char sz[16];

    iSGFormatSize(0, sz, sizeof(sz));
    check(strcmp(sz, "0 KB") == 0, "nothing is 0 KB, not an empty string");

    // Rounded UP. A save needing one byte past a kilobyte needs the next one,
    // and a figure that reported less would be the wrong way to be wrong.
    iSGFormatSize(1, sz, sizeof(sz));
    check(strcmp(sz, "1 KB") == 0, "one byte rounds up to a whole KB");
    iSGFormatSize(1024, sz, sizeof(sz));
    check(strcmp(sz, "1 KB") == 0, "and exactly one KB stays one");
    iSGFormatSize(1025, sz, sizeof(sz));
    check(strcmp(sz, "2 KB") == 0, "and one past it is two");

    // A real save file, which is what the slot list shows.
    iSGFormatSize(356000, sz, sizeof(sz));
    check(strcmp(sz, "348 KB") == 0, "a save file's size reads in KB");

    // The tenth is truncated where the whole number is rounded up, so a
    // figure never reads as more room than there is.
    iSGFormatSize(1024 * 1024, sz, sizeof(sz));
    check(strcmp(sz, "1.0 MB") == 0, "a megabyte crosses into MB");
    iSGFormatSize(1024 * 1024 * 3 / 2, sz, sizeof(sz));
    check(strcmp(sz, "1.5 MB") == 0, "and carries one decimal");

    // The largest figure the layer can produce: iSG_free_bytes clamps a disk's
    // free space to what fits in the S32 the game compares it against.
    iSGFormatSize(0x7FFFFFFF, sz, sizeof(sz));
    check(strcmp(sz, "2.0 GB") == 0, "the free-space clamp reads as 2.0 GB");

    // A negative can only come of an overflow upstream; it is not free space.
    iSGFormatSize(-1, sz, sizeof(sz));
    check(strcmp(sz, "0 KB") == 0, "a negative is nothing, not a negative size");

    // Free space is the one figure that does NOT come off the game's clamped
    // S32, so it must be able to say something larger than that clamp. Any disk
    // this test runs on has more than 2 GB free, which is what makes "not 2.0
    // GB" the assertion worth making here.
    char freesz[16];
    iSGFormatFreeSpace(freesz, sizeof(freesz));
    check(freesz[0] != '\0', "iSGFormatFreeSpace reports something");
    check(strstr(freesz, "GB") != NULL || strstr(freesz, "TB") != NULL,
          "and it is the whole disk, past the 2 GB the game's own figure stops at");
    printf("    (free where saves go: %s)\n", freesz);

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
    const U8* p = (const U8*)iSndDataAcquire(sFakePkgAsset, &pcm, &bytes, NULL);

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

    check(iSndDataAcquire(0x99999999, &pcm, &bytes, NULL) == NULL, "an unknown asset reads nothing");
    check(bytes == 0, "and reports no size");

    // Cached: the same pointer, without going back to the file.
    U32 again = 0;
    const void* second = iSndDataAcquire(sFakePkgAsset, &pcm, &again, NULL);
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
    // count, which is the 64 per block the assets' own avgBytesPerSec implies
    // and not the 65 an IMA-in-WAV decoder would give; and the decode itself,
    // checked here against values worked out by hand from the IMA step table.
    const U32 kBlock = 36;

    snprintf(path, sizeof(path), "%s/adpcm.HOP", dir);
    f = fopen(path, "wb");
    check(f != NULL, "the ADPCM package could be created");
    if (f == NULL)
    {
        return;
    }

    // One block: predictor 1000, step index 0 (step 7), then 32 payload bytes.
    // The header is state, not output, so the first sample comes from the first
    // nibble. The first two nibbles are 0 and 8 -- +step/8 and -step/8, both 0
    // at step 7 -- so the first two samples are 1000, 1000. Every later byte is
    // 0x00, which is nibble 0 twice: a rise of step>>3 each, with the index
    // walking down and staying at 0.
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
    const S16* a = (const S16*)iSndDataAcquire(sFakePkgAsset, &adpcm, &abytes, NULL);

    check(a != NULL, "an ADPCM asset decodes");
    check(abytes == 64 * sizeof(S16),
          "a 36-byte block is 64 samples; the header is state, not a sample");

    if (a != NULL)
    {
        check(a[0] == 1000, "nibble 0 raises the predictor by step/8, 0 at step 7");
        check(a[1] == 1000, "and nibble 8 lowers it by the same");

        // Index walks 0 -> 0 (kImaIndex[0] is -1, clamped at 0), so step stays
        // 7 and every remaining nibble adds 7>>3 == 0. A decoder that got the
        // table or the clamp wrong drifts away from this immediately.
        bool flat = true;
        for (U32 i = 2; i < 64; i++)
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
// The soundtrack override
//
// Built here WITHOUT BFBB_HAVE_FFMPEG, so what runs is the WAVE reader and the
// folder scan -- the two parts that have to work in every configuration. What
// FFmpeg decodes is FFmpeg's business; what this has to get right is which
// file answers for which asset, and that a stereo file arrives as stereo.

// A minimal RIFF/WAVE: 16-bit PCM, `ch` channels at `hz`, `frames` frames of a
// counting pattern that makes a channel swap visible.
static void write_wav(const char* path, U32 ch, U32 hz, U32 frames)
{
    FILE* f = fopen(path, "wb");
    if (f == NULL)
    {
        return;
    }

    U32 dataBytes = frames * ch * 2;
    U32 riffSize = 36 + dataBytes;
    U32 byteRate = hz * ch * 2;

    fwrite("RIFF", 1, 4, f);
    fwrite(&riffSize, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    U32 fmtLen = 16;
    fwrite(&fmtLen, 4, 1, f);
    U16 tag = 1;
    U16 channels16 = (U16)ch;
    U16 align = (U16)(ch * 2);
    U16 bits = 16;
    fwrite(&tag, 2, 1, f);
    fwrite(&channels16, 2, 1, f);
    fwrite(&hz, 4, 1, f);
    fwrite(&byteRate, 4, 1, f);
    fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataBytes, 4, 1, f);

    for (U32 i = 0; i < frames; i++)
    {
        for (U32 c = 0; c < ch; c++)
        {
            // Left counts up from 100, right down from -100, so a decoder that
            // drops or swaps a channel cannot pass.
            S16 v = (S16)(c == 0 ? (100 + (S32)i) : -(100 + (S32)i));
            fwrite(&v, 2, 1, f);
        }
    }

    fclose(f);
}

static void test_soundtrack()
{
    printf("iSoundtrack\n");

    char dir[512];
    if (!scratch_dir("soundtrack", dir, sizeof(dir)))
    {
        check(false, "could not make a temp directory");
        return;
    }

    // No folder is the default, and must answer nothing for everything rather
    // than scanning the working directory.
    iSoundtrackSetFolder("");
    check(iSoundtrackFolder() == NULL, "no folder set reads as no folder");
    check(iSoundtrackFind(0xABCD1234) == NULL, "and overrides nothing");

    // Rule one: the file is named after the asset, so its name IS the key.
    char named[512];
    snprintf(named, sizeof(named), "%s/music_00_hb_44.wav", dir);
    write_wav(named, 2, 44100, 64);

    // Rule two: a file named after the music, which cannot be matched by name.
    char album[512];
    snprintf(album, sizeof(album), "%s/01. Bikini Bottom.wav", dir);
    write_wav(album, 1, 22050, 32);

    iSoundtrackSetFolder(dir);

    U32 aidNamed = iTextPatchAssetID("music_00_hb_44");
    U32 aidAlbum = iTextPatchAssetID("music_10_gy_44");

    check(iSoundtrackFind(aidNamed) != NULL, "a file named after its asset is found by name");
    check(iSoundtrackFind(aidAlbum) == NULL, "and one named after the music is not, on its own");
    check(iSoundtrackFind(0x12345678) == NULL, "an asset with no file is not overridden");

    // The mapping file supplies what the name cannot.
    char mapping[512];
    snprintf(mapping, sizeof(mapping), "%s/soundtrack.txt", dir);
    FILE* mf = fopen(mapping, "w");
    check(mf != NULL, "the mapping file could be written");
    if (mf != NULL)
    {
        fprintf(mf, "; a comment, and a blank line\n\n");
        fprintf(mf, "music_10_gy_44 = 01. Bikini Bottom.wav\n");
        fprintf(mf, "music_01_jf_44 = not_here.wav\n");
        fclose(mf);
    }

    iSoundtrackSetFolder(dir);

    check(iSoundtrackFind(aidAlbum) != NULL, "the mapping file names a file the scan cannot");
    check(iSoundtrackFind(iTextPatchAssetID("music_01_jf_44")) == NULL,
          "a mapping to a file that is not there is dropped, not stored");
    check(iSoundtrackFind(aidNamed) != NULL, "and the by-name match still stands beside it");

    // The decode itself. Stereo has to survive: this is the whole point.
    U32 ch = 0;
    U32 hz = 0;
    U32 bytes = 0;
    void* pcm = iSoundtrackDecode(named, &ch, &hz, &bytes);

    check(pcm != NULL, "a WAVE file decodes");
    check(ch == 2, "a stereo file comes back stereo");
    check(hz == 44100, "at its own sample rate");
    check(bytes == 64 * 2 * sizeof(S16), "with every frame of it");

    if (pcm != NULL)
    {
        const S16* s = (const S16*)pcm;
        check(s[0] == 100 && s[1] == -100, "left and right land in that order");
        check(s[2] == 101 && s[3] == -101, "and stay interleaved");
        free(pcm);
    }

    pcm = iSoundtrackDecode(album, &ch, &hz, &bytes);
    check(pcm != NULL && ch == 1 && hz == 22050,
          "a mono file at another rate is reported as what it is");
    free(pcm);

    // A file that is not audio at all is a NULL, not a crash: iSndData falls
    // back to the disc on exactly this.
    char junk[512];
    snprintf(junk, sizeof(junk), "%s/junk.wav", dir);
    FILE* jf = fopen(junk, "wb");
    if (jf != NULL)
    {
        fwrite("this is not a wave file at all, not even close", 1, 46, jf);
        fclose(jf);
    }

    ch = 99;
    check(iSoundtrackDecode(junk, &ch, &hz, &bytes) == NULL, "a file that is not audio decodes to nothing");
    check(ch == 0, "and says so rather than leaving the caller's channel count alone");

    iSoundtrackSetFolder("");
    check(iSoundtrackCount() == 0, "clearing the folder drops the scan");
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

// The Xbox release's cave reverb, as iSndSetEnvironmentalEffect holds it. The
// numbers themselves are checked against the disassembly they came from, not
// here; what is under test below is that the reverb driven by them does what
// they describe.
static iSndHostReverb reverb_cave()
{
    iSndHostReverb p;

    p.room = -1000;
    p.room_hf = 0;
    p.room_rolloff_factor = 0.0f;
    p.decay_time = 1.65f;
    p.decay_hf_ratio = 1.5f;
    p.reflections = -1363;
    p.reflections_delay = 0.008f;
    p.reverb = -1153;
    p.reverb_delay = 0.012f;
    p.diffusion = 100.0f;
    p.density = 100.0f;
    p.hf_reference = 5000.0f;

    return p;
}

static U32 reverb_seed;

static float reverb_noise()
{
    reverb_seed = reverb_seed * 1664525u + 1013904223u;
    return (float)((reverb_seed >> 9) & 0x7FFFFF) / 4194304.0f - 1.0f;
}

// Drives an impulse through the reverb and returns the time, in seconds, by
// which its envelope has fallen 60 dB below its peak -- which is the definition
// of RT60, and so the thing decay_time is supposed to set. Resolved to the
// 50 ms block the envelope is measured over.
static float reverb_rt60(const iSndHostReverb* p, U32 rate)
{
    const U32 kFrames = 2400;
    const U32 kBlocks = 120;
    static float b[2 * 2400];
    static float rms[120];

    iSndReverbExit();
    iSndReverbInit(rate);
    iSndReverbSet(p);

    // The fade-in advances on processed frames, so silence is what moves it.
    // Measuring the tail through a ramp would stretch the answer.
    memset(b, 0, sizeof(b));
    for (U32 i = 0; i < 3; i++)
    {
        iSndReverbProcess(b, kFrames);
    }

    for (U32 blk = 0; blk < kBlocks; blk++)
    {
        memset(b, 0, sizeof(b));
        if (blk == 0)
        {
            b[0] = 1.0f;
            b[1] = 1.0f;
        }

        iSndReverbProcess(b, kFrames);

        double sum = 0.0;
        for (U32 n = 0; n < kFrames; n++)
        {
            // Frame zero is the impulse itself, which is dry and would swamp
            // the tail it is supposed to be measured against.
            if (blk == 0 && n == 0)
            {
                continue;
            }
            sum += (double)b[n * 2] * (double)b[n * 2];
        }

        rms[blk] = (float)sqrt(sum / (double)kFrames);
    }

    // The peak is in the build-up, which is over well inside 200 ms.
    float peak = 0.0f;
    for (U32 blk = 0; blk < 4; blk++)
    {
        if (rms[blk] > peak)
        {
            peak = rms[blk];
        }
    }

    if (peak <= 0.0f)
    {
        return -1.0f;
    }

    for (U32 blk = 4; blk < kBlocks; blk++)
    {
        if (rms[blk] < peak * 0.001f)
        {
            return (float)(blk * kFrames) / (float)rate;
        }
    }

    return -1.0f;
}

static void test_snd_reverb()
{
    printf("iSndReverb\n");

    const U32 rate = 48000;
    const U32 kFrames = 2400; // 50 ms
    static float blk[2 * 2400];
    static float dry[2 * 2400];

    iSndHostReverb cave = reverb_cave();

    iSndReverbInit(rate);
    check(iSndReverbIdle(), "a reverb with no parameters set is idle");

    // --- the early reflections ----------------------------------------------
    // With the late path silenced, the only thing left is the tap line, so the
    // first thing out of it is the first reflection and its arrival can be
    // read off directly.
    iSndHostReverb early = cave;
    early.reverb = -10000;
    iSndReverbSet(&early);

    memset(blk, 0, sizeof(blk));
    for (U32 i = 0; i < 3; i++)
    {
        iSndReverbProcess(blk, kFrames);
    }
    check(!iSndReverbIdle(), "and is not idle once a set of parameters arrives");

    memset(blk, 0, sizeof(blk));
    blk[0] = 1.0f;
    blk[1] = 1.0f;
    iSndReverbProcess(blk, kFrames);

    // Nothing wet can have arrived yet, so this frame is the dry one exactly.
    // The whole design rests on the dry path being the game's own mix.
    check(blk[0] == 1.0f && blk[1] == 1.0f, "the dry signal passes through untouched");

    U32 first = 0;
    for (U32 n = 1; n < kFrames; n++)
    {
        if (blk[n * 2] != 0.0f)
        {
            first = n;
            break;
        }
    }
    check(first == (U32)(0.008f * (float)rate),
          "the first reflection lands at reflections_delay");

    // --- the decay ----------------------------------------------------------
    // The headline claim: decay_time is an RT60 and the comb feedback is
    // derived from it, so an impulse must be 60 dB down after that long. The
    // HF ratio is put back to one for this, so that the whole spectrum decays
    // together and the answer is a single number.
    iSndHostReverb flat = cave;
    flat.reflections = -10000;
    flat.decay_hf_ratio = 1.0f;
    flat.decay_time = 1.0f;

    float rt1 = reverb_rt60(&flat, rate);
    check(rt1 > 0.8f && rt1 < 1.5f, "a one-second decay_time decays by 60 dB in about a second");

    flat.decay_time = 2.5f;
    float rt2 = reverb_rt60(&flat, rate);
    check(rt2 > 2.0f && rt2 < 3.5f, "and a two-and-a-half-second one takes that much longer");

    // decay_hf_ratio above one is the unusual half of these parameters: it
    // makes high frequencies ring LONGER, which the lowpass in a textbook comb
    // cannot do. So the shelf's direction is worth a measurement of its own.
    flat.decay_time = 1.0f;
    flat.decay_hf_ratio = 1.6f;
    float rtbright = reverb_rt60(&flat, rate);
    check(rtbright > rt1, "a decay_hf_ratio above one makes the tail last longer, not shorter");

    // --- the level ----------------------------------------------------------
    // room and reverb are absolute attenuations of the late path, so the wet
    // signal has to come out at that level relative to the dry one. This is
    // what the network's normalisation is for: eight combs summed raw would be
    // some twenty times too loud and the parameter would mean nothing.
    iSndReverbExit();
    iSndReverbInit(rate);

    iSndHostReverb late = cave;
    late.reflections = -10000;
    iSndReverbSet(&late);

    reverb_seed = 12345;
    double wet_sum = 0.0;
    double dry_sum = 0.0;

    for (U32 b = 0; b < 60; b++)
    {
        for (U32 n = 0; n < kFrames; n++)
        {
            float v = reverb_noise();
            dry[n * 2 + 0] = v;
            dry[n * 2 + 1] = v;
            blk[n * 2 + 0] = v;
            blk[n * 2 + 1] = v;
        }

        iSndReverbProcess(blk, kFrames);

        // Only once the network has filled, which takes a few decay times.
        if (b >= 40)
        {
            for (U32 n = 0; n < kFrames; n++)
            {
                double w = (double)blk[n * 2] - (double)dry[n * 2];
                wet_sum += w * w;
                dry_sum += (double)dry[n * 2] * (double)dry[n * 2];
            }
        }
    }

    float measured = (float)sqrt(wet_sum / dry_sum);
    float wanted = powf(10.0f, (float)(cave.room + cave.reverb) / 2000.0f);

    // Within a factor of two, and no tighter: the normalisation estimates what
    // eight combs do to a broadband signal from their loop gains rather than
    // measuring it, so it is meant to put the level in the right place and not
    // to be exact.
    check(measured > wanted * 0.5f && measured < wanted * 2.0f,
          "the late level comes out where room + reverb puts it");

    // --- stability ----------------------------------------------------------
    // Feedback loops that are a little too hot do not sound wrong, they grow.
    // Full-scale noise for three seconds is what would find it.
    iSndReverbExit();
    iSndReverbInit(rate);
    iSndReverbSet(&cave);

    reverb_seed = 999;
    bool sane = true;

    for (U32 b = 0; b < 60; b++)
    {
        for (U32 n = 0; n < kFrames; n++)
        {
            float v = reverb_noise();
            blk[n * 2 + 0] = v;
            blk[n * 2 + 1] = v;
        }

        iSndReverbProcess(blk, kFrames);

        for (U32 n = 0; n < 2 * kFrames; n++)
        {
            if (!(blk[n] > -4.0f && blk[n] < 4.0f))
            {
                sane = false;
            }
        }
    }

    check(sane, "three seconds of full-scale noise neither diverges nor goes non-finite");

    // The late chain recirculates -- the last stage's output is summed back
    // into the first -- so unlike a bank of parallel combs it can be driven
    // unstable by parameters alone. The game only ever asks for 1.65 seconds,
    // but I3DL2 allows twenty, and the loop gain rises with it.
    iSndReverbExit();
    iSndReverbInit(rate);

    iSndHostReverb huge = cave;
    huge.decay_time = 20.0f;
    huge.decay_hf_ratio = 2.0f;
    huge.room = 0;
    huge.reverb = 0;
    huge.reflections = 0;
    iSndReverbSet(&huge);

    reverb_seed = 31337;
    bool sane_long = true;

    for (U32 b = 0; b < 100; b++)
    {
        for (U32 n = 0; n < kFrames; n++)
        {
            // Silence after the first second, so what is measured is the tail
            // rather than what is being fed in.
            float v = (b < 20) ? reverb_noise() : 0.0f;
            blk[n * 2 + 0] = v;
            blk[n * 2 + 1] = v;
        }

        iSndReverbProcess(blk, kFrames);

        for (U32 n = 0; n < 2 * kFrames; n++)
        {
            if (!(blk[n] > -8.0f && blk[n] < 8.0f))
            {
                sane_long = false;
            }
        }
    }

    check(sane_long, "and the longest decay I3DL2 allows still decays rather than growing");

    // --- switching it off ---------------------------------------------------
    // The scene the game leaves a cave for asks for no effect at all, and the
    // dry mix it gets back has to be the one it handed over.
    iSndReverbSet(NULL);

    memset(blk, 0, sizeof(blk));
    for (U32 i = 0; i < 3; i++)
    {
        iSndReverbProcess(blk, kFrames);
    }
    check(iSndReverbIdle(), "removing the effect settles to idle");

    reverb_seed = 4242;
    for (U32 n = 0; n < kFrames; n++)
    {
        float v = reverb_noise();
        dry[n * 2 + 0] = v;
        dry[n * 2 + 1] = v;
        blk[n * 2 + 0] = v;
        blk[n * 2 + 1] = v;
    }

    iSndReverbProcess(blk, kFrames);
    check(memcmp(blk, dry, sizeof(float) * 2 * kFrames) == 0,
          "after which the mix is passed through bit for bit");

    iSndReverbExit();
}

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

    // A locked stream slot is not free, even with nothing playing on it.
    //
    // xSndStreamLock reserves a slot for an owner without starting anything --
    // zTalkBox takes two for a conversation. Handing one of those out to
    // somebody else puts them where the owner already believes it is, and the
    // owner's next sound comes through the reclaim path and stops them. That is
    // what silenced the level music at the first line of dialogue: it took a
    // slot zTalkBox held, and zTalkBox's next line killed it.
    gSnd.voice[0].lock_owner = 0xF00D;

    S32 locked = iSndFindFreeVoice(128, 0x4, 0);
    check(locked >= 1 && locked < 6, "an unowned stream request skips a slot someone has locked");

    S32 owned = iSndFindFreeVoice(128, 0x4, 0xF00D);
    check(owned == 0, "and its owner still gets it");

    // Locks are a stream thing; the other 58 voices have no owners, and retail
    // does not test one for them.
    gSnd.voice[6].lock_owner = 0xF00D;
    check(iSndFindFreeVoice(128, 0x2, 0) == 6, "a sound voice ignores lock_owner");
    gSnd.voice[6].lock_owner = 0;
    gSnd.voice[0].lock_owner = 0;

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

// The two facts zCameraFlyUpdate's key decoding rests on: a zFlyKey is 64 bytes
// laid out the way the FLY asset lays one out, and the asset's words are
// little-endian.
//
// The second one is why the byte-reversing loop in zCameraFlyUpdate is behind
// PLATFORM_PC. On the console the loop is the only reason the flythrough works
// at all -- it has to flip all 64 words of the four keys before a float can be
// read out of them -- and on a host it is the only reason it would not.
// Nothing else in the build says which way round that is, so the bytes below
// are verbatim from the shipping asset and say it.
//
// It is not one asset either: all 17 FLY assets in the retail tree are a whole
// number of 64-byte keys, and all 17 have frame counters that count up by one
// read little-endian. None does read big-endian.
static const U8 hb01_flythrough_keys[128] = {
    // key 0: frame 0, identity basis at the origin, aperture 0.98/0.735, focal 26.6906
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x48, 0xe1, 0x7a, 0x3f, 0xf6, 0x28, 0x3c, 0x3f, 0x4b, 0x86, 0xd5, 0x41,
    // key 1: frame 1, the first real pose of the shot
    0x01, 0x00, 0x00, 0x00, 0x87, 0x88, 0x7c, 0x3f, 0xeb, 0xc1, 0x24, 0xbe, 0x56, 0x57, 0x02, 0xbd,
    0x25, 0xfe, 0xdb, 0x3d, 0xf9, 0xc8, 0x47, 0x3f, 0x51, 0xb0, 0x1d, 0xbf, 0xe8, 0xd4, 0xfd, 0x3d,
    0xa4, 0xad, 0x1a, 0x3f, 0x0c, 0x81, 0x49, 0x3f, 0xc9, 0x96, 0x78, 0x42, 0xdd, 0x5c, 0xe0, 0x41,
    0x03, 0x9d, 0x64, 0x41, 0x48, 0xe1, 0x7a, 0x3f, 0xf6, 0x28, 0x3c, 0x3f, 0x4b, 0x86, 0xd5, 0x41,
};

// The loop zCameraFlyUpdate runs on the console, over as many words as it is
// given, so the test can show what it does to host-order data.
static void reverse_words(U8* p, int words)
{
    for (int w = 0; w < words; w++, p += 4)
    {
        U8 a = p[0];
        U8 b = p[1];
        p[0] = p[3];
        p[1] = p[2];
        p[2] = b;
        p[3] = a;
    }
}

static F32 row_len(const F32* m)
{
    return sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
}

// xordarray allocates through the game's heap. The ordering test builds its
// array by hand and never calls XOrdInit, so these exist only to satisfy the
// linker; malloc is a truthful stand-in for the one path that could reach them.
U32 gActiveHeap = 0;

void* xMemAlloc(U32, U32 size, S32)
{
    return malloc(size);
}

void* xMemPushTemp(U32 size)
{
    return malloc(size);
}

void xMemPopTemp(void* memory)
{
    free(memory);
}

// The NPC list's ordering, which is what makes talking to a character work.
//
// zNPCMgr keeps every NPC in an st_XORDEREDARRAY sorted by id, and
// zNPCMsg_SendMsg resolves a message's target through XOrdLookup -- a BINARY
// search. An ordering that disagrees with the lookup's key does not make the
// search slow, it makes it fail, and a dropped NPC_MID_TALKSTART is a character
// that cannot be talked to.
//
// The hazard is the object layout. Retail's comparators read the id as
// *(U32*)p, offset zero of the object, which is where CodeWarrior leaves
// xBase::id because it places the vptr at the first virtual's declaration. The
// MSVC ABI puts the vptr at offset zero and moves id to four, so the same
// expression yields a vtable address. These stand-ins have that same shape: a
// vptr the compiler puts first, and an id behind it.
struct ord_npc
{
    virtual ~ord_npc()
    {
    }
    U32 id;
};

// Retail's expression, which on a host reads the vptr rather than the id.
static S32 ord_comp_offset0(void* vkey, void* vitem)
{
    U32 key = *(U32*)vkey;
    U32 item = *(U32*)vitem;
    return key < item ? -1 : (key > item ? 1 : 0);
}

// The port's, reading both sides through the type -- the shape
// zNPCMgr_OrdComp_npcid now has.
static S32 ord_comp_typed(void* vkey, void* vitem)
{
    U32 key = ((ord_npc*)vkey)->id;
    U32 item = ((ord_npc*)vitem)->id;
    return key < item ? -1 : (key > item ? 1 : 0);
}

static S32 ord_test_typed(const void* vkey, void* vitem)
{
    void* key = (void*)((ord_npc*)vitem)->id;
    return vkey < key ? -1 : (vkey > key ? 1 : 0);
}

static void test_npc_ordering()
{
    printf("NPC id ordering (XOrdSort / XOrdLookup)\n");

    check(__builtin_offsetof(ord_npc, id) != 0,
          "a polymorphic object does not keep its id at offset 0 here");

    // Ids as they really are: xStrHash output, unordered, and above 2^31 often
    // enough that a signed compare would be its own bug.
    static const U32 ids[8] = { 0x9995ADB1, 0x1C0FE2A3, 0xF00DBEEF, 0x00000042,
                                0x7FFFFFFF, 0x80000000, 0xABCDEF01, 0x0000A5A5 };

    ord_npc objs[8];
    for (S32 i = 0; i < 8; i++)
    {
        objs[i].id = ids[i];
    }

    void* slots[8];
    st_XORDEREDARRAY arr;
    arr.list = slots;
    arr.cnt = 8;
    arr.max = 8;

    // Sorted the way retail's expression sorts on a host: by vtable pointer,
    // which every one of these objects shares, so nothing moves.
    for (S32 i = 0; i < 8; i++)
    {
        slots[i] = &objs[i];
    }
    XOrdSort(&arr, ord_comp_offset0);

    S32 found_offset0 = 0;
    for (S32 i = 0; i < 8; i++)
    {
        if (XOrdLookup(&arr, (const void*)ids[i], ord_test_typed) >= 0)
        {
            found_offset0++;
        }
    }
    check(found_offset0 < 8,
          "reading the key at offset 0 leaves the list unsorted and lookups fail");

    // Sorted through the type.
    for (S32 i = 0; i < 8; i++)
    {
        slots[i] = &objs[i];
    }
    XOrdSort(&arr, ord_comp_typed);

    S32 ascending = 1;
    for (S32 i = 1; i < 8; i++)
    {
        if (((ord_npc*)slots[i - 1])->id > ((ord_npc*)slots[i])->id)
        {
            ascending = 0;
        }
    }
    check(ascending == 1, "sorting through the type orders the list by id");

    S32 found = 0;
    for (S32 i = 0; i < 8; i++)
    {
        S32 idx = XOrdLookup(&arr, (const void*)ids[i], ord_test_typed);
        if (idx >= 0 && ((ord_npc*)slots[idx])->id == ids[i])
        {
            found++;
        }
    }
    check(found == 8, "every NPC id is then found by the binary search");

    // The lookup must also not invent a hit, or a message would go to the wrong
    // NPC rather than nowhere.
    check(XOrdLookup(&arr, (const void*)0x13579BDF, ord_test_typed) < 0,
          "an id that is not in the list is not found");
}

static void test_flykey()
{
    printf("FLY asset (fly camera keys)\n");

    // zCameraFlyUpdate indexes the asset as an array of zFlyKey and derives the
    // key count as size >> 6, so both of these are load-bearing.
    check(sizeof(zFlyKey) == 64, "sizeof(zFlyKey) == 64");
    check(offsetof(zFlyKey, frame) == 0, "zFlyKey::frame at 0x00");
    check(offsetof(zFlyKey, matrix) == 4, "zFlyKey::matrix at 0x04");
    check(offsetof(zFlyKey, aperture) == 52, "zFlyKey::aperture at 0x34");
    check(offsetof(zFlyKey, focal) == 60, "zFlyKey::focal at 0x3c");

    // hb01_flythrough is 12864 bytes in hb01.HIP, which is 201 whole keys.
    check(12864 % (int)sizeof(zFlyKey) == 0 && 12864 / (int)sizeof(zFlyKey) == 201,
          "hb01_flythrough's 12864 bytes are 201 whole keys");

    zFlyKey keys[2];
    memcpy(keys, hb01_flythrough_keys, sizeof(keys));

    // Read straight out of the asset, which is what the port does.
    check(keys[0].frame == 0 && keys[1].frame == 1, "frame counters read 0 and 1");
    check(keys[0].matrix[0] == 1.0f && keys[0].matrix[4] == 1.0f && keys[0].matrix[8] == 1.0f,
          "key 0's basis has a unit diagonal");
    check(keys[0].matrix[1] == 0.0f && keys[0].matrix[2] == 0.0f && keys[0].matrix[3] == 0.0f,
          "key 0's basis is the identity");
    check(fabsf(keys[0].aperture[0] - 0.98f) < 1e-6f && fabsf(keys[0].focal - 26.6906f) < 1e-3f,
          "key 0's lens is 0.98 aperture / 26.69 focal");
    check(fabsf(row_len(&keys[1].matrix[0]) - 1.0f) < 1e-4f &&
              fabsf(row_len(&keys[1].matrix[3]) - 1.0f) < 1e-4f &&
              fabsf(row_len(&keys[1].matrix[6]) - 1.0f) < 1e-4f,
          "key 1's basis is orthonormal");

    // And what the console arm would make of the same bytes. Every one of these
    // feeds xQuatFromMat, so the flythrough this produces is not a wrong camera
    // path, it is no camera path.
    zFlyKey flipped[2];
    memcpy(flipped, hb01_flythrough_keys, sizeof(flipped));
    reverse_words((U8*)flipped, (int)sizeof(flipped) / 4);

    check(flipped[1].frame == 0x01000000, "byte-reversed, frame 1 reads 16777216");
    check(flipped[0].matrix[0] != 1.0f, "byte-reversed, the identity is no longer 1.0");
    check(fabsf(row_len(&flipped[1].matrix[0]) - 1.0f) > 0.5f,
          "byte-reversed, key 1's basis is not orthonormal");
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

    // First: iConfig parses once per process, and this is what decides which
    // file that one parse reads.
    test_config();
    test_screen();
    test_drawdist();
    test_textpatch();

    test_time();
    test_math();
    test_mem();
    test_file();
    test_idtag();
    test_pad();
    test_savegame();
    test_snd_data();
    test_soundtrack();
#ifdef BFBB_AUDIO_BACKEND_WIN32
    test_snd_mixer();
#endif
    test_snd();
    test_snd_reverb();
    test_hip();
    test_flykey();
    test_npc_ordering();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "passed", failures,
           failures == 1 ? "" : "s");

    return failures ? 1 : 0;
}
