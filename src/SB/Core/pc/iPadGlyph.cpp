// Button-prompt glyphs read from loose PNGs.
//
// The interface and the argument for it are in iPadGlyph.h.
//
// It reaches past the RenderWare shim into librw for decoding a PNG, which the
// C API has no call for and librw already carries lodepng for, and for building
// the talk prompt's geometry. It does the rest through the shim like any other
// platform file. It cannot live beside the shim in rw/: it asks the input
// backend what is bound to what, and bfbb_rw does not link bfbb_platform.

#include <rwcore.h>

#include "rw.h"

#include "iHost.h"
#include "iPad.h"
#include "iPadBind.h"
#include "iPadGlyph.h"
#include "iPadHost.h"

#include "iPadLayout.h"
#include "iToon.h"
#include "xModel.h"
#include "xPad.h"
#include "xString.h"

#include <rpworld.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    // A set is a FOLDER OF FILES, not a fixed twelve.
    //
    // Retail has twelve prompt textures, so a console set has twelve pictures
    // and nothing forces a cache. A keyboard has no such number: a prompt draws
    // whatever key is bound to that button, and [keyboard] can name any of a
    // hundred. So a glyph is looked up by FILE NAME and cached as it is asked
    // for, and a set is however many files it happens to hold.
    //
    // The face four are named for their POSITION rather than for a letter,
    // because the letter is the one thing that is not shared: the button east
    // of jump is printed B on an Xbox pad and X on a GameCube one. A file
    // called b.png could mean either, and gamecube/b.png holding an X reads
    // like a bug rather than like the truth. tools/padglyphs.py writes the same
    // names.
    //
    // The two sticks are not inputs anything binds. They are the pictures
    // button_move_text and button_camtoggle_text ask for, and those prompts
    // mean "push the stick", not "press the click", so they resolve straight to
    // a file and never go near a binding.

    // A PAD binding is written in SDL's input names, and those four ARE
    // positions -- SDL calls the east button `b` whatever is printed on it --
    // so this is a rename, not a mapping.
    //
    // It must NOT be applied to a keyboard binding. There "a" is the A key, and
    // renaming it to "south" would draw a face button for it.
    struct TokenGlyph
    {
        const char* token;
        const char* glyph;
    };

    const TokenGlyph kTokenGlyphs[] = {
        { "a", "south" },
        { "b", "east" },
        { "x", "west" },
        { "y", "north" },
    };

    const S32 kTokenGlyphCount = (S32)(sizeof(kTokenGlyphs) / sizeof(kTokenGlyphs[0]));

    // What each of retail's twelve textures means, in the port's terms.
    //
    // `mask` is the game button the prompt is about, and the thing a binding is
    // looked up by. `fallback` is the glyph to draw when the binding cannot
    // name one input -- a chord, or nothing bound -- and is also what the file
    // names in a set are keyed to, since a set is extracted into the port's
    // slot order.
    //
    // pad_button_R2 carries XPAD_BUTTON_HUD rather than R2. button_status_text
    // is the only prompt that uses it, it is the HUD prompt, and every disc
    // drew its own HUD button in that slot: the GameCube's Z, the PS2's R2, the
    // Xbox's black button. The R2 bit has no prompt of its own.
    //
    // Nothing draws the close camera. No disc has a prompt for it, so giving it
    // a button of its own costs no picture.
    //
    // The face four are numbered by `picture`, and which bit each shows depends
    // on the disc the font came off: iPadLayoutPictureBit. Every disc's menu
    // prompts agree on the buttons themselves: cancel draws the button printed
    // B, options the one printed X, accept A and the third choice Y. The code
    // cancels on TRIANGLE and opens options on O, so B is TRIANGLE and X is O
    // -- which is gc/iPad.cpp's mapping as well.
    //
    // A MOVE is not read here. The consoles do disagree about those, and what
    // they disagree about is which bit the move reads rather than which button
    // the bit is; iPadLayout.h holds that, and iTextPatch.cpp points a move's
    // prompt at the picture to match.
    struct Slot
    {
        const char* name;
        S32 picture; // 1 to 4 for the face four, else 0
        U32 mask;
        const char* fallback;
    };

    const Slot kSlots[] = {
        { "pad_button1", 1, 0, NULL },
        { "pad_button2", 2, 0, NULL },
        { "pad_button3", 3, 0, NULL },
        { "pad_button4", 4, 0, NULL },
        { "pad_button_L1", 0, XPAD_BUTTON_L1, "lt" },
        { "pad_button_R1", 0, XPAD_BUTTON_R1, "rt" },
        { "pad_button_L2", 0, XPAD_BUTTON_L2, "lb" },
        { "pad_button_R2", 0, XPAD_BUTTON_HUD, "rb" },
        { "pad_button_start", 0, XPAD_BUTTON_START, "start" },
        { "pad_button_select", 0, XPAD_BUTTON_SELECT, "back" },
        { "pad_button_L_analog", 0, 0, "stick_left" },
        { "pad_button_R_analog", 0, 0, "stick_right" },
    };

    U32 slotMask(const Slot* slot)
    {
        return (slot->picture != 0) ? iPadLayoutPictureBit(slot->picture) : slot->mask;
    }

    const S32 kSlotCount = (S32)(sizeof(kSlots) / sizeof(kSlots[0]));

    S32 sEnabled;

    // What config.ini asked for, and what that resolved to. They differ when
    // the answer is "auto": `sActive` then holds whatever the pad turned out to
    // be, and a pad swapped mid-game changes it.
    char sChoice[64] = "auto";
    char sActive[64];
    char sRoot[512];

    // One file that has been asked for, whether or not it was there. `tex` is
    // NULL for a file a set does not hold, and the entry is kept anyway: a set
    // may carry only the glyphs it wants to override, and without the negative
    // answer every prompt of every frame would stat the same missing file.
    struct Glyph
    {
        char name[32];
        RwTexture* tex;
    };

    // Twelve is what retail's prompts can ask for at once. The slack is for a
    // keyboard set, where the twelve are drawn from a hundred possible keys and
    // a rebind mid-session asks for a file the cache has not seen.
    const S32 kGlyphMax = 48;

    Glyph sGlyph[kGlyphMax];
    S32 sGlyphCount;
    S32 sLoaded;

    // So an unreadable set says so once rather than once per frame per prompt.
    char sComplained[64];

    // The talk prompt's model for one file of the live set, or a remembered
    // miss (NULL clump), which means the disc's model.
    struct GlyphModel
    {
        char name[32];
        RpClump* clump;
        RpAtomic* atomic;
    };

    // One talk binding is live at a time, so a handful covers a session of
    // rebinding. A full table starts again, as the glyph cache does.
    const S32 kModelMax = 8;

    GlyphModel sModel[kModelMax];
    S32 sModelCount;

    // The disc's atomic, while iPadGlyphModelBegin has an instance pointed at a
    // replacement.
    RpAtomic* sModelDisc;

    void dropModels()
    {
        for (S32 i = 0; i < sModelCount; i++)
        {
            if (sModel[i].clump != NULL)
            {
                iToonForgetModel(sModel[i].clump);
                RpClumpDestroy(sModel[i].clump);
            }
        }
        sModelCount = 0;
    }

    void dropGlyphs()
    {
        dropModels();

        for (S32 i = 0; i < sGlyphCount; i++)
        {
            if (sGlyph[i].tex != NULL)
            {
                RwTextureDestroy(sGlyph[i].tex);
                sGlyph[i].tex = NULL;
            }
        }
        sGlyphCount = 0;
        sLoaded = 0;
    }

    // One PNG into one image, or NULL. A set is allowed to be incomplete, so
    // a missing file is not a failure and not a diagnostic: it means "leave
    // that prompt as the disc drew it".
    rw::Image* readGlyphImage(const char* path)
    {
        // readPNG asserts on a file it cannot open, and an assert is compiled
        // out of a release build -- the nil then reaches lodepng and takes the
        // process with it. So the existence check is load-bearing, not a
        // courtesy.
        if (!iHostPathExists(path))
        {
            return NULL;
        }

        rw::Image* image = rw::readPNG(path);
        if (image == NULL)
        {
            printf("bfbb: %s is not a PNG this can read; that prompt stays as the disc drew it\n",
                   path);
            fflush(stdout);
            return NULL;
        }

        // The prompts are drawn over the game, so a set author's transparency
        // has to survive whatever bit depth they saved at.
        image->convertTo32();
        return image;
    }

    RwTexture* loadTexture(const char* path)
    {
        rw::Image* image = readGlyphImage(path);
        if (image == NULL)
        {
            return NULL;
        }

        rw::Raster* raster = rw::Raster::createFromImage(image);
        image->destroy();

        if (raster == NULL)
        {
            printf("bfbb: no raster could be made for %s\n", path);
            fflush(stdout);
            return NULL;
        }

        rw::Texture* texture = rw::Texture::create(raster);
        if (texture == NULL)
        {
            raster->destroy();
            return NULL;
        }

        // Retail filters these linearly (xFont.cpp does it to every {tex:}
        // raster it resolves) and they are drawn at whatever size the text is,
        // so the same has to happen here or a set would look different from the
        // disc's own glyphs for no reason the player could name.
        texture->setFilter(rw::Texture::LINEAR);
        texture->setAddressU(rw::Texture::CLAMP);
        texture->setAddressV(rw::Texture::CLAMP);

        return reinterpret_cast<RwTexture*>(texture);
    }

    RwTexture* loadGlyph(const char* set, const char* glyph)
    {
        char path[768];
        snprintf(path, sizeof(path), "%s/buttons/%s/%s.png", sRoot, set, glyph);
        return loadTexture(path);
    }

    // Which set to draw. "auto" asks the backend and takes its answer; anything
    // else is a folder name and is used as written.
    const char* resolveSet()
    {
        if (iHostStrCaseCmp(sChoice, "auto") != 0)
        {
            return sChoice;
        }

        // A keyboard is a device with its own pictures, not the absence of a
        // pad, and with both plugged in it is the one last touched that the
        // prompts should show. Asked before iPadHostPadKind, which answers
        // about the controller whether or not anyone is holding it.
        if (iPadHostKeyboardIsLive())
        {
            return "keyboard";
        }

        const char* kind = iPadHostPadKind();
        if (kind != NULL)
        {
            return kind;
        }

        // No pad, or one the backend cannot place. Keep whatever is already
        // being drawn rather than snapping back: a wireless controller going to
        // sleep, or a cable pulled for a moment, would otherwise change every
        // prompt in the game and change them back. Only a pad the backend CAN
        // place moves the set.
        if (sLoaded && sActive[0] != '\0')
        {
            return sActive;
        }

        // Nothing has been drawn yet, so there is no last answer to keep. The
        // Xbox set is what the game's own files hold.
        return "xbox";
    }

    // Loads the set if it is not the one already loaded. False when there is
    // nothing to draw from.
    bool ensureSet()
    {
        const char* want = resolveSet();

        if (sLoaded && iHostStrCaseCmp(sActive, want) == 0)
        {
            return true;
        }

        // A set that is not there has already been reported, and this runs for
        // every prompt of every frame. Answering from the name alone keeps it
        // from being one stat() per prompt for the rest of the session.
        if (!sLoaded && iHostStrCaseCmp(sComplained, want) == 0)
        {
            return false;
        }

        dropGlyphs();

        char dir[700];
        snprintf(dir, sizeof(dir), "%s/buttons/%s", sRoot, want);
        if (!iHostPathExists(dir))
        {
            snprintf(sComplained, sizeof(sComplained), "%s", want);
            printf("bfbb: no button glyphs at %s -- prompts stay as the disc drew them\n", dir);
            fflush(stdout);
            sActive[0] = '\0';
            return false;
        }

        snprintf(sActive, sizeof(sActive), "%s", want);
        sLoaded = 1;

        // Printed on every change, not just the first, because under "auto"
        // this is what a controller being swapped mid-game looks like from
        // outside -- and a set that quietly did not change is the failure worth
        // seeing. No count to go with it: nothing has been read yet, and what a
        // set holds is not knowable without listing the folder.
        printf("bfbb: button prompts from buttons/%s\n", want);
        fflush(stdout);

        return true;
    }

    // The texture for one file of the live set, read the first time it is asked
    // for and remembered after -- including a miss, so a set that does not hold
    // it is not stat'd again every frame.
    RwTexture* glyphNamed(const char* file)
    {
        if (file == NULL || file[0] == '\0')
        {
            return NULL;
        }

        for (S32 i = 0; i < sGlyphCount; i++)
        {
            if (iHostStrCaseCmp(sGlyph[i].name, file) == 0)
            {
                return sGlyph[i].tex;
            }
        }

        if ((size_t)strlen(file) >= sizeof(sGlyph[0].name))
        {
            return NULL;
        }

        // Full. Only reachable by rebinding over and over in one session, and
        // starting the cache again costs one reload of what is on screen --
        // which is cheaper than growing without limit or never caching again.
        if (sGlyphCount == kGlyphMax)
        {
            const char* set = sActive;
            char keep[64];
            snprintf(keep, sizeof(keep), "%s", set);
            dropGlyphs();
            snprintf(sActive, sizeof(sActive), "%s", keep);
            sLoaded = 1;
        }

        Glyph* g = &sGlyph[sGlyphCount++];
        snprintf(g->name, sizeof(g->name), "%s", file);
        g->tex = loadGlyph(sActive, file);
        return g->tex;
    }

    // The FILE a bound input is drawn from. On a pad the four face names are
    // positions under another name and get renamed; on the keyboard they are
    // the letter keys and must be left exactly as they are.
    const char* glyphFileFor(const char* token, S32 fromKeyboard)
    {
        if (!fromKeyboard)
        {
            for (S32 i = 0; i < kTokenGlyphCount; i++)
            {
                if (iHostStrCaseCmp(kTokenGlyphs[i].token, token) == 0)
                {
                    return kTokenGlyphs[i].glyph;
                }
            }
        }

        return token;
    }

    const Slot* findSlot(const char* name)
    {
        for (S32 i = 0; i < kSlotCount; i++)
        {
            if (iHostStrCaseCmp(kSlots[i].name, name) == 0)
            {
                return &kSlots[i];
            }
        }
        return NULL;
    }

    // The file a slot is drawn from when its binding names no picture in the
    // live set. Without the layout lookup a GameCube set would draw its X
    // beside "Return to Game". A set nobody has a table for gets the Xbox's
    // positions.
    const char* slotOwnFile(const Slot* slot)
    {
        U32 mask = slotMask(slot);
        const char* printed = iPadLayoutFaceGlyph(sActive, mask);
        if (printed == NULL && slot->picture != 0)
        {
            printed = iPadLayoutFaceGlyph("xbox", mask);
        }
        return (printed != NULL) ? printed : slot->fallback;
    }

    // The glyph a slot draws in the live set, and in `file` the name it was
    // read under: the bound input's picture when the set has one, else the
    // slot's own. Call after ensureSet.
    RwTexture* slotGlyph(const Slot* slot, const char** file)
    {
        // A prompt about a game button asks what presses it. A prompt about a
        // stick (mask 0) has nothing to ask and goes straight to its picture.
        U32 mask = slotMask(slot);
        if (mask != 0)
        {
            const char* input = iPadHostBoundInput(mask);
            if (input != NULL)
            {
                const char* bound = glyphFileFor(input, iPadHostKeyboardIsLive());
                RwTexture* tex = glyphNamed(bound);
                if (tex != NULL)
                {
                    *file = bound;
                    return tex;
                }
            }
        }

        *file = slotOwnFile(slot);
        return glyphNamed(*file);
    }

    // The folder modelTextureRead looks in, for the one model being read.
    char sModelDir[700];

    // A model's textures are PNGs beside it, named for the texture its
    // material names: gamecube/rt.dff's material names Btn_gc_r, and
    // gamecube/Btn_gc_r.png is that picture. Installed only while the model
    // is read, in place of iSystem.cpp's reader, which looks in the asset
    // store.
    rw::Texture* modelTextureRead(const char* name, const char* mask)
    {
        (void)mask;
        char path[768];
        snprintf(path, sizeof(path), "%s/%s.png", sModelDir, name);
        rw::Texture* tex = reinterpret_cast<rw::Texture*>(loadTexture(path));
        if (tex != NULL)
        {
            snprintf(tex->name, sizeof(tex->name), "%s", name);
        }
        else
        {
            printf("bfbb: %s is missing; that model draws untextured\n", path);
            fflush(stdout);
        }
        return tex;
    }

    RpAtomic* firstAtomic(RpAtomic* atomic, void* data)
    {
        RpAtomic** out = (RpAtomic**)data;
        if (*out == NULL)
        {
            *out = atomic;
        }
        return atomic;
    }

    // `<set>/<file>.dff`, or NULL when the set has none.
    RpClump* loadModel(const char* file)
    {
        char path[768];
        snprintf(path, sizeof(path), "%s/buttons/%s/%s.dff", sRoot, sActive, file);
        if (!iHostPathExists(path))
        {
            return NULL;
        }

        FILE* f = fopen(path, "rb");
        if (f == NULL)
        {
            return NULL;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        U8* buf = (size > 0) ? (U8*)malloc((size_t)size) : NULL;
        bool read = buf != NULL && fread(buf, 1, (size_t)size, f) == (size_t)size;
        fclose(f);

        RpClump* clump = NULL;
        if (read)
        {
            RwMemory mem;
            mem.start = buf;
            mem.length = (RwUInt32)size;
            RwStream* stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &mem);
            if (stream != NULL)
            {
                if (RwStreamFindChunk(stream, rwID_CLUMP, NULL, NULL))
                {
                    // No dictionary current while it reads: librw adds every
                    // texture it reads to the current one, and the scene's
                    // dictionary goes away with the scene while these stay.
                    snprintf(sModelDir, sizeof(sModelDir), "%s/buttons/%s", sRoot, sActive);
                    rw::Texture* (*gameRead)(const char*, const char*) = rw::Texture::readCB;
                    rw::TexDictionary* dict = rw::TexDictionary::getCurrent();
                    rw::Texture::readCB = modelTextureRead;
                    rw::TexDictionary::setCurrent(NULL);

                    clump = RpClumpStreamRead(stream);

                    rw::TexDictionary::setCurrent(dict);
                    rw::Texture::readCB = gameRead;
                }
                RwStreamClose(stream, NULL);
            }
        }
        free(buf);

        if (clump == NULL)
        {
            printf("bfbb: %s is not a model this can read; the talk prompt keeps the disc's\n",
                   path);
            fflush(stdout);
        }
        return clump;
    }

    bool texelSolid(const rw::Image* image, S32 x, S32 y)
    {
        if (x < 0 || y < 0 || x >= image->width || y >= image->height)
        {
            return false;
        }
        return image->pixels[y * image->stride + x * 4 + 3] >= 128;
    }

    struct SlabBuilder
    {
        rw::V3d* pos;
        rw::V3d* nrm;
        rw::TexCoords* uv;
        rw::Triangle* tri;
        S32 verts;
        S32 tris;
    };

    // One quad, corners in order around it. Both windings, so the slab shows
    // whichever way the pass culls.
    void slabQuad(SlabBuilder* b, const rw::V3d* p, rw::V3d n, const rw::TexCoords* t)
    {
        static const U16 kTris[4][3] = { { 0, 1, 2 }, { 0, 2, 3 }, { 0, 2, 1 }, { 0, 3, 2 } };

        S32 base = b->verts;
        for (S32 k = 0; k < 4; k++)
        {
            b->pos[base + k] = p[k];
            b->nrm[base + k] = n;
            b->uv[base + k] = t[k];
        }
        b->verts += 4;

        for (S32 k = 0; k < 4; k++)
        {
            rw::Triangle* tri = &b->tri[b->tris++];
            tri->v[0] = (U16)(base + kTris[k][0]);
            tri->v[1] = (U16)(base + kTris[k][1]);
            tri->v[2] = (U16)(base + kTris[k][2]);
            tri->matId = 0;
        }
    }

    // A glyph picture made solid: its opaque texels pushed out into a slab,
    // the picture on both faces, and each edge texel's colour down the wall
    // beside it. A key glyph comes out a keycap and a face button a disc, so
    // this stands in for any input the set holds no model of.
    //
    // Laid out the way the disc's fx_glyph_talk is: the face looks down +x, y
    // is up, and u runs with +z. The faces are built from runs of opaque
    // texels rather than one quad, because the talk prompt draws in the
    // opaque pass and a transparent texel there would draw as its colour.
    RpClump* extrudeGlyph(const char* file, RwTexture* tex)
    {
        // About the disc model's size: its trigger is 0.56 tall and 0.74 wide.
        const F32 kWidth = 0.64f;
        const F32 kDepth = 0.2f;

        char path[768];
        snprintf(path, sizeof(path), "%s/buttons/%s/%s.png", sRoot, sActive, file);
        rw::Image* image = readGlyphImage(path);
        if (image == NULL)
        {
            return NULL;
        }

        S32 w = image->width;
        S32 h = image->height;

        S32 runs = 0;
        S32 walls = 0;
        for (S32 y = 0; y < h; y++)
        {
            for (S32 x = 0; x < w; x++)
            {
                if (!texelSolid(image, x, y))
                {
                    continue;
                }
                runs += !texelSolid(image, x - 1, y);
                walls += !texelSolid(image, x - 1, y) + !texelSolid(image, x + 1, y) +
                         !texelSolid(image, x, y - 1) + !texelSolid(image, x, y + 1);
            }
        }

        S32 quads = 2 * runs + walls;
        if (runs == 0 || quads * 4 > 0xFFFF)
        {
            image->destroy();
            return NULL;
        }

        rw::Geometry* geo = rw::Geometry::create(quads * 4, quads * 4,
                                                 rw::Geometry::POSITIONS | rw::Geometry::TEXTURED |
                                                     rw::Geometry::NORMALS | rw::Geometry::LIGHT);
        if (geo == NULL)
        {
            image->destroy();
            return NULL;
        }

        SlabBuilder b;
        b.pos = geo->morphTargets[0].vertices;
        b.nrm = geo->morphTargets[0].normals;
        b.uv = geo->texCoords[0];
        b.tri = geo->triangles;
        b.verts = 0;
        b.tris = 0;

        F32 texel = kWidth / (F32)((w > h) ? w : h);
        F32 front = 0.5f * kDepth;
        F32 back = -0.5f * kDepth;

#define SLAB_Z(col) (((F32)(col) - 0.5f * (F32)w) * texel)
#define SLAB_Y(row) ((0.5f * (F32)h - (F32)(row)) * texel)

        for (S32 y = 0; y < h; y++)
        {
            for (S32 x = 0; x < w; x++)
            {
                if (!texelSolid(image, x, y))
                {
                    continue;
                }

                // A run of the faces starts here.
                if (!texelSolid(image, x - 1, y))
                {
                    S32 end = x;
                    while (texelSolid(image, end, y))
                    {
                        end++;
                    }

                    F32 z0 = SLAB_Z(x), z1 = SLAB_Z(end);
                    F32 y0 = SLAB_Y(y), y1 = SLAB_Y(y + 1);
                    F32 u0 = (F32)x / w, u1 = (F32)end / w;
                    F32 v0 = (F32)y / h, v1 = (F32)(y + 1) / h;
                    rw::TexCoords t[4] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };

                    rw::V3d pf[4] = {
                        { front, y0, z0 }, { front, y0, z1 }, { front, y1, z1 }, { front, y1, z0 }
                    };
                    slabQuad(&b, pf, rw::V3d{ 1.0f, 0.0f, 0.0f }, t);

                    rw::V3d pb[4] = {
                        { back, y0, z0 }, { back, y0, z1 }, { back, y1, z1 }, { back, y1, z0 }
                    };
                    slabQuad(&b, pb, rw::V3d{ -1.0f, 0.0f, 0.0f }, t);
                }

                // The walls take this texel's colour, so the sides are the
                // glyph's own outline.
                rw::TexCoords c = { ((F32)x + 0.5f) / w, ((F32)y + 0.5f) / h };
                rw::TexCoords t[4] = { c, c, c, c };
                F32 z0 = SLAB_Z(x), z1 = SLAB_Z(x + 1);
                F32 y0 = SLAB_Y(y), y1 = SLAB_Y(y + 1);

                if (!texelSolid(image, x - 1, y))
                {
                    rw::V3d p[4] = {
                        { front, y0, z0 }, { back, y0, z0 }, { back, y1, z0 }, { front, y1, z0 }
                    };
                    slabQuad(&b, p, rw::V3d{ 0.0f, 0.0f, -1.0f }, t);
                }
                if (!texelSolid(image, x + 1, y))
                {
                    rw::V3d p[4] = {
                        { front, y0, z1 }, { back, y0, z1 }, { back, y1, z1 }, { front, y1, z1 }
                    };
                    slabQuad(&b, p, rw::V3d{ 0.0f, 0.0f, 1.0f }, t);
                }
                if (!texelSolid(image, x, y - 1))
                {
                    rw::V3d p[4] = {
                        { front, y0, z0 }, { front, y0, z1 }, { back, y0, z1 }, { back, y0, z0 }
                    };
                    slabQuad(&b, p, rw::V3d{ 0.0f, 1.0f, 0.0f }, t);
                }
                if (!texelSolid(image, x, y + 1))
                {
                    rw::V3d p[4] = {
                        { front, y1, z0 }, { front, y1, z1 }, { back, y1, z1 }, { back, y1, z0 }
                    };
                    slabQuad(&b, p, rw::V3d{ 0.0f, -1.0f, 0.0f }, t);
                }
            }
        }

#undef SLAB_Z
#undef SLAB_Y

        image->destroy();

        rw::Material* mat = rw::Material::create();
        mat->setTexture(reinterpret_cast<rw::Texture*>(tex));
        geo->matList.appendMaterial(mat);
        mat->destroy();

        geo->calculateBoundingSphere();
        geo->buildMeshes();

        rw::Clump* clump = rw::Clump::create();
        rw::Frame* frame = rw::Frame::create();
        rw::Atomic* atomic = rw::Atomic::create();
        clump->setFrame(frame);
        atomic->setFrame(frame);
        atomic->setGeometry(geo, 0);
        geo->destroy();
        RpClumpAddAtomic(reinterpret_cast<RpClump*>(clump), reinterpret_cast<RpAtomic*>(atomic));

        return reinterpret_cast<RpClump*>(clump);
    }

    // The talk prompt is button_picture_05, which every disc points at R1, and
    // R1 is what zNPCGoalVillager.cpp talks on.
    //
    // The set's own model of the bound input wins. Failing that, the slot's own
    // picture is the button the disc modelled, so the disc's model is right --
    // that is the Xbox set and the PS2's, whose disc carries the same trigger.
    // Anything else, a key or a rebound button, is its glyph made solid.
    RpAtomic* talkModel(RpAtomic* disc)
    {
        const Slot* slot = findSlot("pad_button_R1");
        if (slot == NULL)
        {
            return disc;
        }

        const char* file = NULL;
        RwTexture* tex = slotGlyph(slot, &file);
        if (file == NULL || strlen(file) >= sizeof(sModel[0].name))
        {
            return disc;
        }

        for (S32 i = 0; i < sModelCount; i++)
        {
            if (iHostStrCaseCmp(sModel[i].name, file) == 0)
            {
                return (sModel[i].atomic != NULL) ? sModel[i].atomic : disc;
            }
        }

        if (sModelCount == kModelMax)
        {
            dropModels();
        }

        RpClump* clump = loadModel(file);
        if (clump == NULL && tex != NULL && iHostStrCaseCmp(file, slotOwnFile(slot)) != 0)
        {
            clump = extrudeGlyph(file, tex);
        }

        GlyphModel* m = &sModel[sModelCount++];
        snprintf(m->name, sizeof(m->name), "%s", file);
        m->clump = clump;
        m->atomic = NULL;
        if (clump != NULL)
        {
            RpClumpForAllAtomics(clump, firstAtomic, &m->atomic);
        }

        return (m->atomic != NULL) ? m->atomic : disc;
    }
} // namespace

void iPadGlyphModelBegin(xModelInstance* model, S32 talk)
{
    sModelDisc = NULL;
    if (!talk || !sEnabled || model == NULL || model->Data == NULL || !ensureSet())
    {
        return;
    }

    RpAtomic* atomic = talkModel(model->Data);
    if (atomic != model->Data)
    {
        sModelDisc = model->Data;
        model->Data = atomic;
    }
}

void iPadGlyphModelEnd(xModelInstance* model)
{
    if (sModelDisc != NULL && model != NULL)
    {
        model->Data = sModelDisc;
    }
    sModelDisc = NULL;
}

RwTexture* iPadGlyphTexture(U32 textureID, RwTexture* disc)
{
    if (!sEnabled || textureID == 0)
    {
        return disc;
    }

    static U32 sSlotIDs[kSlotCount];
    if (sSlotIDs[0] == 0)
    {
        for (S32 i = 0; i < kSlotCount; i++)
        {
            sSlotIDs[i] = xStrHash(kSlots[i].name);
        }
    }

    const Slot* slot = NULL;
    for (S32 i = 0; i < kSlotCount; i++)
    {
        if (sSlotIDs[i] == textureID)
        {
            slot = &kSlots[i];
            break;
        }
    }
    if (slot == NULL)
    {
        return disc;
    }

    if (!ensureSet())
    {
        return disc;
    }

    const char* file;
    RwTexture* glyph = slotGlyph(slot, &file);
    return (glyph != NULL) ? glyph : disc;
}

void iPadGlyphSetEnabled(S32 on)
{
    if (!on)
    {
        dropGlyphs();
    }
    sEnabled = on ? 1 : 0;
}

S32 iPadGlyphEnabled()
{
    return sEnabled;
}

void iPadGlyphSetChoice(const char* name)
{
    snprintf(sChoice, sizeof(sChoice), "%s", (name != NULL && name[0] != '\0') ? name : "auto");
    dropGlyphs();
    sComplained[0] = '\0';
}

void iPadGlyphSetRoot(const char* dir)
{
    snprintf(sRoot, sizeof(sRoot), "%s", (dir != NULL) ? dir : ".");
    dropGlyphs();
}

const char* iPadGlyphActiveSet()
{
    return (sLoaded && sActive[0] != '\0') ? sActive : NULL;
}

void iPadGlyphExit()
{
    dropGlyphs();
    sActive[0] = '\0';
}

RwTexture* iPadGlyphFor(const char* name, U32 nameLen)
{
    if (!sEnabled || name == NULL || nameLen == 0 || nameLen >= 64)
    {
        return NULL;
    }

    // xFont hands over a slice of the markup rather than a string, and this is
    // called for every {tex:} in the game -- most of which are not prompts --
    // so the copy happens before the table walk and nothing else does.
    char slot[64];
    memcpy(slot, name, nameLen);
    slot[nameLen] = '\0';

    const Slot* found = findSlot(slot);
    if (found == NULL || !ensureSet())
    {
        return NULL;
    }

    const char* file;
    return slotGlyph(found, &file);
}
