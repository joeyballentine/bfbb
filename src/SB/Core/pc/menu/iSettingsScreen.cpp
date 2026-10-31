// The in-game settings screen.
//
// A list of config.ini settings, a few rows at a time: up and down pick one,
// left and right change it, and the change is applied there and then --
// through the same setters iSystem.cpp's ApplyConfig uses at startup -- and
// written back to config.ini when the screen closes (iConfigSave). A setting
// that cannot change while the game runs says so and takes effect on the next
// start. A change to the window that could leave the player unable to see the
// game asks to be kept, and puts itself back if nobody answers.
//
// It is reached through the game's own save and load modes, so no game code
// knows it exists: the title's Settings entry switches to Load mode and the
// pause menu's to Save mode, exactly as their Load and Save entries do, and
// iSGLoadLoop / iSGSaveLoop (iSaveScreen.cpp) run this instead of a save
// screen when the entry that got them there is the Settings one.

#include "iSettingsScreen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rwcore.h>

#include "iAssetOverride.h"
#include "iBoot.h"
#include "iConfig.h"
#include "iPadBind.h"
#include "iPadHost.h"
#include "iDistort.h"
#include "iDrawDist.h"
#include "iGlow.h"
#include "iHost.h"
#include "iPadGlyph.h"
#include "iPadStick.h"
#include "iScreen.h"
#include "iMenuMouse.h"
#include "iMenuWide.h"
#include "iSnapshot.h"
#include "iTime.h"
#include "iWindow.h"
#include "iCamera.h"
#include "xEvent.h"
#include "xPad.h"
#include "xString.h"
#include "zGame.h"
#include "zGameState.h"
#include "zGlobals.h"
#include "zSaveLoad.h"
#include "zScene.h"
#include "zCamera.h"
#include "zUI.h"
#include "zUIFont.h"
#include "zMusic.h"
#include "zRumble.h"
#include "xSnd.h"
#include "iSnd.h"

namespace
{
    enum When
    {
        NOW,
        NEXT_AREA,
        RESTART
    };

    // What backs out of the screen and its pages: cancel, and the pause
    // button, which is Escape on a keyboard and what a PC player reaches for.
    const U32 kBack = XPAD_BUTTON_TRIANGLE | XPAD_BUTTON_START;

    // The tabs, in order. The shoulder buttons move between them.
    enum Tab
    {
        TAB_VIDEO,
        TAB_AUDIO,
        TAB_CONTROLS,
        TAB_GAME,
        TAB_COUNT
    };
    const char* const kTabNames[TAB_COUNT] = { "Video", "Audio", "Controls", "Game" };

    struct Setting
    {
        Tab tab;
        const char* label;

        // The config.ini key. "video.resolution" is not one: it stands for
        // video.width and video.height together.
        const char* key;

        // The words the setting takes, '|' between them, and what the screen
        // calls each one. NULL names means the words themselves.
        const char* words;
        const char* names;

        When when;
        const char* help;

        // Applies a word. NULL for one that only takes effect on restart, or
        // that the game reads again itself.
        void (*apply)(const char* word);

        // Could leave the player without a usable picture, so it asks to be
        // kept.
        bool confirm;
    };

    bool On(const char* w)
    {
        return iHostStrCaseCmp(w, "on") == 0;
    }

    iWindowMode ModeFor(const char* w)
    {
        if (iHostStrCaseCmp(w, "windowed") == 0)
        {
            return iWINDOW_WINDOWED;
        }
        return iHostStrCaseCmp(w, "fullscreen") == 0 ? iWINDOW_FULLSCREEN : iWINDOW_BORDERLESS;
    }

    void ApplyMode(const char* w)
    {
        iWindowSetMode(ModeFor(w));
    }

    void ApplyVSync(const char* w)
    {
        iWindowSetVSync(On(w));
    }

    void ApplyFrameRate(const char* w)
    {
        S32 fps;
        if (iHostStrCaseCmp(w, "display") == 0)
        {
            fps = iWindowGetDisplayRefreshRate();
        }
        else if (iHostStrCaseCmp(w, "off") == 0)
        {
            fps = 0;
        }
        else
        {
            fps = atoi(w);
        }
        iWindowSetFrameRate(fps > 0 ? fps : 0);
    }

    void ApplyFOV(const char* w)
    {
        iScreenSetFOV((F32)atof(w));
    }

    void ApplyUI(const char* w)
    {
        iScreenSetUIMode(iHostStrCaseCmp(w, "native") == 0 ? iSCREENUI_NATIVE : iSCREENUI_PILLARBOX);
    }

    void ApplyDrawDistance(const char* w)
    {
        iDrawDistSetUnlimited(On(w));
        iCameraSetNearFarClip(0.0f, iDrawDistFarClip());
    }

    void ApplyGlow(const char* w)
    {
        iGlowSetEnabled(On(w));
    }

    void ApplyDistortion(const char* w)
    {
        iDistortSetEnabled(On(w));
    }

    void ApplySnapshot(const char* w)
    {
        iSnapshotSetEnabled(On(w));
    }

    void ApplyDeadzone(const char* w)
    {
        iPadStickSetDeadzone(iHostStrCaseCmp(w, "auto") == 0 ? -1.0f : (F32)atof(w));
    }

    void ApplySticks(const char* w)
    {
        iPadStickSetSwap(iHostStrCaseCmp(w, "swapped") == 0);
    }

    // Both axes, from config.ini: Store has already written this one there.
    void ApplyInvert(const char*)
    {
        iPadStickSetInvert(iConfigGetBool("input.invert_camera_x", FALSE),
                           iConfigGetBool("input.invert_camera_y", FALSE));
    }

    // The startup applied the speed by multiplying the base scales (zMain.cpp,
    // after SB.INI); a change multiplies them again by new over old, so
    // SB.INI's base survives however often it is changed.
    void ApplyCameraSpeed(const char* w)
    {
        const F32 was = iBootCameraSensitivity();
        const F32 now = (F32)atof(w);
        if (was <= 0.0f || now <= 0.0f)
        {
            return;
        }
        zcam_pad_pyaw_scale *= now / was;
        zcam_pad_pitch_scale *= now / was;
        iBootSetCameraSensitivity(now);
    }

    void ApplyIcons(const char* w)
    {
        iPadGlyphSetChoice(w);
        iPadGlyphSetEnabled(iHostStrCaseCmp(w, "off") != 0);
    }

    // The three the retail Options page set, and set the same way:
    // zDispatcher.cpp's handlers for its commands.
    void ApplyMusicVolume(const char* w)
    {
        iSettingsApplyMusicVolume(atoi(w));
    }

    void ApplySFXVolume(const char* w)
    {
        iSettingsApplySFXVolume(atoi(w));
    }

    void ApplySoundMode(const char* w)
    {
        iSettingsApplySoundMode(iHostStrCaseCmp(w, "mono") != 0);
    }

    void ApplyRolloff(const char* w)
    {
        iSndSetXboxRolloff(On(w));
    }

    // And a pulse when it goes on, as retail's page gave one.
    void ApplyVibration(const char* w)
    {
        iSettingsApplyVibration(On(w), TRUE);
    }

    const Setting kSettings[] = {
        { TAB_VIDEO, "Window", "video.mode", "borderless|windowed|fullscreen",
          "Borderless|Windowed|Fullscreen", NOW,
          "Borderless fills the screen as a window. Alt+Enter also switches.", ApplyMode, true },
        { TAB_VIDEO, "Resolution", "video.resolution", NULL, NULL, RESTART,
          "Rendering resolution. The image is scaled to fit the window.", NULL, false },
        { TAB_VIDEO, "VSync", "video.vsync", "on|off", "On|Off", NOW,
          "Wait for the display's refresh before showing a frame. Prevents tearing.", ApplyVSync,
          false },
        { TAB_VIDEO, "Frame rate limit", "video.framerate", "30|60|120|144|165|240|display|off",
          "30|60|120|144|165|240|Monitor|Unlimited", NOW,
          "Maximum frames per second. 60 is the original.", ApplyFrameRate, false },
        { TAB_VIDEO, "Field of view", "video.fov", "60|65|70|75|80|85|90|95|100|105|110", NULL, NOW,
          "Horizontal field of view in degrees at 4:3. 75 is the original.", ApplyFOV, false },
        { TAB_VIDEO, "HUD layout", "video.ui", "pillarbox|native", "4:3 (original)|Screen edges",
          NOW, "HUD position on wide screens.", ApplyUI, false },
        { TAB_VIDEO, "Draw distance", "video.draw_distance", "on|off", "Unlimited|Console",
          NEXT_AREA, "How far away objects are drawn.", ApplyDrawDistance, false },
        { TAB_VIDEO, "Anti-aliasing", "video.msaa", "1|2|4|8", "Off|2x|4x|8x", RESTART,
          "Smooths jagged edges. Lowers performance.", NULL, false },
        { TAB_VIDEO, "Glow", "xbox.glow", "on|off", "On|Off", NOW, "Xbox bloom effect.", ApplyGlow,
          false },
        { TAB_VIDEO, "Screen warps", "xbox.distortion", "on|off", "On|Off", NOW,
          "Cruise Bubble screen distortion.", ApplyDistortion, false },
        { TAB_VIDEO, "Loading-screen still", "xbox.snapshot", "on|off", "On|Off", NOW,
          "Show a still of the previous level on the loading screen. Also used for save pictures.",
          ApplySnapshot, false },
        { TAB_AUDIO, "Music volume", "audio.music_volume", "0|1|2|3|4|5|6|7|8|9|10", NULL, NOW,
          "How loud the music is.", ApplyMusicVolume, false },
        { TAB_AUDIO, "Effects volume", "audio.sfx_volume", "0|1|2|3|4|5|6|7|8|9|10", NULL, NOW,
          "How loud sound effects and voices are.", ApplySFXVolume, false },
        { TAB_AUDIO, "Sound mode", "audio.sound_mode", "stereo|mono", "Stereo|Mono", NOW,
          "Mono plays everything through both speakers alike.", ApplySoundMode, false },
        { TAB_AUDIO, "Cave echo", "xbox.reverb", "on|off", "On|Off", NEXT_AREA,
          "Reverb in caves and the Mermalair.", NULL, false },
        { TAB_AUDIO, "Distance falloff", "xbox.sound_rolloff", "on|off", "Xbox|GameCube", NOW,
          "How sounds fade with distance. GameCube is louder far away.", ApplyRolloff, false },
        { TAB_CONTROLS, "Keyboard buttons", "bind.keyboard", "", "Change", NOW,
          "Keys for each game button.", NULL, false },
        { TAB_CONTROLS, "Controller buttons", "bind.pad", "", "Change", NOW,
          "Controller buttons for each game button.", NULL, false },
        { TAB_CONTROLS, "Stick deadzone", "input.deadzone", "auto|5|10|15|20|25|30",
          "Auto|5%|10%|15%|20%|25%|30%", NOW, "How far a stick moves before input registers.",
          ApplyDeadzone, false },
        { TAB_CONTROLS, "Button pictures", "input.button_icons", "auto|xbox|gamecube|ps2|off",
          "Auto|Xbox|GameCube|PlayStation|Original", NOW,
          "Controller type shown in button prompts.", ApplyIcons, false },
        { TAB_CONTROLS, "Camera speed", "input.camera_sensitivity", "0.5|0.75|1.0|1.25|1.5|2.0",
          "0.5x|0.75x|1x|1.25x|1.5x|2x", NOW, "Camera stick speed.", ApplyCameraSpeed, false },
        { TAB_CONTROLS, "Sticks", "input.sticks", "normal|swapped", "Normal|Swapped", NOW,
          "Normal: left stick moves, right stick turns the camera.", ApplySticks, false },
        { TAB_CONTROLS, "Invert camera X", "input.invert_camera_x", "off|on", "Off|On", NOW,
          "Reverse the camera's left and right.", ApplyInvert, false },
        { TAB_CONTROLS, "Invert camera Y", "input.invert_camera_y", "off|on", "Off|On", NOW,
          "Reverse the camera's up and down.", ApplyInvert, false },
        { TAB_CONTROLS, "Vibration", "input.vibration", "on|off", "On|Off", NOW,
          "Controller vibration.", ApplyVibration, false },
        { TAB_CONTROLS, "Mouse camera", "mouse.look", "auto|on|off", "Auto|On|Off", NOW,
          "Turn the camera with the mouse. Auto: while using the keyboard and mouse.", NULL,
          false },
        { TAB_CONTROLS, "Mouse sensitivity", "mouse.sensitivity",
          "0.25|0.5|0.75|1.0|1.25|1.5|2.0|2.5|3.0", "0.25x|0.5x|0.75x|1x|1.25x|1.5x|2x|2.5x|3x",
          NOW, "How far the camera turns for each movement of the mouse.", NULL, false },
        { TAB_CONTROLS, "Mouse smoothing", "mouse.smoothing", "0|10|20|40|80",
          "Off|10 ms|20 ms|40 ms|80 ms", NOW, "Spreads each mouse movement over a few frames.",
          NULL, false },
        { TAB_GAME, "Intro movies", "game.intro_movies", "on|off", "On|Off", RESTART,
          "Logos shown at startup.", NULL, false },
    };
    const S32 kCount = (S32)(sizeof(kSettings) / sizeof(kSettings[0]));

    // The resolutions offered, and the display's own when it is not one of
    // them. Built when the screen opens.
    char sResWords[256];

    // The tab showing, and the settings on it: sSel and sTop index this list.
    Tab sTab;
    S32 sList[kCount];
    S32 sListCount;

    S32 sSel;
    S32 sTop;
    bool sRestart;

    // The tab under the menu pointer, or -1.
    S32 sHoverTab = -1;

    void BuildList()
    {
        sListCount = 0;
        for (S32 i = 0; i < kCount; i++)
        {
            if (kSettings[i].tab == sTab)
            {
                sList[sListCount++] = i;
            }
        }
    }

    const Setting& Selected()
    {
        return kSettings[sList[sSel]];
    }

    void Send(const char* name, U32 event)
    {
        xBase* to = zSceneFindObject(xStrHash(name));
        if (to != NULL)
        {
            zEntEvent(to, event);
        }
    }

    // The n'th '|' word of `list`, or false past the end.
    bool Word(const char* list, S32 n, char* out, size_t size)
    {
        const char* p = list;
        for (S32 i = 0; p != NULL; i++)
        {
            const char* bar = strchr(p, '|');
            size_t len = bar != NULL ? (size_t)(bar - p) : strlen(p);
            if (i == n)
            {
                if (len >= size)
                {
                    len = size - 1;
                }
                memcpy(out, p, len);
                out[len] = '\0';
                return true;
            }
            p = bar != NULL ? bar + 1 : NULL;
        }
        return false;
    }

    S32 WordCount(const char* list)
    {
        S32 n = 1;
        for (const char* p = list; *p != '\0'; p++)
        {
            n += *p == '|';
        }
        return n;
    }

    const char* Words(const Setting& s)
    {
        return s.words != NULL ? s.words : sResWords;
    }

    bool Profiled(const char* key)
    {
        return strcmp(key, "video.ui") == 0 || strcmp(key, "video.draw_distance") == 0 ||
               strcmp(key, "video.resolution") == 0;
    }

    bool CustomProfile()
    {
        return iHostStrCaseCmp(iConfigGetString("video.profile", "custom"), "custom") == 0;
    }

    // What the setting is now, as one of its words where it can be.
    void Current(const Setting& s, char* out, size_t size)
    {
        if (strcmp(s.key, "video.mode") == 0)
        {
            switch (iWindowGetMode())
            {
            case iWINDOW_WINDOWED:
                snprintf(out, size, "windowed");
                return;
            case iWINDOW_FULLSCREEN:
                snprintf(out, size, "fullscreen");
                return;
            default:
                snprintf(out, size, "borderless");
                return;
            }
        }

        // video.profile's vanilla and modern decide these three themselves;
        // what is showing is what they decided.
        if (Profiled(s.key) && !CustomProfile())
        {
            if (strcmp(s.key, "video.ui") == 0)
            {
                snprintf(out, size, iScreenGetUIMode() == iSCREENUI_NATIVE ? "native" : "pillarbox");
            }
            else if (strcmp(s.key, "video.draw_distance") == 0)
            {
                snprintf(out, size, iDrawDistUnlimited() ? "on" : "off");
            }
            else
            {
                snprintf(out, size, "%dx%d", (int)iScreenWidth(), (int)iScreenHeight());
            }
            return;
        }

        if (strcmp(s.key, "video.resolution") == 0)
        {
            snprintf(out, size, "%dx%d", (int)iConfigGetInt("video.width", 640),
                     (int)iConfigGetInt("video.height", 480));
            return;
        }

        snprintf(out, size, "%s", iConfigGetString(s.key, ""));
    }

    // Which word the setting is on. Numbers compare as numbers, so a file
    // saying 75.0 is on 75. -1 for a value the list does not have.
    S32 IndexOf(const Setting& s)
    {
        char cur[64];
        Current(s, cur, sizeof(cur));

        char w[64];
        for (S32 i = 0; Word(Words(s), i, w, sizeof(w)); i++)
        {
            if (iHostStrCaseCmp(w, cur) == 0)
            {
                return i;
            }
            char* end = NULL;
            const double a = strtod(w, &end);
            if (end != w && *end == '\0')
            {
                char* end2 = NULL;
                const double b = strtod(cur, &end2);
                if (end2 != cur && *end2 == '\0' && a == b)
                {
                    return i;
                }
            }
        }
        return -1;
    }

    void Name(const Setting& s, S32 i, char* out, size_t size)
    {
        if (i < 0)
        {
            Current(s, out, size);
            return;
        }
        if (s.names == NULL || !Word(s.names, i, out, size))
        {
            Word(Words(s), i, out, size);
        }
    }

    void BuildResolutions()
    {
        static const char* const kCommon[] = { "640x480",   "1280x720",  "1600x900",
                                               "1920x1080", "2560x1440", "3840x2160" };
        sResWords[0] = '\0';

        S32 dw = 0;
        S32 dh = 0;
        char native[32] = "";
        if (iWindowGetDisplaySize(&dw, &dh) && dw > 0 && dh > 0)
        {
            snprintf(native, sizeof(native), "%dx%d", (int)dw, (int)dh);
        }

        for (size_t i = 0; i < sizeof(kCommon) / sizeof(kCommon[0]); i++)
        {
            if (sResWords[0] != '\0')
            {
                strcat(sResWords, "|");
            }
            strcat(sResWords, kCommon[i]);
        }
        if (native[0] != '\0' && strstr(sResWords, native) == NULL)
        {
            strcat(sResWords, "|");
            strcat(sResWords, native);
        }
    }

    // video.profile's vanilla and modern set the render size, the HUD and the
    // draw distance themselves, and would override a change to any of them.
    // Changing one therefore turns the profile to custom, with the other two
    // pinned at what the profile had them on, so nothing else moves.
    void UnProfile()
    {
        if (CustomProfile())
        {
            return;
        }

        char v[32];
        snprintf(v, sizeof(v), "%d", (int)iScreenWidth());
        iConfigSet("video.width", v);
        snprintf(v, sizeof(v), "%d", (int)iScreenHeight());
        iConfigSet("video.height", v);
        iConfigSet("video.ui", iScreenGetUIMode() == iSCREENUI_NATIVE ? "native" : "pillarbox");
        iConfigSet("video.draw_distance", iDrawDistUnlimited() ? "on" : "off");
        iConfigSet("video.profile", "custom");
    }

    void Store(const Setting& s, const char* word)
    {
        if (Profiled(s.key))
        {
            UnProfile();
        }

        if (strcmp(s.key, "video.resolution") == 0)
        {
            int w = 0;
            int h = 0;
            if (sscanf(word, "%dx%d", &w, &h) == 2)
            {
                char v[16];
                snprintf(v, sizeof(v), "%d", w);
                iConfigSet("video.width", v);
                snprintf(v, sizeof(v), "%d", h);
                iConfigSet("video.height", v);
            }
            return;
        }

        iConfigSet(s.key, word);
    }

    void SetText(const char* fmt, S32 row, const char* text)
    {
        char name[32];
        sprintf(name, fmt, (int)row);
        iAssetTextSet(xStrHash(name), text);
    }

    void DrawHelp(const char* override)
    {
        char help[256];
        if (override != NULL)
        {
            snprintf(help, sizeof(help), "%s", override);
        }
        else
        {
            const Setting& s = Selected();
            const char* when = s.when == RESTART     ? "{n}Applies after restart."
                               : s.when == NEXT_AREA ? "{n}Applies in the next area."
                                                     : "{n}";
            snprintf(help, sizeof(help), "%s%s%s", s.help, when,
                     sRestart ? "{n}Some changes apply after restart." : "");
        }
        iAssetTextSet(xStrHash(ISETTINGS_HELP_TEXT), help);
    }

    bool IsBindEntry(const Setting& s)
    {
        return strncmp(s.key, "bind.", 5) == 0;
    }

    // The authored layout, spread toward the frame on a wide screen: labels
    // out to the left, values out to the right, the title and help as wide as
    // both. See iMenuWide.h.
    void Layout()
    {
        const F32 s = 0.6f * iMenuWideMargin();
        char name[32];

        iMenuWidePlace(ISETTINGS_TITLE, 45.0f - s, 560.0f + 2.0f * s);
        for (S32 row = 0; row < ISETTINGS_ROWS; row++)
        {
            sprintf(name, ISETTINGS_LABEL, (int)row);
            iMenuWidePlace(name, 55.0f - s, 270.0f + s);
            sprintf(name, ISETTINGS_VALUE, (int)row);
            iMenuWidePlace(name, 330.0f + s, 270.0f);
        }
        iMenuWidePlace(ISETTINGS_HELP, 55.0f - s, 390.0f + s);
        iMenuWidePlace(ISETTINGS_BACK, 450.0f + s, 150.0f);
    }

    zUIFont* Widget(const char* name)
    {
        xBase* b = zSceneFindObject(xStrHash(name));
        return (b != NULL && b->baseType == eBaseTypeUIFont) ? (zUIFont*)b : NULL;
    }

    // The track spans the rows; the thumb is the share of them showing, where
    // they are. Both hidden when every row fits. `count` rows from row `top`.
    void DrawScrollbar(S32 top, S32 count)
    {
        zUIFont* track = Widget(ISETTINGS_SCROLL_TRACK);
        zUIFont* thumb = Widget(ISETTINGS_SCROLL_THUMB);
        if (track == NULL || thumb == NULL)
        {
            return;
        }

        const bool needed = count > ISETTINGS_ROWS;
        Send(ISETTINGS_SCROLL_TRACK, needed ? eEventVisible : eEventInvisible);
        Send(ISETTINGS_SCROLL_THUMB, needed ? eEventVisible : eEventInvisible);
        if (!needed)
        {
            return;
        }

        const F32 x = 606.0f + 0.6f * iMenuWideMargin();
        const F32 y = ISETTINGS_ROW_Y;
        const F32 height = ISETTINGS_ROW_STEP * ISETTINGS_ROWS - 2.0f;

        track->fasset->pos.x = x;
        thumb->fasset->pos.x = x;
        thumb->fasset->pos.y = y + height * (F32)top / (F32)count;
        thumb->fasset->dim[1] = (U16)(height * (F32)ISETTINGS_ROWS / (F32)count);
    }

    void Draw();
    void SelectRow(S32 row, bool on);

    // A click on the track above or below the thumb turns a page, as on any
    // scrollbar.
    void ScrollbarClick()
    {
        zUIFont* thumb = Widget(ISETTINGS_SCROLL_THUMB);
        F32 px;
        F32 py;
        if (thumb == NULL || sListCount <= ISETTINGS_ROWS || !iMenuMouseClicked() ||
            !iMenuMouseOver(zSceneFindObject(xStrHash(ISETTINGS_SCROLL_TRACK))) ||
            !iMenuMousePosition(&px, &py))
        {
            return;
        }

        const F32 y0 = thumb->fasset->pos.y;
        const F32 y1 = y0 + thumb->fasset->dim[1];
        S32 page = (py < y0) ? -ISETTINGS_ROWS : (py >= y1 ? ISETTINGS_ROWS : 0);
        if (page == 0)
        {
            return;
        }

        SelectRow(sSel - sTop, false);
        sTop += page;
        S32 most = sListCount - ISETTINGS_ROWS;
        sTop = sTop < 0 ? 0 : (sTop > most ? most : sTop);
        sSel =
            sSel < sTop ? sTop : (sSel >= sTop + ISETTINGS_ROWS ? sTop + ISETTINGS_ROWS - 1 : sSel);
        SelectRow(sSel - sTop, true);
        Draw();
    }

    // The tab bar: the shoulder buttons' pictures either side, the tab showing
    // in the menu's own dark teal and the rest faded toward the background.
    //
    // Cut off after tab `last`'s name for the pointer to measure, -1 for just
    // the first picture; TAB_COUNT is the whole bar.
    void TitleText(char* title, size_t size, S32 last)
    {
        // A tab under the pointer is brighter, in iMenuMouseBrighten's teal.
        U8 r = 0;
        U8 g = 70;
        U8 b = 85;
        iMenuMouseBrighten(&r, &g, &b);
        char hover[16];
        snprintf(hover, sizeof(hover), "{c=ff%02x%02x%02x}", r, g, b);

        snprintf(title, size, "{i:button_picture_07}");
        for (S32 t = 0; t < TAB_COUNT && t <= last; t++)
        {
            const char* colour = (t == sTab) ? "" : (t == sHoverTab ? hover : "{c=ff6f98a3}");
            char one[64];
            snprintf(one, sizeof(one), "%s%s%s%s", t == 0 ? " " : "   ", colour, kTabNames[t],
                     t == sTab ? "" : "{~:c}");
            strncat(title, one, size - strlen(title) - 1);
        }
        if (last >= TAB_COUNT)
        {
            strncat(title, "   {i:button_picture_05}", size - strlen(title) - 1);
        }
    }

    // The tab under the pointer on the tab bar: a name, or the shoulder
    // picture either side, which turns the page the way its button does.
    // -1 when the pointer is not on the bar.
    S32 PointerTab()
    {
        xBase* bar = zSceneFindObject(xStrHash(ISETTINGS_TITLE));
        F32 px;
        F32 py;
        if (!iMenuMouseOver(bar) || !iMenuMousePosition(&px, &py))
        {
            return -1;
        }

        // Each tab takes the gap before its name as well as the name.
        char title[256];
        F32 end;
        TitleText(title, sizeof(title), -1);
        if (!iMenuMouseTextEnd(bar, title, &end) || px <= end)
        {
            return (sTab + TAB_COUNT - 1) % TAB_COUNT;
        }
        for (S32 t = 0; t < TAB_COUNT; t++)
        {
            TitleText(title, sizeof(title), t);
            if (iMenuMouseTextEnd(bar, title, &end) && px <= end + 4.0f)
            {
                return t;
            }
        }
        return (sTab + 1) % TAB_COUNT;
    }

    // What the pointer is over on a row's value: one of the two arrows, a
    // slider notch, or anywhere else on it. Kept so that the arrow under it can
    // be drawn lit.
    enum
    {
        PART_NONE = 0,
        PART_LEFT = -1,
        PART_RIGHT = 1,
        PART_NOTCH = 2 // PART_NOTCH + n is notch n
    };
    S32 sHoverRow = -1;
    S32 sHoverPart = PART_NONE;

    // The on-screen row each slider set is drawn on this time, or -1.
    S32 sSliderRow[ISETTINGS_SLIDERS];

    bool IsSlider(const Setting& s)
    {
        return strcmp(s.key, "audio.music_volume") == 0 || strcmp(s.key, "audio.sfx_volume") == 0;
    }

    S32 SliderFor(S32 row)
    {
        for (S32 k = 0; k < ISETTINGS_SLIDERS; k++)
        {
            if (sSliderRow[k] == row)
            {
                return k;
            }
        }
        return -1;
    }

    // A value as the row shows it: its name, which the row's arrows sit either
    // side of; nothing on a slider, which is all bamboo.
    void ValueText(const Setting& s, S32 row, char* out, size_t size)
    {
        if (IsBindEntry(s))
        {
            snprintf(out, size, "{i:button_picture_01} Change");
            return;
        }
        if (IsSlider(s))
        {
            out[0] = '\0';
            return;
        }

        // The arrows either side are the row's own sprites.
        Name(s, IndexOf(s), out, size);
    }

    void PlaceUI(const char* name, F32 x, F32 y)
    {
        xBase* b = zSceneFindObject(xStrHash(name));
        if (b != NULL && b->baseType == eBaseTypeUI)
        {
            _zUI* ui = (_zUI*)b;
            ui->sasset->pos.x = x;
            ui->sasset->pos.y = y;
        }
    }

    void ShowUI(const char* name, bool on)
    {
        Send(name, on ? eEventVisible : eEventInvisible);
    }

    // The value column: 270 across from x0, an arrow at each end, and the
    // eleven notches centred between them.
    const F32 kValueW = 270.0f;
    const F32 kArrow = (F32)ISETTINGS_ARROW_SIZE;
    const F32 kSliderStep = 18.0f;
    const F32 kNotchW = 22.0f;

    F32 ValueX()
    {
        return 330.0f + 0.6f * iMenuWideMargin();
    }

    F32 SliderNotchX(F32 x0, S32 n)
    {
        const F32 run = kNotchW + kSliderStep * (ISETTINGS_SLIDER_LEVELS - 1);
        return x0 + 0.5f * (kValueW - run) + kSliderStep * n;
    }

    // The arrows either side of every row with something to step through, the
    // one under the pointer glowing as the Options page's did.
    void DrawArrows()
    {
        const F32 x0 = ValueX();
        char name[48];
        for (S32 row = 0; row < ISETTINGS_ROWS; row++)
        {
            const bool shown =
                sTop + row < sListCount && !IsBindEntry(kSettings[sList[sTop + row]]);
            const F32 y = ISETTINGS_ROW_Y + ISETTINGS_ROW_STEP * row + 0.5f * (32.0f - kArrow);
            const bool litLeft = shown && sHoverRow == row && sHoverPart == PART_LEFT;
            const bool litRight = shown && sHoverRow == row && sHoverPart == PART_RIGHT;

            sprintf(name, ISETTINGS_ARROW_LEFT, (int)row);
            PlaceUI(name, x0, y);
            ShowUI(name, shown);
            sprintf(name, ISETTINGS_ARROW_LEFT_GLOW, (int)row);
            PlaceUI(name, x0, y);
            ShowUI(name, litLeft);
            sprintf(name, ISETTINGS_ARROW_RIGHT, (int)row);
            PlaceUI(name, x0 + kValueW - kArrow, y);
            ShowUI(name, shown);
            sprintf(name, ISETTINGS_ARROW_RIGHT_GLOW, (int)row);
            PlaceUI(name, x0 + kValueW - kArrow, y);
            ShowUI(name, litRight);
        }
    }

    // Every row's arrows and every slider put away, for a bind page, which has
    // the rows without them.
    void HideValueParts()
    {
        char name[48];
        for (S32 row = 0; row < ISETTINGS_ROWS; row++)
        {
            sprintf(name, ISETTINGS_ARROW_LEFT, (int)row);
            ShowUI(name, false);
            sprintf(name, ISETTINGS_ARROW_LEFT_GLOW, (int)row);
            ShowUI(name, false);
            sprintf(name, ISETTINGS_ARROW_RIGHT, (int)row);
            ShowUI(name, false);
            sprintf(name, ISETTINGS_ARROW_RIGHT_GLOW, (int)row);
            ShowUI(name, false);
        }
        for (S32 k = 0; k < ISETTINGS_SLIDERS; k++)
        {
            for (S32 n = 0; n < ISETTINGS_SLIDER_LEVELS; n++)
            {
                sprintf(name, ISETTINGS_SLIDER_ON, (int)k, (int)n);
                ShowUI(name, false);
                sprintf(name, ISETTINGS_SLIDER_OFF, (int)k, (int)n);
                ShowUI(name, false);
            }
        }
    }

    // The bamboo on every row whose setting is a volume, and nothing left over
    // from the last tab.
    void DrawSliders()
    {
        for (S32 k = 0; k < ISETTINGS_SLIDERS; k++)
        {
            sSliderRow[k] = -1;
        }

        S32 used = 0;
        for (S32 row = 0; row < ISETTINGS_ROWS && sTop + row < sListCount; row++)
        {
            const Setting& s = kSettings[sList[sTop + row]];
            if (IsSlider(s) && used < ISETTINGS_SLIDERS)
            {
                sSliderRow[used++] = row;
            }
        }

        const F32 x0 = ValueX();
        char name[48];
        for (S32 k = 0; k < ISETTINGS_SLIDERS; k++)
        {
            const S32 row = sSliderRow[k];
            const bool shown = row >= 0;
            const F32 y = ISETTINGS_ROW_Y + ISETTINGS_ROW_STEP * row;
            S32 level = 0;
            if (shown)
            {
                level = atoi(iConfigGetString(kSettings[sList[sTop + row]].key, "0"));
            }

            for (S32 n = 0; n < ISETTINGS_SLIDER_LEVELS; n++)
            {
                const F32 nx = SliderNotchX(x0, n);
                const F32 ny = y + ((n % 5) == 0 ? 1.0f : 4.0f);
                sprintf(name, ISETTINGS_SLIDER_ON, (int)k, (int)n);
                PlaceUI(name, nx, ny);
                ShowUI(name, shown && n <= level);
                sprintf(name, ISETTINGS_SLIDER_OFF, (int)k, (int)n);
                PlaceUI(name, nx, ny);
                ShowUI(name, shown && n > level);
            }
        }
    }

    // What on row `row`'s value the pointer is over: an arrow, a notch on a
    // slider, or neither.
    S32 PointerPart(S32 row)
    {
        if (row < 0 || sTop + row >= sListCount)
        {
            return PART_NONE;
        }
        const Setting& s = kSettings[sList[sTop + row]];
        if (IsBindEntry(s))
        {
            return PART_NONE;
        }

        char name[48];
        sprintf(name, ISETTINGS_ARROW_LEFT, (int)row);
        if (iMenuMouseOver(zSceneFindObject(xStrHash(name))))
        {
            return PART_LEFT;
        }
        sprintf(name, ISETTINGS_ARROW_RIGHT, (int)row);
        if (iMenuMouseOver(zSceneFindObject(xStrHash(name))))
        {
            return PART_RIGHT;
        }

        if (IsSlider(s))
        {
            F32 px;
            F32 py;
            if (iMenuMousePosition(&px, &py))
            {
                const F32 x0 = ValueX();
                const F32 y = ISETTINGS_ROW_Y + ISETTINGS_ROW_STEP * row;
                if (py >= y && py < y + 32.0f)
                {
                    // The nearest notch, over the whole run of them.
                    const F32 first = SliderNotchX(x0, 0) + 11.0f;
                    const F32 last = SliderNotchX(x0, ISETTINGS_SLIDER_LEVELS - 1) + 11.0f;
                    if (px >= first - kSliderStep * 0.5f && px < last + kSliderStep * 0.5f)
                    {
                        S32 n = (S32)((px - first) / kSliderStep + 0.5f);
                        n = n < 0 ?
                                0 :
                                (n >= ISETTINGS_SLIDER_LEVELS ? ISETTINGS_SLIDER_LEVELS - 1 : n);
                        return PART_NOTCH + n;
                    }
                }
            }
            return PART_NONE;
        }

        return PART_NONE;
    }

    void Draw()
    {
        Layout();

        char title[256];
        TitleText(title, sizeof(title), TAB_COUNT);
        iAssetTextSet(xStrHash(ISETTINGS_TITLE_TEXT), title);

        DrawSliders();
        DrawArrows();

        for (S32 row = 0; row < ISETTINGS_ROWS; row++)
        {
            const S32 i = sTop + row;
            if (i >= sListCount)
            {
                SetText(ISETTINGS_LABEL_TEXT, row, "");
                SetText(ISETTINGS_VALUE_TEXT, row, "");
                continue;
            }

            const Setting& s = kSettings[sList[i]];
            char shown[128];
            ValueText(s, row, shown, sizeof(shown));
            SetText(ISETTINGS_LABEL_TEXT, row, s.label);
            SetText(ISETTINGS_VALUE_TEXT, row, shown);
        }

        DrawScrollbar(sTop, sListCount);
        DrawHelp(NULL);
    }

    void SelectRow(S32 row, bool on)
    {
        char name[32];
        sprintf(name, ISETTINGS_LABEL, (int)row);
        Send(name, on ? eEventUISelect : eEventUIUnselect);
        sprintf(name, ISETTINGS_VALUE, (int)row);
        Send(name, on ? eEventUISelect : eEventUIUnselect);
    }

    // The row on screen the pointer is over, label or value, or -1.
    S32 PointerRow()
    {
        // The label, the value's words, and the arrows either side of them,
        // which are outside the words now that the words are centred.
        static const char* const kParts[] = { ISETTINGS_LABEL, ISETTINGS_VALUE,
                                              ISETTINGS_ARROW_LEFT, ISETTINGS_ARROW_RIGHT };
        for (S32 row = 0; row < ISETTINGS_ROWS && sTop + row < sListCount; row++)
        {
            for (size_t i = 0; i < sizeof(kParts) / sizeof(kParts[0]); i++)
            {
                char name[48];
                sprintf(name, kParts[i], (int)row);
                if (iMenuMouseOver(zSceneFindObject(xStrHash(name))))
                {
                    return row;
                }
            }
        }
        return -1;
    }

    // "Keep this?" for a change that could leave the player without a picture
    // they can use. Anything but a yes within the time puts it back.
    bool Confirm()
    {
        const S32 kSeconds = 15;
        const iTime start = iTimeGet();

        for (;;)
        {
            const F32 elapsed = iTimeDiffSec(start, iTimeGet());
            const S32 left = kSeconds - (S32)elapsed;
            if (left <= 0)
            {
                return false;
            }

            char text[160];
            snprintf(text, sizeof(text),
                     "Keep this setting? {i:button_picture_01} Keep  {i:ui_cancel} Revert{n}"
                     "Reverting in %d.",
                     (int)left);
            DrawHelp(text);

            zSaveLoad_Tick();
            const U32 pressed = mPad[globals.currentActivePad].pressed;
            if (pressed & XPAD_BUTTON_X)
            {
                return true;
            }
            if (pressed & kBack)
            {
                return false;
            }
        }
    }


    // ------------------------------------------------------------------
    // The binding pages: one row per button the game reads, what presses it,
    // and a row that puts them all back. Confirm on a row waits for the next
    // key or controller button and binds it; Escape, or eight seconds, leaves
    // it alone. Options binds nothing to the row, except on the rows the menus
    // need (MenuNeeds). Written to config.ini's [keyboard] or [pad] and read
    // again at once (iPadHostReloadBindings).

    // What each row of kPadBindButtons is called here. The game's own names
    // are the GameCube's, and what a button does depends on the preset, so the
    // help line carries the table's description instead.
    const char* const kBindLabels[] = {
        "A button",      "B button",      "X button",     "Y button",     "Z (camera close)",
        "Show HUD",      "Cruise bubble", "Talk / bus",   "Spare L (L2)", "Spare R (R2)",
        "Start (pause)", "Select",        "D-pad up",     "D-pad down",   "D-pad left",
        "D-pad right",   "Sneak",         "First person",
    };
    const S32 kBindLabelCount = (S32)(sizeof(kBindLabels) / sizeof(kBindLabels[0]));

    // Rows: one per button, then the reset row.
    S32 BindRows()
    {
        return kPadBindButtonCount + 1;
    }

    void BindKey(bool pad, S32 row, char* out, size_t size)
    {
        snprintf(out, size, "%s.%s", pad ? "pad" : "keyboard", kPadBindButtons[row].name);
    }

    // Buttons this screen and the pause menu cannot be worked without. Clearing
    // one would leave no way back in here to undo it.
    bool MenuNeeds(S32 row)
    {
        const U32 mask = kPadBindButtons[row].mask;
        return mask == XPAD_BUTTON_X || mask == XPAD_BUTTON_TRIANGLE || mask == XPAD_BUTTON_UP ||
               mask == XPAD_BUTTON_DOWN || mask == XPAD_BUTTON_START;
    }

    // A help line naming the rows that one press of `row`'s input also sets,
    // or "" when there are none.
    void SharedWith(bool pad, S32 row, char* out, size_t size)
    {
        out[0] = '\0';

        const iPadBind* binds = iPadHostBindings(pad);
        if (binds == NULL)
        {
            return;
        }

        size_t have = 0;
        for (S32 i = 0; i < kPadBindButtonCount && i < IPAD_BIND_MAX_BUTTONS; i++)
        {
            if (i == row || !iPadBindOverlap(binds[row], binds[i]))
            {
                continue;
            }

            const char* label = i < kBindLabelCount ? kBindLabels[i] : kPadBindButtons[i].name;
            const int n = snprintf(out + have, size - have, "%s%s",
                                   have == 0 ? "{n}Shares an input with " : ", ", label);
            if (n < 0 || (size_t)n >= size - have)
            {
                out[have] = '\0';
                break;
            }
            have += (size_t)n;
        }
    }

    void DrawBindPage(bool pad, S32 sel, S32 top, const char* help)
    {
        Layout();
        iAssetTextSet(xStrHash(ISETTINGS_TITLE_TEXT),
                      pad ? "Controller buttons" : "Keyboard buttons");
        HideValueParts();
        DrawScrollbar(top, BindRows());

        for (S32 r = 0; r < ISETTINGS_ROWS; r++)
        {
            const S32 i = top + r;
            if (i >= BindRows())
            {
                SetText(ISETTINGS_LABEL_TEXT, r, "");
                SetText(ISETTINGS_VALUE_TEXT, r, "");
                continue;
            }
            if (i == kPadBindButtonCount)
            {
                SetText(ISETTINGS_LABEL_TEXT, r, "Reset all");
                SetText(ISETTINGS_VALUE_TEXT, r, "{i:button_picture_01} Defaults");
                continue;
            }

            char key[64];
            BindKey(pad, i, key, sizeof(key));
            SetText(ISETTINGS_LABEL_TEXT, r,
                    i < kBindLabelCount ? kBindLabels[i] : kPadBindButtons[i].name);
            const char* bound = iConfigGetString(key, "");
            const bool nothing = bound[0] == '\0' || iHostStrCaseCmp(bound, "none") == 0;
            SetText(ISETTINGS_VALUE_TEXT, r, nothing ? "(nothing)" : bound);
        }

        if (help != NULL)
        {
            DrawHelp(help);
            return;
        }

        char text[256];
        if (sel == kPadBindButtonCount)
        {
            snprintf(text, sizeof(text),
                     "Reset every binding on this page to its default.{n}"
                     "{i:ui_cancel} Back");
        }
        else
        {
            const char* does = kPadBindButtons[sel].does;
            // Short enough that the line of buttons after it always fits in
            // the help text's 256 bytes (mnu4_text.cpp); SharedWith stops at a
            // whole name.
            char shared[100];
            SharedWith(pad, sel, shared, sizeof(shared));
            snprintf(text, sizeof(text),
                     "%s%s%s{n}{i:button_picture_01} Change  %s"
                     "{i:ui_cancel} Back",
                     does != NULL ? "Action: " : "", does != NULL ? does : "", shared,
                     MenuNeeds(sel) ? "" : "{i:button_picture_04} Clear  ");
        }
        DrawHelp(text);
    }

    // Wait for the next input on the device and bind it. FALSE if left alone.
    bool Capture(bool pad, S32 row)
    {
        const iTime start = iTimeGet();
        char token[32];

        // The confirm press that got here is still down: forget it first.
        iPadHostCaptureKey(TRUE, token, sizeof(token));
        iPadHostCaptureButton(TRUE, token, sizeof(token));

        for (;;)
        {
            const S32 left = 8 - (S32)iTimeDiffSec(start, iTimeGet());
            if (left <= 0)
            {
                return false;
            }

            char help[160];
            snprintf(help, sizeof(help), "Press a %s for %s.{n}Esc to cancel (%d).",
                     pad ? "controller button" : "key",
                     row < kBindLabelCount ? kBindLabels[row] : kPadBindButtons[row].name,
                     (int)left);
            DrawHelp(help);

            zSaveLoad_Tick();

            // Escape cancels on either page, and so cannot be bound here.
            bool gotKey = iPadHostCaptureKey(FALSE, token, sizeof(token)) != 0;
            if (gotKey && strcmp(token, "escape") == 0)
            {
                return false;
            }
            if (!pad && gotKey)
            {
                break;
            }
            if (pad && iPadHostCaptureButton(FALSE, token, sizeof(token)))
            {
                break;
            }
        }

        char key[64];
        BindKey(pad, row, key, sizeof(key));
        iConfigSet(key, token);
        iPadHostReloadBindings();
        Send("MNU4 MOVE B SFX", eEventPlay);
        return true;
    }

    void RunBindPage(bool pad)
    {
        S32 sel = 0;
        S32 top = 0;

        for (S32 r = 0; r < ISETTINGS_ROWS; r++)
        {
            SelectRow(r, false);
        }
        SelectRow(0, true);
        DrawBindPage(pad, sel, top, NULL);

        for (;;)
        {
            zSaveLoad_Tick();
            const U32 pressed = mPad[globals.currentActivePad].pressed;

            if (pressed & kBack)
            {
                Send("MNU4 DENY SFX", eEventPlay);
                break;
            }

            S32 move = 0;
            if (pressed & XPAD_BUTTON_UP)
            {
                move = -1;
            }
            else if (pressed & XPAD_BUTTON_DOWN)
            {
                move = 1;
            }
            if (move != 0 && sel + move >= 0 && sel + move < BindRows())
            {
                SelectRow(sel - top, false);
                sel += move;
                if (sel < top)
                {
                    top = sel;
                }
                else if (sel >= top + ISETTINGS_ROWS)
                {
                    top = sel - ISETTINGS_ROWS + 1;
                }
                SelectRow(sel - top, true);
                DrawBindPage(pad, sel, top, NULL);
            }

            if (pressed & XPAD_BUTTON_X)
            {
                if (sel == kPadBindButtonCount)
                {
                    for (S32 i = 0; i < kPadBindButtonCount; i++)
                    {
                        char key[64];
                        BindKey(pad, i, key, sizeof(key));
                        iConfigUnset(key);
                    }
                    iPadHostReloadBindings();
                    Send("MNU4 MOVE B SFX", eEventPlay);
                }
                else
                {
                    Capture(pad, sel);
                }
                DrawBindPage(pad, sel, top, NULL);
            }
            else if ((pressed & XPAD_BUTTON_O) && sel < kPadBindButtonCount && MenuNeeds(sel))
            {
                Send("MNU4 DENY SFX", eEventPlay);
            }
            else if ((pressed & XPAD_BUTTON_O) && sel < kPadBindButtonCount)
            {
                // `none` rather than an empty value: bfbb_config reads an empty
                // line as "use the default". See iPadBind.h.
                char key[64];
                BindKey(pad, sel, key, sizeof(key));
                iConfigSet(key, "none");
                iPadHostReloadBindings();
                Send("MNU4 MOVE B SFX", eEventPlay);
                DrawBindPage(pad, sel, top, NULL);
            }
        }

        // Back to the list the page was opened from.
        SelectRow(sel - top, false);
        SelectRow(sSel - sTop, true);
        Draw();
    }

    // Left and right stop at the ends; X goes round.
    void ChangeTo(S32 to);

    void Change(S32 dir, bool wrap)
    {
        const Setting& s = Selected();
        if (IsBindEntry(s))
        {
            // Only Confirm opens a page; left and right have nothing to move.
            if (wrap)
            {
                RunBindPage(strcmp(s.key, "bind.pad") == 0);
            }
            return;
        }
        const S32 n = WordCount(Words(s));
        const S32 was = IndexOf(s);

        S32 to = (was < 0 ? 0 : was + dir);
        if (wrap)
        {
            to = (to + n) % n;
        }
        ChangeTo(to);
    }

    // The selected setting to its word `to`, applied, confirmed if it asks to
    // be. Nothing for a word it has already.
    void ChangeTo(S32 to)
    {
        const Setting& s = Selected();
        const S32 n = WordCount(Words(s));
        const S32 was = IndexOf(s);
        if (to < 0 || to >= n || to == was)
        {
            return;
        }

        char before[64];
        Current(s, before, sizeof(before));
        char word[64];
        Word(Words(s), to, word, sizeof(word));

        Send("MNU4 MOVE B SFX", eEventPlay);
        Store(s, word);

        bool live = s.when != RESTART && s.apply != NULL;
        if (live)
        {
            s.apply(word);

            // A window the renderer could not switch -- Vulkan's exclusive
            // fullscreen is set at startup -- is kept as chosen for next time.
            if (strcmp(s.key, "video.mode") == 0 && iWindowGetMode() != ModeFor(word))
            {
                live = false;
                sRestart = true;
            }
        }
        else if (s.when == RESTART)
        {
            sRestart = true;
        }

        Draw();

        if (live && s.confirm && !Confirm())
        {
            s.apply(before);
            Store(s, before);
            Draw();
        }
    }

    void Show(S32 fromPause)
    {
        if (fromPause)
        {
            Send("PAUSE OPTIONS GROUP", eEventUIFocusOff);
            Send("PAUSE OPTIONS GROUP", eEventInvisible);
        }
        else
        {
            Send("MNU3 START GROUP", eEventUIFocusOff);
            Send("MNU3 START GROUP", eEventInvisible);
            Send("MNU3 BLUE ALPHA 1 UI", eEventVisible);
        }

        Send(ISETTINGS_GROUP, eEventVisible);
        Send(ISETTINGS_GROUP, eEventUIFocusOn);
        for (S32 row = 0; row < ISETTINGS_ROWS; row++)
        {
            SelectRow(row, false);
        }
        SelectRow(sSel - sTop, true);
        Draw();
    }

    void Hide(S32 fromPause)
    {
        Send(ISETTINGS_GROUP, eEventUIFocusOff_Unselect);
        Send(ISETTINGS_GROUP, eEventInvisible);

        if (fromPause)
        {
            // As retail's save screen puts the pause menu back.
            gGameMode = eGameMode_Pause;
            Send("PAUSE OPTIONS GROUP", eEventVisible);
            Send("PAUSE OPTIONS BKG GROUP", eEventUIFocusOn_Select);
            Send("PAUSE OPTIONS GROUP", eEventUIFocusOn);
            Send("PAUSE OPTION MGR UIF", eEventUIFocusOn_Select);
            Send(ISETTINGS_PAUSE_ENTRY, eEventUIFocusOn_Select);
        }
        else
        {
            Send("MNU3 START GROUP", eEventVisible);
            Send("MNU3 START GROUP", eEventUIFocusOn);
            Send(ISETTINGS_TITLE_ENTRY, eEventUISelect);
        }
    }
} // namespace

S32 iSettingsRequested(S32 fromPause)
{
    _zUI* entry = (_zUI*)zSceneFindObject(
        xStrHash(fromPause ? ISETTINGS_PAUSE_ENTRY : ISETTINGS_TITLE_ENTRY));
    return entry != NULL && (entry->uiFlags & 2) != 0;
}

void iSettingsRun(S32 fromPause)
{
    BuildResolutions();
    sTab = TAB_VIDEO;
    sHoverTab = -1;
    BuildList();
    sSel = 0;
    sTop = 0;
    sRestart = false;

    Show(fromPause);
    iMenuMouseOwn(TRUE);

    for (;;)
    {
        zSaveLoad_Tick();
        const U32 pressed = mPad[globals.currentActivePad].pressed;

        if (pressed & kBack)
        {
            Send("MNU4 DENY SFX", eEventPlay);
            break;
        }

        S32 move = 0;
        if (pressed & XPAD_BUTTON_UP)
        {
            move = -1;
        }
        else if (pressed & XPAD_BUTTON_DOWN)
        {
            move = 1;
        }
        if (move != 0 && sSel + move >= 0 && sSel + move < sListCount)
        {
            SelectRow(sSel - sTop, false);
            sSel += move;
            if (sSel < sTop)
            {
                sTop = sSel;
            }
            else if (sSel >= sTop + ISETTINGS_ROWS)
            {
                sTop = sSel - ISETTINGS_ROWS + 1;
            }
            SelectRow(sSel - sTop, true);
            Draw();
        }

        ScrollbarClick();

        // The pointer selects the row it moves onto, and a click on a row is
        // the accept button on it, pressed next frame.
        const S32 pointed = PointerRow();

        // The arrow under the pointer is drawn lit, so the screen is redrawn
        // when that changes.
        const S32 part = PointerPart(pointed);
        if (pointed != sHoverRow || part != sHoverPart)
        {
            sHoverRow = pointed;
            sHoverPart = part;
            Draw();
        }

        if (pointed >= 0 && (iMenuMouseMoved() || iMenuMouseClicked()))
        {
            if (sTop + pointed != sSel)
            {
                SelectRow(sSel - sTop, false);
                sSel = sTop + pointed;
                SelectRow(pointed, true);
                Draw();
            }

            // An arrow steps that way, a notch is that level, and anywhere
            // else on the row is the accept button.
            if (iMenuMouseClicked())
            {
                if (part == PART_LEFT || part == PART_RIGHT)
                {
                    Change(part, false);
                }
                else if (part >= PART_NOTCH)
                {
                    ChangeTo(part - PART_NOTCH);
                }
                else
                {
                    iMenuMouseAccept();
                }
            }
        }

        // L1 and R1 turn the page, and go round.
        S32 page = 0;
        if (pressed & XPAD_BUTTON_L1)
        {
            page = -1;
        }
        else if (pressed & XPAD_BUTTON_R1)
        {
            page = 1;
        }
        S32 tab = (sTab + page + TAB_COUNT) % TAB_COUNT;

        // Or a click on the tab bar: a name, or the picture of the shoulder
        // button that turns that way. Asked every frame, which is what shows
        // the glove over the bar.
        const S32 pointedTab = PointerTab();
        if (pointedTab != sHoverTab)
        {
            sHoverTab = pointedTab;
            Draw();
        }
        if (page == 0 && iMenuMouseClicked() && pointedTab >= 0 && pointedTab != sTab)
        {
            tab = pointedTab;
            page = 1;
        }

        if (page != 0)
        {
            SelectRow(sSel - sTop, false);
            sTab = (Tab)tab;
            BuildList();
            sSel = 0;
            sTop = 0;
            // Selecting the row plays the turn's sound: its link's.
            SelectRow(0, true);
            Draw();
            continue;
        }

        if (pressed & XPAD_BUTTON_LEFT)
        {
            Change(-1, false);
        }
        else if (pressed & XPAD_BUTTON_RIGHT)
        {
            Change(1, false);
        }
        else if (pressed & XPAD_BUTTON_X)
        {
            Change(1, true);
        }
    }

    iMenuMouseOwn(FALSE);
    iConfigSave();
    Hide(fromPause);
}

void iSettingsApplyMusicVolume(S32 level)
{
    F32 v = 0.1f * (F32)level;
    xSndSetCategoryVol(SND_CAT_MUSIC, CLAMP(v, 0.0f, 1.0f));
    zMusicRefreshVolume();
}

void iSettingsApplySFXVolume(S32 level)
{
    F32 v = 0.1f * (F32)level;
    v = CLAMP(v, 0.0f, 1.0f);
    xSndSetCategoryVol(SND_CAT_GAME, v);
    xSndSetCategoryVol(SND_CAT_DIALOG, v);
    xSndSetCategoryVol(SND_CAT_UI, v);
}

void iSettingsApplySoundMode(S32 stereo)
{
    // The mixer reads iSnd's flag; zVar's Sound Mode reads gSnd's.
    iSndStereo(stereo ? 1 : 0);
    gSnd.stereo = stereo ? 1 : 0;
}

void iSettingsApplyVibration(S32 on, S32 pulse)
{
    globals.option_vibration = on ? 1 : 0;
    xPadRumbleEnable(globals.currentActivePad, globals.option_vibration);
    if (on && pulse)
    {
        zRumbleStart(globals.currentActivePad, SDR_Test);
    }
}

void iSettingsApplyAudio()
{
    // The fader only: nothing is playing yet, and zMusic is not up to refresh.
    F32 music = 0.1f * (F32)iConfigGetInt("audio.music_volume", 7);
    xSndSetCategoryVol(SND_CAT_MUSIC, CLAMP(music, 0.0f, 1.0f));
    iSettingsApplySFXVolume(iConfigGetInt("audio.sfx_volume", 10));
    iSettingsApplySoundMode(
        iHostStrCaseCmp(iConfigGetString("audio.sound_mode", "stereo"), "mono") != 0);
}
