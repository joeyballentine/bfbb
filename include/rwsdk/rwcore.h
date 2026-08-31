#ifndef RWCORE_H
#define RWCORE_H

#include <rwsdk/rwplcore.h>

/* C compatibility: these headers use bare tag names as types. */
typedef struct rxHeapFreeBlock rxHeapFreeBlock;
typedef struct rxHeapSuperBlockDescriptor rxHeapSuperBlockDescriptor;
typedef struct RxHeap RxHeap;
typedef struct rxHeapBlockHeader rxHeapBlockHeader;
typedef struct RxClusterDefinition RxClusterDefinition;
typedef struct RxOutputSpec RxOutputSpec;
typedef struct RxClusterRef RxClusterRef;
typedef struct RxIoSpec RxIoSpec;
typedef struct RxNodeMethods RxNodeMethods;
typedef struct RxNodeDefinition RxNodeDefinition;
typedef struct RxPipelineCluster RxPipelineCluster;
typedef struct RxCluster RxCluster;
typedef struct RxPacket RxPacket;
typedef struct RxPipelineNode RxPipelineNode;
typedef struct RxPipelineNodeTopSortData RxPipelineNodeTopSortData;
typedef struct RxPipelineNodeParam RxPipelineNodeParam;
typedef struct RxPipelineRequiresCluster RxPipelineRequiresCluster;
typedef struct RxPipeline RxPipeline;
typedef struct RwRaster RwRaster;
typedef struct RxRenderStateVector RxRenderStateVector;
typedef struct RwImage RwImage;
typedef struct RwTexDictionary RwTexDictionary;
typedef struct RwTexture RwTexture;
typedef struct RwFrame RwFrame;
typedef struct RwObjectHasFrame RwObjectHasFrame;
typedef struct rwFrameList rwFrameList;
typedef struct RwBBox RwBBox;
typedef struct RwFrustumPlane RwFrustumPlane;
typedef struct RwCamera RwCamera;


typedef struct RxObjSpace3DVertex RxObjSpace3DVertex;
struct RxObjSpace3DVertex
{
    RwReal x;
    RwReal y;
    RwReal z;
    RwReal nx;
    RwReal ny;
    RwReal nz;
    // The colour bytes are in the order the RENDERER reads them, which is not
    // the same on every backend.
    //
    // librw's d3d Im3DVertex has a packed `uint32 color` where these four bytes
    // are, and it is a D3DCOLOR -- ARGB in a word, so on a little-endian host
    // the lowest byte is BLUE. Declaring r first put red where D3D reads blue
    // and swapped the two channels on every Im3D vertex the game drew.
    //
    // It hid because nearly every Im3D caller passes a greyscale colour, where
    // r == b and the swap is invisible: xShadowSimple draws (0,0,0) and
    // xLaserBolt (255,255,255). The coloured ones are the melee streaks, and
    // SpongeBob's pale yellow (255,255,128) came out as (128,255,255).
    //
    // Keyed on the BACKEND and not on PLATFORM_PC, which is what it used to
    // say. librw's gl3 Im3DVertex holds four separate bytes in RGBA order and
    // feeds them to glVertexAttribPointer as GL_UNSIGNED_BYTE x4, so a GL3
    // build wants exactly the console's order and taking D3D's would put the
    // same swap back -- in the build where the two channels were never crossed
    // in the first place. The Im2D vertex in rwplcore.h has been keyed this way
    // since it was written; this one was not, because there was only ever one
    // PC backend to be wrong about.
    //
    // The field NAMES stay put, so every RwIm3DVertexSetRGBA call still means
    // what it says; only the bytes move. The GameCube keeps RenderWare's own
    // order, where GX reads the colour as RGBA.
#if defined(RW_D3D9) || defined(RW_D3D8)
    RwUInt8 b;
    RwUInt8 g;
    RwUInt8 r;
    RwUInt8 a;
#else
    RwUInt8 r;
    RwUInt8 g;
    RwUInt8 b;
    RwUInt8 a;
#endif
    RwReal u;
    RwReal v;
};

typedef RxObjSpace3DVertex RxObjSpace3DLitVertex;
typedef RxObjSpace3DLitVertex RwIm3DVertex;

#define RwIm3DVertexSetPos(_vert, _imx, _imy, _imz)                                               \
MACRO_START                                                                                       \
{                                                                                                 \
    (_vert)->x = _imx;                                                                            \
    (_vert)->y = _imy;                                                                            \
    (_vert)->z = _imz;                                                                            \
}                                                                                                 \
MACRO_STOP

#define RwIm3DVertexSetNormal(_vert, _imx, _imy, _imz)                                            \
MACRO_START                                                                                       \
{                                                                                                 \
    (_vert)->nx = _imx;                                                                           \
    (_vert)->ny = _imy;                                                                           \
    (_vert)->nz = _imz;                                                                           \
}                                                                                                 \
MACRO_STOP

#define RwIm3DVertexSetRGBA(_vert, _r, _g, _b, _a)                                                \
MACRO_START                                                                                       \
{                                                                                                 \
    (_vert)->r = _r;                                                                              \
    (_vert)->g = _g;                                                                              \
    (_vert)->b = _b;                                                                              \
    (_vert)->a = _a;                                                                              \
}                                                                                                 \
MACRO_STOP

#define RwIm3DVertexSetUV(_vert, _u, _v)                                                          \
MACRO_START                                                                                       \
{                                                                                                 \
    (_vert)->u = _u;                                                                              \
    (_vert)->v = _v;                                                                              \
}                                                                                                 \
MACRO_STOP

enum RwIm3DTransformFlags
{
    rwIM3D_VERTEXUV = 1,
    rwIM3D_ALLOPAQUE = 2,
    rwIM3D_NOCLIP = 4,
    rwIM3D_VERTEXXYZ = 8,
    rwIM3D_VERTEXRGBA = 16,
    rwIM3DTRANSFORMFLAGSFORCEENUMSIZEINT = RWFORCEENUMSIZEINT
};
typedef enum RwIm3DTransformFlags RwIm3DTransformFlags;

/* rxHeapFreeBlock is typedef'd above */
/* rxHeapSuperBlockDescriptor is typedef'd above */
/* RxHeap is typedef'd above */
/* rxHeapBlockHeader is typedef'd above */
struct rxHeapFreeBlock
{
    RwUInt32 size;
    rxHeapBlockHeader* ptr;
};

struct rxHeapSuperBlockDescriptor
{
    void* start;
    RwUInt32 size;
    rxHeapSuperBlockDescriptor* next;
};

struct RxHeap
{
    RwUInt32 superBlockSize;
    rxHeapSuperBlockDescriptor* head;
    rxHeapBlockHeader* headBlock;
    rxHeapFreeBlock* freeBlocks;
    RwUInt32 entriesAlloced;
    RwUInt32 entriesUsed;
    RwBool dirty;
};

struct rxHeapBlockHeader
{
    rxHeapBlockHeader *prev, *next;
    RwUInt32 size;
    rxHeapFreeBlock* freeEntry;
    RwUInt32 pad[4];
};

enum RxClusterValidityReq
{
    rxCLREQ_DONTWANT = 0,
    rxCLREQ_REQUIRED = 1,
    rxCLREQ_OPTIONAL = 2,
    rxCLUSTERVALIDITYREQFORCEENUMSIZEINT = RWFORCEENUMSIZEINT
};
typedef enum RxClusterValidityReq RxClusterValidityReq;

enum RxClusterValid
{
    rxCLVALID_NOCHANGE = 0,
    rxCLVALID_VALID = 1,
    rxCLVALID_INVALID = 2,
    rxCLUSTERVALIDFORCEENUMSIZEINT = RWFORCEENUMSIZEINT
};
typedef enum RxClusterValid RxClusterValid;

/* RxPipelineNode is typedef'd above */
/* RxNodeDefinition is typedef'd above */
/* RxPipeline is typedef'd above */
/* RxPipelineNodeParam is typedef'd above */
/* RxPipelineNodeTopSortData is typedef'd above */
struct RxClusterDefinition
{
    RwChar* name;
    RwUInt32 defaultStride;
    RwUInt32 defaultAttributes;
    const RwChar* attributeSet;
};

struct RxOutputSpec
{
    RwChar* name;
    RxClusterValid* outputClusters;
    RxClusterValid allOtherClusters;
};

enum RxClusterForcePresent
{
    rxCLALLOWABSENT = FALSE,
    rxCLFORCEPRESENT = TRUE,
    rxCLUSTERFORCEPRESENTFORCEENUMSIZEINT = RWFORCEENUMSIZEINT
};
typedef enum RxClusterForcePresent RxClusterForcePresent;

struct RxClusterRef
{
    RxClusterDefinition* clusterDef;
    RxClusterForcePresent forcePresent;
    RwUInt32 reserved;
};

struct RxIoSpec
{
    RwUInt32 numClustersOfInterest;
    RxClusterRef* clustersOfInterest;
    RxClusterValidityReq* inputRequirements;
    RwUInt32 numOutputs;
    RxOutputSpec* outputs;
};

typedef RwBool (*RxNodeBodyFn)(RxPipelineNode* self, const RxPipelineNodeParam* params);
typedef RwBool (*RxNodeInitFn)(RxNodeDefinition* self);
typedef void (*RxNodeTermFn)(RxNodeDefinition* self);
typedef RwBool (*RxPipelineNodeInitFn)(RxPipelineNode* self);
typedef void (*RxPipelineNodeTermFn)(RxPipelineNode* self);
typedef RwBool (*RxPipelineNodeConfigFn)(RxPipelineNode* self, RxPipeline* pipeline);
typedef RwUInt32 (*RxConfigMsgHandlerFn)(RxPipelineNode* self, RwUInt32 msg, RwUInt32 intparam,
                                         void* ptrparam);

struct RxNodeMethods
{
    RxNodeBodyFn nodeBody;
    RxNodeInitFn nodeInit;
    RxNodeTermFn nodeTerm;
    RxPipelineNodeInitFn pipelineNodeInit;
    RxPipelineNodeTermFn pipelineNodeTerm;
    RxPipelineNodeConfigFn pipelineNodeConfig;
    RxConfigMsgHandlerFn configMsgHandler;
};

enum RxNodeDefEditable
{
    rxNODEDEFCONST = FALSE,
    rxNODEDEFEDITABLE = TRUE,
    rxNODEDEFEDITABLEFORCEENUMSIZEINT = RWFORCEENUMSIZEINT
};
typedef enum RxNodeDefEditable RxNodeDefEditable;

struct RxNodeDefinition
{
    RwChar* name;
    RxNodeMethods nodeMethods;
    RxIoSpec io;
    RwUInt32 pipelineNodePrivateDataSize;
    RxNodeDefEditable editable;
    RwInt32 InputPipesCnt;
};

struct RxPipelineCluster
{
    RxClusterDefinition* clusterRef;
    RwUInt32 creationAttributes;
};

struct RxCluster
{
    RwUInt16 flags;
    RwUInt16 stride;
    void* data;
    void* currentData;
    RwUInt32 numAlloced;
    RwUInt32 numUsed;
    RxPipelineCluster* clusterRef;
    RwUInt32 attributes;
};

struct RxPacket
{
    RwUInt16 flags;
    RwUInt16 numClusters;
    RxPipeline* pipeline;
    RwUInt32* inputToClusterSlot;
    RwUInt32* slotsContinue;
    RxPipelineCluster** slotClusterRefs;
    RxCluster clusters[1];
};

struct RxPipelineNode
{
    RxNodeDefinition* nodeDef;
    RwUInt32 numOutputs;
    RwUInt32* outputs;
    RxPipelineCluster** slotClusterRefs;
    RwUInt32* slotsContinue;
    void* privateData;
    RwUInt32* inputToClusterSlot;
    RxPipelineNodeTopSortData* topSortData;
    void* initializationData;
    RwUInt32 initializationDataSize;
};

typedef struct rxReq rxReq;
struct RxPipelineNodeTopSortData
{
    RwUInt32 numIns;
    RwUInt32 numInsVisited;
    rxReq* req;
};

struct RxPipelineNodeParam
{
    void* dataParam;
    RxHeap* heap;
};

enum rxEmbeddedPacketState
{
    rxPKST_PACKETLESS = 0,
    rxPKST_UNUSED = 1,
    rxPKST_INUSE = 2,
    rxPKST_PENDING = 3,
    rxEMBEDDEDPACKETSTATEFORCEENUMSIZEINT = RWFORCEENUMSIZEINT
};
typedef enum rxEmbeddedPacketState rxEmbeddedPacketState;

struct RxPipelineRequiresCluster
{
    RxClusterDefinition* clusterDef;
    RxClusterValidityReq rqdOrOpt;
    RwUInt32 slotIndex;
};

struct RxPipeline
{
    RwBool locked;
    RwUInt32 numNodes;
    RxPipelineNode* nodes;
    RwUInt32 packetNumClusterSlots;
    rxEmbeddedPacketState embeddedPacketState;
    RxPacket* embeddedPacket;
    RwUInt32 numInputRequirements;
    RxPipelineRequiresCluster* inputRequirements;
    void* superBlock;
    RwUInt32 superBlockSize;
    RwUInt32 entryPoint;
    RwUInt32 pluginId;
    RwUInt32 pluginData;
};

typedef RwUInt32* RxNodeOutput;
typedef RxPipelineNode* RxNodeInput;
typedef RxPipeline RxLockedPipe;

enum RwRasterLockMode
{
    rwRASTERLOCKWRITE = 0x01,
    rwRASTERLOCKREAD = 0x02,
    rwRASTERLOCKNOFETCH = 0x04,
    rwRASTERLOCKRAW = 0x08,
    rwRASTERLOCKMODEFORCEENUMSIZEINT = RWFORCEENUMSIZEINT
};
typedef enum RwRasterLockMode RwRasterLockMode;

#define rwRASTERLOCKREADWRITE (rwRASTERLOCKREAD | rwRASTERLOCKWRITE)

enum RwRasterFlipMode
{
    rwRASTERFLIPDONTWAIT = 0,
    rwRASTERFLIPWAITVSYNC = 1,
    rwRASTERFLIPMODEFORCEENUMSIZEINT = RWFORCEENUMSIZEINT
};
typedef enum RwRasterFlipMode RwRasterFlipMode;

enum RwRasterType
{
    rwRASTERTYPENORMAL = 0x00,
    rwRASTERTYPEZBUFFER = 0x01,
    rwRASTERTYPECAMERA = 0x02,
    rwRASTERTYPETEXTURE = 0x04,
    rwRASTERTYPECAMERATEXTURE = 0x05,
    rwRASTERTYPEMASK = 0x07,
    rwRASTERDONTALLOCATE = 0x80,
    rwRASTERTYPEFORCEENUMSIZEINT = RWFORCEENUMSIZEINT
};
typedef enum RwRasterType RwRasterType;

enum RwRasterFormat
{
    rwRASTERFORMATDEFAULT = 0x0000,
    rwRASTERFORMAT1555 = 0x0100,
    rwRASTERFORMAT565 = 0x0200,
    rwRASTERFORMAT4444 = 0x0300,
    rwRASTERFORMATLUM8 = 0x0400,
    rwRASTERFORMAT8888 = 0x0500,
    rwRASTERFORMAT888 = 0x0600,
    rwRASTERFORMAT16 = 0x0700,
    rwRASTERFORMAT24 = 0x0800,
    rwRASTERFORMAT32 = 0x0900,
    rwRASTERFORMAT555 = 0x0a00,
    rwRASTERFORMATAUTOMIPMAP = 0x1000,
    rwRASTERFORMATPAL8 = 0x2000,
    rwRASTERFORMATPAL4 = 0x4000,
    rwRASTERFORMATMIPMAP = 0x8000,
    rwRASTERFORMATPIXELFORMATMASK = 0x0f00,
    rwRASTERFORMATMASK = 0xff00,
    rwRASTERFORMATFORCEENUMSIZEINT = RWFORCEENUMSIZEINT
};
typedef enum RwRasterFormat RwRasterFormat;

enum RwRasterPrivateFlag
{
    rwRASTERGAMMACORRECTED = 0x01,
    rwRASTERPIXELLOCKEDREAD = 0x02,
    rwRASTERPIXELLOCKEDWRITE = 0x04,
    rwRASTERPALETTELOCKEDREAD = 0x08,
    rwRASTERPALETTELOCKEDWRITE = 0x10,
    rwRASTERPIXELLOCKEDRAW = 0x20,
    rwRASTERPRIVATEFLAGFORCEENUMSIZEINT = RWFORCEENUMSIZEINT
};
typedef enum RwRasterPrivateFlag RwRasterPrivateFlag;

#define rwRASTERPIXELLOCKED (rwRASTERPIXELLOCKEDREAD | rwRASTERPIXELLOCKEDWRITE)
#define rwRASTERPALETTELOCKED (rwRASTERPALETTELOCKEDREAD | rwRASTERPALETTELOCKEDWRITE)
#define rwRASTERLOCKED (rwRASTERPIXELLOCKED | rwRASTERPALETTELOCKED)

// Mirrored onto rw::Raster: librw's field ORDER under RenderWare's field
// NAMES, so an RwRaster* IS an rw::Raster*. See the RwFrame comment further
// down and src/SB/Core/pc/rw/layout_stream.cpp, which asserts every offset.
//
// Two differences from RenderWare beyond the reordering. librw widens the four
// byte-sized descriptors (cType, cFlags, privateFlags, cFormat) and the two
// sub-raster offsets to RwInt32, and it stores cFormat unshifted -- the whole
// rwRASTERFORMAT* value rather than RenderWare's value>>8 -- so
// RwRasterGetFormat is spelled differently below. And it carries a `platform`
// field RenderWare has no counterpart for, naming the driver that owns the
// pixels; it is librw's, not ours, and nothing in the game should read it.
#ifndef PLATFORM_PC
struct RwRaster
{
    RwRaster* parent;
    RwUInt8* cpPixels;
    RwUInt8* palette;
    RwInt32 width, height, depth;
    RwInt32 stride;
    RwInt16 nOffsetX, nOffsetY;
    RwUInt8 cType;
    RwUInt8 cFlags;
    RwUInt8 privateFlags;
    RwUInt8 cFormat;
    RwUInt8* originalPixels;
    RwInt32 originalWidth;
    RwInt32 originalHeight;
    RwInt32 originalStride;
};
#else
struct RwRaster
{
    RwInt32 platform; // librw only: which driver owns the pixels
    RwInt32 cType;
    RwInt32 cFlags;
    RwInt32 privateFlags;
    RwInt32 cFormat; // librw calls this 'format', and does NOT shift it down
    RwInt32 width, height, depth;
    RwInt32 stride;
    RwUInt8* cpPixels; // librw calls this 'pixels'
    RwUInt8* palette;
    RwUInt8* originalPixels;
    RwInt32 originalWidth;
    RwInt32 originalHeight;
    RwInt32 originalStride;
    RwRaster* parent;
    RwInt32 nOffsetX, nOffsetY;
};
#endif

#define RwRasterGetWidth(_raster) ((_raster)->width)

#define RwRasterGetHeight(_raster) ((_raster)->height)

#define RwRasterGetStride(_raster) ((_raster)->stride)

#define RwRasterGetDepth(_raster) ((_raster)->depth)

#ifndef PLATFORM_PC
#define RwRasterGetFormat(_raster) ((((_raster)->cFormat) & (rwRASTERFORMATMASK >> 8)) << 8)
#else
#define RwRasterGetFormat(_raster) (((_raster)->cFormat) & rwRASTERFORMATMASK)
#endif

// Same expression either way: RenderWare and librw both keep the low three
// bits of the raster flags word here, unshifted.
#define RwRasterGetType(_raster) (((_raster)->cType) & rwRASTERTYPEMASK)

#define RwRasterGetParent(_raster) ((_raster)->parent)

struct RxRenderStateVector
{
    RwUInt32 Flags;
    RwShadeMode ShadeMode;
    RwBlendFunction SrcBlend;
    RwBlendFunction DestBlend;
    RwRaster* TextureRaster;
    RwTextureAddressMode AddressModeU;
    RwTextureAddressMode AddressModeV;
    RwTextureFilterMode FilterMode;
    RwRGBA BorderColor;
    RwFogType FogType;
    RwRGBA FogColor;
};

// Mirrored onto rw::Image. The only difference is the `bpp` librw caches
// between depth and stride; every other field lines up under RenderWare's own
// name. Note that `flags` does NOT mean what it means on the console: librw
// uses bit 0 and bit 1 to record whether it owns the pixels and the palette,
// where RenderWare kept rwIMAGEGAMMACORRECTED there. Nothing in the game reads
// image->flags, and the port must not start.
#ifndef PLATFORM_PC
struct RwImage
{
    RwInt32 flags;
    RwInt32 width;
    RwInt32 height;
    RwInt32 depth;
    RwInt32 stride;
    RwUInt8* cpPixels;
    RwRGBA* palette;
};
#else
struct RwImage
{
    RwInt32 flags;
    RwInt32 width;
    RwInt32 height;
    RwInt32 depth;
    RwInt32 bpp; // librw only: bytes per pixel, cached from depth
    RwInt32 stride;
    RwUInt8* cpPixels; // librw calls this 'pixels'
    RwRGBA* palette;
};
#endif

#define RwImageSetStrideMacro(_image, _stride) (((_image)->stride = (_stride)), (_image))

#define RwImageSetPixelsMacro(_image, _pixels) (((_image)->cpPixels = (_pixels)), (_image))

#define RwImageSetPaletteMacro(_image, _palette) (((_image)->palette = (_palette)), (_image))

#define RwImageGetWidthMacro(_image) ((_image)->width)

#define RwImageGetHeightMacro(_image) ((_image)->height)

#define RwImageGetDepthMacro(_image) ((_image)->depth)

#define RwImageGetStrideMacro(_image) ((_image)->stride)

#define RwImageGetPixelsMacro(_image) ((_image)->cpPixels)

#define RwImageGetPaletteMacro(_image) ((_image)->palette)

#define RwImageSetStride(_image, _stride) RwImageSetStrideMacro(_image, _stride)

#define RwImageSetPixels(_image, _pixels) RwImageSetPixelsMacro(_image, _pixels)

#define RwImageSetPalette(_image, _palette) RwImageSetPaletteMacro(_image, _palette)

#define RwImageGetWidth(_image) RwImageGetWidthMacro(_image)

#define RwImageGetHeight(_image) RwImageGetHeightMacro(_image)

#define RwImageGetDepth(_image) RwImageGetDepthMacro(_image)

#define RwImageGetStride(_image) RwImageGetStrideMacro(_image)

#define RwImageGetPixels(_image) RwImageGetPixelsMacro(_image)

#define RwImageGetPalette(_image) RwImageGetPaletteMacro(_image)

// RenderWare's and librw's texture dictionaries already agree byte for byte --
// object, then the list of textures, then the link into the global list of
// dictionaries -- so this one needs no PC variant. layout_stream.cpp asserts
// that, so it stops being taken on trust the moment librw changes.
struct RwTexDictionary
{
    RwObject object;
    RwLinkList texturesInDict;
    RwLLLink lInInstance; // librw calls this 'inGlobalList'
};

// Mirrored onto rw::Texture, which agrees with RenderWare all the way down and
// then appends a second link. Keeping the trailing member is what makes the
// sizes match, and the size is what a caller allocating a texture depends on.
#ifndef PLATFORM_PC
struct RwTexture
{
    RwRaster* raster;
    RwTexDictionary* dict;
    RwLLLink lInDictionary;
    RwChar name[32];
    RwChar mask[32];
    RwUInt32 filterAddressing;
    RwInt32 refCount;
};
#else
struct RwTexture
{
    RwRaster* raster;
    RwTexDictionary* dict;
    RwLLLink lInDictionary; // librw calls this 'inDict'
    RwChar name[32];
    RwChar mask[32];
    RwUInt32 filterAddressing;
    RwInt32 refCount;
    RwLLLink lInGlobalList; // librw only, and librw's own comment says so:
                            // "actually not in RW"
};
#endif

typedef RwTexture* (*RwTextureCallBackRead)(const RwChar* name, const RwChar* maskName);
typedef RwTexture* (*RwTextureCallBack)(RwTexture* texture, void* pData);

#define RwTextureGetRasterMacro(_tex) ((_tex)->raster)

#define RwTextureAddRefMacro(_tex) (((_tex)->refCount++), (_tex))

#define RwTextureAddRefVoidMacro(_tex)                                                             \
    MACRO_START                                                                                    \
    {                                                                                              \
        (_tex)->refCount++;                                                                        \
    }                                                                                              \
    MACRO_STOP

#define RwTextureGetNameMacro(_tex) ((_tex)->name)

#define RwTextureGetMaskNameMacro(_tex) ((_tex)->mask)

#define RwTextureGetDictionaryMacro(_tex) ((_tex)->dict)

#define RwTextureSetFilterModeMacro(_tex, _filtering)                                              \
    (((_tex)->filterAddressing = ((_tex)->filterAddressing & ~rwTEXTUREFILTERMODEMASK) |           \
                                 (((RwUInt32)(_filtering)) & rwTEXTUREFILTERMODEMASK)),            \
     (_tex))

#define RwTextureGetFilterModeMacro(_tex)                                                          \
    ((RwTextureFilterMode)((_tex)->filterAddressing & rwTEXTUREFILTERMODEMASK))

#define RwTextureSetAddressingMacro(_tex, _addressing)                                             \
    (((_tex)->filterAddressing =                                                                   \
          ((_tex)->filterAddressing & ~rwTEXTUREADDRESSINGMASK) |                                  \
          (((((RwUInt32)(_addressing)) << 8) & rwTEXTUREADDRESSINGUMASK) |                         \
           ((((RwUInt32)(_addressing)) << 12) & rwTEXTUREADDRESSINGVMASK))),                       \
     (_tex))

#define RwTextureSetAddressingUMacro(_tex, _addressing)                                            \
    (((_tex)->filterAddressing = ((_tex)->filterAddressing & ~rwTEXTUREADDRESSINGUMASK) |          \
                                 (((RwUInt32)(_addressing) << 8) & rwTEXTUREADDRESSINGUMASK)),     \
     (_tex))

#define RwTextureSetAddressingVMacro(_tex, _addressing)                                            \
    (((_tex)->filterAddressing = ((_tex)->filterAddressing & ~rwTEXTUREADDRESSINGVMASK) |          \
                                 (((RwUInt32)(_addressing) << 12) & rwTEXTUREADDRESSINGVMASK)),    \
     (_tex))

#define RwTextureGetAddressingMacro(_tex)                                                          \
    (((((_tex)->filterAddressing & rwTEXTUREADDRESSINGUMASK) >> 8) ==                              \
      (((_tex)->filterAddressing & rwTEXTUREADDRESSINGVMASK) >> 12)) ?                             \
         ((RwTextureAddressMode)(((_tex)->filterAddressing & rwTEXTUREADDRESSINGVMASK) >> 12)) :   \
         rwTEXTUREADDRESSNATEXTUREADDRESS)

#define RwTextureGetAddressingUMacro(_tex)                                                         \
    ((RwTextureAddressMode)(((_tex)->filterAddressing & rwTEXTUREADDRESSINGUMASK) >> 8))

#define RwTextureGetAddressingVMacro(_tex)                                                         \
    ((RwTextureAddressMode)(((_tex)->filterAddressing & rwTEXTUREADDRESSINGVMASK) >> 12))

#define RwTextureGetRaster(_tex) RwTextureGetRasterMacro(_tex)

#define RwTextureAddRef(_tex) RwTextureAddRefMacro(_tex)

#define RwTextureGetName(_tex) RwTextureGetNameMacro(_tex)

#define RwTextureGetMaskName(_tex) RwTextureGetMaskNameMacro(_tex)

#define RwTextureGetDictionary(_tex) RwTextureGetDictionaryMacro(_tex)

#define RwTextureSetFilterMode(_tex, _filtering) RwTextureSetFilterModeMacro(_tex, _filtering)

#define RwTextureGetFilterMode(_tex) RwTextureGetFilterModeMacro(_tex)

#define RwTextureSetAddressing(_tex, _addressing) RwTextureSetAddressingMacro(_tex, _addressing)

#define RwTextureSetAddressingU(_tex, _addressing) RwTextureSetAddressingUMacro(_tex, _addressing)

#define RwTextureSetAddressingV(_tex, _addressing) RwTextureSetAddressingVMacro(_tex, _addressing)

#define RwTextureGetAddressing(_tex) RwTextureGetAddressingMacro(_tex)

#define RwTextureGetAddressingU(_tex) RwTextureGetAddressingUMacro(_tex)

#define RwTextureGetAddressingV(_tex) RwTextureGetAddressingVMacro(_tex)

// The port mirrors librw's field ORDER while keeping RenderWare's field NAMES,
// so that an RwFrame* and an rw::Frame* are the same bytes and game code
// compiles unmodified. Nothing converts at the seam: the game holds RwFrame*
// for the lifetime of an entity and writes ->modelling directly at dozens of
// sites, while librw mutates the same objects through its own parent/child
// links, so two layouts with a copy between them could not stay in step.
//
// The offsets are librw's, taken from the compiler rather than read off the
// header: object 0, inDirtyList 8, objectList 16, matrix 24, ltm 88,
// child 152, next 156, root 160, sizeof 164 (32-bit). src/SB/Core/pc/rw/
// static_asserts every one of them, so this stops compiling if librw moves.
//
// The GameCube keeps retail's own order, because that build has to stay
// byte-identical. Guarded on GAMECUBE rather than __MWERKS__ deliberately: this
// is a question about the console, not the compiler. Safe here because nothing
// outside src/SB includes this header, and src/SB is the only thing built with
// -DPLATFORM_PC, which only the port defines.
#ifndef PLATFORM_PC
struct RwFrame
{
    RwObject object;
    RwLLLink inDirtyListLink;
    RwMatrix modelling;
    RwMatrix ltm;
    RwLinkList objectList;
    struct RwFrame* child;
    struct RwFrame* next;
    struct RwFrame* root;
};
#else
struct RwFrame
{
    RwObject object;
    RwLLLink inDirtyListLink;
    RwLinkList objectList;
    RwMatrix modelling; // librw calls this 'matrix'
    RwMatrix ltm;
    struct RwFrame* child;
    struct RwFrame* next;
    struct RwFrame* root;
};
#endif

typedef RwFrame* (*RwFrameCallBack)(RwFrame* frame, void* data);

#define RwFrameGetParent(_f) ((RwFrame*)rwObjectGetParent(_f))

#define RwFrameGetMatrix(_f) (&(_f)->modelling)

/* RwObjectHasFrame is typedef'd above */
typedef RwObjectHasFrame* (*RwObjectHasFrameSyncFunction)(RwObjectHasFrame* object);

struct RwObjectHasFrame
{
    RwObject object;
    RwLLLink lFrame;
    RwObjectHasFrameSyncFunction sync;
};

#define rwObjectHasFrameSetFrame(object, frame) _rwObjectHasFrameSetFrame(object, frame)
#define rwObjectHasFrameReleaseFrame(object) _rwObjectHasFrameReleaseFrame(object)

struct rwFrameList
{
    RwFrame** frames;
    RwInt32 numFrames;
};

struct RwBBox
{
    RwV3d sup;
    RwV3d inf;
};

enum RwCameraClearMode
{
    rwCAMERACLEARIMAGE = 0x1,
    rwCAMERACLEARZ = 0x2,
    rwCAMERACLEARSTENCIL = 0x4,
    rwCAMERACLEARMODEFORCEENUMSIZEINT = RWFORCEENUMSIZEINT
};
typedef enum RwCameraClearMode RwCameraClearMode;

enum RwCameraProjection
{
    rwNACAMERAPROJECTION = 0,
    rwPERSPECTIVE = 1,
    rwPARALLEL = 2,
    rwCAMERAPROJECTIONFORCEENUMSIZEINT = RWFORCEENUMSIZEINT
};
typedef enum RwCameraProjection RwCameraProjection;

enum RwFrustumTestResult
{
    rwSPHEREOUTSIDE = 0,
    rwSPHEREBOUNDARY = 1,
    rwSPHEREINSIDE = 2,
    rwFRUSTUMTESTRESULTFORCEENUMSIZEINT = RWFORCEENUMSIZEINT
};
typedef enum RwFrustumTestResult RwFrustumTestResult;

// Needs no PC variant: rw::FrustumPlane declares only closestX/Y/Z and the
// compiler pads it to the same 20 bytes RenderWare spells out with `pad`, so
// the two already agree field for field. layout_camera.cpp asserts that rather
// than leaving it on trust -- a frustum plane is read as an array element in
// iCamera.cpp and iModel.cpp, so the STRIDE matters as much as the offsets.
struct RwFrustumPlane
{
    RwPlane plane;
    RwUInt8 closestX;
    RwUInt8 closestY;
    RwUInt8 closestZ;
    RwUInt8 pad;
};

/* RwCamera is typedef'd above */
typedef RwCamera* (*RwCameraBeginUpdateFunc)(RwCamera* camera);
typedef RwCamera* (*RwCameraEndUpdateFunc)(RwCamera* camera);

// Mirrored onto rw::Camera the way RwFrame is: librw's field ORDER under
// RenderWare's field NAMES, so an RwCamera* IS an rw::Camera*. Every offset is
// asserted in src/SB/Core/pc/rw/layout_camera.cpp, taken from the compiler.
//
// Three things to know about the PC variant.
//
// `recipViewWindow` is GONE. RenderWare caches 1/viewWindow next to the view
// window; librw recomputes it in cameraSync and keeps nothing. Nothing in the
// game reads the field, and dropping it is what keeps every later offset equal
// to librw's -- so a port that starts wanting it has to compute it, not read it.
//
// The last six fields are librw's own and have no RenderWare counterpart. They
// are named after librw's members, as RwRaster::platform is, so that the
// assertions can name them: devView/devProj are the device-side view and
// projection matrices a GL or D3D backend builds in beginUpdate (RawMatrix, 16
// floats each), and clump/inClump/world/originalSync/originalBeginUpdate/
// originalEndUpdate are how librw implements what RenderWare does with plugin
// extensions on RwCamera. Game code must not touch any of them. Their
// counterparts on the RenderWare side -- the world sector list and the camera's
// clump link -- live in plugin blocks past the end of the struct there.
//
// beginUpdate and endUpdate keep RenderWare's NAMES but librw's SIGNATURES:
// librw's callbacks return void where RenderWare's return the camera. Nothing
// in the game reads or calls them, and this way a call through RenderWare's
// RwCameraBeginUpdateFunc is a compile error rather than a wrong return value.
#ifndef PLATFORM_PC
struct RwCamera
{
    RwObjectHasFrame object;
    RwCameraProjection projectionType;
    RwCameraBeginUpdateFunc beginUpdate;
    RwCameraEndUpdateFunc endUpdate;
    RwMatrix viewMatrix;
    RwRaster* frameBuffer;
    RwRaster* zBuffer;
    RwV2d viewWindow;
    RwV2d recipViewWindow;
    RwV2d viewOffset;
    RwReal nearPlane;
    RwReal farPlane;
    RwReal fogPlane;
    RwReal zScale, zShift;
    RwFrustumPlane frustumPlanes[6];
    RwBBox frustumBoundBox;
    RwV3d frustumCorners[8];
};
#else
struct RwCamera
{
    RwObjectHasFrame object;
    void (*beginUpdate)(RwCamera*); // librw calls this 'beginUpdateCB'
    void (*endUpdate)(RwCamera*); // librw calls this 'endUpdateCB'
    RwV2d viewWindow;
    RwV2d viewOffset;
    RwReal nearPlane;
    RwReal farPlane;
    RwReal fogPlane;
    RwCameraProjection projectionType; // librw calls this 'projection'
    RwMatrix viewMatrix;
    RwReal zScale, zShift;
    RwFrustumPlane frustumPlanes[6];
    RwV3d frustumCorners[8];
    RwBBox frustumBoundBox;
    RwRaster* frameBuffer;
    RwRaster* zBuffer;
    RwReal devView[16]; // librw only, from here down
    RwReal devProj[16];
    void* clump;
    RwLLLink inClump;
    void* world;
    void* originalSync;
    void* originalBeginUpdate;
    void* originalEndUpdate;
};
#endif

typedef RwCamera* (*RwCameraCallBack)(RwCamera* camera, void* data);

#define RwCameraGetViewOffset(_camera) (&((_camera)->viewOffset))

#define RwCameraSetRaster(_camera, _raster) (((_camera)->frameBuffer = (_raster)), (_camera))

#define RwCameraGetRaster(_camera) ((_camera)->frameBuffer)

#define RwCameraSetZRaster(_camera, _raster) (((_camera)->zBuffer = (_raster)), (_camera))

#define RwCameraGetZRaster(_camera) ((_camera)->zBuffer)

#define RwCameraGetNearClipPlane(_camera) ((_camera)->nearPlane)

#define RwCameraGetFarClipPlane(_camera) ((_camera)->farPlane)

#define RwCameraSetFogDistance(_camera, _distance) (((_camera)->fogPlane = (_distance)), (_camera))

#define RwCameraGetFogDistance(_camera) ((_camera)->fogPlane)

#define RwCameraGetCurrentCamera() ((RwCamera*)RWSRCGLOBAL(curCamera))

#define RwCameraGetProjection(_camera) ((_camera)->projectionType)

#define RwCameraGetViewWindow(_camera) (&((_camera)->viewWindow))

#define RwCameraGetViewMatrix(_camera) (&((_camera)->viewMatrix))

#define RwCameraSetFrame(_camera, _frame)                                                          \
    (_rwObjectHasFrameSetFrame((_camera), (_frame)), (_camera))

#define RwCameraGetFrame(_camera) ((RwFrame*)rwObjectGetParent((_camera)))

#ifdef __cplusplus
extern "C" {
#endif

extern RwBBox* RwBBoxCalculate(RwBBox* boundBox, const RwV3d* verts, RwInt32 numVerts);
extern RwBBox* RwBBoxInitialize(RwBBox* boundBox, const RwV3d* vertex);
extern RwBBox* RwBBoxAddPoint(RwBBox* boundBox, const RwV3d* vertex);
extern RwInt32 RwCameraRegisterPluginStream(RwUInt32 pluginID, RwPluginDataChunkReadCallBack readCB,
                                            RwPluginDataChunkWriteCallBack writeCB,
                                            RwPluginDataChunkGetSizeCallBack getSizeCB);
extern RwCamera* RwCameraStreamRead(RwStream* stream);
extern RwInt32 RwFrameRegisterPluginStream(RwUInt32 pluginID, RwPluginDataChunkReadCallBack readCB,
                                           RwPluginDataChunkWriteCallBack writeCB,
                                           RwPluginDataChunkGetSizeCallBack getSizeCB);
extern RwBool _rwFrameListFindFrame(const rwFrameList* frameList, const RwFrame* frame,
                                    RwInt32* npIndex);
extern rwFrameList* _rwFrameListDeinitialize(rwFrameList* frameList);
extern rwFrameList* _rwFrameListStreamRead(RwStream* stream, rwFrameList* fl);
extern RwInt32 RwTextureRegisterPluginStream(RwUInt32 pluginID,
                                             RwPluginDataChunkReadCallBack readCB,
                                             RwPluginDataChunkWriteCallBack writeCB,
                                             RwPluginDataChunkGetSizeCallBack getSizeCB);
extern RwUInt32 RwTextureStreamGetSize(const RwTexture* texture);
extern const RwTexture* RwTextureStreamWrite(const RwTexture* texture, RwStream* stream);
extern RwTexture* RwTextureStreamRead(RwStream* stream);
extern RwTexDictionary* RwTexDictionaryStreamRead(RwStream* stream);
extern RwCamera* RwCameraEndUpdate(RwCamera* camera);
extern RwCamera* RwCameraBeginUpdate(RwCamera* camera);
extern RwCamera* RwCameraSetViewOffset(RwCamera* camera, const RwV2d* offset);
extern RwCamera* RwCameraSetNearClipPlane(RwCamera* camera, RwReal nearClip);
extern RwCamera* RwCameraSetFarClipPlane(RwCamera* camera, RwReal farClip);
extern RwFrustumTestResult RwCameraFrustumTestSphere(const RwCamera* camera,
                                                     const RwSphere* sphere);
extern RwCamera* RwCameraClear(RwCamera* camera, RwRGBA* colour, RwInt32 clearMode);
extern RwCamera* RwCameraShowRaster(RwCamera* camera, void* pDev, RwUInt32 flags);
extern RwCamera* RwCameraSetProjection(RwCamera* camera, RwCameraProjection projection);
extern RwCamera* RwCameraSetViewWindow(RwCamera* camera, const RwV2d* viewWindow);
extern RwInt32 RwCameraRegisterPlugin(RwInt32 size, RwUInt32 pluginID,
                                      RwPluginObjectConstructor constructCB,
                                      RwPluginObjectDestructor destructCB,
                                      RwPluginObjectCopy copyCB);
extern RwBool RwCameraDestroy(RwCamera* camera);
extern RwCamera* RwCameraCreate(void);
extern RwBool RwFrameDirty(const RwFrame* frame);
extern RwFrame* RwFrameCreate(void);
extern RwBool RwFrameDestroy(RwFrame* frame);
extern RwBool RwFrameDestroyHierarchy(RwFrame* frame);
extern RwFrame* RwFrameUpdateObjects(RwFrame* frame);
extern RwMatrix* RwFrameGetLTM(RwFrame* frame);
extern RwFrame* RwFrameGetRoot(const RwFrame* frame);
extern RwFrame* RwFrameAddChild(RwFrame* parent, RwFrame* child);
extern RwFrame* RwFrameRemoveChild(RwFrame* child);
extern RwFrame* RwFrameForAllChildren(RwFrame* frame, RwFrameCallBack callBack, void* data);
extern RwFrame* RwFrameTranslate(RwFrame* frame, const RwV3d* v, RwOpCombineType combine);
extern RwFrame* RwFrameTransform(RwFrame* frame, const RwMatrix* m, RwOpCombineType combine);
extern RwFrame* RwFrameRotate(RwFrame* frame, const RwV3d* axis, RwReal angle,
                              RwOpCombineType combine);
extern RwFrame* RwFrameOrthoNormalize(RwFrame* frame);
extern RwInt32 RwFrameRegisterPlugin(RwInt32 size, RwUInt32 pluginID,
                                     RwPluginObjectConstructor constructCB,
                                     RwPluginObjectDestructor destructCB,
                                     RwPluginObjectCopy copyCB);
extern RwImage* RwImageCreate(RwInt32 width, RwInt32 height, RwInt32 depth);
extern RwBool RwImageDestroy(RwImage* image);
extern RwImage* RwImageAllocatePixels(RwImage* image);
extern RwImage* RwImageFreePixels(RwImage* image);
extern RwImage* RwImageMakeMask(RwImage* image);
extern RwImage* RwImageApplyMask(RwImage* image, const RwImage* mask);
extern const RwChar* RwImageFindFileType(const RwChar* imageName);
extern RwImage* RwImageReadMaskedImage(const RwChar* imageName, const RwChar* maskname);
extern RwRGBA* RwRGBASetFromPixel(RwRGBA* rgbOut, RwUInt32 pixelValue, RwInt32 rasterFormat);
extern RwImage* RwImageCopy(RwImage* destImage, const RwImage* sourceImage);
extern RwImage* RwImageGammaCorrect(RwImage* image);
extern RwBool RwImageSetGamma(RwReal gammaValue);
extern RwImage* RwImageSetFromRaster(RwImage* image, RwRaster* raster);
extern RwRaster* RwRasterSetFromImage(RwRaster* raster, RwImage* image);
extern RwImage* RwImageFindRasterFormat(RwImage* ipImage, RwInt32 nRasterType, RwInt32* npWidth,
                                        RwInt32* npHeight, RwInt32* npDepth, RwInt32* npFormat);
extern RwRaster* RwRasterUnlock(RwRaster* raster);
extern RwRaster* RwRasterUnlockPalette(RwRaster* raster);
extern RwBool RwRasterDestroy(RwRaster* raster);
extern RwInt32 RwRasterRegisterPlugin(RwInt32 size, RwUInt32 pluginID,
                                      RwPluginObjectConstructor constructCB,
                                      RwPluginObjectDestructor destructCB,
                                      RwPluginObjectCopy copyCB);
extern RwUInt8* RwRasterLockPalette(RwRaster* raster, RwInt32 lockMode);
extern RwInt32 RwRasterGetNumLevels(RwRaster* raster);
extern RwRaster* RwRasterShowRaster(RwRaster* raster, void* dev, RwUInt32 flags);
extern RwRaster* RwRasterCreate(RwInt32 width, RwInt32 height, RwInt32 depth, RwInt32 flags);
extern RwUInt8* RwRasterLock(RwRaster* raster, RwUInt8 level, RwInt32 lockMode);
extern RwImage* RwImageResample(RwImage* dstImage, const RwImage* srcImage);
extern RwImage* RwImageCreateResample(const RwImage* srcImage, RwInt32 width, RwInt32 height);
extern void _rwFrameSyncDirty(void);
extern RwBool RwTextureSetReadCallBack(RwTextureCallBackRead callBack);
extern RwBool RwTextureSetMipmapping(RwBool enable);
extern RwBool RwTextureGetMipmapping(void);
extern RwBool RwTextureSetAutoMipmapping(RwBool enable);
extern RwBool RwTextureGetAutoMipmapping(void);
extern RwTexture* RwTextureSetRaster(RwTexture* texture, RwRaster* raster);
extern RwTexDictionary* RwTexDictionaryCreate(void);
extern RwBool RwTexDictionaryDestroy(RwTexDictionary* dict);
extern const RwTexDictionary* RwTexDictionaryForAllTextures(const RwTexDictionary* dict,
                                                            RwTextureCallBack fpCallBack,
                                                            void* pData);
extern RwTexture* RwTextureCreate(RwRaster* raster);
extern RwBool RwTextureDestroy(RwTexture* texture);
extern RwTexture* RwTextureSetName(RwTexture* texture, const RwChar* name);
extern RwTexture* RwTextureSetMaskName(RwTexture* texture, const RwChar* maskName);
extern RwTexture* RwTexDictionaryAddTexture(RwTexDictionary* dict, RwTexture* texture);
extern RwTexture* RwTexDictionaryRemoveTexture(RwTexture* texture);
extern RwTexture* RwTexDictionaryFindNamedTexture(RwTexDictionary* dict, const RwChar* name);
extern RwTexDictionary* RwTexDictionaryGetCurrent(void);
extern RwBool RwTextureGenerateMipmapName(RwChar* name, RwChar* maskName, RwUInt8 mipLevel,
                                          RwInt32 format);
extern RwTexture* RwTextureRead(const RwChar* name, const RwChar* maskName);
extern RwInt32 RwTextureRegisterPlugin(RwInt32 size, RwUInt32 pluginID,
                                       RwPluginObjectConstructor constructCB,
                                       RwPluginObjectDestructor destructCB,
                                       RwPluginObjectCopy copyCB);
extern RwBool RwTextureRasterGenerateMipmaps(RwRaster* raster, RwImage* image);
extern void _rwObjectHasFrameSetFrame(void* object, RwFrame* frame);
extern void _rwObjectHasFrameReleaseFrame(void* object);
extern void* RwIm3DTransform(RwIm3DVertex* pVerts, RwUInt32 numVerts, RwMatrix* ltm,
                             RwUInt32 flags);
extern RwBool RwIm3DEnd(void);
extern RwBool RwIm3DRenderIndexedPrimitive(RwPrimitiveType primType, RwImVertexIndex* indices,
                                           RwInt32 numIndices);
extern RwBool RwIm3DRenderPrimitive(RwPrimitiveType primType);
extern RxPipeline* RwIm3DSetTransformPipeline(RxPipeline* pipeline);
extern RxPipeline* RwIm3DSetRenderPipeline(RxPipeline* pipeline, RwPrimitiveType primType);
extern void _rxPacketDestroy(RxPacket* Packet);
extern RwBool _rxPipelineClose(void);
extern RwBool _rxPipelineOpen(void);
extern RxHeap* RxHeapGetGlobalHeap(void);
extern RxPipeline* RxPipelineExecute(RxPipeline* pipeline, void* data, RwBool heapReset);
extern RxPipeline* RxPipelineCreate(void);
extern void _rxPipelineDestroy(RxPipeline* Pipeline);
extern RxPipeline* RxLockedPipeUnlock(RxLockedPipe* pipeline);
extern RxLockedPipe* RxPipelineLock(RxPipeline* pipeline);
extern RxPipelineNode* RxPipelineFindNodeByName(RxPipeline* pipeline, const RwChar* name,
                                                RxPipelineNode* start, RwInt32* nodeIndex);
extern RxLockedPipe* RxLockedPipeAddFragment(RxLockedPipe* pipeline, RwUInt32* firstIndex,
                                             RxNodeDefinition* nodeDef0, ...);
extern RxPipeline* RxLockedPipeAddPath(RxLockedPipe* pipeline, RxNodeOutput out, RxNodeInput in);
extern void RxHeapFree(RxHeap* heap, void* block);
extern RwBool _rxHeapReset(RxHeap* heap);
extern void RxHeapDestroy(RxHeap* heap);
extern RxHeap* RxHeapCreate(RwUInt32 size);
extern RxRenderStateVector*
RxRenderStateVectorSetDefaultRenderStateVector(RxRenderStateVector* rsvp);
extern RxRenderStateVector* RxRenderStateVectorLoadDriverState(RxRenderStateVector* rsvp);
extern void RwGameCubeCameraTextureFlush(RwRaster* ras, RwUInt32 param);

#ifdef __cplusplus
}
#endif

#endif