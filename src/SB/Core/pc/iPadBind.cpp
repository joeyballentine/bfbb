// The control bindings. The grammar and the argument for it are in iPadBind.h.

#include "iPadBind.h"

#include "iConfig.h"
#include "iHost.h"
#include "iPadHost.h"
#include "xPad.h"

#include <stdio.h>
#include <string.h>

// The order here is the order a generated config.ini lists them, so it reads
// like a controller: face buttons, shoulders, then the rest.
//
// The LEFT column is the GameCube's name for the game button, because that is
// the console the decomp is of and xPad.h's own names are the PS2's. It is not
// a claim about which host button presses it -- that is the `pad` column, and
// under a preset it is not even that. `does` is what disambiguates a row, and
// it is what a generated file prints beside each line.
//
// The `pad` column is the base every preset starts from, written in SDL's
// names, which are POSITIONS -- `a` is the button under your thumb on every
// controller ever made, whatever letter is printed on it. It is what answers
// before a pad is known, which is when the config writer asks, and the letters
// it happens to name are an Xbox pad's.
//
// A preset overrides it by LETTER. See kPadBindPresets below.
const iPadBindButton kPadBindButtons[] = {
    { "a", XPAD_BUTTON_X, "a", "space", "jump, confirm" },
    { "b", XPAD_BUTTON_TRIANGLE, "b", "lctrl", "cancel; Bubble Spin on gamecube" },
    { "x", XPAD_BUTTON_O, "x", "e", "options; Bubble Bounce on gamecube" },
    { "y", XPAD_BUTTON_SQUARE, "y", "q", "Bubble Bash" },
    { "z", XPAD_BUTTON_Z, "rb", "f", "near camera, HUD" },
    { "l1", XPAD_BUTTON_L1, "lt", "z", "camera left" },
    { "r1", XPAD_BUTTON_R1, "rt", "c", "camera right" },
    { "l2", XPAD_BUTTON_L2, "lb", "x", NULL },
    { "r2", XPAD_BUTTON_R2, "rb", "v", NULL },
    { "start", XPAD_BUTTON_START, "start", "enter", "pause" },
    { "select", XPAD_BUTTON_SELECT, "back", "backspace", NULL },
    { "up", XPAD_BUTTON_UP, "dpup", "up", "d-pad; also walks" },
    { "down", XPAD_BUTTON_DOWN, "dpdown", "down", NULL },
    { "left", XPAD_BUTTON_LEFT, "dpleft", "left", NULL },
    { "right", XPAD_BUTTON_RIGHT, "dpright", "right", NULL },
};

const S32 kPadBindButtonCount = (S32)(sizeof(kPadBindButtons) / sizeof(kPadBindButtons[0]));

// The presets. Entries line up with kPadBindButtons above by position; a NULL
// takes that row's own default.
//
// **The face four are the same on every console, so they are the same here.**
// Each disc's menu prompts name a button_picture_NN, that TEXT asset names a
// pad_button texture, and that texture is a picture of a physical button. Read
// end to end on all three discs, they agree: accept is A, cancel is B, the
// third menu choice is Y and options is X. The code accepts on XPAD_BUTTON_X,
// cancels on TRIANGLE, reads SQUARE for the third and O for options, so every
// console maps A->X, B->TRIANGLE, X->O, Y->SQUARE. That is gc/iPad.cpp exactly.
//
// So a preset does not move the face buttons. What the releases really changed
// is which of those bits each of the player's MOVES reads -- the Xbox spins on
// O and bounces on TRIANGLE, the PS2 spins on SQUARE -- and that lives in
// iPadLayout.h, applied where the player reads the pad. It has to be done there
// rather than here: a menu and a move share a bit, and moving the bit would
// move the menu with it.
//
// A face row is written as the LETTER printed on the button, marked with '#',
// and iPadBindPadDefault turns it into whatever input carries that letter on
// the pad in hand. `#B` is "the button printed B" -- east on an Xbox pad, west
// on a GameCube one -- so cancel is on the button the prompt draws wherever you
// play it. Positions would not do that: the consoles print different letters in
// the same places, and it is the letter the prompt is a picture of.
//
// What is left for a preset is the shoulders, where the GameCube is a button
// short, and which glyph set is drawn -- and that last one is
// input.button_icons, not this.
#define PAD_LABEL_A "#A"
#define PAD_LABEL_B "#B"
#define PAD_LABEL_X "#X"
#define PAD_LABEL_Y "#Y"

#define PAD_FACES PAD_LABEL_A, PAD_LABEL_B, PAD_LABEL_X, PAD_LABEL_Y

const iPadBindPreset kPadBindPresets[] = {
    // FIRST, and so what "auto" settles on when it cannot place the pad. Four
    // shoulders, so nothing chords: the black button was the Xbox's HUD button
    // -- font.HIP draws it for the status prompt -- and R2 has no prompt of its
    // own anywhere in the game, so both land on RB rather than leaving a bit
    // the game reads unreachable.
    { "xbox", "the Xbox original: Spin on X, Bounce on B, four shoulders", { PAD_FACES } },

    // Same buttons as the Xbox, drawn with PlayStation glyphs, except that the
    // PS2 release put Spin on square rather than circle. Its four shoulders sit
    // the same way.
    { "ps2", "the PS2 original: Spin on square, Bounce on circle", { PAD_FACES } },

    // Three shoulders where the game wants four, so Z modifies: L alone is L1,
    // Z+L is L2, and holding Z still sets Z in its own right. SDL gives a
    // GameCube pad no leftshoulder at all, so the base's `l2 = lb` is a button
    // that does not exist there and the chord is the only way to reach L2.
    { "gamecube",
      "the GameCube original: Spin on B, Bounce on X, Z chords the shoulders",
      { PAD_FACES, "rb", "lt+!rb", "rt+!rb", "lt+rb", "rt+rb" } },
};

const S32 kPadBindPresetCount = (S32)(sizeof(kPadBindPresets) / sizeof(kPadBindPresets[0]));

const char* iPadBindActivePreset()
{
    const char* want = iConfigGetString("input.preset", "auto");

    if (iHostStrCaseCmp(want, "auto") == 0)
    {
        const char* kind = iPadHostPadKind();
        want = (kind != NULL) ? kind : "xbox";
    }

    return want;
}

const char* iPadBindPadDefault(const iPadBindButton* button)
{
    if (button == NULL)
    {
        return NULL;
    }

    // Safe to ask iConfig from here. Every accessor calls iConfigLoad, which
    // sets its `loaded` flag before it parses anything precisely so a getter
    // reached during parsing cannot recurse.
    //
    // Asking the backend on every call rather than caching is what lets a
    // controller swapped mid-game rebind: iPadHostSDL.cpp reloads the bindings
    // when port 0 changes hands, and this is what then answers differently.
    const char* want = iPadBindActivePreset();

    S32 row = (S32)(button - kPadBindButtons);
    if (row < 0 || row >= kPadBindButtonCount || row >= IPAD_BIND_MAX_BUTTONS)
    {
        return button->pad;
    }

    for (S32 i = 0; i < kPadBindPresetCount; i++)
    {
        if (iHostStrCaseCmp(kPadBindPresets[i].name, want) != 0)
        {
            continue;
        }

        const char* bound = kPadBindPresets[i].pad[row];
        if (bound == NULL)
        {
            return button->pad;
        }

        // A '#' row names a LETTER, and which input carries it is the pad's
        // business, not ours. Falling back on the row's own default covers the
        // two cases where nobody can answer -- no pad yet, which is when the
        // config writer asks, and a pad with no letter of that name on it.
        if (bound[0] == '#')
        {
            const char* input = iPadHostInputForLabel(bound[1]);
            return (input != NULL) ? input : button->pad;
        }

        return bound;
    }

    return button->pad;
}

S16 iPadBindSoleInput(const iPadBind& bind)
{
    if (bind.alts < 1)
    {
        return -1;
    }

    S16 found = -1;
    for (S32 i = 0; i < bind.length[0]; i++)
    {
        if (bind.negate[0][i])
        {
            continue;
        }
        if (found >= 0)
        {
            return -1;
        }
        found = bind.id[0][i];
    }

    return found;
}

const char* iPadBindTokenName(S16 id, const iPadBindToken* tokens, S32 tokenCount)
{
    for (S32 i = 0; i < tokenCount; i++)
    {
        if (tokens[i].id == id)
        {
            return tokens[i].name;
        }
    }
    return NULL;
}

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
} // namespace

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

void iPadBindLoad(iPadBindDevice device, const iPadBindToken* tokens, S32 tokenCount, iPadBind* out)
{
    const char* section = (device == IPAD_BIND_PAD) ? "pad" : "keyboard";

    for (S32 i = 0; i < kPadBindButtonCount && i < IPAD_BIND_MAX_BUTTONS; i++)
    {
        const iPadBindButton* b = &kPadBindButtons[i];
        const char* fallback = (device == IPAD_BIND_PAD) ? iPadBindPadDefault(b) : b->key;

        char key[96];
        snprintf(key, sizeof(key), "%s.%s", section, b->name);

        iPadBindParse(iConfigGetString(key, fallback), tokens, tokenCount, key, &out[i]);
    }

    // The array is sized by a macro and filled from a table whose length the
    // compiler will not hand over, so this is where the two are compared. A
    // button past the end would silently never be pressable.
    if (kPadBindButtonCount > IPAD_BIND_MAX_BUTTONS)
    {
        printf("bfbb: %d buttons to bind but room for %d; the last %d are unbound\n",
               (int)kPadBindButtonCount, (int)IPAD_BIND_MAX_BUTTONS,
               (int)(kPadBindButtonCount - IPAD_BIND_MAX_BUTTONS));
        fflush(stdout);
    }
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
