#include "iAssetOverride.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iFile.h"
#include "iHost.h"
#include "menu/iAssetBuild.h"
#include "xpkrsvc.h"

void MNU3_Build(iAssetPkg& p);
void MNU4_Build(iAssetPkg& p);
void MNU5_Build(iAssetPkg& p);
extern const U32 kMNU3OwnedTypes[];
extern const U32 kMNU4OwnedTypes[];
extern const U32 kMNU5OwnedTypes[];

namespace
{
struct Override
{
    const char* package; // the HIP's file name, any folder, any case
    const U32* types;    // zero-terminated
    void (*build)(iAssetPkg&);
};

const Override kOverrides[] = {
    { "mnu3.HIP", kMNU3OwnedTypes, MNU3_Build },
    { "mnu4.HIP", kMNU4OwnedTypes, MNU4_Build },
    { "mnu5.HIP", kMNU5OwnedTypes, MNU5_Build },
};

// A package open with an override. Few at once: the menu scene and whatever
// is loading beside it.
struct Open
{
    st_PACKER_READ_DATA* pr;
    const Override* ov;
    iAssetPkg* pkg;
    S32 verified;
};

const S32 kMaxOpen = 8;
Open sOpen[kMaxOpen];

S32 sEnabled = TRUE;
st_PACKER_READ_FUNCS* sReal;
st_PACKER_READ_FUNCS sWrapped;

const char* BaseName(const char* path)
{
    const char* b = path;
    for (const char* c = path; *c != '\0'; c++)
    {
        if (*c == '/' || *c == '\\')
        {
            b = c + 1;
        }
    }
    return b;
}

Open* Find(st_PACKER_READ_DATA* pr)
{
    for (S32 i = 0; i < kMaxOpen; i++)
    {
        if (sOpen[i].pr == pr && pr != NULL)
        {
            return &sOpen[i];
        }
    }
    return NULL;
}

S32 Owns(const Open* o, U32 type)
{
    for (const U32* t = o->ov->types; *t != 0; t++)
    {
        if (*t == type)
        {
            return TRUE;
        }
    }
    return FALSE;
}

// The HIP's asset type, or 0 if the HIP has no such asset.
U32 RealType(st_PACKER_READ_DATA* pr, U32 aid)
{
    st_PKR_ASSET_TOCINFO info;
    if (!sReal->PkgHasAsset(pr, aid) || !sReal->GetAssetInfo(pr, aid, &info) ||
        info.typeref == NULL)
    {
        return 0;
    }
    return info.typeref->typetag;
}

st_PACKER_ASSETTYPE* TypeRef(st_PACKER_READ_DATA* pr, U32 type)
{
    for (st_PACKER_ASSETTYPE* t = pr->types; t->typetag != 0; t++)
    {
        if (t->typetag == type)
        {
            return t;
        }
    }
    return NULL;
}

void FillInfo(st_PACKER_READ_DATA* pr, const iAssetEntry* e, st_PKR_ASSET_TOCINFO* info)
{
    info->aid = e->aid;
    info->typeref = TypeRef(pr, e->type);
    info->sector = 0;
    info->plus_offset = 0;
    info->size = e->size;
    info->mempos = e->data;
}

void Verify(Open* o)
{
    if (o->verified)
    {
        return;
    }
    const char* env = getenv("BFBB_ASSET_VERIFY");
    if (env == NULL || env[0] == '\0' || env[0] == '0')
    {
        o->verified = TRUE;
        return;
    }

    // Wait for the HIP: its assets are ready once its layers have loaded.
    for (S32 i = 0; i < o->pkg->Count(); i++)
    {
        U32 aid = o->pkg->Entry(i).aid;
        if (sReal->PkgHasAsset(o->pr, aid) && !sReal->IsAssetReady(o->pr, aid))
        {
            return;
        }
    }
    o->verified = TRUE;

    S32 same = 0, differ = 0, added = 0, removed = 0;
    for (S32 i = 0; i < o->pkg->Count(); i++)
    {
        const iAssetEntry& e = o->pkg->Entry(i);
        if (!sReal->PkgHasAsset(o->pr, e.aid))
        {
            added++;
            printf("bfbb: %s: asset %08X ('%.4s') is not in the HIP\n", o->ov->package, e.aid,
                   (const char*)&e.type);
            continue;
        }
        U32 size = sReal->GetAssetSize(o->pr, e.aid);
        void* data = sReal->LoadAsset(o->pr, e.aid, NULL, NULL);
        if (size == e.size && memcmp(data, e.data, size) == 0)
        {
            same++;
        }
        else
        {
            differ++;
            printf("bfbb: %s: asset %08X ('%.4s') differs from the HIP (%u vs %u bytes)\n",
                   o->ov->package, e.aid, (const char*)&e.type, e.size, size);
        }
    }

    // What the HIP has of the owned types that the code does not.
    for (const U32* t = o->ov->types; *t != 0; t++)
    {
        S32 n = sReal->AssetCount(o->pr, *t);
        for (S32 i = 0; i < n; i++)
        {
            st_PKR_ASSET_TOCINFO info;
            if (sReal->GetAssetInfoByType(o->pr, *t, i, &info) && o->pkg->Find(info.aid) == NULL)
            {
                removed++;
                printf("bfbb: %s: HIP asset %08X is not in the code\n", o->ov->package,
                       info.aid);
            }
        }
    }

    // Objects are created in type-index order, so the order is checked too.
    S32 reordered = 0;
    for (const U32* t = o->ov->types; *t != 0; t++)
    {
        S32 n = sReal->AssetCount(o->pr, *t);
        for (S32 i = 0; i < n && i < o->pkg->CountType(*t); i++)
        {
            st_PKR_ASSET_TOCINFO info;
            if (sReal->GetAssetInfoByType(o->pr, *t, i, &info) &&
                o->pkg->ByType(*t, i)->aid != info.aid)
            {
                reordered++;
            }
        }
    }

    printf("bfbb: %s: %d assets match the HIP, %d differ, %d added, %d removed, "
           "%d out of order\n",
           o->ov->package, same, differ, added, removed, reordered);
}

Open* Owned(st_PACKER_READ_DATA* pr, U32 type)
{
    Open* o = Find(pr);
    if (o == NULL || !Owns(o, type))
    {
        return NULL;
    }
    Verify(o);
    return o;
}

// An ID the code built, or NULL. *hidden is set when the HIP has the ID as an
// owned type the code left out, so the asset must look absent.
const iAssetEntry* Lookup(st_PACKER_READ_DATA* pr, U32 aid, S32* hidden)
{
    *hidden = FALSE;
    Open* o = Find(pr);
    if (o == NULL)
    {
        return NULL;
    }
    Verify(o);
    const iAssetEntry* e = o->pkg->Find(aid);
    if (e == NULL)
    {
        U32 type = RealType(pr, aid);
        *hidden = type != 0 && Owns(o, type);
    }
    return e;
}

st_PACKER_READ_DATA* W_Init(void* userdata, char* pkgfile, U32 opts, S32* cltver,
                            st_PACKER_ASSETTYPE* types)
{
    st_PACKER_READ_DATA* pr = sReal->Init(userdata, pkgfile, opts, cltver, types);
    if (pr == NULL || !sEnabled)
    {
        return pr;
    }

    const char* name = BaseName(pkgfile);
    for (U32 i = 0; i < sizeof(kOverrides) / sizeof(kOverrides[0]); i++)
    {
        if (iHostStrCaseCmp(name, kOverrides[i].package) != 0)
        {
            continue;
        }
        for (S32 j = 0; j < kMaxOpen; j++)
        {
            if (sOpen[j].pr == NULL)
            {
                sOpen[j].pr = pr;
                sOpen[j].ov = &kOverrides[i];
                sOpen[j].pkg = new iAssetPkg;
                sOpen[j].verified = FALSE;
                kOverrides[i].build(*sOpen[j].pkg);

                // The packer skips an asset another loaded package already has
                // (mnu4.HIP shares some with mnu3.HIP), and retail never makes
                // an object for it. The built assets follow the same rule.
                for (S32 k = sOpen[j].pkg->Count() - 1; k >= 0; k--)
                {
                    const iAssetEntry& e = sOpen[j].pkg->Entry(k);
                    if (PKR_FRIEND_assetIsGameDup(e.aid, pr, -1, 0, 0, NULL))
                    {
                        printf("bfbb: %s: asset %08X is already loaded from another "
                               "package; the code's copy is ignored\n",
                               kOverrides[i].package, e.aid);
                        sOpen[j].pkg->Remove(k);
                    }
                }
                printf("bfbb: %s: %d assets built from code\n", kOverrides[i].package,
                       sOpen[j].pkg->Count());
                return pr;
            }
        }
        printf("bfbb: %s: too many overridden packages open; using the HIP\n",
               kOverrides[i].package);
    }
    return pr;
}

void W_Done(st_PACKER_READ_DATA* pr)
{
    Open* o = Find(pr);
    if (o != NULL)
    {
        delete o->pkg;
        memset(o, 0, sizeof(*o));
    }
    sReal->Done(pr);
}

U32 W_GetAssetSize(st_PACKER_READ_DATA* pr, U32 aid)
{
    S32 hidden;
    const iAssetEntry* e = Lookup(pr, aid, &hidden);
    if (e != NULL)
    {
        return e->size;
    }
    return hidden ? 0 : sReal->GetAssetSize(pr, aid);
}

void* W_LoadAsset(st_PACKER_READ_DATA* pr, U32 aid, const char* name, void* dflt)
{
    S32 hidden;
    const iAssetEntry* e = Lookup(pr, aid, &hidden);
    if (e != NULL)
    {
        return e->data;
    }
    return hidden ? NULL : sReal->LoadAsset(pr, aid, name, dflt);
}

void* W_AssetByType(st_PACKER_READ_DATA* pr, U32 type, S32 idx, U32* size)
{
    Open* o = Owned(pr, type);
    if (o == NULL)
    {
        return sReal->AssetByType(pr, type, idx, size);
    }
    const iAssetEntry* e = o->pkg->ByType(type, idx < 0 ? 0 : idx);
    if (size != NULL)
    {
        *size = e != NULL ? e->size : 0;
    }
    return e != NULL ? e->data : NULL;
}

S32 W_AssetCount(st_PACKER_READ_DATA* pr, U32 type)
{
    Open* o = Owned(pr, type);
    return o != NULL ? o->pkg->CountType(type) : sReal->AssetCount(pr, type);
}

S32 W_IsAssetReady(st_PACKER_READ_DATA* pr, U32 aid)
{
    S32 hidden;
    if (Lookup(pr, aid, &hidden) != NULL)
    {
        return TRUE;
    }
    return hidden ? FALSE : sReal->IsAssetReady(pr, aid);
}

S32 W_GetAssetInfo(st_PACKER_READ_DATA* pr, U32 aid, st_PKR_ASSET_TOCINFO* info)
{
    S32 hidden;
    const iAssetEntry* e = Lookup(pr, aid, &hidden);
    if (e != NULL)
    {
        FillInfo(pr, e, info);
        return TRUE;
    }
    return hidden ? FALSE : sReal->GetAssetInfo(pr, aid, info);
}

S32 W_GetAssetInfoByType(st_PACKER_READ_DATA* pr, U32 type, S32 idx, st_PKR_ASSET_TOCINFO* info)
{
    Open* o = Owned(pr, type);
    if (o == NULL)
    {
        return sReal->GetAssetInfoByType(pr, type, idx, info);
    }
    const iAssetEntry* e = o->pkg->ByType(type, idx < 0 ? 0 : idx);
    if (e == NULL)
    {
        return FALSE;
    }
    FillInfo(pr, e, info);
    return TRUE;
}

S32 W_PkgHasAsset(st_PACKER_READ_DATA* pr, U32 aid)
{
    S32 hidden;
    if (Lookup(pr, aid, &hidden) != NULL)
    {
        return TRUE;
    }
    return hidden ? FALSE : sReal->PkgHasAsset(pr, aid);
}
} // namespace

void iAssetOverrideSetEnabled(S32 on)
{
    sEnabled = on;
}

st_PACKER_READ_FUNCS* iFilePackageReadFuncs(st_PACKER_READ_FUNCS* funcs)
{
    sReal = funcs;
    sWrapped = *funcs;
    sWrapped.Init = W_Init;
    sWrapped.Done = W_Done;
    sWrapped.GetAssetSize = W_GetAssetSize;
    sWrapped.LoadAsset = W_LoadAsset;
    sWrapped.AssetByType = W_AssetByType;
    sWrapped.AssetCount = W_AssetCount;
    sWrapped.IsAssetReady = W_IsAssetReady;
    sWrapped.GetAssetInfo = W_GetAssetInfo;
    sWrapped.GetAssetInfoByType = W_GetAssetInfoByType;
    sWrapped.PkgHasAsset = W_PkgHasAsset;
    return &sWrapped;
}
