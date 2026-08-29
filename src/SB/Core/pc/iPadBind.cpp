// The control bindings. The grammar and the argument for it are in iPadBind.h.

#include "iPadBind.h"

#include "iHost.h"
#include "xPad.h"

#include <stdio.h>
#include <string.h>

// The order here is the order a generated config.ini lists them, so it reads
// like a controller: face buttons, shoulders, then the rest.
//
// The pad defaults are gc/iPad.cpp's mapping carried onto an Xbox controller.
// The GameCube has three shoulders where the game wants four, so Z modifies:
// L alone is L1, Z+L is L2, and holding Z sets Z in its own right. RB stands
// in for Z, and the triggers for L and R. LB, LS and RS go unbound -- the
// GameCube has no fourth shoulder and no stick clicks, so nothing needs them,
// and they are the obvious places for someone remapping to put something.
const iPadBindButton kPadBindButtons[] = {
    { "a", XPAD_BUTTON_X, "a", "space", "jump, confirm" },
    { "b", XPAD_BUTTON_TRIANGLE, "b", "lctrl", "Bubble Spin, cancel" },
    { "x", XPAD_BUTTON_O, "x", "e", "Bubble Bounce, pick up, throw" },
    { "y", XPAD_BUTTON_SQUARE, "y", "q", "Bubble Bash" },
    { "z", XPAD_BUTTON_Z, "rb", "f", "near camera" },
    { "l1", XPAD_BUTTON_L1, "lt+!rb", "z", "camera left" },
    { "r1", XPAD_BUTTON_R1, "rt+!rb", "c", "camera right" },
    { "l2", XPAD_BUTTON_L2, "lt+rb", "x", NULL },
    { "r2", XPAD_BUTTON_R2, "rt+rb", "v", NULL },
    { "start", XPAD_BUTTON_START, "start", "enter", "pause" },
    { "select", XPAD_BUTTON_SELECT, "back", "backspace", NULL },
    { "up", XPAD_BUTTON_UP, "dpup", "up", "d-pad; also walks" },
    { "down", XPAD_BUTTON_DOWN, "dpdown", "down", NULL },
    { "left", XPAD_BUTTON_LEFT, "dpleft", "left", NULL },
    { "right", XPAD_BUTTON_RIGHT, "dpright", "right", NULL },
};

const S32 kPadBindButtonCount = (S32)(sizeof(kPadBindButtons) / sizeof(kPadBindButtons[0]));

const iPadBindButton* iPadBindFind(const char* name)
{
    for (S32 i = 0; i < kPadBindButtonCount; i++)
    {
        if (iHostStrCaseCmp(kPadBindButtons[i].name, name) == 0)
        {
            return &kPadBindButtons[i];
        }
    }
    return NULL;
}

namespace
{
    const S32 kMaxToken = 24;

    const char* skipSpace(const char* s)
    {
        while (*s == ' ' || *s == '\t')
        {
            s++;
        }
        return s;
    }

    // Copies up to the next `stop` character or the end, trimmed. Returns the
    // position to carry on from.
    const char* takeToken(const char* s, char stop, char* out, S32 outSize)
    {
        s = skipSpace(s);

        const char* end = s;
        while (*end != '\0' && *end != stop)
        {
            end++;
        }

        const char* trimmed = end;
        while (trimmed > s && (trimmed[-1] == ' ' || trimmed[-1] == '\t'))
        {
            trimmed--;
        }

        S32 n = (S32)(trimmed - s);
        if (n >= outSize)
        {
            n = outSize - 1;
        }
        memcpy(out, s, (size_t)n);
        out[n] = '\0';

        return end;
    }

    S16 lookup(const char* name, const iPadBindToken* tokens, S32 tokenCount)
    {
        for (S32 i = 0; i < tokenCount; i++)
        {
            if (iHostStrCaseCmp(tokens[i].name, name) == 0)
            {
                return tokens[i].id;
            }
        }
        return -1;
    }
}

bool iPadBindParse(const char* text, const iPadBindToken* tokens, S32 tokenCount, const char* what,
                   iPadBind* out)
{
    memset(out, 0, sizeof(*out));

    if (text == NULL)
    {
        return true;
    }

    // Built to the side and only committed at the end, so a line that fails
    // half way through leaves the button unbound rather than partly bound.
    iPadBind built;
    memset(&built, 0, sizeof(built));

    const char* s = skipSpace(text);
    if (*s == '\0')
    {
        // Deliberately empty. Not an error: it is how a button is turned off.
        *out = built;
        return true;
    }

    bool ok = true;

    while (*s != '\0')
    {
        char alternative[kMaxToken * IPAD_BIND_CHORD + 8];
        s = takeToken(s, ',', alternative, (S32)sizeof(alternative));

        if (*s == ',')
        {
            s++;

            // A separator with nothing after it. Caught here rather than left
            // to the loop condition, which would end quietly and accept the
            // line -- and a line ending in a stray ',' is someone half way
            // through an edit, not someone who meant this.
            if (*skipSpace(s) == '\0')
            {
                printf("bfbb: %s: ',' with nothing after it, ignored\n", what);
                ok = false;
                break;
            }
        }

        if (alternative[0] == '\0')
        {
            printf("bfbb: %s: empty alternative, ignored\n", what);
            ok = false;
            continue;
        }

        if (built.alts == IPAD_BIND_ALTS)
        {
            printf("bfbb: %s: more than %d alternatives, the rest are ignored\n", what,
                   (int)IPAD_BIND_ALTS);
            ok = false;
            break;
        }

        S8 length = 0;
        S32 positives = 0;
        bool altOk = true;

        for (const char* c = alternative; *c != '\0';)
        {
            char token[kMaxToken];
            c = takeToken(c, '+', token, (S32)sizeof(token));

            if (*c == '+')
            {
                c++;

                if (*skipSpace(c) == '\0')
                {
                    printf("bfbb: %s: '+' with nothing after it, ignored\n", what);
                    altOk = false;
                    break;
                }
            }

            const char* name = token;
            bool negate = false;
            if (name[0] == '!')
            {
                negate = true;
                name = skipSpace(name + 1);
            }

            if (name[0] == '\0')
            {
                printf("bfbb: %s: '+' with nothing after it, ignored\n", what);
                altOk = false;
                break;
            }

            if (length == IPAD_BIND_CHORD)
            {
                printf("bfbb: %s: more than %d inputs joined by '+', ignored\n", what,
                       (int)IPAD_BIND_CHORD);
                altOk = false;
                break;
            }

            S16 id = lookup(name, tokens, tokenCount);
            if (id < 0)
            {
                printf("bfbb: %s: '%s' is not an input this device has, ignored\n", what, name);
                altOk = false;
                break;
            }

            built.id[built.alts][length] = id;
            built.negate[built.alts][length] = negate ? 1 : 0;
            length++;

            if (!negate)
            {
                positives++;
            }
        }

        if (!altOk)
        {
            ok = false;
            continue;
        }

        // All negations would be satisfied by an idle controller, which is a
        // button that is on until you press something. Always a mistake.
        if (positives == 0)
        {
            printf("bfbb: %s: '%s' is only '!' terms, so nothing would press it; ignored\n", what,
                   alternative);
            ok = false;
            continue;
        }

        built.length[built.alts] = length;
        built.alts++;
    }

    if (!ok)
    {
        return false;
    }

    *out = built;
    return true;
}

bool iPadBindHeld(const iPadBind& bind, bool (*held)(S16 id))
{
    for (S32 a = 0; a < bind.alts; a++)
    {
        bool all = true;
        for (S32 i = 0; i < bind.length[a]; i++)
        {
            if (held(bind.id[a][i]) == (bind.negate[a][i] != 0))
            {
                all = false;
                break;
            }
        }

        if (all)
        {
            return true;
        }
    }

    return false;
}
