// config.ini. The argument for a second parser is in iConfig.h.

#include "iConfig.h"

#include "iHost.h"
#include "iPadBind.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    // Fixed storage, because this runs before xMemMgr has a heap and before
    // RenderWare has an allocator -- that is the whole point of the file. A
    // settings file is a few dozen lines; a file that exceeds these is
    // truncated with a message rather than silently losing its tail.
    // 64 was enough when every setting was a feature switch. The two binding
    // sections are one line per game button each, so a file that spells out
    // every binding is already 45 lines before anyone adds a comment.
    const S32 kMaxEntries = 128;
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
          "Folder holding boot.HIP, FONT.HIP and fmv/. Empty means the folder the\n"
          "; game was started from. BFBB_ASSETS overrides this." },
        { "video", "mode", "fullscreen",
          "How the picture is presented: fullscreen, borderless, windowed." },
        { "video", "width", "640", "Render width in pixels." },
        { "video", "height", "480",
          "Render height in pixels. Anything other than 4:3 widens the view rather\n"
          "; than stretching it." },
        { "video", "ui", "pillarbox",
          "Where the interface sits on a screen that is not 4:3: pillarbox (all of\n"
          "; it in a centred 4:3 box), native (the HUD out at the screen edges)." },
        { "video", "framerate", "60",
          "Frames a second, simulation and picture both: 60, display (the monitor's\n"
          "; rate), 0 or off for no cap, or any number." },
        { "video", "vsync", "on",
          "Wait for the display before showing a finished frame. Stops tearing, and\n"
          "; caps the rate at the refresh rate." },
        { "video", "draw_distance", "on",
          "Draw everything however far away. Off restores the console's culling,\n"
          "; detail swaps and 400-unit world clip." },
        { "video", "msaa", "4",
          "Samples per pixel, for smoother edges: 1 (off), 2, 4, 8. A count the card\n"
          "; will not grant falls back to off." },
        { "video", "per_pixel_lighting", "on",
          "Light characters once per pixel instead of once per vertex." },
        { "video", "load_time", "1",
          "What to do about loads too fast to see: seconds to hold the loading\n"
          "; screen for, fancy to wipe the still off the loaded level instead,\n"
          "; or off for neither." },
        { "video", "shadow_resolution", "auto",
          "Character shadow texture size: auto (half the render height, rounded up\n"
          "; to a power of two), or a power of two from 64 to 4096." },
        { "xbox", "glow", "on", "The full-screen glow, the Xbox version's bloom." },
        { "xbox", "distortion", "on", "The Cruise Bubble's screen warp." },
        { "xbox", "snapshot", "on",
          "Use a still of the level you just left as the loading screen." },
        { "xbox", "reverb", "on", "Cave reverb, in the Mermalair and the caves." },
        { "xbox", "sound_rolloff", "on",
          "Fade and pan a sound the way the Xbox does. Off uses the GameCube's\n"
          "; curves, which hold an ambient near full volume out to its radius -- the\n"
          "; Kelp Forest waterfall is much louder that way -- and which put a centred\n"
          "; sound 3 dB down, which the Xbox does not." },
        { "input", "controller", "auto",
          "Which controller to play with: auto (the first one present), or 1 to 4 to\n"
          "; pin it to that slot." },
        { "input", "preset", "auto",
          "Which console's controls to start from: auto (follows the pad plugged\n"
          "; in), xbox, ps2, gamecube. A line in [pad] wins over this." },
        { "input", "button_icons", "auto",
          "Which controller's buttons the prompts draw: auto, xbox, gamecube, ps2,\n"
          "; off (the ones on the disc), or a folder name under buttons/. The glyph\n"
          "; follows your binding, not the console named here." },
        { "audio", "soundtrack", "",
          "Folder of your own music to play instead of the game's; empty uses the\n"
          "; game's. Files are matched to tracks by asset name, or by a\n"
          "; soundtrack.txt beside them holding one 'asset name = file' per line." },
        { "text", "font", "",
          "A TrueType file to draw the game's text with, or empty for the game's\n"
          "; own font.\n"
          ";\n"
          "; The game's fonts are texture atlases authored for 640x480, so above that\n"
          "; they are magnified and text is the first thing to go soft. This draws\n"
          "; the same letterforms from an outline at the size they are actually\n"
          "; drawn at. Layout, spacing, colour and every tag stay the game's.\n"
          ";\n"
          "; No font ships with the port. The face the game itself used is\n"
          "; SpongeBoyTT1; any .ttf works. tools/getfont.py fetches one and prints\n"
          "; the line to paste here." },
        { "text", "font_upscale", "0",
          "How many times the game's own cell resolution to draw that font at,\n"
          "; or 0 to match the render size.\n"
          ";\n"
          "; The atlas was authored against a 480-line framebuffer and is drawn\n"
          "; magnified by however much taller the render size is, so 0 uses that\n"
          "; ratio -- one atlas pixel per screen pixel, which at 640x480 is 1 and\n"
          "; so exactly the softness the game shipped with. Below it the text is\n"
          "; blurrier than the display can show; above it the letters read as\n"
          "; crisper than the art around them.\n"
          ";\n"
          "; A sharpness setting, not a taste one: a glyph lands in exactly the box\n"
          "; the artwork had it in whatever this is, so it cannot move anything." },
        { "text", "font_padding", "0.5",
          "How far to inset a glyph inside that box, in the game's own atlas\n"
          "; pixels.\n"
          ";\n"
          "; The box is measured by testing for any non-zero alpha, so it includes\n"
          "; the whole anti-aliased fringe and the original letter's solid body stops\n"
          "; short of it. An outline drawn to fill the box exactly reads as too\n"
          "; heavy. Larger is smaller letters; negative grows them past the box." },
        { "text", "platform_wording", "on",
          "Rewrite the Xbox wording in the game's text -- dashboard, memory card\n"
          "; slots -- as it loads. The files on disk are never touched." },
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

    // The two binding sections are not in kSettings, because their contents
    // are one row per game button and that list already exists in
    // iPadBind.cpp. Splitting it in two so the table could stay a literal
    // would be the one thing the table exists to prevent.
    //
    // Everything the table does for a normal setting, these two functions do
    // for a binding: name a key as known, and answer it with the default the
    // generated file was written with.
    const char* kPadSection = "pad";
    const char* kKeyboardSection = "keyboard";

    // "pad.a" -> the row for "a", when `key` is in `section`. NULL otherwise.
    const iPadBindButton* findBinding(const char* key, const char* section)
    {
        size_t n = strlen(section);
        if (strncmp(key, section, n) != 0 || key[n] != '.')
        {
            return NULL;
        }
        return iPadBindFind(key + n + 1);
    }

    // The default binding for "pad.a" or "keyboard.a", or NULL for a key that
    // is neither.
    const char* bindingDefault(const char* key)
    {
        const iPadBindButton* b = findBinding(key, kPadSection);
        if (b != NULL)
        {
            return iPadBindPadDefault(b);
        }

        b = findBinding(key, kKeyboardSection);
        if (b != NULL)
        {
            return b->key;
        }

        return NULL;
    }

    // [pad] and [keyboard]. One line per game button, and the grammar spelled
    // out once at the top of each -- a comment per binding would be thirty
    // paragraphs all saying the same thing.
    void writeBindingSection(FILE* f, const char* section, const char* inputs, const char* extra)
    {
        fprintf(f, "\n[%s]\n", section);
        fprintf(f, "; What presses each game button. Left of the '=' is the button, right\n");
        fprintf(f, "; of it is what presses it:\n");
        fprintf(f, ";\n");
        fprintf(f, ";   %s\n", inputs);
        fprintf(f, ";\n");
        fprintf(f, "; ',' is either one, '+' is both at once, '!' is NOT held, and nothing\n");
        fprintf(f, "; at all leaves the button unpressable.\n");
        fprintf(f, ";\n");
        fprintf(f, "%s", extra);

        // The [pad] lines are written COMMENTED OUT, and that is not tidiness.
        // A line here beats input.preset, so a generated file that spelled all
        // fifteen out would pin the pad to whichever preset was current when
        // the file was made and leave the setting doing nothing ever after.
        // Commented, they are what they should be: a listing of every button
        // and what is on it now, with uncommenting one the way to take it over.
        //
        // [keyboard] has no preset behind it, so its lines are the values and
        // are written as values.
        bool pad = (section == kPadSection);

        if (pad)
        {
            fprintf(f, "; These are what input.preset gives you. Uncomment a line to take that\n");
            fprintf(f, "; one button over; the rest keep following the preset.\n");
            fprintf(f, ";\n");
        }

        for (S32 i = 0; i < kPadBindButtonCount; i++)
        {
            const iPadBindButton* b = &kPadBindButtons[i];
            const char* value = pad ? iPadBindPadDefault(b) : b->key;
            const char* lead = pad ? "; " : "";

            if (b->does != NULL)
            {
                fprintf(f, "%s%-6s = %-9s ; %s\n", lead, b->name, value, b->does);
            }
            else
            {
                fprintf(f, "%s%-6s = %s\n", lead, b->name, value);
            }
        }
    }

    void writeBindings(FILE* f)
    {
        writeBindingSection(
            f, kPadSection, "a b x y lb rb lt rt ls rs back start dpup dpdown dpleft dpright",
            "; The '!' is for the gamecube preset, where l1 and l2 share a trigger and\n"
            "; rb stands in for Z. ls and rs start bound to nothing. The sticks are\n"
            "; not remappable: left moves, right turns the camera.\n"
            ";\n");

        writeBindingSection(
            f, kKeyboardSection,
            "any key by name -- letters, digits, space, enter, tab, escape,\n"
            ";   backspace, shift, ctrl, alt, up, down, left, right, f1 to f12,\n"
            ";   and the numeric keypad as numpad0 to numpad9",
            "; Only read while nothing is on the controller. WASD moves and IJKL turns\n"
            "; the camera; those are not remappable.\n"
            ";\n");
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
        if (s != NULL)
        {
            return s->value;
        }

        return bindingDefault(key);
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

        if (findSetting(e->key) == NULL && bindingDefault(e->key) == NULL)
        {
            printf("bfbb: %s:%d: unknown setting '%s', ignored\n", path, (int)lineNo, e->key);
            return;
        }

        sCount++;
    }

    // Defined below, next to the writer it shares its layout with.
    void appendMissingSettings(const char* path);

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

        // Only now, with the whole file parsed: whether a key is missing is a
        // question about the file as a whole, not about any one line.
        appendMissingSettings(path);
        return true;
    }

    // Whether the file that was just read mentions a key at all. Distinct from
    // valueOf, which answers from the settings table when the file is silent --
    // which is exactly the case being looked for here.
    bool fileHas(const char* section, const char* name)
    {
        char key[kMaxKey];
        snprintf(key, sizeof(key), "%s.%s", section, name);
        lowerInPlace(key);
        return findEntry(key) != NULL;
    }

    // Settings this build has that the file does not.
    //
    // A config.ini written by an older build is missing every setting added
    // since, and iConfigWriteDefaults refuses to touch a file that already
    // exists -- so those settings stayed invisible, sitting at their defaults
    // with nothing anywhere naming them. That defeats the reason the file is
    // written at all: nobody should have to learn from documentation that a
    // setting exists.
    //
    // APPENDED, never rewritten. The existing bytes are not read back,
    // reordered or reformatted, so comments, ordering and hand edits survive
    // exactly, and the worst a failed write can do is leave a partial line at
    // the end -- which the parser reports by name rather than misreading.
    //
    // The section headers therefore repeat. The parser tracks the current
    // section as it goes and does not care, and one block under one banner is
    // easier to find than the same keys threaded back into place would be.
    void appendMissingSettings(const char* path)
    {
        S32 missing = 0;
        for (size_t i = 0; i < kSettingCount; i++)
        {
            if (!fileHas(kSettings[i].section, kSettings[i].name))
            {
                missing++;
            }
        }
        // Only [keyboard]. [pad] is neither counted here nor written below: a
        // generated file lists the pad bindings COMMENTED, so input.preset
        // stays in charge of them, and a comment is not an entry -- fileHas
        // would call all fifteen missing on every run and append fifteen more
        // commented lines each time. Nothing is lost by leaving them out, since
        // a pad binding the file does not mention is answered by the preset,
        // which is what the setting is for.
        for (S32 i = 0; i < kPadBindButtonCount; i++)
        {
            if (!fileHas(kKeyboardSection, kPadBindButtons[i].name))
            {
                missing++;
            }
        }

        if (missing == 0)
        {
            return;
        }

        FILE* f = fopen(path, "ab");
        if (f == NULL)
        {
            printf("bfbb: %s is missing %d setting%s this build has, and could not be "
                   "added to; they are at their defaults\n",
                   path, (int)missing, missing == 1 ? "" : "s");
            fflush(stdout);
            return;
        }

        fprintf(f, "\n");
        fprintf(f, "; ------------------------------------------------------------------\n");
        fprintf(f, "; Settings added by a newer build of the port. Every value below is\n");
        fprintf(f, "; its default, so this block changes nothing.\n");

        const char* section = NULL;
        for (size_t i = 0; i < kSettingCount; i++)
        {
            if (fileHas(kSettings[i].section, kSettings[i].name))
            {
                continue;
            }
            if (section == NULL || strcmp(section, kSettings[i].section) != 0)
            {
                section = kSettings[i].section;
                fprintf(f, "\n[%s]\n", section);
            }
            fprintf(f, "\n; %s\n", kSettings[i].comment);
            fprintf(f, "%s = %s\n", kSettings[i].name, kSettings[i].value);
        }

        // The keyboard bindings, one line each, carrying their grammar in the
        // header the file already has further up. [pad] is left alone, for the
        // reason given where the count is taken.
        bool wroteHeader = false;
        for (S32 i = 0; i < kPadBindButtonCount; i++)
        {
            const iPadBindButton* b = &kPadBindButtons[i];
            if (fileHas(kKeyboardSection, b->name))
            {
                continue;
            }
            if (!wroteHeader)
            {
                fprintf(f, "\n[%s]\n", kKeyboardSection);
                wroteHeader = true;
            }

            if (b->does != NULL)
            {
                fprintf(f, "%-6s = %-9s ; %s\n", b->name, b->key, b->does);
            }
            else
            {
                fprintf(f, "%-6s = %s\n", b->name, b->key);
            }
        }

        fclose(f);
        printf("bfbb: %s did not have %d setting%s this build has; appended at the "
               "defaults\n",
               path, (int)missing, missing == 1 ? "" : "s");
        fflush(stdout);
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
} // namespace

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
    fprintf(f, "; Every value here is the default, so deleting this file changes nothing.\n");
    fprintf(f, "; Booleans take on/off, true/false, yes/no or 1/0.\n");

    const char* section = NULL;
    for (size_t i = 0; i < kSettingCount; i++)
    {
        if (section == NULL || strcmp(section, kSettings[i].section) != 0)
        {
            section = kSettings[i].section;
            fprintf(f, "\n[%s]\n", section);
        }

        fprintf(f, "\n; %s\n", kSettings[i].comment);
        fprintf(f, "%s = %s\n", kSettings[i].name, kSettings[i].value);
    }

    writeBindings(f);

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
