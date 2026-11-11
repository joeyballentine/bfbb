#ifndef MODS_MODEL_H
#define MODS_MODEL_H

#include "iModFolder.h"

#include <types.h>

#include <stddef.h>

// The mods page without a window: which mods there are, which ones
// `[assets] mod` turns on and in what order, which files they share, and
// installing and removing them. What a mod folder and its mod.ini are is in
// iModFolder.h.
//
// Turning mods on and ordering them edits the setting through config_model.h,
// so it is saved, applied and discarded with every other setting. Installing
// and removing change the disk at once.

const S32 kModsModelMaxPath = 512;

struct ModsModelEntry
{
    // Where it is, and what `[assets] mod` says to name it: "mods/<folder>"
    // for one in the library, the folder itself for one outside it.
    char folder[kModsModelMaxPath];
    char setting[kModsModelMaxPath];

    iModInfo info;
    bool inLibrary;

    // Named by the setting but not there. The game skips it.
    bool missing;
};

// Look again: every folder in the library, then each folder the setting names
// that is somewhere else or not there. Returns how many.
S32 ModsModelScan();
S32 ModsModelCount();
const ModsModelEntry* ModsModelAt(S32 i);

// The library, `<exe dir>/mods`, or "" if the host cannot say.
const char* ModsModelLibrary();

// The mods the setting turns on, highest priority first, as entries. None is
// the original game.
S32 ModsModelEnabledCount();
S32 ModsModelEnabledAt(S32 rank);

// Where entry `i` is in that order, or -1 when it is off.
S32 ModsModelRankOf(S32 i);

// Turn entry `i` on, at the top, or off.
void ModsModelSetEnabled(S32 i, bool on);

// Move an entry that is on up (-1, higher priority) or down (+1).
void ModsModelMove(S32 i, S32 delta);

// Turn on a folder that is not in the library, where it is, at the top.
void ModsModelUseFolder(const char* folder);

// The files entries `a` and `b` both have, by path relative to their roots
// and ignoring case; mod.ini and mod.json are not counted. `examples` gets
// the first few, separated by ", ". Returns how many.
S32 ModsModelOverlap(S32 a, S32 b, char* examples, size_t examplesSize);

// The folder name to suggest for something unpacked into `staged`: the asset
// root's own name when the archive had one, `archive`'s file name without its
// extension otherwise.
void ModsModelSuggestName(const char* staged, const char* archive, char* out, size_t outSize);

// Whether `name` can be a new folder in the library. False fills in `why`.
bool ModsModelNameOk(const char* name, char* why, size_t whySize);

// Make a new, empty folder in the library to unpack an archive into.
bool ModsModelStage(char* out, size_t outSize, char* why, size_t whySize);

// Move the asset root found in `staged` into the library as `name`, then
// delete what is left of `staged`. `*index` is the new entry.
bool ModsModelInstall(const char* staged, const char* name, S32* index, char* why, size_t whySize);

// Copy a folder that holds a mod into the library as `name`.
bool ModsModelCopyIn(const char* source, const char* name, S32* index, char* why, size_t whySize);

// Delete a library entry's folder and everything in it. One outside the
// library is never deleted. It is turned off first.
bool ModsModelRemove(S32 i, char* why, size_t whySize);

// Delete a folder and everything in it.
bool ModsModelRemoveTree(const char* folder);

#endif
