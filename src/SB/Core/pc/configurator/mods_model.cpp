// The mods page's model. See mods_model.h.

#include "mods_model.h"

#include "config_model.h"
#include "iHost.h"
#include "iModManifest.h"
#include "iSaveFile.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    const S32 kMaxMods = 128;
    const size_t kMaxName = 256;

    ModsModelEntry gMods[kMaxMods];
    S32 gModCount;
    char gLibrary[kModsModelMaxPath];

    // The entries the setting turns on, highest priority first.
    S32 gEnabled[kModFolderMax];
    S32 gEnabledCount;

    // The prefix a library entry's setting carries, relative to the exe dir.
    const char* const kLibraryPrefix = "mods/";

    // Names in a folder, copied out: iHostDirNext's string lasts only until
    // the next call, and a walk that recurses opens handles of its own.
    struct NameList
    {
        char (*names)[kMaxName];
        S32 count;
    };

    bool listDir(const char* dir, NameList* out)
    {
        out->names = NULL;
        out->count = 0;

        iHostDir* d = iHostDirOpen(dir);
        if (d == NULL)
        {
            return false;
        }

        S32 room = 0;
        const char* name;
        while ((name = iHostDirNext(d)) != NULL)
        {
            if (out->count == room)
            {
                room = room == 0 ? 32 : room * 2;
                void* grown = realloc(out->names, (size_t)room * kMaxName);
                if (grown == NULL)
                {
                    break;
                }
                out->names = (char(*)[kMaxName])grown;
            }
            snprintf(out->names[out->count++], kMaxName, "%s", name);
        }

        iHostDirClose(d);
        return true;
    }

    void freeList(NameList* list)
    {
        free(list->names);
        list->names = NULL;
        list->count = 0;
    }

    bool isDir(const char* path)
    {
        iHostFileInfo info;
        return iHostStat(path, &info) && info.is_dir;
    }

    bool samePath(const char* a, const char* b)
    {
        char x[kModsModelMaxPath];
        char y[kModsModelMaxPath];
        if (!iHostAbsolutePath(a, x, sizeof(x)))
        {
            snprintf(x, sizeof(x), "%s", a);
        }
        if (!iHostAbsolutePath(b, y, sizeof(y)))
        {
            snprintf(y, sizeof(y), "%s", b);
        }
        return iHostStrCaseCmp(x, y) == 0;
    }

    S32 modSetting()
    {
        return ConfigModelFind("assets", "mod");
    }

    S32 addEntry(const char* folder, const char* setting, bool inLibrary, bool missing)
    {
        if (gModCount == kMaxMods)
        {
            return -1;
        }

        ModsModelEntry* e = &gMods[gModCount];
        memset(e, 0, sizeof(*e));
        snprintf(e->folder, sizeof(e->folder), "%s", folder);
        snprintf(e->setting, sizeof(e->setting), "%s", setting);
        e->inLibrary = inLibrary;
        e->missing = missing;
        if (missing)
        {
            iSaveFileLeaf(folder, e->info.name, sizeof(e->info.name));
        }
        else
        {
            iModFolderInfo(folder, &e->info);
        }
        return gModCount++;
    }

    // Write the order in gEnabled back to the setting.
    void writeSetting()
    {
        const S32 s = modSetting();
        if (s < 0)
        {
            return;
        }

        char text[kModFolderMax * (kModsModelMaxPath + 3)];
        size_t have = 0;
        text[0] = '\0';
        for (S32 r = 0; r < gEnabledCount; r++)
        {
            const int n = snprintf(text + have, sizeof(text) - have, "%s%s", r == 0 ? "" : " | ",
                                   gMods[gEnabled[r]].setting);
            if (n < 0 || (size_t)n >= sizeof(text) - have)
            {
                break;
            }
            have += (size_t)n;
        }

        ConfigModelSetText(s, text);
    }

    // Every file under `dir`, as paths relative to `root` in lower case.
    struct FileList
    {
        char (*paths)[kMaxName];
        S32 count;
        S32 room;
    };

    void collectFiles(const char* dir, size_t rootLength, FileList* out, S32 depth)
    {
        NameList list;
        if (depth > 8 || !listDir(dir, &list))
        {
            return;
        }

        for (S32 i = 0; i < list.count; i++)
        {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", dir, list.names[i]);
            if (isDir(path))
            {
                collectFiles(path, rootLength, out, depth + 1);
                continue;
            }

            const char* rel = path + rootLength + 1;
            if (iHostStrCaseCmp(rel, "mod.ini") == 0 || iHostStrCaseCmp(rel, "mod.json") == 0)
            {
                continue;
            }

            if (out->count == out->room)
            {
                out->room = out->room == 0 ? 256 : out->room * 2;
                void* grown = realloc(out->paths, (size_t)out->room * kMaxName);
                if (grown == NULL)
                {
                    break;
                }
                out->paths = (char(*)[kMaxName])grown;
            }

            char* to = out->paths[out->count++];
            snprintf(to, kMaxName, "%s", rel);
            for (char* c = to; *c != '\0'; c++)
            {
                *c = (char)tolower((unsigned char)*c);
            }
        }

        freeList(&list);
    }

    int comparePaths(const void* a, const void* b)
    {
        return strcmp((const char*)a, (const char*)b);
    }

    bool copyFile(const char* from, const char* to)
    {
        FILE* in = fopen(from, "rb");
        if (in == NULL)
        {
            return false;
        }
        FILE* out = fopen(to, "wb");
        if (out == NULL)
        {
            fclose(in);
            return false;
        }

        bool ok = true;
        char buffer[65536];
        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0)
        {
            if (fwrite(buffer, 1, n, out) != n)
            {
                ok = false;
                break;
            }
        }

        ok = ok && !ferror(in);
        fclose(in);
        ok = (fclose(out) == 0) && ok;
        return ok;
    }

    bool copyTree(const char* from, const char* to)
    {
        if (!iHostMakeDir(to))
        {
            return false;
        }

        NameList list;
        if (!listDir(from, &list))
        {
            return false;
        }

        bool ok = true;
        for (S32 i = 0; i < list.count && ok; i++)
        {
            char src[1024];
            char dst[1024];
            snprintf(src, sizeof(src), "%s/%s", from, list.names[i]);
            snprintf(dst, sizeof(dst), "%s/%s", to, list.names[i]);
            ok = isDir(src) ? copyTree(src, dst) : copyFile(src, dst);
        }

        freeList(&list);
        return ok;
    }

    // A Heavy Mod Manager mod keeps its mod.json beside files/, the folder
    // that is installed. The game reads it from the installed folder, so it
    // is copied into `dest`.
    bool copyManifest(const char* root, const char* dest)
    {
        char inside[1024];
        char manifest[1024];
        snprintf(inside, sizeof(inside), "%s/mod.json", root);
        if (iHostPathExists(inside) || !iModManifestPath(root, manifest, sizeof(manifest)))
        {
            return true;
        }

        char to[1024];
        snprintf(to, sizeof(to), "%s/mod.json", dest);
        return copyFile(manifest, to);
    }

    S32 findFolder(const char* folder)
    {
        for (S32 i = 0; i < gModCount; i++)
        {
            if (samePath(gMods[i].folder, folder))
            {
                return i;
            }
        }
        return -1;
    }
} // namespace

const char* ModsModelLibrary()
{
    if (gLibrary[0] == '\0' && !iModFolderLibrary(gLibrary, sizeof(gLibrary)))
    {
        gLibrary[0] = '\0';
    }
    return gLibrary;
}

S32 ModsModelScan()
{
    gModCount = 0;

    const char* library = ModsModelLibrary();
    NameList list;
    if (library[0] != '\0' && listDir(library, &list))
    {
        // Sorted by name, so the list does not follow the order the
        // filesystem happens to return.
        qsort(list.names, (size_t)list.count, kMaxName, [](const void* a, const void* b) {
            return iHostStrCaseCmp((const char*)a, (const char*)b);
        });

        for (S32 i = 0; i < list.count; i++)
        {
            // Dot folders are the install's staging folders, and anything
            // else a tool hides.
            if (list.names[i][0] == '.')
            {
                continue;
            }

            char folder[kModsModelMaxPath];
            char setting[kModsModelMaxPath];
            snprintf(folder, sizeof(folder), "%s/%s", library, list.names[i]);
            snprintf(setting, sizeof(setting), "%s%s", kLibraryPrefix, list.names[i]);
            if (isDir(folder))
            {
                addEntry(folder, setting, true, false);
            }
        }
        freeList(&list);
    }

    // Each folder the setting names, in its order. One somewhere else, or not
    // there at all, is listed too, so the page shows what the game will run.
    gEnabledCount = 0;
    const S32 s = modSetting();
    iModFolderPath listed[kModFolderMax];
    const S32 count = s >= 0 ? iModFolderSplit(ConfigModelText(s), listed, kModFolderMax) : 0;
    for (S32 i = 0; i < count; i++)
    {
        char folder[kModsModelMaxPath];
        S32 at;
        if (iModFolderResolve(listed[i], folder, sizeof(folder)))
        {
            at = findFolder(folder);
            if (at < 0)
            {
                at = addEntry(folder, listed[i], false, false);
            }
        }
        else
        {
            at = addEntry(listed[i], listed[i], false, true);
        }

        if (at >= 0 && ModsModelRankOf(at) < 0)
        {
            gEnabled[gEnabledCount++] = at;
        }
    }

    return gModCount;
}

S32 ModsModelCount()
{
    return gModCount;
}

const ModsModelEntry* ModsModelAt(S32 i)
{
    return &gMods[i];
}

S32 ModsModelEnabledCount()
{
    return gEnabledCount;
}

S32 ModsModelEnabledAt(S32 rank)
{
    return gEnabled[rank];
}

S32 ModsModelRankOf(S32 i)
{
    for (S32 r = 0; r < gEnabledCount; r++)
    {
        if (gEnabled[r] == i)
        {
            return r;
        }
    }
    return -1;
}

void ModsModelSetEnabled(S32 i, bool on)
{
    const S32 rank = ModsModelRankOf(i);
    if (on == (rank >= 0))
    {
        return;
    }

    if (on)
    {
        if (gEnabledCount == kModFolderMax)
        {
            return;
        }
        memmove(&gEnabled[1], &gEnabled[0], (size_t)gEnabledCount * sizeof(gEnabled[0]));
        gEnabled[0] = i;
        gEnabledCount++;
    }
    else
    {
        memmove(&gEnabled[rank], &gEnabled[rank + 1],
                (size_t)(gEnabledCount - rank - 1) * sizeof(gEnabled[0]));
        gEnabledCount--;
    }

    writeSetting();
}

void ModsModelMove(S32 i, S32 delta)
{
    const S32 rank = ModsModelRankOf(i);
    const S32 to = rank + delta;
    if (rank < 0 || to < 0 || to >= gEnabledCount)
    {
        return;
    }

    gEnabled[rank] = gEnabled[to];
    gEnabled[to] = i;
    writeSetting();
}

void ModsModelUseFolder(const char* folder)
{
    S32 at = findFolder(folder);
    if (at < 0)
    {
        at = addEntry(folder, folder, false, false);
    }
    if (at >= 0)
    {
        ModsModelSetEnabled(at, true);
    }
    ModsModelScan();
}

S32 ModsModelOverlap(S32 a, S32 b, char* examples, size_t examplesSize)
{
    examples[0] = '\0';
    if (gMods[a].missing || gMods[b].missing)
    {
        return 0;
    }

    FileList fa = {};
    FileList fb = {};
    collectFiles(gMods[a].folder, strlen(gMods[a].folder), &fa, 0);
    collectFiles(gMods[b].folder, strlen(gMods[b].folder), &fb, 0);
    qsort(fa.paths, (size_t)fa.count, kMaxName, comparePaths);
    qsort(fb.paths, (size_t)fb.count, kMaxName, comparePaths);

    S32 shared = 0;
    size_t have = 0;
    for (S32 i = 0, j = 0; i < fa.count && j < fb.count;)
    {
        const int order = strcmp(fa.paths[i], fb.paths[j]);
        if (order < 0)
        {
            i++;
            continue;
        }
        if (order > 0)
        {
            j++;
            continue;
        }

        if (shared < 5)
        {
            const int n = snprintf(examples + have, examplesSize - have, "%s%s",
                                   shared == 0 ? "" : ", ", fa.paths[i]);
            if (n > 0 && (size_t)n < examplesSize - have)
            {
                have += (size_t)n;
            }
        }
        shared++;
        i++;
        j++;
    }

    free(fa.paths);
    free(fb.paths);
    return shared;
}

void ModsModelSuggestName(const char* staged, const char* archive, char* out, size_t outSize)
{
    char root[kModsModelMaxPath];
    if (iModFolderFindAssets(staged, root, sizeof(root)) && !samePath(root, staged))
    {
        // A mod made for a GameCube mod manager keeps the disc's files in
        // files/, beside its mod.json, and that name says nothing about the
        // mod. The folder around it does, unless it is the archive's root.
        char leaf[kModsModelMaxPath];
        iSaveFileLeaf(root, leaf, sizeof(leaf));
        char* slash = strrchr(root, '/');
        if (iHostStrCaseCmp(leaf, "files") != 0 || slash == NULL)
        {
            snprintf(out, outSize, "%s", leaf);
            return;
        }

        *slash = '\0';
        if (!samePath(root, staged))
        {
            iSaveFileLeaf(root, out, outSize);
            return;
        }
    }

    char leaf[kModsModelMaxPath];
    iSaveFileLeaf(archive, leaf, sizeof(leaf));
    char* dot = strrchr(leaf, '.');
    if (dot != NULL && dot != leaf)
    {
        *dot = '\0';
    }
    snprintf(out, outSize, "%s", leaf);
}

bool ModsModelNameOk(const char* name, char* why, size_t whySize)
{
    if (name[0] == '\0' || name[0] == '.' || name[0] == ' ')
    {
        snprintf(why, whySize, "Give the mod a folder name. It cannot start with '.' or a space.");
        return false;
    }

    if (strpbrk(name, "/\\:*?\"<>|") != NULL)
    {
        snprintf(why, whySize, "A folder name cannot contain / \\ : * ? \" < > or |.");
        return false;
    }

    char folder[kModsModelMaxPath];
    snprintf(folder, sizeof(folder), "%s/%s", ModsModelLibrary(), name);
    if (iHostPathExists(folder))
    {
        snprintf(why, whySize, "There is already a mod called %s.", name);
        return false;
    }

    return true;
}

bool ModsModelStage(char* out, size_t outSize, char* why, size_t whySize)
{
    const char* library = ModsModelLibrary();
    if (library[0] == '\0' || !iHostMakeDir(library))
    {
        snprintf(why, whySize, "Could not make the mods folder %s.", library);
        return false;
    }

    for (S32 n = 0; n < 1000; n++)
    {
        snprintf(out, outSize, "%s/.install-%d", library, (int)n);
        if (!iHostPathExists(out) && iHostMakeDir(out))
        {
            return true;
        }
    }

    snprintf(why, whySize, "Could not make a folder in %s to unpack into.", library);
    return false;
}

bool ModsModelInstall(const char* staged, const char* name, S32* index, char* why, size_t whySize)
{
    char root[kModsModelMaxPath];
    if (!iModFolderFindAssets(staged, root, sizeof(root)))
    {
        snprintf(why, whySize,
                 "This does not look like a mod: there are no .HIP or .HOP files in it.");
        return false;
    }

    if (!ModsModelNameOk(name, why, whySize))
    {
        return false;
    }

    char dest[kModsModelMaxPath];
    snprintf(dest, sizeof(dest), "%s/%s", ModsModelLibrary(), name);

    if (!copyManifest(root, root))
    {
        snprintf(why, whySize, "Could not copy the mod's mod.json.");
        return false;
    }

    if (!iHostRenameReplace(root, dest))
    {
        snprintf(why, whySize, "Could not move the mod into %s.", dest);
        return false;
    }

    if (iHostPathExists(staged))
    {
        ModsModelRemoveTree(staged);
    }

    ModsModelScan();
    *index = findFolder(dest);
    return true;
}

bool ModsModelCopyIn(const char* source, const char* name, S32* index, char* why, size_t whySize)
{
    char root[kModsModelMaxPath];
    if (!iModFolderFindAssets(source, root, sizeof(root)))
    {
        snprintf(why, whySize,
                 "This does not look like a mod: there are no .HIP or .HOP files in it.");
        return false;
    }

    if (!ModsModelNameOk(name, why, whySize))
    {
        return false;
    }

    const char* library = ModsModelLibrary();
    char dest[kModsModelMaxPath];
    snprintf(dest, sizeof(dest), "%s/%s", library, name);

    if (!iHostMakeDir(library) || !copyTree(root, dest) || !copyManifest(root, dest))
    {
        ModsModelRemoveTree(dest);
        snprintf(why, whySize, "Could not copy the mod into %s.", dest);
        return false;
    }

    ModsModelScan();
    *index = findFolder(dest);
    return true;
}

bool ModsModelRemove(S32 i, char* why, size_t whySize)
{
    if (i < 0 || i >= gModCount || !gMods[i].inLibrary)
    {
        snprintf(why, whySize, "Only a mod in %s can be removed here.", ModsModelLibrary());
        return false;
    }

    char folder[kModsModelMaxPath];
    snprintf(folder, sizeof(folder), "%s", gMods[i].folder);

    ModsModelSetEnabled(i, false);

    if (!ModsModelRemoveTree(folder))
    {
        snprintf(why, whySize, "Could not delete everything in %s.", folder);
        ModsModelScan();
        return false;
    }

    ModsModelScan();
    return true;
}

bool ModsModelRemoveTree(const char* folder)
{
    NameList list;
    if (!listDir(folder, &list))
    {
        return !iHostPathExists(folder);
    }

    bool ok = true;
    for (S32 i = 0; i < list.count; i++)
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", folder, list.names[i]);
        ok = (isDir(path) ? ModsModelRemoveTree(path) : iHostRemoveFile(path)) && ok;
    }
    freeList(&list);

    return iHostRemoveDir(folder) && ok;
}
