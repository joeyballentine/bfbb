// Layout assertions for the asset-reading path: rasters, textures, texture
// dictionaries and images.
//
// Same job as layout.cpp -- no code, just the claims the reinterpret_casts in
// raster.cpp, texture.cpp and image.cpp rest on, checked by the compiler.
// Split into its own file only so that the two groups can be worked on
// independently; read layout.cpp's header comment for why any of this exists.
//
// The offsets are librw's, taken from a throwaway program that printed
// offsetof for every member rather than read off rwobjects.h, because the
// header is full of macros and static members that make eyeballing it
// unreliable.
//
// RwStream is deliberately absent: it is not mirrored. RenderWare's stream is
// a POD tagged union and librw's is an abstract class, so the port leaves the
// type incomplete and defines its own in stream.h. There is no layout claim to
// check, which is exactly why there are no assertions for it.

#include <rwcore.h>

#include "rw.h"

#include <stddef.h>

#define SAME_SIZE(ours, theirs)                                                                    \
    static_assert(sizeof(ours) == sizeof(theirs), #ours " and " #theirs " differ in size")

#define SAME_OFFSET(ours, ourfield, theirs, theirfield)                                            \
    static_assert(offsetof(ours, ourfield) == offsetof(theirs, theirfield),                        \
                  #ours "." #ourfield " is not where " #theirs "." #theirfield " is")

// --- RwRaster --------------------------------------------------------------
//
// The heaviest reorder of the four. RenderWare leads with parent and the pixel
// pointers and packs the four descriptors into bytes; librw leads with the
// descriptors as int32s and puts parent second to last. `platform` has no
// RenderWare counterpart at all, so it is only checked for position.

SAME_SIZE(RwRaster, rw::Raster);
SAME_OFFSET(RwRaster, platform, rw::Raster, platform);
SAME_OFFSET(RwRaster, cType, rw::Raster, type);
SAME_OFFSET(RwRaster, cFlags, rw::Raster, flags);
SAME_OFFSET(RwRaster, privateFlags, rw::Raster, privateFlags);
SAME_OFFSET(RwRaster, cFormat, rw::Raster, format);
SAME_OFFSET(RwRaster, width, rw::Raster, width);
SAME_OFFSET(RwRaster, height, rw::Raster, height);
SAME_OFFSET(RwRaster, depth, rw::Raster, depth);
SAME_OFFSET(RwRaster, stride, rw::Raster, stride);
SAME_OFFSET(RwRaster, cpPixels, rw::Raster, pixels);
SAME_OFFSET(RwRaster, palette, rw::Raster, palette);
SAME_OFFSET(RwRaster, originalPixels, rw::Raster, originalPixels);
SAME_OFFSET(RwRaster, originalWidth, rw::Raster, originalWidth);
SAME_OFFSET(RwRaster, originalHeight, rw::Raster, originalHeight);
SAME_OFFSET(RwRaster, originalStride, rw::Raster, originalStride);
SAME_OFFSET(RwRaster, parent, rw::Raster, parent);
SAME_OFFSET(RwRaster, nOffsetX, rw::Raster, offsetX);
SAME_OFFSET(RwRaster, nOffsetY, rw::Raster, offsetY);

// The widened descriptors carry the same values, so their widths matter as
// much as their offsets: a byte read of librw's little-endian int32 would
// happen to work on x86 and silently break the moment anything reads
// cFormat's high byte, which RwRasterGetFormat does.
static_assert(sizeof(((RwRaster*)0)->cType) == sizeof(((rw::Raster*)0)->type),
              "RwRaster.cType is not as wide as rw::Raster.type");
static_assert(sizeof(((RwRaster*)0)->cFormat) == sizeof(((rw::Raster*)0)->format),
              "RwRaster.cFormat is not as wide as rw::Raster.format");
static_assert(sizeof(((RwRaster*)0)->nOffsetX) == sizeof(((rw::Raster*)0)->offsetX),
              "RwRaster.nOffsetX is not as wide as rw::Raster.offsetX");

// RwRasterCreate passes RenderWare's flags word straight to librw, which
// splits it exactly the way RenderWare's driver does.
static_assert((int)rwRASTERTYPENORMAL == (int)rw::Raster::NORMAL, "raster type NORMAL differs");
static_assert((int)rwRASTERTYPEZBUFFER == (int)rw::Raster::ZBUFFER, "raster type ZBUFFER differs");
static_assert((int)rwRASTERTYPECAMERA == (int)rw::Raster::CAMERA, "raster type CAMERA differs");
static_assert((int)rwRASTERTYPETEXTURE == (int)rw::Raster::TEXTURE, "raster type TEXTURE differs");
static_assert((int)rwRASTERDONTALLOCATE == (int)rw::Raster::DONTALLOCATE,
              "raster DONTALLOCATE differs");
static_assert((int)rwRASTERFORMAT8888 == (int)rw::Raster::C8888, "raster format 8888 differs");
static_assert((int)rwRASTERFORMAT1555 == (int)rw::Raster::C1555, "raster format 1555 differs");
static_assert((int)rwRASTERFORMATPAL8 == (int)rw::Raster::PAL8, "raster format PAL8 differs");
static_assert((int)rwRASTERFORMATMIPMAP == (int)rw::Raster::MIPMAP, "raster MIPMAP differs");

// --- RwTexture -------------------------------------------------------------
//
// Identical up to refCount; librw then appends a link into its global texture
// list, which RenderWare has no counterpart for.

SAME_SIZE(RwTexture, rw::Texture);
SAME_OFFSET(RwTexture, raster, rw::Texture, raster);
SAME_OFFSET(RwTexture, dict, rw::Texture, dict);
SAME_OFFSET(RwTexture, lInDictionary, rw::Texture, inDict);
SAME_OFFSET(RwTexture, name, rw::Texture, name);
SAME_OFFSET(RwTexture, mask, rw::Texture, mask);
SAME_OFFSET(RwTexture, filterAddressing, rw::Texture, filterAddressing);
SAME_OFFSET(RwTexture, refCount, rw::Texture, refCount);
SAME_OFFSET(RwTexture, lInGlobalList, rw::Texture, inGlobalList);

static_assert(sizeof(((RwTexture*)0)->name) == sizeof(((rw::Texture*)0)->name),
              "texture name is not 32 bytes on both sides");
static_assert(sizeof(((RwTexture*)0)->mask) == sizeof(((rw::Texture*)0)->mask),
              "texture mask name is not 32 bytes on both sides");

// The filter/addressing word is packed in place by the RwTextureSet*/Get*
// macros in rwcore.h, so the two sides have to agree on what the bits mean,
// not just on where the word is. RenderWare's enums start at 1 like librw's.
static_assert((int)rwFILTERNEAREST == (int)rw::Texture::NEAREST, "filter NEAREST differs");
static_assert((int)rwFILTERLINEAR == (int)rw::Texture::LINEAR, "filter LINEAR differs");
static_assert((int)rwFILTERLINEARMIPLINEAR == (int)rw::Texture::LINEARMIPLINEAR,
              "filter LINEARMIPLINEAR differs");
static_assert((int)rwTEXTUREADDRESSWRAP == (int)rw::Texture::WRAP, "addressing WRAP differs");
static_assert((int)rwTEXTUREADDRESSCLAMP == (int)rw::Texture::CLAMP, "addressing CLAMP differs");
static_assert(rwTEXTUREFILTERMODEMASK == 0xFF, "filter mode is not librw's low byte");
static_assert(rwTEXTUREADDRESSINGUMASK == 0xF00, "U addressing is not librw's second nibble");
static_assert(rwTEXTUREADDRESSINGVMASK == 0xF000, "V addressing is not librw's third nibble");

// --- RwTexDictionary -------------------------------------------------------
//
// Already identical, hence no PC variant of the struct. This is the assertion
// that keeps "already identical" from being a comment nobody rechecks.

SAME_SIZE(RwTexDictionary, rw::TexDictionary);
SAME_OFFSET(RwTexDictionary, object, rw::TexDictionary, object);
SAME_OFFSET(RwTexDictionary, texturesInDict, rw::TexDictionary, textures);
SAME_OFFSET(RwTexDictionary, lInInstance, rw::TexDictionary, inGlobalList);

// --- RwImage ---------------------------------------------------------------

SAME_SIZE(RwImage, rw::Image);
SAME_OFFSET(RwImage, flags, rw::Image, flags);
SAME_OFFSET(RwImage, width, rw::Image, width);
SAME_OFFSET(RwImage, height, rw::Image, height);
SAME_OFFSET(RwImage, depth, rw::Image, depth);
SAME_OFFSET(RwImage, bpp, rw::Image, bpp);
SAME_OFFSET(RwImage, stride, rw::Image, stride);
SAME_OFFSET(RwImage, cpPixels, rw::Image, pixels);
SAME_OFFSET(RwImage, palette, rw::Image, palette);

// RwImage's palette is typed RwRGBA* and librw's uint8*, which is fine only
// because RwRGBA is the four bytes librw indexes by hand.
static_assert(sizeof(RwRGBA) == 4, "RwRGBA is not four bytes");
