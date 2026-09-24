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
      "Folder containing boot.HIP, font.HIP and fmv/. Empty: the folder the game\n"
      "; starts in. BFBB_ASSETS overrides this.",
      ICONFIG_FOLDER, NULL, kNone, kNone },
    { "assets", "mod", "",
      "Mod folder with the same layout as the asset folder. Its files replace the\n"
      "; originals. Empty: none. BFBB_MOD overrides this.",
      ICONFIG_FOLDER, NULL, kNone, kNone },
    { "assets", "platform_wording", "on",
      "Replace Xbox wording (dashboard, memory cards) in the game's text. Files on\n"
      "; disk are not changed.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "assets", "code_menu", "on",
      "Use the port's menus, save list and settings screen. Off: the original\n"
      "; menus.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "game", "boot", "",
      "Start in this scene instead of the menu: a four-character scene ID, e.g.\n"
      "; jf01. Empty: start at the menu. Overrides SB.INI's BOOT=.",
      ICONFIG_STRING, NULL, kNone, kNone },
    { "game", "intro_movies", "on",
      "Play the Nickelodeon, THQ and RenderWare logos at startup.", ICONFIG_BOOL, NULL,
      kNone, kNone },
    { "game", "save_folder", "",
      "Folder for saves. Empty: the per-user data folder. BFBB_SAVE_DIR overrides\n"
      "; this.",
      ICONFIG_FOLDER, NULL, kNone, kNone },
    { "video", "mode", "borderless",
      "Window mode: borderless, fullscreen (exclusive) or windowed. Alt+Enter\n"
      "; switches between windowed and borderless.",
      ICONFIG_ENUM, "borderless|fullscreen|windowed", kNone, kNone },
    { "video", "profile",
#ifdef __ANDROID__
      "modern",
#else
      "custom",
#endif
      "Render size, HUD and draw distance: vanilla, modern, or custom (the lines below).",
      ICONFIG_ENUM, "custom|vanilla|modern", kNone, kNone },
    { "video", "width", "640", "Render width in pixels.", ICONFIG_INT, NULL, 320.0f, 15360.0f },
    { "video", "height", "480",
      "Render height in pixels. Wider than 4:3 shows more at the sides instead of\n"
      "; stretching.",
      ICONFIG_INT, NULL, 240.0f, 8640.0f },
    { "video", "ui", "pillarbox",
      "HUD position on wide screens: pillarbox (inside a centered 4:3 area) or\n"
      "; native (at the screen edges).",
      ICONFIG_ENUM, "pillarbox|native", kNone, kNone },
    { "video", "framerate", "60",
      "Frame rate cap: a number, display (the monitor's refresh rate), or 0 or off\n"
      "; for no cap.",
      ICONFIG_INT, "display|off", 0.0f, 1000.0f },
    { "video", "vsync", "on",
      "Wait for the display's refresh before showing a frame. Prevents tearing.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "video", "draw_distance", "on",
      "Draw objects at any distance. Off: the original culling, level of detail and\n"
      "; 400-unit clip.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "video", "msaa", "1",
      "Anti-aliasing samples per pixel: 1 (off), 2, 4 or 8. Falls back to off if\n"
      "; the GPU doesn't support the value.",
      ICONFIG_ENUM, "1|2|4|8", kNone, kNone },
    { "video", "fov", "75",
      "Horizontal field of view in degrees at 4:3. Wider screens show more at the\n"
      "; sides.",
      ICONFIG_FLOAT, NULL, 30.0f, 140.0f },
    { "video", "per_pixel_lighting", "off",
      "Light characters per pixel instead of per vertex.", ICONFIG_BOOL, NULL, kNone,
      kNone },
    { "video", "backend", "auto",
      "Renderer: auto, d3d9, d3d11, gl3 or vulkan. auto picks the best one in this\n"
      "; build. Only gl3 and vulkan run outside Windows.",
      ICONFIG_ENUM, "auto|d3d9|d3d11|gl3|vulkan", kNone, kNone },
    { "video", "pipeline", "auto",
      "Direct3D 9 rendering path: auto, shader or fixed. fixed supports pre-2002\n"
      "; GPUs but has no glow, distortion or per-pixel lighting.",
      ICONFIG_ENUM, "auto|shader|fixed", kNone, kNone },
    { "video", "load_time", "1",
      "Minimum loading screen time in seconds. fancy: wipe to the new level\n"
      "; instead. off: neither.",
      ICONFIG_FLOAT, "fancy|off", 0.0f, 30.0f },
    { "video", "shadow_resolution", "auto",
      "Character shadow texture size: auto (half the render height, rounded up to a\n"
      "; power of two) or a power of two from 64 to 4096.",
      ICONFIG_ENUM, "auto|64|128|256|512|1024|2048|4096", kNone, kNone },
    { "xbox", "glow", "on", "Xbox bloom effect.", ICONFIG_BOOL, NULL,
      kNone, kNone },
    { "xbox", "distortion", "on", "Cruise Bubble screen distortion.", ICONFIG_BOOL, NULL, kNone,
      kNone },
    { "xbox", "snapshot", "on", "Show a still of the previous level on the loading screen. Also used for save\n"
      "; pictures.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "xbox", "reverb", "on", "Reverb in caves and the Mermalair.", ICONFIG_BOOL, NULL,
      kNone, kNone },
    { "xbox", "sound_rolloff", "on",
      "Xbox volume and panning over distance. Off: GameCube curves (louder ambient\n"
      "; sounds, quieter centered sounds).",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "fixes", "menu_rope", "on",
      "Show the rope at the corners of the pause menu's bamboo frame. Off: the\n"
      "; original frame without it.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "fixes", "sky_clip", "on",
      "Shrink skydomes that are too large for the camera's far clip. Off: the\n"
      "; original sky, which is clipped on the Goo Lagoon pier.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "input", "controller", "auto",
      "Controller to use: auto (the first one found) or 1 to 4 for a specific slot.",
      ICONFIG_INT, "auto", 1.0f, 4.0f },
    { "input", "preset", "auto",
      "Default controls: auto (matches the connected controller), xbox, ps2 or\n"
      "; gamecube. Lines in [pad] override this.",
      ICONFIG_ENUM, "auto|xbox|ps2|gamecube", kNone, kNone },
    { "input", "deadzone", "auto",
      "Stick deadzone as a percentage of full movement: auto (the controller's own)\n"
      "; or 0 to 90.",
      ICONFIG_INT, "auto", 0.0f, 90.0f },
    { "input", "camera_sensitivity", "1.0",
      "Right-stick camera speed. 1.0 is the original speed.",
      ICONFIG_FLOAT, NULL, 0.1f, 5.0f },
    { "input", "button_icons", "auto",
      "Button prompts: auto, xbox, gamecube, ps2, off (the original prompts) or a\n"
      "; folder name under buttons/. Prompts follow your bindings.",
      ICONFIG_STRING, "auto|xbox|gamecube|ps2|off", kNone, kNone },
    { "input", "touch_controls", "auto",
      "On-screen touch controls: auto (on for Android), on or off.",
      ICONFIG_ENUM, "auto|on|off", kNone, kNone },
    { "audio", "soundtrack", "",
      "Folder with replacement music. Empty: the game's music. Files are matched to\n"
      "; tracks by asset name, or by 'asset name = file' lines in a soundtrack.txt.",
      ICONFIG_FOLDER, NULL, kNone, kNone },
    { "font", "face", "",
      "A .ttf font for the game's text. Empty: the original font. No font is\n"
      "; included; tools/getfont.py downloads one and prints the line to paste here.",
      ICONFIG_FONT, NULL, kNone, kNone },
    { "font", "sans", "auto",
      "Font for the copyright, memory card and controller screens. auto: the\n"
      "; system's Arial, the original typeface. off: the original rendering.",
      ICONFIG_FONT, "auto|off", kNone, kNone },
    { "font", "upscale", "0",
      "Font rendering scale as a multiple of the original glyph size, or 0 to match\n"
      "; the render size. Higher is sharper.",
      ICONFIG_INT, NULL, 0.0f, 8.0f },
    { "font", "padding", "auto",
      "Glyph inset within its cell, in original atlas pixels, or auto to measure\n"
      "; it. Larger values make smaller letters; negative values make larger ones.",
      ICONFIG_FLOAT, "auto", -8.0f, 8.0f },
    { "font", "fit", "box",
      "How glyphs fill the original letter space: box (stretch to fit), width (keep\n"
      "; the height, use the font's width) or natural (no fitting).",
      ICONFIG_ENUM, "box|width|natural", kNone, kNone },
    { "font", "sans_fit", "natural", "Same as fit, for the sans font.", ICONFIG_ENUM,
      "box|width|natural", kNone, kNone },
    { "experimental", "hipoly_assets", "off",
      "Smooth level and model geometry into curved surfaces at load. Adds a few\n"
      "; seconds to loads.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "experimental", "hipoly_factor", "1.0",
      "Amount of rounding. 2.0 doubles it; 0 disables it.",
      ICONFIG_FLOAT, NULL, 0.0f, 8.0f, "hipoly_assets" },
    { "experimental", "hipoly_target", "1.0",
      "Target edge length for the level, in units. Smaller is finer and slower.", ICONFIG_FLOAT,
      NULL, 0.05f, 8.0f, "hipoly_assets" },
    { "experimental", "hipoly_max_level", "6", "Maximum segments per edge.", ICONFIG_INT,
      NULL, 1.0f, 15.0f, "hipoly_assets" },
    { "experimental", "hipoly_passes", "2",
      "Smoothing passes for models. More is smoother and slower to load. The level\n"
      "; always gets one pass.",
      ICONFIG_INT, NULL, 1.0f, 4.0f, "hipoly_assets" },
    { "experimental", "hipoly_crease", "60",
      "Angle in degrees above which folds in built surfaces stay sharp.", ICONFIG_FLOAT, NULL, 0.0f,
      180.0f, "hipoly_assets" },
    { "experimental", "hipoly_natural_crease", "100",
      "Same as hipoly_crease, for rock, sand, kelp and other terrain.", ICONFIG_FLOAT, NULL, 0.0f, 180.0f,
      "hipoly_assets" },
    { "experimental", "hipoly_fillet", "0",
      "How far rounding reaches from sharp terrain folds, in units. 0: off. 1.5\n"
      "; rounds rock edges but flattens some detail.",
      ICONFIG_FLOAT, NULL, 0.0f, 10.0f, "hipoly_assets" },
    { "experimental", "hipoly_model_target", "0.25", "Target edge length for models, in units.",
      ICONFIG_FLOAT, NULL, 0.02f, 4.0f, "hipoly_assets" },
    { "experimental", "hipoly_inset", "0.75",
      "Rounding style for the level and models: 0 bulges faces outward, 1 cuts\n"
      "; corners inward.",
      ICONFIG_FLOAT, NULL, 0.0f, 1.0f, "hipoly_assets" },
    { "experimental", "hipoly_flat_floors", "covered",
      "Floors and ceilings that keep their original height: covered (those under a\n"
      "; decal or another face), all, or off (any may bulge).",
      ICONFIG_ENUM, "covered|all|off", kNone, kNone, "hipoly_assets" },
    { "experimental", "hipoly_budget", "600000",
      "Maximum triangles in a level's geometry. Smoothing is coarsened above it.", ICONFIG_INT, NULL,
      10000.0f, 4000000.0f, "hipoly_assets" },
    { "experimental", "world_lighting", "off",
      "Real-time level lighting instead of painted vertex colors: off, on (the\n"
      "; level's own light kit, if any) or bake (lights fitted to the vertex colors).\n"
      "; Loses the shading baked into the colors.",
      ICONFIG_ENUM, "off|on|bake", kNone, kNone },
    { "experimental", "world_light_contrast", "1",
      "Contrast between lit and shaded areas with bake lighting. 1: as fitted.\n"
      "; Higher values clip bright areas; each level has its own cap.",
      ICONFIG_FLOAT, NULL, 0.0f, 4.0f, "world_lighting" },
    { "experimental", "world_model_shade", "0.8",
      "Shade cast by placed models onto the level, computed at load. Requires toon\n"
      "; on and world_light_shadows off.",
      ICONFIG_FLOAT, NULL, 0.0f, 1.0f, "world_lighting" },
    { "experimental", "world_light_shadows", "off",
      "Shadows from the level's own geometry, computed at load, so the light can't\n"
      "; move. Low detail: only vertices can be shadowed.",
      ICONFIG_BOOL, NULL, kNone, kNone, "world_lighting" },
    { "experimental", "day_night_cycle", "off",
      "Length of a day in seconds, or off. Requires world lighting on and\n"
      "; world_light_shadows off.",
      ICONFIG_FLOAT, "off", 5.0f, 3600.0f, "world_lighting" },
    { "experimental", "solid_flat_props", "on",
      "Give flat props some thickness so outlines can go around them. Adds a few\n"
      "; hundred triangles per prop.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "experimental", "toon", "off",
      "Cartoon rendering: stepped lighting, stronger colors and outlines around\n"
      "; characters.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "experimental", "world_outline", "on",
      "Outline the level as well as models. Requires toon on. Draws the level\n"
      "; twice.",
      ICONFIG_BOOL, NULL, kNone, kNone, "toon" },
    { "experimental", "toon_all", "on",
      "Apply cel shading and outlines to all models, not only characters. The level\n"
      "; keeps its painted lighting.",
      ICONFIG_BOOL, NULL, kNone, kNone, "toon" },
    { "experimental", "toon_strength", "0.4",
      "Strength of the cel shading. 0: the original lighting. 1: hard bands with no\n"
      "; falloff.",
      ICONFIG_FLOAT, NULL, 0.0f, 1.0f, "toon" },
    { "experimental", "toon_bands", "3",
      "Number of lighting steps. 2: one lit tone and one shaded tone.",
      ICONFIG_FLOAT, NULL, 1.0f, 8.0f, "toon" },
    { "experimental", "toon_saturation", "1.5",
      "Color saturation boost at constant brightness. 1: unchanged.",
      ICONFIG_FLOAT, NULL, 0.0f, 3.0f, "toon" },
    { "experimental", "toon_light", "scene",
      "Where character shading is measured from: the brightest room light, the\n"
      "; character's front, or the camera.",
      ICONFIG_ENUM, "scene|face|camera", kNone, kNone, "toon" },
    { "experimental", "toon_colors", "0",
      "Shades per color on characters, to flatten upscaled textures. 0: unchanged.",
      ICONFIG_FLOAT, NULL, 0.0f, 32.0f, "toon" },
    { "experimental", "toon_outline", "0.05",
      "Character outline thickness in world units. SpongeBob is about 2 units tall.\n"
      "; 0: off.",
      ICONFIG_FLOAT, NULL, 0.0f, 0.2f, "toon" },
    { "experimental", "toon_room_level", "1.25",
      "How much a level's brightness affects shading. 1: as painted. Higher values\n"
      "; exaggerate it. Outdoor levels are unaffected.",
      ICONFIG_FLOAT, NULL, 1.0f, 4.0f, "toon" },
    { "experimental", "toon_ink", "0.45",
      "Outline darkness relative to the surface. Black stays black at any value.",
      ICONFIG_FLOAT, NULL, 0.0f, 1.0f, "toon" },
    { "experimental", "toon_ink_saturation", "1.5",
      "Outline color saturation. 1: the surface color, darkened. Higher values keep\n"
      "; more of the hue.",
      ICONFIG_FLOAT, NULL, 0.0f, 3.0f, "toon" },
    { "experimental", "toon_ink_gamma", "0.85",
      "Brightness curve for outlines. Below 1 brightens the midtones.",
      ICONFIG_FLOAT, NULL, 0.2f, 2.0f, "toon" },
    { "experimental", "toon_outline_min", "1.5",
      "Minimum outline thickness in pixels, so distant characters keep an outline.\n"
      "; 0: no minimum.",
      ICONFIG_FLOAT, NULL, 0.0f, 8.0f, "toon" },
    { "experimental", "toon_outline_max", "4",
      "Maximum outline thickness in pixels at 1440p, scaled for other resolutions.\n"
      "; 0: no maximum.",
      ICONFIG_FLOAT, NULL, 0.0f, 16.0f, "toon" },
    { "experimental", "toon_flat_strength", "0.75",
      "Cel shading strength on props made of flat panels.",
      ICONFIG_FLOAT, NULL, 0.0f, 1.0f, "toon" },
    { "experimental", "toon_sky_bright", "on",
      "Draw skydomes at full brightness, without their painted gradient. Off: the\n"
      "; original sky.",
      ICONFIG_BOOL, NULL, kNone, kNone, "toon" },
    { "experimental", "toon_tiki_rim", "off",
      "Rim lighting on tikis. Their welded corners make it show as a stripe.",
      ICONFIG_BOOL, NULL, kNone, kNone, "toon" },
    { "experimental", "toon_flat_smooth", "on",
      "Smooth normals on props made of flat panels, so shading and rim light change\n"
      "; gradually across faces.",
      ICONFIG_BOOL, NULL, kNone, kNone, "toon" },
    { "experimental", "toon_flat_rim", "on",
      "Rim lighting on props made of flat panels. Turn off for faceted props, where\n"
      "; it shows as square bands.",
      ICONFIG_BOOL, NULL, kNone, kNone, "toon" },
    { "experimental", "toon_outline_bias", "0",
      "How far outlines give way to what is behind the model, in outline widths.\n"
      "; -1: the model's own surface.",
      ICONFIG_FLOAT, NULL, -8.0f, 8.0f, "toon" },
    { "experimental", "toon_text_brightness", "1.35",
      "Brightness boost for text under the cartoon look. 1: unchanged.",
      ICONFIG_FLOAT, NULL, 0.25f, 4.0f, "toon" },
    { "experimental", "toon_text_saturation", "1.2",
      "Saturation boost for text under the cartoon look. 1: unchanged.",
      ICONFIG_FLOAT, NULL, 0.0f, 3.0f, "toon" },
    { "experimental", "toon_wrap", "0",
      "How far shading wraps around a character's far side, leaving room for a\n"
      "; second tone. 0: a hard edge.",
      ICONFIG_FLOAT, NULL, 0.0f, 1.0f, "toon" },
    { "experimental", "toon_rim", "0.25",
      "Brightness of the rim light on character silhouettes. Keeps dark characters\n"
      "; visible on dark backgrounds.",
      ICONFIG_FLOAT, NULL, 0.0f, 1.0f, "toon" },
    { "experimental", "toon_rim_blend", "room",
      "How rim light is applied: toward the room's color, screened over the\n"
      "; surface, or added.",
      ICONFIG_ENUM, "room|screen|add", kNone, kNone, "toon" },
    { "experimental", "toon_occlusion", "0",
      "How much a model's baked vertex colors darken its shading.",
      ICONFIG_FLOAT, NULL, 0.0f, 1.0f, "toon" },
    { "experimental", "toon_goo_wave", "0.35",
      "Apparent steepness of goo ripples, as a slope. 0: the level's actual ripple.",
      ICONFIG_FLOAT, NULL, 0.0f, 2.0f, "toon" },
    { "experimental", "toon_goo_gloss", "0.5",
      "Brightness of the goo's highlight under the cartoon look.",
      ICONFIG_FLOAT, NULL, 0.0f, 1.0f, "toon" },
    { "experimental", "toon_goo_gloss_edge", "0.92",
      "Where the goo highlight starts. Higher: a smaller, tighter highlight.",
      ICONFIG_FLOAT, NULL, 0.0f, 1.0f, "toon" },
    { "experimental", "toon_hardness", "0",
      "How sharply shading breaks at corners, against the smoothed normals outlines\n"
      "; need. 0: fully smoothed.",
      ICONFIG_FLOAT, NULL, 0.0f, 1.0f, "toon" },
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
        static const char* const kWords[] = { "1", "true", "yes", "on", "0", "false", "no", "off" };
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
            say(why, whySize, "Must be on or off.");
            return false;
        }
        return true;

    case ICONFIG_ENUM:
        // isChoice above was the whole test, so reaching here is a failure.
        {
            char text[256];
            snprintf(text, sizeof(text), "Must be one of: %s", setting->choices);
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
                snprintf(text, sizeof(text), "Must be a number or one of: %s", setting->choices);
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
                snprintf(text, sizeof(text), "Must be a number.");
            }
            say(why, whySize, text);
            return false;
        }

        if (setting->kind == ICONFIG_INT && n != (double)(long)n)
        {
            say(why, whySize, "Must be a whole number.");
            return false;
        }

        if (setting->min != setting->max && (n < setting->min || n > setting->max))
        {
            char text[128];
            snprintf(text, sizeof(text), "Must be between %g and %g.", setting->min,
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
