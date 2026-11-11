#ifndef IMODMANIFEST_H
#define IMODMANIFEST_H

#include <types.h>

#include <stddef.h>

// PC-only: Heavy Mod Manager's mod.json.
//
// A Heavy Mod Manager mod is a folder holding mod.json and files/, the disc
// files the mod changes. The manager installs it by copying files/ over the
// game's files, with two exceptions the port reproduces at load time:
//
// - MergeFiles lists packages, one per line, relative to files/. Each holds
//   only the mod's changes to the game's package of the same name, and is
//   merged into it (iHipMerge.h).
// - INIReplacements holds SB.INI lines, one per line, that replace the game's
//   lines with the same key (iModManifestApplyIni).
//
// IpsPatchBase64, DOLPatches, ArCodes and GeckoCodes patch the game's
// executable. The port cannot apply them; iModPatch.h has the ones written
// out in C++.

struct iModManifest
{
    // '\n'-separated, as in mod.json. NULL when the field is missing.
    char* mergeFiles;
    char* iniReplacements;

    // Whether any of the executable patches is non-empty.
    bool patchesExecutable;
};

// Where the mod.json for mod folder `folder` is: `folder`/mod.json, or, when
// `folder` is the mod's files/ folder, the mod.json beside it. False when
// there is none.
bool iModManifestPath(const char* folder, char* out, size_t outSize);

// Reads the mod.json for mod folder `folder`. False, with `out` zeroed, when
// there is none. A mod.json that is not valid JSON is reported on stdout and
// read as none.
bool iModManifestRead(const char* folder, iModManifest* out);
void iModManifestFree(iModManifest* manifest);

// Whether `rel`, a path relative to the mod's files/, is in MergeFiles. Case
// is ignored and '/' matches '\'.
bool iModManifestMerges(const iModManifest* manifest, const char* rel);

// SB.INI text with Heavy Mod Manager's INIReplacements applied. A line is
// `key = value`, and '#' starts a comment. Each replacement removes the game's
// lines with the same key, ignoring case, and is added at the end, so a key
// SB.INI lists several times, like TaskStatus, is replaced as a whole. For
// ScenePlayerMapping, ThresholdPointsRange and AlternateCostumeMapping, which
// SB.INI lists once per scene, the key is the name and the value's first
// word. Returns malloc'd text, `outSize` bytes long with a NUL after them.
char* iModManifestApplyIni(const char* ini, size_t iniSize, const char* replacements,
                           size_t* outSize);

#endif
