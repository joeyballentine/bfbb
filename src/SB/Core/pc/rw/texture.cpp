// RenderWare C API: RwTexture and RwTexDictionary.
//
// Both types are mirrored onto librw's (see include/rwsdk/rwcore.h and
// layout_stream.cpp), so an RwTexture* IS an rw::Texture* and these are casts
// and calls. The dictionary needed no reordering at all -- RenderWare and
// librw already lay it out identically.

#include <rwcore.h>

#include "stream.h" // brings in librw's rw.h, which must not be included twice

static inline rw::Texture* asTexture(RwTexture* t)
{
    return reinterpret_cast<rw::Texture*>(t);
}

static inline RwTexture* asRwTexture(rw::Texture* t)
{
    return reinterpret_cast<RwTexture*>(t);
}

static inline rw::TexDictionary* asTexDictionary(const RwTexDictionary* d)
{
    return const_cast<rw::TexDictionary*>(reinterpret_cast<const rw::TexDictionary*>(d));
}

// Hands each texture's raster to librw's platform conversion and puts back
// whatever comes out. convertTexToCurrentPlatform DESTROYS the old raster when
// it converts and returns the same pointer when no conversion is needed, so the
// result has to be stored rather than assumed either way.
static rw::Texture* convertRasterToPlatform(rw::Texture* texture, void* data)
{
    (void)data;

    if (texture != NULL && texture->raster != NULL)
    {
        texture->raster = rw::Raster::convertTexToCurrentPlatform(texture->raster);
    }

    return texture;
}

RwTexDictionary* RwTexDictionaryStreamRead(RwStream* stream)
{
    if (stream == NULL)
    {
        return NULL;
    }

    // Both sides expect the rwID_TEXDICTIONARY chunk header to have been eaten
    // already -- zAssetTypes.cpp calls RwStreamFindChunk first -- and pick up
    // at the STRUCT chunk inside it.
    rw::TexDictionary* dict = rw::TexDictionary::streamRead(stream);
    if (dict == NULL)
    {
        return NULL;
    }

    // **The conversion librw will not do for you.**
    //
    // A TXD holds NATIVE rasters -- BFBB's are Xbox ones, because these are the
    // Xbox game's assets -- and TexDictionary::streamRead gives back exactly
    // that: a raster whose `platform` is PLATFORM_XBOX, holding swizzled or DXT
    // Xbox pixel data that the D3D9 device cannot bind.
    //
    // librw has the conversion (Raster::convertTexToCurrentPlatform, with real
    // xbox_to_d3d and xbox_to_gl3 paths behind it) and **calls it from nowhere
    // at all**: grep the whole library and the only hits are its definition and
    // its declaration. It is the application's job, and RenderWare's own
    // RwTexDictionaryStreamRead did it as part of reading, because on a console
    // the native format WAS the device's format and no step was visible.
    //
    // Without this every texture in the game reads successfully and stays
    // blank, which is precisely what the asset test reported for a whole day:
    // "read ok, 1 textures; 128x128 8-bit raster=EMPTY". A missing conversion
    // looks exactly like a missing backend.
    // Walked by hand: librw's TexDictionary has no ForAllTextures of its own,
    // only the LinkList. The `next` link is read before the body runs, matching
    // what RwTexDictionaryForAllTextures below does, so that this stays correct
    // if a conversion ever has to replace a texture rather than its raster.
    rw::LinkList& textures = dict->textures;
    for (rw::LLLink* cur = textures.link.next; cur != textures.end();)
    {
        rw::Texture* texture = rw::Texture::fromDict(cur);
        rw::LLLink* next = cur->next;

        convertRasterToPlatform(texture, NULL);

        cur = next;
    }

    return reinterpret_cast<RwTexDictionary*>(dict);
}

RwBool RwTexDictionaryDestroy(RwTexDictionary* dict)
{
    if (dict == NULL)
    {
        return FALSE;
    }

    // Takes the textures with it, as RenderWare's does. RWTX_Read in
    // zAssetTypes.cpp relies on that: it removes the one texture it wants from
    // the dictionary first, precisely so this call does not free it too.
    asTexDictionary(dict)->destroy();
    return TRUE;
}

const RwTexDictionary* RwTexDictionaryForAllTextures(const RwTexDictionary* dict,
                                                     RwTextureCallBack fpCallBack, void* pData)
{
    if (dict == NULL || fpCallBack == NULL)
    {
        return dict;
    }

    rw::TexDictionary* d = asTexDictionary(dict);

    // FORLIST reads each link's successor before handing the entry over, so a
    // callback that removes the texture it was given does not derail the walk.
    FORLIST(link, d->textures)
    {
        if (fpCallBack(asRwTexture(rw::Texture::fromDict(link)), pData) == NULL)
        {
            // RenderWare stops early when the callback returns NULL.
            break;
        }
    }

    return dict;
}

RwTexture* RwTexDictionaryRemoveTexture(RwTexture* texture)
{
    if (texture == NULL)
    {
        return NULL;
    }

    rw::Texture* t = asTexture(texture);
    if (t->dict == NULL)
    {
        // Not in a dictionary, so there is nothing to remove it from. librw's
        // TexDictionary::remove asserts on this rather than tolerating it.
        return NULL;
    }

    t->dict->remove(t);
    return texture;
}

RwTexture* RwTextureCreate(RwRaster* raster)
{
    return asRwTexture(rw::Texture::create(reinterpret_cast<rw::Raster*>(raster)));
}

RwBool RwTextureDestroy(RwTexture* texture)
{
    if (texture == NULL)
    {
        return FALSE;
    }

    // Reference counted on both sides: this drops one reference and only frees
    // the texture, and its raster, when the last one goes. That is why
    // zAssetTypes.cpp can RwTextureAddRef a texture it pulled out of a
    // dictionary and then destroy the dictionary.
    asTexture(texture)->destroy();
    return TRUE;
}

// The three global texture settings RenderWareInit makes, all of which librw
// has an exact counterpart for.

// The hook the asset system installs so that a texture named inside a model's
// material list is resolved out of the game's own asset store rather than off a
// filesystem. iSystem.cpp's TextureRead does exactly that with xSTFindAsset,
// and without this every textured model would come back untextured.
//
// librw declares readCB with the same two arguments in the same order, so the
// callback the game supplies can be stored directly.
RwBool RwTextureSetReadCallBack(RwTextureCallBackRead callBack)
{
    rw::Texture::readCB = reinterpret_cast<rw::Texture* (*)(const char*, const char*)>(callBack);
    return TRUE;
}

RwBool RwTextureSetMipmapping(RwBool enable)
{
    rw::Texture::setMipmapping(enable != FALSE);
    return TRUE;
}

RwBool RwTextureSetAutoMipmapping(RwBool enable)
{
    rw::Texture::setAutoMipmapping(enable != FALSE);
    return TRUE;
}
