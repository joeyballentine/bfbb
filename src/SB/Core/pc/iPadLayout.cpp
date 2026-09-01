// The per-console move layout. The table and the argument for it are in
// iPadLayout.h.

#include "iPadLayout.h"

#include "iHost.h"
#include "iPadBind.h"
#include "xPad.h"

namespace
{
    // One preset's departure from the GameCube's, as the pair of bits it
    // transposes. The GameCube's own row is the identity and is not listed.
    struct Swap
    {
        const char* preset;
        U32 a;
        U32 b;
    };

    const Swap kSwaps[] = {
        // Spin moves to the button printed X and Bounce to the one printed B.
        { "xbox", XPAD_BUTTON_TRIANGLE, XPAD_BUTTON_O },

        // Spin moves to square and Bash to triangle. Bounce does not move.
        { "ps2", XPAD_BUTTON_TRIANGLE, XPAD_BUTTON_SQUARE },
    };

    const S32 kSwapCount = (S32)(sizeof(kSwaps) / sizeof(kSwaps[0]));

    // Where each console PRINTS the four face bits, as the position the glyph
    // files are named after.
    //
    // The same reading as the table above, stopped one step earlier: a disc's
    // cancel prompt draws the button printed B and its options prompt the one
    // printed X, and the code cancels on TRIANGLE and opens options on O. The
    // PlayStation prints shapes rather than letters, so its "B" is triangle and
    // its "X" is circle -- which is where its own prompts point.
    struct FaceSet
    {
        const char* set;
        const char* x;
        const char* triangle;
        const char* o;
        const char* square;
    };

    const FaceSet kFaceSets[] = {
        { "xbox", "south", "east", "west", "north" },
        { "gamecube", "south", "west", "east", "north" },
        { "ps2", "south", "north", "east", "west" },
    };

    const S32 kFaceSetCount = (S32)(sizeof(kFaceSets) / sizeof(kFaceSets[0]));
} // namespace

const char* iPadLayoutFaceGlyph(const char* set, U32 mask)
{
    if (set == NULL)
    {
        return NULL;
    }

    for (S32 i = 0; i < kFaceSetCount; i++)
    {
        if (iHostStrCaseCmp(kFaceSets[i].set, set) != 0)
        {
            continue;
        }

        if (mask == XPAD_BUTTON_X)
            return kFaceSets[i].x;
        if (mask == XPAD_BUTTON_TRIANGLE)
            return kFaceSets[i].triangle;
        if (mask == XPAD_BUTTON_O)
            return kFaceSets[i].o;
        if (mask == XPAD_BUTTON_SQUARE)
            return kFaceSets[i].square;
        break;
    }

    return NULL;
}

U32 iPadLayoutButton(U32 mask)
{
    const char* preset = iPadBindActivePreset();

    for (S32 i = 0; i < kSwapCount; i++)
    {
        if (iHostStrCaseCmp(kSwaps[i].preset, preset) != 0)
        {
            continue;
        }

        if (mask == kSwaps[i].a)
        {
            return kSwaps[i].b;
        }

        if (mask == kSwaps[i].b)
        {
            return kSwaps[i].a;
        }

        break;
    }

    return mask;
}
