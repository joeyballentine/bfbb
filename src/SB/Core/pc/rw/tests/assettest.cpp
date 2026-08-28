// Does librw read BFBB's actual Xbox assets, through the shim's RenderWare C API?
//
// Answer, measured against hb01.HOP (Hoop Boulevard) with LIBRW_PLATFORM=D3D9
// on 2026-08-26:
//
//   RWTX 17576   bytes -> read ok, 1 textures; 128x128 32-bit raster=has pixels
//   RWTX 2216    bytes -> read ok, 1 textures; 32x32   32-bit raster=has pixels
//   RWTX 5288    bytes -> read ok, 1 textures; 64x64   32-bit raster=has pixels
//   RWTX 17576   bytes -> read ok, 1 textures; 128x128 32-bit raster=has pixels
//   JSP  1758696 bytes -> read ok, 296 atomics, 31268 verts, 33539 tris
//   JSP  509172  bytes -> no CLUMP chunk at the start
//
// Every texture dictionary reads and every raster comes back with real pixel
// data in it. The second JSP is not a failure: it is Heavy Iron's own 0xbeef01
// chunk, which xJSP_MultiStreamRead reads separately for the node list and the
// collision tree.
//
// TWO THINGS THAT LOOKED LIKE FAILURES AND WERE NOT, both worth knowing before
// reading a result off this test again:
//
//   * Under LIBRW_PLATFORM=NULL every raster reported EMPTY. That was true --
//     there was no device to upload pixels to -- and it stayed true after a
//     D3D9 backend was linked, for a different reason: librw hands back NATIVE
//     Xbox rasters and calls Raster::convertTexToCurrentPlatform from nowhere
//     at all. The conversion is the application's job and the shim now does it
//     in RwTexDictionaryStreamRead. A missing conversion looks exactly like a
//     missing backend.
//   * The check for "has pixels" used to read raster->originalPixels and
//     ->cpPixels. Those are STAGING pointers, live only while a raster is
//     locked; a device-backed raster keeps its pixels in the driver and reports
//     NULL for both. The test locks the raster now and scans for a nonzero
//     byte, because a freshly created raster locks just as successfully as a
//     loaded one and hands back a page of zeros.
//
// STILL UNEXPLAINED: every texture's name comes back empty. The selftest
// confirms RwTexture::name is mirrored at the right offset, so the names are
// genuinely absent from these dictionaries rather than misread. BFBB looks up
// RWTX assets by their HIP directory name, so nothing may depend on the
// texture's own -- but zAssetTypes.cpp's RWTX_Read is the place to check
// before trusting that.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rwcore.h>
#include <rpworld.h>
#include <rpskin.h>
#include <rpmatfx.h>

// Pulls in librw's own header. The raster check below asks librw directly --
// locking a raster is not something the RenderWare C API exposes here -- and
// this is the one include that provides it. Not "rw.h": librw's header has no
// include guard, and stream.h is the file that owns including it.
#include "../stream.h"

#include "iWindow.h"

static unsigned char* g_file;
static long g_size;

static int g_texcount, g_w, g_h, g_d, g_verts, g_tris;
static bool g_haspixels;
static char g_firstname[64];

static RwTexture* CountTexCB(RwTexture* t, void*)
{
    if (g_texcount == 0 && t)
    {
        snprintf(g_firstname, sizeof(g_firstname), "%s", t->name);
        if (t->raster)
        {
            g_w = t->raster->width; g_h = t->raster->height; g_d = t->raster->depth;

            // originalPixels/cpPixels are STAGING pointers, live only while a
            // raster is locked. A device-backed raster keeps its pixels in the
            // driver's texture object, so both are NULL and reading them says
            // "EMPTY" about a raster that is perfectly full -- which is what
            // this test reported until the day a D3D9 backend was linked.
            //
            // Locking is the portable question: it hands back the pixels
            // whatever platform they live on. The nonzero scan is the rest of
            // it, because a freshly created raster locks just as successfully
            // as a loaded one and returns a page of zeros.
            rw::Raster* ras = reinterpret_cast<rw::Raster*>(t->raster);
            rw::uint8* px = ras->lock(0, rw::Raster::LOCKREAD);
            g_haspixels = false;
            if (px != NULL)
            {
                int span = ras->width * (ras->depth / 8);
                if (span > 4096) { span = 4096; }
                for (int i = 0; i < span; i++)
                {
                    if (px[i] != 0) { g_haspixels = true; break; }
                }
                ras->unlock(0);
            }
        }
    }
    g_texcount++;
    return t;
}

static RpAtomic* CountGeomCB(RpAtomic* a, void*)
{
    if (a && a->geometry)
    {
        g_verts += a->geometry->numVertices;
        g_tris += a->geometry->numTriangles;
    }
    return a;
}

static unsigned be32(const unsigned char* p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

int main(int argc, char** argv)
{
    FILE* f = fopen(argv[1], "rb");
    if (!f) { printf("cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END); g_size = ftell(f); fseek(f, 0, SEEK_SET);
    g_file = (unsigned char*)malloc(g_size);
    fread(g_file, 1, g_size, f);
    fclose(f);
    printf("%s  (%ld bytes)\n\n", argv[1], g_size);

    // A real backend needs a window before the engine can open on it. Under
    // LIBRW_PLATFORM=NULL there is none and this stays headless -- which is the
    // configuration this test was first written against, where every raster
    // came back EMPTY because there was no device to upload pixels to.
#ifndef RW_NULL
    iWindowParams windowParams;
    windowParams.title = "bfbb assettest";
    windowParams.width = 640;
    windowParams.height = 480;
    windowParams.mode = iWINDOW_WINDOWED;
    if (!iWindowOpen(&windowParams))
    {
        printf("could not open a window; the render backend cannot start" );
        return 1;
    }
#endif

    RwEngineInit(NULL, 0, 0);
    RpWorldPluginAttach();
    RpSkinPluginAttach();
    RpMatFXPluginAttach();
    RwEngineOpen(NULL);
    RwEngineStart();

    // Walk ATOC -> AHDR entries. Container fields are big-endian on every
    // platform; the payloads they point at are little-endian on Xbox.
    unsigned char* atoc = (unsigned char*)memchr(g_file, 'A', g_size);
    while (atoc && memcmp(atoc, "ATOC", 4) != 0)
        atoc = (unsigned char*)memchr(atoc + 1, 'A', g_size - (atoc + 1 - g_file));
    unsigned char* end = atoc + 8 + be32(atoc + 4);
    unsigned char* off = atoc + 8;
    off += 8 + be32(off + 4); // AINF

    int txds = 0, txdok = 0, ntex = 0;
    int clumps = 0, clumpok = 0, natomic = 0;

    while (off < end && memcmp(off, "AHDR", 4) == 0)
    {
        unsigned sz = be32(off + 4);
        unsigned char* b = off + 8;
        char type[5] = { (char)b[4], (char)b[5], (char)b[6], (char)b[7], 0 };
        unsigned aoff = be32(b + 8), asize = be32(b + 12);

        if (strcmp(type, "RWTX") == 0 && txds < 4)
        {
            txds++;
            RwMemory mem = { g_file + aoff, asize };
            RwStream* s = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &mem);
            if (s && RwStreamFindChunk(s, rwID_TEXDICTIONARY, NULL, NULL))
            {
                RwTexDictionary* txd = RwTexDictionaryStreamRead(s);
                if (txd) { txdok++; }
                printf("  RWTX %-7u bytes -> %s", asize, txd ? "read ok" : "NULL");
                if (txd)
                {
                    g_texcount = 0; g_firstname[0] = 0; g_w = g_h = g_d = 0;
                    RwTexDictionaryForAllTextures(txd, CountTexCB, NULL);
                    ntex += g_texcount;
                    printf(", %d textures; first \"%s\" %dx%d %d-bit raster=%s",
                           g_texcount, g_firstname, g_w, g_h, g_d,
                           g_haspixels ? "has pixels" : "EMPTY");
                }
                putchar(10);
            }
            else printf("  RWTX %-7u bytes -> no TEXDICTIONARY chunk\n", asize);
            if (s) RwStreamClose(s, NULL);
        }

        if (strcmp(type, "JSP ") == 0 && clumps < 4)
        {
            clumps++;
            RwMemory mem = { g_file + aoff, asize };
            RwStream* s = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &mem);
            if (s && RwStreamFindChunk(s, rwID_CLUMP, NULL, NULL))
            {
                RpClump* c = RpClumpStreamRead(s);
                if (c) { clumpok++; natomic += RpClumpGetNumAtomics(c);
                         g_verts = g_tris = 0;
                         RpClumpForAllAtomics(c, CountGeomCB, NULL); }
                printf("  JSP  %-7u bytes -> %s", asize, c ? "read ok" : "NULL");
                if (c) printf(", %d atomics, %d verts, %d tris",
                              RpClumpGetNumAtomics(c), g_verts, g_tris);
                printf("\n");
            }
            else printf("  JSP  %-7u bytes -> no CLUMP chunk at the start\n", asize);
            if (s) RwStreamClose(s, NULL);
        }

        off += 8 + sz;
    }

    printf("\ntexture dictionaries: %d/%d read\n", txdok, txds);
    printf("clumps:               %d/%d read, %d atomics total\n", clumpok, clumps, natomic);
    return 0;
}
