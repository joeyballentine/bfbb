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

RwTexDictionary* RwTexDictionaryStreamRead(RwStream* stream)
{
    if (stream == NULL)
    {
        return NULL;
    }

    // Both sides expect the rwID_TEXDICTIONARY chunk header to have been eaten
    // already -- zAssetTypes.cpp calls RwStreamFindChunk first -- and pick up
    // at the STRUCT chunk inside it.
    return reinterpret_cast<RwTexDictionary*>(rw::TexDictionary::streamRead(stream));
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
