// The control bindings: the half that reads config.ini and asks the pad in
// hand. The tables and the parser are in iPadBindTable.cpp; the grammar and
// the argument for it are in iPadBind.h.

#include "iPadBind.h"

#include "iConfig.h"
#include "iHost.h"
#include "iPadHost.h"

#include <stdio.h>
#include <string.h>

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

    const char* bound = iPadBindPresetEntry(want, (S32)(button - kPadBindButtons));
    if (bound == NULL)
    {
        return button->pad;
    }

    // A '#' row names a LETTER, and which input carries it is the pad's
    // business, not ours. Falling back on the row's own default covers the two
    // cases where nobody can answer -- no pad yet, which is when the config
    // writer asks, and a pad with no letter of that name on it.
    if (bound[0] == '#')
    {
        const char* input = iPadHostInputForLabel(bound[1]);
        return (input != NULL) ? input : button->pad;
    }

    return bound;
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
