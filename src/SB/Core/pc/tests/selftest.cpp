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
#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif
#include <string.h>

#include <types.h>

#include "config_model.h"
#include "mods_model.h"
#include "saves_model.h"
#include "iBoot.h"
#include "iConfig.h"
#include "iConfigEdit.h"
#include "iConfigTable.h"
#include "iDrawDist.h"
#include "iFile.h"
#include "iGCMem.h"
#include "iModPatch.h"
#include "iModManifest.h"
#include "iGCSwap.h"
#include "iHipMerge.h"
#include "iHost.h"
#include "iSnd.h"
#include "iSndData.h"
#include "iSndDsp.h"
#include "iSoundtrack.h"
#include "xpkrsvc.h"
#include "xhipio.h"
#include "iSndHost.h"
#include "iSndReverb.h"
#include "xSnd.h"
#include "iMath.h"
#include "iMemMgr.h"
#include "iModFolder.h"
#include "iSaveFile.h"
#include "iCameraLook.h"
#include "iPadKeyboard.h"
#include "iPadLayout.h"
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
#include "zGlobals.h"
#include "xEvent.h"
#include "xLinkAsset.h"

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

    // Two different claims, and only the first is about this code.
    //
    // The floor says the sleep actually slept -- an early return, or a tick
    // rate wrong by a factor, lands well under it. It is what a change to
    // iHostSleepUntilNs can break, so it stays tight.
    //
    // The ceiling only says the sleep finished, because how long AFTER its
    // deadline a thread is rescheduled is the machine's business and not the
    // port's. It was 0.20 s and failed intermittently on a shared CI runner,
    // which is the check asserting something it cannot know. A second is still
    // an order of magnitude clear of the mistakes worth catching -- a
    // milliseconds-for-nanoseconds slip overshoots by a thousand.
    check(dt > 0.10f && dt < 1.0f, "iTimeDiffSec reports ~0.12 s for a 0.12 s sleep");

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

    // 64 MB: retail's 0x384000 ran out under a 64-bit build's larger objects
    // and under the tessellated models experimental.hipoly_assets hands the
    // goo effect. iMemMgr.cpp sizes it; this checks the value that reaches the
    // game.
    check(gMemInfo.DRAM.size == 0x4000000, "DRAM is 64 MB");

    // xMemInitHeap does its arithmetic on gMemInfo.DRAM.addr as a U32, so the
    // arena has to be addressable in 32 bits and survive the round trip.
    U32 base = gMemInfo.DRAM.addr;
    void* p = (void*)(unsigned long)base;
    check((U32)(unsigned long)p == base, "the arena's address round-trips through U32");

    // xMemInit puts gxHeap[1] and gxHeap[2] at DRAM.addr + DRAM.size. Retail
    // leaves those past its own allocation; iMemInit reserves twice the size
    // so they are backed. Write to the far end to prove it.
    volatile U8* top = (volatile U8*)p + (2 * gMemInfo.DRAM.size) - 1;
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
                                "a      = space     ; jump, confirm\n"
                                "b      = mouse2,lctrl\n"
                                "x      = mouse1, e\n"
                                "y      = q\n"
                                "z      = f\n"
                                "hud    = g\n"
                                "l1     = z\n"
                                "r1     = c\n"
                                "l2     = x\n"
                                "r2     = v\n"
                                "start = f1\n"
                                "select = backspace\n"
                                "up     = up\n"
                                "down   = down\n"
                                "left   = left\n"
                                "right  = right\n";

    FILE* f = fopen(path, "wb");
    if (f == NULL)
    {
        check(false, "could not write a config.ini");
        return;
    }
    fwrite(kFile, 1, sizeof(kFile) - 1, f);
    // More entries than the parser once held, then one more. A key past the
    // cap read as missing, so the load appended it again on every launch.
    fprintf(f, "[fixes]\n");
    for (int i = 0; i < 130; i++)
    {
        fprintf(f, "melee_once = on\n");
    }
    fprintf(f, "[cheats]\npanhandle = on\n");
    fclose(f);

    // A mod whose mod.ini replaces one setting, tries to move the asset root,
    // and names a setting that does not exist. Loaded with the file, so it is
    // set up here.
    char modDir[512];
    char modIni[512];
    snprintf(modDir, sizeof(modDir), "%s/testmod", dir);
    snprintf(modIni, sizeof(modIni), "%s/mod.ini", modDir);
    iHostMakeDir(modDir);
    {
        static const char kModIni[] = "[mod]\n"
                                      "name = Test Mod\n"
                                      "[video]\n"
                                      "fov = 75\n"
                                      "nosuch = 1\n"
                                      "[assets]\n"
                                      "path = elsewhere\n";
        FILE* m = fopen(modIni, "wb");
        if (m != NULL)
        {
            fwrite(kModIni, 1, sizeof(kModIni) - 1, m);
            fclose(m);
        }
    }
    // A second mod, below the first, setting the same key.
    char modDir2[512];
    char modIni2[512];
    snprintf(modDir2, sizeof(modDir2), "%s/testmod2", dir);
    snprintf(modIni2, sizeof(modIni2), "%s/mod.ini", modDir2);
    iHostMakeDir(modDir2);
    {
        static const char kModIni2[] = "[video]\nfov = 60\n";
        FILE* m = fopen(modIni2, "wb");
        if (m != NULL)
        {
            fwrite(kModIni2, 1, sizeof(kModIni2) - 1, m);
            fclose(m);
        }
    }

    char mods[1100];
    snprintf(mods, sizeof(mods), "%s | %s", modDir, modDir2);
    iHostSetEnv("BFBB_MOD", mods);

    check(iHostSetEnv("BFBB_CONFIG", path), "BFBB_CONFIG can be set");

    iConfigLoad();

    iHostSetEnv("BFBB_MOD", NULL);

    check(iConfigGetFloat("video.fov", 0.0f) == 75.0f,
          "a setting the mod's mod.ini gives wins, the first mod's over the second's");
    check(iConfigGetBool("video.nosuch", TRUE) == TRUE, "an unknown setting in mod.ini is dropped");

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
          "the asset path survives spaces and backslashes, and a mod cannot move it");

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
    check(strcmp(iConfigGetString("xbox.nothing", "d"), "d") == 0, "an unknown string falls back");

    // The two binding sections are not in the settings table -- their contents
    // are one row per game button, and that list lives in iPadBind.cpp. They
    // still have to behave like every other key: known, defaulted from the same
    // place the generated file is written from, and reported when misspelled.
    check(strcmp(iConfigGetString("pad.select", "!"), "ls") == 0,
          "a [pad] binding comes off the file");
    check(strcmp(iConfigGetString("keyboard.start", "!"), "f1") == 0, "and a [keyboard] one");
    check(strcmp(iConfigGetString("keyboard.r1", "!"), iPadBindFind("r1")->key) == 0,
          "a [keyboard] line at an old build's default answers with this build's");

    // Answered from the ACTIVE PRESET, not from the row's own column: the
    // generated file and a missing key have to agree, and under a preset the
    // row's column is only the base one preset happens to be written against.
    // The harness file names no preset, so this is the default one.
    const iPadBindButton* b = iPadBindFind("b");
    check(b != NULL && strcmp(iConfigGetString("pad.b", "!"), iPadBindPadDefault(b)) == 0,
          "a binding the file omits is answered from the preset, not the fallback");
    // The default is "auto", which asks the backend what kind of pad is on
    // port 0. Nothing identifiable is plugged in at this point in the run, and
    // nobody can say which button is printed B without a pad, so the row's own
    // base answers -- and that column is written for an Xbox pad.
    check(b != NULL && strcmp(iPadBindPadDefault(b), "b") == 0,
          "with no pad to read letters off, a preset falls back on the row's base");
    check(b != NULL && strcmp(iConfigGetString("keyboard.b", "!"), b->key) == 0, "on both devices");

    check(strcmp(iConfigGetString("pad.nonsense", "!"), "!") == 0,
          "a binding for a button that does not exist is dropped, like any unknown key");

    check(iConfigGetInt("input.controller", 0) == 3, "input.controller comes off the file");
    check(iConfigGetBool("cheats.panhandle", FALSE) == TRUE,
          "a setting past the 128th entry comes off the file");
    // The frame rate. The file does not mention it, so this is the table's
    // default reaching a caller whose fallback is the wrong answer -- and it is
    // the one setting where the wrong answer is not visible on screen but in
    // how fast the game runs.
    check(iConfigGetInt("video.framerate", 1) == 60,
          "video.framerate defaults to the console's rate");
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
            check(strstr(grown, "msaa = 1") != NULL,
                  "a setting the file never had is appended at its default");
            check(strstr(grown, "shadow_resolution = auto") != NULL, "and so is another");

            // The bindings are not in the settings table -- they come from
            // iPadBind.cpp -- and are NOT appended. An appended line would be a
            // value, and a value pins that key past any later change to the
            // defaults; an omitted one is answered by the build's default.
            check(countLinesStartingWith(grown, "panhandle") == 1,
                  "a setting past the 128th entry is not appended again");

            check(countLinesStartingWith(grown, "view ") == 0,
                  "a keyboard binding the file omits is left to the default");

            // The file's [keyboard] block is an old build's: every row live.
            // The rows at that build's defaults come out so the keys follow
            // this build's; the one the player changed stays.
            check(countLinesStartingWith(grown, "r1 ") == 0 &&
                      countLinesStartingWith(grown, "b ") == 0,
                  "a keyboard line at an old build's default is taken out");
            check(strstr(grown, "start = f1") != NULL, "and one the player changed is kept");

            // The pad's does NOT arrive. A generated file lists those commented
            // so input.preset stays in charge, and a comment is not an entry --
            // appending them would add fifteen more commented lines on every
            // single run, forever. Nothing is lost: a pad binding the file does
            // not mention is answered by the preset.
            check(countLinesStartingWith(grown, "; y ") == 0,
                  "and the pad's is not appended at all, so the file cannot grow each run");

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

    // The in-game settings screen: a set value is what the getters answer from
    // then on, and the save writes it into the file in place -- the rest of the
    // file, the unknown line included, comes back as it was.
    iConfigSet("XBOX.glow", "on");
    iConfigSet("video.fov", "90");
    check(iConfigGetBool("xbox.glow", FALSE) == TRUE, "a set value is answered at once");
    check(iConfigGetFloat("video.fov", 0.0f) == 90.0f,
          "and so is one the file never had, over the mod's");
    check(iConfigSave(), "the changed settings save");
    {
        char saved[8192];
        FILE* r = fopen(path, "rb");
        size_t n = (r != NULL) ? fread(saved, 1, sizeof(saved) - 1, r) : 0;
        if (r != NULL)
        {
            fclose(r);
        }
        saved[n] = '\0';

        check(strstr(saved, "Glow   =   on") != NULL, "the set value replaces the line's own");
        check(strstr(saved, "OFF") == NULL, "and the old value is gone");
        check(strstr(saved, "fov = 90") != NULL, "a setting the file lacked is added");
        check(strstr(saved, "fov = 75") == NULL, "and the mod's value is never written");
        check(strstr(saved, "; the port's settings") != NULL && strstr(saved, "glwo = off") != NULL,
              "the comment and the unknown line survive the save");
    }
    check(iConfigSave(), "a save with nothing changed succeeds without writing");

    // A binding put back to its default: answered from the table again, and
    // its line taken out of the file rather than rewritten.
    iConfigUnset("keyboard.start");
    check(strcmp(iConfigGetString("keyboard.start", "!"), iPadBindFind("start")->key) == 0,
          "an unset binding answers with its default");
    check(iConfigSave(), "and the save succeeds");
    {
        char saved[8192];
        FILE* r = fopen(path, "rb");
        size_t n = (r != NULL) ? fread(saved, 1, sizeof(saved) - 1, r) : 0;
        if (r != NULL)
        {
            fclose(r);
        }
        saved[n] = '\0';
        check(strstr(saved, "start = f1") == NULL, "and its line is gone from the file");
        check(strstr(saved, "[keyboard]") != NULL, "while its section stays");
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
        char buf[32768];
        size_t n = fread(buf, 1, sizeof(buf) - 1, r);
        fclose(r);
        buf[n] = '\0';

        check(strstr(buf, "[assets]") != NULL, "it has the [assets] section header");
        check(strstr(buf, "path =") != NULL, "and the asset path, empty as it has to be");
        check(strstr(buf, "[video]") != NULL, "it has the [video] section header");
        check(strstr(buf, "mode = borderless") != NULL, "and the window mode at its default");
        check(strstr(buf, "width = 640") != NULL, "and the render width at its default");
        check(strstr(buf, "height = 480") != NULL, "and the height");
        check(strstr(buf, "framerate = 60") != NULL, "and the frame rate at the console's");
        check(strstr(buf, "vsync = on") != NULL, "and vsync");
        check(strstr(buf, "draw_distance = on") != NULL, "and the draw distance");
        check(strstr(buf, "msaa = 1") != NULL, "and the sample count");
        check(strstr(buf, "per_pixel_lighting = off") != NULL, "and per-pixel lighting");
        check(strstr(buf, "pipeline = auto") != NULL, "and the render pipeline");
        check(strstr(buf, "[xbox]") != NULL, "it has the [xbox] section header");
        check(strstr(buf, "glow = on") != NULL, "and glow at its default");
        check(strstr(buf, "distortion = on") != NULL, "and distortion");
        check(strstr(buf, "snapshot = on") != NULL, "and snapshot");
        check(strstr(buf, "reverb = on") != NULL, "and reverb");
        check(strstr(buf, "sound_rolloff = on") != NULL, "and the sound rolloff");
        check(strstr(buf, "[font]") != NULL, "it has the [font] section header");
        // Anchored on the newline: unanchored, "fit = box" would also be
        // found inside a "sans_fit = box" line.
        check(strstr(buf, "\nfit = box") != NULL, "and the glyph fit at its default");
        check(strstr(buf, "platform_wording = on") != NULL, "and the text rewrite at its default");
    }

    iHostRemoveFile(written);
    iHostRemoveFile(modIni);
    iHostRemoveDir(modDir);
    iHostRemoveFile(modIni2);
    iHostRemoveDir(modDir2);
    iHostRemoveDir(dir);
}

// The settings table's domains, and the in-place writer the configurator saves
// through. Both are checked here rather than in the configurator, which is a
// window and cannot be run by ctest.
static void test_config_edit()
{
    printf("iConfigTable, iConfigEdit\n");

    // --- the domains ---------------------------------------------------
    // Every default has to be a value its own domain admits. This is the one
    // that catches a domain written wrong: a range that excludes the default,
    // or a choices list that forgot the word the default is.
    S32 badDefaults = 0;
    for (S32 i = 0; i < kConfigSettingCount; i++)
    {
        if (!iConfigTableValidate(&kConfigSettings[i], kConfigSettings[i].value, NULL, 0))
        {
            printf("    %s.%s defaults to \"%s\", which its own domain rejects\n",
                   kConfigSettings[i].section, kConfigSettings[i].name, kConfigSettings[i].value);
            badDefaults++;
        }
    }
    check(badDefaults == 0, "every setting's default is a value its domain admits");

    // And every setting has a comment, since that comment is what the file and
    // the configurator both show.
    S32 noComment = 0;
    for (S32 i = 0; i < kConfigSettingCount; i++)
    {
        char summary[512];
        iConfigTableSummary(&kConfigSettings[i], summary, sizeof(summary));
        if (summary[0] == '\0')
        {
            noComment++;
        }
    }
    check(noComment == 0, "and a description that survives being unwrapped");

    {
        const iConfigSetting* fov = iConfigTableFind("video.fov");
        check(fov != NULL, "video.fov is in the table");
        if (fov != NULL)
        {
            check(iConfigTableValidate(fov, "90", NULL, 0), "and takes 90");
            check(!iConfigTableValidate(fov, "wide", NULL, 0), "and not a word");
            check(!iConfigTableValidate(fov, "500", NULL, 0), "and not 500");
        }

        // A number OR a word, which is the case the kind alone cannot express.
        const iConfigSetting* rate = iConfigTableFind("VIDEO.FRAMERATE");
        check(rate != NULL, "the lookup is case-insensitive");
        if (rate != NULL)
        {
            check(iConfigTableValidate(rate, "144", NULL, 0), "framerate takes a number");
            check(iConfigTableValidate(rate, "display", NULL, 0), "and the word display");
            check(!iConfigTableValidate(rate, "fast", NULL, 0), "and not any other word");
            check(!iConfigTableValidate(rate, "59.94", NULL, 0), "and not a fraction");
        }

        const iConfigSetting* mode = iConfigTableFind("video.mode");
        if (mode != NULL)
        {
            check(iConfigTableValidate(mode, "WINDOWED", NULL, 0), "an enum ignores case");
            check(!iConfigTableValidate(mode, "window", NULL, 0), "and takes only its words");
        }

        const iConfigSetting* glow = iConfigTableFind("xbox.glow");
        if (glow != NULL)
        {
            check(iConfigTableValidate(glow, "yes", NULL, 0), "a switch takes yes");
            check(!iConfigTableValidate(glow, "of", NULL, 0), "and not a near miss");
        }

        char why[128];
        why[0] = '\0';
        const iConfigSetting* width = iConfigTableFind("video.width");
        if (width != NULL)
        {
            iConfigTableValidate(width, "twelve", why, sizeof(why));
            check(why[0] != '\0', "a rejection says why");
        }
    }

    // --- the writer ------------------------------------------------------
    char dir[512];
    if (!scratch_dir("configedit", dir, sizeof(dir)))
    {
        check(false, "could not make a temp directory");
        return;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/config.ini", dir);

    // A file with everything the writer has to leave alone: comments in both
    // spellings, a trailing comment on a value, ragged spacing, a blank line,
    // a section this build does not know, and a commented-out binding.
    static const char kFile[] = "; a settings file\r\n"
                                "[video]\r\n"
                                "mode = fullscreen   ; how the picture is presented\r\n"
                                "width=640\r\n"
                                "\r\n"
                                "# a hash comment\r\n"
                                "height  =  480\r\n"
                                "[mystery]\r\n"
                                "something = 3\r\n"
                                "[pad]\r\n"
                                "; a  = x\r\n";

    FILE* f = fopen(path, "wb");
    if (f == NULL)
    {
        check(false, "could not write a config.ini");
        return;
    }
    fwrite(kFile, 1, sizeof(kFile) - 1, f);
    fclose(f);

    // A round trip that changes nothing must change no bytes. This is the
    // whole promise of editing lines rather than key/value pairs, and it is
    // the one a rewrite-from-scratch writer cannot keep.
    {
        iConfigEditFile* file = iConfigEditOpen(path);
        check(file != NULL, "the file opens");
        if (file != NULL)
        {
            check(!iConfigEditChanged(file), "and reads as unchanged");
            check(iConfigEditSave(file, path), "and saves");
            iConfigEditClose(file);
        }

        char buf[2048];
        FILE* r = fopen(path, "rb");
        size_t n = (r != NULL) ? fread(buf, 1, sizeof(buf) - 1, r) : 0;
        if (r != NULL)
        {
            fclose(r);
        }
        buf[n] = '\0';

        check(n == sizeof(kFile) - 1 && memcmp(buf, kFile, n) == 0,
              "a round trip with no edits changes no bytes");
    }

    // The same promise for a file that ends its lines with LF. A writer that
    // always put CRLF back would rewrite every line of it, so a save that
    // changed one value came out as a diff touching the whole file.
    {
        char lfPath[512];
        snprintf(lfPath, sizeof(lfPath), "%s/lf.ini", dir);

        static const char kLf[] = "[video]\nmode = fullscreen\nwidth = 640\n";

        FILE* w = fopen(lfPath, "wb");
        if (w != NULL)
        {
            fwrite(kLf, 1, sizeof(kLf) - 1, w);
            fclose(w);
        }

        iConfigEditFile* file = iConfigEditOpen(lfPath);
        if (file != NULL)
        {
            iConfigEditSave(file, lfPath);
            iConfigEditClose(file);
        }

        char buf[256];
        FILE* r = fopen(lfPath, "rb");
        size_t n = (r != NULL) ? fread(buf, 1, sizeof(buf) - 1, r) : 0;
        if (r != NULL)
        {
            fclose(r);
        }

        check(n == sizeof(kLf) - 1 && memcmp(buf, kLf, n) == 0,
              "and an LF file keeps its LF endings");

        // And a value written into one does not drag CRLF in behind it.
        file = iConfigEditOpen(lfPath);
        if (file != NULL)
        {
            iConfigEditSet(file, "video", "width", "1920");
            iConfigEditSave(file, lfPath);
            iConfigEditClose(file);
        }

        r = fopen(lfPath, "rb");
        n = (r != NULL) ? fread(buf, 1, sizeof(buf) - 1, r) : 0;
        if (r != NULL)
        {
            fclose(r);
        }
        buf[n] = '\0';

        check(strstr(buf, "\r") == NULL, "and gains none when a value is written");
        check(strstr(buf, "width = 1920") != NULL, "with the value written");

        iHostRemoveFile(lfPath);
    }

    {
        iConfigEditFile* file = iConfigEditOpen(path);
        if (file == NULL)
        {
            check(false, "the file reopens");
            iHostRemoveFile(path);
            iHostRemoveDir(dir);
            return;
        }

        check(strcmp(iConfigEditGet(file, "video", "width"), "640") == 0, "a value reads back");
        check(strcmp(iConfigEditGet(file, "video", "height"), "480") == 0,
              "and one written with ragged spacing");
        check(strcmp(iConfigEditGet(file, "video", "mode"), "fullscreen") == 0,
              "and one with a trailing comment, without the comment");
        check(iConfigEditGet(file, "video", "fov") == NULL, "a key the file lacks reads as absent");
        check(iConfigEditGet(file, "pad", "a") == NULL, "and a commented-out one is not a value");

        iConfigEditSet(file, "video", "mode", "windowed");
        iConfigEditSet(file, "video", "width", "1920");
        iConfigEditSet(file, "video", "fov", "90");
        check(iConfigEditChanged(file), "setting three values marks it changed");
        check(iConfigEditSave(file, path), "and it saves");
        iConfigEditClose(file);
    }

    {
        char buf[2048];
        FILE* r = fopen(path, "rb");
        size_t n = (r != NULL) ? fread(buf, 1, sizeof(buf) - 1, r) : 0;
        if (r != NULL)
        {
            fclose(r);
        }
        buf[n] = '\0';

        check(strstr(buf, "mode = windowed") != NULL, "the changed value is in the file");
        check(strstr(buf, "; how the picture is presented") != NULL,
              "and its trailing comment survived");
        check(strstr(buf, "width=1920") != NULL,
              "and the one written without spaces, still without them");
        check(strstr(buf, "fov = 90") != NULL, "and the one the file did not have");
        check(strstr(buf, "; a settings file") != NULL, "the banner is untouched");
        check(strstr(buf, "# a hash comment") != NULL, "and a hash comment");
        check(strstr(buf, "[mystery]") != NULL, "and a section this build does not know");
        check(strstr(buf, "something = 3") != NULL, "with its value");
        check(strstr(buf, "; a  = x") != NULL, "and the commented-out binding, still commented");

        // Inserted into [video], not appended past [mystery] and [pad] where
        // it would read as a setting of theirs -- and where the game's parser
        // would call it pad.fov and report it as unknown.
        const char* fov = strstr(buf, "fov = 90");
        const char* mystery = strstr(buf, "[mystery]");
        check(fov != NULL && mystery != NULL && fov < mystery,
              "a new key lands in its own section, not at the end of the file");
    }

    // A key set into a section the file has no header for gets one.
    {
        iConfigEditFile* file = iConfigEditOpen(path);
        if (file != NULL)
        {
            iConfigEditSet(file, "audio", "soundtrack", "D:\\music");
            iConfigEditSave(file, path);
            iConfigEditClose(file);
        }

        char buf[2048];
        FILE* r = fopen(path, "rb");
        size_t n = (r != NULL) ? fread(buf, 1, sizeof(buf) - 1, r) : 0;
        if (r != NULL)
        {
            fclose(r);
        }
        buf[n] = '\0';

        const char* header = strstr(buf, "[audio]");
        check(header != NULL, "a section the file lacked gets a header");
        check(header != NULL && strstr(header, "soundtrack = D:\\music") != NULL,
              "with the value under it");
    }

    // And unsetting one removes the line, so the setting goes back to being
    // answered from the table.
    {
        iConfigEditFile* file = iConfigEditOpen(path);
        if (file != NULL)
        {
            iConfigEditUnset(file, "video", "fov");
            iConfigEditSave(file, path);
            iConfigEditClose(file);

            file = iConfigEditOpen(path);
            check(file != NULL && iConfigEditGet(file, "video", "fov") == NULL,
                  "unsetting a value takes its line out");
            iConfigEditClose(file);
        }
    }

    iHostRemoveFile(path);
    iHostRemoveDir(dir);
}

// The configurator's model: the settings as editable values, and the rules
// about which of them reach the file. Here rather than in bfbb_config, which
// is a window and cannot be run by ctest.
static void test_config_model()
{
    printf("config_model\n");

    char dir[512];
    if (!scratch_dir("configmodel", dir, sizeof(dir)))
    {
        check(false, "could not make a temp directory");
        return;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/config.ini", dir);

    static const char kFile[] = "[video]\nmode = windowed\n";

    FILE* w = fopen(path, "wb");
    if (w != NULL)
    {
        fwrite(kFile, 1, sizeof(kFile) - 1, w);
        fclose(w);
    }

    char why[1024];
    why[0] = '\0';

    if (!ConfigModelOpen(path, why, sizeof(why)))
    {
        check(false, "the model opens a file");
        iHostRemoveFile(path);
        iHostRemoveDir(dir);
        return;
    }

    check(ConfigModelSettingCount() == kConfigSettingCount, "it carries every setting");
    check(ConfigModelSectionCount() > 0, "and the table's sections");
    check(!ConfigModelDirty(), "and starts clean");

    // Two settings to work with: one the file names, and one it does not.
    S32 mode = -1;
    S32 absent = -1;
    for (S32 i = 0; i < ConfigModelSettingCount(); i++)
    {
        const iConfigSetting* s = ConfigModelSetting(i);
        if (strcmp(s->section, "video") == 0 && strcmp(s->name, "mode") == 0)
        {
            mode = i;
        }
        else if (absent < 0 && strcmp(s->section, "video") == 0 && s->kind == ICONFIG_INT)
        {
            absent = i;
        }
    }

    check(mode >= 0 && absent >= 0, "video.mode and an int beside it are both in the table");
    if (mode < 0 || absent < 0)
    {
        ConfigModelClose();
        iHostRemoveFile(path);
        iHostRemoveDir(dir);
        return;
    }

    check(strcmp(ConfigModelText(mode), "windowed") == 0, "a value the file names reads from it");
    check(strcmp(ConfigModelText(absent), ConfigModelSetting(absent)->value) == 0,
          "and one it does not reads the table's default");

    // Writing the same text back is not a change. This is what keeps a front
    // end that re-reads every control on every save from marking the whole
    // file dirty.
    ConfigModelSetText(mode, "windowed");
    check(!ConfigModelDirty(), "setting a value to what it already was changes nothing");

    ConfigModelSetText(mode, "fullscreen");
    check(ConfigModelDirty(), "and setting it to something else marks it dirty");

    check(ConfigModelSave(why, sizeof(why), NULL, NULL) == CONFIG_MODEL_OK, "it saves");
    check(!ConfigModelDirty(), "and is clean afterwards");

    // The mod folders: a list the mods page writes, and a Windows path with
    // spaces in it saves as typed.
    S32 mod = -1;
    for (S32 i = 0; i < ConfigModelSettingCount(); i++)
    {
        const iConfigSetting* s = ConfigModelSetting(i);
        if (strcmp(s->section, "assets") == 0 && strcmp(s->name, "mod") == 0)
        {
            mod = i;
        }
    }

    check(mod >= 0, "assets.mod is in the table");
    if (mod >= 0)
    {
        check(!ConfigModelWantsBrowse(ConfigModelSetting(mod)),
              "and is a list, not one folder with a Browse button");
        check(strcmp(ConfigModelSectionName(ConfigModelSectionOf(mod)), "assets") == 0,
              "in the assets section");

        ConfigModelSetText(mod, "C:\\mods\\BFBBMix Xbox");
        check(ConfigModelSave(why, sizeof(why), NULL, NULL) == CONFIG_MODEL_OK,
              "a mod folder with a space in it saves");
    }

    ConfigModelClose();

    {
        char buf[8192];
        FILE* r = fopen(path, "rb");
        size_t n = (r != NULL) ? fread(buf, 1, sizeof(buf) - 1, r) : 0;
        if (r != NULL)
        {
            fclose(r);
        }
        buf[n] = '\0';

        check(strstr(buf, "mode = fullscreen") != NULL, "the changed value is in the file");
        check(strstr(buf, "mod = C:\\mods\\BFBBMix Xbox") != NULL, "and so is the mod folder");

        // The rule the window used to own. A setting the file never mentioned
        // and that nobody touched is answered from the table, and writing it
        // out would pin it to today's default -- so it stays out.
        char key[128];
        snprintf(key, sizeof(key), "%s ", ConfigModelSetting(absent)->name);
        check(strstr(buf, key) == NULL, "and an untouched setting the file lacked is still absent");
    }

    // A value the table will not take stops the save and names itself.
    if (ConfigModelOpen(path, why, sizeof(why)))
    {
        ConfigModelSetText(absent, "twelve");

        S32 bad = -1;
        S32 section = -1;
        why[0] = '\0';

        check(ConfigModelSave(why, sizeof(why), &bad, &section) == CONFIG_MODEL_BAD_VALUE,
              "a value outside its domain refuses to save");
        check(bad == absent, "and says which setting");
        check(section == ConfigModelSectionOf(absent), "and which section it is in");
        check(why[0] != '\0', "and why");

        ConfigModelResetSection(section);
        check(ConfigModelIsDefault(absent), "resetting the section puts it back to the default");
        check(ConfigModelSave(why, sizeof(why), NULL, NULL) == CONFIG_MODEL_OK,
              "and then it saves");

        ConfigModelClose();
    }
    else
    {
        check(false, "the model reopens the file it wrote");
    }

    // Grouping. Every `group` in the table has to name a setting that is
    // really there, in the same section: one that does not resolves to -1 and
    // the setting quietly stops being folded where it was meant to be, which
    // is the kind of thing only a check notices.
    if (ConfigModelOpen(path, why, sizeof(why)))
    {
        S32 grouped = 0;
        S32 unresolved = 0;
        S32 strays = 0;
        S32 nested = 0;

        for (S32 i = 0; i < ConfigModelSettingCount(); i++)
        {
            const iConfigSetting* s = ConfigModelSetting(i);
            const S32 master = ConfigModelGroupOf(i);

            if (s->group == NULL)
            {
                if (master >= 0)
                {
                    strays++;
                }
                continue;
            }

            grouped++;

            if (master < 0)
            {
                printf("  ! %s.%s hangs off \"%s\", which is not in that section\n", s->section,
                       s->name, s->group);
                unresolved++;
                continue;
            }

            // A master that is itself a detail would be a group inside a
            // group, which nothing draws.
            if (ConfigModelGroupOf(master) >= 0)
            {
                nested++;
            }
        }

        check(grouped > 0, "some settings are grouped under another");
        check(unresolved == 0, "and every group names a setting in its own section");
        check(strays == 0, "an ungrouped setting reports no master");
        check(nested == 0, "and no group hangs off another group");

        ConfigModelClose();
    }
    else
    {
        check(false, "the model reopens for the grouping check");
    }

    // A file that is not there is written at the defaults, as the game does
    // it, rather than being an error.
    {
        char missing[512];
        snprintf(missing, sizeof(missing), "%s/fresh.ini", dir);

        why[0] = '\0';
        check(ConfigModelOpen(missing, why, sizeof(why)), "a missing file is created");
        check(iHostPathExists(missing), "and is on disk");
        ConfigModelClose();
        iHostRemoveFile(missing);
    }

    iHostRemoveFile(path);
    iHostRemoveDir(dir);
}

static S32 bind_row(const char* name)
{
    for (S32 i = 0; i < ConfigModelBindCount(); i++)
    {
        if (strcmp(ConfigModelBindName(i), name) == 0)
        {
            return i;
        }
    }
    return -1;
}

// The [keyboard] and [pad] pages: bindings read from the file, written back
// through the game's own parser, and an empty one taking its line out again.
static void test_config_model_bindings()
{
    printf("config_model bindings\n");

    char dir[512];
    if (!scratch_dir("configbind", dir, sizeof(dir)))
    {
        check(false, "could not make a temp directory");
        return;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/config.ini", dir);

    static const char kFile[] = "[keyboard]\na = space\n\n[pad]\nb = x\n";
    FILE* w = fopen(path, "wb");
    if (w != NULL)
    {
        fwrite(kFile, 1, sizeof(kFile) - 1, w);
        fclose(w);
    }

    char why[1024];
    why[0] = '\0';
    if (!ConfigModelOpen(path, why, sizeof(why)))
    {
        check(false, "the model opens a file with bindings");
        iHostRemoveFile(path);
        iHostRemoveDir(dir);
        return;
    }

    const S32 a = bind_row("a");
    const S32 b = bind_row("b");
    const S32 y = bind_row("y");
    check(a >= 0 && b >= 0 && y >= 0, "a, b and y are bindable");
    if (a < 0 || b < 0 || y < 0)
    {
        ConfigModelClose();
        iHostRemoveFile(path);
        iHostRemoveDir(dir);
        return;
    }

    check(strcmp(ConfigModelBindText(CONFIG_MODEL_KEYBOARD, a), "space") == 0,
          "a keyboard binding reads from the file");
    check(strcmp(ConfigModelBindText(CONFIG_MODEL_PAD, b), "x") == 0, "and a pad one");
    check(ConfigModelBindText(CONFIG_MODEL_PAD, a)[0] == '\0',
          "and one the file lacks reads empty, meaning the default");

    char def[256];
    ConfigModelBindDescribeDefault(CONFIG_MODEL_KEYBOARD, a, def, sizeof(def));
    check(strcmp(def, iPadBindFind("a")->key) == 0, "the keyboard default is the table's key");
    ConfigModelBindDescribeDefault(CONFIG_MODEL_PAD, a, def, sizeof(def));
    check(strstr(def, "printed A") != NULL, "and a face button's pad default names its letter");

    ConfigModelBindSetText(CONFIG_MODEL_KEYBOARD, a, "space, enter");
    ConfigModelBindSetText(CONFIG_MODEL_PAD, a, "lt+!rb");
    ConfigModelBindSetText(CONFIG_MODEL_PAD, b, "");
    check(ConfigModelDirty(), "changing a binding marks the model dirty");
    check(ConfigModelSave(why, sizeof(why), NULL, NULL) == CONFIG_MODEL_OK,
          "alternatives, chords and negation all save");
    ConfigModelClose();

    {
        char buf[8192];
        FILE* r = fopen(path, "rb");
        size_t n = (r != NULL) ? fread(buf, 1, sizeof(buf) - 1, r) : 0;
        if (r != NULL)
        {
            fclose(r);
        }
        buf[n] = '\0';

        const char* pad = strstr(buf, "[pad]");
        check(strstr(buf, "a = space, enter") != NULL, "the keyboard binding is in the file");
        check(pad != NULL && strstr(pad, "a = lt+!rb") != NULL, "the pad one is under [pad]");
        check(pad != NULL && strstr(pad, "b = ") == NULL,
              "and an emptied binding's line is gone");
    }

    if (ConfigModelOpen(path, why, sizeof(why)))
    {
        ConfigModelBindSetText(CONFIG_MODEL_KEYBOARD, y, "space+nonsense");

        S32 bad = -1;
        S32 section = -1;
        check(ConfigModelSave(why, sizeof(why), &bad, &section) == CONFIG_MODEL_BAD_VALUE,
              "a binding naming no key refuses to save");
        check(bad == y && section == ConfigModelSectionCount() + CONFIG_MODEL_KEYBOARD,
              "and says which button on which page");

        check(ConfigModelBindResetAll(CONFIG_MODEL_KEYBOARD), "resetting the page changes it");
        check(ConfigModelSave(why, sizeof(why), NULL, NULL) == CONFIG_MODEL_OK,
              "and then it saves");
        ConfigModelClose();
    }
    else
    {
        check(false, "the model reopens the file it wrote");
    }

    iHostRemoveFile(path);
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
    check(iScreenStretchX(1.0f) == 1280.0f && iScreenStretchY(1.0f) == 1024.0f, "5:4 fills it too");

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

    // config.ini's video.fov, which iCameraSetFOV adds to whatever the game
    // asked for. Zero at the game's own 75, so a config.ini that names the
    // default cannot move the camera.
    check(iScreenFOVOffset() == 0.0f, "no FOV offset until something sets one");
    iScreenSetFOV(75.0f);
    check(iScreenFOVOffset() == 0.0f, "and naming the game's own 75 is still none");
    iScreenSetFOV(90.0f);
    check(iScreenFOVOffset() == 15.0f, "a wider one is the difference from 75");
    iScreenSetFOV(0.0f);
    check(iScreenFOVOffset() == 15.0f, "a frustum that would go flat is refused");
    iScreenSetFOV(75.0f);
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

static void test_boot()
{
    printf("iBoot\n");

    check(iBootScene()[0] == '\0', "no boot scene until something sets one");
    check(iBootIntroMovies() == TRUE, "and the logos play");

    iBootSetScene("jf01");
    check(strcmp(iBootScene(), "jf01") == 0, "a four-character id is taken");

    // A scene id is four characters and nothing checks that again downstream:
    // zMainReadINI copies it into globals.sceneStart and zMainLoop packs the
    // first four bytes into a tag, so a short one would pack whatever follows
    // it in the buffer and a long one would silently lose its tail. Both then
    // reach xSTPreLoadScene, whose load loop has no exit and nobody to return
    // a failure to.
    iBootSetScene("jellyfish fields");
    check(iBootScene()[0] == '\0', "a name that is not an id is refused, not truncated");
    iBootSetScene("jf");
    check(iBootScene()[0] == '\0', "and so is a short one");
    iBootSetScene("");
    check(iBootScene()[0] == '\0', "empty means the menu");

    iBootSetIntroMovies(FALSE);
    check(iBootIntroMovies() == FALSE, "the logos can be turned off");
    iBootSetIntroMovies(TRUE);

    check(iBootCameraSensitivity() == 1.0f, "the camera runs at the game's own speed");
    iBootSetCameraSensitivity(2.0f);
    check(iBootCameraSensitivity() == 2.0f, "a multiplier is taken");

    // Zero is a camera the right stick cannot turn at all, which reads as a
    // dead controller rather than as a setting.
    iBootSetCameraSensitivity(0.0f);
    check(iBootCameraSensitivity() == 2.0f, "zero is refused");
    iBootSetCameraSensitivity(1.0f);
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
    check(strcmp(p.buf, "Please don't turn off your Xbox console.") == 0,
          "and the text is retail's");

    iTextPatchSetEnabled(TRUE);
    check(iTextPatchEnabled() != FALSE, "the switch reads back");

    // The word swap, on the string a player actually meets.
    patch_setup(&p, "Please don't turn off your Xbox console.", 64);
    check(patch_run(&p, "MNU4 AUTO SAVE TXT"), "on, the console's name goes");
    check(strcmp(p.buf, "Please don't turn off your computer.") == 0,
          "'your Xbox console' becomes 'your computer'");

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
    check(strcmp(p.buf, "Your PC doesn't have enough free blocks.") == 0,
          "'Xbox' alone becomes 'PC'");

    patch_setup(&p, "Memory card (8MB) (for PlayStation\xae\x32) is not inserted.", 96);
    patch_run(&p, "some other asset");
    check(strstr(p.buf, "PlayStation") == NULL, "and PlayStation goes with it");

    // The whole point of stepping over markup: what is inside the braces is a
    // lookup key. An asset called "xbox_button" that became "PC_button" would
    // resolve to nothing, and the failure would show up as a missing glyph
    // three menus away from this file.
    // A MOVE's prompt follows the preset, because that is the one thing the
    // three releases really changed: the GameCube spins on TRIANGLE and the
    // Xbox on O. The port ships the Xbox archives, so under the xbox preset
    // their numbering is already right and nothing moves; under the others the
    // prompt has to be pointed at the picture the move has moved to. Asking
    // iPadLayoutButton rather than hard-coding an arm keeps this true whichever
    // pad the machine running the tests happens to have plugged in.
    {
        // Spelled out because xPad.h is not in scope this far up the file.
        const U32 kTriangle = 0x80000u;
        const U32 kSquare = 0x40000u;

        const U32 spin = iPadLayoutButton(kTriangle);
        const char* digits = (spin == kTriangle) ? "03" : (spin == kSquare) ? "02" : "04";

        char want[64];
        snprintf(want, sizeof(want), "{i:button_picture_%s} Spin", digits);

        patch_setup(&p, "{i:button_picture_04} Spin", 64);
        patch_run(&p, "button_spinattack_text");
        check(strcmp(p.buf, want) == 0, "a move's prompt names the button the preset moved it to");
        check(strstr(p.buf, " Spin") != NULL, "and the word beside it is untouched either way");

        // The GameCube's font draws cancel with picture 02, where the Xbox's
        // draws the third choice. Its ui_cancel says so as it loads.
        patch_setup(&p, "{i:button_picture_02}", 64);
        patch_run(&p, "ui_cancel");
        check(iPadLayoutPictureBit(2) == kTriangle && iPadLayoutPictureBit(3) == kSquare,
              "a GameCube font's ui_cancel turns pictures 2 and 3 round");

        const char* gcDigits = (spin == kTriangle) ? "02" : (spin == kSquare) ? "03" : "04";
        snprintf(want, sizeof(want), "{i:button_picture_%s} Spin", gcDigits);
        patch_setup(&p, "{i:button_picture_04} Spin", 64);
        patch_run(&p, "button_spinattack_text");
        check(strcmp(p.buf, want) == 0, "and a move's prompt is numbered the GameCube's way");

        patch_setup(&p, "{i:button_picture_03}", 64);
        patch_run(&p, "ui_cancel");
        check(iPadLayoutPictureBit(2) == kSquare && iPadLayoutPictureBit(3) == kTriangle,
              "and an Xbox font's turns them back");
    }

    // A MENU prompt is not rewritten at all. All three discs cancel on the
    // button printed B, so the Xbox archives already name the right picture for
    // this executable, and rewriting them is how cancel and options ended up
    // drawing one glyph between them.
    patch_setup(&p, "{i:button_picture_03} Cancel", 64);
    patch_run(&p, "text_menu_cancel");
    check(strcmp(p.buf, "{i:button_picture_03} Cancel") == 0,
          "a menu prompt is left as the disc drew it");

    patch_setup(&p, "{i:button_picture_04} Options", 64);
    patch_run(&p, "ui_options");
    check(strcmp(p.buf, "{i:button_picture_04} Options") == 0,
          "and cancel and options keep the different pictures retail gave them");

    // Only for the asset it is listed against. The same markup in anything else
    // is somebody else's correct prompt and must be left alone.
    patch_setup(&p, "{i:button_picture_04}", 64);
    patch_run(&p, "some other asset");
    check(strcmp(p.buf, "{i:button_picture_04}") == 0,
          "and the same picture in another asset is not touched");

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
    check(strcmp(p.buf, "{i:ui_cancel} Quit to Desktop") == 0, "and replaces the whole string");

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

    // An override too long for its asset is grown into a copy instead: the
    // GameCube's strings are shorter than the Xbox's.
    {
        patch_setup(&p, "Reboot to Xbox Dashboard", 64);
        patch_run(&p, "text_menu_reboot");

        U8 asset[8] = { 2, 0, 0, 0, 'H', 'i', 0, 0 };
        U32 grownSize = 0;
        U8* grown = (U8*)iTextPatchGrown(iTextPatchAssetID("text_menu_reboot"), asset, 4,
                                         sizeof(asset), &grownSize);
        U32 len = 0;
        if (grown != NULL)
        {
            memcpy(&len, grown, sizeof(len));
        }
        check(grown != NULL && strcmp((const char*)grown + 4, p.buf) == 0 &&
                  grownSize == 4 + strlen(p.buf) + 1 && len == strlen(p.buf),
              "an override too long for its asset is written into a copy");
        U32 again = 0;
        check(iTextPatchGrown(iTextPatchAssetID("text_menu_reboot"), asset, 4, sizeof(asset),
                              &again) == grown,
              "and the copy is reused on the next load");

        U8 roomy[64] = { 0 };
        check(iTextPatchGrown(iTextPatchAssetID("text_menu_reboot"), roomy, 4, sizeof(roomy),
                              &again) == NULL,
              "an override that fits is left to iTextPatchAsset");
        check(iTextPatchGrown(iTextPatchAssetID("some other asset"), asset, 4, sizeof(asset),
                              &again) == NULL,
              "and an asset with no override gets no copy");
    }

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

    // The talk and exit prompts. The phrase goes at load and an include stands
    // in for it; what the include says is decided as it is drawn. This process
    // has no pad, so the keyboard is live and R1 is not a trigger.
    patch_setup(&p, "Pull the {i:button_exitlevel_text} to enter the Theater", 64);
    patch_run(&p, "a level's exit prompt");
    check(strcmp(p.buf, "{i:pc_say_1c} to enter the Theater") == 0,
          "the trigger phrase becomes a placeholder");
    patch_setup(&p, "Press the {insert:button_talktonpc_text} button to read", 64);
    patch_run(&p, "a sign");
    check(strcmp(p.buf, "{i:pc_say_4b} to read") == 0, "and so does the button form");

    const char* said = iTextPatchInsert(iTextPatchAssetID("pc_say_1c"));
    check(said != NULL && strcmp(said, "Press {i:button_exitlevel_text}") == 0,
          "off a trigger the placeholder says Press");
    said = iTextPatchInsert(iTextPatchAssetID("button_exitlevel_text"));
    check(said != NULL && strcmp(said, "{i:button_picture_05}") == 0,
          "and the include loses its Trigger");
    check(iTextPatchInsert(iTextPatchAssetID("button_jump_text")) == NULL,
          "an include that names no trigger draws its own asset");

    // Global, and later tests do not expect it on.
    iTextPatchSetEnabled(FALSE);
    said = iTextPatchInsert(iTextPatchAssetID("pc_say_1c"));
    check(said != NULL && strcmp(said, "Pull the {i:button_exitlevel_text}") == 0,
          "off, a placeholder says what shipped");
    check(iTextPatchInsert(iTextPatchAssetID("button_exitlevel_text")) == NULL,
          "and the include is the asset's own");
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

    // **A wrong-case DIRECTORY, which is the case that actually ships.** The
    // asset system asks for `GL/GL01.HIP` because a scene tag is spelled in
    // upper case, and an extraction has the folder as `gl`. Resolving only the
    // last component meant looking inside a directory that does not exist, so
    // the pack was invisible on any case-sensitive filesystem -- Android's, and
    // every Linux build.
    {
        char sub[512];
        snprintf(sub, sizeof(sub), "%s/gl", dir);
        if (iHostMakeDir(sub))
        {
            snprintf(path, sizeof(path), "%s/gl01.hip", sub);
            FILE* g = fopen(path, "wb");
            fwrite(payload, 1, sizeof(payload) - 1, g);
            fclose(g);

            tag_xFile nested = {};
            check(iFileOpen("GL/GL01.HIP", 0, &nested) == 0,
                  "a wrong-case directory resolves too, not just the leaf");
            iFileClose(&nested);

            tag_xFile nomatch = {};
            check(iFileOpen("GL/NOSUCH.HIP", 0, &nomatch) != 0,
                  "and a missing file under it still reports failure");

            tag_xFile nodir = {};
            check(iFileOpen("ZZ/GL01.HIP", 0, &nodir) != 0,
                  "as does a directory that is not there under any spelling");
        }
    }

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
    check(memcmp(again, payload, 16) == 0,
          "and returns the same bytes, the offset not having moved");

    // The callback's seek is what advances it.
    iFileSeek(&afile, 16, IFILE_SEEK_CUR);
    check(afile.ps.offset == 16, "seeking forward is what advances the position");

    iFileClose(&afile);
    iFileExit();

    iHostRemoveFile(path);
    iHostRemoveDir(dir);
}

// Writes `size` bytes to dir/name. Returns false when the file cannot be made.
static bool write_scratch(const char* dir, const char* name, const void* data, size_t size)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* f = fopen(path, "wb");
    if (f == NULL)
    {
        return false;
    }
    fwrite(data, 1, size, f);
    fclose(f);
    return true;
}

// The first `size` bytes of `name` through iFileOpen, or "" when it will not open.
static void read_through_ifile(const char* name, char* out, size_t size)
{
    memset(out, 0, size);
    tag_xFile file = {};
    if (iFileOpen(name, 0, &file) == 0)
    {
        iFileRead(&file, out, (U32)size - 1);
        iFileClose(&file);
    }
}

// The mod folder: a file there is read in place of the asset root's file at
// the same relative path, whatever the case, and a package from another
// console's release is passed over for the original.
static void putBE32(U8* p, U32 v)
{
    p[0] = (U8)(v >> 24);
    p[1] = (U8)(v >> 16);
    p[2] = (U8)(v >> 8);
    p[3] = (U8)v;
}

static void putBE16(U8* p, U32 v)
{
    p[0] = (U8)(v >> 8);
    p[1] = (U8)v;
}

static U32 getLE32(const U8* p)
{
    return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24);
}

static U32 getLE16(const U8* p)
{
    return (U32)p[0] | ((U32)p[1] << 8);
}

// GameCube writes through eEventGivePowerUp's unchecked index. The addresses
// are the retail GameCube's: globals is at 0x803C0558, its camera.mat.right.x
// at 0x803C056C, and player.g.PowerUp at 0x803C0F15. zMain.cpp defines globals
// in the game.
zGlobals globals;

static void test_gcmem()
{
    printf("iGCMem\n");

    const U32 kPowerUp = 0x803C0F15;
    const U32 kRightX = 0x803C056C;
    U8* rightx = (U8*)&globals.camera.mat.right.x;

    check(iGCMemAddress(kPowerUp) == &globals.player.g.PowerUp[0] &&
              iGCMemAddress(kPowerUp + 1) == &globals.player.g.PowerUp[1],
          "PowerUp is where the GameCube has it");
    check(iGCMemAddress(kRightX) == rightx + 3 && iGCMemAddress(kRightX + 3) == rightx,
          "a float's bytes come back in the PC's order");
    check(iGCMemAddress(0x803C0560) == NULL, "a pointer is not mapped");
    check(iGCMemAddress(0x80000000) == NULL && iGCMemAddress(0x803C0558 + 0x1FC8) == NULL,
          "nor is anything outside globals");

    xLinkAsset link;
    memset(&link, 0, sizeof(link));
    link.dstEvent = eEventGivePowerUp;
    link.param[0] = 1.0f;
    iGCMemLink(&link);
    check(link.dstEvent == eEventGivePowerUp && link.param[0] == 1.0f,
          "a link that gives a power-up is kept as it is");

    link.param[0] = (F32)(S32)(kRightX - kPowerUp);
    iGCMemLink(&link);
    check(link.dstEvent == eEventGivePowerUp &&
              &globals.player.g.PowerUp[(S32)link.param[0]] == rightx + 3,
          "a link that writes a float's first byte writes the PC's last byte of it");

    link.param[0] = (F32)(S32)(0x803C0560 - kPowerUp);
    iGCMemLink(&link);
    check(link.dstEvent == eEventUnknown, "a link that writes a pointer is dropped");

    link.dstEvent = eEventGivePowerUp;
    link.param[0] = -1e9f;
    iGCMemLink(&link);
    check(link.dstEvent == eEventUnknown, "and so is one that writes outside globals");

    // BFBB Deluxe's DOL patch reads two bytes of .sbss padding, which its hb00
    // sets with indices 43487 and 43488.
    check(!iModPatchSpinHover() && !iModPatchBowlFullTurn(), "the Deluxe patch starts off");
    check(iGCMemAddress(0x803CB8F4) != NULL && iGCMemAddress(0x803CB8F8) == NULL,
          "its four bytes of padding are mapped, and nothing past them");

    link.dstEvent = eEventGivePowerUp;
    link.param[0] = 43487.0f;
    iGCMemLink(&link);
    globals.player.g.PowerUp[(S32)link.param[0]] = 1;
    check(link.dstEvent == eEventGivePowerUp && iModPatchSpinHover() && !iModPatchBowlFullTurn(),
          "Deluxe's first write turns on the spin hover");

    link.param[0] = 43488.0f;
    iGCMemLink(&link);
    globals.player.g.PowerUp[(S32)link.param[0]] = 1;
    check(iModPatchBowlFullTurn(), "and its second the full bowl turn");
    *iGCMemAddress(0x803CB8F4) = 0;
    *iGCMemAddress(0x803CB8F5) = 0;
}

// GameCube assets turned around in place. The whole-disc check is
// tools/gcswap/check.py, against the Xbox copy of every asset; these are the
// shapes it depends on, built by hand.
static void test_gcswap()
{
    printf("iGCSwap\n");

    // A TIMR -- xTimerAsset, seconds and randomRange -- with one link after
    // it. baseFlags is 16-bit and linkCount a byte; the link's two event
    // numbers are 16-bit too.
    U8 timr[0x10 + 0x20];
    memset(timr, 0, sizeof(timr));
    putBE32(timr + 0x0, 0x11223344); // id
    timr[4] = 0x0D; // baseType
    timr[5] = 1; // linkCount
    putBE16(timr + 0x6, 0x0102); // baseFlags
    putBE32(timr + 0x8, 0x41100000); // seconds, 9.0
    putBE32(timr + 0xC, 0x3F000000); // randomRange, 0.5
    putBE16(timr + 0x10, 0x0203); // srcEvent
    putBE16(timr + 0x12, 0x0405); // dstEvent
    putBE32(timr + 0x14, 0xAABBCCDD); // dstAssetID

    check(iGCSwapKnowsType('TIMR'), "iGCSwap has a rule for TIMR");
    check(iGCSwapAsset('TIMR', timr, sizeof(timr)) == IGCSWAP_DONE, "a TIMR swaps");
    check(getLE32(timr + 0x0) == 0x11223344 && getLE16(timr + 0x6) == 0x0102,
          "its id and 16-bit baseFlags come out little-endian");
    check(timr[4] == 0x0D && timr[5] == 1, "and the bytes stay where they were");
    check(getLE32(timr + 0x8) == 0x41100000 && getLE32(timr + 0xC) == 0x3F000000,
          "and so do both floats");
    check(getLE16(timr + 0x10) == 0x0203 && getLE16(timr + 0x12) == 0x0405 &&
              getLE32(timr + 0x14) == 0xAABBCCDD,
          "the link at the end is swapped field by field");

    // randomRange is little-endian on the disc in some GameCube timers: the
    // packer never swapped it. A value that only makes sense as is stays.
    memset(timr, 0, sizeof(timr));
    putBE32(timr + 0x8, 0x41100000);
    timr[0xC] = 0x00; // 0.5, little-endian
    timr[0xD] = 0x00;
    timr[0xE] = 0x00;
    timr[0xF] = 0x3F;
    iGCSwapAsset('TIMR', timr, 0x10);
    check(getLE32(timr + 0xC) == 0x3F000000, "a randomRange left little-endian is kept");

    // An older, shorter TIMR without randomRange: what is there is swapped.
    memset(timr, 0, sizeof(timr));
    putBE32(timr + 0x8, 0x41100000);
    check(iGCSwapAsset('TIMR', timr, 0xC) == IGCSWAP_DONE && getLE32(timr + 0x8) == 0x41100000,
          "an asset older and shorter than its struct swaps the fields it has");

    // An SKB1 animation: header, one key of eight 16-bit fields, two times,
    // then 16-bit offsets.
    U8 anim[0x1C + 0x10 + 8 + 4];
    memset(anim, 0, sizeof(anim));
    putBE32(anim + 0x0, 0x534B4231); // 'SKB1'
    putBE16(anim + 0x8, 1); // BoneCount
    putBE16(anim + 0xA, 2); // TimeCount
    putBE32(anim + 0xC, 1); // KeyCount
    putBE32(anim + 0x10, 0x3F800000);
    for (U32 k = 0; k < 8; k++)
    {
        putBE16(anim + 0x1C + k * 2, 0x0100 + k);
    }
    putBE32(anim + 0x2C, 0x3F800000);
    putBE32(anim + 0x30, 0x40000000);
    putBE16(anim + 0x34, 0x0007);
    putBE16(anim + 0x36, 0x0009);

    check(iGCSwapAsset('ANIM', anim, sizeof(anim)) == IGCSWAP_DONE, "an SKB1 animation swaps");
    check(getLE32(anim) == 0x534B4231 && getLE16(anim + 0x8) == 1 && getLE16(anim + 0xA) == 2 &&
              getLE32(anim + 0xC) == 1,
          "its header's counts are 16- and 32-bit where they should be");
    check(getLE16(anim + 0x1C) == 0x0100 && getLE16(anim + 0x2A) == 0x0107,
          "every key field is 16-bit");
    check(getLE32(anim + 0x2C) == 0x3F800000 && getLE32(anim + 0x30) == 0x40000000,
          "the times are floats");
    check(getLE16(anim + 0x34) == 7 && getLE16(anim + 0x36) == 9, "and the offsets 16-bit");

    U8 junk[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    check(iGCSwapAsset('ZZZZ', junk, sizeof(junk)) == IGCSWAP_UNKNOWN && junk[0] == 1,
          "a type with no rule is left as it was");
    check(iGCSwapAsset('ANIM', junk, sizeof(junk)) == IGCSWAP_MALFORMED,
          "and an animation too short for its header says so");

    // SNDI, the GameCube sound table: num_sfx, total_size, num_streams and
    // num_cutscene, then 0x64-byte entries -- a DSPADPCM header and an id.
    U8 sndi[0x10 + 2 * 0x64];
    memset(sndi, 0, sizeof(sndi));
    putBE32(sndi + 0x0, 1);
    putBE32(sndi + 0x4, 0xCDCDCDCD);
    putBE32(sndi + 0x8, 1);
    putBE32(sndi + 0xC, 0);
    U8* e = sndi + 0x10;
    putBE32(e + 0x00, 3200); // num_samples
    putBE32(e + 0x04, 3660); // num_nibbles
    putBE32(e + 0x08, 22050); // sample_rate
    putBE16(e + 0x0C, 1); // loop_flag
    putBE32(e + 0x10, 2); // loop_start
    putBE16(e + 0x1C, 0x0800); // coef[0]
    putBE16(e + 0x3A, 0xFF00); // coef[15]
    putBE16(e + 0x3E, 0x0011); // pred_scale
    putBE16(e + 0x40, 0x1234); // yn1
    putBE32(e + 0x60, 0xAABBCCDD);
    putBE32(e + 0x64 + 0x60, 0x01020304);

    check(iGCSwapKnowsType('SNDI'), "iGCSwap has a rule for SNDI");
    check(iGCSwapAsset('SNDI', sndi, sizeof(sndi)) == IGCSWAP_DONE, "a GameCube SNDI swaps");
    check(getLE32(sndi + 0x0) == 1 && getLE32(sndi + 0x8) == 1 && getLE32(sndi + 0xC) == 0,
          "its counts come out little-endian");
    check(getLE32(sndi + 0x4) == IGCSWAP_SNDI_GAMECUBE, "and total_size is stamped as GameCube");
    check(getLE32(e + 0x00) == 3200 && getLE32(e + 0x04) == 3660 && getLE32(e + 0x08) == 22050 &&
              getLE16(e + 0x0C) == 1 && getLE32(e + 0x10) == 2,
          "an entry's lengths, rate and loop fields swap at their widths");
    check(getLE16(e + 0x1C) == 0x0800 && getLE16(e + 0x3A) == 0xFF00 && getLE16(e + 0x3E) == 0x11 &&
              getLE16(e + 0x40) == 0x1234,
          "and its coefficients and context as 16-bit");
    check(getLE32(e + 0x60) == 0xAABBCCDD && getLE32(e + 0x64 + 0x60) == 0x01020304,
          "and every entry's asset id");

    memset(sndi, 0, sizeof(sndi));
    putBE32(sndi + 0x0, 3);
    check(iGCSwapAsset('SNDI', sndi, sizeof(sndi)) == IGCSWAP_MALFORMED,
          "a table counting more entries than it holds says so");
}

static void test_file_mod()
{
    printf("iFile mod folder\n");

    char base[512];
    char mod[512];
    if (!scratch_dir("modbase", base, sizeof(base)) || !scratch_dir("modover", mod, sizeof(mod)))
    {
        check(false, "could not make the temp directories");
        return;
    }

    char baseGl[512];
    char modGl[512];
    snprintf(baseGl, sizeof(baseGl), "%s/gl", base);
    snprintf(modGl, sizeof(modGl), "%s/GL", mod);
    iHostMakeDir(baseGl);
    iHostMakeDir(modGl);

    // HIPA > PACK > PLAT "GC\0GameCube\0", sizes big-endian as the packer
    // writes them on every platform.
    static const U8 gcHip[] = { 'H', 'I', 'P', 'A', 0, 0, 0, 0,  'P', 'A', 'C', 'K', 0, 0, 0, 20,
                                'P', 'L', 'A', 'T', 0, 0, 0, 12, 'G', 'C', 0,   'G', 'a', 'm',
                                'e', 'C', 'u', 'b', 'e', 0 };
    // The same with PLAT "P2\0PS2\0", a release the port does not read.
    static const U8 ps2Hip[] = { 'H', 'I', 'P', 'A', 0,  0,   0,   0,   'P', 'A', 'C',
                                 'K', 0,   0,   0,   16, 'P', 'L', 'A', 'T', 0,   0,
                                 0,   8,   'P', '2', 0,  'P', 'S', '2', 0,   0 };

    bool made =
        write_scratch(baseGl, "gl01.HIP", "BASE", 4) &&
        write_scratch(base, "ONLY.HIP", "BASE", 4) && write_scratch(base, "GC.HIP", "BASE", 4) &&
        write_scratch(base, "GC.HOP", "BASE", 4) && write_scratch(base, "P2.HOP", "BASE", 4) &&
        write_scratch(modGl, "GL01.hip", "MODDED", 6) &&
        write_scratch(mod, "gc.hip", gcHip, sizeof(gcHip)) &&
        write_scratch(mod, "gc.hop", gcHip, sizeof(gcHip)) &&
        write_scratch(mod, "p2.hop", ps2Hip, sizeof(ps2Hip));
    check(made, "the scratch asset and mod folders can be written");

    iHostSetEnv("BFBB_ASSETS", base);
    iHostSetEnv("BFBB_MOD", mod);
    iFileInit();
    iFileSetPath((char*)"."); // what SB.INI's PATH= does once it loads

    char got[16];
    read_through_ifile("GL/GL01.HIP", got, sizeof(got));
    check(strcmp(got, "MODDED") == 0, "a mod file replaces the original at the same relative path");
    check(strncmp(iFileModName(), "bfbb_pc_modover_", 16) == 0,
          "the mod's name is its folder's last component, for its own saves");

    read_through_ifile("ONLY.HIP", got, sizeof(got));
    check(strcmp(got, "BASE") == 0, "a file the mod does not have comes from the asset root");

    // GameCube packages are read; a PS2 one would load and then crash.
    read_through_ifile("GC.HIP", got, sizeof(got));
    check(memcmp(got, "HIPA", 4) == 0, "a GameCube .HIP in the mod folder is read");
    read_through_ifile("GC.HOP", got, sizeof(got));
    check(memcmp(got, "HIPA", 4) == 0, "and a GameCube .HOP");
    read_through_ifile("P2.HOP", got, sizeof(got));
    check(strcmp(got, "BASE") == 0, "but a PS2 package is passed over");

    char loaded[16];
    char path[512];
    U32* buf = iFileLoad((char*)"GL/GL01.HIP", NULL, NULL);
    memcpy(loaded, buf != NULL ? (const char*)buf : "", 7);
    loaded[6] = 0;
    free(buf);
    check(strcmp(loaded, "MODDED") == 0, "iFileLoad's absolute path is overridden too");

    // Two mods, the first listed wins. The top one's copy of TWO.HOP is a
    // PS2 package, so the one below supplies it.
    char top[512];
    char topGl[512];
    if (scratch_dir("modtop", top, sizeof(top)))
    {
        snprintf(topGl, sizeof(topGl), "%s/gl", top);
        iHostMakeDir(topGl);
        write_scratch(topGl, "gl01.HIP", "TOP", 3);
        write_scratch(top, "two.hop", ps2Hip, sizeof(ps2Hip));
        write_scratch(mod, "TWO.HOP", "SECOND", 6);

        snprintf(path, sizeof(path), "%s | %s", top, mod);
        iHostSetEnv("BFBB_MOD", path);
        iFileInit();
        iFileSetPath((char*)".");

        read_through_ifile("GL/GL01.HIP", got, sizeof(got));
        check(strcmp(got, "TOP") == 0, "with two mods, the first listed supplies a file both have");
        read_through_ifile("TWO.HOP", got, sizeof(got));
        check(strcmp(got, "SECOND") == 0, "and a PS2 package in it falls to the next mod");
        read_through_ifile("ONLY.HIP", got, sizeof(got));
        check(strcmp(got, "BASE") == 0, "and a file neither has comes from the asset root");
        check(iFileModCount() == 2 && strncmp(iFileModName(), "bfbb_pc_modtop_", 15) == 0 &&
                  strstr(iFileModName(), "+bfbb_pc_modover_") != NULL,
              "their saves are named for both, in order");

        snprintf(path, sizeof(path), "%s/gl01.HIP", topGl);
        iHostRemoveFile(path);
        snprintf(path, sizeof(path), "%s/two.hop", top);
        iHostRemoveFile(path);
        snprintf(path, sizeof(path), "%s/TWO.HOP", mod);
        iHostRemoveFile(path);
        iHostRemoveDir(topGl);
        iHostRemoveDir(top);
    }

    // A mod folder that is not there is ignored rather than breaking the root.
    snprintf(path, sizeof(path), "%s/nosuch", mod);
    iHostSetEnv("BFBB_MOD", path);
    iFileInit();
    read_through_ifile("GL/GL01.HIP", got, sizeof(got));
    check(strcmp(got, "BASE") == 0, "a missing mod folder leaves the asset root in force");
    check(iFileModName()[0] == '\0', "and names no mod, so saves stay where the disc's are");

    iHostSetEnv("BFBB_ASSETS", NULL);
    iHostSetEnv("BFBB_MOD", NULL);
    iFileInit();
    iFileExit();

    const char* files[][2] = { { baseGl, "gl01.HIP" }, { base, "ONLY.HIP" }, { base, "GC.HIP" },
                               { base, "GC.HOP" },     { base, "P2.HOP" },   { modGl, "GL01.hip" },
                               { mod, "gc.hip" },      { mod, "gc.hop" },    { mod, "p2.hop" } };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++)
    {
        snprintf(path, sizeof(path), "%s/%s", files[i][0], files[i][1]);
        iHostRemoveFile(path);
    }
    iHostRemoveDir(baseGl);
    iHostRemoveDir(modGl);
    iHostRemoveDir(base);
    iHostRemoveDir(mod);
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

// Mod folders as the game and bfbb_config both find them: the setting, the
// mod.ini, and the asset root inside an unpacked archive.
// The Xbox archives' talk boxes need O and TRIANGLE transposed; the
// GameCube's are read as they are.
static void test_talkbox_buttons()
{
    printf("iPadTalkBoxButtons\n");

    const U32 kO = 0x20000u;
    const U32 kTriangle = 0x80000u;

    iPadLayoutSetCancelPicture(3);
    check(iPadTalkBoxButtons(kO) == kTriangle && iPadTalkBoxButtons(kTriangle) == kO,
          "with the Xbox's data, O and TRIANGLE trade places");

    iPadLayoutSetCancelPicture(2);
    check(iPadTalkBoxButtons(kO) == kO && iPadTalkBoxButtons(kTriangle) == kTriangle,
          "with the GameCube's, they reach the talk box as pressed");

    iPadLayoutSetCancelPicture(3);
}

static void test_mod_folder()
{
    printf("iModFolder\n");

    char dir[512];
    if (!scratch_dir("modfolder", dir, sizeof(dir)))
    {
        check(false, "could not make a temp directory");
        return;
    }

    char got[512];
    char typed[512];
    snprintf(typed, sizeof(typed), "%s/", dir);
    check(iModFolderResolve(typed, got, sizeof(got)) && strcmp(got, dir) == 0,
          "a folder resolves to itself, without its trailing slash");

    snprintf(typed, sizeof(typed), "%s", dir);
    for (char* c = typed; *c != '\0'; c++)
    {
        if (*c == '/')
        {
            *c = '\\';
        }
    }
    check(iModFolderResolve(typed, got, sizeof(got)) && strcmp(got, dir) == 0,
          "and so does the same folder typed with backslashes");

    snprintf(typed, sizeof(typed), "%s/nosuch", dir);
    check(!iModFolderResolve(typed, got, sizeof(got)), "a folder that is not there does not");
    check(!iModFolderResolve("", got, sizeof(got)), "nor does an empty setting");

    iModFolderPath listed[kModFolderMax];
    check(iModFolderSplit(" mods/A |mods/B|| C:\\x y ", listed, kModFolderMax) == 3 &&
              strcmp(listed[0], "mods/A") == 0 && strcmp(listed[1], "mods/B") == 0 &&
              strcmp(listed[2], "C:\\x y") == 0,
          "a list of mods splits on '|', trimmed, empty entries dropped");
    check(iModFolderSplit("", listed, kModFolderMax) == 0, "and an empty setting is no mods");

    char saveName[256];
    iModFolderSaveName(listed, 2, saveName, sizeof(saveName));
    check(strcmp(saveName, "A+B") == 0, "several mods keep their saves under their names joined");
    iModFolderSaveName(listed, 1, saveName, sizeof(saveName));
    check(strcmp(saveName, "A") == 0, "and one mod's saves keep the folder they always had");

    char mod[512];
    snprintf(mod, sizeof(mod), "%s/Cool Mod", dir);
    iHostMakeDir(mod);

    iModInfo info;
    iModFolderInfo(mod, &info);
    check(!info.hasIni && strcmp(info.name, "Cool Mod") == 0,
          "a mod without a mod.ini is named for its folder");

    static const char kIni[] = "[mod]\n"
                               "name = The Cool Mod\n"
                               "version = 1.2\n"
                               "author = someone ; a comment\n"
                               "[Video]\n"
                               "FOV = 75\n"
                               "[xbox]\n"
                               "glow = off\n";
    write_scratch(mod, "mod.ini", kIni, sizeof(kIni) - 1);

    iModFolderInfo(mod, &info);
    check(info.hasIni && strcmp(info.name, "The Cool Mod") == 0 && strcmp(info.version, "1.2") == 0,
          "mod.ini's [mod] section names it and gives its version");
    check(strcmp(info.author, "someone") == 0, "and its author, without the comment");

    struct Seen
    {
        char keys[4][64];
        char values[4][64];
        S32 count;
    } seen = {};
    struct Collect
    {
        static void fn(const char* key, const char* value, void* context)
        {
            Seen* s = (Seen*)context;
            if (s->count < 4)
            {
                snprintf(s->keys[s->count], sizeof(s->keys[0]), "%s", key);
                snprintf(s->values[s->count], sizeof(s->values[0]), "%s", value);
                s->count++;
            }
        }
    };
    const S32 n = iModFolderSettings(mod, Collect::fn, &seen);
    check(n == 2 && seen.count == 2, "every other section is settings, [mod] not among them");
    check(strcmp(seen.keys[0], "video.fov") == 0 && strcmp(seen.values[0], "75") == 0,
          "named section.name, folded to lower case");
    check(strcmp(seen.keys[1], "xbox.glow") == 0 && strcmp(seen.values[1], "off") == 0,
          "in file order");

    // An archive's own folder around the mod, and a readme beside it.
    char pack[512];
    char packRoot[512];
    char packHb[512];
    snprintf(pack, sizeof(pack), "%s/pack", dir);
    snprintf(packRoot, sizeof(packRoot), "%s/BFBBMix", pack);
    snprintf(packHb, sizeof(packHb), "%s/hb", packRoot);
    iHostMakeDir(pack);
    iHostMakeDir(packRoot);
    iHostMakeDir(packHb);
    write_scratch(pack, "readme.txt", "x", 1);
    write_scratch(packRoot, "boot.HIP", "x", 1);
    write_scratch(packHb, "hb01.HIP", "x", 1);
    check(iModFolderFindAssets(pack, got, sizeof(got)) && strcmp(got, packRoot) == 0,
          "an archive's asset root is the folder holding its packages");

    // Only level packages: the root is above their level folder.
    char levels[512];
    char levelsHb[512];
    snprintf(levels, sizeof(levels), "%s/levels", dir);
    snprintf(levelsHb, sizeof(levelsHb), "%s/HB", levels);
    iHostMakeDir(levels);
    iHostMakeDir(levelsHb);
    write_scratch(levelsHb, "hb01.hip", "x", 1);
    check(iModFolderFindAssets(levels, got, sizeof(got)) && strcmp(got, levels) == 0,
          "a package in its level folder puts the root above that folder");

    char empty[512];
    snprintf(empty, sizeof(empty), "%s/empty", dir);
    iHostMakeDir(empty);
    write_scratch(empty, "notes.txt", "x", 1);
    check(!iModFolderFindAssets(empty, got, sizeof(got)), "a folder with no packages is no mod");

    check(ModsModelRemoveTree(dir) && !iHostPathExists(dir),
          "a folder and everything under it can be deleted");
}

// Installing, picking and removing a mod in the library beside the
// executable. Under a name nobody would give a mod, and the library folder is
// removed afterwards only if this left it empty.
static void test_mods_model()
{
    printf("mods_model\n");

    char dir[512];
    if (!scratch_dir("modsmodel", dir, sizeof(dir)))
    {
        check(false, "could not make a temp directory");
        return;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/config.ini", dir);
    write_scratch(dir, "config.ini", "[assets]\nmod =\n", 15);

    char why[512];
    if (!ConfigModelOpen(path, why, sizeof(why)))
    {
        check(false, "the model opens a file");
        ModsModelRemoveTree(dir);
        return;
    }

    const bool hadLibrary = iHostPathExists(ModsModelLibrary());

    char staged[512];
    check(ModsModelStage(staged, sizeof(staged), why, sizeof(why)), "a staging folder is made");

    // An archive's own folder around the mod, as a zip unpacks.
    char inner[600];
    char innerHb[600];
    snprintf(inner, sizeof(inner), "%s/Selftest Mod 5.0", staged);
    snprintf(innerHb, sizeof(innerHb), "%s/hb", inner);
    iHostMakeDir(inner);
    iHostMakeDir(innerHb);
    write_scratch(inner, "boot.HIP", "x", 1);
    write_scratch(innerHb, "hb01.HIP", "x", 1);
    write_scratch(staged, "readme.txt", "x", 1);

    char name[256];
    ModsModelSuggestName(staged, "C:/Downloads/thing.zip", name, sizeof(name));
    check(strcmp(name, "Selftest Mod 5.0") == 0, "the suggested name is the mod's own folder");

    // A mod-manager layout: the disc's files in files/, beside mod.json.
    char managed[512];
    char managedFiles[600];
    check(ModsModelStage(managed, sizeof(managed), why, sizeof(why)), "a second staging folder");
    snprintf(managedFiles, sizeof(managedFiles), "%s/files", managed);
    iHostMakeDir(managedFiles);
    write_scratch(managedFiles, "boot.HIP", "x", 1);
    write_scratch(managed, "mod.json", "{}", 2);
    ModsModelSuggestName(managed, "C:/Downloads/Second Strike.zip", name, sizeof(name));
    check(strcmp(name, "Second Strike") == 0, "files/ at the archive's root takes the archive's name");

    ModsModelRemoveTree(managedFiles);
    char wrapped[600];
    char wrappedFiles[700];
    snprintf(wrapped, sizeof(wrapped), "%s/Boo-kini v1.1", managed);
    snprintf(wrappedFiles, sizeof(wrappedFiles), "%s/files", wrapped);
    iHostMakeDir(wrapped);
    iHostMakeDir(wrappedFiles);
    write_scratch(wrappedFiles, "boot.HIP", "x", 1);
    ModsModelSuggestName(managed, "C:/Downloads/boo.zip", name, sizeof(name));
    check(strcmp(name, "Boo-kini v1.1") == 0, "and files/ inside a folder takes that folder's name");
    ModsModelRemoveTree(managed);

    check(!ModsModelNameOk("a/b", why, sizeof(why)) && !ModsModelNameOk(".x", why, sizeof(why)),
          "a name with a separator or a leading dot is refused");

    char unique[64];
    snprintf(unique, sizeof(unique), "bfbb selftest %llu",
             (unsigned long long)(iHostMonotonicNs() % 1000000000ULL));

    S32 index = -1;
    check(ModsModelInstall(staged, unique, &index, why, sizeof(why)) && index >= 0,
          "it installs into the library under the name given");
    check(!iHostPathExists(staged), "and the staging folder is gone, readme and all");

    if (index >= 0)
    {
        const ModsModelEntry* e = ModsModelAt(index);
        char boot[700];
        snprintf(boot, sizeof(boot), "%s/boot.HIP", e->folder);
        check(e->inLibrary && iHostPathExists(boot), "the asset root is the mod's folder");

        char want[300];
        snprintf(want, sizeof(want), "mods/%s", unique);
        check(strcmp(e->setting, want) == 0,
              "and [assets] mod names it relative to the executable");

        const S32 setting = ConfigModelFind("assets", "mod");

        check(ModsModelRankOf(index) == -1 && ModsModelEnabledCount() == 0,
              "nothing is on until it is turned on");
        ModsModelSetEnabled(index, true);
        check(ModsModelRankOf(index) == 0 && ConfigModelDirty() &&
                  strcmp(ConfigModelText(setting), want) == 0,
              "turning it on sets [assets] mod");

        // A second mod, outside the library, with one file the first also has.
        char other[600];
        snprintf(other, sizeof(other), "%s/other", dir);
        iHostMakeDir(other);
        write_scratch(other, "BOOT.hip", "y", 1);
        write_scratch(other, "font.HIP", "y", 1);

        ModsModelUseFolder(other);
        check(ModsModelEnabledCount() == 2 && ModsModelRankOf(index) == 1,
              "a folder turned on goes to the top of the load order");

        char list[700];
        snprintf(list, sizeof(list), "%s | %s", other, want);
        check(strcmp(ConfigModelText(setting), list) == 0,
              "and [assets] mod lists both, top first");

        const S32 top = ModsModelEnabledAt(0);
        char examples[256];
        check(ModsModelOverlap(index, top, examples, sizeof(examples)) == 1 &&
                  strcmp(examples, "boot.hip") == 0,
              "the files two mods share are found whatever their case");

        ModsModelMove(index, -1);
        check(ModsModelRankOf(index) == 0 && ModsModelRankOf(top) == 1, "a mod can be moved up");

        // A folder the setting names that is not there is listed, and can be
        // turned off to take it out.
        char withMissing[800];
        snprintf(withMissing, sizeof(withMissing), "%s | nowhere/at all", ConfigModelText(setting));
        ConfigModelSetText(setting, withMissing);
        ModsModelScan();
        const S32 missing = ModsModelEnabledAt(2);
        check(ModsModelEnabledCount() == 3 && ModsModelAt(missing)->missing,
              "a mod folder that is not there is listed as missing");
        ModsModelSetEnabled(missing, false);
        check(strstr(ConfigModelText(setting), "nowhere") == NULL, "and turning it off drops it");

        check(ModsModelRemove(index, why, sizeof(why)), "it can be removed");
        check(strcmp(ConfigModelText(setting), other) == 0, "which takes it out of the load order");
    }

    if (!hadLibrary)
    {
        iHostRemoveDir(ModsModelLibrary());
    }

    ConfigModelClose();
    ModsModelRemoveTree(dir);
}

static void put32(U8* p, U32 v)
{
    p[0] = (U8)v;
    p[1] = (U8)(v >> 8);
    p[2] = (U8)(v >> 16);
    p[3] = (U8)(v >> 24);
}

static U32 tagOf(const char* s)
{
    return ((U32)s[0] << 24) | ((U32)s[1] << 16) | ((U32)s[2] << 8) | (U32)s[3];
}

// The bytes of a save as xsavegame writes one: a leader, one more chunk, and
// the header with its crc. Returns the save's size.
static size_t make_save(U8* out, size_t room, const char* label, S32 progress)
{
    memset(out, 0, room);

    size_t at = 16;
    put32(out + at, tagOf("LEDR"));
    put32(out + at + 4, 0x60);
    put32(out + at + 8, 0x58);
    snprintf((char*)out + at + 12, 64, "%s", label);
    put32(out + at + 12 + 64, (U32)progress);
    at += 12 + 0x60;

    put32(out + at, tagOf("ROOM"));
    put32(out + at + 4, 4);
    put32(out + at + 8, 4);
    memcpy(out + at + 12, "HB01", 4);
    at += 16;

    put32(out, tagOf("GDAT"));
    put32(out + 4, 1);
    put32(out + 8, (U32)at);
    put32(out + 12, iSaveFileCrc(out + 16, at - 16));
    return at;
}

// Save files as bfbb_config reads them, and an Xbox one's signature.
static void test_save_file()
{
    printf("iSaveFile\n");

    U8 save[512];
    const size_t size = make_save(save, sizeof(save), "Jellyfish Fields", 42);

    char why[256];
    size_t total = 0;
    check(iSaveFileCheck(save, size, &total, why, sizeof(why)) && total == size,
          "a save checks out at its own size");

    // An Xbox file is the same with 20 bytes of signature after it.
    memset(save + size, 0xAB, 20);
    total = 0;
    check(iSaveFileCheck(save, size + 20, &total, why, sizeof(why)) && total == size,
          "an Xbox save checks out too, its signature left off");

    check(!iSaveFileCheck(save, size - 1, &total, why, sizeof(why)), "a save cut short does not");

    save[size - 1] ^= 1;
    check(!iSaveFileCheck(save, size, &total, why, sizeof(why)),
          "nor does one with a byte changed");
    save[size - 1] ^= 1;

    save[0] ^= 1;
    check(!iSaveFileCheck(save, size, &total, why, sizeof(why)), "nor anything that is not a save");
    save[0] ^= 1;

    iSaveFileLeader leader;
    check(iSaveFileReadLeader(save, size, &leader) &&
              strcmp(leader.label, "Jellyfish Fields") == 0 && leader.progress == 42,
          "the leader gives the label and progress the save screen lists");

    // SaveMeta.xbx: UTF-16LE with a byte order mark.
    static const char kMeta[] = "Name=SpongeBob02\r\n";
    U8 meta[2 + 2 * sizeof(kMeta)];
    meta[0] = 0xFF;
    meta[1] = 0xFE;
    for (size_t i = 0; i < sizeof(kMeta) - 1; i++)
    {
        meta[2 + 2 * i] = (U8)kMeta[i];
        meta[3 + 2 * i] = 0;
    }
    char name[64];
    check(iSaveFileXboxMetaName(meta, 2 + 2 * (sizeof(kMeta) - 1), name, sizeof(name)) &&
              strcmp(name, "SpongeBob02") == 0,
          "SaveMeta.xbx names the file the save had on the Xbox");
    check(iSaveFileGameOf(name) == 2 && iSaveFileGameOf("spongebob00") == 0 &&
              iSaveFileGameOf("SpongeBob03") == -1,
          "and that name is one of the three games, or none");

    char leaf[64];
    iSaveFileLeaf("D:\\mods\\BFBBMix\\", leaf, sizeof(leaf));
    check(strcmp(leaf, "BFBBMix") == 0, "a mod's saves are named for its folder");
}

// Importing an Xbox save into the folder the game would use.
static void test_saves_model()
{
    printf("saves_model\n");

    char dir[512];
    char saves[512];
    if (!scratch_dir("savesmodel", dir, sizeof(dir)))
    {
        check(false, "could not make a temp directory");
        return;
    }
    snprintf(saves, sizeof(saves), "%s/saves", dir);

    char path[512];
    snprintf(path, sizeof(path), "%s/config.ini", dir);
    write_scratch(dir, "config.ini", "[game]\n", 7);

    char why[512];
    if (!ConfigModelOpen(path, why, sizeof(why)))
    {
        check(false, "the model opens a file");
        ModsModelRemoveTree(dir);
        return;
    }

    iHostSetEnv("BFBB_SAVE_DIR", saves);

    char folder[512];
    SavesModelFolder(folder, sizeof(folder));
    check(strcmp(folder, saves) == 0, "the save folder is the game's, BFBB_SAVE_DIR included");

    iSaveFileLeader leader;
    check(!SavesModelRead(1, &leader), "an empty game reads as empty");

    U8 xsv[512];
    const size_t size = make_save(xsv, sizeof(xsv), "Rock Bottom", 7);
    memset(xsv + size, 0xCD, 20);

    static const U8 kMeta[] = { 'N', 0, 'a', 0, 'm', 0, 'e', 0, '=', 0, 'S', 0, 'p', 0, 'o', 0,
                                'n', 0, 'g', 0, 'e', 0, 'B', 0, 'o', 0, 'b', 0, '0', 0, '1', 0 };

    SavesModelXbox save;
    check(SavesModelReadXbox(xsv, size + 20, kMeta, sizeof(kMeta), "test", &save, why, sizeof(why)),
          "an Xbox save reads");
    check(save.size == size && save.game == 1, "without its signature, and knowing it was game 2");

    check(SavesModelImport(&save, save.game, why, sizeof(why)), "it imports, making the folder");
    check(SavesModelRead(1, &leader) && strcmp(leader.label, "Rock Bottom") == 0,
          "and game 2 now holds it");

    char file[600];
    snprintf(file, sizeof(file), "%s/SpongeBob01", saves);
    iHostFileInfo info;
    check(iHostStat(file, &info) && info.size == size, "as the PC's file, the signature cut off");

    write_scratch(saves, "SpongeBob01.tga", "old", 3);
    check(SavesModelImport(&save, 1, why, sizeof(why)), "a second import replaces it");

    char bak[600];
    char tga[600];
    snprintf(bak, sizeof(bak), "%s/SpongeBob01.bak", saves);
    snprintf(tga, sizeof(tga), "%s/SpongeBob01.tga", saves);
    check(iHostPathExists(bak), "keeping the save it replaced beside it");
    check(!iHostPathExists(tga), "and deleting that save's thumbnail");

    SavesModelFreeXbox(&save);

    xsv[20] ^= 1;
    check(!SavesModelReadXbox(xsv, size + 20, NULL, 0, "test", &save, why, sizeof(why)),
          "a damaged Xbox save is refused");

    iHostSetEnv("BFBB_SAVE_DIR", NULL);
    ConfigModelClose();
    ModsModelRemoveTree(dir);
}

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

    check(iPadBindParse("  None ", kFakeTokens, kFakeTokenCount, "test", &b) && b.alts == 0,
          "'none' is the same as empty, in any case and with spaces round it");
    check(!iPadBindParse("none, one", kFakeTokens, kFakeTokenCount, "test", &b),
          "but only as the whole binding");
    check(!iPadBindParse("nonesuch", kFakeTokens, kFakeTokenCount, "test", &b),
          "and not as the start of a longer word");

    // One press setting two buttons, which the binding screens report.
    {
        iPadBind x;
        iPadBind y;

        iPadBindParse("one", kFakeTokens, kFakeTokenCount, "test", &x);
        iPadBindParse("three, one", kFakeTokens, kFakeTokenCount, "test", &y);
        check(iPadBindOverlap(x, y), "two bindings sharing an input overlap");

        iPadBindParse("two", kFakeTokens, kFakeTokenCount, "test", &y);
        check(!iPadBindOverlap(x, y), "and two on different inputs do not");

        iPadBindParse("one+!two", kFakeTokens, kFakeTokenCount, "test", &x);
        iPadBindParse("one+two", kFakeTokens, kFakeTokenCount, "test", &y);
        check(!iPadBindOverlap(x, y), "the gamecube preset's l1 and l2 do not overlap");

        iPadBindParse("two", kFakeTokens, kFakeTokenCount, "test", &x);
        check(iPadBindOverlap(x, y) && iPadBindOverlap(y, x),
              "but its z does overlap l2, since the chord holds z down, either way round");

        iPadBindParse("", kFakeTokens, kFakeTokenCount, "test", &x);
        check(!iPadBindOverlap(x, x), "and an empty binding overlaps nothing");
    }

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
    check(!iPadBindParse("+one", kFakeTokens, kFakeTokenCount, "test", &b), "and a leading one");

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

    // ---------------------------------------------------------------------
    // Which single input a binding names, which is how a button prompt picks
    // its picture. See iPadGlyph.h.

    iPadBindParse("two", kFakeTokens, kFakeTokenCount, "test", &b);
    check(iPadBindSoleInput(b) == FAKE_TWO, "one input is that input");

    iPadBindParse("one+two", kFakeTokens, kFakeTokenCount, "test", &b);
    check(iPadBindSoleInput(b) < 0, "a chord of two names neither");

    // The case the whole thing turns on: the GameCube preset writes L1 as
    // `lt+!rb`, and that IS the left trigger -- the negation says when it
    // counts, not what it is. A prompt that fell back here would draw the
    // console's own L1 art while the trigger was what pressed it.
    iPadBindParse("one+!two", kFakeTokens, kFakeTokenCount, "test", &b);
    check(iPadBindSoleInput(b) == FAKE_ONE, "a negated member does not count against it");

    iPadBindParse("", kFakeTokens, kFakeTokenCount, "test", &b);
    check(iPadBindSoleInput(b) < 0, "and an empty binding names nothing");

    check(iPadBindTokenName(FAKE_THREE, kFakeTokens, kFakeTokenCount) != NULL &&
              strcmp(iPadBindTokenName(FAKE_THREE, kFakeTokens, kFakeTokenCount), "three") == 0,
          "an input's name comes back from its id");
    check(iPadBindTokenName(99, kFakeTokens, kFakeTokenCount) == NULL,
          "and an id the table does not have has no name");

    // ---------------------------------------------------------------------
    // The presets.

    check(kPadBindPresetCount > 0, "there is at least one preset");

    for (S32 i = 0; i < kPadBindPresetCount; i++)
    {
        if (kPadBindPresets[i].name == NULL || kPadBindPresets[i].does == NULL)
        {
            check(false, "every preset has a name and a description");
            return;
        }
    }
    check(true, "every preset has a name and a description");

    // A preset is positional against kPadBindButtons, so a row inserted in one
    // and not the other silently rebinds every button after it. Nothing in the
    // types catches that; this is what does.
    for (S32 i = 0; i < kPadBindPresetCount; i++)
    {
        for (S32 r = kPadBindButtonCount; r < IPAD_BIND_MAX_BUTTONS; r++)
        {
            if (kPadBindPresets[i].pad[r] != NULL)
            {
                check(false, "no preset binds past the end of the button table");
                return;
            }
        }
    }
    check(true, "no preset binds past the end of the button table");

    // Every preset must be sayable in the grammar, on the real token table, or
    // a player selecting it gets a diagnostic per button and an unbound pad.
    // The fake table above cannot answer for it -- these are the names a
    // backend actually publishes.
    static const iPadBindToken kRealPadTokens[] = {
        { "a", 0 },     { "b", 1 },       { "x", 2 },       { "y", 3 },
        { "lb", 4 },    { "rb", 5 },      { "lt", 6 },      { "rt", 7 },
        { "ls", 8 },    { "rs", 9 },      { "back", 10 },   { "start", 11 },
        { "dpup", 12 }, { "dpdown", 13 }, { "dpleft", 14 }, { "dpright", 15 },
    };
    static const S32 kRealPadTokenCount = (S32)(sizeof(kRealPadTokens) / sizeof(kRealPadTokens[0]));

    for (S32 i = 0; i < kPadBindPresetCount; i++)
    {
        for (S32 r = 0; r < kPadBindButtonCount && r < IPAD_BIND_MAX_BUTTONS; r++)
        {
            const char* text = kPadBindPresets[i].pad[r];
            if (text == NULL)
            {
                text = kPadBindButtons[r].pad;
            }

            // A '#' row names a letter printed on a button, not an input, and
            // is resolved against the connected pad before it ever reaches the
            // parser. What has to hold here is that the letter is one a face
            // button can carry.
            if (text != NULL && text[0] == '#')
            {
                if (strlen(text) != 2 || strchr("ABXY", text[1]) == NULL)
                {
                    check(false, "every preset parses against a backend's token table");
                    return;
                }
                continue;
            }

            if (!iPadBindParse(text, kRealPadTokens, kRealPadTokenCount, "preset", &b))
            {
                check(false, "every preset parses against a backend's token table");
                return;
            }
        }
    }
    check(true, "every preset parses against a backend's token table");

    // What a preset does and does not do.
    //
    // It does NOT move the face buttons. Every disc's menu prompts resolve to
    // the same physical button -- accept A, cancel B, options X -- so all three
    // consoles map A to XPAD_BUTTON_X, B to TRIANGLE and X to O, and a preset
    // that disagreed here would put a prompt and its button on opposite sides
    // of the pad. What it moves is the shoulders; iPadLayout moves the bit each
    // of the player's MOVES reads.
    {
        const iPadBindPreset* gc = NULL;
        const iPadBindPreset* xb = NULL;
        const iPadBindPreset* ps = NULL;
        for (S32 i = 0; i < kPadBindPresetCount; i++)
        {
            if (strcmp(kPadBindPresets[i].name, "gamecube") == 0)
                gc = &kPadBindPresets[i];
            if (strcmp(kPadBindPresets[i].name, "xbox") == 0)
                xb = &kPadBindPresets[i];
            if (strcmp(kPadBindPresets[i].name, "ps2") == 0)
                ps = &kPadBindPresets[i];
        }

        check(gc != NULL && xb != NULL && ps != NULL, "all three console presets are there");

        if (gc != NULL && xb != NULL && ps != NULL)
        {
            S32 cancel = -1;
            S32 options = -1;
            S32 l2 = -1;
            for (S32 i = 0; i < kPadBindButtonCount; i++)
            {
                if (kPadBindButtons[i].mask == XPAD_BUTTON_TRIANGLE)
                    cancel = i;
                if (kPadBindButtons[i].mask == XPAD_BUTTON_O)
                    options = i;
                if (kPadBindButtons[i].mask == XPAD_BUTTON_L2)
                    l2 = i;
            }

            check(cancel >= 0 && options >= 0 && l2 >= 0, "cancel, options and L2 have rows");

            // Written as a LETTER, because which physical button carries a
            // letter is the pad's business and the prompt is a picture of the
            // letter. Positions would put cancel east on one pad and west on
            // another while the prompt drew the same glyph for both.
            for (S32 i = 0; i < kPadBindPresetCount; i++)
            {
                const char* c = kPadBindPresets[i].pad[cancel];
                const char* o = kPadBindPresets[i].pad[options];
                if (c == NULL || o == NULL || strcmp(c, "#B") != 0 || strcmp(o, "#X") != 0)
                {
                    check(false, "every preset cancels on B and opens options on X");
                    return;
                }
            }
            check(true, "every preset cancels on B and opens options on X");

            // The GameCube read the HUD and the close camera off one button,
            // Z, because it had nowhere else to put them. The Xbox had white
            // and black, so the two presets have to disagree here -- and a
            // preset that put them back together on a pad with a button spare
            // would be throwing one away.
            {
                S32 z = -1;
                S32 hud = -1;
                for (S32 i = 0; i < kPadBindButtonCount; i++)
                {
                    if (kPadBindButtons[i].mask == XPAD_BUTTON_Z)
                        z = i;
                    if (kPadBindButtons[i].mask == XPAD_BUTTON_HUD)
                        hud = i;
                }

                check(z >= 0 && hud >= 0 && z != hud,
                      "the close camera and the HUD are two buttons, not one bit");

                if (z >= 0 && hud >= 0)
                {
                    const char* gz = (gc->pad[z] != NULL) ? gc->pad[z] : kPadBindButtons[z].pad;
                    const char* gh =
                        (gc->pad[hud] != NULL) ? gc->pad[hud] : kPadBindButtons[hud].pad;
                    check(strcmp(gz, gh) == 0, "gamecube puts both on one button, as that pad did");

                    const char* xz = (xb->pad[z] != NULL) ? xb->pad[z] : kPadBindButtons[z].pad;
                    const char* xh =
                        (xb->pad[hud] != NULL) ? xb->pad[hud] : kPadBindButtons[hud].pad;
                    check(strcmp(xz, xh) != 0, "and xbox gives them one each");
                }
            }

            // The GameCube has three shoulders where the game wants four, and
            // SDL gives that pad no leftshoulder at all, so L2 has to be chorded
            // there and is a button of its own on the other two.
            check(gc->pad[l2] != NULL && strcmp(gc->pad[l2], "lt+rb") == 0,
                  "the gamecube preset chords L2 onto the trigger");
            check(xb->pad[l2] == NULL && strcmp(kPadBindButtons[l2].pad, "lb") == 0,
                  "and the xbox preset leaves it on a shoulder of its own");

            // xbox and ps2 bind the same pad. They differ in the glyphs drawn
            // for them and in where the PS2 put Bubble Spin, and neither of
            // those is a binding.
            for (S32 r = 0; r < kPadBindButtonCount && r < IPAD_BIND_MAX_BUTTONS; r++)
            {
                const char* a = xb->pad[r];
                const char* c = ps->pad[r];
                if ((a == NULL) != (c == NULL) || (a != NULL && strcmp(a, c) != 0))
                {
                    check(false, "xbox and ps2 bind the same buttons");
                    return;
                }
            }
            check(true, "xbox and ps2 bind the same buttons");
        }
    }
}

// The keyboard's own bindings, which a prompt names while the keyboard is in
// use. `b` on a pad is the east face button and `b` on a keyboard is the B key,
// and drawing one for the other is the fault this guards.
static void test_pad_keyboard_glyphs()
{
#ifdef BFBB_INPUT_HAVE_KEYBOARD
    const iPadBindButton* jump = iPadBindFind("a");
    check(jump != NULL, "there is a row for jump");

    if (jump != NULL)
    {
        // The first of the row's alternatives, which is the one a prompt draws.
        const char* key = iPadKeyboardBoundInput(jump->mask);
        size_t first = strcspn(jump->key, ", ");
        check(key != NULL && strlen(key) == first && strncmp(key, jump->key, first) == 0,
              "the keyboard names the key the jump row defaults to");
    }

    // A row that leads with a mouse button names the button, which is what its
    // prompt draws.
    const iPadBindButton* spin = iPadBindFind("x");
    if (spin != NULL)
    {
        const char* input = iPadKeyboardBoundInput(spin->mask);
        check(input != NULL && strcmp(input, "mouse1") == 0,
              "a mouse-then-key binding names the mouse button");
    }

    check(iPadKeyboardBoundInput(0) == NULL, "a button no row carries names nothing");

    // A set is a folder of files named after binding tokens, so every token the
    // keyboard defaults bind must have a picture. Read from the source tree,
    // which is what the build copies.
    S32 missing = 0;
    char first[64] = "";
    for (S32 i = 0; i < kPadBindButtonCount; i++)
    {
        const char* key = iPadKeyboardBoundInput(kPadBindButtons[i].mask);
        if (key == NULL)
        {
            continue;
        }

        char path[768];
        snprintf(path, sizeof(path), "%s/src/SB/Core/pc/res/buttons/keyboard/%s.png",
                 BFBB_SOURCE_DIR, key);
        if (!iHostPathExists(path))
        {
            if (missing++ == 0)
            {
                snprintf(first, sizeof(first), "%s", key);
            }
        }
    }
    if (missing != 0)
    {
        printf("  (%d bound key(s) have no picture, first is '%s' -- run tools/keyglyphs.py)\n",
               (int)missing, first);
    }
    check(missing == 0, "every key the defaults bind has a picture in buttons/keyboard");
#else
    check(true, "no keyboard in this build");
#endif
}

// Two sources turn the camera at once, and it sees the sum. A source that is
// not steering contributes nothing.
static void test_camera_look()
{
    iCameraLookSetActive(iCAMERALOOK_TOUCH, FALSE);
    iCameraLookSetActive(iCAMERALOOK_MOUSE, FALSE);
    check(!iCameraLookActive(), "no source, no orbit");

    iCameraLookSetActive(iCAMERALOOK_MOUSE, TRUE);
    check(iCameraLookActive(), "one source is enough");

    iCameraLookSet(iCAMERALOOK_TOUCH, 1.0f, 1.0f);
    iCameraLookSet(iCAMERALOOK_MOUSE, 0.25f, -0.5f);
    check(iCameraLookYaw() == 0.25f, "an inactive source adds nothing");
    check(iCameraLookPitch() == -0.5f, "on either axis");

    iCameraLookSetActive(iCAMERALOOK_TOUCH, TRUE);
    iCameraLookSet(iCAMERALOOK_TOUCH, 1.0f, 0.0f);
    iCameraLookSet(iCAMERALOOK_MOUSE, 0.5f, 0.0f);
    check(iCameraLookYaw() == 1.5f, "two sources add");
    check(iCameraLookYaw() == 0.0f, "and a read consumes them");

    iCameraLookSetActive(iCAMERALOOK_TOUCH, FALSE);
    iCameraLookSetActive(iCAMERALOOK_MOUSE, FALSE);
}

// Where each set prints the face buttons. This is what a prompt falls back on
// when there is no binding to ask -- nobody on a pad, or a button chorded --
// and getting it wrong draws the button beside the one that acts.
//
// The three rows are read off the discs: each one's cancel prompt draws the
// button printed B and its options prompt the one printed X, and the code
// cancels on TRIANGLE and opens options on O.
static void test_pad_face_glyphs()
{
    struct Row
    {
        const char* set;
        const char* accept;
        const char* cancel;
        const char* options;
        const char* misc;
    };

    const Row rows[] = {
        { "xbox", "south", "east", "west", "north" },
        { "gamecube", "south", "west", "east", "north" },
        { "ps2", "south", "north", "east", "west" },
    };

    for (S32 i = 0; i < (S32)(sizeof(rows) / sizeof(rows[0])); i++)
    {
        const char* a = iPadLayoutFaceGlyph(rows[i].set, XPAD_BUTTON_X);
        const char* c = iPadLayoutFaceGlyph(rows[i].set, XPAD_BUTTON_TRIANGLE);
        const char* o = iPadLayoutFaceGlyph(rows[i].set, XPAD_BUTTON_O);
        const char* m = iPadLayoutFaceGlyph(rows[i].set, XPAD_BUTTON_SQUARE);

        if (a == NULL || c == NULL || o == NULL || m == NULL || strcmp(a, rows[i].accept) != 0 ||
            strcmp(c, rows[i].cancel) != 0 || strcmp(o, rows[i].options) != 0 ||
            strcmp(m, rows[i].misc) != 0)
        {
            check(
                false,
                "every set prints accept, cancel, options and the third choice where its disc does");
            return;
        }
    }
    check(true,
          "every set prints accept, cancel, options and the third choice where its disc does");

    // Every console puts jump under the thumb, and no two face bits may land on
    // one file or a set would draw the same picture for two buttons.
    for (S32 i = 0; i < (S32)(sizeof(rows) / sizeof(rows[0])); i++)
    {
        const char* seen[4];
        seen[0] = iPadLayoutFaceGlyph(rows[i].set, XPAD_BUTTON_X);
        seen[1] = iPadLayoutFaceGlyph(rows[i].set, XPAD_BUTTON_TRIANGLE);
        seen[2] = iPadLayoutFaceGlyph(rows[i].set, XPAD_BUTTON_O);
        seen[3] = iPadLayoutFaceGlyph(rows[i].set, XPAD_BUTTON_SQUARE);

        for (S32 x = 0; x < 4; x++)
        {
            for (S32 y = x + 1; y < 4; y++)
            {
                if (strcmp(seen[x], seen[y]) == 0)
                {
                    check(false, "and no two of them onto one picture");
                    return;
                }
            }
        }
    }
    check(true, "and no two of them onto one picture");

    // A set nobody has a table for is a folder someone drew themselves. Those
    // are named after positions already, so the slot's own answer stands.
    check(iPadLayoutFaceGlyph("homemade", XPAD_BUTTON_TRIANGLE) == NULL,
          "a set the port does not know is left to name its own files");
    check(iPadLayoutFaceGlyph("xbox", XPAD_BUTTON_Z) == NULL,
          "and a bit that is not a face button has no letter to look up");
}

// The per-console move layout. Every release read XPAD_BUTTON_X for jump and
// disagreed only about the other three, and each disagreement is a
// transposition -- so applying one twice has to give the bit back.
static void test_pad_layout()
{
    const U32 faces[4] = { XPAD_BUTTON_X, XPAD_BUTTON_SQUARE, XPAD_BUTTON_O, XPAD_BUTTON_TRIANGLE };

    for (S32 i = 0; i < 4; i++)
    {
        if (iPadLayoutButton(iPadLayoutButton(faces[i])) != faces[i])
        {
            check(false, "the layout is its own inverse");
            return;
        }
    }
    check(true, "the layout is its own inverse");

    check(iPadLayoutButton(XPAD_BUTTON_X) == XPAD_BUTTON_X, "jump does not move on any console");
    check(iPadLayoutButton(XPAD_BUTTON_Z) == XPAD_BUTTON_Z,
          "and nothing outside the face four moves at all");

    // A permutation, or a button would do two jobs and another none.
    {
        U32 seen[4];
        for (S32 i = 0; i < 4; i++)
        {
            seen[i] = iPadLayoutButton(faces[i]);
        }

        for (S32 i = 0; i < 4; i++)
        {
            for (S32 j = i + 1; j < 4; j++)
            {
                if (seen[i] == seen[j])
                {
                    check(false, "no two moves land on the same button");
                    return;
                }
            }
        }
        check(true, "no two moves land on the same button");
    }
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

    // config.ini's input.deadzone. Auto has to hand the backend back its own
    // constant unchanged -- the two sticks have different ones, and a setting
    // that quietly flattened them to one number would be a behaviour change
    // for anyone who never touched it.
    check(iPadStickDeadzone(IPAD_STICK_DEADZONE_LEFT) == IPAD_STICK_DEADZONE_LEFT,
          "auto leaves each stick its own deadzone");
    check(iPadStickDeadzone(IPAD_STICK_DEADZONE_RIGHT) == IPAD_STICK_DEADZONE_RIGHT, "both of them");

    iPadStickSetDeadzone(50.0f);
    check(iPadStickDeadzone(IPAD_STICK_DEADZONE_LEFT) ==
              iPadStickDeadzone(IPAD_STICK_DEADZONE_RIGHT),
          "a set deadzone is the same on both sticks");
    check(abs(iPadStickDeadzone(0) - 16383) <= 1, "and half deflection is half of full scale");

    iPadStickSetDeadzone(0.0f);
    iPadStickConvert(1, 0, iPadStickDeadzone(IPAD_STICK_DEADZONE_LEFT), &x, &y);
    check(x > 0.0f, "zero means the smallest movement registers");

    iPadStickSetDeadzone(-1.0f);
    check(iPadStickDeadzone(IPAD_STICK_DEADZONE_LEFT) == IPAD_STICK_DEADZONE_LEFT,
          "and it goes back to auto");

    // input.sticks and input.invert_camera_*. The move stick reads (1, 2) and
    // the camera stick (3, 4) going in.
    F32 mx = 1.0f, my = 2.0f, cx = 3.0f, cy = 4.0f;
    iPadStickSwap(&mx, &my, &cx, &cy);
    iPadStickInvertCamera(&cx, &cy);
    check(mx == 1.0f && my == 2.0f && cx == 3.0f && cy == 4.0f, "the defaults change nothing");

    iPadStickSetInvert(TRUE, FALSE);
    iPadStickInvertCamera(&cx, &cy);
    check(cx == -3.0f && cy == 4.0f, "invert x reverses the camera's x alone");

    cx = 3.0f;
    iPadStickSetInvert(FALSE, TRUE);
    iPadStickInvertCamera(&cx, &cy);
    check(cx == 3.0f && cy == -4.0f, "invert y reverses the camera's y alone");

    mx = 1.0f, my = 2.0f, cx = 3.0f, cy = 4.0f;
    iPadStickSetSwap(TRUE);
    iPadStickSwap(&mx, &my, &cx, &cy);
    iPadStickInvertCamera(&cx, &cy);
    check(mx == 3.0f && my == 4.0f && cx == 1.0f && cy == -2.0f,
          "swapped, the right stick moves and the left stick's y is the inverted one");

    iPadStickSetSwap(FALSE);
    iPadStickSetInvert(FALSE, FALSE);
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
#define TEST_PAD_HUD 0x200000 // PC-only: the HUD, which the GameCube read off Z


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
    check(virtual_press(SDL_GAMEPAD_BUTTON_SOUTH) == TEST_PAD_X, "the south face button is jump");
    check(virtual_press(SDL_GAMEPAD_BUTTON_EAST) == TEST_PAD_TRIANGLE,
          "east on this pad is printed B, and B cancels");
    check(virtual_press(SDL_GAMEPAD_BUTTON_WEST) == TEST_PAD_O,
          "west is printed X, and X opens options");
    check(virtual_press(SDL_GAMEPAD_BUTTON_NORTH) == TEST_PAD_SQUARE, "north is Bubble Bash");

    check(virtual_press(SDL_GAMEPAD_BUTTON_START) == TEST_PAD_START, "start is start");
    check(virtual_press(SDL_GAMEPAD_BUTTON_DPAD_UP) == TEST_PAD_UP, "the d-pad comes through");
    check(virtual_press(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) == (TEST_PAD_L2 | TEST_PAD_Z),
          "LB is L2 and the close camera, as on the other backend");

    check(virtual_press(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) == (TEST_PAD_HUD | TEST_PAD_R2),
          "and RB shows the HUD, which is the other half of the GameCube's Z");

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

    // RB does not modify the triggers under this preset. That is the GameCube
    // one's trick, for a controller with three shoulders where the game wants
    // four; the default has four and each says one thing.
    SDL_SetJoystickVirtualButton(sVirtualJoystick, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    iPadHostPoll();
    U32 modified = virtual_buttons();
    check(modified == (TEST_PAD_HUD | TEST_PAD_R2 | TEST_PAD_L1 | TEST_PAD_R1),
          "RB held leaves the triggers as L1 and R1");
    check((modified & TEST_PAD_L2) == 0, "and promotes nothing to L2");

    // The harness config.ini rebinds one button -- `select = ls` -- so this is
    // the whole path from that line to the bits the game reads, on this backend
    // too. Without it the binding layer could be inert on the SDL side and
    // every check above would still pass on the defaults.
    check(virtual_press(SDL_GAMEPAD_BUTTON_LEFT_STICK) == TEST_PAD_SELECT,
          "a rebound button answers to what config.ini gave it");

    // The letter lookup input.preset resolves through. A face button's letter
    // and its position are independent -- a GameCube pad prints B west of the
    // stick where an Xbox pad prints X -- so a preset names the letter and this
    // is what finds it. The virtual pad is a standard one, so every letter sits
    // on its own position and the answer is the identity; what is being pinned
    // is that the lookup is wired to a real pad at all and comes back with a
    // token name a binding could have been written with.
    //
    // Only when the virtual pad reached port 0. The lookup answers for the pad
    // the GAME is reading, and a real controller plugged into the machine
    // running the tests takes that port first -- which is not a failure, just
    // somebody else's pad.
    if (sVirtualPort == 0)
    {
        const char* a = iPadHostInputForLabel('A');
        const char* b = iPadHostInputForLabel('B');
        const char* x = iPadHostInputForLabel('X');
        const char* y = iPadHostInputForLabel('Y');

        check(a != NULL && b != NULL && x != NULL && y != NULL,
              "every face letter resolves to an input on a connected pad");

        if (a != NULL && b != NULL && x != NULL && y != NULL)
        {
            check(strcmp(a, "a") == 0 && strcmp(b, "b") == 0 && strcmp(x, "x") == 0 &&
                      strcmp(y, "y") == 0,
                  "and on a standard pad each letter is its own position");
        }

        check(iPadHostInputForLabel('Q') == NULL, "a letter no face carries resolves to nothing");
    }
    else
    {
        printf("  (face-letter lookup not checked: a real controller holds port 0)\n");
    }

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
    check(strcmp(xUtil_idtag2string(0x48423030, 0), "HB00") == 0, "a printable tag round-trips");

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

#ifdef BFBB_INPUT_BACKEND_SDL
    test_pad_sdl_virtual();
#endif

    test_pad_bindings();
    test_pad_stick_deadzone();
    test_pad_layout();
    test_pad_face_glyphs();
    test_pad_keyboard_glyphs();
    test_camera_look();
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
    check(strcmp(isg->mcdata[0].root, isg->mcdata[1].root) != 0,
          "and the two are different places");

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

// zEnt.h's weak xSndPlay3D overload, which zGlobals.h brings in, calls this one.
U32 xSndPlay3D(U32, F32, F32, U32, U32, const xVec3*, F32, F32, sound_category, F32)
{
    return 0;
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

// isavegame.cpp writes a still beside a save through rw/savethumb.cpp, which
// needs a renderer this binary does not have. The tests save with the PC
// folders off, where no still is taken; these only have to link.
void iSaveThumbCapture()
{
}

S32 iSaveThumbHave()
{
    return FALSE;
}

S32 iSaveThumbWrite(const char*)
{
    return FALSE;
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

    check(iSndDataAcquire(0x99999999, &pcm, &bytes, NULL) == NULL,
          "an unknown asset reads nothing");
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

    // Stereo, which retail never ships and BFBBMix does: a 72-byte block, both
    // headers first, then four-byte words alternating left and right. The left
    // is predictor 1000 over all-zero words, so it holds at 1000 as above. The
    // right is predictor -2000 and its first word starts 0x07: nibble 7 at
    // step 7 adds 0+1+3+7 = 11 and moves the index to 8 (step 16), then nibble
    // 0 adds 16>>3 = 2. A decoder that swapped the channels or took the words
    // in the wrong order puts those values somewhere else.
    snprintf(path, sizeof(path), "%s/adpcm.HOP", dir);
    f = fopen(path, "wb");
    check(f != NULL, "the stereo ADPCM package could be created");
    if (f == NULL)
    {
        return;
    }

    const U8 stereoHead[16] = { 1000 & 0xff, (1000 >> 8) & 0xff, 0, 0,          // left header
                                (U8)(-2000 & 0xff), (U8)((-2000 >> 8) & 0xff), 0, 0, // right
                                0, 0, 0, 0,                                     // left word 0
                                0x07, 0, 0, 0 };                                // right word 0
    fwrite(stereoHead, 1, sizeof(stereoHead), f);
    for (U32 i = sizeof(stereoHead); i < 2 * kBlock; i++)
    {
        fputc(0x00, f);
    }
    fclose(f);

    sFakePkgAsset = 0x0AD9C001;
    sFakePkgSize = 2 * kBlock;

    adpcm.channels = 2;
    adpcm.block_align = 2 * kBlock;

    abytes = 0;
    iSndDataPcm spcm;
    const S16* s = (const S16*)iSndDataAcquire(sFakePkgAsset, &adpcm, &abytes, &spcm);

    check(s != NULL, "a stereo ADPCM asset decodes");
    check(abytes == 64 * 2 * sizeof(S16), "a 72-byte stereo block is 64 frames of two samples");
    check(spcm.channels == 2, "and reaches the mixer as two channels");

    if (s != NULL)
    {
        check(s[1] == -1989 && s[3] == -1987,
              "the right channel's first word lands in the right channel's first frames");

        bool left = true;
        for (U32 i = 0; i < 64; i++)
        {
            left = left && s[i * 2] == 1000;
        }
        check(left, "and the left channel holds, untouched by the right's nibbles");
    }

    iSndDataRelease(sFakePkgAsset);
    iSndDataReset();

    // GameCube DSP ADPCM. Coefficient pair 1 is (1.0, 0), so each sample is
    // the last plus nibble * scale. The frames are the ones test_snd_dsp
    // decodes; here they come through the cache, once bare and once behind the
    // big-endian header copy a cutscene sound carries.
    iSndDspHeader dsp;
    memset(&dsp, 0, sizeof(dsp));
    dsp.num_samples = 16;
    dsp.num_nibbles = 2 * 16 + 2;
    dsp.sample_rate = 22050;
    dsp.coef[2] = 2048;

    const U8 frames[16] = { 0x11, 0x12, 0x34, 0x5F, 0, 0, 0, 0, 0x00, 0x7F, 0, 0, 0, 0, 0, 0 };

    for (S32 headed = 0; headed < 2; headed++)
    {
        snprintf(path, sizeof(path), "%s/dsp.HOP", dir);
        f = fopen(path, "wb");
        check(f != NULL, "the DSP ADPCM package could be created");
        if (f == NULL)
        {
            return;
        }

        U8 head[ISNDDSP_HEADER_SIZE];
        memset(head, 0, sizeof(head));
        putBE32(head + 0x0, dsp.num_samples);
        putBE32(head + 0x4, dsp.num_nibbles);
        putBE32(head + 0x8, dsp.sample_rate);
        if (headed)
        {
            fwrite(head, 1, sizeof(head), f);
        }
        fwrite(frames, 1, sizeof(frames), f);
        fclose(f);

        snprintf(sFakePkgName, sizeof(sFakePkgName), "dsp.HOP");
        sFakePkgAsset = 0x0D5B0000 + headed;
        sFakePkgOffset = 0;
        sFakePkgSize = (U32)sizeof(frames) + (headed ? ISNDDSP_HEADER_SIZE : 0);

        iSndDataFormat gc;
        gc.format_tag = ISNDDATA_FORMAT_GC_DSPADPCM;
        gc.channels = 1;
        gc.block_align = ISNDDSP_FRAME_BYTES;
        gc.dsp = &dsp;

        U32 gbytes = 0;
        const S16* g = (const S16*)iSndDataAcquire(sFakePkgAsset, &gc, &gbytes, NULL);

        check(g != NULL,
              headed ? "a cutscene's DSP ADPCM sound decodes" : "a DSP ADPCM sound decodes");
        check(gbytes == 16 * sizeof(S16), "to num_samples 16-bit samples");
        if (g != NULL)
        {
            check(g[0] == 2 && g[4] == 30 && g[5] == 28 && g[13] == 28 && g[14] == 7 && g[15] == -1,
                  headed ? "from past its header copy" : "with the table's coefficients");
        }

        iSndDataRelease(sFakePkgAsset);
        iSndDataReset();
    }
}

// ---------------------------------------------------------------------------
// GameCube DSP ADPCM

static void test_snd_dsp()
{
    printf("iSndDsp\n");

    check(iSndDspNibbleToSample(2) == 0 && iSndDspNibbleToSample(15) == 13 &&
              iSndDspNibbleToSample(18) == 14 && iSndDspNibbleToSample(34) == 28,
          "a nibble address skips each frame's two header nibbles");
    check(iSndDspBytesFor(0) == 0 && iSndDspBytesFor(14) == 8 && iSndDspBytesFor(15) == 16,
          "fourteen samples to an eight-byte frame");

    iSndDspHeader h;
    memset(&h, 0, sizeof(h));
    h.num_samples = 16;
    h.coef[2] = 2048; // pair 1: hist1 * 1.0
    h.coef[3] = 0;

    // Frame 0: pair 1, scale 2, nibbles 1 2 3 4 5 -1 then zeros, high nibble
    // first. Frame 1: pair 0 (both coefficients 0), scale 1, nibbles 7 -1.
    const U8 frames[16] = { 0x11, 0x12, 0x34, 0x5F, 0, 0, 0, 0, 0x00, 0x7F, 0, 0, 0, 0, 0, 0 };
    S16 out[24];

    check(iSndDspDecode(frames, sizeof(frames), &h, out) == 16, "two frames give 16 samples");
    check(out[0] == 2 && out[1] == 6 && out[2] == 12 && out[3] == 20 && out[4] == 30,
          "each sample is the last plus nibble * scale under (1.0, 0)");
    check(out[5] == 28 && out[13] == 28, "a negative nibble steps down; zeros hold");
    check(out[14] == 7 && out[15] == -1, "pair 0 ignores the history, and rounds toward -inf");

    h.yn1 = 100;
    iSndDspDecode(frames, sizeof(frames), &h, out);
    check(out[0] == 102, "the header's yn1 seeds the history");
    h.yn1 = 0;

    // A frame cut short decodes as far as its bytes go, and the rest of
    // num_samples is silence.
    h.num_samples = 20;
    memset(out, 0x55, sizeof(out));
    check(iSndDspDecode(frames, 10, &h, out) == 16, "a two-byte last frame gives two samples");
    check(out[14] == 7 && out[15] == -1 && out[16] == 0 && out[19] == 0,
          "and what the data does not reach is silence");

    // Scale 2^12, nibble 7, twice: 28672 then 57344, which clamps.
    const U8 loud[8] = { 0x1C, 0x77, 0, 0, 0, 0, 0, 0 };
    h.num_samples = 2;
    iSndDspDecode(loud, sizeof(loud), &h, out);
    check(out[0] == 28672 && out[1] == 32767, "a sample past 16 bits clamps");

    U8 asset[ISNDDSP_HEADER_SIZE + 8];
    memset(asset, 0, sizeof(asset));
    h.num_samples = 14;
    h.num_nibbles = 16;
    h.sample_rate = 32000;
    putBE32(asset + 0x0, 14);
    putBE32(asset + 0x4, 16);
    putBE32(asset + 0x8, 32000);
    check(iSndDspDataOffset(asset, sizeof(asset), &h) == ISNDDSP_HEADER_SIZE,
          "a sound that starts with its own header is read past it");
    putBE32(asset + 0x8, 22050);
    check(iSndDspDataOffset(asset, sizeof(asset), &h) == 0, "and one that does not, from byte 0");

    // iSndDspSwapHeader: the u32s, the u16s and the coefficients.
    U8 be[ISNDDSP_HEADER_SIZE];
    memset(be, 0, sizeof(be));
    putBE32(be + 0x00, 0x00123456);
    putBE16(be + 0x0C, 1);
    putBE32(be + 0x10, 2);
    putBE16(be + 0x1C, 0xF800);
    putBE16(be + 0x3E, 0x0047);
    putBE16(be + 0x48, 0xFFFE);
    iSndDspSwapHeader(be);
    iSndDspHeader sw;
    memcpy(&sw, be, sizeof(sw));
    check(sw.num_samples == 0x00123456 && sw.loop_flag == 1 && sw.loop_start == 2 &&
              sw.coef[0] == -2048 && sw.pred_scale == 0x47 && sw.loop_yn2 == -2,
          "a big-endian header swaps field by field");
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
    check(iSoundtrackDecode(junk, &ch, &hz, &bytes) == NULL,
          "a file that is not audio decodes to nothing");
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

#ifdef BFBB_AUDIO_BACKEND_SDL
void iSndHostTestMix(U32 rate, float* out, U32 frames);

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

    iSndHostTestMix(2000, out, 100);

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
    iSndHostTestMix(2000, out, 100);
    check(!iSndHostIsPlaying(v), "the voice finishes when the samples run out");

    // --- volume per side -----------------------------------------------------
    iSndHostSetVol(v, 0.5f, 0.0f);
    iSndHostStart(v, &s);
    iSndHostTestMix(1000, out, 16);

    check(fabsf(out[2 * 8] - (800.0f / 32768.0f) * 0.5f) < 0.001f, "the left gain applies");
    check(fabsf(out[2 * 8 + 1]) < 0.0001f, "and a zero right gain silences that side");

    // --- looping -------------------------------------------------------------
    s.looping = true;
    iSndHostSetVol(v, 1.0f, 1.0f);
    iSndHostStart(v, &s);

    // 150 output frames at the source rate: past the 100-frame sample, so the
    // last 50 are the loop's second pass.
    iSndHostTestMix(1000, out, 150);
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

    iSndHostTestMix(1000, out, 50);
    check(iSndHostIsPlaying(v), "and is still going halfway through its length");
    check(fabsf(out[0]) < 0.0001f, "while contributing nothing to the mix");

    iSndHostTestMix(1000, out, 60);
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
    check(first == (U32)(0.008f * (float)rate), "the first reflection lands at reflections_delay");

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

    // Retail turned a voice pointer into an index with offset/100, which is
    // sizeof(xSndVoiceInfo) on a 32-bit build. The port does it with pointer
    // arithmetic instead, so what has to hold is the round trip -- and that is
    // what is checked here, at whatever the struct's size is. The 100 is still
    // asserted where it is still true, because a 32-bit build that stops
    // matching retail's layout has changed something it should not have.
    if (sizeof(void*) == 4)
    {
        check(sizeof(xSndVoiceInfo) == 100, "xSndVoiceInfo is 100 bytes, the retail layout");
    }
    {
        bool round_trip = true;
        for (S32 i = 0; i < (S32)ARRAY_SIZE(gSnd.voice); i++)
        {
            if ((S32)(&gSnd.voice[i] - gSnd.voice) != i)
            {
                round_trip = false;
            }
        }
        check(round_trip, "a voice pointer turns back into its own index, as iSndPlay needs");
    }
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
    // conversion happened at all. +12 is an octave, so the 0.1 s sample runs
    // out at 0.05 s at that pitch and at 0.1 s without it.
    //
    // The commonest value is 0, which converts to 1.0 either way, so a port
    // that forgot the conversion looks correct until the HUD counter reaches
    // 6.5 semitones and plays six and a half times too fast.
    //
    // The pitch-0 pass is the control: same sample, same call, only the pitch
    // differs, so the octave pass's negative means the pitch did it rather than
    // the voice never starting. It is checked at the start of its sample and
    // not partway through -- a voice advances by the samples the mixer has
    // consumed, so "still playing" some way in races the scheduler on a loaded
    // machine and says nothing about pitch either way.
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

        if (pass == 1)
        {
            check(iSndIsPlayingByHandle(vp->sndID), "and the same sample at pitch 0 starts");
        }

        // Past the 0.05 s the octave pass runs out at, and short of the 0.1 s
        // the pitch-0 one lasts.
        iHostSleepUntilNs(iHostMonotonicNs() + 80000000ULL);
        iSndHostUpdate();

        if (pass == 0)
        {
            check(!iSndIsPlayingByHandle(vp->sndID),
                  "+12 semitones is an octave up, so the sample ends in half the time");
        }

        iSndStop(0x4400 + pass);
    }

    // A GameCube table, as iGCSwap leaves it: host byte order, total_size
    // stamped, 0x64-byte entries that are the DSPADPCM header and an id. One
    // sound, one stream, one cutscene sound.
    static U32 gcTable[(0x10 + 3 * 0x64) / 4];
    memset(gcTable, 0, sizeof(gcTable));
    gcTable[0] = 1;
    gcTable[1] = IGCSWAP_SNDI_GAMECUBE;
    gcTable[2] = 1;
    gcTable[3] = 1;
    const U32 gcIds[3] = { 0x33333333, 0x44444444, 0x55555555 };
    for (U32 i = 0; i < 3; i++)
    {
        iSndDspHeader h;
        memset(&h, 0, sizeof(h));
        h.num_samples = 3200 * (i + 1);
        h.sample_rate = 16000 * (i + 1);
        h.loop_start = 2;
        memcpy((U8*)gcTable + 0x10 + i * 0x64, &h, sizeof(h));
        memcpy((U8*)gcTable + 0x10 + i * 0x64 + 0x60, &gcIds[i], 4);
    }

    check(iSndLoadSounds(gcTable) == 1, "iSndLoadSounds accepts a GameCube table");

    test_lookup* g0 = (test_lookup*)iSndLookup(gcIds[0]);
    check(g0 != NULL && g0->num_samples == 3200 && g0->sample_rate == 16000 && g0->ID >= 0x1000,
          "a GameCube sound looks up with its own length and rate, as a sound");
    test_lookup* g1 = (test_lookup*)iSndLookup(gcIds[1]);
    check(g1 != NULL && g1->num_samples == 6400 && g1->sample_rate == 32000 && g1->ID > 0 &&
              g1->ID < 0x1000,
          "a GameCube stream looks up as a stream");
    test_lookup* g2 = (test_lookup*)iSndLookup(gcIds[2]);
    check(g2 != NULL && g2->num_samples == 9600 && g2->ID < 0x1000,
          "and a cutscene sound past both, also in the stream range");
    check(iSndLookup(SND_ASSET) != NULL, "the Xbox table below it still answers");

    iSndSceneExit();
    check(iSndLookup(gcIds[0]) == NULL && iSndLookup(SND_ASSET) != NULL,
          "a scene ending pops the GameCube table and leaves the one below");

    iSndExit();
    iHostSetEnv("BFBB_AUDIO", NULL);
}

// ---------------------------------------------------------------------------
// The two distance curves
//
// xbox.sound_rolloff picks between the GameCube's attenuation and the Xbox's,
// and the difference is the whole of issue #27: at 30 units from an emitter
// with a 10/50 radius pair the GameCube is 8 dB louder. Both are arithmetic
// nobody hears going subtly wrong, so both are pinned here.
//
// The gain is read off the two sides in whichever way the setting's own pan law
// makes independent of the pan: in quadrature under the GameCube's equal-power
// law, off the louder side under the Xbox's. Either way these stay tests of the
// attenuation. The pan laws themselves are pinned in test_snd_pan.

void iSndTestVoiceGain(const xSndVoiceInfo* vp, S32 xboxRolloff, F32* left, F32* right);

static F32 gain_at(F32 vol, F32 inner, F32 outer, F32 dist, S32 xboxRolloff)
{
    xSndVoiceInfo vp;
    memset(&vp, 0, sizeof(vp));

    vp.flags = 0x8;
    vp.category = SND_CAT_GAME;
    vp.vol = vol;
    vp.innerRadius2 = inner * inner;
    vp.outerRadius2 = outer * outer;

    // Straight ahead, so the pan comes out centred.
    vp.playPos.x = 0.0f;
    vp.playPos.y = 0.0f;
    vp.playPos.z = dist;

    F32 l;
    F32 r;
    iSndTestVoiceGain(&vp, xboxRolloff, &l, &r);

    if (xboxRolloff)
    {
        return (l > r) ? l : r;
    }

    return sqrtf(l * l + r * r);
}

static void test_snd_rolloff()
{
    printf("iSnd (distance curves)\n");

    memset(&gSnd, 0, sizeof(gSnd));
    for (S32 i = 0; i < 8; i++)
    {
        gSnd.categoryVolFader[i] = 1.0f;
    }
    gSnd.right.x = 1.0f;

    // The GameCube's: flat inside the inner radius, sqrt of the squared-distance
    // interpolation between them, silence past the outer one.
    check(fabsf(gain_at(1.0f, 10.0f, 50.0f, 0.0f, FALSE) - 1.0f) < 0.001f,
          "GameCube: full volume at the emitter");
    check(fabsf(gain_at(1.0f, 10.0f, 50.0f, 10.0f, FALSE) - 1.0f) < 0.001f,
          "and still full at the inner radius");
    check(fabsf(gain_at(1.0f, 10.0f, 50.0f, 30.0f, FALSE) - 0.8165f) < 0.001f,
          "0.82 at 30 units, most of the way out");
    check(gain_at(1.0f, 10.0f, 50.0f, 50.0f, FALSE) < 0.001f, "and nothing at the outer radius");
    check(gain_at(1.0f, 10.0f, 50.0f, 80.0f, FALSE) < 0.001f, "or beyond it");

    // The Xbox's: DirectSound's inverse law, cut off past the max distance,
    // because the buffers carry DSBCAPS_MUTE3DATMAXDISTANCE.
    check(fabsf(gain_at(1.0f, 10.0f, 50.0f, 0.0f, TRUE) - 1.0f) < 0.001f,
          "Xbox: full volume at the emitter");
    check(fabsf(gain_at(1.0f, 10.0f, 50.0f, 10.0f, TRUE) - 1.0f) < 0.001f,
          "and still full at the min distance");
    check(fabsf(gain_at(1.0f, 10.0f, 50.0f, 30.0f, TRUE) - 0.3333f) < 0.001f,
          "min over distance at 30 units, 8 dB below the GameCube");
    check(fabsf(gain_at(1.0f, 10.0f, 50.0f, 50.0f, TRUE) - 0.2f) < 0.001f,
          "0.2 at the max distance, where the GameCube is already silent");
    check(gain_at(1.0f, 10.0f, 50.0f, 80.0f, TRUE) < 0.001f, "and silent past it");

    // A tight radius pair is the case that catches a missing cutoff: Goo
    // Lagoon 01's fires are 9 in and 10 out, which is a decibel down at the
    // edge and has to go to nothing rather than stay there.
    check(fabsf(gain_at(1.0f, 9.0f, 10.0f, 10.0f, TRUE) - 0.9f) < 0.001f,
          "0.9 at the edge of a 9/10 emitter");
    check(gain_at(1.0f, 9.0f, 10.0f, 11.0f, TRUE) < 0.001f, "and nothing a unit past it");

    // A radius under 1 is clamped to 1 on the way into DirectSound, so an
    // emitter with no inner radius does not divide the whole level away.
    check(fabsf(gain_at(1.0f, 0.0f, 50.0f, 1.0f, TRUE) - 1.0f) < 0.001f,
          "an inner radius of 0 reads as 1");
    check(fabsf(gain_at(1.0f, 0.0f, 50.0f, 4.0f, TRUE) - 0.25f) < 0.001f,
          "and attenuates from there");

    // The volume curve, which the Xbox applies to every voice and not just the
    // positioned ones: 10*log2 truncated to whole decibels.
    check(fabsf(gain_at(0.7f, 10.0f, 50.0f, 5.0f, FALSE) - 0.7f) < 0.001f,
          "GameCube: the port passes a volume through as it is");
    check(fabsf(gain_at(0.7f, 10.0f, 50.0f, 5.0f, TRUE) - 0.56234f) < 0.001f,
          "Xbox: 0.7 is -5 dB, not -3.1");
    check(fabsf(gain_at(0.9f, 10.0f, 50.0f, 5.0f, TRUE) - 0.89125f) < 0.001f,
          "and 0.9 is -1 dB");

    // The floor DirectSound is handed is -64 dB, but a category the player has
    // silenced has to be silent, so zero stays zero.
    gSnd.categoryVolFader[SND_CAT_GAME] = 0.0f;
    check(gain_at(1.0f, 10.0f, 50.0f, 5.0f, TRUE) == 0.0f, "a muted category is silent");
    gSnd.categoryVolFader[SND_CAT_GAME] = 1.0f;
}

// ---------------------------------------------------------------------------
// The pan laws.
//
// The two agree at the ends and differ in the middle: the GameCube crossfades
// with equal power, so a centred sound is 3 dB down on both sides, while
// DirectSound cuts one side and leaves the other at full. The gap lands on
// every unpositioned voice -- a third of the live ones in play -- because the
// Xbox never pans those at all. It reads as the game being quiet rather than as
// anything to do with panning, so it is pinned here.
//
// Which speaker a given direction goes to is not what these check. They assert
// only that one side is full and the other silent, which both laws do.

static void pan_at(F32 x, F32 z, U32 flags, S32 xboxRolloff, F32* l, F32* r)
{
    xSndVoiceInfo vp;
    memset(&vp, 0, sizeof(vp));

    vp.flags = flags;
    vp.category = SND_CAT_GAME;
    vp.vol = 1.0f;
    vp.innerRadius2 = 100.0f;
    vp.outerRadius2 = 2500.0f;

    // Inside the inner radius either way, so distance contributes nothing and
    // what comes back is the pan on its own. x is across the listener, z ahead.
    vp.playPos.x = x;
    vp.playPos.y = 0.0f;
    vp.playPos.z = z;

    iSndTestVoiceGain(&vp, xboxRolloff, l, r);
}

static void test_snd_pan()
{
    printf("iSnd (pan laws)\n");

    memset(&gSnd, 0, sizeof(gSnd));
    for (S32 i = 0; i < 8; i++)
    {
        gSnd.categoryVolFader[i] = 1.0f;
    }
    gSnd.right.x = 1.0f;
    gSnd.right.y = 0.0f;
    gSnd.right.z = 0.0f;

    F32 l;
    F32 r;

    // A voice with no position. The Xbox leaves its buffer at DSBPAN_CENTER,
    // which is full on both sides.
    pan_at(0.0f, 0.0f, 0x0, TRUE, &l, &r);
    check(fabsf(l - 1.0f) < 0.001f && fabsf(r - 1.0f) < 0.001f,
          "Xbox: an unpositioned voice is full on both sides");

    pan_at(0.0f, 0.0f, 0x0, FALSE, &l, &r);
    check(fabsf(l - 0.70711f) < 0.001f && fabsf(r - 0.70711f) < 0.001f,
          "GameCube: the same voice is 3 dB down on both");

    // A positioned voice the listener is facing straight on. Retail's index runs
    // 0 to 0x7f, so its 0x40 is a sixth of a decibel off true centre and the two
    // sides come back very slightly apart.
    pan_at(0.0f, 5.0f, 0x8, TRUE, &l, &r);
    check(fabsf(l - 1.0f) < 0.02f && fabsf(r - 1.0f) < 0.02f,
          "Xbox: a centred 3D voice is full on both sides too");

    pan_at(0.0f, 5.0f, 0x8, FALSE, &l, &r);
    check(fabsf(l - 0.70711f) < 0.02f && fabsf(r - 0.70711f) < 0.02f,
          "GameCube: and 3 dB down on both");

    // Hard over, where the laws agree: all of it on one side, none on the other.
    pan_at(5.0f, 0.0f, 0x8, TRUE, &l, &r);
    check(fabsf(l - 1.0f) < 0.001f && r < 0.001f, "Xbox: hard over is full and silent");

    pan_at(5.0f, 0.0f, 0x8, FALSE, &l, &r);
    check(fabsf(l - 1.0f) < 0.001f && r < 0.001f, "GameCube: and hard over agrees");

    // The other way, to catch a law that only works in one direction.
    pan_at(-5.0f, 0.0f, 0x8, TRUE, &l, &r);
    check(l < 0.001f && fabsf(r - 1.0f) < 0.001f, "Xbox: hard the other way is the mirror");

    pan_at(-5.0f, 0.0f, 0x8, FALSE, &l, &r);
    check(l < 0.001f && fabsf(r - 1.0f) < 0.001f, "GameCube: as is the GameCube's");

    // Part of the way over. The near side stays at the voice's own volume and
    // the far side is cut, which is what SetPan does; the pan never adds gain.
    pan_at(3.0f, 4.0f, 0x8, TRUE, &l, &r);
    check(fabsf(l - 1.0f) < 0.001f, "Xbox: the near side of a partial pan is uncut");
    check(r > 0.001f && r < l, "and the far side is down but not silent");
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
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x80,
    0x3f,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x80,
    0x3f,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x80,
    0x3f,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x48,
    0xe1,
    0x7a,
    0x3f,
    0xf6,
    0x28,
    0x3c,
    0x3f,
    0x4b,
    0x86,
    0xd5,
    0x41,
    // key 1: frame 1, the first real pose of the shot
    0x01,
    0x00,
    0x00,
    0x00,
    0x87,
    0x88,
    0x7c,
    0x3f,
    0xeb,
    0xc1,
    0x24,
    0xbe,
    0x56,
    0x57,
    0x02,
    0xbd,
    0x25,
    0xfe,
    0xdb,
    0x3d,
    0xf9,
    0xc8,
    0x47,
    0x3f,
    0x51,
    0xb0,
    0x1d,
    0xbf,
    0xe8,
    0xd4,
    0xfd,
    0x3d,
    0xa4,
    0xad,
    0x1a,
    0x3f,
    0x0c,
    0x81,
    0x49,
    0x3f,
    0xc9,
    0x96,
    0x78,
    0x42,
    0xdd,
    0x5c,
    0xe0,
    0x41,
    0x03,
    0x9d,
    0x64,
    0x41,
    0x48,
    0xe1,
    0x7a,
    0x3f,
    0xf6,
    0x28,
    0x3c,
    0x3f,
    0x4b,
    0x86,
    0xd5,
    0x41,
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

    check(hf->basesector(ld) == 0, "base sector is 0 on a host, where there is no disc");

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

// A package built in memory for the merge tests: HIPA, PACK { PVER PCNT PLAT },
// DICT { ATOC LTOC }, STRM { DHDR DPAK }, as the retail packer lays it out.
struct SynthAsset
{
    U32 id;
    U32 type;
    const void* data;
    U32 size;
};

struct SynthLayer
{
    U32 type;
    S32 count;
    U32 ids[8];
};

struct SynthBuf
{
    U8 bytes[16384];
    size_t size;

    void be(U32 v)
    {
        bytes[size++] = (U8)(v >> 24);
        bytes[size++] = (U8)(v >> 16);
        bytes[size++] = (U8)(v >> 8);
        bytes[size++] = (U8)v;
    }

    void raw(const void* p, size_t n)
    {
        memcpy(bytes + size, p, n);
        size += n;
    }

    size_t begin(const char* tag)
    {
        size_t at = size;
        raw(tag, 4);
        be(0);
        return at;
    }

    void end(size_t at)
    {
        U32 n = (U32)(size - at - 8);
        bytes[at + 4] = (U8)(n >> 24);
        bytes[at + 5] = (U8)(n >> 16);
        bytes[at + 6] = (U8)(n >> 8);
        bytes[at + 7] = (U8)n;
        if (n & 1)
        {
            bytes[size++] = 0;
        }
    }
};

static void synth_hip(SynthBuf* b, const char* plat, const SynthAsset* assets, S32 count,
                      const SynthLayer* layers, S32 layerCount)
{
    b->size = 0;
    b->end(b->begin("HIPA"));

    size_t pack = b->begin("PACK");
    size_t c = b->begin("PVER");
    b->be(2);
    b->be(0x000A000F);
    b->be(1);
    b->end(c);
    c = b->begin("PCNT");
    b->be((U32)count);
    b->be((U32)layerCount);
    b->be(0);
    b->be(0);
    b->be(0);
    b->end(c);
    c = b->begin("PLAT");
    b->raw(plat, 2);
    b->raw("\0\0Name\0\0", 8);
    b->end(c);
    b->end(pack);

    size_t offsetAt[16];
    size_t dict = b->begin("DICT");
    size_t atoc = b->begin("ATOC");
    c = b->begin("AINF");
    b->be(0);
    b->end(c);
    for (S32 i = 0; i < count; i++)
    {
        size_t ahdr = b->begin("AHDR");
        b->be(assets[i].id);
        b->be(assets[i].type);
        offsetAt[i] = b->size;
        b->be(0);
        b->be(assets[i].size);
        b->be(0);
        b->be(0);
        size_t adbg = b->begin("ADBG");
        b->be(16);
        b->raw("\0\0name\0\0", 8);
        b->be(0x12345678);
        b->end(adbg);
        b->end(ahdr);
    }
    b->end(atoc);

    size_t ltoc = b->begin("LTOC");
    c = b->begin("LINF");
    b->be(0);
    b->end(c);
    for (S32 l = 0; l < layerCount; l++)
    {
        size_t lhdr = b->begin("LHDR");
        b->be(layers[l].type);
        b->be((U32)layers[l].count);
        for (S32 i = 0; i < layers[l].count; i++)
        {
            b->be(layers[l].ids[i]);
        }
        b->end(lhdr);
    }
    b->end(ltoc);
    b->end(dict);

    size_t strm = b->begin("STRM");
    c = b->begin("DHDR");
    b->be(0xFFFFFFFF);
    b->end(c);
    size_t dpak = b->begin("DPAK");
    for (S32 i = 0; i < count; i++)
    {
        while (b->size % 16 != 0)
        {
            b->bytes[b->size++] = 0x33;
        }
        U32 off = (U32)b->size;
        b->bytes[offsetAt[i]] = (U8)(off >> 24);
        b->bytes[offsetAt[i] + 1] = (U8)(off >> 16);
        b->bytes[offsetAt[i] + 2] = (U8)(off >> 8);
        b->bytes[offsetAt[i] + 3] = (U8)off;
        b->raw(assets[i].data, assets[i].size);
    }
    b->end(dpak);
    b->end(strm);
}

// What a merged package holds, read back with a walker of the tests' own.
struct ReadAsset
{
    U32 id;
    U32 type;
    U32 offset;
    U32 size;
    U32 pad;
};

struct ReadLayer
{
    U32 type;
    S32 count;
    U32 ids[16];
};

struct ReadHip
{
    ReadAsset assets[32];
    S32 count;
    ReadLayer layers[16];
    S32 layerCount;
    U32 pcntAssets;
    U32 pcntLayers;
    const U8* file;
    size_t fileSize;

    const ReadAsset* find(U32 id) const
    {
        for (S32 i = 0; i < count; i++)
        {
            if (assets[i].id == id)
            {
                return &assets[i];
            }
        }
        return NULL;
    }

    // The asset's data, if it lies in the file.
    bool holds(U32 id, const void* data, U32 size) const
    {
        const ReadAsset* a = find(id);
        return a != NULL && a->size == size && a->offset + size <= fileSize &&
               memcmp(file + a->offset, data, size) == 0;
    }
};

static U32 rd_be(const U8* p)
{
    return ((U32)p[0] << 24) | ((U32)p[1] << 16) | ((U32)p[2] << 8) | p[3];
}

static void read_chunks(const U8* p, size_t size, ReadHip* out)
{
    size_t at = 0;
    while (at + 8 <= size)
    {
        U32 id = rd_be(p + at);
        U32 n = rd_be(p + at + 4);
        const U8* body = p + at + 8;

        if (id == TAG4('P', 'A', 'C', 'K') || id == TAG4('D', 'I', 'C', 'T') ||
            id == TAG4('A', 'T', 'O', 'C') || id == TAG4('L', 'T', 'O', 'C'))
        {
            read_chunks(body, n, out);
        }
        else if (id == TAG4('P', 'C', 'N', 'T'))
        {
            out->pcntAssets = rd_be(body);
            out->pcntLayers = rd_be(body + 4);
        }
        else if (id == TAG4('A', 'H', 'D', 'R') && out->count < 32)
        {
            ReadAsset* a = &out->assets[out->count++];
            a->id = rd_be(body);
            a->type = rd_be(body + 4);
            a->offset = rd_be(body + 8);
            a->size = rd_be(body + 12);
            a->pad = rd_be(body + 16);
        }
        else if (id == TAG4('L', 'H', 'D', 'R') && out->layerCount < 16)
        {
            ReadLayer* l = &out->layers[out->layerCount++];
            l->type = rd_be(body);
            l->count = (S32)rd_be(body + 4);
            for (S32 i = 0; i < l->count && i < 16; i++)
            {
                l->ids[i] = rd_be(body + 8 + 4 * i);
            }
        }

        at += 8 + n + (n & 1);
    }
}

static void read_hip(const U8* p, size_t size, ReadHip* out)
{
    memset(out, 0, sizeof(*out));
    out->file = p;
    out->fileSize = size;
    read_chunks(p, size, out);
}

// Whether every layer is laid out the way the packer reads it: from a 2K
// boundary, its assets back to back in order, each pad the gap to the next.
static bool layers_readable(const ReadHip& h)
{
    for (S32 l = 0; l < h.layerCount; l++)
    {
        const ReadLayer& layer = h.layers[l];
        for (S32 i = 0; i < layer.count; i++)
        {
            const ReadAsset* a = h.find(layer.ids[i]);
            if (a == NULL || a->offset + a->size > h.fileSize)
            {
                return false;
            }
            if (i == 0 && a->offset % 0x800 != 0)
            {
                return false;
            }
            if (i + 1 < layer.count)
            {
                const ReadAsset* next = h.find(layer.ids[i + 1]);
                if (next == NULL || a->offset + a->size + a->pad != next->offset)
                {
                    return false;
                }
            }
        }
    }
    return true;
}

static void le32(U8* p, U32 v)
{
    p[0] = (U8)v;
    p[1] = (U8)(v >> 8);
    p[2] = (U8)(v >> 16);
    p[3] = (U8)(v >> 24);
}

static U32 rd_le(const U8* p)
{
    return ((U32)p[3] << 24) | ((U32)p[2] << 16) | ((U32)p[1] << 8) | p[0];
}

// A merged package read back whole.
struct MergedBytes
{
    U8* data;
    U32 size;
};

// Merges `mod` into `base` through iHipSourceMerge and reads the result.
static bool merge_synth(const U8* base, size_t baseSize, const U8* mod, size_t modSize,
                        MergedBytes* out, char* why, size_t whySize)
{
    U8* b = (U8*)malloc(baseSize);
    U8* m = (U8*)malloc(modSize);
    memcpy(b, base, baseSize);
    memcpy(m, mod, modSize);

    iHipSource* merged = iHipSourceMerge(iHipSourceMemory(b, (U32)baseSize),
                                         iHipSourceMemory(m, (U32)modSize), why, whySize);
    if (merged == NULL)
    {
        return false;
    }

    out->size = iHipSourceSize(merged);
    out->data = (U8*)malloc(out->size);
    bool ok = iHipSourceRead(merged, 0, out->data, out->size);

    // The same bytes read in small pieces that straddle the merge's spans.
    U8* piecewise = (U8*)malloc(out->size);
    for (U32 at = 0; at < out->size; at += 7)
    {
        U32 n = out->size - at < 7 ? out->size - at : 7;
        ok = ok && iHipSourceRead(merged, at, piecewise + at, n);
    }
    ok = ok && memcmp(piecewise, out->data, out->size) == 0;
    free(piecewise);

    U8 past[4] = { 1, 1, 1, 1 };
    ok = ok && iHipSourceRead(merged, out->size - 2, past, 4) && past[2] == 0 && past[3] == 0;

    iHipSourceClose(merged);
    return ok;
}

static void test_hip_merge()
{
    printf("Heavy Mod Manager package merge\n");

    const U32 TEXT = TAG4('T', 'E', 'X', 'T');
    const U32 MODL = TAG4('M', 'O', 'D', 'L');
    const U32 LODT = TAG4('L', 'O', 'D', 'T');
    const U32 SNDI = TAG4('S', 'N', 'D', 'I');
    const U32 JAW = TAG4('J', 'A', 'W', ' ');
    enum
    {
        DEFAULT = 0,
        TEXTURE = 1,
        MODEL = 3
    };

    // LODT: a count, then 32-byte entries keyed by their first word. Xbox
    // payloads are little-endian.
    U8 baseLod[4 + 2 * 32] = {};
    le32(baseLod, 2);
    le32(baseLod + 4, 0xA1);
    le32(baseLod + 8, 100);
    le32(baseLod + 36, 0xA2);
    le32(baseLod + 40, 200);
    U8 modLod[4 + 2 * 32] = {};
    le32(modLod, 2);
    le32(modLod + 4, 0xA2);
    le32(modLod + 8, 222);
    le32(modLod + 36, 0xA3);
    le32(modLod + 40, 333);

    SynthAsset baseAssets[] = {
        { 0x10, TEXT, "game text", 9 },
        { 0x20, TEXT, "untouched", 9 },
        { 0x30, LODT, baseLod, sizeof(baseLod) },
    };
    SynthLayer baseLayers[] = {
        { TEXTURE, 1, { 0x10 } },
        { DEFAULT, 2, { 0x20, 0x30 } },
    };
    SynthAsset modAssets[] = {
        { 0x10, MODL, "the mod's version", 17 },
        { 0x40, MODL, "a new model", 11 },
        { 0x50, LODT, modLod, sizeof(modLod) },
    };
    SynthLayer modLayers[] = {
        { DEFAULT, 1, { 0x50 } },
        { TEXTURE, 1, { 0x10 } },
        { MODEL, 1, { 0x40 } },
    };

    static SynthBuf base;
    static SynthBuf mod;
    synth_hip(&base, "XB", baseAssets, 3, baseLayers, 2);
    synth_hip(&mod, "XB", modAssets, 3, modLayers, 3);

    MergedBytes out;
    char why[256] = "";
    bool ok = merge_synth(base.bytes, base.size, mod.bytes, mod.size, &out, why, sizeof(why));
    check(ok, "an Xbox mod package merges into an Xbox game package");
    if (!ok)
    {
        printf("    %s\n", why);
        return;
    }

    ReadHip h;
    read_hip(out.data, out.size, &h);

    check(h.count == 4 && h.pcntAssets == 4,
          "the game's three assets and the mod's new one, and the count says so");
    check(h.holds(0x10, "the mod's version", 17) && h.find(0x10)->type == MODL,
          "an asset the mod has too takes the mod's data and type");
    check(h.holds(0x20, "untouched", 9), "an asset the mod lacks keeps the game's data");
    check(h.holds(0x40, "a new model", 11), "an asset only the mod has is added");
    check(h.find(0x50) == NULL, "the mod's LODT is merged, not added");

    bool idOrder = true;
    for (S32 i = 1; i < h.count; i++)
    {
        idOrder = idOrder && h.assets[i - 1].id < h.assets[i].id;
    }
    check(idOrder, "the asset table is in ID order");

    // The mod's TEXTURE and DEFAULT layers emptied out; its MODEL layer goes
    // between the game's TEXTURE and DEFAULT.
    check(h.layerCount == 3 && h.pcntLayers == 3, "a mod layer left with no assets is dropped");
    check(h.layerCount == 3 && h.layers[0].type == TEXTURE && h.layers[0].count == 1 &&
              h.layers[0].ids[0] == 0x10 && h.layers[1].type == MODEL &&
              h.layers[1].ids[0] == 0x40 && h.layers[2].type == DEFAULT && h.layers[2].count == 2,
          "a replaced asset stays in the game's layer, and layers are in retail's type order");
    check(layers_readable(h), "every layer is one block from a 2K boundary, pads exact");

    const ReadAsset* lod = h.find(0x30);
    bool lodRight = lod != NULL && lod->size == 4 + 3 * 32;
    if (lodRight)
    {
        const U8* t = out.data + lod->offset;
        lodRight = rd_le(t) == 3 && rd_le(t + 4) == 0xA1 && rd_le(t + 8) == 100 &&
                   rd_le(t + 36) == 0xA2 && rd_le(t + 40) == 222 && rd_le(t + 68) == 0xA3 &&
                   rd_le(t + 72) == 333;
    }
    check(lodRight, "LODT: a mod entry replaces the game's with the same model, moved to the end");
    free(out.data);

    // SNDI on the Xbox: three lists, 44-byte entries keyed by the sound at 24.
    // A mod entry replaces only within its own list.
    U8 baseSndi[12 + 3 * 44] = {};
    le32(baseSndi, 2);
    le32(baseSndi + 4, 1);
    le32(baseSndi + 8, 0);
    le32(baseSndi + 12 + 24, 0x111);
    le32(baseSndi + 56 + 24, 0x222);
    le32(baseSndi + 100 + 24, 0x111);
    baseSndi[56] = 0xBB;
    U8 modSndi[12 + 44] = {};
    le32(modSndi, 1);
    le32(modSndi + 12 + 24, 0x222);
    modSndi[12] = 0xCC;

    // JAW: a count, (sound, start, length) per entry, then the data.
    U8 baseJaw[4 + 2 * 12 + 8] = {};
    le32(baseJaw, 2);
    le32(baseJaw + 4, 0x701);
    le32(baseJaw + 8, 0);
    le32(baseJaw + 12, 3);
    le32(baseJaw + 16, 0x702);
    le32(baseJaw + 20, 4);
    le32(baseJaw + 24, 4);
    memcpy(baseJaw + 28, "abc\0wxyz", 8);
    U8 modJaw[4 + 12 + 5] = {};
    le32(modJaw, 1);
    le32(modJaw + 4, 0x701);
    le32(modJaw + 8, 0);
    le32(modJaw + 12, 5);
    memcpy(modJaw + 16, "HELLO", 5);

    SynthAsset baseSounds[] = { { 0x60, SNDI, baseSndi, sizeof(baseSndi) },
                                { 0x70, JAW, baseJaw, sizeof(baseJaw) } };
    SynthLayer soundLayer[] = { { DEFAULT, 2, { 0x60, 0x70 } } };
    SynthAsset modSounds[] = { { 0x61, SNDI, modSndi, sizeof(modSndi) },
                               { 0x71, JAW, modJaw, sizeof(modJaw) } };
    SynthLayer modSoundLayer[] = { { DEFAULT, 2, { 0x61, 0x71 } } };
    synth_hip(&base, "XB", baseSounds, 2, soundLayer, 1);
    synth_hip(&mod, "XB", modSounds, 2, modSoundLayer, 1);

    ok = merge_synth(base.bytes, base.size, mod.bytes, mod.size, &out, why, sizeof(why));
    check(ok, "sound tables merge");
    if (ok)
    {
        read_hip(out.data, out.size, &h);
        const ReadAsset* sndi = h.find(0x60);
        bool sndiRight = h.count == 2 && sndi != NULL && sndi->size == 12 + 3 * 44;
        if (sndiRight)
        {
            const U8* t = out.data + sndi->offset;
            sndiRight = rd_le(t) == 2 && rd_le(t + 4) == 1 && rd_le(t + 8) == 0 &&
                        rd_le(t + 12 + 24) == 0x111 && rd_le(t + 56 + 24) == 0x222 &&
                        t[56] == 0xCC && rd_le(t + 100 + 24) == 0x111;
        }
        check(sndiRight, "SNDI: the mod's sound replaces the game's in the same list only");

        const ReadAsset* jaw = h.find(0x70);
        bool jawRight = jaw != NULL;
        if (jawRight)
        {
            const U8* t = out.data + jaw->offset;
            const U8* rawData = t + 4 + 2 * 12;
            jawRight = rd_le(t) == 2 && rd_le(t + 4) == 0x702 && rd_le(t + 12) == 4 &&
                       memcmp(rawData + rd_le(t + 8), "wxyz", 4) == 0 && rd_le(t + 16) == 0x701 &&
                       rd_le(t + 24) == 5 && memcmp(rawData + rd_le(t + 20), "HELLO", 5) == 0;
        }
        check(jawRight, "JAW: the mod's mouth data replaces the game's for the same sound");
        free(out.data);
    }

    // GameCube SNDI: four header words, the second filler; 100-byte entries
    // keyed by the sound at 96, big-endian.
    U8 gcBase[16 + 100] = {};
    gcBase[3] = 1;
    memset(gcBase + 4, 0xCD, 4);
    gcBase[16 + 99] = 0x55;
    U8 gcMod[16 + 100] = {};
    gcMod[3] = 1;
    gcMod[16 + 99] = 0x66;
    SynthAsset gcBaseAssets[] = { { 0x80, SNDI, gcBase, sizeof(gcBase) } };
    SynthAsset gcModAssets[] = { { 0x81, SNDI, gcMod, sizeof(gcMod) } };
    SynthLayer gcBaseLayer[] = { { DEFAULT, 1, { 0x80 } } };
    SynthLayer gcModLayer[] = { { DEFAULT, 1, { 0x81 } } };
    synth_hip(&base, "GC", gcBaseAssets, 1, gcBaseLayer, 1);
    synth_hip(&mod, "GC", gcModAssets, 1, gcModLayer, 1);
    ok = merge_synth(base.bytes, base.size, mod.bytes, mod.size, &out, why, sizeof(why));
    if (ok)
    {
        read_hip(out.data, out.size, &h);
        const ReadAsset* sndi = h.find(0x80);
        const U8* t = sndi != NULL ? out.data + sndi->offset : NULL;
        check(t != NULL && sndi->size == 16 + 200 && rd_be(t) == 2 && rd_be(t + 4) == 0xCDCDCDCD &&
                  t[16 + 99] == 0x55 && t[116 + 99] == 0x66,
              "GameCube SNDI: entries with different sounds both stay, the filler word kept");
        free(out.data);
    }
    else
    {
        check(false, "GameCube sound tables merge");
    }

    // Refusals.
    synth_hip(&base, "XB", baseAssets, 3, baseLayers, 2);
    synth_hip(&mod, "GC", modAssets, 3, modLayers, 3);
    check(!merge_synth(base.bytes, base.size, mod.bytes, mod.size, &out, why, sizeof(why)) &&
              strstr(why, "GC") != NULL,
          "a GameCube mod package does not merge into an Xbox game package");

    synth_hip(&mod, "XB", modAssets, 3, modLayers, 3);
    check(!merge_synth(base.bytes, base.size / 2, mod.bytes, mod.size, &out, why, sizeof(why)),
          "a truncated game package is refused");

    SynthLayer stray[] = { { MODEL, 1, { 0x99 } } };
    synth_hip(&mod, "XB", modAssets, 3, stray, 1);
    check(!merge_synth(base.bytes, base.size, mod.bytes, mod.size, &out, why, sizeof(why)) &&
              strstr(why, "00000099") != NULL,
          "a layer naming an asset the package lacks is refused");
}

static void test_mod_manifest()
{
    printf("Heavy Mod Manager mod.json\n");

    char top[512];
    if (!scratch_dir("hmm", top, sizeof(top)))
    {
        check(false, "could not make a temp directory");
        return;
    }

    char files[600];
    snprintf(files, sizeof(files), "%s/files", top);
    iHostMakeDir(files);

    // A byte order mark, fields the reader steps over, escapes.
    static const char kJson[] =
        "\xEF\xBB\xBF{\"Game\":2,\"Nested\":{\"a\":[1,\"x\\\"y\",{}],\"b\":null},"
        "\"MergeFiles\":\"font.HIP\\nhb\\\\hb01.HIP\\r\\n\",\"DOLPatches\":\"\","
        "\"INIReplacements\":\"BOOT=hb02\\nName = caf\\u00e9\",\"IpsPatchBase64\":\"UEFUQ0g=\","
        "\"Version\":\"1.3\"}";
    write_scratch(top, "mod.json", kJson, sizeof(kJson) - 1);

    iModManifest m;
    check(iModManifestRead(files, &m), "the mod.json beside a mod's files/ folder is found");
    check(m.mergeFiles != NULL && strcmp(m.mergeFiles, "font.HIP\nhb\\hb01.HIP\r\n") == 0,
          "MergeFiles is read with its escapes decoded");
    check(m.iniReplacements != NULL &&
              strcmp(m.iniReplacements, "BOOT=hb02\nName = caf\xC3\xA9") == 0,
          "INIReplacements is read, \\u escapes as UTF-8");
    check(m.patchesExecutable, "a non-empty IPS patch is noticed");
    check(iModManifestMerges(&m, "FONT.hip") && iModManifestMerges(&m, "HB/hb01.hip") &&
              iModManifestMerges(&m, "hb\\HB01.HIP"),
          "a merge file matches ignoring case, '/' matching '\\', and the line's CR");
    check(!iModManifestMerges(&m, "hb01.HIP") && !iModManifestMerges(&m, "font.HIP2"),
          "but only the whole path");
    iModManifestFree(&m);

    char other[600];
    snprintf(other, sizeof(other), "%s/other", top);
    iHostMakeDir(other);
    check(!iModManifestRead(other, &m), "only files/ looks beside itself for mod.json");

    write_scratch(top, "mod.json", "{\"MergeFiles\":\"a\"", 17);
    check(!iModManifestRead(files, &m) && m.mergeFiles == NULL, "unterminated JSON is not read");

    char path[700];
    snprintf(path, sizeof(path), "%s/mod.json", top);
    iHostRemoveFile(path);
    iHostRemoveDir(other);
    iHostRemoveDir(files);
    iHostRemoveDir(top);

    static const char kIni[] = "# SB.INI\r\n"
                               "BOOT=HB00\r\n"
                               "G.TakeDamage = 1   # comment\r\n"
                               "TaskStatus = a\r\n"
                               "TaskStatus = b\r\n"
                               "ScenePlayerMapping = HB01 0\r\n"
                               "ScenePlayerMapping = HB02 0\r\n"
                               "NoMusic=0";
    size_t n;
    char* got = iModManifestApplyIni(kIni, sizeof(kIni) - 1,
                                     "boot = hb02\n"
                                     "G.TakeDamage=0 # off\n"
                                     "TaskStatus = c\n"
                                     "ScenePlayerMapping = HB02   1\n"
                                     "no equals sign\n",
                                     &n);
    static const char kWant[] = "# SB.INI\r\n"
                                "ScenePlayerMapping = HB01 0\r\n"
                                "NoMusic=0\n"
                                "boot=hb02\n"
                                "G.TakeDamage=0\n"
                                "TaskStatus=c\n"
                                "ScenePlayerMapping=HB02 1\n";
    check(got != NULL && n == strlen(got) && strcmp(got, kWant) == 0,
          "INIReplacements: same keys replaced ignoring case, lists as a whole, scene keys by "
          "scene, comments kept");
    free(got);
}

// The merge through iFileOpen: a mod with mod.json beside files/, its
// packages merged into the asset root's, its SB.INI lines applied.
static void test_file_mod_merge()
{
    printf("iFile Heavy Mod Manager mods\n");

    char base[512];
    char top[512];
    char lower[512];
    if (!scratch_dir("mergebase", base, sizeof(base)) ||
        !scratch_dir("mergetop", top, sizeof(top)) ||
        !scratch_dir("mergelow", lower, sizeof(lower)))
    {
        check(false, "could not make the temp directories");
        return;
    }

    char topFiles[600];
    snprintf(topFiles, sizeof(topFiles), "%s/files", top);
    iHostMakeDir(topFiles);

    const U32 TEXT = TAG4('T', 'E', 'X', 'T');
    SynthAsset baseAssets[] = { { 0x10, TEXT, "game one", 8 }, { 0x20, TEXT, "game two", 8 } };
    SynthLayer baseLayers[] = { { 0, 2, { 0x10, 0x20 } } };
    SynthAsset topAssets[] = { { 0x10, TEXT, "top one", 7 }, { 0x30, TEXT, "top three", 9 } };
    SynthLayer topLayers[] = { { 0, 2, { 0x10, 0x30 } } };
    SynthAsset lowAssets[] = { { 0x10, TEXT, "low one", 7 }, { 0x20, TEXT, "low two", 7 } };
    SynthLayer lowLayers[] = { { 0, 2, { 0x10, 0x20 } } };

    static SynthBuf b;
    synth_hip(&b, "XB", baseAssets, 2, baseLayers, 1);
    bool made = write_scratch(base, "PKG.HIP", b.bytes, b.size) &&
                write_scratch(base, "WHOLE.HIP", b.bytes, b.size) &&
                write_scratch(base, "SB.INI", "BOOT=HB00\nNoMusic=0\n", 20);
    synth_hip(&b, "XB", topAssets, 2, topLayers, 1);
    made = made && write_scratch(topFiles, "pkg.hip", b.bytes, b.size) &&
           write_scratch(topFiles, "whole.hip", b.bytes, b.size);
    synth_hip(&b, "XB", lowAssets, 2, lowLayers, 1);
    made = made && write_scratch(lower, "PKG.HIP", b.bytes, b.size);

    static const char kTopJson[] = "{\"MergeFiles\":\"pkg.HIP\",\"INIReplacements\":\"BOOT=hb02\"}";
    static const char kLowJson[] = "{\"MergeFiles\":\"PKG.HIP\",\"INIReplacements\":\"NoMusic=1\"}";
    made = made && write_scratch(top, "mod.json", kTopJson, sizeof(kTopJson) - 1) &&
           write_scratch(lower, "mod.json", kLowJson, sizeof(kLowJson) - 1);
    check(made, "the scratch asset and mod folders can be written");

    char setting[1200];
    snprintf(setting, sizeof(setting), "%s|%s", topFiles, lower);
    iHostSetEnv("BFBB_ASSETS", base);
    iHostSetEnv("BFBB_MOD", setting);
    iFileInit();
    iFileSetPath((char*)".");

    static U8 got[16384];
    tag_xFile file = {};
    bool opened = iFileOpen("PKG.HIP", 0, &file) == 0;
    check(opened, "a merged package opens");
    if (opened)
    {
        U32 size = iFileGetSize(&file);
        iFileRead(&file, got, size < sizeof(got) ? size : sizeof(got));
        iFileClose(&file);

        ReadHip h;
        read_hip(got, size, &h);
        check(h.count == 3 && h.holds(0x10, "top one", 7),
              "the higher mod's change to an asset wins over the lower mod's");
        check(h.holds(0x20, "low two", 7),
              "the lower mod's change shows where the higher has none");
        check(h.holds(0x30, "top three", 9), "and the higher mod's new asset is added");
    }

    opened = iFileOpen("WHOLE.HIP", 0, &file) == 0;
    if (opened)
    {
        U32 size = iFileGetSize(&file);
        iFileRead(&file, got, size < sizeof(got) ? size : sizeof(got));
        iFileClose(&file);
        ReadHip h;
        read_hip(got, size, &h);
        check(h.count == 2 && h.holds(0x30, "top three", 9) && h.find(0x20) == NULL,
              "a package not in MergeFiles replaces the game's whole");
    }
    else
    {
        check(false, "a package not in MergeFiles opens");
    }

    char ini[64];
    read_through_ifile("SB.INI", ini, sizeof(ini));
    check(strcmp(ini, "NoMusic=1\nBOOT=hb02\n") == 0, "both mods' SB.INI lines apply");

    iHostSetEnv("BFBB_ASSETS", NULL);
    iHostSetEnv("BFBB_MOD", NULL);
    iFileInit();
    iFileExit();

    const char* made_files[][2] = { { base, "PKG.HIP" },       { base, "WHOLE.HIP" },
                                    { base, "SB.INI" },        { topFiles, "pkg.hip" },
                                    { topFiles, "whole.hip" }, { top, "mod.json" },
                                    { lower, "PKG.HIP" },      { lower, "mod.json" } };
    char path[1200];
    for (size_t i = 0; i < sizeof(made_files) / sizeof(made_files[0]); i++)
    {
        snprintf(path, sizeof(path), "%s/%s", made_files[i][0], made_files[i][1]);
        iHostRemoveFile(path);
    }
    iHostRemoveDir(topFiles);
    iHostRemoveDir(top);
    iHostRemoveDir(lower);
    iHostRemoveDir(base);
}

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);

    // An assert writes to stderr and exits instead of opening a dialog.
    //
    // The MSVC runtime's default for a failed assert in a Debug build is a
    // modal message box, and a test run that stops for one looks exactly like a
    // test run that hangs -- which on a build machine is a job that runs until
    // its timeout with nothing to show for it. This is the whole of what a
    // headless assert needs.
#if defined(_WIN32) && defined(_DEBUG)
    _set_error_mode(_OUT_TO_STDERR);
#endif
    printf("bfbb PC platform layer selftest\n\n");

    // First: iConfig parses once per process, and this is what decides which
    // file that one parse reads.
    test_config();
    test_config_edit();
    test_config_model();
    test_config_model_bindings();
    test_screen();
    test_drawdist();
    test_boot();
    test_textpatch();

    test_time();
    test_math();
    test_mem();
    test_file();
    test_file_mod();
    test_file_mod_merge();
    test_mod_manifest();
    test_hip_merge();
    test_mod_folder();
    test_mods_model();
    test_save_file();
    test_saves_model();
    test_gcswap();
    test_gcmem();
    test_idtag();
    test_pad();
    test_talkbox_buttons();
    test_savegame();
    test_snd_dsp();
    test_snd_data();
    test_soundtrack();
#ifdef BFBB_AUDIO_BACKEND_SDL
    test_snd_mixer();
#endif
    test_snd();
    test_snd_rolloff();
    test_snd_pan();
    test_snd_reverb();
    test_hip();
    test_flykey();
    test_npc_ordering();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "passed", failures,
           failures == 1 ? "" : "s");

    return failures ? 1 : 0;
}
