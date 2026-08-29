// config.ini. The argument for a second parser is in iConfig.h.

#include "iConfig.h"

#include "iHost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    // Fixed storage, because this runs before xMemMgr has a heap and before
    // RenderWare has an allocator -- that is the whole point of the file. A
    // settings file is a few dozen lines; a file that exceeds these is
    // truncated with a message rather than silently losing its tail.
    const S32 kMaxEntries = 64;
    const S32 kMaxKey = 64;
    const S32 kMaxValue = 512;
    const S32 kMaxLine = 512;
    const S32 kMaxPath = 512;

    struct Entry
    {
        char key[kMaxKey];
        char value[kMaxValue];
    };

    Entry sEntries[kMaxEntries];
    S32 sCount;
    S32 sLoaded;
    char sPath[kMaxPath];
    S32 sHavePath;

    // Every setting the port has, its default, and what it is for.
    //
    // This one table does three jobs, and that it does all three is the reason
    // it exists rather than three lists that could disagree:
    //
    //   1. A key not in it is REPORTED at load rather than ignored. The
    //      accessors all take a fallback, so an unknown key is otherwise
    //      indistinguishable from an absent one at the point of use -- which
    //      means `glwo = off` would read as "the glow is on" and nothing
    //      anywhere would say why.
    //   2. It is the default. An accessor whose key is missing from the file
    //      answers from here, NOT from the fallback its caller passed, so a
    //      default cannot be changed in one place and not the other.
    //   3. It is what gets written when there is no config.ini, comments and
    //      all, so the generated file documents itself.
    //
    // `section` is written as a [header] when it changes, so keep entries that
    // share one together.
    struct Setting
    {
        const char* section;
        const char* name;
        const char* value;
        const char* comment;
    };

    const Setting kSettings[] = {
        { "assets", "path", "",
          "Folder holding the game's files -- the one with boot.HIP, FONT.HIP and\n"
          "; fmv/ directly inside it. Not a parent folder, not a disc image. No\n"
          "; assets ship with the port, so nothing runs until this is set.\n"
          ";\n"
          "; Forward or back slashes both work. Spaces need no quotes: everything\n"
          "; after the '=' is the path.\n"
          ";\n"
          "; Empty means the folder the game was started from. BFBB_ASSETS\n"
          "; overrides this when it is set." },
        { "video", "mode", "fullscreen",
          "fullscreen, borderless or windowed.\n"
          ";\n"
          "; This does not change the render size below. The picture is scaled to\n"
          "; fit, so the game can render at 640x480 and fill a 4K display, or\n"
          "; render higher and be sampled back down.\n"
          ";\n"
          "; Aspect ratio comes from the render size, not the display. A 4:3 size\n"
          "; on a 16:9 monitor gets black bars at the sides. Use a 16:9 size to\n"
          "; fill a 16:9 screen.\n"
          ";\n"
          ";   fullscreen  exclusive fullscreen at the current desktop resolution.\n"
          ";   borderless  a borderless window covering one monitor. Nothing else\n"
          ";               on the desktop is disturbed and alt-tab is instant.\n"
          ";   windowed    a normal window, opened at the size below." },
        { "video", "width", "640",
          "Render width in pixels. The consoles drew 640x480, and every texture,\n"
          "; font and HUD element was made for it, so larger sizes sharpen the 3D\n"
          "; but only magnify the 2D art." },
        { "video", "height", "480",
          "Render height in pixels. Any ratio other than 4:3 is widescreen; there\n"
          "; is no separate switch. The camera keeps its vertical view and widens,\n"
          "; so 1280x720 shows more to the left and right instead of stretching." },
        { "video", "ui", "pillarbox",
          "Where the interface goes on a screen that is not 4:3.\n"
          ";\n"
          ";   pillarbox  the whole interface stays in a centred 4:3 box, as the\n"
          ";              console drew it, sitting in from each side.\n"
          ";   native     the HUD moves out to the real screen edges at the size\n"
          ";              it would have had. Each counter moves with its own icon,\n"
          ";              and anything centred stays centred.\n"
          ";\n"
          "; Menus, textboxes and cutscene overlays stay in the 4:3 box either way\n"
          "; -- they are full-screen art with nothing to anchor. At 4:3 the two\n"
          "; settings do the same thing." },
        { "video", "draw_distance", "on",
          "Draw everything, however far away. The consoles stopped drawing an\n"
          "; object past a distance the level author set, swapped distant ones for\n"
          "; lower-detail models, and clipped the world at 400 units. Off restores\n"
          "; those limits exactly. Fog is unaffected, and nothing extra is\n"
          "; simulated -- only drawn." },
        { "video", "alpha_cutout", "on",
          "Draw texture transparency as a hard edge instead of a fade, on scenery\n"
          "; the game draws as solid.\n"
          ";\n"
          "; Foliage, fences, grates and cave walls are shapes punched out of a\n"
          "; texture. The consoles blended the half-transparent pixels at the edge\n"
          "; of the shape, which is a one-pixel soft edge at 640x480 but six pixels\n"
          "; of sky showing through the level at 4K. On, those pixels are drawn\n"
          "; solid up to a cutoff and dropped after it, so the silhouette matches\n"
          "; the artwork at any resolution.\n"
          ";\n"
          "; off matches the consoles. A number from 1 to 255 sets the cutoff:\n"
          "; lower keeps more of the band and stays softer, higher trims the shape.\n"
          "; on means 128. Glass, water, particles and the interface are unaffected\n"
          "; -- they ask to be blended, and are." },
        { "xbox", "glow", "on",
          "The full-screen glow, usually called the Xbox version's bloom." },
        { "xbox", "distortion", "on", "The Cruise Bubble's screen warp." },
        { "xbox", "snapshot", "on",
          "Use a still of the level you just left as the loading screen\n"
          "; background, instead of the GameCube release's background texture." },
        { "xbox", "reverb", "on", "Cave reverb, in the Mermalair and the caves." },
        { "audio", "soundtrack", "",
          "Folder of your own music files to play instead of the game's. Empty\n"
          "; uses the game's music.\n"
          ";\n"
          "; The game's music is mono, as is every sound in it, so this is mainly\n"
          "; how to get a stereo soundtrack in. Any sample rate and channel count\n"
          "; works; the mixer resamples as it already does. WAVE always works, and\n"
          "; a build made with FFmpeg reads anything else FFmpeg can.\n"
          ";\n"
          "; Files are matched to tracks by name: music_00_hb_44.flac needs nothing\n"
          "; else. Files named after the music instead -- as a soundtrack release\n"
          "; is -- need a soundtrack.txt beside them, one 'asset name = file' per\n"
          "; line.\n"
          ";\n"
          "; Looping tracks loop where the game's version ended, not where the file\n"
          "; does, so a release with a full ending still loops like the console\n"
          "; did." },
        { "text", "platform_wording", "on",
          "The game's text comes from the Xbox release and mentions Xbox hardware:\n"
          "; the pause menu offers to reboot to the dashboard, autosaves warn about\n"
          "; turning off your console, and saves are called memory card slots --\n"
          "; here they are a folder. On, that text is rewritten as it loads; the\n"
          "; files on disk are never touched. Off leaves it as the disc has it." },
    };

    const size_t kSettingCount = sizeof(kSettings) / sizeof(kSettings[0]);

    void lowerInPlace(char* s)
    {
        for (; *s != '\0'; s++)
        {
            if (*s >= 'A' && *s <= 'Z')
            {
                *s = (char)(*s + ('a' - 'A'));
            }
        }
    }

    char* trim(char* s)
    {
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        {
            s++;
        }

        if (*s == '\0')
        {
            return s;
        }

        char* end = s + strlen(s) - 1;
        while (end > s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
        {
            end--;
        }
        end[1] = '\0';

        return s;
    }

    // The table's entry for "section.name", or NULL.
    const Setting* findSetting(const char* key)
    {
        for (size_t i = 0; i < kSettingCount; i++)
        {
            char full[kMaxKey];
            snprintf(full, sizeof(full), "%s.%s", kSettings[i].section, kSettings[i].name);
            if (iHostStrCaseCmp(full, key) == 0)
            {
                return &kSettings[i];
            }
        }
        return NULL;
    }

    const Entry* findEntry(const char* key)
    {
        for (S32 i = 0; i < sCount; i++)
        {
            if (iHostStrCaseCmp(sEntries[i].key, key) == 0)
            {
                return &sEntries[i];
            }
        }
        return NULL;
    }

    // What a query should read, in order: the file, then the table's default,
    // then the caller's fallback for a key that is in neither. The middle step
    // is what keeps the written defaults and the running defaults the same.
    const char* valueOf(const char* key)
    {
        const Entry* e = findEntry(key);
        if (e != NULL)
        {
            return e->value;
        }

        const Setting* s = findSetting(key);
        return s != NULL ? s->value : NULL;
    }

    // One line of the file. `section` is updated by a [header] and is what a
    // bare key is qualified with.
    void parseLine(char* line, char* section, size_t sectionSize, const char* path, S32 lineNo)
    {
        // Comments. Both spellings, because both are what people type into a
        // file called config.ini.
        char* comment = strchr(line, ';');
        if (comment != NULL)
        {
            *comment = '\0';
        }
        comment = strchr(line, '#');
        if (comment != NULL)
        {
            *comment = '\0';
        }

        char* s = trim(line);
        if (*s == '\0')
        {
            return;
        }

        if (*s == '[')
        {
            char* close = strchr(s, ']');
            if (close == NULL)
            {
                printf("bfbb: %s:%d: section header with no ']', ignored\n", path, (int)lineNo);
                return;
            }
            *close = '\0';
            snprintf(section, sectionSize, "%s", trim(s + 1));
            lowerInPlace(section);
            return;
        }

        char* eq = strchr(s, '=');
        if (eq == NULL)
        {
            printf("bfbb: %s:%d: no '=', ignored: %s\n", path, (int)lineNo, s);
            return;
        }

        *eq = '\0';
        char* name = trim(s);
        char* value = trim(eq + 1);

        if (*name == '\0')
        {
            printf("bfbb: %s:%d: no key before '=', ignored\n", path, (int)lineNo);
            return;
        }

        if (sCount == kMaxEntries)
        {
            printf("bfbb: %s:%d: more than %d settings, the rest are ignored\n", path, (int)lineNo,
                   (int)kMaxEntries);
            return;
        }

        Entry* e = &sEntries[sCount];
        if (section[0] != '\0')
        {
            snprintf(e->key, sizeof(e->key), "%s.%s", section, name);
        }
        else
        {
            snprintf(e->key, sizeof(e->key), "%s", name);
        }
        lowerInPlace(e->key);
        snprintf(e->value, sizeof(e->value), "%s", value);

        if (findSetting(e->key) == NULL)
        {
            printf("bfbb: %s:%d: unknown setting '%s', ignored\n", path, (int)lineNo, e->key);
            return;
        }

        sCount++;
    }

    bool readFile(const char* path)
    {
        FILE* f = fopen(path, "rb");
        if (f == NULL)
        {
            return false;
        }

        snprintf(sPath, sizeof(sPath), "%s", path);
        sHavePath = 1;

        char section[kMaxKey];
        section[0] = '\0';

        char line[kMaxLine];
        S32 lineNo = 0;
        while (fgets(line, sizeof(line), f) != NULL)
        {
            lineNo++;
            parseLine(line, section, sizeof(section), path, lineNo);
        }

        fclose(f);
        return true;
    }

    // Where to write one when none was found. Beside the executable, because
    // that is where it will still be next time -- the working directory is
    // searched first but is wherever the game happened to be started from, and
    // writing there would leave a file that stops being found the moment
    // someone starts it from somewhere else.
    void createDefaultFile()
    {
        char dir[kMaxPath];
        char path[kMaxPath];

        if (iHostExeDir(dir, sizeof(dir)))
        {
            snprintf(path, sizeof(path), "%s/config.ini", dir);
        }
        else
        {
            snprintf(path, sizeof(path), "config.ini");
        }

        if (iConfigWriteDefaults(path))
        {
            snprintf(sPath, sizeof(sPath), "%s", path);
            sHavePath = 1;
            printf("bfbb: no config.ini, so one was written with the defaults: %s\n", path);
        }
        else
        {
            printf("bfbb: no config.ini, and one could not be written to %s; using defaults\n",
                   path);
        }
        fflush(stdout);
    }
}

bool iConfigWriteDefaults(const char* path)
{
    // "wx" rather than "w": exclusive, so this can never overwrite a file --
    // including one that appeared between the load's read and here. The
    // argument for writing one at all is in iConfig.h.
    FILE* f = fopen(path, "wxb");
    if (f == NULL)
    {
        return false;
    }

    fprintf(f, "; Battle for Bikini Bottom, PC port -- settings.\n");
    fprintf(f, ";\n");
    fprintf(f, "; Written because there was no config.ini. Every value here is a\n");
    fprintf(f, "; default, so deleting this file changes nothing.\n");
    fprintf(f, ";\n");
    fprintf(f, "; Booleans take on/off, true/false, yes/no or 1/0.\n");

    const char* section = NULL;
    for (size_t i = 0; i < kSettingCount; i++)
    {
        if (section == NULL || strcmp(section, kSettings[i].section) != 0)
        {
            section = kSettings[i].section;
            fprintf(f, "\n[%s]\n", section);

            // The one thing the per-setting comments cannot say, said once
            // where the section is introduced.
            if (strcmp(section, "assets") == 0)
            {
                fprintf(f, "; Where the game's files are. This is the one setting the port\n");
                fprintf(f, "; cannot guess and cannot run without.\n");
            }
            else if (strcmp(section, "video") == 0)
            {
                fprintf(f, "; The size the game renders at, and how that picture is shown.\n");
                fprintf(f, "; The two are independent: the rendered picture is scaled onto\n");
                fprintf(f, "; whatever it lands on -- a window, a borderless window covering\n");
                fprintf(f, "; a monitor, or the display itself.\n");
                fprintf(f, ";\n");
                fprintf(f, "; Any resolution works, and anything not 4:3 is widescreen.\n");
            }
            else if (strcmp(section, "xbox") == 0)
            {
                fprintf(f, "; Things the Xbox release did that the GameCube release did not.\n");
                fprintf(f, "; Turning one off gives the GameCube behaviour instead.\n");
            }
            else if (strcmp(section, "text") == 0)
            {
                fprintf(f, "; The game's own text, which is the console release's. Nothing here\n");
                fprintf(f, "; edits the files on disk -- the text is rewritten as it loads.\n");
            }
        }

        fprintf(f, "\n; %s\n", kSettings[i].comment);
        fprintf(f, "%s = %s\n", kSettings[i].name, kSettings[i].value);
    }

    fclose(f);
    return true;
}

void iConfigLoad()
{
    if (sLoaded)
    {
        return;
    }

    // Set before the search, not after. Every accessor calls this function, so
    // a report printed during parsing that ever grows a getter behind it would
    // otherwise recurse.
    sLoaded = 1;

    const char* explicitPath = getenv("BFBB_CONFIG");
    if (explicitPath != NULL && explicitPath[0] != '\0')
    {
        if (!readFile(explicitPath))
        {
            // Named explicitly and not there: create it where it was asked
            // for. A path someone typed is a stronger statement about where
            // the file belongs than the search order is.
            if (iConfigWriteDefaults(explicitPath))
            {
                snprintf(sPath, sizeof(sPath), "%s", explicitPath);
                sHavePath = 1;
                printf("bfbb: BFBB_CONFIG names '%s', which did not exist; wrote the defaults "
                       "there\n",
                       explicitPath);
            }
            else
            {
                printf("bfbb: BFBB_CONFIG names '%s', which could not be opened or created; "
                       "using defaults\n",
                       explicitPath);
            }
            fflush(stdout);
        }
        return;
    }

    if (readFile("config.ini"))
    {
        return;
    }

    char beside[kMaxPath];
    if (iHostExeDir(beside, sizeof(beside)))
    {
        char path[kMaxPath];
        snprintf(path, sizeof(path), "%s/config.ini", beside);
        if (readFile(path))
        {
            return;
        }
    }

    createDefaultFile();
}

const char* iConfigPath()
{
    iConfigLoad();
    return sHavePath ? sPath : NULL;
}

S32 iConfigGetBool(const char* key, S32 def)
{
    iConfigLoad();

    const char* v = valueOf(key);
    if (v == NULL)
    {
        return def;
    }

    static const char* const kTrue[] = { "1", "true", "yes", "on" };
    static const char* const kFalse[] = { "0", "false", "no", "off" };

    for (size_t i = 0; i < sizeof(kTrue) / sizeof(kTrue[0]); i++)
    {
        if (iHostStrCaseCmp(v, kTrue[i]) == 0)
        {
            return TRUE;
        }
    }

    for (size_t i = 0; i < sizeof(kFalse) / sizeof(kFalse[0]); i++)
    {
        if (iHostStrCaseCmp(v, kFalse[i]) == 0)
        {
            return FALSE;
        }
    }

    printf("bfbb: config: '%s' is not on or off, using the default: %s\n", key, v);
    return def;
}

S32 iConfigGetInt(const char* key, S32 def)
{
    iConfigLoad();

    const char* v = valueOf(key);
    if (v == NULL)
    {
        return def;
    }

    char* end = NULL;
    long parsed = strtol(v, &end, 10);
    if (end == v || (end != NULL && *end != '\0'))
    {
        printf("bfbb: config: '%s' is not a whole number, using the default: %s\n", key, v);
        return def;
    }

    return (S32)parsed;
}

F32 iConfigGetFloat(const char* key, F32 def)
{
    iConfigLoad();

    const char* v = valueOf(key);
    if (v == NULL)
    {
        return def;
    }

    char* end = NULL;
    double parsed = strtod(v, &end);
    if (end == v || (end != NULL && *end != '\0'))
    {
        printf("bfbb: config: '%s' is not a number, using the default: %s\n", key, v);
        return def;
    }

    return (F32)parsed;
}

const char* iConfigGetString(const char* key, const char* def)
{
    iConfigLoad();

    const char* v = valueOf(key);
    return v != NULL ? v : def;
}
