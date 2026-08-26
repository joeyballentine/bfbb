// Does librw read BFBB's actual Xbox assets, through the shim's RenderWare C API?
//
// Answer, measured against hb01.HOP (Hoop Boulevard) on 2026-08-26:
//
//   RWTX 17576   bytes -> read ok, 1 textures; first 128x128 8-bit raster=EMPTY
//   JSP  1758696 bytes -> read ok, 296 atomics, 31268 verts, 33539 tris
//
// Geometry comes through whole: that is the level's real mesh, read by
// RpClumpStreamRead out of a retail Xbox pack with no conversion step. Texture
// dictionaries parse and their headers are right -- the dimensions and the
// 8-bit palettised depth are what an Xbox TXD should say.
//
// The rasters are empty because LIBRW_PLATFORM=NULL has no render backend to
// upload pixels to; librw's null driver does nothing in rasterCreate. That is
// the NULL platform, not a format problem, and it is the next thing to retest
// against a GL3 or D3D9 librw. Note the shim deliberately #errors against those
// backends today (see engine_start.cpp and im.cpp) because EngineOpenParams and
// RwIm2DVertex are unresolved for them -- so this is a measurement of where the
// line currently is, not a claim that textures work.
//
// Build: see README.md, then
//   assettest.exe "<path>/hb/hb01.HOP"
//
// Not a unit test: it opens a retail HOP, walks its table of contents with the
// same AHDR layout xpkrsvc uses, and hands the raw asset bytes to
// RwTexDictionaryStreamRead and RpClumpStreamRead. Everything the game would do
// to load a level's textures and geometry, minus the game.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rwcore.h>
#include <rpworld.h>
#include <rpskin.h>
#include <rpmatfx.h>

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
        if (t->raster) { g_w = t->raster->width; g_h = t->raster->height; g_d = t->raster->depth;
                         g_haspixels = (t->raster->originalPixels != NULL) || (t->raster->cpPixels != NULL); }
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
