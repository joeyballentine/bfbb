#include "xJSP.h"
#include "zGlobals.h"

#include <types.h>
#include <string.h>

#ifdef PLATFORM_PC
#include "iHipoly.h"
#include "iToon.h"
// The bake fit reads the world's geometry before iHipolyWorld replaces it.
#include "iEnvNormals.h"
#include "iScreen.h"
#endif

static RwV3d* sCurrVert;
static volatile S32 sAtomicStartCount;
static RwV3d** sAtomicStartVert;


static RpAtomic* CountAtomicCB(RpAtomic* atomic, void* data)
{
    sAtomicStartCount++;
    *(U32*)data += atomic->geometry->mesh->totalIndicesInMesh;
    return atomic;
}

static RpMesh* AddMeshCB(RpMesh* mesh, RpMeshHeader*, void* pData)
{
    RwV3d** stripVert = (RwV3d**)pData;
    for (U32 i = 0; i < mesh->numIndices; i++) {
        **stripVert = sCurrVert[mesh->indices[i]];
        (*stripVert)++;
    }
    return mesh;
}

static RpAtomic* AddAtomicCB(RpAtomic* atomic, void* data)
{
    // The dwarf defines this variable but it looks like it isnt used
    // TempAtomicList** tmpList;
    sAtomicStartVert[--sAtomicStartCount] = *(RwV3d**)data;
    sCurrVert = atomic->geometry->morphTarget->verts;
    _rpMeshHeaderForAllMeshes(atomic->geometry->mesh, (RpMeshCallBack)&AddMeshCB, data);
    return atomic;
}

static RpAtomic* AddAtomicPrecalcedVertCB(RpAtomic* atomic, void* data)
{
    sAtomicStartVert[--sAtomicStartCount] = *(RwV3d**)data;
    *(RwV3d**)data += atomic->geometry->mesh->totalIndicesInMesh;
    return atomic;
}

static RpAtomic* ListAtomicCB(RpAtomic* atomic, void* data)
{
    RpAtomic*** aList = (RpAtomic***)data;
    **aList = atomic;
    (*aList)++;
    return atomic;
}

void xJSP_MultiStreamRead(void* data, U32 size, xJSPHeader** jsp)
{
    U32 i;
    RpClump* clump;
    xClumpCollBSPTree* colltree;

    if (!*jsp)
    {
        *jsp = (xJSPHeaderGC*)RwMalloc(sizeof(xJSPHeaderGC));
        memset(*jsp, 0, sizeof(xJSPHeaderGC));
    }

    xJSPHeaderGC* hdr = (xJSPHeaderGC*)*jsp;

    __rwMark mark;
    __rwMark* mp = (__rwMark*)data;
    memmove(&mark, mp, sizeof(__rwMark));
    RwMemNative32(&mark, sizeof(__rwMark));

    if (mark.type == 0xBEEF01)
    {
        data = mp + 1;
#ifdef PLATFORM_PC
        // PORT: experimental.hipoly_assets rebuilds the clump's geometry here,
        // with the whole world in hand, and the tree read below is the one it
        // builds over the new triangles. The walk over the asset's marks
        // continues on the shipped buffer either way.
        // Before iHipolyWorld, which replaces the geometry the rig is read
        // from. iEnvNormals.h says why the shipped mesh is the one to read.
        if (iScreenWorldLighting() != IWORLDLIGHT_OFF)
        {
            iEnvFitShippedRig(hdr->clump);
        }

        U32 hipolySize = 0;
        void* hipolyTree = iHipolyWorld(hdr->clump, data, mark.length, &hipolySize);
        colltree = xClumpColl_StaticBufferInit(hipolyTree ? hipolyTree : data,
                                               hipolyTree ? hipolySize : mark.length);
        if (hipolyTree)
        {
            iHipolyWorldAttach(colltree, hipolyTree);
        }
#else
        colltree = xClumpColl_StaticBufferInit(data, mark.length);
#endif

        size -= mark.length + sizeof(__rwMark);
        data = (U8*)data + mark.length;

        mp = (__rwMark*)data;
        memmove(&mark, mp, sizeof(__rwMark));
        RwMemNative32(&mark, sizeof(__rwMark));

        data = mp + 1;

        xJSPHeader* tmphdr = (xJSPHeader*)data;
        strncpy(hdr->idtag, tmphdr->idtag, sizeof(hdr->idtag));
        hdr->version = tmphdr->version;
        hdr->jspNodeCount = tmphdr->jspNodeCount;
        hdr->colltree = colltree;
#ifdef BFBB_PTR64
        // xJSPHeader is 24 bytes on disk -- its three pointer fields are 4
        // bytes each there and are dead weight anyway, since all three are
        // filled in from elsewhere. sizeof(xJSPHeader) is 40 where a pointer
        // is 8, so the node list has to be found at the on-disk stride.
        hdr->jspNodeList = (xJSPNodeInfo*)((U8*)tmphdr + 24);
#else
        hdr->jspNodeList = (xJSPNodeInfo*)(tmphdr + 1);
#endif

        size -= mark.length + sizeof(__rwMark);

        for (i = 0; i < colltree->numTriangles; i++)
        {
            colltree->triangles[i].matIndex =
                hdr->jspNodeList[hdr->jspNodeCount - 1 - colltree->triangles[i].v.i.atomIndex]
                    .originalMatIndex;
        }

        if (size >= sizeof(__rwMark))
        {
            mp = (__rwMark*)((U8*)data + mark.length);
            memmove(&mark, mp, sizeof(__rwMark));
            RwMemNative32(&mark, sizeof(__rwMark));

            data = mp + 1;

#ifdef PLATFORM_PC
            // Precalculated strip vertices describe the shipped geometry, not
            // the rebuilt one.
            if (mark.type == 0xBEEF03 && !hipolyTree)
#else
            if (mark.type == 0xBEEF03)
#endif
            {
                hdr->stripVecCount = *(U32*)data;
                data = (U32*)data + 1;
                hdr->stripVecList = (RwV3d*)data;

                sAtomicStartCount = RpClumpGetNumAtomics(hdr->clump);
                sAtomicStartVert = (RwV3d**)RwMalloc(sAtomicStartCount * sizeof(RwV3d*));

                RwV3d* currVec = hdr->stripVecList;
                RpClumpForAllAtomics(hdr->clump, AddAtomicPrecalcedVertCB, &currVec);
            }
        }

        if (hdr->stripVecCount == 0)
        {
            hdr->stripVecCount = 0;
            sAtomicStartCount = 0;

            RpClumpForAllAtomics(hdr->clump, CountAtomicCB, &hdr->stripVecCount);

            hdr->stripVecList = (RwV3d*)RwMalloc(hdr->stripVecCount * sizeof(RwV3d));
            sAtomicStartVert = (RwV3d**)RwMalloc(sAtomicStartCount * sizeof(RwV3d**));

            RwV3d* currVec = hdr->stripVecList;
            RpClumpForAllAtomics(hdr->clump, AddAtomicCB, &currVec);
        }

        for (i = 0; i < colltree->numTriangles; i++)
        {
            colltree->triangles[i].v.p = sAtomicStartVert[colltree->triangles[i].v.i.atomIndex] +
                                         colltree->triangles[i].v.i.meshVertIndex;
        }

        RwFree(sAtomicStartVert);
    }
    else
    {
        RwMemory rwmem;
        rwmem.start = (RwUInt8*)data;
        rwmem.length = size;

        RwStream* stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &rwmem);
        RwStreamFindChunk(stream, rwID_CLUMP, NULL, NULL);
        clump = RpClumpStreamRead(stream);
        RwStreamClose(stream, NULL);

        if (!hdr->clump)
        {
            hdr->clump = clump;
        }
        else
        {
            S32 i, atomCount;

            atomCount = RpClumpGetNumAtomics(clump);
            if (atomCount)
            {
                RpAtomic** atomList = (RpAtomic**)RwMalloc(atomCount * sizeof(RpAtomic*));
                RpAtomic** atomCurr = atomList;
                RpClumpForAllAtomics(clump, ListAtomicCB, &atomCurr);

                for (i = atomCount - 1; i >= 0; i--)
                {
                    RpClumpRemoveAtomic(clump, atomList[i]);
                    RpClumpAddAtomic(hdr->clump, atomList[i]);
                    RpAtomicSetFrame(atomList[i], RpClumpGetFrame(hdr->clump));
                }

                RwFree(atomList);
            }

            RpClumpDestroy(clump);
        }
    }
}

void xJSP_Destroy(xJSPHeader* jsp)
{
    if (globals.sceneCur->env->geom->jsp == jsp && globals.sceneCur->env->geom->world)
    {
        RpWorldDestroy(globals.sceneCur->env->geom->world);
        globals.sceneCur->env->geom->world = NULL;
    }

#ifdef PLATFORM_PC
    iToonForgetModel(jsp->clump);
    iHipolyForget(jsp->clump);
#endif
    RpClumpDestroy(jsp->clump);
#ifdef PLATFORM_PC
    iHipolyWorldDetach(jsp->colltree);
#endif
    RwFree(jsp->colltree);

    U32* tp = (U32*)((xJSPHeaderGC*)jsp)->stripVecList;
    U32 test = *(tp - sizeof(U32));
    RwMemNative32(&test, sizeof(U32));
    if (test != 0xBEEF03)
    {
        RwFree(((xJSPHeaderGC*)jsp)->stripVecList);
    }

    RwFree(jsp);
}
