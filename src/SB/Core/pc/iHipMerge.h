#ifndef IHIPMERGE_H
#define IHIPMERGE_H

#include <types.h>

#include <stddef.h>

// PC-only: Heavy Mod Manager's package merge.
//
// A Heavy Mod Manager mod lists packages in its mod.json's MergeFiles (see
// iModManifest.h). Each of those holds only the assets the mod adds or
// changes, and the manager merges it into the game's package of the same name
// when it installs the mod. The port does the same merge when the package is
// opened, with the manager's rules:
//
// - A mod asset with the ID of one of the game's assets replaces that asset's
//   type, flags, debug block and data. It stays in the game's layer.
// - A mod COLL, JAW, LODT, PIPT, SHDW or SNDI table is merged into the game's
//   first table of the same type, entry by entry. A mod entry removes the
//   game's entries with the same key and is appended at the end.
// - Any other mod asset is added. The mod's layers are added after the game's,
//   less the assets that replaced or merged into the game's; a layer left
//   empty is dropped.
// - Layers are ordered by type the way the retail packages are: textures, BSP,
//   JSP info, models, animations, default, cutscenes, sound RAM, sound TOC.
//
// Both packages must come from the same platform.
//
// The merged package is not built whole. Its header and merged tables are
// made in memory, and its asset data is read from the two packages when it is
// read, so a 200 MB package costs a few hundred KB to open.

// A file's bytes, made when it is opened: a file on disk, a buffer, or a
// merge of two packages.
struct iHipSource;

// NULL when the file will not open.
iHipSource* iHipSourceFile(const char* path);

// Takes `data`, which must come from malloc.
iHipSource* iHipSourceMemory(U8* data, U32 size);

// `mod` merged into `base`. Takes both, and closes them on failure, which
// returns NULL with the reason in `why`.
iHipSource* iHipSourceMerge(iHipSource* base, iHipSource* mod, char* why, size_t whySize);

U32 iHipSourceSize(const iHipSource* source);

// Reads `size` bytes at `offset`. Bytes past the end read as zeros. False
// when a file underneath could not be read.
bool iHipSourceRead(iHipSource* source, U32 offset, void* buf, U32 size);

void iHipSourceClose(iHipSource* source);

#endif
