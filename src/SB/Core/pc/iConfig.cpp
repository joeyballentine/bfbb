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
        { "video", "framerate", "60",
          "How many frames a second the game runs at.\n"
          ";\n"
          "; This is the simulation rate as well as the picture rate -- the port runs\n"
          "; one update per frame, as the consoles did -- so it is not purely\n"
          "; cosmetic. 60 is what the GameCube video interface gave the game, and\n"
          "; what every number in its source was tuned against.\n"
          ";\n"
          ";   60          the console rate.\n"
          ";   display     the refresh rate of the monitor the game is on.\n"
          ";   0 or off    no cap. As fast as the machine will go, or as fast as\n"
          ";               the display allows when vsync is on below.\n"
          ";   any number  that many frames a second.\n"
          ";\n"
          "; Above 60 the game is in territory it was never run in. The rates that\n"
          "; were written per frame rather than per second have been converted --\n"
          "; see docs/UNCAPPED.md for which, and why -- but anything still keyed to\n"
          "; a frame count rather than to elapsed time runs faster than it should." },
        { "video", "vsync", "on",
          "Wait for the display before showing a finished frame.\n"
          ";\n"
          "; On, a frame is never torn in half, and the frame rate cannot exceed the\n"
          "; refresh rate of the monitor whatever framerate says above. Off, frames\n"
          "; are shown the moment they are finished, which is what framerate needs\n"
          "; to be free of the display -- and what tears.\n"
          ";\n"
          "; This is separate from framerate on purpose. Vsync decides whether a\n"
          "; frame is whole; framerate decides how fast the game runs. On a 144 Hz\n"
          "; monitor, vsync on with framerate 60 gives sixty whole frames a second." },
        { "video", "draw_distance", "on",
          "Draw everything, however far away. The consoles stopped drawing an\n"
          "; object past a distance the level author set, swapped distant ones for\n"
          "; lower-detail models, and clipped the world at 400 units. Off restores\n"
          "; those limits exactly. Fog is unaffected, and nothing extra is\n"
          "; simulated -- only drawn." },
        { "video", "msaa", "4",
          "How many samples each pixel is drawn with.\n"
          ";\n"
          "; Antialiasing. The consoles drew one sample per pixel into a picture\n"
          "; a quarter the size, so their stepped edges were never as visible as\n"
          "; yours are. More samples means smoother edges on everything the game\n"
          "; draws.\n"
          ";\n"
          "; 1 is off. 2, 4 and 8 are the counts most cards offer, and 4 is the\n"
          "; default. A count the card will not grant falls back to off." },
        { "video", "alpha_to_coverage", "on",
          "Draw the see-through edges of foliage, fences, grates and cave walls\n"
          "; as coverage rather than blending them.\n"
          ";\n"
          "; Those are solid shapes punched out of a texture, and magnifying one\n"
          "; blurs the edge where the shape ends. Blended, that blur mixes into\n"
          "; whatever was drawn there first rather than into what is really\n"
          "; behind the surface -- which is what makes the sky show through the\n"
          "; edges of a cave wall. Drawn as coverage the same blur is spread over\n"
          "; the pixel's samples, and what belongs behind is drawn into the ones\n"
          "; the shape does not cover.\n"
          ";\n"
          "; This needs msaa above: there is nowhere to put the coverage at one\n"
          "; sample per pixel, so with msaa = 1 this does nothing whatever it is\n"
          "; set to. Some cards do not offer it at all, and it is off on those." },
        { "video", "per_pixel_lighting", "on",
          "Work out the lighting on a character once per pixel instead of once\n"
          "; per corner of a triangle.\n"
          ";\n"
          "; The consoles lit each vertex and let the hardware blend between them\n"
          "; across the triangle. On a low-polygon model that shows: a curved\n"
          "; surface lights in visible flat facets, and a highlight that should\n"
          "; slide over a face instead crawls between corners. Lighting each pixel\n"
          "; on its own makes the same lights land smoothly.\n"
          ";\n"
          "; It changes nothing the artists drew. The lights, their colours, and\n"
          "; the baked-in colour of the level are all exactly as they were -- this\n"
          "; is only where the sum is worked out.\n"
          ";\n"
          "; The level itself barely moves, because its lighting was baked into\n"
          "; the artwork rather than computed. Characters and objects are what\n"
          "; this is for." },
        { "video", "shadow_resolution", "auto",
          "How sharp the shadows under characters are.\n"
          ";\n"
          "; A character's shadow is drawn by rendering the character into a\n"
          "; square texture and projecting that onto the ground. The consoles made\n"
          "; that texture 256 pixels across against a 480-pixel picture, so it was\n"
          "; never magnified much. A PC draws the same shadow across far more\n"
          "; pixels, and the edge goes blocky.\n"
          ";\n"
          "; auto keeps the consoles' ratio -- half the render height, rounded up\n"
          "; to a power of two -- so the shadow stays as sharp relative to the\n"
          "; picture as it was on a television, at whatever size is set above:\n"
          ";\n"
          ";   480 -> 256    720 -> 512    1080 -> 1024    2160 -> 2048\n"
          ";\n"
          "; A power of two from 64 to 4096 pins it instead. Larger costs video\n"
          "; memory and a little time per shadow; 256 is what the consoles used.\n"
          "; The texture is never made larger than the render size either way." },
        { "xbox", "glow", "on",
          "The full-screen glow, usually called the Xbox version's bloom." },
        { "xbox", "distortion", "on", "The Cruise Bubble's screen warp." },
        { "xbox", "snapshot", "on",
          "Use a still of the level you just left as the loading screen\n"
          "; background, instead of the GameCube release's background texture." },
        { "xbox", "reverb", "on", "Cave reverb, in the Mermalair and the caves." },
        { "input", "controller", "auto",
          "Which controller to play with, when more than one is plugged in.\n"
          ";\n"
          "; auto uses the first one Windows lists that is actually there, so a\n"
          "; single controller works whichever slot it landed in. A number from 1\n"
          "; to 4 pins the game to that slot and ignores the rest -- which is what\n"
          "; to set when a wheel, a flight stick or a dormant wireless receiver is\n"
          "; holding slot 1 and the pad you want is behind it.\n"
          ";\n"
          "; The keyboard covers this controller whenever nothing is on it." },
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
            return b->pad;
        }

        b = findBinding(key, kKeyboardSection);
        return b != NULL ? b->key : NULL;
    }

    // [pad] and [keyboard]. One line per game button, and the grammar spelled
    // out once at the top of each -- a comment per binding would be thirty
    // paragraphs all saying the same thing.
    void writeBindingSection(FILE* f, const char* section, const char* inputs, const char* extra)
    {
        fprintf(f, "\n[%s]\n", section);
        fprintf(f, "; What presses each button the game reads. Left of the '=' is the\n");
        fprintf(f, "; button, named as the console named it, and the trailing comment\n");
        fprintf(f, "; says what it does. Right of it is what presses it:\n");
        fprintf(f, ";\n");
        fprintf(f, ";   %s\n", inputs);
        fprintf(f, ";\n");
        fprintf(f, "; ',' between two of them means either one on its own. '+' means\n");
        fprintf(f, "; both at once, and '!' means NOT held. Nothing after the '=' leaves\n");
        fprintf(f, "; the button unpressable, which is how to turn one off.\n");
        fprintf(f, ";\n");
        fprintf(f, "%s", extra);

        for (S32 i = 0; i < kPadBindButtonCount; i++)
        {
            const iPadBindButton* b = &kPadBindButtons[i];
            const char* value = (section == kPadSection) ? b->pad : b->key;

            if (b->does != NULL)
            {
                fprintf(f, "%-6s = %-9s ; %s\n", b->name, value, b->does);
            }
            else
            {
                fprintf(f, "%-6s = %s\n", b->name, value);
            }
        }
    }

    void writeBindings(FILE* f)
    {
        writeBindingSection(
            f, kPadSection,
            "a b x y lb rb lt rt ls rs back start dpup dpdown dpleft dpright",
            "; The '!' is why l1 and l2 can share one trigger below: the GameCube\n"
            "; had three shoulders where the game wants four, so it read Z as a\n"
            "; modifier, and rb stands in for Z here.\n"
            ";\n"
            "; lb, ls and rs start out bound to nothing -- the GameCube has no\n"
            "; fourth shoulder and no stick clicks, so they are free.\n"
            ";\n"
            "; The sticks are not remappable: the left one moves and the right one\n"
            "; turns the camera, as they did on the console.\n"
            ";\n");

        writeBindingSection(
            f, kKeyboardSection,
            "any key by name -- letters, digits, space, enter, tab, escape,\n"
            ";   backspace, shift, ctrl, alt, up, down, left, right, f1 to f12,\n"
            ";   and the numeric keypad as numpad0 to numpad9",
            "; The keyboard is only read while nothing is on the controller chosen\n"
            "; above. WASD moves and IJKL turns the camera; those are not\n"
            "; remappable, for the same reason the sticks are not.\n"
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
        for (S32 i = 0; i < kPadBindButtonCount; i++)
        {
            if (!fileHas(kPadSection, kPadBindButtons[i].name))
            {
                missing++;
            }
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
        fprintf(f, "; Settings added by a newer build of the port.\n");
        fprintf(f, ";\n");
        fprintf(f, "; Every value below is its default, so this block changes nothing\n");
        fprintf(f, "; about what the game was already doing. The [section] headers repeat\n");
        fprintf(f, "; the ones above on purpose -- the settings are new, the sections they\n");
        fprintf(f, "; belong to are not.\n");

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

        // The bindings, which are one line each and carry their grammar in the
        // header the file already has further up.
        for (S32 pass = 0; pass < 2; pass++)
        {
            const char* sect = pass == 0 ? kPadSection : kKeyboardSection;
            bool wroteHeader = false;
            for (S32 i = 0; i < kPadBindButtonCount; i++)
            {
                const iPadBindButton* b = &kPadBindButtons[i];
                if (fileHas(sect, b->name))
                {
                    continue;
                }
                if (!wroteHeader)
                {
                    fprintf(f, "\n[%s]\n", sect);
                    wroteHeader = true;
                }
                const char* value = pass == 0 ? b->pad : b->key;
                if (b->does != NULL)
                {
                    fprintf(f, "%-6s = %-9s ; %s\n", b->name, value, b->does);
                }
                else
                {
                    fprintf(f, "%-6s = %s\n", b->name, value);
                }
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
            else if (strcmp(section, "input") == 0)
            {
                fprintf(f, "; Which controller the game reads. What each of its buttons does\n");
                fprintf(f, "; is further down, in [pad] and [keyboard].\n");
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
