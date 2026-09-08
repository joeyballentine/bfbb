// The settings table. What it is for is in iConfigTable.h.

#include "iConfigTable.h"

#include "iHost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Every setting the port has, its default, and what it is for.
//
// This one table does four jobs, and that it does all four is the reason it
// exists rather than four lists that could disagree:
//
//   1. A key not in it is REPORTED at load rather than ignored. The accessors
//      all take a fallback, so an unknown key is otherwise indistinguishable
//      from an absent one at the point of use -- which means `glwo = off`
//      would read as "the glow is on" and nothing anywhere would say why.
//   2. It is the default. An accessor whose key is missing from the file
//      answers from here, NOT from the fallback its caller passed, so a
//      default cannot be changed in one place and not the other.
//   3. It is what gets written when there is no config.ini, comments and all,
//      so the generated file documents itself.
//   4. It is the domain: what a value may be, which is what the configurator
//      draws a control from and checks a typed value against.
//
// `section` is written as a [header] when it changes, so keep entries that
// share one together.
//
// The last three columns are the domain -- kind, the words the value may be
// besides a kind value, and the numeric range. `kNone` is the range for a
// setting that has no range worth offering.
namespace
{
    const F32 kNone = 0.0f;
}

const iConfigSetting kConfigSettings[] = {
    { "assets", "path", "",
      "Folder holding boot.HIP, FONT.HIP and fmv/. Empty means the folder the\n"
      "; game was started from. BFBB_ASSETS overrides this.",
      ICONFIG_FOLDER, NULL, kNone, kNone },
    { "game", "boot", "",
      "Start straight in this scene, skipping the menu: a four-character scene\n"
      "; id, e.g. jf01. Empty starts at the menu. Overrides SB.INI's BOOT=.",
      ICONFIG_STRING, NULL, kNone, kNone },
    { "game", "intro_movies", "on",
      "Play the Nickelodeon, THQ and RenderWare logos before the title screen.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "game", "save_folder", "",
      "Folder to keep saves in. Empty uses this machine's own per-user data\n"
      "; folder. BFBB_SAVE_DIR overrides this.",
      ICONFIG_FOLDER, NULL, kNone, kNone },
    { "video", "mode", "fullscreen",
      "How the picture is presented: fullscreen, borderless, windowed.",
      ICONFIG_ENUM, "fullscreen|borderless|windowed", kNone, kNone },
    { "video", "width", "640", "Render width in pixels.", ICONFIG_INT, NULL, 320.0f, 15360.0f },
    { "video", "height", "480",
      "Render height in pixels. Anything other than 4:3 widens the view rather\n"
      "; than stretching it.",
      ICONFIG_INT, NULL, 240.0f, 8640.0f },
    { "video", "ui", "pillarbox",
      "Where the interface sits on a screen that is not 4:3: pillarbox (all of\n"
      "; it in a centred 4:3 box), native (the HUD out at the screen edges).",
      ICONFIG_ENUM, "pillarbox|native", kNone, kNone },
    { "video", "framerate", "60",
      "Frames a second, simulation and picture both: 60, display (the monitor's\n"
      "; rate), 0 or off for no cap, or any number.",
      ICONFIG_INT, "display|off", 0.0f, 1000.0f },
    { "video", "vsync", "on",
      "Wait for the display before showing a finished frame. Stops tearing, and\n"
      "; caps the rate at the refresh rate.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "video", "draw_distance", "on",
      "Draw everything however far away. Off restores the console's culling,\n"
      "; detail swaps and 400-unit world clip.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "video", "msaa", "4",
      "Samples per pixel, for smoother edges: 1 (off), 2, 4, 8. A count the card\n"
      "; will not grant falls back to off.",
      ICONFIG_ENUM, "1|2|4|8", kNone, kNone },
    { "video", "fov", "75",
      "Horizontal field of view in degrees, as the game measures it at 4:3.\n"
      "; A wider screen already shows more to the sides at the same number.",
      ICONFIG_FLOAT, NULL, 30.0f, 140.0f },
    { "video", "per_pixel_lighting", "off",
      "Light characters once per pixel instead of once per vertex.", ICONFIG_BOOL, NULL, kNone,
      kNone },
    { "video", "load_time", "1",
      "What to do about loads too fast to see: seconds to hold the loading\n"
      "; screen for, fancy to wipe the still off the loaded level instead,\n"
      "; or off for neither.",
      ICONFIG_FLOAT, "fancy|off", 0.0f, 30.0f },
    { "video", "shadow_resolution", "auto",
      "Character shadow texture size: auto (half the render height, rounded up\n"
      "; to a power of two), or a power of two from 64 to 4096.",
      ICONFIG_ENUM, "auto|64|128|256|512|1024|2048|4096", kNone, kNone },
    { "xbox", "glow", "on", "The full-screen glow, the Xbox version's bloom.", ICONFIG_BOOL, NULL,
      kNone, kNone },
    { "xbox", "distortion", "on", "The Cruise Bubble's screen warp.", ICONFIG_BOOL, NULL, kNone,
      kNone },
    { "xbox", "snapshot", "on", "Use a still of the level you just left as the loading screen.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "xbox", "reverb", "on", "Cave reverb, in the Mermalair and the caves.", ICONFIG_BOOL, NULL,
      kNone, kNone },
    { "xbox", "sound_rolloff", "on",
      "Fade and pan a sound the way the Xbox does. Off uses the GameCube's\n"
      "; curves, which hold an ambient near full volume out to its radius -- the\n"
      "; Kelp Forest waterfall is much louder that way -- and which put a centred\n"
      "; sound 3 dB down, which the Xbox does not.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "fixes", "menu_rope", "on",
      "Draw the pause menu's bamboo frame so the rope shows at its corners.\n"
      "; Off is the console's frame, with the corners bare.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "fixes", "sky_clip", "on",
      "Shrink a skydome too big for its level's fog to fit inside the camera.\n"
      "; Off is the console's sky, which in Goo Lagoon's pier is clipped away.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "input", "controller", "auto",
      "Which controller to play with: auto (the first one present), or 1 to 4 to\n"
      "; pin it to that slot.",
      ICONFIG_INT, "auto", 1.0f, 4.0f },
    { "input", "preset", "auto",
      "Which console's controls to start from: auto (follows the pad plugged\n"
      "; in), xbox, ps2, gamecube. A line in [pad] wins over this.",
      ICONFIG_ENUM, "auto|xbox|ps2|gamecube", kNone, kNone },
    { "input", "deadzone", "auto",
      "How far a stick must move before the game sees it, as a percentage of\n"
      "; full deflection: auto (the controller's own), or 0 to 90.",
      ICONFIG_INT, "auto", 0.0f, 90.0f },
    { "input", "camera_sensitivity", "1.0",
      "How fast the right stick turns and pitches the camera, as a multiple\n"
      "; of the game's own speed.",
      ICONFIG_FLOAT, NULL, 0.1f, 5.0f },
    { "input", "button_icons", "auto",
      "Which controller's buttons the prompts draw: auto, xbox, gamecube, ps2,\n"
      "; off (the ones on the disc), or a folder name under buttons/. The glyph\n"
      "; follows your binding, not the console named here.",
      ICONFIG_STRING, "auto|xbox|gamecube|ps2|off", kNone, kNone },
    { "audio", "soundtrack", "",
      "Folder of your own music to play instead of the game's; empty uses the\n"
      "; game's. Files are matched to tracks by asset name, or by a\n"
      "; soundtrack.txt beside them holding one 'asset name = file' per line.",
      ICONFIG_FOLDER, NULL, kNone, kNone },
    { "text", "font", "",
      "A TrueType file to draw the game's text with, or empty for the game's\n"
      "; own font.\n"
      ";\n"
      "; The game's fonts are texture atlases authored for 640x480, so above that\n"
      "; they are magnified and text is the first thing to go soft. This draws\n"
      "; the same letterforms from an outline at the size they are actually\n"
      "; drawn at. Layout, spacing, colour and every tag stay the game's.\n"
      ";\n"
      "; It draws the SpongeBob face and its numerals. The sans serif the\n"
      "; copyright screen and the memory card messages are in is a different\n"
      "; typeface and has its own setting below.\n"
      ";\n"
      "; No font ships with the port. The face the game itself used is\n"
      "; SpongeBoyTT1; any .ttf works. tools/getfont.py fetches one and prints\n"
      "; the line to paste here.",
      ICONFIG_FONT, NULL, kNone, kNone },
    { "text", "font_sans", "auto",
      "The same, for the sans serif the copyright screen, the memory card\n"
      "; messages and the controller messages are drawn in.\n"
      ";\n"
      "; auto follows the setting above and uses the system's own Arial, which\n"
      "; is the face that atlas is -- so it sharpens those screens without\n"
      "; changing what they look like. off leaves them as the game has them,\n"
      "; and a path names some other .ttf.\n"
      ";\n"
      "; The game's small system font is left alone either way: its glyphs are\n"
      "; hand-pixelled at 6x8, where an outline is not the same thing.",
      ICONFIG_FONT, "auto|off", kNone, kNone },
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
      "; the artwork had it in whatever this is, so it cannot move anything.",
      ICONFIG_INT, NULL, 0.0f, 8.0f },
    { "text", "font_padding", "auto",
      "How far to inset a glyph inside that box, in the game's own atlas\n"
      "; pixels, or auto to measure it.\n"
      ";\n"
      "; The box is measured by testing for any non-zero alpha, so it includes\n"
      "; the whole anti-aliased fringe and the original letter's solid body stops\n"
      "; short of it. An outline drawn to fill the box exactly reads as too\n"
      "; heavy. Larger is smaller letters; negative grows them past the box.\n"
      ";\n"
      "; auto tries every inset that is distinct at the size the text is being\n"
      "; drawn at and keeps the one whose letters land on the most of the same\n"
      "; pixels as the game's own.",
      ICONFIG_FLOAT, "auto", -8.0f, 8.0f },
    { "text", "font_weight", "auto",
      "How much to thicken that font's strokes, in the game's own atlas\n"
      "; pixels, or auto to measure it. 0 draws the face as it is.\n"
      ";\n"
      "; The game's atlases are hand-drawn and heavier than most text faces at\n"
      "; the same size, so a substitute can land the right size and still read\n"
      "; as too light beside the artwork around it.\n"
      ";\n"
      "; auto sweeps it together with font_padding and prints what it picked,\n"
      "; so a font drops in without being tuned by hand. tools/fontfit runs the\n"
      "; same sweep outside the game -- see src/SB/Core/pc/README.md.",
      ICONFIG_FLOAT, "auto", -8.0f, 8.0f },
    { "text", "font_sans_weight", "auto",
      "The same, for the font_sans face. Separate because the two atlases are\n"
      "; drawn at different weights: the sans one is the lighter of the two, and\n"
      "; a substitute for it usually needs nothing.",
      ICONFIG_FLOAT, "auto", -8.0f, 8.0f },
    { "text", "font_fit", "box",
      "How each glyph of that font is placed in the space the game's own\n"
      "; letter took up: box, width or natural.\n"
      ";\n"
      "; box stretches the glyph to fill it, which is exact -- every letter\n"
      "; lands where the artwork had it -- at the price of the face's own\n"
      "; proportions. width keeps the height and lets the width be the face's.\n"
      "; natural stops fitting: one size for the whole font, every letter on\n"
      "; one baseline, which is what a font normally looks like and no longer\n"
      "; exactly where the game drew it.",
      ICONFIG_ENUM, "box|width|natural", kNone, kNone },
    { "text", "font_sans_fit", "natural",
      "The same, for the font_sans face. natural by default: that face draws\n"
      "; the copyright notice and the memory card messages, where the game's\n"
      "; own letter positions are not worth keeping and a stretched sans looks\n"
      "; like it has been sat on.",
      ICONFIG_ENUM, "box|width|natural", kNone, kNone },
    { "text", "platform_wording", "on",
      "Rewrite the Xbox wording in the game's text -- dashboard, memory card\n"
      "; slots -- as it loads. The files on disk are never touched.",
      ICONFIG_BOOL, NULL, kNone, kNone },
};

const S32 kConfigSettingCount = (S32)(sizeof(kConfigSettings) / sizeof(kConfigSettings[0]));

const iConfigSetting* iConfigTableFind(const char* key)
{
    for (S32 i = 0; i < kConfigSettingCount; i++)
    {
        char full[128];
        snprintf(full, sizeof(full), "%s.%s", kConfigSettings[i].section, kConfigSettings[i].name);
        if (iHostStrCaseCmp(full, key) == 0)
        {
            return &kConfigSettings[i];
        }
    }
    return NULL;
}

namespace
{
    // Whether `value` is one of the '|'-separated words in `choices`.
    bool isChoice(const char* choices, const char* value)
    {
        if (choices == NULL)
        {
            return false;
        }

        const char* p = choices;
        while (*p != '\0')
        {
            const char* bar = strchr(p, '|');
            size_t n = (bar != NULL) ? (size_t)(bar - p) : strlen(p);

            char word[64];
            if (n < sizeof(word))
            {
                memcpy(word, p, n);
                word[n] = '\0';
                if (iHostStrCaseCmp(word, value) == 0)
                {
                    return true;
                }
            }

            if (bar == NULL)
            {
                break;
            }
            p = bar + 1;
        }

        return false;
    }

    bool isBool(const char* value)
    {
        static const char* const kWords[] = { "1",  "true", "yes", "on",
                                              "0",  "false", "no", "off" };
        for (size_t i = 0; i < sizeof(kWords) / sizeof(kWords[0]); i++)
        {
            if (iHostStrCaseCmp(value, kWords[i]) == 0)
            {
                return true;
            }
        }
        return false;
    }

    // The value as a number, or false when it is not one at all. Whole-number
    // strictness is the caller's, because "1.5" is a number and is still not a
    // frame count.
    bool asNumber(const char* value, double* out)
    {
        char* end = NULL;
        double parsed = strtod(value, &end);
        if (end == value || end == NULL || *end != '\0')
        {
            return false;
        }
        *out = parsed;
        return true;
    }

    void say(char* why, size_t whySize, const char* text)
    {
        if (why != NULL && whySize > 0)
        {
            snprintf(why, whySize, "%s", text);
        }
    }
} // namespace

bool iConfigTableValidate(const iConfigSetting* setting, const char* value, char* why,
                          size_t whySize)
{
    if (setting == NULL || value == NULL)
    {
        return false;
    }

    if (isChoice(setting->choices, value))
    {
        return true;
    }

    switch (setting->kind)
    {
    case ICONFIG_BOOL:
        if (!isBool(value))
        {
            say(why, whySize, "Has to be on or off.");
            return false;
        }
        return true;

    case ICONFIG_ENUM:
        // isChoice above was the whole test, so reaching here is a failure.
        {
            char text[256];
            snprintf(text, sizeof(text), "Has to be one of: %s", setting->choices);
            for (char* p = text; *p != '\0'; p++)
            {
                if (*p == '|')
                {
                    *p = ' ';
                }
            }
            say(why, whySize, text);
        }
        return false;

    case ICONFIG_INT:
    case ICONFIG_FLOAT:
    {
        double n = 0.0;
        if (!asNumber(value, &n))
        {
            char text[256];
            if (setting->choices != NULL)
            {
                snprintf(text, sizeof(text), "Has to be a number, or one of: %s", setting->choices);
                for (char* p = text; *p != '\0'; p++)
                {
                    if (*p == '|')
                    {
                        *p = ' ';
                    }
                }
            }
            else
            {
                snprintf(text, sizeof(text), "Has to be a number.");
            }
            say(why, whySize, text);
            return false;
        }

        if (setting->kind == ICONFIG_INT && n != (double)(long)n)
        {
            say(why, whySize, "Has to be a whole number.");
            return false;
        }

        if (setting->min != setting->max && (n < setting->min || n > setting->max))
        {
            char text[128];
            snprintf(text, sizeof(text), "Outside the usual range, %g to %g.", setting->min,
                     setting->max);
            say(why, whySize, text);
            return false;
        }

        return true;
    }

    case ICONFIG_STRING:
    case ICONFIG_FOLDER:
    case ICONFIG_FONT:
        // Anything, including empty. Whether the path is there is a question
        // for whoever is about to open it, not for the table -- a config
        // written on one machine and copied to another is ordinary.
        return true;
    }

    return true;
}

void iConfigTableWriteDefaults(FILE* f)
{
    const char* section = NULL;
    for (S32 i = 0; i < kConfigSettingCount; i++)
    {
        if (section == NULL || strcmp(section, kConfigSettings[i].section) != 0)
        {
            section = kConfigSettings[i].section;
            fprintf(f, "\n[%s]\n", section);
        }

        fprintf(f, "\n; %s\n", kConfigSettings[i].comment);
        fprintf(f, "%s = %s\n", kConfigSettings[i].name, kConfigSettings[i].value);
    }
}

void iConfigTableSummary(const iConfigSetting* setting, char* out, size_t outSize)
{
    if (out == NULL || outSize == 0)
    {
        return;
    }
    out[0] = '\0';
    if (setting == NULL || setting->comment == NULL)
    {
        return;
    }

    size_t written = 0;
    const char* p = setting->comment;

    while (*p != '\0')
    {
        const char* nl = strchr(p, '\n');
        size_t n = (nl != NULL) ? (size_t)(nl - p) : strlen(p);

        // Continuation lines carry the writer's "; " and are joined on with a
        // space. A line that is only "; " ends the summary: it is the blank
        // line before the paragraphs that belong in the file.
        if (p != setting->comment)
        {
            const char* text = p;
            size_t left = n;
            if (left >= 1 && text[0] == ';')
            {
                text++;
                left--;
            }
            while (left > 0 && (*text == ' ' || *text == '\t'))
            {
                text++;
                left--;
            }
            if (left == 0)
            {
                break;
            }

            if (written + 1 < outSize)
            {
                out[written++] = ' ';
            }
            p = text;
            n = left;
        }

        for (size_t i = 0; i < n && written + 1 < outSize; i++)
        {
            out[written++] = p[i];
        }

        if (nl == NULL)
        {
            break;
        }
        p = nl + 1;
    }

    out[written < outSize ? written : outSize - 1] = '\0';
}
