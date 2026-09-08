// bfbb_config -- the settings front end. What it edits and why it is a
// separate program is in README.md beside this file.
//
// Everything it knows about the settings comes from kConfigSettings, and every
// byte it writes goes through iConfigEdit. It has no list of its own: adding a
// setting to the table gives it a control here, with its default, its
// description and the values it accepts, and nothing in this file has to be
// told.

#include "iConfigEdit.h"
#include "iConfigTable.h"
#include "iHost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// windows.h first, and not sorted with the rest: the other three are all
// written against the types it defines and do not include it themselves.
#include <windows.h>

#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>

namespace
{
    const S32 kMaxValue = 512;
    const S32 kMaxPath = 512;

    // Control ids. The rows are built at runtime, so a row's controls are
    // numbered from a base and the row is recovered by dividing.
    const int kIdSectionList = 100;
    const int kIdSave = 101;
    const int kIdCancel = 102;
    const int kIdResetSection = 103;

    const int kIdRowBase = 1000;
    const int kIdsPerRow = 4;
    const int kIdRowValue = 0;
    const int kIdRowBrowse = 1;

    // What the file said when it was opened, what the user has typed, and
    // whether the file mentioned it at all.
    //
    // `present` is not decoration. A setting the file does not name is one the
    // game answers from the table, and writing it out at its current value
    // would pin it to today's default -- so a value that was never touched is
    // left out of the file entirely, and one that was is written.
    struct Value
    {
        char text[kMaxValue];
        bool present;
    };

    // One setting, laid out.
    //
    // The four windows are created once, when the section is shown, and only
    // MOVED after that. Scrolling a pane by destroying and rebuilding its
    // contents flickers, loses the caret and throws away half-typed text.
    struct Row
    {
        S32 setting;
        HWND label;
        HWND control;
        HWND browse;
        HWND desc;

        // The description's height at the width it was last measured for. Kept
        // rather than measured every time, because a scroll changes neither
        // the text nor the width and re-measuring every row on every wheel
        // notch is the other way to make scrolling feel wrong.
        int descH;
    };

    struct App
    {
        HWND main;
        HWND sections;
        HWND pane;
        HWND status;
        HFONT font;

        char path[kMaxPath];
        iConfigEditFile* file;

        Value values[128];

        // The distinct sections of kConfigSettings, in table order.
        const char* sectionNames[16];
        S32 sectionCount;
        S32 section;

        Row rows[64];
        S32 rowCount;

        // The laid-out height of the pane's contents, and how far down it is
        // scrolled. Both in pixels.
        int contentHeight;
        int scroll;

        // The pane width the descriptions were last measured at. A resize
        // changes how they wrap and so how tall each row is; a scroll does
        // not, and this is what tells the two apart.
        int measuredWidth;

        int dpi;
    };

    App gApp;

    // Every length in this file is written for 96 DPI and passed through here.
    int px(int atNinetySix)
    {
        return MulDiv(atNinetySix, gApp.dpi, 96);
    }

    // -------------------------------------------------------------------
    // The settings, as values

    void collectSections()
    {
        gApp.sectionCount = 0;
        for (S32 i = 0; i < kConfigSettingCount; i++)
        {
            bool seen = false;
            for (S32 j = 0; j < gApp.sectionCount; j++)
            {
                if (strcmp(gApp.sectionNames[j], kConfigSettings[i].section) == 0)
                {
                    seen = true;
                    break;
                }
            }
            if (!seen &&
                gApp.sectionCount < (S32)(sizeof(gApp.sectionNames) / sizeof(gApp.sectionNames[0])))
            {
                gApp.sectionNames[gApp.sectionCount++] = kConfigSettings[i].section;
            }
        }
    }

    // Fill `values` from the file, falling back to the table's default for a
    // setting the file does not mention -- which is what the game would run
    // with, so it is what the control should show.
    void loadValues()
    {
        for (S32 i = 0; i < kConfigSettingCount; i++)
        {
            const iConfigSetting* s = &kConfigSettings[i];
            const char* have = iConfigEditGet(gApp.file, s->section, s->name);

            gApp.values[i].present = (have != NULL);
            snprintf(gApp.values[i].text, kMaxValue, "%s", have != NULL ? have : s->value);
        }
    }

    bool isDefault(S32 i)
    {
        return iHostStrCaseCmp(gApp.values[i].text, kConfigSettings[i].value) == 0;
    }

    // -------------------------------------------------------------------
    // Reading the choices column

    // The n'th '|'-separated word of `choices`, or false past the end.
    bool choiceAt(const char* choices, S32 index, char* out, size_t outSize)
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

    // -------------------------------------------------------------------
    // Building one row's control

    bool wantsCombo(const iConfigSetting* s)
    {
        return s->kind == ICONFIG_ENUM || s->choices != NULL;
    }

    bool wantsBrowse(const iConfigSetting* s)
    {
        return s->kind == ICONFIG_FOLDER || s->kind == ICONFIG_FONT;
    }

    // Everything a row shows below its control: what the setting is for, and
    // -- only when the value is not the default -- what the default was. That
    // second line is the answer to "what did this used to say", which is the
    // question someone has after changing four things and disliking the
    // result.
    void describe(S32 i, char* out, size_t outSize)
    {
        char summary[512];
        iConfigTableSummary(&kConfigSettings[i], summary, sizeof(summary));

        if (isDefault(i))
        {
            snprintf(out, outSize, "%s", summary);
            return;
        }

        const char* def = kConfigSettings[i].value;
        snprintf(out, outSize, "%s  (default: %s)", summary, def[0] != '\0' ? def : "empty");
    }

    // The flags an SS_LEFT | SS_NOPREFIX static draws its text with. The
    // measurement below and the control itself have to agree exactly, or a
    // description is given the height of one wrap and drawn at another --
    // which loses whichever line does not fit.
    const UINT kDescFlags = DT_WORDBREAK | DT_EXPANDTABS | DT_NOPREFIX;

    int measureText(HWND parent, const char* text, int width)
    {
        HDC dc = GetDC(parent);
        HFONT old = (HFONT)SelectObject(dc, gApp.font);

        RECT r = { 0, 0, width, 0 };
        DrawTextA(dc, text, -1, &r, DT_CALCRECT | kDescFlags);

        SelectObject(dc, old);
        ReleaseDC(parent, dc);
        return r.bottom - r.top;
    }

    // A closed combo box answers the wheel by CHANGING ITS SELECTION. Over a
    // scrolling pane that means running the wheel down the window silently
    // switches the window mode, the preset and the UI anchoring on the way
    // past, and the file records every one of them.
    //
    // So a closed one hands the wheel to the pane instead. An open one keeps
    // it, because then the wheel is picking from the list, which is what the
    // pointer is over and what the user meant.
    LRESULT CALLBACK comboWheelProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id,
                                    DWORD_PTR)
    {
        if (msg == WM_MOUSEWHEEL && SendMessage(wnd, CB_GETDROPPEDSTATE, 0, 0) == FALSE)
        {
            SendMessage(GetParent(wnd), WM_MOUSEWHEEL, wp, lp);
            return 0;
        }

        if (msg == WM_NCDESTROY)
        {
            RemoveWindowSubclass(wnd, comboWheelProc, id);
        }

        return DefSubclassProc(wnd, msg, wp, lp);
    }

    HWND make(const char* cls, const char* text, DWORD style, int x, int y, int w, int h, int id)
    {
        HWND h2 = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                                  gApp.pane, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
        SendMessage(h2, WM_SETFONT, (WPARAM)gApp.font, TRUE);
        return h2;
    }

    void destroyRows()
    {
        for (S32 i = 0; i < gApp.rowCount; i++)
        {
            if (gApp.rows[i].label != NULL)
            {
                DestroyWindow(gApp.rows[i].label);
            }
            if (gApp.rows[i].control != NULL)
            {
                DestroyWindow(gApp.rows[i].control);
            }
            if (gApp.rows[i].browse != NULL)
            {
                DestroyWindow(gApp.rows[i].browse);
            }
            if (gApp.rows[i].desc != NULL)
            {
                DestroyWindow(gApp.rows[i].desc);
            }
        }
        gApp.rowCount = 0;
    }

    // Where a row's four windows go, in CONTENT coordinates -- measured from
    // the top of the whole section, not from the top of the pane. What is on
    // screen is this shifted up by gApp.scroll, and that subtraction is the
    // only thing scrolling does.
    struct RowRects
    {
        RECT label;
        RECT control;
        RECT browse;
        RECT desc;
        int bottom;
    };

    // The geometry for one row at pane width `width`, starting at content y.
    // Shared by the code that creates the windows and the code that moves
    // them, so a row cannot be built at one size and positioned at another.
    RowRects rowRects(const Row* row, int width, int y)
    {
        const iConfigSetting* s = &kConfigSettings[row->setting];

        const int margin = px(14);
        const int labelW = px(150);
        const int gap = px(10);
        const int controlH = px(23);
        const int browseW = px(78);
        const int rowGap = px(16);

        int controlX = margin + labelW + gap;

        // A path is worth all the width there is; everything else is a number
        // or a word and a fixed box reads as a column.
        int controlW = px(200);
        if (s->kind == ICONFIG_FOLDER || s->kind == ICONFIG_FONT || s->kind == ICONFIG_STRING)
        {
            controlW = width - controlX - margin;
            if (wantsBrowse(s))
            {
                controlW -= browseW + px(6);
            }
            if (controlW < px(120))
            {
                controlW = px(120);
            }
        }

        int descW = width - margin * 2;
        if (descW < px(120))
        {
            descW = px(120);
        }

        RowRects r;
        memset(&r, 0, sizeof(r));

        SetRect(&r.label, margin, y + px(4), margin + labelW, y + px(4) + px(18));

        if (s->kind == ICONFIG_BOOL)
        {
            SetRect(&r.control, controlX, y + px(3), controlX + px(60), y + px(3) + px(18));
        }
        else if (wantsCombo(s))
        {
            // A combo box's height is how far the LIST drops, not how tall the
            // box is -- that follows the font. So it is given the drop height
            // here and every time it is moved, or the list would open one item
            // tall after the first scroll.
            SetRect(&r.control, controlX, y, controlX + controlW, y + px(220));
        }
        else
        {
            SetRect(&r.control, controlX, y, controlX + controlW, y + controlH);
        }

        if (wantsBrowse(s))
        {
            SetRect(&r.browse, controlX + controlW + px(6), y,
                    controlX + controlW + px(6) + browseW, y + controlH);
        }

        int below = y + controlH + px(3);
        SetRect(&r.desc, margin, below, margin + descW, below + row->descH);

        r.bottom = below + row->descH + rowGap;
        return r;
    }

    int paneWidth()
    {
        RECT client;
        GetClientRect(gApp.pane, &client);
        return client.right - client.left;
    }

    int panePage()
    {
        RECT client;
        GetClientRect(gApp.pane, &client);
        return client.bottom - client.top;
    }

    // The description a row should be showing, set on the window and measured.
    // Only when it has actually changed: SetWindowText repaints, and this runs
    // for every row whenever any one value changes.
    void refreshDescription(Row* row, int width)
    {
        char want[768];
        describe(row->setting, want, sizeof(want));

        char have[768];
        GetWindowTextA(row->desc, have, sizeof(have));

        int descW = width - px(14) * 2;
        if (descW < px(120))
        {
            descW = px(120);
        }

        if (strcmp(want, have) != 0)
        {
            SetWindowTextA(row->desc, want);
        }

        row->descH = measureText(gApp.pane, want, descW);
    }

    void updateScrollBar()
    {
        int page = panePage();

        int most = gApp.contentHeight - page;
        if (most < 0)
        {
            most = 0;
        }
        if (gApp.scroll > most)
        {
            gApp.scroll = most;
        }
        if (gApp.scroll < 0)
        {
            gApp.scroll = 0;
        }

        SCROLLINFO si;
        memset(&si, 0, sizeof(si));
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
        si.nMin = 0;
        si.nMax = gApp.contentHeight > 0 ? gApp.contentHeight - 1 : 0;
        si.nPage = (UINT)page;
        si.nPos = gApp.scroll;
        SetScrollInfo(gApp.pane, SB_VERT, &si, TRUE);
    }

    // Put every row's windows where the current width and scroll say they go.
    // Nothing is created or destroyed here.
    //
    // `remeasure` re-reads each description and works out how tall it wraps to,
    // which is needed when the text changed or the pane was resized and is
    // wasted work on a scroll.
    //
    // The moves go through one DeferWindowPos batch so the whole pane arrives
    // in a single frame. Moving forty windows one at a time is what a scroll
    // that tears and flickers looks like.
    void layoutRows(bool remeasure)
    {
        int width = paneWidth();

        if (remeasure || width != gApp.measuredWidth)
        {
            for (S32 i = 0; i < gApp.rowCount; i++)
            {
                refreshDescription(&gApp.rows[i], width);
            }
            gApp.measuredWidth = width;
        }

        // The content height has to be known before anything is placed, so
        // that updateScrollBar can clamp a scroll that is now past the end --
        // otherwise a resize that makes the content shorter leaves the rows
        // pushed off the top.
        int y = px(14);
        for (S32 i = 0; i < gApp.rowCount; i++)
        {
            y = rowRects(&gApp.rows[i], width, y).bottom;
        }
        gApp.contentHeight = y;

        updateScrollBar();

        HDWP dwp = BeginDeferWindowPos(gApp.rowCount * 4);

        y = px(14);
        for (S32 i = 0; i < gApp.rowCount; i++)
        {
            Row* row = &gApp.rows[i];
            RowRects r = rowRects(row, width, y);

            struct
            {
                HWND wnd;
                const RECT* at;
            } parts[] = {
                { row->label, &r.label },
                { row->control, &r.control },
                { row->browse, &r.browse },
                { row->desc, &r.desc },
            };

            for (size_t p = 0; p < sizeof(parts) / sizeof(parts[0]); p++)
            {
                if (parts[p].wnd == NULL)
                {
                    continue;
                }

                const RECT* at = parts[p].at;
                if (dwp != NULL)
                {
                    dwp = DeferWindowPos(dwp, parts[p].wnd, NULL, at->left, at->top - gApp.scroll,
                                         at->right - at->left, at->bottom - at->top,
                                         SWP_NOZORDER | SWP_NOACTIVATE);
                }
                else
                {
                    // The batch could not be started, which is out of memory
                    // and not worth a second code path beyond this one.
                    SetWindowPos(parts[p].wnd, NULL, at->left, at->top - gApp.scroll,
                                 at->right - at->left, at->bottom - at->top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }

            y = r.bottom;
        }

        if (dwp != NULL)
        {
            EndDeferWindowPos(dwp);
        }

        // The gaps between rows belong to the pane, and moving a child does
        // not repaint what it moved away from.
        InvalidateRect(gApp.pane, NULL, TRUE);
    }

    // Create the current section's windows. Called on a section change and
    // nowhere else -- everything after that is layoutRows.
    void buildRows()
    {
        destroyRows();

        int width = paneWidth();

        for (S32 i = 0; i < kConfigSettingCount; i++)
        {
            const iConfigSetting* s = &kConfigSettings[i];
            if (strcmp(s->section, gApp.sectionNames[gApp.section]) != 0)
            {
                continue;
            }

            if (gApp.rowCount >= (S32)(sizeof(gApp.rows) / sizeof(gApp.rows[0])))
            {
                break;
            }

            Row* row = &gApp.rows[gApp.rowCount];
            memset(row, 0, sizeof(*row));
            row->setting = i;

            int id = kIdRowBase + gApp.rowCount * kIdsPerRow;

            // Created off-screen at a nominal size. layoutRows below is what
            // puts them where they belong, and it is the only code that knows
            // the geometry.
            // SS_NOPREFIX on both statics. Without it a '&' is a mnemonic
            // marker: the character after it is underlined and the '&' itself
            // does not draw, so a path like D:\Rock & Roll loses it -- and the
            // measurement above, which passes DT_NOPREFIX, would not agree.
            row->label = make("STATIC", s->name, SS_LEFT | SS_NOPREFIX, 0, 0, px(150), px(18), 0);

            if (s->kind == ICONFIG_BOOL)
            {
                row->control = make("BUTTON", "on", BS_AUTOCHECKBOX | BS_NOTIFY | WS_TABSTOP, 0, 0,
                                    px(60), px(18), id + kIdRowValue);

                bool on = (iHostStrCaseCmp(gApp.values[i].text, "on") == 0 ||
                           iHostStrCaseCmp(gApp.values[i].text, "true") == 0 ||
                           iHostStrCaseCmp(gApp.values[i].text, "yes") == 0 ||
                           iHostStrCaseCmp(gApp.values[i].text, "1") == 0);
                SendMessage(row->control, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
            }
            else if (wantsCombo(s))
            {
                // An enum is a list and nothing else. Every other kind with
                // choices takes a value BESIDES them -- `framerate` is a
                // number or the word "display" -- so its box stays typable and
                // the list is a shortcut to the words.
                DWORD style = (s->kind == ICONFIG_ENUM) ? CBS_DROPDOWNLIST : CBS_DROPDOWN;
                row->control = make("COMBOBOX", "", style | WS_TABSTOP | WS_VSCROLL, 0, 0, px(200),
                                    px(220), id + kIdRowValue);

                char word[64];
                for (S32 c = 0; choiceAt(s->choices, c, word, sizeof(word)); c++)
                {
                    SendMessageA(row->control, CB_ADDSTRING, 0, (LPARAM)word);
                }

                S32 at = (S32)SendMessageA(row->control, CB_FINDSTRINGEXACT, (WPARAM)-1,
                                           (LPARAM)gApp.values[i].text);
                if (at != CB_ERR)
                {
                    SendMessage(row->control, CB_SETCURSEL, (WPARAM)at, 0);
                }
                else
                {
                    SetWindowTextA(row->control, gApp.values[i].text);
                }

                SetWindowSubclass(row->control, comboWheelProc, 1, 0);
            }
            else
            {
                row->control = make("EDIT", gApp.values[i].text,
                                    ES_LEFT | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 0, 0,
                                    px(200), px(23), id + kIdRowValue);
            }

            if (wantsBrowse(s))
            {
                row->browse = make("BUTTON", "Browse...", BS_PUSHBUTTON | WS_TABSTOP, 0, 0, px(78),
                                   px(23), id + kIdRowBrowse);
            }

            row->desc = make("STATIC", "", SS_LEFT | SS_NOPREFIX, 0, 0, px(200), px(18), 0);

            gApp.rowCount++;
        }

        // Nothing has been measured at this width yet, whatever the last
        // section was measured at.
        gApp.measuredWidth = -1;
        layoutRows(true);
    }

    void scrollTo(int pos)
    {
        int most = gApp.contentHeight - panePage();
        if (most < 0)
        {
            most = 0;
        }
        if (pos < 0)
        {
            pos = 0;
        }
        if (pos > most)
        {
            pos = most;
        }

        if (pos == gApp.scroll)
        {
            return;
        }

        gApp.scroll = pos;
        layoutRows(false);

        SCROLLINFO si;
        memset(&si, 0, sizeof(si));
        si.cbSize = sizeof(si);
        si.fMask = SIF_POS;
        si.nPos = gApp.scroll;
        SetScrollInfo(gApp.pane, SB_VERT, &si, TRUE);
    }

    // Scroll `rect`, in content coordinates, into view -- by the least that
    // will do it. For tabbing: a control the pane has scrolled past still
    // takes focus, and typing into something invisible is the other way this
    // reads as broken.
    void scrollIntoView(const RECT* rect)
    {
        int page = panePage();
        int top = rect->top - px(14);
        int bottom = rect->bottom + px(14);

        if (top < gApp.scroll)
        {
            scrollTo(top);
        }
        else if (bottom > gApp.scroll + page)
        {
            scrollTo(bottom - page);
        }
    }

    void scrollRowIntoView(S32 index)
    {
        int width = paneWidth();
        int y = px(14);

        for (S32 i = 0; i < gApp.rowCount; i++)
        {
            RowRects r = rowRects(&gApp.rows[i], width, y);
            if (i == index)
            {
                // The control and its label, not the description under it: a
                // row whose description is six lines long would otherwise
                // scroll its own control off the top to fit.
                RECT want = r.label;
                want.bottom = r.control.top + px(23);
                scrollIntoView(&want);
                return;
            }
            y = r.bottom;
        }
    }

    // -------------------------------------------------------------------
    // Reading the controls back

    void harvestRow(const Row* row)
    {
        const iConfigSetting* s = &kConfigSettings[row->setting];
        Value* v = &gApp.values[row->setting];

        char before[kMaxValue];
        snprintf(before, sizeof(before), "%s", v->text);

        if (s->kind == ICONFIG_BOOL)
        {
            bool on = SendMessage(row->control, BM_GETCHECK, 0, 0) == BST_CHECKED;
            snprintf(v->text, kMaxValue, "%s", on ? "on" : "off");
        }
        else
        {
            GetWindowTextA(row->control, v->text, kMaxValue);
        }

        // Only a change makes it a value the file has to carry. A setting the
        // file never mentioned and that nobody touched stays out of it.
        if (strcmp(before, v->text) != 0)
        {
            v->present = true;
        }
    }

    void harvestVisible()
    {
        for (S32 i = 0; i < gApp.rowCount; i++)
        {
            harvestRow(&gApp.rows[i]);
        }
    }

    // -------------------------------------------------------------------
    // Browse

    void browseFolder(Row* row)
    {
        char current[kMaxPath];
        GetWindowTextA(row->control, current, sizeof(current));

        BROWSEINFOA bi;
        memset(&bi, 0, sizeof(bi));
        bi.hwndOwner = gApp.main;
        bi.lpszTitle = kConfigSettings[row->setting].name;
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;

        LPITEMIDLIST picked = SHBrowseForFolderA(&bi);
        if (picked == NULL)
        {
            return;
        }

        char path[MAX_PATH];
        if (SHGetPathFromIDListA(picked, path))
        {
            SetWindowTextA(row->control, path);
            harvestRow(row);
            layoutRows(true);
        }

        CoTaskMemFree(picked);
    }

    void browseFont(Row* row)
    {
        char path[kMaxPath];
        GetWindowTextA(row->control, path, sizeof(path));

        // "auto" and "off" are values here, not paths, and handing one to the
        // dialog as a filename gets it rejected rather than ignored.
        if (strchr(path, '\\') == NULL && strchr(path, '/') == NULL)
        {
            path[0] = '\0';
        }

        OPENFILENAMEA ofn;
        memset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = gApp.main;
        ofn.lpstrFilter = "TrueType fonts\0*.ttf;*.otf;*.ttc\0All files\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = sizeof(path);
        ofn.lpstrTitle = "Font";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

        if (GetOpenFileNameA(&ofn))
        {
            SetWindowTextA(row->control, path);
            harvestRow(row);
            layoutRows(true);
        }
    }

    // -------------------------------------------------------------------
    // Save

    // The first setting whose value the table will not accept, or -1.
    S32 firstBadValue(char* why, size_t whySize, S32* sectionOut)
    {
        for (S32 i = 0; i < kConfigSettingCount; i++)
        {
            if (!iConfigTableValidate(&kConfigSettings[i], gApp.values[i].text, why, whySize))
            {
                for (S32 j = 0; j < gApp.sectionCount; j++)
                {
                    if (strcmp(gApp.sectionNames[j], kConfigSettings[i].section) == 0)
                    {
                        *sectionOut = j;
                        break;
                    }
                }
                return i;
            }
        }
        return -1;
    }

    void showSection(S32 which)
    {
        harvestVisible();
        gApp.section = which;
        gApp.scroll = 0;
        SendMessage(gApp.sections, LB_SETCURSEL, (WPARAM)which, 0);
        buildRows();
    }

    bool save()
    {
        harvestVisible();

        char why[256];
        why[0] = '\0';
        S32 section = 0;
        S32 bad = firstBadValue(why, sizeof(why), &section);
        if (bad >= 0)
        {
            char text[512];
            snprintf(text, sizeof(text), "%s.%s is \"%s\".\n\n%s", kConfigSettings[bad].section,
                     kConfigSettings[bad].name, gApp.values[bad].text, why);
            MessageBoxA(gApp.main, text, "That value will not do", MB_OK | MB_ICONWARNING);
            showSection(section);
            return false;
        }

        for (S32 i = 0; i < kConfigSettingCount; i++)
        {
            const iConfigSetting* s = &kConfigSettings[i];
            if (!gApp.values[i].present)
            {
                continue;
            }
            if (!iConfigEditSet(gApp.file, s->section, s->name, gApp.values[i].text))
            {
                MessageBoxA(gApp.main, "There is no room left in config.ini for another line.",
                            "Could not save", MB_OK | MB_ICONERROR);
                return false;
            }
        }

        if (!iConfigEditSave(gApp.file, gApp.path))
        {
            char text[kMaxPath + 128];
            snprintf(text, sizeof(text), "%s could not be written to.", gApp.path);
            MessageBoxA(gApp.main, text, "Could not save", MB_OK | MB_ICONERROR);
            return false;
        }

        return true;
    }

    void resetSection()
    {
        for (S32 i = 0; i < kConfigSettingCount; i++)
        {
            if (strcmp(kConfigSettings[i].section, gApp.sectionNames[gApp.section]) != 0)
            {
                continue;
            }
            if (!isDefault(i))
            {
                snprintf(gApp.values[i].text, kMaxValue, "%s", kConfigSettings[i].value);
                gApp.values[i].present = true;
            }
        }
        buildRows();
    }

    // -------------------------------------------------------------------
    // Finding the file

    // The same order the game searches in, so the configurator edits the file
    // the game will read: BFBB_CONFIG, then the working directory, then beside
    // the executable. An argument overrides all three, for editing one config
    // while a different one is in place.
    //
    // Beside the executable is also where a missing one is created, again as
    // the game does it.
    void findPath(const char* fromCommandLine)
    {
        if (fromCommandLine != NULL && fromCommandLine[0] != '\0')
        {
            snprintf(gApp.path, sizeof(gApp.path), "%s", fromCommandLine);
            return;
        }

        const char* named = getenv("BFBB_CONFIG");
        if (named != NULL && named[0] != '\0')
        {
            snprintf(gApp.path, sizeof(gApp.path), "%s", named);
            return;
        }

        if (iHostPathExists("config.ini"))
        {
            snprintf(gApp.path, sizeof(gApp.path), "config.ini");
            return;
        }

        char dir[kMaxPath];
        if (iHostExeDir(dir, sizeof(dir)))
        {
            snprintf(gApp.path, sizeof(gApp.path), "%s/config.ini", dir);
        }
        else
        {
            snprintf(gApp.path, sizeof(gApp.path), "config.ini");
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

    // -------------------------------------------------------------------
    // Windows

    Row* rowOfControl(int id, int* which)
    {
        if (id < kIdRowBase)
        {
            return NULL;
        }

        int index = (id - kIdRowBase) / kIdsPerRow;
        if (index >= gApp.rowCount)
        {
            return NULL;
        }

        *which = (id - kIdRowBase) % kIdsPerRow;
        return &gApp.rows[index];
    }

    LRESULT CALLBACK paneProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_VSCROLL:
        {
            SCROLLINFO si;
            memset(&si, 0, sizeof(si));
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(wnd, SB_VERT, &si);

            int pos = gApp.scroll;
            switch (LOWORD(wp))
            {
            case SB_LINEUP: pos -= px(24); break;
            case SB_LINEDOWN: pos += px(24); break;
            case SB_PAGEUP: pos -= (int)si.nPage; break;
            case SB_PAGEDOWN: pos += (int)si.nPage; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: pos = si.nTrackPos; break;
            default: break;
            }

            scrollTo(pos);
            return 0;
        }

        case WM_MOUSEWHEEL:
        {
            // Three lines a notch, as the system is set, so this scrolls like
            // everything else on the machine does. WHEEL_PAGESCROLL is what
            // "one screen at a time" comes back as.
            UINT lines = 3;
            SystemParametersInfoA(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);

            int step = (lines == WHEEL_PAGESCROLL) ? panePage() : (int)lines * px(19);
            scrollTo(gApp.scroll - GET_WHEEL_DELTA_WPARAM(wp) * step / WHEEL_DELTA);
            return 0;
        }

        case WM_COMMAND:
        {
            int which = 0;
            Row* row = rowOfControl(LOWORD(wp), &which);
            if (row == NULL)
            {
                break;
            }

            if (which == kIdRowBrowse && HIWORD(wp) == BN_CLICKED)
            {
                if (kConfigSettings[row->setting].kind == ICONFIG_FONT)
                {
                    browseFont(row);
                }
                else
                {
                    browseFolder(row);
                }
                return 0;
            }

            // Tabbing reaches a control the pane has scrolled past, and
            // typing into something off the top of the window is the other way
            // this reads as broken.
            if (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == CBN_SETFOCUS ||
                HIWORD(wp) == BN_SETFOCUS)
            {
                scrollRowIntoView((S32)(row - gApp.rows));
                return 0;
            }

            // The "(default: x)" line under a row follows what is in the
            // control, so anything that finishes a change re-reads it. Not on
            // every keystroke: EN_CHANGE while typing a path would re-measure
            // and shuffle the rows under the caret.
            if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == CBN_SELCHANGE ||
                HIWORD(wp) == EN_KILLFOCUS || HIWORD(wp) == CBN_KILLFOCUS)
            {
                harvestRow(row);
                layoutRows(true);
                return 0;
            }
            break;
        }

        // OPAQUE, and that is the whole point of this handler.
        //
        // The pane is WS_CLIPCHILDREN, so its own erase skips every rectangle a
        // child occupies. A child drawing with a transparent background
        // therefore erases nothing either, and the text under a description
        // that changed -- or a row that moved -- stays on screen with the new
        // text drawn over it. It reads as text that is cut off, because half
        // the letters on the line belong to the string before it.
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            SetBkColor((HDC)wp, GetSysColor(COLOR_WINDOW));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);

        default: break;
        }

        return DefWindowProc(wnd, msg, wp, lp);
    }

    void layoutMain()
    {
        RECT client;
        GetClientRect(gApp.main, &client);

        const int margin = px(10);
        const int listW = px(120);
        const int barH = px(44);

        int height = client.bottom - client.top;
        int width = client.right - client.left;

        MoveWindow(gApp.sections, margin, margin, listW, height - barH - margin * 2, TRUE);
        MoveWindow(gApp.pane, margin + listW + px(10), margin,
                   width - listW - margin * 2 - px(10), height - barH - margin * 2, TRUE);
        MoveWindow(gApp.status, margin, height - barH + px(12), width - px(300), px(20), TRUE);

        int buttonY = height - barH + px(6);
        int buttonW = px(84);
        int buttonH = px(26);
        int x = width - margin - buttonW;

        SetWindowPos(GetDlgItem(gApp.main, kIdSave), NULL, x, buttonY, buttonW, buttonH,
                     SWP_NOZORDER);
        x -= buttonW + px(8);
        SetWindowPos(GetDlgItem(gApp.main, kIdCancel), NULL, x, buttonY, buttonW, buttonH,
                     SWP_NOZORDER);
        x -= px(150) + px(8);
        SetWindowPos(GetDlgItem(gApp.main, kIdResetSection), NULL, x, buttonY, px(150), buttonH,
                     SWP_NOZORDER);
    }

    LRESULT CALLBACK mainProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_SIZE:
            if (gApp.pane != NULL)
            {
                layoutMain();
                layoutRows(true);
            }
            return 0;

        case WM_GETMINMAXINFO:
        {
            MINMAXINFO* mmi = (MINMAXINFO*)lp;
            mmi->ptMinTrackSize.x = px(560);
            mmi->ptMinTrackSize.y = px(400);
            return 0;
        }

        // The wheel goes to whatever has focus, which after clicking a section
        // or a button is not the pane. Send it there when the pointer is over
        // it, which is where the user is looking.
        case WM_MOUSEWHEEL:
        {
            POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
            RECT r;
            GetWindowRect(gApp.pane, &r);
            if (PtInRect(&r, pt))
            {
                SendMessage(gApp.pane, WM_MOUSEWHEEL, wp, lp);
                return 0;
            }
            break;
        }

        case WM_COMMAND:
            switch (LOWORD(wp))
            {
            case kIdSectionList:
                if (HIWORD(wp) == LBN_SELCHANGE)
                {
                    showSection((S32)SendMessage(gApp.sections, LB_GETCURSEL, 0, 0));
                }
                return 0;

            case kIdSave:
                if (save())
                {
                    DestroyWindow(wnd);
                }
                return 0;

            case kIdCancel: DestroyWindow(wnd); return 0;

            case kIdResetSection: resetSection(); return 0;

            default: break;
            }
            break;

        case WM_DESTROY: PostQuitMessage(0); return 0;

        default: break;
        }

        return DefWindowProc(wnd, msg, wp, lp);
    }

    HFONT shellFont()
    {
        NONCLIENTMETRICSA metrics;
        memset(&metrics, 0, sizeof(metrics));
        metrics.cbSize = sizeof(metrics);

        if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
        {
            metrics.lfMessageFont.lfHeight =
                -MulDiv(-metrics.lfMessageFont.lfHeight, gApp.dpi, 96);
            return CreateFontIndirectA(&metrics.lfMessageFont);
        }

        return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
} // namespace

int APIENTRY WinMain(HINSTANCE instance, HINSTANCE, LPSTR commandLine, int show)
{
    INITCOMMONCONTROLSEX controls;
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    // For SHBrowseForFolder's new-style dialog. Apartment threaded because
    // that is what the shell dialogs want.
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    gApp.dpi = 96;

    findPath(commandLine);

    gApp.file = iConfigEditOpen(gApp.path);
    if (gApp.file == NULL)
    {
        // Not there, or not readable. Writing one is the same answer the game
        // gives, and leaves the two agreeing about where the file lives.
        if (!iHostPathExists(gApp.path) && createFile(gApp.path))
        {
            gApp.file = iConfigEditOpen(gApp.path);
        }
    }

    if (gApp.file == NULL)
    {
        char text[kMaxPath + 200];
        snprintf(text, sizeof(text),
                 "%s could not be read, and one could not be written there either.\n\n"
                 "Run the game once to have it write a config.ini, or start this with the "
                 "path to one.",
                 gApp.path);
        MessageBoxA(NULL, text, "bfbb settings", MB_OK | MB_ICONERROR);
        return 1;
    }

    collectSections();
    loadValues();

    WNDCLASSA cls;
    memset(&cls, 0, sizeof(cls));
    cls.lpfnWndProc = mainProc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursor(NULL, IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    cls.lpszClassName = "bfbb_config";
    cls.hIcon = LoadIconA(instance, MAKEINTRESOURCEA(1));
    RegisterClassA(&cls);

    WNDCLASSA pane;
    memset(&pane, 0, sizeof(pane));
    pane.lpfnWndProc = paneProc;
    pane.hInstance = instance;
    pane.hCursor = LoadCursor(NULL, IDC_ARROW);
    pane.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    pane.lpszClassName = "bfbb_config_pane";
    RegisterClassA(&pane);

    gApp.main = CreateWindowExA(0, "bfbb_config", "Battle for Bikini Bottom - Settings",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 780, 620, NULL,
                                NULL, instance, NULL);
    if (gApp.main == NULL)
    {
        return 1;
    }

    // After the window exists, so it is this monitor's rather than the primary
    // one's. Everything measured before now is at 96 and is measured again by
    // the layout below.
    HDC dc = GetDC(gApp.main);
    gApp.dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(gApp.main, dc);

    gApp.font = shellFont();

    gApp.sections = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", NULL,
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY, 0, 0, 10, 10,
                                    gApp.main, (HMENU)(INT_PTR)kIdSectionList, instance, NULL);
    SendMessage(gApp.sections, WM_SETFONT, (WPARAM)gApp.font, TRUE);

    for (S32 i = 0; i < gApp.sectionCount; i++)
    {
        SendMessageA(gApp.sections, LB_ADDSTRING, 0, (LPARAM)gApp.sectionNames[i]);
    }
    SendMessage(gApp.sections, LB_SETCURSEL, 0, 0);

    gApp.pane = CreateWindowExA(WS_EX_CLIENTEDGE, "bfbb_config_pane", NULL,
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0, 10, 10,
                                gApp.main, NULL, instance, NULL);

    char status[kMaxPath + 32];
    snprintf(status, sizeof(status), "%s", gApp.path);
    gApp.status = CreateWindowExA(0, "STATIC", status, WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS, 0,
                                  0, 10, 10, gApp.main, NULL, instance, NULL);
    SendMessage(gApp.status, WM_SETFONT, (WPARAM)gApp.font, TRUE);

    struct
    {
        const char* text;
        int id;
        DWORD style;
    } buttons[] = {
        { "Reset this section", kIdResetSection, BS_PUSHBUTTON },
        { "Cancel", kIdCancel, BS_PUSHBUTTON },
        { "Save", kIdSave, BS_DEFPUSHBUTTON },
    };

    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
    {
        HWND button = CreateWindowExA(0, "BUTTON", buttons[i].text,
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | buttons[i].style, 0, 0,
                                      10, 10, gApp.main, (HMENU)(INT_PTR)buttons[i].id, instance,
                                      NULL);
        SendMessage(button, WM_SETFONT, (WPARAM)gApp.font, TRUE);
    }

    layoutMain();
    buildRows();

    ShowWindow(gApp.main, show);
    UpdateWindow(gApp.main);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        if (!IsDialogMessage(gApp.main, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    iConfigEditClose(gApp.file);
    CoUninitialize();
    return 0;
}
