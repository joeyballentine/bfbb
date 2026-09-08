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
      "Folder holding boot.HIP, FONT.HIP and fmv/. Empty means the folder you\n"
      "; started the game from. BFBB_ASSETS overrides this.",
      ICONFIG_FOLDER, NULL, kNone, kNone },
    { "assets", "platform_wording", "on",
      "Rewrite the Xbox wording in the game's text as it loads: dashboard,\n"
      "; memory card slots. The port never changes the files on disk.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "game", "boot", "",
      "Start straight in this scene, skipping the menu: a four-character scene\n"
      "; id like jf01. Empty starts at the menu. Overrides SB.INI's BOOT=.",
      ICONFIG_STRING, NULL, kNone, kNone },
    { "game", "intro_movies", "on",
      "Play the Nickelodeon, THQ and RenderWare logos before the title screen.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "game", "save_folder", "",
      "Folder to keep saves in. Empty uses this machine's own per-user data\n"
      "; folder. BFBB_SAVE_DIR overrides this.",
      ICONFIG_FOLDER, NULL, kNone, kNone },
    { "video", "mode", "fullscreen", "Window mode: fullscreen, borderless, windowed.",
      ICONFIG_ENUM, "fullscreen|borderless|windowed", kNone, kNone },
    { "video", "width", "640", "Render width in pixels.", ICONFIG_INT, NULL, 320.0f, 15360.0f },
    { "video", "height", "480",
      "Render height in pixels. A shape other than 4:3 widens the view rather\n"
      "; than stretching it.",
      ICONFIG_INT, NULL, 240.0f, 8640.0f },
    { "video", "ui", "pillarbox",
      "Where the HUD sits on a screen wider than 4:3: pillarbox (in a centred\n"
      "; 4:3 box), native (out at the screen edges).",
      ICONFIG_ENUM, "pillarbox|native", kNone, kNone },
    { "video", "framerate", "60",
      "Frame rate cap, simulation and picture both: a number, display for the\n"
      "; monitor's rate, or 0 or off for none.",
      ICONFIG_INT, "display|off", 0.0f, 1000.0f },
    { "video", "vsync", "on",
      "Wait for the display before showing a finished frame. Stops tearing and\n"
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
      "Horizontal field of view in degrees, measured at 4:3. A wider screen\n"
      "; shows more to the sides at the same number.",
      ICONFIG_FLOAT, NULL, 30.0f, 140.0f },
    { "video", "per_pixel_lighting", "off",
      "Light characters once per pixel instead of once per vertex.", ICONFIG_BOOL, NULL, kNone,
      kNone },
    { "video", "backend", "auto",
      "Which renderer draws: auto, d3d9, d3d11, or gl3. auto takes the best one\n"
      "; this build has; gl3 is the one that runs off Windows.",
      ICONFIG_ENUM, "auto|d3d9|d3d11|gl3", kNone, kNone },
    { "video", "pipeline", "auto",
      "Which Direct3D 9 path draws: auto, shader, or fixed. fixed runs on cards\n"
      "; from before 2002 and loses the glow, the distortion and per-pixel light.",
      ICONFIG_ENUM, "auto|shader|fixed", kNone, kNone },
    { "video", "load_time", "1",
      "Seconds to hold the loading screen for, when a load is too fast to see.\n"
      "; fancy wipes the still off the loaded level instead. off does neither.",
      ICONFIG_FLOAT, "fancy|off", 0.0f, 30.0f },
    { "video", "shadow_resolution", "auto",
      "Character shadow texture size: auto (half the render height, rounded up\n"
      "; to a power of two), or a power of two from 64 to 4096.",
      ICONFIG_ENUM, "auto|64|128|256|512|1024|2048|4096", kNone, kNone },
    { "debug", "view", "off",
      "Draw a picture of the renderer's own buffers in the corner: depth (near\n"
      "; white to far black), bands (the same distance in contour bands), both.\n"
      "; OpenGL only.",
      ICONFIG_ENUM, "off|depth|bands|both", kNone, kNone },
    { "xbox", "glow", "on", "The full-screen glow, the Xbox version's bloom.", ICONFIG_BOOL, NULL,
      kNone, kNone },
    { "xbox", "distortion", "on", "The Cruise Bubble's screen warp.", ICONFIG_BOOL, NULL, kNone,
      kNone },
    { "xbox", "snapshot", "on", "Use a still of the previous level as the loading screen.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "xbox", "reverb", "on", "Cave reverb, in the Mermalair and the caves.", ICONFIG_BOOL, NULL,
      kNone, kNone },
    { "xbox", "sound_rolloff", "on",
      "Fade and pan a sound the way the Xbox does. Off uses the GameCube's\n"
      "; curves, which are louder for ambients and quieter for a centred sound.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "fixes", "menu_rope", "on",
      "Draw the pause menu's bamboo frame so the rope shows at its corners.\n"
      "; Off is the console's frame, with the corners bare.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "fixes", "sky_clip", "on",
      "Shrink a skydome too big for its level's fog to fit inside the camera.\n"
      "; Off is the console's sky, which the camera clips away in Goo Lagoon's\n"
      "; pier.",
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
      "How fast the right stick turns and pitches the camera. 1.0 is the\n"
      "; game's own speed.",
      ICONFIG_FLOAT, NULL, 0.1f, 5.0f },
    { "input", "button_icons", "auto",
      "Which controller's buttons the prompts draw: auto, xbox, gamecube, ps2,\n"
      "; off (the ones on the disc), or a folder name under buttons/. The glyph\n"
      "; follows your binding, not the console named here.",
      ICONFIG_STRING, "auto|xbox|gamecube|ps2|off", kNone, kNone },
    { "audio", "soundtrack", "",
      "Folder of your own music to play instead of the game's. Empty uses the\n"
      "; game's. The port matches a file to a track by asset name, or by a\n"
      "; soundtrack.txt beside them holding one 'asset name = file' per line.",
      ICONFIG_FOLDER, NULL, kNone, kNone },
    { "font", "face", "",
      "A .ttf to draw the game's text with, or empty for the game's own font.\n"
      "; Sharper than the game's atlas above 640x480. No font ships with the\n"
      "; port. tools/getfont.py fetches one and prints the line to paste here.",
      ICONFIG_FONT, NULL, kNone, kNone },
    { "font", "sans", "auto",
      "The same, for the sans serif on the copyright, memory card and\n"
      "; controller screens. auto uses the system's Arial, which is the face\n"
      "; that atlas is. off leaves those screens as the game has them.",
      ICONFIG_FONT, "auto|off", kNone, kNone },
    { "font", "upscale", "0",
      "How many times the game's own cell resolution to draw the face at, or\n"
      "; 0 to match the render size. Higher is sharper, and the glyph lands in\n"
      "; the same cell either way.",
      ICONFIG_INT, NULL, 0.0f, 8.0f },
    { "font", "padding", "auto",
      "How far to inset a glyph inside its atlas cell, in the game's own atlas\n"
      "; pixels, or auto to measure it. Larger is smaller letters. Negative\n"
      "; grows them past the cell.",
      ICONFIG_FLOAT, "auto", -8.0f, 8.0f },
    { "font", "weight", "auto",
      "How much to thicken the face's strokes, in the game's own atlas\n"
      "; pixels, or auto to measure it. 0 draws the face as it is.\n"
      "; tools/fontfit runs the same measurement outside the game.",
      ICONFIG_FLOAT, "auto", -8.0f, 8.0f },
    { "font", "sans_weight", "auto",
      "The same, for the sans face.", ICONFIG_FLOAT, "auto", -8.0f, 8.0f },
    { "font", "fit", "box",
      "How each glyph fills the space the game's own letter took: box (stretch\n"
      "; it to fit), width (keep the height, let the width be the face's),\n"
      "; natural (no fitting at all).",
      ICONFIG_ENUM, "box|width|natural", kNone, kNone },
    { "font", "sans_fit", "natural",
      "The same, for the sans face.", ICONFIG_ENUM, "box|width|natural", kNone, kNone },
    { "experimental", "hipoly_assets", "off",
      "Smooth the level and its models into curved surfaces as they load.\n"
      "; Loads take a few seconds longer.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "experimental", "hipoly_factor", "1.0",
      "How far the smoothing rounds things: scales every bulge and the\n"
      "; fillet. 2.0 rounds twice as far, 0 rounds nothing.",
      ICONFIG_FLOAT, NULL, 0.0f, 8.0f },
    { "experimental", "hipoly_target", "1.0",
      "Edge length the level is cut to, in units. Smaller is finer and slower.",
      ICONFIG_FLOAT, NULL, 0.05f, 8.0f },
    { "experimental", "hipoly_max_level", "6", "Most segments an edge is cut into.",
      ICONFIG_INT, NULL, 1.0f, 15.0f },
    { "experimental", "hipoly_passes", "2",
      "Times the smoothing runs over a model, each pass over the last pass's\n"
      "; mesh. More is smoother and slower to load. The level gets one pass.",
      ICONFIG_INT, NULL, 1.0f, 4.0f },
    { "experimental", "hipoly_crease", "60",
      "Degrees. Built surfaces folded sharper than this keep the fold.",
      ICONFIG_FLOAT, NULL, 0.0f, 180.0f },
    { "experimental", "hipoly_natural_crease", "100",
      "The same for rock, sand, kelp and other landscape.",
      ICONFIG_FLOAT, NULL, 0.0f, 180.0f },
    { "experimental", "hipoly_fillet", "0",
      "Units. How far from a sharp landscape fold the rounding reaches; 0 is off.\n"
      "; 1.5 rounds the rock's folds over, at the cost of some flattened detail.",
      ICONFIG_FLOAT, NULL, 0.0f, 10.0f },
    { "experimental", "hipoly_model_target", "0.25",
      "Edge length models are cut to, in units.",
      ICONFIG_FLOAT, NULL, 0.02f, 4.0f },
    { "experimental", "hipoly_inset", "0.5",
      "How much of a model's rounding cuts its corners in rather than bowing\n"
      "; its faces out: 0 bows out only, 1 cuts in only, 0.5 is half of each.",
      ICONFIG_FLOAT, NULL, 0.0f, 1.0f },
    { "experimental", "hipoly_flat_floors", "on",
      "Keep the level's floors and ceilings at their shipped height; only\n"
      "; steep faces round. Off lets gentle ground bow too, through any\n"
      "; decal lying on it.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "experimental", "hipoly_budget", "600000",
      "Most triangles a level's world may have; the cut coarsens past it.",
      ICONFIG_INT, NULL, 10000.0f, 4000000.0f },
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
