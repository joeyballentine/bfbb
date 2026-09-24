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
#include "iDistort.h"
#include "iDrawDist.h"
#include "iGlow.h"
#include "iHost.h"
#include "iPadGlyph.h"
#include "iPadStick.h"
#include "iScreen.h"
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

namespace
{
    enum When
    {
        NOW,
        NEXT_AREA,
        RESTART
    };

    // The tabs, in order. The shoulder buttons move between them.
    enum Tab
    {
        TAB_DISPLAY,
        TAB_EFFECTS,
        TAB_CONTROLS,
        TAB_GAME,
        TAB_COUNT
    };
    const char* const kTabNames[TAB_COUNT] = { "Display", "Effects", "Controls", "Game" };

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

    void ApplyMode(const char* w)
    {
        iWindowSetMode(iHostStrCaseCmp(w, "windowed") == 0 ? iWINDOW_WINDOWED : iWINDOW_BORDERLESS);
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

    const Setting kSettings[] = {
        { TAB_DISPLAY, "Window", "video.mode", "borderless|windowed|fullscreen",
          "Borderless|Windowed|Fullscreen", NOW,
          "Borderless covers the screen without taking it over. Alt+Enter switches too.",
          ApplyMode, true },
        { TAB_DISPLAY, "Resolution", "video.resolution", NULL, NULL, RESTART,
          "The size the game renders at. The picture is scaled to the window.", NULL, false },
        { TAB_DISPLAY, "VSync", "video.vsync", "on|off", "On|Off", NOW,
          "Wait for the display between frames, so the picture does not tear.", ApplyVSync,
          false },
        { TAB_DISPLAY, "Frame rate limit", "video.framerate", "30|60|120|144|165|240|display|off",
          "30|60|120|144|165|240|Monitor|Unlimited", NOW,
          "The most frames a second the game runs at. 60 is the console's.", ApplyFrameRate,
          false },
        { TAB_DISPLAY, "Field of view", "video.fov", "60|65|70|75|80|85|90|95|100|105|110", NULL, NOW,
          "How wide the camera sees, in degrees across a 4:3 picture. 75 is the console's.",
          ApplyFOV, false },
        { TAB_DISPLAY, "HUD layout", "video.ui", "pillarbox|native", "4:3, as the console|Screen edges", NOW,
          "Where the HUD sits on a wide screen.", ApplyUI, false },
        { TAB_DISPLAY, "Draw distance", "video.draw_distance", "on|off", "Unlimited|Console", NEXT_AREA,
          "How far away things are still drawn.", ApplyDrawDistance, false },
        { TAB_DISPLAY, "Anti-aliasing", "video.msaa", "1|2|4|8", "Off|2x|4x|8x", RESTART,
          "Smooths jagged edges, at a cost in speed.", NULL, false },
        { TAB_EFFECTS, "Glow", "xbox.glow", "on|off", "On|Off", NOW,
          "The Xbox version's soft glow around bright things.", ApplyGlow, false },
        { TAB_EFFECTS, "Screen warps", "xbox.distortion", "on|off", "On|Off", NOW,
          "The Xbox version's heat-haze and cruise-bubble screen effects.", ApplyDistortion,
          false },
        { TAB_EFFECTS, "Loading-screen still", "xbox.snapshot", "on|off", "On|Off", NOW,
          "Show the level being left behind the loading screen. Save pictures need it.",
          ApplySnapshot, false },
        { TAB_EFFECTS, "Cave echo", "xbox.reverb", "on|off", "On|Off", NEXT_AREA,
          "The Xbox version's echo in caves and big rooms.", NULL, false },
        { TAB_CONTROLS, "Stick deadzone", "input.deadzone", "auto|5|10|15|20|25|30",
          "Controller's own|5%|10%|15%|20%|25%|30%", NOW,
          "How far a stick moves before the game notices.", ApplyDeadzone, false },
        { TAB_CONTROLS, "Button pictures", "input.button_icons", "auto|xbox|gamecube|ps2|off",
          "Match the controller|Xbox|GameCube|PlayStation|The game's own", NOW,
          "Which controller's buttons the prompts show.", ApplyIcons, false },
        { TAB_CONTROLS, "Camera speed", "input.camera_sensitivity", "0.5|0.75|1.0|1.25|1.5|2.0",
          "0.5x|0.75x|1x|1.25x|1.5x|2x", NOW, "How fast the right stick turns the camera.",
          ApplyCameraSpeed, false },
        { TAB_GAME, "Intro movies", "game.intro_movies", "on|off", "On|Off", RESTART,
          "The logos before the title screen.", NULL, false },
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
            const char* when = s.when == RESTART     ? "{n}Takes effect when the game next starts."
                               : s.when == NEXT_AREA ? "{n}Takes effect in the next area."
                                                     : "{n}";
            snprintf(help, sizeof(help), "%s%s%s", s.help, when,
                     sRestart ? "{n}Some changes wait for the game to restart." : "");
        }
        iAssetTextSet(xStrHash(ISETTINGS_HELP_TEXT), help);
    }

    void Draw()
    {
        // The tab bar: the shoulder buttons' pictures either side, the tab
        // showing in the menu's own dark teal and the rest faded toward the
        // background.
        char title[256];
        snprintf(title, sizeof(title), "{i:button_picture_07} ");
        for (S32 t = 0; t < TAB_COUNT; t++)
        {
            char one[48];
            snprintf(one, sizeof(one), t == sTab ? "%s   " : "{c=ff6f98a3}%s{~:c}   ",
                     kTabNames[t]);
            strncat(title, one, sizeof(title) - strlen(title) - 1);
        }
        strncat(title, "{i:button_picture_05}", sizeof(title) - strlen(title) - 1);
        iAssetTextSet(xStrHash(ISETTINGS_TITLE_TEXT), title);

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
            char value[80];
            Name(s, IndexOf(s), value, sizeof(value));

            char shown[96];
            snprintf(shown, sizeof(shown), "< %s >", value);
            SetText(ISETTINGS_LABEL_TEXT, row, s.label);
            SetText(ISETTINGS_VALUE_TEXT, row, shown);
        }

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
                     "Keep this? {i:button_picture_01} keeps it, {i:button_picture_03} puts it "
                     "back.{n}Putting it back in %d.",
                     (int)left);
            DrawHelp(text);

            zSaveLoad_Tick();
            const U32 pressed = mPad[globals.currentActivePad].pressed;
            if (pressed & XPAD_BUTTON_X)
            {
                return true;
            }
            if (pressed & XPAD_BUTTON_TRIANGLE)
            {
                return false;
            }
        }
    }

    // Left and right stop at the ends; X goes round.
    void Change(S32 dir, bool wrap)
    {
        const Setting& s = Selected();
        const S32 n = WordCount(Words(s));
        const S32 was = IndexOf(s);

        S32 to = (was < 0 ? 0 : was + dir);
        if (wrap)
        {
            to = (to + n) % n;
        }
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

        // Exclusive fullscreen is a video mode the renderer picks at startup,
        // and so is going back from it.
        const bool live = s.when != RESTART && s.apply != NULL &&
                          !(strcmp(s.key, "video.mode") == 0 &&
                            (iHostStrCaseCmp(word, "fullscreen") == 0 ||
                             iWindowGetMode() == iWINDOW_FULLSCREEN));
        if (live)
        {
            s.apply(word);
        }
        else if (s.when == RESTART || strcmp(s.key, "video.mode") == 0)
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
    sTab = TAB_DISPLAY;
    BuildList();
    sSel = 0;
    sTop = 0;
    sRestart = false;

    Show(fromPause);

    for (;;)
    {
        zSaveLoad_Tick();
        const U32 pressed = mPad[globals.currentActivePad].pressed;

        if (pressed & XPAD_BUTTON_TRIANGLE)
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
        if (page != 0)
        {
            SelectRow(sSel - sTop, false);
            sTab = (Tab)((sTab + page + TAB_COUNT) % TAB_COUNT);
            BuildList();
            sSel = 0;
            sTop = 0;
            SelectRow(0, true);
            Send("MNU4 MOVE B SFX", eEventPlay);
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

    iConfigSave();
    Hide(fromPause);
}
