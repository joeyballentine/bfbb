#ifndef ITEXTPATCH_H
#define ITEXTPATCH_H

#include <types.h>

// PC-only: rewriting the console out of the game's own text, as it loads.
//
// The port runs on the Xbox assets, and those assets talk about an Xbox. The
// pause menu offers to "Reboot to Xbox Dashboard"; an autosave asks the player
// not to turn off "your Xbox console"; the load screen names the save location
// as "MEMORY CARD slot 1", which on this build is a directory. None of that is
// code -- it is TEXT assets inside the HIP archives, and none of it is true
// here.
//
// **Why at load rather than in the files.** A patched archive is a second copy
// of the install that has to be rebuilt whenever the rules change, and it makes
// the port's behaviour depend on which copy someone happens to be pointing
// BFBB_ASSETS at. Rewriting on the way through costs one pass over a few
// hundred short strings per level load and leaves the player's own files
// untouched, so the switch below is a real switch: off is exactly retail.
//
// **Where it hooks in.** zAssetTypes.cpp gives the 'TEXT' asset type a
// readXForm, which the packer calls once per asset as a layer is transformed,
// with the bytes it just read. That is the only place every TEXT asset in the
// game passes through, whichever archive it came from and whoever asks for it
// later.
//
// **It rewrites in place.** The transform never lengthens a string: the
// substring rules all replace a console's name with a shorter word, and a
// whole-asset override is refused unless it fits the space the asset already
// occupies. So there is no allocation, no lifetime to manage and no unload
// hook, and an asset that cannot be rewritten is left exactly as it was rather
// than truncated. pc_selftest checks the no-growth invariant over the whole
// rule table, which is what keeps a future rule from quietly breaking it.
//
// **Markup is not text.** The strings are xtextbox markup -- {i:PS2_MEMCARD}
// includes another TEXT asset, {tex:pad_button1;...} names a texture,
// {var:MCName} calls into zVar.cpp. Those names are lookup keys, and rewriting
// one silently breaks the lookup, so the substitution pass steps over every
// {...} span instead of reading into it. The asset an include names is
// rewritten when it is loaded in its own right, which is why replacing
// PS2_MEMCARD alone settles the eleven messages that include it.

// Off leaves every string exactly as the disc shipped it. Pushed from
// iSystem.cpp's ApplyConfig, the way the render features are, so that neither
// this file nor the game code that calls it has to know what config.ini is.
void iTextPatchSetEnabled(S32 on);
S32 iTextPatchEnabled();

// Rewrites one TEXT asset's string, in place. `assetID` is the packer's asset
// id, which is xStrHash of the asset's name. `text` is the string after the
// xTextAsset header, and `capacity` is the bytes it has to live in, terminator
// included -- the asset's size less that header, since the packer rounds every
// asset up and the slack is the asset's own.
//
// Returns TRUE if anything changed. A NULL or unterminated string, a zero
// capacity, or the switch being off are all "nothing changed", not errors.
S32 iTextPatchAsset(U32 assetID, char* text, U32 capacity);

// The asset id for a name -- xStrHash, reproduced. Public for pc_selftest,
// which pins it against ids read out of the retail archives.
U32 iTextPatchAssetID(const char* name);

// TRUE if no substitution rule's replacement is longer than the text it
// replaces, which is what makes the in-place rewrite safe. The check lives
// here, with the table, and pc_selftest asserts it: a rule added later that
// breaks it fails the tests rather than the player's save screen.
S32 iTextPatchRulesFit();

#endif
