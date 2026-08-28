// Rewriting the console out of the game's own text. The argument for doing it
// here rather than to the archives is in iTextPatch.h.

#include "iTextPatch.h"

#include <stdio.h>
#include <string.h>

namespace
{
    S32 sEnabled;

    // The registered-trademark sign, as the game's font encodes it. It has to
    // be spliced in as its own literal rather than written "\xae2": a hex
    // escape in C runs as far as there are hex digits, so "\xae2" is one
    // character 0xAE2, not two.
    #define REG "\xae"

    // A word swap, applied to every TEXT asset outside its {markup}.
    //
    // NO REPLACEMENT MAY BE LONGER THAN WHAT IT REPLACES. The substitution runs
    // in place over the asset's own bytes and relies on the write pointer never
    // overtaking the read pointer; a longer replacement would corrupt the
    // string it is halfway through, and then the asset after it. pc_selftest
    // asserts the invariant over this whole table, so a rule that breaks it
    // fails the build's tests rather than the player's save screen.
    //
    // Order matters: the first match at a position wins, so the specific
    // phrasings come before the bare names they contain.
    struct Rule
    {
        const char* from;
        const char* to;
    };

    const Rule kRules[] = {
        // "please do not turn off your Xbox console" -- the autosave warnings,
        // which are the strings a player actually meets.
        { "your Xbox console", "your computer" },
        { "Xbox console", "computer" },

        // The Xbox saved to its hard disk, and could drop back to its own
        // dashboard from the pause menu. Neither has a counterpart here.
        { "Xbox Hard Disk", "hard drive" },
        { "Xbox Dashboard", "Desktop" },

        { "Xbox", "PC" },

        { "Nintendo GameCube", "PC" },
        { "GameCube", "PC" },
        { "Game Cube", "PC" },

        // PS2 text the Xbox release never finished stripping. It survives in
        // the shared menu assets, where the memory-card flow came from.
        { "PlayStation" REG "2", "PC" },
        { "PlayStation 2", "PC" },
        { "PlayStation", "PC" },

        { "DUALSHOCK" REG "2 analog controller", "controller" },
        { "analog controller (DUALSHOCK" REG "2)", "controller" },
        { "DUALSHOCK" REG "2", "controller" },
        { "DUALSHOCK 2", "controller" },

        // "gamepad" and not "controller" for the bare name, which is one
        // character shorter than it: the no-growth rule above is not a style
        // preference, and this is the one place a word swap runs up against it.
        { "DUALSHOCK", "gamepad" },
    };

    const size_t kRuleCount = sizeof(kRules) / sizeof(kRules[0]);

    #undef REG

    // Whole strings, replaced by name.
    //
    // A word swap is enough where the sentence around the console's name still
    // means something without it. These are the ones where it does not: they
    // name a memory card, a card slot, formatting, or a hard disk -- hardware
    // and operations this build does not have and cannot offer, so no
    // substitution can make them true. Saves here are a directory, and that is
    // what they are rewritten to say.
    //
    // "blocks", as a unit of free space, is deliberately NOT in here. It is
    // imprecise rather than wrong, and rewriting the three messages that use it
    // would mean inventing prose for paths this port cannot reach: isavegame.cpp
    // answers every "is there room" with yes.
    //
    // The name is the asset's own, and iTextPatchAssetID hashes it to the id the
    // packer passes in. Names longer than 31 characters are truncated in the
    // archive's debug chunk but hashed in full, so a few here are longer than
    // anything an asset listing will show.
    struct Override
    {
        const char* name;
        const char* text;
    };

    const Override kOverrides[] = {
        // The four assets the rest include by name. Fixing these settles every
        // message that says {i:PS2_MEMCARD} without an entry of its own.
        { "PS2_MEMCARD", "save folder" }, // "memory card (8MB) (for {i:PS2_NAME})"
        { "PS2_NAME", "PC" },
        { "PS2_PAD", "controller" },
        { "PS2_PAD_PAL", "controller" },

        // The save location, under "Load saved game" on the load screen. This
        // is {var:MCName} in zVar.cpp, and it is the one the player sees every
        // time they load. Retail says "MEMORY CARD slot 1" in the shared menu
        // archive and "Xbox Hard Disk" in the level archives.
        { "LD MC1 TXT", "save folder" },
        { "LD MC2 TXT", "save folder" },

        // The Xbox's own save browser.
        { "LD ACCESS HARD DISK? TXT", "Press {i:button_picture_01} to access saved games" },
        { "LD ACCESSING HARD DISK TXT", "Accessing saved games..." },
        { "LD NOGAMES TXT", "No saved games were found." },

        // Loading, and what goes wrong while loading.
        { "LD ERR NOCARD TXT", "No save folder was found." },
        { "LD ERR NOGAME TXT", "No Spongebob games were found." },
        { "LD MC MISSING TXT", "The save folder could not be opened." },
        { "LD DAMAGED SAVE GAME TXT",
          "{c=ffff0000}Load failed!{~:c}{n}The data could not be loaded correctly!{n}Check your "
          "save folder and please try again." },
        { "LD MC DONTREMOVE PAL TXT",
          "WARNING: {var:MCAccessType}...{n}Do not close the game while it is saving." },

        // Formatting a card, which here is creating a directory.
        { "LD BADFORMAT TXT",
          "{c=ffff0000}Save failed!{~:c}{n}The save folder could not be written to.{n}Press "
          "{i:ui_accept} to continue." },
        { "LD FORMAT PROMPT TXT", "Your save folder is not ready.{n}{n}Do you wish to create it?" },
        { "LD FORMATCONFIRM TXT",
          "Are you sure you wish to create your save folder?  Press {i:ui_accept} to confirm." },
        { "SV FORMAT CONFIRM TXT", "Are you sure you wish to create your save folder?" },

        // Saving.
        { "SV MC DONTREMOVE PAL TXT",
          "WARNING: {var:MCAccessType}...{n}Do not close the game while it is saving." },
        { "MNU3 CANT CREATE A NEW GAME TXT", "The save folder could not be created." },
        { "MNU3 CREATE A NEW GAME PAL TXT",
          "Would you like to create a game file to save your progress?{n}{n}(The game uses an "
          "autosave feature. While playing, do not close the game.)" },

        // Autosave. The three failures differ only in their first sentence; the
        // rest of each is retail's, unchanged, because the way back to the
        // Options menu is the same on any platform.
        { "MNU4 AUTO SAVE PAL TXT",
          "{red=.3}{blue=0}{green=0}Autosaving Data...{~:c}{n}{n}Do not close the game while it is "
          "saving." },
        { "MNU4 AUTO SAVE CHANGED TXT",
          "{i:keyword}Data could not be autosaved correctly!{~:c}{n}Your save folder has "
          "{i:keyword}changed{~:c}. The {i:autosave} feature has been {i:keyword}disabled{~:c}. "
          "{i:blacktext}To re-enable {i:autosave}, go to {red=0}{blue=0}{green=0.2}Options{~:c} in "
          "the {red=0}{blue=0}{green=0.2}Pause Menu{~:c} and save to a save game file.{n}Press "
          "{i:ui_accept} to continue without saving." },
        { "MNU4 AUTO SAVE FAILED UNFORMATTED TXT",
          "{i:keyword}Data could not be autosaved correctly!{~:c}{n}Your save folder is not "
          "{i:keyword}ready{~:c}. The {i:autosave} feature has been {i:keyword}disabled{~:c}. "
          "{i:blacktext}To re-enable {i:autosave}, go to {red=0}{blue=0}{green=0.2}Options{~:c} in "
          "the {red=0}{blue=0}{green=0.2}Pause Menu{~:c} and save to a save game file.{n}Press "
          "{i:ui_accept} to continue without saving." },
        { "MNU4 AUTO SAVE FAILED NOSPACE TXT",
          "{i:keyword}Data could not be autosaved correctly!{~:c}{n}There is not enough free space "
          "to save. The {i:autosave} feature has been {i:keyword}disabled{~:c}. {i:blacktext}To "
          "re-enable {i:autosave}, go to {red=0}{blue=0}{green=0.2}Options{~:c} in the "
          "{red=0}{blue=0}{green=0.2}Pause Menu{~:c} and save to a save game file.{n}Press "
          "{i:ui_accept} to continue without saving." },

        // The pause menu's last entry, and the controller prompt. Both are live
        // on this build: the first is one button press away at any time, the
        // second appears whenever a pad is unplugged.
        { "text_menu_reboot", "{i:button_picture_03} Quit to Desktop" },
        { "text_no_controller",
          "No controller is detected.{n}Please connect a controller, and press the {i:ui_accept} "
          "button to continue" },
        { "text_no_controller_pal",
          "No controller is detected.{n}Please connect a controller, and press the {i:ui_accept} "
          "button to continue" },

        // Retail drops {i:ps2_save_size} in here, which quotes the KB three PS2
        // saves need. There is nothing to put in its place.
        { "text_mem_card_no_card",
          "No save folder could be opened. Do you want to start the "
          "game?{n}{n}{n}{n}{font=0}{w*1.5}{h*1.5}{i:button_picture_03}No{n}{i:button_picture_01}"
          "Yes" },
    };

    const size_t kOverrideCount = sizeof(kOverrides) / sizeof(kOverrides[0]);

    // Filled on the first patch, not at static initialisation: the order in
    // which these run against the config load is not worth depending on.
    U32 sOverrideID[kOverrideCount];
    S32 sHashed;

    char foldByte(char c)
    {
        if (c >= 'A' && c <= 'Z')
        {
            return (char)(c + ('a' - 'A'));
        }

        return c;
    }

    // Case-insensitive over ASCII, and spelled out rather than reaching for
    // strncasecmp, which is POSIX -- the port builds against Microsoft's
    // library, where it is _strnicmp, and compat/string.h exists to avoid
    // exactly this kind of per-toolchain branch rather than to add to it.
    S32 matchesAt(const char* s, const char* pattern)
    {
        for (; *pattern != '\0'; s++, pattern++)
        {
            if (foldByte(*s) != foldByte(*pattern))
            {
                return FALSE;
            }
        }

        return TRUE;
    }

    // TRUE if `s` reads as sentence case -- a capital followed by something
    // that is not another capital.
    //
    // This is what decides whether a replacement inherits the capital it landed
    // on. Matching folds case, so "Your Xbox console" and "your Xbox console"
    // hit the same rule, and without this the first of them came out as "your
    // computer" in the middle of a save screen. Propagating a capital
    // unconditionally is worse, though: "insert a DUALSHOCK" would become
    // "insert a Gamepad". A shouted name is not a sentence, and the second
    // character is what tells them apart.
    S32 isSentenceCase(const char* s)
    {
        if (s[0] < 'A' || s[0] > 'Z')
        {
            return FALSE;
        }

        return s[1] < 'A' || s[1] > 'Z';
    }

    // The word swaps, in place. Safe because no replacement is longer than what
    // it replaces, so `dst` never catches `src`.
    S32 applyRules(char* text)
    {
        char* src = text;
        char* dst = text;
        S32 changed = FALSE;

        while (*src != '\0')
        {
            size_t i;
            S32 matched = FALSE;

            for (i = 0; i < kRuleCount; i++)
            {
                if (matchesAt(src, kRules[i].from))
                {
                    const char* to = kRules[i].to;

                    if (isSentenceCase(src) && *to >= 'a' && *to <= 'z')
                    {
                        *dst++ = (char)(*to++ - ('a' - 'A'));
                    }

                    while (*to != '\0')
                    {
                        *dst++ = *to++;
                    }

                    src += strlen(kRules[i].from);
                    matched = TRUE;
                    changed = TRUE;
                    break;
                }
            }

            if (matched)
            {
                continue;
            }

            // A markup span is copied whole and never read into. What is inside
            // is an asset name, a texture name or a variable name -- keys, not
            // prose, and a rewritten key looks up nothing. An unclosed brace
            // runs to the end of the string, which is what retail's parser does
            // with one too.
            if (*src == '{')
            {
                while (*src != '\0')
                {
                    char c = *src++;
                    *dst++ = c;

                    if (c == '}')
                    {
                        break;
                    }
                }

                continue;
            }

            *dst++ = *src++;
        }

        *dst = '\0';

        return changed;
    }
}

U32 iTextPatchAssetID(const char* name)
{
    // xStrHash from xString.cpp, reproduced. It is not called: this library
    // sits below the game code in the link, and pc_selftest links neither
    // xString.cpp nor the RenderWare headers it includes. selftest.cpp pins
    // this copy against asset ids read out of the retail archives, so the two
    // cannot drift without a test failing.
    U32 hash = 0;
    U32 i;

    while (i = *name, i != NULL)
    {
        hash = (i - (i & (S32)i >> 1 & 0x20) & 0xff) + hash * 0x83;
        name++;
    }

    return hash;
}

S32 iTextPatchRulesFit()
{
    size_t i;
    for (i = 0; i < kRuleCount; i++)
    {
        if (strlen(kRules[i].to) > strlen(kRules[i].from))
        {
            printf("bfbb: text rule '%s' -> '%s' is longer than what it replaces\n",
                   kRules[i].from, kRules[i].to);
            return FALSE;
        }
    }

    return TRUE;
}

void iTextPatchSetEnabled(S32 on)
{
    sEnabled = on;
}

S32 iTextPatchEnabled()
{
    return sEnabled;
}

S32 iTextPatchAsset(U32 assetID, char* text, U32 capacity)
{
    if (!sEnabled || text == NULL || capacity == 0)
    {
        return FALSE;
    }

    // The terminator has to be inside the asset. An asset whose text runs to
    // the end of its own bytes without one is not a string, and walking it
    // would read into whatever the packer put next.
    if (memchr(text, '\0', capacity) == NULL)
    {
        return FALSE;
    }

    if (!sHashed)
    {
        size_t i;
        for (i = 0; i < kOverrideCount; i++)
        {
            sOverrideID[i] = iTextPatchAssetID(kOverrides[i].name);
        }

        sHashed = TRUE;
    }

    size_t i;
    for (i = 0; i < kOverrideCount; i++)
    {
        if (sOverrideID[i] != assetID)
        {
            continue;
        }

        if (strlen(kOverrides[i].text) + 1 <= capacity)
        {
            strcpy(text, kOverrides[i].text);
            return TRUE;
        }

        // Said out loud rather than truncated. The replacement is written
        // against the sizes in the retail archives, so this means either a
        // table entry that grew past them or an archive this port has not seen;
        // either way the retail string is the safe thing to leave on screen.
        printf("bfbb: text patch for '%s' needs %u bytes, the asset has %u -- left alone\n",
               kOverrides[i].name, (U32)strlen(kOverrides[i].text) + 1, capacity);
        break;
    }

    return applyRules(text);
}
