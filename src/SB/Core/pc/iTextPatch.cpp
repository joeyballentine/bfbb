// Rewriting the console out of the game's own text. The argument for doing it
// here rather than to the archives is in iTextPatch.h.

#include "iTextPatch.h"

#include "iPadLayout.h"
#include "xPad.h"

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
    // "blocks" is in here too, and it is the one case where the text was not
    // merely dated but actively wrong. A block is 8 KB of a memory card, and the
    // figures the game drops into these sentences are plain byte counts on a
    // host -- "2147483647 block(s)" free. The numbers themselves are fixed where
    // they are produced, by iSGFormatSize, which supplies a unit with each one;
    // these entries are the other half of that, taking the now-duplicated
    // "block(s)" back out of the sentence around it.
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

    // Prompts for a MOVE, which is the one thing the three releases really
    // disagree about.
    //
    // Their pad layers are the same and their menus are the same. What each
    // build changed is which bit zEntPlayer reads for a move: the GameCube
    // spins on TRIANGLE and bounces on O, the Xbox has those two the other way
    // round, the PS2 spins on SQUARE and bashes on TRIANGLE. iPadLayout.h
    // carries the table and the evidence for it.
    //
    // The port ships the Xbox archives, so their move prompts are right only
    // under the xbox preset. Under the others the prompt has to name the
    // picture whose button the move has moved to, or a sign in Bikini Bottom
    // tells you to press a button that does something else.
    //
    // Rewriting two digits rather than the whole string keeps every word around
    // them, so a translated archive keeps its translation.
    enum
    {
        MOVE_SPIN,
        MOVE_BASH,
        MOVE_BOUNCE
    };

    // The bit each move reads in the code this is, before any preset moves it.
    const U32 kMoveBit[] = { XPAD_BUTTON_TRIANGLE, XPAD_BUTTON_SQUARE, XPAD_BUTTON_O };

    struct PictureFix
    {
        const char* name;
        S32 move;
    };

    const PictureFix kPictureFixes[] = {
        { "button_spinattack_text", MOVE_SPIN },
        { "button_bellyattack_text", MOVE_SPIN },
        { "button_chopattack_text", MOVE_SPIN },
        { "button_kickattack_text", MOVE_SPIN },

        { "button_bashattack_text", MOVE_BASH },

        // Bounce, and the two things that share its button: picking up and
        // throwing. Retail gives each its own prompt and they all name the same
        // picture on a given disc.
        { "button_bounceattack_text", MOVE_BOUNCE },
        { "button_bowlattack_text", MOVE_BOUNCE },
        { "button_flopattack_text", MOVE_BOUNCE },
        { "button_lassoattack_text", MOVE_BOUNCE },
        { "button_lassoswing_text", MOVE_BOUNCE },
        { "button_pickup_text", MOVE_BOUNCE },
        { "button_throw_text", MOVE_BOUNCE },
    };

    // The two digits of the button_picture_NN that draws this bit. The mapping
    // is the discs' own and is the same on all three: 1 is accept, 2 the third
    // menu choice, 3 cancel, 4 options. iPadGlyph.cpp reads the same table from
    // the other end.
    const char* pictureForBit(U32 mask)
    {
        switch (mask)
        {
        case XPAD_BUTTON_X:
            return "01";
        case XPAD_BUTTON_SQUARE:
            return "02";
        case XPAD_BUTTON_TRIANGLE:
            return "03";
        case XPAD_BUTTON_O:
            return "04";
        default:
            return NULL;
        }
    }

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
        //
        // Both are real: isavegame.cpp exposes two targets, so the second button
        // on the save and load screens is a second folder with its own three
        // game slots rather than the dead entry it used to be.
        { "LD MC1 TXT", "save folder" },
        { "LD MC2 TXT", "second save folder" },

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

        // Free space, in blocks of a card that is not there. The number in each
        // of these now arrives with its own unit on it, so the "block(s)" that
        // followed it goes.
        { "MNU3 FREE DISK TEXT", "Available Free Space: {var:SpaceAvailableString}" },
        { "MNU4 FREE DISK TEXT", "Available Free Space: {var:SpaceAvailableString}" },
        { "SV NOSPACE TXT",
          "There is not enough free space to create new save games. Press {i:ui_accept} to "
          "continue." },
        { "SV BADSAVE TXT",
          "{c=ffff0000}Save failed!{~:c} There is not enough free space to create new save games. "
          "Press {i:ui_accept} to continue." },
        { "MNU4 AUTO SAVE FAILED TXT",
          "{i:keyword}Warning: Autosave failed! {~:c}{n}There is not enough free space to create a "
          "new save game. The {i:autosave}{i:blacktext} feature has been {i:keyword}disabled{~:c}. "
          "{i:blacktext}To re-enable {i:autosave}{i:blacktext}, go to the "
          "{red=0}{blue=0}{green=0.2}Options{~:c}{i:blacktext} in the "
          "{red=0}{blue=0}{green=0.2}Pause Menu{~:c}{i:blacktext} and save to a game save." },
        { "text_mem_card_no_space",
          "There is not enough free space to save games. You need to free more space to save a new "
          "{i:game_name} game.{n}{n}{n}Press {i:ui_accept} to continue or {i:ui_cancel} to free "
          "more space." },
        { "text_mem_card_no_space_no_save",
          "There is not enough free space to save games. You need "
          "{c=ff00d244}{var:BadCardNeeded}{~:c} more to save a new {i:game_name} "
          "game.{n}{n}{n}Press {i:ui_accept} to continue without saving or {i:ui_cancel} to free "
          "more space." },
        { "text_mem_card_no_space_overwrite",
          "There is not enough free space to save games. You need "
          "{c=ff00d244}{var:BadCardNeeded}{~:c} more to save a new {i:game_name} game.{n}Existing "
          "game saves may be loaded and overwritten.{n}{n}{n}Press {i:ui_accept} to continue or "
          "{i:ui_cancel} to free more space." },

        // Retail drops {i:ps2_save_size} in here, which quotes the KB three PS2
        // saves need. There is nothing to put in its place.
        { "text_mem_card_no_card",
          "No save folder could be opened. Do you want to start the "
          "game?{n}{n}{n}{n}{font=0}{w*1.5}{h*1.5}{i:button_picture_03}No{n}{i:button_picture_01}"
          "Yes" },
    };

    const size_t kOverrideCount = sizeof(kOverrides) / sizeof(kOverrides[0]);
    const size_t kPictureFixCount = sizeof(kPictureFixes) / sizeof(kPictureFixes[0]);

    // Filled on the first patch, not at static initialisation: the order in
    // which these run against the config load is not worth depending on.
    U32 sOverrideID[kOverrideCount];
    U32 sPictureFixID[kPictureFixCount];

    // Rewrites the picture this asset names, in place. Both numbers are two
    // digits, so nothing moves and no length can change.
    S32 applyPictureFix(U32 assetID, char* text)
    {
        static const char kTag[] = "button_picture_";
        const size_t tagLen = sizeof(kTag) - 1;
        S32 changed = FALSE;
        size_t i;

        for (i = 0; i < kPictureFixCount; i++)
        {
            if (sPictureFixID[i] != assetID)
            {
                continue;
            }

            // Patched at load, so a preset changed mid-session does not reach
            // prompts already in memory until the next level load. Everything
            // else about a pad swap is live; this one is not, because the text
            // is only rewritten as it comes off the disc.
            const char* want = pictureForBit(iPadLayoutButton(kMoveBit[kPictureFixes[i].move]));
            if (want == NULL)
            {
                continue;
            }

            char* at = text;
            while ((at = strstr(at, kTag)) != NULL)
            {
                char* digits = at + tagLen;

                // Only the face four. A move prompt names one of those and
                // nothing else, and leaving the rest alone means a shoulder or
                // a stick picture in the same string cannot be walked on.
                if (digits[0] == '0' && digits[1] >= '1' && digits[1] <= '4' &&
                    (digits[0] != want[0] || digits[1] != want[1]))
                {
                    digits[0] = want[0];
                    digits[1] = want[1];
                    changed = TRUE;
                }

                at = digits;
            }
        }

        return changed;
    }
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
} // namespace

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
            printf("bfbb: text rule '%s' -> '%s' is longer than what it replaces\n", kRules[i].from,
                   kRules[i].to);
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

        for (i = 0; i < kPictureFixCount; i++)
        {
            sPictureFixID[i] = iTextPatchAssetID(kPictureFixes[i].name);
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

    S32 fixed = applyPictureFix(assetID, text);

    return applyRules(text) || fixed;
}
