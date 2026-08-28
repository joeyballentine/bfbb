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
    const S32 kMaxValue = 192;
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
        { "video", "width", "640",
          "The width the game renders at, in pixels. 640x480 is what the consoles\n"
          "; drew, and is what every texture, font and HUD element was authored for,\n"
          "; so anything larger magnifies the 2D art -- the 3D gets sharper, the\n"
          "; interface does not." },
        { "video", "height", "480",
          "The height, in pixels. A ratio other than 4:3 is widescreen and needs no\n"
          "; switch of its own: the camera keeps its vertical view and widens, so\n"
          "; 1280x720 shows more of the world to the left and right, and the\n"
          "; interface keeps its shape in the middle rather than stretching." },
        { "xbox", "glow", "on",
          "The full-screen glow -- what people call the Xbox version's bloom." },
        { "xbox", "distortion", "on", "The Cruise Bubble's screen warp." },
        { "xbox", "snapshot", "on",
          "The loading screen stands on a still of the level you just left,\n"
          "; rather than on the background texture the GameCube release uses." },
        { "xbox", "reverb", "on", "Cave reverb, in the Mermalair and the caves." },
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
    fprintf(f, "; Written because there was no config.ini. Every value here is the\n");
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
            if (strcmp(section, "video") == 0)
            {
                fprintf(f, "; The size the game renders at. The window opens at this size too,\n");
                fprintf(f, "; but the two are independent -- the picture is scaled to whatever\n");
                fprintf(f, "; the window becomes.\n");
                fprintf(f, ";\n");
                fprintf(f, "; Any resolution works, and anything that is not 4:3 is widescreen.\n");
            }
            else if (strcmp(section, "xbox") == 0)
            {
                fprintf(f, "; Things the Xbox release did that the GameCube release did not.\n");
                fprintf(f, "; Turning one off leaves the GameCube behaviour in its place.\n");
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
