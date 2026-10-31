// The mouse in the game's menus. The argument for the shape of it is in
// iMenuMouse.h.

#include "iMenuMouse.h"

#include "iPadHost.h"
#include "iPadLayout.h"
#include "iScreen.h"
#include "iWindow.h"
#include "xEnt.h"
#include "xEvent.h"
#include "xFont.h"
#include "xPad.h"
#include "zGlobals.h"
#include "zScene.h"
#include "zUI.h"
#include "zUIFont.h"

#include <string.h>

namespace
{
    // The pointer in the menus' own units, 640x480 across the UI box.
    F32 sX;
    F32 sY;
    S32 sShowing;
    S32 sMoved;
    S32 sClicked;

    // A screen's row was under the pointer last frame, so the glove shows.
    S32 sRowOver;
    S32 sRowOverNext;

    S32 sOwned;
    S32 sAcceptQueued;

    // The hint under the pointer, drawn brighter.
    const xBase* sHoverHint;

    // Window pixels to the menus' units: through the letterbox every backend
    // puts the render size in, then through the centred 4:3 UI box.
    S32 ToMenu(F32 wx, F32 wy, F32* mx, F32* my)
    {
        S32 ww = 0;
        S32 wh = 0;
        iWindowGetSize(&ww, &wh);
        F32 rw = (F32)iScreenWidth();
        F32 rh = (F32)iScreenHeight();
        if (ww <= 0 || wh <= 0 || rw <= 0.0f || rh <= 0.0f)
        {
            return FALSE;
        }

        F32 s = (F32)ww / rw;
        if (rh * s > (F32)wh)
        {
            s = (F32)wh / rh;
        }
        F32 rx = (wx - 0.5f * ((F32)ww - rw * s)) / s;
        F32 ry = (wy - 0.5f * ((F32)wh - rh * s)) / s;

        *mx = (rx - iScreenUIOriginXF()) / iScreenUIWidthF() * 640.0f;
        *my = (ry - iScreenUIOriginYF()) / iScreenUIHeightF() * 480.0f;
        return TRUE;
    }

    bool IsUI(const xBase* b)
    {
        return b != NULL && (b->baseType == eBaseTypeUI || b->baseType == eBaseTypeUIFont);
    }

    // Where an option's words are drawn, in the menus' units: its text laid out
    // exactly as zUIFont_Render lays it out, lines and justification and all.
    //
    // Not the option's rectangle. That is the box the words may use, and the
    // words sit at the top of it and as wide as they happen to be: the title
    // list's boxes are 60 high and 30 apart, so a box test puts the option
    // below under the lower half of every word.
    void BuildBox(const zUIFont* ent, const char* text, xtextbox& tb)
    {
        const zUIFontAsset& a = *ent->fasset;

        tb = xtextbox::create();
        tb.font.id = a.fontID;
        tb.font.width = NSCREENX(a.cdim[0]);
        tb.font.height = NSCREENY(a.cdim[1]);
        tb.font.space = NSCREENX(a.space[0]) - tb.font.width;
        tb.font.clip.assign(NSCREENX(a.pos.x), NSCREENY(a.pos.y), NSCREENX(a.dim[0]),
                            NSCREENY(a.dim[1]));
        tb.bounds = tb.font.clip;
        tb.bounds.contract(NSCREENX(a.inset[2]), NSCREENY(a.inset[0]), NSCREENX(a.inset[3]),
                           NSCREENY(a.inset[1]));
        tb.flags = (ent->uiFontFlags & 0x4) ? 0x2 : ((ent->uiFontFlags & 0x2) ? 0x1 : 0);
        tb.set_text(text);

        if (a.uiFontFlags & 0xc00)
        {
            S32 size;
            F32 yextent = tb.yextent(NSCREENY(a.max_height), size, false);
            if (yextent > tb.bounds.h)
            {
                if ((a.uiFontFlags & 0xc00) == 0xc00)
                {
                    tb.bounds.y -= 0.5f * (yextent - tb.bounds.h);
                }
                else if (a.uiFontFlags & 0x400)
                {
                    tb.bounds.y -= yextent - tb.bounds.h;
                }
                tb.bounds.h = yextent;
            }
        }
    }

    // Where a laid-out line starts, after its justification.
    F32 LineX(const xtextbox& tb, const xtextbox::jot_line& line)
    {
        F32 x = tb.bounds.x + line.bounds.x;
        if (tb.flags & 0x2)
        {
            x += 0.5f * (tb.bounds.w - line.bounds.w);
        }
        else if (tb.flags & 0x1)
        {
            x += tb.bounds.w - line.bounds.w;
        }
        return x;
    }

    bool TextRect(const zUIFont* ent, F32* x0, F32* y0, F32* x1, F32* y1)
    {
        if (ent->text == NULL || ent->text[0] == '\0')
        {
            return false;
        }

        xtextbox tb;
        BuildBox(ent, ent->text, tb);

        const xtextbox::layout& l = tb.temp_layout(false);
        if (l._lines_size == 0)
        {
            return false;
        }

        // xtextbox::layout::render's placement of each line.
        const F32 top = l._lines[0].bounds.y;
        F32 lx0 = 1e9f;
        F32 ly0 = 1e9f;
        F32 lx1 = -1e9f;
        F32 ly1 = -1e9f;
        for (size_t i = 0; i < l._lines_size; i++)
        {
            const xtextbox::jot_line& line = l._lines[i];
            if (line.bounds.w <= 0.0f)
            {
                continue;
            }

            F32 x = LineX(tb, line);
            F32 y = tb.bounds.y + line.bounds.y - top;

            lx0 = x < lx0 ? x : lx0;
            ly0 = y < ly0 ? y : ly0;
            lx1 = x + line.bounds.w > lx1 ? x + line.bounds.w : lx1;
            ly1 = y + line.bounds.h > ly1 ? y + line.bounds.h : ly1;
        }
        if (lx1 < lx0)
        {
            return false;
        }

        // A little slack round the words, so the gap between two letters or a
        // short word is not a miss.
        const F32 kSlackX = 8.0f;
        const F32 kSlackY = 4.0f;
        *x0 = lx0 * 640.0f - kSlackX;
        *y0 = ly0 * 480.0f - kSlackY;
        *x1 = lx1 * 640.0f + kSlackX;
        *y1 = ly1 * 480.0f + kSlackY;
        return true;
    }

    // Under the pointer, and how far from the middle of the option, for options
    // that still overlap. -1 when not under it.
    F32 Distance(const _zUI* ui)
    {
        const zUIAsset* a = ui->sasset;
        F32 x0 = a->pos.x;
        F32 y0 = a->pos.y;
        F32 x1 = x0 + a->dim[0];
        F32 y1 = y0 + a->dim[1];
        if (ui->baseType == eBaseTypeUIFont)
        {
            TextRect((const zUIFont*)ui, &x0, &y0, &x1, &y1);
        }
        if (sX < x0 || sX >= x1 || sY < y0 || sY >= y1)
        {
            return -1.0f;
        }
        F32 dx = sX - 0.5f * (x0 + x1);
        F32 dy = sY - 0.5f * (y0 + y1);
        return dx * dx + dy * dy;
    }

    bool HasLink(const xBase* b, U32 srcEvent)
    {
        for (S32 i = 0; i < b->linkCount; i++)
        {
            if (b->link[i].srcEvent == srcEvent)
            {
                return true;
            }
        }
        return false;
    }

    bool IsMove(U32 srcEvent)
    {
        return srcEvent == eEventPadPressUp || srcEvent == eEventPadPressDown ||
               srcEvent == eEventPadPressLeft || srcEvent == eEventPadPressRight;
    }

    // Live options: shown, enabled, and in the group that has focus. The
    // managers that sit selected under every menu are a few pixels across and
    // have nothing to point at.
    bool IsLive(const _zUI* ui)
    {
        return xEntIsVisible(ui) && (ui->baseFlags & 1) && (ui->uiFlags & 0x8) &&
               ui->sasset->dim[0] >= 8 && ui->sasset->dim[1] >= 8;
    }

    // Every pad press the selected options' links answer with a move, walked
    // outward from them. kMax is more options than any one menu shows.
    const S32 kMax = 256;

    struct Step
    {
        xBase* node;
        S32 from; // index of the step it was reached from, -1 at a start
        U32 event; // the press, sent to `from`'s node, that selects this one
    };

    Step sSteps[kMax];
    S32 sStepCount;

    S32 FindStep(const xBase* b)
    {
        for (S32 i = 0; i < sStepCount; i++)
        {
            if (sSteps[i].node == b)
            {
                return i;
            }
        }
        return -1;
    }

    // From the options selected now. Not from the managers, which are selected
    // under every page of a menu and would reach the pages not showing.
    void AddStart(_zUI* ui)
    {
        if ((ui->baseFlags & 1) && (ui->uiFlags & 0x8) && (ui->uiFlags & 0x2) &&
            ui->sasset->dim[0] >= 8 && ui->sasset->dim[1] >= 8 && sStepCount < kMax &&
            FindStep(ui) < 0)
        {
            sSteps[sStepCount].node = ui;
            sSteps[sStepCount].from = -1;
            sSteps[sStepCount].event = 0;
            sStepCount++;
        }
    }

    void Walk(zScene* s)
    {
        sStepCount = 0;

        _zUI* ui = (_zUI*)s->baseList[eBaseTypeUI];
        for (U32 i = 0; i < s->baseCount[eBaseTypeUI]; i++)
        {
            AddStart(&ui[i]);
        }
        zUIFont* font = (zUIFont*)s->baseList[eBaseTypeUIFont];
        for (U32 i = 0; i < s->baseCount[eBaseTypeUIFont]; i++)
        {
            AddStart(&font[i]);
        }

        for (S32 at = 0; at < sStepCount; at++)
        {
            xBase* b = sSteps[at].node;
            for (S32 l = 0; l < b->linkCount; l++)
            {
                const xLinkAsset& link = b->link[l];
                if (!IsMove(link.srcEvent) ||
                    (link.dstEvent != eEventUISelect && link.dstEvent != eEventUIFocusOn_Select) ||
                    (link.chkAssetID != 0 && link.chkAssetID != b->id))
                {
                    continue;
                }

                xBase* to = zSceneFindObject(link.dstAssetID);
                if (!IsUI(to) || FindStep(to) >= 0 || sStepCount == kMax)
                {
                    continue;
                }

                sSteps[sStepCount].node = to;
                sSteps[sStepCount].from = at;
                sSteps[sStepCount].event = link.srcEvent;
                sStepCount++;
            }
        }
    }

    // Sends the presses that walk the selection to step `at`, first one first.
    void Follow(S32 at)
    {
        S32 path[kMax];
        S32 n = 0;
        while (at >= 0 && sSteps[at].from >= 0 && n < kMax)
        {
            path[n++] = at;
            at = sSteps[at].from;
        }

        for (S32 i = n - 1; i >= 0; i--)
        {
            xBase* from = sSteps[sSteps[path[i]].from].node;
            zEntEvent(from, from, sSteps[path[i]].event);
        }
    }

    // The button a hint names, for hints like "{i:ui_options} Options" that
    // lead with a button picture: the bit the picture is of, or 0 for text
    // that is anything else. Formatting tags before the picture are skipped.
    U32 HintButton(const zUIFont* ent)
    {
        struct Name
        {
            const char* include;
            U32 mask;
        };

        // The same on every disc. A picture's number is not; iPadLayout.h.
        static const Name kNames[] = {
            { "ui_accept", XPAD_BUTTON_X },
            { "ui_misc", XPAD_BUTTON_SQUARE },
            { "ui_cancel", XPAD_BUTTON_TRIANGLE },
            { "ui_options", XPAD_BUTTON_O },
        };
        static const char kPicture[] = "button_picture_0";
        const size_t kPictureLen = sizeof(kPicture) - 1;

        const char* s = ent->text;
        if (s == NULL)
        {
            return 0;
        }

        while (*s == ' ')
        {
            s++;
        }

        while (*s == '{')
        {
            const char* close = strchr(s, '}');
            if (close == NULL)
            {
                return 0;
            }

            const char* name = NULL;
            if (strncmp(s, "{i:", 3) == 0)
            {
                name = s + 3;
            }
            else if (strncmp(s, "{insert:", 8) == 0)
            {
                name = s + 8;
            }

            if (name != NULL)
            {
                size_t n = (size_t)(close - name);
                if (n == kPictureLen + 1 && strncmp(name, kPicture, kPictureLen) == 0)
                {
                    return iPadLayoutPictureBit(name[kPictureLen] - '0');
                }
                for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++)
                {
                    if (strlen(kNames[i].include) == n && strncmp(name, kNames[i].include, n) == 0)
                    {
                        return kNames[i].mask;
                    }
                }
                return 0;
            }

            s = close + 1;
        }

        return 0;
    }

    // The hint under the pointer and the button it names, or NULL.
    zUIFont* PickHint(zScene* s, U32* mask)
    {
        zUIFont* font = (zUIFont*)s->baseList[eBaseTypeUIFont];
        for (U32 i = 0; i < s->baseCount[eBaseTypeUIFont]; i++)
        {
            zUIFont* ui = &font[i];
            if (!xEntIsVisible(ui) || !(ui->baseFlags & 1))
            {
                continue;
            }

            U32 bit = HintButton(ui);
            F32 x0, y0, x1, y1;
            if (bit != 0 && TextRect(ui, &x0, &y0, &x1, &y1) && sX >= x0 && sX < x1 && sY >= y0 &&
                sY < y1)
            {
                *mask = bit;
                return ui;
            }
        }
        return NULL;
    }

    bool HasMoveLink(const xBase* b)
    {
        for (S32 i = 0; i < b->linkCount; i++)
        {
            if (IsMove(b->link[i].srcEvent))
            {
                return true;
            }
        }
        return false;
    }

    // The option under the pointer that a pad could select, or NULL.
    //
    // One the walk reaches counts even hidden and out of focus: the pause
    // map's level bubbles are each drawn only while selected and focused, and
    // the walk is what says they are this screen's. A shown option under the
    // pointer still wins over a hidden one.
    _zUI* Pick(zScene* s, S32* step)
    {
        _zUI* best = NULL;
        F32 bestDistance = 0.0f;
        bool bestShown = false;
        *step = -1;

        for (S32 type = 0; type < 2; type++)
        {
            U32 count = s->baseCount[type == 0 ? eBaseTypeUI : eBaseTypeUIFont];
            for (U32 i = 0; i < count; i++)
            {
                _zUI* ui = (type == 0) ? &((_zUI*)s->baseList[eBaseTypeUI])[i] :
                                         &((zUIFont*)s->baseList[eBaseTypeUIFont])[i];

                S32 at = FindStep(ui);
                if (at < 0)
                {
                    continue;
                }

                // Reachable, or the selected option itself if it is part of a
                // list or has something to accept. The taxi reads accept off
                // the pad rather than through a link, so a list is enough.
                bool reached = sSteps[at].from >= 0;
                bool shown = IsLive(ui);
                if (reached ? (ui->sasset->dim[0] < 8 || ui->sasset->dim[1] < 8) :
                              (!shown || !(HasMoveLink(ui) || HasLink(ui, eEventPadPressX) ||
                                           HasLink(ui, eEventPadPressStart))))
                {
                    continue;
                }

                F32 d = Distance(ui);
                if (d < 0.0f)
                {
                    continue;
                }
                if (best != NULL && (bestShown && !shown))
                {
                    continue;
                }
                if (best != NULL && bestShown == shown && d >= bestDistance)
                {
                    continue;
                }

                best = ui;
                bestDistance = d;
                bestShown = shown;
                *step = at;
            }
        }

        return best;
    }
} // namespace

U32 iMenuMouseUpdate(S32 inMenu)
{
    sRowOver = sRowOverNext;
    sRowOverNext = 0;
    sMoved = 0;
    sClicked = 0;
    sHoverHint = NULL;

    S32 accept = sAcceptQueued;
    sAcceptQueued = 0;

    // A scene still loading has counts before it has lists.
    zScene* s = globals.sceneCur;
    if (!inMenu || s == NULL ||
        (s->baseCount[eBaseTypeUI] != 0 && s->baseList[eBaseTypeUI] == NULL) ||
        (s->baseCount[eBaseTypeUIFont] != 0 && s->baseList[eBaseTypeUIFont] == NULL))
    {
        sShowing = iPadHostSetPointer(FALSE, FALSE);
        return 0;
    }

    F32 wx = 0.0f;
    F32 wy = 0.0f;
    S32 moved = 0;
    S32 clicked = 0;
    S32 wheel = 0;
    sShowing = iPadHostPointer(&wx, &wy, &moved, &clicked, &wheel) && ToMenu(wx, wy, &sX, &sY);

    _zUI* over = NULL;
    S32 step = -1;
    if (sShowing && !sOwned)
    {
        Walk(s);
        over = Pick(s, &step);
    }

    // A hint like "{i:ui_options} Options" presses its button, on any screen,
    // wherever no option is under the pointer.
    U32 hint = 0;
    const xBase* hovered = NULL;
    if (sShowing && over == NULL && !sRowOver)
    {
        hovered = PickHint(s, &hint);
    }

    sShowing = iPadHostSetPointer(TRUE, over != NULL || sRowOver || hint != 0);
    if (!sShowing)
    {
        return 0;
    }
    sHoverHint = hovered;

    sMoved = moved;
    sClicked = clicked;

    U32 add = accept ? XPAD_BUTTON_X : 0;

    // The wheel steps through a list the way the d-pad does.
    if (wheel > 0)
    {
        add |= XPAD_BUTTON_UP;
    }
    else if (wheel < 0)
    {
        add |= XPAD_BUTTON_DOWN;
    }

    if (over != NULL && (moved || clicked) && sSteps[step].from >= 0)
    {
        Follow(step);
    }

    if (over != NULL && clicked)
    {
        bool startOnly = !HasLink(over, eEventPadPressX) && HasLink(over, eEventPadPressStart);
        add |= startOnly ? XPAD_BUTTON_START : XPAD_BUTTON_X;
    }
    else if (hint != 0 && clicked)
    {
        add |= hint;
    }

    return add;
}

void iMenuMouseBrighten(U8* r, U8* g, U8* b)
{
    if ((S32)*r + *g + *b > 600)
    {
        // White text: SpongeBob's yellow.
        *r = 255;
        *g = 230;
        *b = 90;
        return;
    }

    // The menus' dark teal, 0 70 85, to a vivid one about twice as bright
    // rather than toward white: a faded colour is how these menus draw what
    // is NOT selected.
    S32 rr = *r / 2;
    S32 gg = *g * 2 + 20;
    S32 bb = *b * 2 + 20;
    *r = (U8)rr;
    *g = (U8)(gg > 255 ? 255 : gg);
    *b = (U8)(bb > 255 ? 255 : bb);
}

void iMenuMouseTint(const xBase* ent, U8* r, U8* g, U8* b)
{
    if (ent != NULL && ent == sHoverHint)
    {
        iMenuMouseBrighten(r, g, b);
    }
}

S32 iMenuMouseOver(xBase* ent)
{
    if (!sShowing || !IsUI(ent))
    {
        return FALSE;
    }

    _zUI* ui = (_zUI*)ent;
    if (!xEntIsVisible(ui) || Distance(ui) < 0.0f)
    {
        return FALSE;
    }

    sRowOverNext = 1;
    return TRUE;
}

S32 iMenuMouseMoved()
{
    return sShowing && sMoved;
}

S32 iMenuMouseClicked()
{
    return sShowing && sClicked;
}

S32 iMenuMousePosition(F32* x, F32* y)
{
    if (!sShowing)
    {
        return FALSE;
    }
    *x = sX;
    *y = sY;
    return TRUE;
}

S32 iMenuMouseTextEnd(xBase* ent, const char* prefix, F32* x)
{
    if (ent == NULL || ent->baseType != eBaseTypeUIFont)
    {
        return FALSE;
    }

    const zUIFont* font = (const zUIFont*)ent;
    if (font->text == NULL || font->text[0] == '\0')
    {
        return FALSE;
    }

    // Two layouts through one shared buffer, so the first is read out before
    // the second is made.
    xtextbox tb;
    BuildBox(font, font->text, tb);
    const xtextbox::layout& whole = tb.temp_layout(false);
    if (whole._lines_size == 0)
    {
        return FALSE;
    }
    const F32 start = LineX(tb, whole._lines[0]);

    xtextbox part;
    BuildBox(font, prefix, part);
    const xtextbox::layout& upto = part.temp_layout(false);
    const F32 width = (upto._lines_size != 0) ? upto._lines[0].bounds.w : 0.0f;

    *x = (start + width) * 640.0f;
    return TRUE;
}

void iMenuMouseOwn(S32 on)
{
    sOwned = on ? 1 : 0;
}

void iMenuMouseAccept()
{
    sAcceptQueued = 1;
}
