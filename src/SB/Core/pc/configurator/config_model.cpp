// The configurator's model. What it is for is in config_model.h; what the
// program as a whole edits and why it is separate is in README.md.

#include "config_model.h"

#include "iConfigEdit.h"
#include "iHost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    // What the file said when it was opened, what the user has typed, and
    // whether the file mentioned it at all.
    //
    // `present` is not decoration. A setting the file does not name is one the
    // game answers from the table, and writing it out at its current value
    // would pin it to today's default -- so a value that was never touched is
    // left out of the file entirely, and one that was is written.
    struct Value
    {
        char text[kConfigModelMaxValue];
        bool present;
    };

    struct Model
    {
        char path[kConfigModelMaxPath];
        iConfigEditFile* file;

        Value* values;

        // Per setting, the index of the setting it is a detail of, or -1.
        // Resolved once rather than searched per row, since a front end asks
        // for it every time it lays out a section.
        S32* groups;

        const char** sectionNames;
        S32 sectionCount;

        bool dirty;
    };

    Model gModel;

    // The names the build gives the game, in the order they are tried. Two
    // rather than a #ifdef because that is the whole of the difference: the
    // Windows build writes bfbb.exe and every other one writes bfbb.
    const char* const kGameNames[] = { "bfbb.exe", "bfbb" };

    void collectSections()
    {
        gModel.sectionCount = 0;
        for (S32 i = 0; i < kConfigSettingCount; i++)
        {
            bool seen = false;
            for (S32 j = 0; j < gModel.sectionCount; j++)
            {
                if (strcmp(gModel.sectionNames[j], kConfigSettings[i].section) == 0)
                {
                    seen = true;
                    break;
                }
            }
            if (!seen)
            {
                gModel.sectionNames[gModel.sectionCount++] = kConfigSettings[i].section;
            }
        }
    }

    // Turn each row's `group` name into the index it refers to. A name that
    // does not resolve inside the same section leaves the setting ungrouped --
    // a wrong table entry hides a setting otherwise, and a setting shown in
    // the wrong place is easier to notice than one that is not shown at all.
    void resolveGroups()
    {
        for (S32 i = 0; i < kConfigSettingCount; i++)
        {
            gModel.groups[i] = -1;

            const char* group = kConfigSettings[i].group;
            if (group == NULL)
            {
                continue;
            }

            for (S32 j = 0; j < kConfigSettingCount; j++)
            {
                if (j != i && strcmp(kConfigSettings[j].section, kConfigSettings[i].section) == 0 &&
                    strcmp(kConfigSettings[j].name, group) == 0)
                {
                    gModel.groups[i] = j;
                    break;
                }
            }
        }
    }

    void loadValues()
    {
        for (S32 i = 0; i < kConfigSettingCount; i++)
        {
            const iConfigSetting* s = &kConfigSettings[i];
            const char* have = iConfigEditGet(gModel.file, s->section, s->name);

            gModel.values[i].present = (have != NULL);
            snprintf(gModel.values[i].text, kConfigModelMaxValue, "%s",
                     have != NULL ? have : s->value);
        }
    }

    // The same order the game searches in, so the configurator edits the file
    // the game will read: BFBB_CONFIG, then the working directory, then beside
    // the executable. An argument overrides all three, for editing one config
    // while a different one is in place.
    //
    // Beside the executable is also where a missing one is created, again as
    // the game does it.
    //
    // The answer is made ABSOLUTE, and not only so a status line names a file
    // someone can go and find. ConfigModelStartGame hands this path to the
    // game as BFBB_CONFIG and starts it in a different working directory, so a
    // relative "config.ini" would name a different file there -- and the game
    // creates one it cannot find, which would look like the settings being
    // ignored.
    void findPath(const char* fromCommandLine)
    {
        char picked[kConfigModelMaxPath];

        if (fromCommandLine != NULL && fromCommandLine[0] != '\0')
        {
            snprintf(picked, sizeof(picked), "%s", fromCommandLine);
        }
        else
        {
            const char* named = getenv("BFBB_CONFIG");
            char dir[kConfigModelMaxPath];

            if (named != NULL && named[0] != '\0')
            {
                snprintf(picked, sizeof(picked), "%s", named);
            }
            else if (iHostPathExists("config.ini"))
            {
                snprintf(picked, sizeof(picked), "config.ini");
            }
            else if (iHostExeDir(dir, sizeof(dir)))
            {
                snprintf(picked, sizeof(picked), "%s/config.ini", dir);
            }
            else
            {
                snprintf(picked, sizeof(picked), "config.ini");
            }
        }

        if (!iHostAbsolutePath(picked, gModel.path, sizeof(gModel.path)))
        {
            snprintf(gModel.path, sizeof(gModel.path), "%s", picked);
        }
    }

    // Write one at the defaults, for a first run where the game has not been
    // started yet. The banner is the game's, so the two files read the same;
    // the binding sections are not written, because those come from a table
    // this program does not link and the game fills them in from the preset.
    bool createFile(const char* path)
    {
        FILE* f = iHostCreateNewFile(path);
        if (f == NULL)
        {
            return false;
        }

        fprintf(f, "; Battle for Bikini Bottom, PC port -- settings.\n");
        fprintf(f, "; Every value here is the default, so deleting this file changes nothing.\n");
        fprintf(f, "; Booleans take on/off, true/false, yes/no or 1/0.\n");

        iConfigTableWriteDefaults(f);

        fclose(f);
        return true;
    }
} // namespace

bool ConfigModelOpen(const char* fromCommandLine, char* why, size_t whySize)
{
    memset(&gModel, 0, sizeof(gModel));

    // Sized off the table rather than fixed, so that adding settings to it
    // cannot walk off the end of either array. The section count is bounded by
    // the setting count, one section per setting being the worst case.
    gModel.values = (Value*)calloc((size_t)kConfigSettingCount, sizeof(Value));
    gModel.groups = (S32*)calloc((size_t)kConfigSettingCount, sizeof(S32));
    gModel.sectionNames = (const char**)calloc((size_t)kConfigSettingCount, sizeof(const char*));
    if (gModel.values == NULL || gModel.groups == NULL || gModel.sectionNames == NULL)
    {
        snprintf(why, whySize, "Out of memory.");
        ConfigModelClose();
        return false;
    }

    findPath(fromCommandLine);

    gModel.file = iConfigEditOpen(gModel.path);
    if (gModel.file == NULL)
    {
        // Not there, or not readable. Writing one is the same answer the game
        // gives, and leaves the two agreeing about where the file lives.
        if (!iHostPathExists(gModel.path) && createFile(gModel.path))
        {
            gModel.file = iConfigEditOpen(gModel.path);
        }
    }

    if (gModel.file == NULL)
    {
        snprintf(why, whySize,
                 "%s could not be read, and one could not be written there either.\n\n"
                 "Run the game once to have it write a config.ini, or start this with the "
                 "path to one.",
                 gModel.path);
        ConfigModelClose();
        return false;
    }

    collectSections();
    resolveGroups();
    loadValues();
    return true;
}

void ConfigModelClose()
{
    if (gModel.file != NULL)
    {
        iConfigEditClose(gModel.file);
    }
    free(gModel.values);
    free(gModel.groups);
    free((void*)gModel.sectionNames);
    memset(&gModel, 0, sizeof(gModel));
}

const char* ConfigModelPath()
{
    return gModel.path;
}

S32 ConfigModelSettingCount()
{
    return kConfigSettingCount;
}

const iConfigSetting* ConfigModelSetting(S32 i)
{
    return &kConfigSettings[i];
}

S32 ConfigModelSectionCount()
{
    return gModel.sectionCount;
}

const char* ConfigModelSectionName(S32 section)
{
    return gModel.sectionNames[section];
}

S32 ConfigModelGroupOf(S32 setting)
{
    return gModel.groups[setting];
}

S32 ConfigModelSectionOf(S32 setting)
{
    for (S32 j = 0; j < gModel.sectionCount; j++)
    {
        if (strcmp(gModel.sectionNames[j], kConfigSettings[setting].section) == 0)
        {
            return j;
        }
    }
    return 0;
}

const char* ConfigModelText(S32 setting)
{
    return gModel.values[setting].text;
}

void ConfigModelSetText(S32 setting, const char* text)
{
    Value* v = &gModel.values[setting];

    if (strcmp(v->text, text) == 0)
    {
        return;
    }

    snprintf(v->text, kConfigModelMaxValue, "%s", text);

    // Only a change makes it a value the file has to carry. A setting the file
    // never mentioned and that nobody touched stays out of it.
    v->present = true;
    gModel.dirty = true;
}

bool ConfigModelIsDefault(S32 setting)
{
    return iHostStrCaseCmp(gModel.values[setting].text, kConfigSettings[setting].value) == 0;
}

void ConfigModelDescribe(S32 setting, char* out, size_t outSize)
{
    char summary[512];
    iConfigTableSummary(&kConfigSettings[setting], summary, sizeof(summary));

    if (ConfigModelIsDefault(setting))
    {
        snprintf(out, outSize, "%s", summary);
        return;
    }

    const char* def = kConfigSettings[setting].value;
    snprintf(out, outSize, "%s  (default: %s)", summary, def[0] != '\0' ? def : "empty");
}

bool ConfigModelChoiceAt(const char* choices, S32 index, char* out, size_t outSize)
{
    if (choices == NULL)
    {
        return false;
    }

    const char* p = choices;
    for (S32 i = 0;; i++)
    {
        const char* bar = strchr(p, '|');
        size_t n = (bar != NULL) ? (size_t)(bar - p) : strlen(p);

        if (i == index)
        {
            if (n >= outSize)
            {
                n = outSize - 1;
            }
            memcpy(out, p, n);
            out[n] = '\0';
            return true;
        }

        if (bar == NULL)
        {
            return false;
        }
        p = bar + 1;
    }
}

bool ConfigModelWantsCombo(const iConfigSetting* s)
{
    return s->kind == ICONFIG_ENUM || s->choices != NULL;
}

bool ConfigModelWantsBrowse(const iConfigSetting* s)
{
    return s->kind == ICONFIG_FOLDER || s->kind == ICONFIG_FONT;
}

bool ConfigModelDirty()
{
    return gModel.dirty;
}

ConfigModelResult ConfigModelSave(char* why, size_t whySize, S32* badSetting, S32* badSection)
{
    for (S32 i = 0; i < kConfigSettingCount; i++)
    {
        char reason[256];
        reason[0] = '\0';

        if (iConfigTableValidate(&kConfigSettings[i], gModel.values[i].text, reason,
                                 sizeof(reason)))
        {
            continue;
        }

        snprintf(why, whySize, "%s.%s is \"%s\".\n\n%s", kConfigSettings[i].section,
                 kConfigSettings[i].name, gModel.values[i].text, reason);

        if (badSetting != NULL)
        {
            *badSetting = i;
        }
        if (badSection != NULL)
        {
            *badSection = ConfigModelSectionOf(i);
        }
        return CONFIG_MODEL_BAD_VALUE;
    }

    for (S32 i = 0; i < kConfigSettingCount; i++)
    {
        const iConfigSetting* s = &kConfigSettings[i];
        if (!gModel.values[i].present)
        {
            continue;
        }
        if (!iConfigEditSet(gModel.file, s->section, s->name, gModel.values[i].text))
        {
            snprintf(why, whySize, "There is no room left in config.ini for another line.");
            return CONFIG_MODEL_NO_ROOM;
        }
    }

    if (!iConfigEditSave(gModel.file, gModel.path))
    {
        snprintf(why, whySize, "%s could not be written to.", gModel.path);
        return CONFIG_MODEL_WRITE_FAILED;
    }

    gModel.dirty = false;
    return CONFIG_MODEL_OK;
}

bool ConfigModelResetSection(S32 section)
{
    bool changed = false;

    for (S32 i = 0; i < kConfigSettingCount; i++)
    {
        if (strcmp(kConfigSettings[i].section, gModel.sectionNames[section]) != 0)
        {
            continue;
        }
        if (!ConfigModelIsDefault(i))
        {
            ConfigModelSetText(i, kConfigSettings[i].value);
            changed = true;
        }
    }

    return changed;
}

bool ConfigModelGamePath(char* out, size_t outSize)
{
    char dir[kConfigModelMaxPath];
    if (!iHostExeDir(dir, sizeof(dir)))
    {
        return false;
    }

    for (size_t i = 0; i < sizeof(kGameNames) / sizeof(kGameNames[0]); i++)
    {
        snprintf(out, outSize, "%s/%s", dir, kGameNames[i]);
        if (iHostPathExists(out))
        {
            return true;
        }
    }

    return false;
}

bool ConfigModelStartGame(char* why, size_t whySize)
{
    char exe[kConfigModelMaxPath];
    char dir[kConfigModelMaxPath];

    dir[0] = '\0';
    iHostExeDir(dir, sizeof(dir));

    if (!ConfigModelGamePath(exe, sizeof(exe)))
    {
        snprintf(why, whySize, "There is no %s in %s.", kGameNames[0], dir);
        return false;
    }

    // iHostSetChildEnv rather than iHostSetEnv: on Windows those are two
    // different environments and only this one is inherited. The path is
    // absolute -- see findPath -- which matters because the child is started
    // in a different working directory.
    iHostSetChildEnv("BFBB_CONFIG", gModel.path);

    // Its own directory as the working directory, which is where it starts
    // from when someone runs it themselves. `assets path` being empty means
    // "the folder the game was started from", so this is not cosmetic.
    if (!iHostRunDetached(exe, dir))
    {
        snprintf(why, whySize, "%s would not start.", exe);
        return false;
    }

    return true;
}
