#include <types.h>

#include "xAnim.h"
#include "xstransvc.h"
#include "xMemMgr.h"

#include "zAnimList.h"

S32 nals;
// NOTE (Square): I think these are Asset IDs, not Anim IDs
U32* aids; // anim IDs (not AIDS, you fool)
xAnimTable** atbls; // anim tables
S32* anused;

const char* astnames[2][10] = {
    "stop0", "stop1", "stop2", "stop3", "stop4", "stop5", "stop6", "stop7", "stop8", "stop9",
    "loop0", "loop1", "loop2", "loop3", "loop4", "loop5", "loop6", "loop7", "loop8", "loop9",
};

U32 AlwaysConditional(xAnimTransition*, xAnimSingle*, void*)
{
    return 1;
}

// Equivalent. Blocked on the reload-after-store compiler defect, not on shape.
// The target does `stw r3, nals; lwz r0, nals; cmpwi r0, 0` and then reuses that
// reloaded r0 for `slwi r4, r0, 2`; our compiler forwards the stored value, so it
// needs an extra `mr r4, r3` and shifts the forwarded copy instead. Measured:
// plain `if (nals == 0)` gives 98.581% (four differing lines); the volatile read
// below gives 99.488% (the load reappears, but volatile forbids CSE-ing it into
// the shift, so the copy stays). Binding the volatile read to a local folds the
// load away again and drops back to 98.581%. Also measured and rejected: a
// volatile *store* to nals, and reading through a `S32* pnals = &nals` (both
// 98.581% -- the store forwards anyway / the pointer is constant-propagated).
// No source form reaches 100%.
void zAnimListInit()
{
    // ALST = Animation list https://battlepedia.org/ALST
    nals = xSTAssetCountByType('ALST'); // 0x414C5354
    // The volatile read is a matching device: the original reloads nals here
    // instead of reusing the just-stored value. It does not reach a byte-exact
    // match on its own -- see the comment above zAnimListInit.
    if (*(volatile S32*)&nals == 0)
    {
        return;
    }

    // << 2 is the same as multiplying by 4
    // so it's just allocating size in bytes
    aids = (U32*)xMemAllocSize(nals * sizeof(S32));
    atbls = (xAnimTable**)xMemAllocSize(nals * sizeof(S32));
    anused = (S32*)xMemAllocSize(nals * sizeof(S32));

    for (S32 i = 0; i < nals; i++)
    {
        U32 size;
        zAnimListAsset* zala = (zAnimListAsset*)xSTFindAssetByType('ALST', i, &size);
        st_PKR_ASSET_TOCINFO ainfo;
        xSTGetAssetInfoByType('ALST', i, &ainfo);
        xAnimTable* atbl = xAnimTableNew("", NULL, 0);

        aids[i] = ainfo.aid;
        atbls[i] = atbl;
        anused[i] = 0;

        void* buf0 = xSTFindAsset(zala->ids[0], &size);
        if (buf0)
        {
            xAnimFile* afile = xAnimFileNew(buf0, "", 0, NULL);
            xAnimTableNewState(atbl, "idle", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                               xAnimDefaultBeforeEnter, NULL, NULL);
            xAnimTableAddFile(atbl, afile, "idle");
            anused[i]++;
        }

        for (S32 j = 1; j < 10; j++)
        {
            if (zala->ids[j] == 0)
            {
                continue;
            }

            void* buf = xSTFindAsset(zala->ids[j], &size);
            if (!buf)
            {
                continue;
            }

            xAnimFile* afile = xAnimFileNew(buf, "", 0, NULL);
            if ((S32)buf0)
            {
                xAnimTableNewState(atbl, astnames[0][j], 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL,
                                   NULL, xAnimDefaultBeforeEnter, NULL, NULL);
                xAnimTableNewTransition(atbl, astnames[0][j], "idle", AlwaysConditional, NULL, 0x10,
                                        0, 0.0f, 0.0f, 0, 0, 0.0f, 0);
            }
            else
            {
                xAnimTableNewState(atbl, astnames[0][j], 0, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                                   xAnimDefaultBeforeEnter, NULL, NULL);
            }

            xAnimTableAddFile(atbl, afile, astnames[0][j]);
            xAnimTableNewState(atbl, astnames[1][j], 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                               xAnimDefaultBeforeEnter, NULL, NULL);
            xAnimTableAddFile(atbl, afile, astnames[1][j]);
            anused[i]++;
        }
    }
}

void zAnimListExit()
{
    nals = 0;
    aids = NULL;
    atbls = NULL;
    anused = NULL;
}

xAnimTable* zAnimListGetTable(U32 id)
{
    U32* current_id = aids;

    for (S32 i = 0; i < nals; i++)
    {
        if (id == *current_id)
        {
            return atbls[i];
        }
        current_id++;
    }

    return NULL;
}

S32 zAnimListGetNumUsed(U32 id)
{
    U32* current_id = aids;

    for (S32 i = 0; i < nals; i++)
    {
        if (id == *current_id)
        {
            return anused[i];
        }
        current_id++;
    }

    return 0;
}
