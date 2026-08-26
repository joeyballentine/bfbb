// RenderWare C API: RpClump.
//
// RpClump is mirrored onto rw::Clump (see include/rwsdk/rpworld.h and
// layout_clump.cpp), so an RpClump* IS an rw::Clump* and most of this is a
// cast and a call.
//
// Two of the six are not, and both times the reason is that RenderWare's own
// implementation of them is in this repository -- src/rwsdk/world/baclump.c is
// decompiled, matching code -- and it does something librw does not.
// RpClumpAddAtomic is the important one: see below.

#include <rwcore.h>
#include <rpworld.h>

#include "stream.h" // brings in librw's rw.h, which must not be included twice

static inline rw::Clump* asClump(RpClump* clump)
{
    return reinterpret_cast<rw::Clump*>(clump);
}

static inline rw::Atomic* asAtomic(RpAtomic* atomic)
{
    return reinterpret_cast<rw::Atomic*>(atomic);
}

RpClump* RpClumpStreamRead(RwStream* stream)
{
    if (stream == NULL)
    {
        return NULL;
    }

    // Both sides expect the rwID_CLUMP chunk header to have been eaten already
    // -- iModel.cpp:143 and xJSP.cpp:154 both call RwStreamFindChunk first --
    // and pick up at the STRUCT chunk inside it.
    //
    // librw appends each atomic as it reads it, so RpClumpForAllAtomics
    // afterwards walks them in the order the file lists them. RenderWare's own
    // reader is the one function of this group that is NOT decompiled in
    // src/rwsdk, so that cannot be checked against retail here; what can be
    // said is that file order is the only order that survives a write/read
    // round trip through RenderWare's own exporter, given that RpClumpAddAtomic
    // prepends (below). iModel.cpp cares -- it returns the FIRST atomic of a
    // multi-atomic model as the model -- so if a model ever comes out
    // inside-out, this is the line to doubt first.
    return reinterpret_cast<RpClump*>(rw::Clump::streamRead(stream));
}

RwBool RpClumpDestroy(RpClump* clump)
{
    if (clump == NULL)
    {
        return FALSE;
    }

    // Takes the atomics, lights, cameras and the whole frame hierarchy with
    // it, exactly as RpClumpDestroy in src/rwsdk/world/baclump.c does. xJSP.cpp
    // relies on that in both directions: it moves the atomics it wants OUT of
    // a temporary clump before destroying it, and then lets the destroy take
    // the frames that clump owned.
    //
    // One difference, and it is librw's assert rather than a behaviour:
    // Clump::destroy asserts the clump is not still in a world, where
    // RenderWare would free it and leave the world holding a dangling link.
    // Nothing in the game calls RpWorldAddClump, so neither path is reachable
    // today.
    asClump(clump)->destroy();
    return TRUE;
}

// RenderWare PREPENDS -- baclump.c uses rwLinkListAddLLLink, which inserts at
// the head -- where librw's Clump::addAtomic appends. That is not a detail:
// xJSP.cpp:173 moves atomics from one clump to another by walking them into an
// array in list order and then adding them back in REVERSE index order, which
// only preserves their order if each add goes to the front. Appending instead
// would reverse the atomic list of every merged JSP, and xJSP builds its
// stripVecList by walking that list, so the vertex data would end up attached
// to the wrong pieces of the level.
//
// So this does the list surgery itself rather than call librw. It is the same
// two writes librw's addAtomic makes, on the other end of the list.
RpClump* RpClumpAddAtomic(RpClump* clump, RpAtomic* atomic)
{
    if (clump == NULL || atomic == NULL)
    {
        return clump;
    }

    atomic->clump = clump;
    asClump(clump)->atomics.add(&asAtomic(atomic)->inClump);
    return clump;
}

RpClump* RpClumpRemoveAtomic(RpClump* clump, RpAtomic* atomic)
{
    if (clump == NULL || atomic == NULL)
    {
        return clump;
    }

    // Unlike addAtomic, librw's removeAtomic is what RenderWare's is, minus an
    // assert that the atomic really is in THIS clump. Going through librw
    // keeps that assert, which is worth having: RpClumpRemoveAtomic on the
    // wrong clump silently corrupts both lists.
    asClump(clump)->removeAtomic(asAtomic(atomic));
    return clump;
}

RpClump* RpClumpForAllAtomics(RpClump* clump, RpAtomicCallBack callback, void* pData)
{
    if (clump == NULL || callback == NULL)
    {
        return clump;
    }

    // The `next` link is read before the callback runs, because callers remove
    // the atomic they were handed: xJSP.cpp's ListAtomicCB does not, but
    // RpClumpDestroy's equivalent walk does, and RenderWare's own
    // RpClumpForAllAtomics takes the same precaution.
    rw::LinkList& atomics = asClump(clump)->atomics;
    for (rw::LLLink* cur = atomics.link.next; cur != atomics.end();)
    {
        rw::Atomic* a = rw::Atomic::fromClump(cur);
        rw::LLLink* next = cur->next;

        if (callback(reinterpret_cast<RpAtomic*>(a), pData) == NULL)
        {
            // RenderWare stops early on a NULL return. iModel.cpp's
            // NextAtomicCallback does not use that, but xJSP.cpp's CountAtomicCB
            // would break the array it is filling if it were ignored.
            return clump;
        }

        cur = next;
    }

    return clump;
}

RwInt32 RpClumpGetNumAtomics(RpClump* clump)
{
    if (clump == NULL)
    {
        return 0;
    }

    return asClump(clump)->countAtomics();
}
