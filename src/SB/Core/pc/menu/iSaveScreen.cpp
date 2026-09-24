// The PC save and load screens: one list of every save, newest first, in
// place of retail's folder chooser and its three slots.
//
// Retail's screens are zSaveLoad_LoadLoop and zSaveLoad_SaveLoop, a state
// machine that asks for a memory card and then a slot on it. This runs instead
// of them when the PC menus are on (iSGLoadLoop / iSGSaveLoop, isavegame.h),
// and underneath it is the same code: zSaveLoad_LoadGame and zSaveLoad_SaveGame
// do the work, on the folder and slot this picks, and the same prompts report
// what goes wrong. With the PC menus off, retail's loops run as they always did.
//
// The saves are every (folder, slot) pair the backend reaches -- retail's two
// folders and the PC's extra ones (iSGTargetMax) -- so saves made before this
// existed are in the list where they were. A new save goes in the first empty
// slot, retail's folders first.
//
// The widgets are the menu data's (iSaveScreen.h): a few rows whose text is
// rewritten as the list scrolls, inside the groups the retail screens focus.
// Up and down move the selection here; the rows' own links play the sounds and
// undo the screen on Back, as retail's rows did.

#include "iSaveScreen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iAssetOverride.h"
#include "iSaveThumb.h"
#include "iSettingsScreen.h"
#include "isavegame.h"
#include "xstransvc.h"
#include "zUI.h"
#include "xEvent.h"
#include "xPad.h"
#include "xString.h"
#include "xsavegame.h"
#include "zGame.h"
#include "zGameState.h"
#include "zGlobals.h"
#include "zSaveLoad.h"
#include "zScene.h"

// zSaveLoad.cpp's state, which the retail screens share through these and
// which the work this hands off to reads.
extern S32 promptSel;
extern S32 currentCard;
extern S32 currentGame;
extern S32 sAccessType;
extern U32 saveSuccess;
extern char sceneRead[32];
extern char* thumbIconMap[15];

namespace
{
    const S32 kMaxSaves = ISG_MAX_TARGETS * ISG_NUM_FILES;

    struct Entry
    {
        S32 tgt;
        S32 game;

        // "New save" rather than a save: the first empty slot.
        bool fresh;
        bool corrupt;

        char label[64];
        char when[32];
        U32 sortKey;
        S32 progress;
        S32 thumb;
    };

    Entry sEntries[kMaxSaves + 1];
    S32 sCount;
    S32 sSel;
    S32 sTop;

    void Send(const char* name, U32 event)
    {
        xBase* to = zSceneFindObject(xStrHash(name));
        if (to != NULL)
        {
            zEntEvent(to, event);
        }
    }

    void RowName(bool save, S32 row, char* out)
    {
        sprintf(out, save ? ISAVESCREEN_SAVE_ROW : ISAVESCREEN_LOAD_ROW, (int)row);
    }

    // xSGGameModDate's "MM/DD/YYYY HH:MM:SS" as a number that sorts: seconds
    // are dropped, and what is left fits in 32 bits until 4000 AD.
    U32 SortKey(const char* d)
    {
        int mon = 0, day = 0, year = 0, hr = 0, min = 0, sec = 0;
        if (sscanf(d, "%d/%d/%d %d:%d:%d", &mon, &day, &year, &hr, &min, &sec) < 3)
        {
            return 0;
        }
        return (U32)(((((year - 2000) * 13 + mon) * 32 + day) * 24 + hr) * 60 + min);
    }

    // "MM/DD/YY HH:MM" out of the same string.
    void ShortDate(const char* d, char* out, size_t size)
    {
        int mon = 0, day = 0, year = 0, hr = 0, min = 0, sec = 0;
        if (sscanf(d, "%d/%d/%d %d:%d:%d", &mon, &day, &year, &hr, &min, &sec) < 3)
        {
            snprintf(out, size, "%s", d);
            return;
        }
        snprintf(out, size, "%02d/%02d/%02d %02d:%02d", mon, day, year % 100, hr, min);
    }

    int NewestFirst(const void* a, const void* b)
    {
        const Entry* x = (const Entry*)a;
        const Entry* y = (const Entry*)b;
        if (x->sortKey != y->sortKey)
        {
            return x->sortKey > y->sortKey ? -1 : 1;
        }
        // Same minute: folder then slot, so the order does not shuffle.
        return (x->tgt * ISG_NUM_FILES + x->game) - (y->tgt * ISG_NUM_FILES + y->game);
    }

    // Every save the backend reaches, newest first, with "New save" on top for
    // the save screen when there is an empty slot to put one in.
    void Collect(bool save)
    {
        sCount = 0;
        S32 freeTgt = -1;
        S32 freeGame = -1;

        for (S32 tgt = 0; tgt < iSGTargetMax(); tgt++)
        {
            st_XSAVEGAME_DATA* sg = xSGInit(XSG_MODE_LOAD);
            xSGTgtSelect(sg, tgt);

            for (S32 g = 0; g < ISG_NUM_FILES; g++)
            {
                if (xSGGameIsEmpty(sg, g))
                {
                    if (freeTgt < 0)
                    {
                        freeTgt = tgt;
                        freeGame = g;
                    }
                    continue;
                }

                Entry& e = sEntries[sCount++];
                memset(&e, 0, sizeof(e));
                e.tgt = tgt;
                e.game = g;

                snprintf(e.label, sizeof(e.label), "%s", xSGGameLabel(sg, g));
                // As retail's slot list does: a save made on the menu is a
                // save in SpongeBob's house.
                if (strcmpi(e.label, zSceneGetLevelName('MNU3')) == 0)
                {
                    snprintf(e.label, sizeof(e.label), "%s", zSceneGetLevelName('HB01'));
                }
                e.corrupt = !IsValidName(e.label);

                const char* date = xSGGameModDate(sg, g);
                e.sortKey = SortKey(date);
                ShortDate(date, e.when, sizeof(e.when));
                e.progress = xSGGameProgress(sg, g);
                e.thumb = e.label[0] != '\0' ? xSGGameThumbIndex(sg, g) : -1;
            }

            xSGDone(sg);
        }

        qsort(sEntries, (size_t)sCount, sizeof(Entry), NewestFirst);

        if (save && freeTgt >= 0)
        {
            memmove(&sEntries[1], &sEntries[0], sizeof(Entry) * (size_t)sCount);
            Entry& e = sEntries[0];
            memset(&e, 0, sizeof(e));
            e.fresh = true;
            e.tgt = freeTgt;
            e.game = freeGame;
            e.thumb = -1;
            sCount++;
        }
    }

    void DescribeEntry(const Entry& e, char* out, size_t size)
    {
        if (e.fresh)
        {
            snprintf(out, size, "New save");
        }
        else if (e.corrupt)
        {
            snprintf(out, size, "Corrupt save file{n}%s", e.when);
        }
        else
        {
            snprintf(out, size, "%s{n}%d%%   %s", e.label, (int)e.progress, e.when);
        }
    }

    // The picture beside the list: the save's own still when it has one
    // (iSaveThumb.h), retail's stock picture of the level when it does not.
    //
    // The still is served to the thumbnail widget as a run-time asset under
    // this name, and the widget's box is reshaped to it; the stock pictures
    // are square, and the box goes back to retail's for them.
    const char* const kStill = "PC SAVE STILL";

    // Retail's box, and the width a still is drawn at inside the same centre.
    const F32 kBoxX = 430.0f;
    const F32 kBoxY = 180.0f;
    const F32 kBoxSize = 128.0f;
    const F32 kStillWidth = 176.0f;

    RwTexture* sStill;

    zUIAsset* ThumbAsset(bool save)
    {
        return (zUIAsset*)xSTFindAsset(xStrHash(save ? "MNU4 THUMBICON" : "MNU3 THUMBICON"),
                                       NULL);
    }

    void ThumbBox(bool save, F32 aspect)
    {
        zUIAsset* a = ThumbAsset(save);
        if (a == NULL)
        {
            return;
        }

        F32 w = kBoxSize;
        F32 h = kBoxSize;
        if (aspect > 0.0f)
        {
            w = kStillWidth;
            h = kStillWidth / aspect;
        }
        a->dim[0] = (U16)(w + 0.5f);
        a->dim[1] = (U16)(h + 0.5f);
        a->pos.x = kBoxX + 0.5f * kBoxSize - 0.5f * w;
        a->pos.y = kBoxY + 0.5f * kBoxSize - 0.5f * h;
    }

    // Take the still away. The widget is pointed at nothing FIRST: it looks its
    // texture up every frame and does not check what it gets back.
    void DropStill(bool save)
    {
        if (sStill == NULL)
        {
            return;
        }
        zChangeThumbIcon("");
        iAssetOverrideSetRuntime(xStrHash(kStill), NULL);
        iSaveThumbFree(sStill);
        sStill = NULL;
        ThumbBox(save, 0.0f);
    }

    void ShowPicture(bool save, const Entry& e)
    {
        DropStill(save);

        char path[512];
        F32 aspect = 0.0f;
        if (!e.fresh && iSGThumbPath(e.tgt, e.game, path, sizeof(path)))
        {
            sStill = iSaveThumbLoad(path, &aspect);
        }

        if (sStill != NULL)
        {
            iAssetOverrideSetRuntime(xStrHash(kStill), sStill);
            ThumbBox(save, aspect);
            zChangeThumbIcon(kStill);
            zSendEventToThumbIcon(eEventVisible);
            return;
        }

        if (e.thumb >= 0 && e.thumb < 15)
        {
            zChangeThumbIcon(thumbIconMap[e.thumb]);
            zSendEventToThumbIcon(eEventVisible);
        }
        else
        {
            zSendEventToThumbIcon(eEventInvisible);
        }
    }

    // The title, the rows' text, and which row is selected.
    void Draw(bool save)
    {
        char text[ISAVESCREEN_ROW_TEXT_SIZE];

        S32 saves = sCount - ((save && sCount > 0 && sEntries[0].fresh) ? 1 : 0);
        char title[64];
        if (saves == 0)
        {
            snprintf(title, sizeof(title), save ? "Save Game" : "Load saved game");
        }
        else
        {
            snprintf(title, sizeof(title), "%s{n}%d of %d", save ? "Save Game" : "Load saved game",
                     (int)(sSel + 1), (int)sCount);
        }
        iAssetTextSet(xStrHash(save ? "SAVE GAME TXT" : "LD LOAD GAME TXT"), title);

        for (S32 row = 0; row < ISAVESCREEN_ROWS; row++)
        {
            const S32 i = sTop + row;
            text[0] = '\0';
            if (i < sCount)
            {
                DescribeEntry(sEntries[i], text, sizeof(text));
            }
            else if (i == 0)
            {
                snprintf(text, sizeof(text), "No saved games were found.");
            }

            char name[32];
            sprintf(name, ISAVESCREEN_ROW_TEXT, (int)row);
            iAssetTextSet(xStrHash(name), text);
        }

        if (sCount == 0)
        {
            zSendEventToThumbIcon(eEventInvisible);
            return;
        }

        ShowPicture(save, sEntries[sSel]);
    }

    void SelectRow(bool save, S32 row, bool on)
    {
        char name[32];
        RowName(save, row, name);
        Send(name, on ? eEventUISelect : eEventUIUnselect);
    }

    void Show(bool save)
    {
        if (save)
        {
            Send("SV SAVE GAME TITLE UIF", eEventVisible);
            Send("BLUE ALPHA 1 UI", eEventVisible);
            Send("MNU4 DISK FREE", eEventUIFocusOn);
            Send("SV GAMESLOT GROUP", eEventUIFocusOn);
        }
        else
        {
            Send("MNU3 START GROUP", eEventUIFocusOff_Unselect);
            Send("MNU3 LD TITLE GROUP", eEventVisible);
            Send("MNU3 BLUE ALPHA 1 UI", eEventVisible);
            Send("MNU3 DISK FREE", eEventUIFocusOn);
            Send("LD GAMESLOT GROUP", eEventEnable);
            Send("LD GAMESLOT GROUP", eEventUIFocusOn);
        }

        for (S32 row = 0; row < ISAVESCREEN_ROWS; row++)
        {
            SelectRow(save, row, false);
        }
        if (sCount > 0)
        {
            SelectRow(save, sSel - sTop, true);
        }
        Draw(save);
    }

    // Up and down. Returns once the player has chosen a save (TRUE, the choice
    // in sSel) or backed out (FALSE; the rows' Back links have already taken
    // the screen down).
    bool Choose(bool save)
    {
        promptSel = -1;

        for (;;)
        {
            zSaveLoad_Tick();

            if (promptSel == 4)
            {
                return false;
            }

            const U32 pressed = mPad[globals.currentActivePad].pressed;
            S32 move = 0;
            if (pressed & XPAD_BUTTON_UP)
            {
                move = -1;
            }
            else if (pressed & XPAD_BUTTON_DOWN)
            {
                move = 1;
            }

            if (move != 0 && sSel + move >= 0 && sSel + move < sCount)
            {
                SelectRow(save, sSel - sTop, false);
                sSel += move;
                if (sSel < sTop)
                {
                    sTop = sSel;
                }
                else if (sSel >= sTop + ISAVESCREEN_ROWS)
                {
                    sTop = sSel - ISAVESCREEN_ROWS + 1;
                }
                SelectRow(save, sSel - sTop, true);
                Draw(save);
            }

            if ((pressed & XPAD_BUTTON_X) && sCount > 0)
            {
                return true;
            }
        }
    }

    U32 SceneCode()
    {
        return (U32)sceneRead[0] << 24 | (U32)sceneRead[1] << 16 | (U32)sceneRead[2] << 8 |
               (U32)sceneRead[3];
    }

    U32 LoadScreen()
    {
        zSaveLoadInit();
        sAccessType = 1;
        Collect(false);
        sSel = 0;
        sTop = 0;

        for (;;)
        {
            Show(false);
            if (!Choose(false))
            {
                // As retail's loop ends on Back: to the title.
                zGameModeSwitch(eGameMode_Title);
                zGameStateSwitch(0);
                break;
            }

            const Entry& e = sEntries[sSel];
            currentCard = e.tgt;
            currentGame = e.game;

            Send("LD GAMESLOT GROUP", eEventUIFocusOff_Unselect);
            Send("LD MAKE INVISIBLE", eEventInvisible);

            const S32 rc = zSaveLoad_LoadGame();
            if (rc == 1)
            {
                zGameModeSwitch(eGameMode_Game);
                zGameStateSwitch(0);
                globals.autoSaveFeature = 1;
                break;
            }

            if (rc == 7)
            {
                zSaveLoad_DamagedSaveGameErrorPrompt(1);
            }
            else
            {
                zSaveLoad_ErrorPrompt(1);
            }
        }

        DropStill(false);
        zSendEventToThumbIcon(eEventInvisible);
        return SceneCode();
    }

    // What retail's SaveLoop does when the folder chooser is backed out of:
    // put the pause menu's Options back, or the new-game question on the title.
    void SaveBack()
    {
        if (gGameState != 0)
        {
            gGameMode = eGameMode_Pause;
            Send("PAUSE OPTIONS BKG GROUP", eEventUIFocusOn_Select);
            Send("PAUSE OPTIONS GROUP", eEventUIFocusOn);
            Send("PAUSE OPTION MGR UIF", eEventUIFocusOn_Select);
            Send("PAUSE OPTION SAVE UIF", eEventUIFocusOn_Select);
        }
        else
        {
            gGameMode = eGameMode_Title;
            Send("MNU3 START CREATE NEW GAME NO", eEventUIFocusOn);
            Send("MNU3 START CREATE NEW GAME NO", eEventUIUnselect);
            Send("MNU3 START CREATE NEW GAME YES", eEventUIFocusOn_Select);
            Send("MNU3 START CREATE NEW GAME", eEventUIFocusOn);
            Send("BLUE ALPHA 1 UI", eEventInvisible);
        }
    }

    U32 SaveScreen()
    {
        zSaveLoadInit();
        saveSuccess = 0;
        Collect(true);
        sSel = 0;
        sTop = 0;

        for (;;)
        {
            Show(true);
            if (!Choose(true))
            {
                SaveBack();
                break;
            }

            const Entry& e = sEntries[sSel];
            currentCard = e.tgt;
            currentGame = e.game;

            if (!e.fresh)
            {
                // Retail's own question, and its answers: 3 is yes, 2 and 4
                // are no, 5 is the folder gone.
                const S32 answer = e.corrupt ? zSaveLoad_CardPromptOverwriteDamaged()
                                             : zSaveLoad_CardPromptOverwrite();
                if (answer != 3)
                {
                    if (answer == 5)
                    {
                        zSaveLoad_ErrorPrompt(0);
                    }
                    continue;
                }
            }
            else
            {
                iSGMakeTarget(e.tgt);
            }

            Send("SV GAMESLOT GROUP", eEventUIFocusOff_Unselect);
            Send("SV MAKE INVISIBLE", eEventInvisible);
            DropStill(true);

            // The still is the frame the pause kept: the pause menu has been
            // on screen since. From the title there is no game to picture.
            iSGSetThumbSource(gGameState != 0 ? ISG_THUMB_KEPT : ISG_THUMB_NONE);
            sAccessType = 2;
            const S32 rc = zSaveLoad_SaveGame();
            iSGSetThumbSource(ISG_THUMB_NOW);
            if (rc == 1)
            {
                zGameModeSwitch(eGameMode_Game);
                zGameStateSwitch(1);
                saveSuccess = 1;
                Send("MNU4 SAVE COMPLETED", eEventVisible);
                break;
            }

            switch (rc)
            {
            case 7:
                zSaveLoad_SaveDamagedErrorPrompt(0);
                break;
            case 8:
                zSaveLoad_CardYankedErrorPrompt(0);
                break;
            case 10:
                zSaveLoad_CardPromptSpace(0);
                break;
            default:
                zSaveLoad_ErrorPrompt(0);
                break;
            }

            // A failed save may have left a slot behind or taken one.
            Collect(true);
            if (sSel >= sCount)
            {
                sSel = sCount > 0 ? sCount - 1 : 0;
            }
            if (sTop > sSel)
            {
                sTop = sSel;
            }
        }

        DropStill(true);
        sAccessType = 0;
        return saveSuccess;
    }
} // namespace

// The title's Settings entry comes through Load mode and the pause menu's
// through Save mode; see iSettingsScreen.cpp.
U32 iSGLoadLoop()
{
    if (!iAssetOverrideEnabled())
    {
        return zSaveLoad_LoadLoop();
    }
    if (iSettingsRequested(FALSE))
    {
        iSettingsRun(FALSE);
        zGameModeSwitch(eGameMode_Title);
        zGameStateSwitch(0);

        // zMenuUpdateMode reads '0000' as "stay on the menu".
        return '0000';
    }
    return LoadScreen();
}

U32 iSGSaveLoop()
{
    if (!iAssetOverrideEnabled())
    {
        return zSaveLoad_SaveLoop();
    }
    if (iSettingsRequested(TRUE))
    {
        iSettingsRun(TRUE);
        return 0;
    }
    return SaveScreen();
}
