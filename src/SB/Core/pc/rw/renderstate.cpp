// RenderWare C API: RwRenderStateSet/Get, and RxRenderStateVectorLoadDriverState.
//
// Unlike the rest of this directory these are not casts and calls, for two
// reasons.
//
// FIRST: the game reads render state back. Not as a debugging aid -- as the
// save/restore idiom that every overlay in the game is built on.
//
//     xFont.cpp:620      nine RwRenderStateGet calls into a static `oldrs`
//     xFont.cpp:644      the matching nine RwRenderStateSet calls to restore
//     xShadowSimple:660  RxRenderStateVectorLoadDriverState, then five restores
//
// So a Get has to return what the last Set was given. Forwarding a Get to
// librw would not do that: rw::GetRenderState asks the render device, and the
// device under LIBRW_PLATFORM=NULL answers 0 to every question. xfont would
// then "restore" every state to zero -- rwBLENDNABLEND, rwSHADEMODENASHADEMODE
// -- and the first frame of text would take the rest of the frame with it.
// That is why the shim keeps its own copy and answers out of it. RenderWare's
// drivers cache render state exactly this way for exactly this reason.
//
// SECOND: librw models thirteen of RenderWare's twenty-three states. The ones
// it has no counterpart for are listed under "Recorded but not rendered" below.
// Those are RECORDED, so the save/restore idiom keeps working and a Get is
// still truthful about what was Set -- but their Set returns FALSE, because the
// value did not reach a renderer and saying otherwise would be a lie. They are
// listed in TODO.md as well.
//
// The copy is not a second source of truth for anything librw owns: every state
// that librw does model is forwarded to it on the way in. Nothing else in the
// port calls rw::SetRenderState.

#include <rwcore.h>

#include "rw.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// RxRenderStateVector::Flags
//
// RenderWare packs the five boolean states that have no field of their own into
// the Flags word. Two of the bits are PROVEN by the game, which unpacks them by
// hand rather than by name at xShadowSimple.cpp:687-688:
//
//     RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,      (void*)((xrsv.Flags >> 2) & 1));
//     RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)((xrsv.Flags >> 3) & 1));
//
// The other three are inferred: the five booleans appear in the RwRenderState
// enumeration in the order TEXTUREPERSPECTIVE, ZTESTENABLE, ZWRITEENABLE,
// VERTEXALPHAENABLE, FOGENABLE, and assigning them bits in that order puts
// ZWRITEENABLE at bit 2 and VERTEXALPHAENABLE at bit 3, which is what the game
// says they are. Nothing in src/SB reads the other three bits, so the inference
// is not load-bearing today; it is written down here so that whoever finds it
// wrong knows what it was based on.
//
// Kept local to this file rather than added to rwcore.h because the game does
// not use the names -- it shifts the raw word -- so exporting them would be
// inventing an interface nothing asked for.
enum
{
    RSFLAG_TEXTUREPERSPECTIVE = 0x1,
    RSFLAG_ZTESTENABLE = 0x2,
    RSFLAG_ZWRITEENABLE = 0x4,
    RSFLAG_VERTEXALPHAENABLE = 0x8,
    RSFLAG_FOGENABLE = 0x10
};

// The states RenderWare has and RxRenderStateVector does not carry. They are
// here so that a Set/Get pair round-trips for them too.
struct ShimStencilState
{
    RwBool enable;
    RwStencilOperation fail;
    RwStencilOperation zfail;
    RwStencilOperation pass;
    RwStencilFunction function;
    RwUInt32 functionRef;
    RwUInt32 functionMask;
    RwUInt32 functionWriteMask;
};

// Zero-initialised, which is NOT RenderWare's set of driver defaults -- a Get
// before the first Set reports rwSHADEMODENASHADEMODE and rwBLENDNABLEND rather
// than gouraud and src-alpha. That is deliberate: the defaults could not be
// checked against a RenderWare source, and inventing them would put a wrong
// number somewhere a reader would trust. It costs nothing in practice, because
// zRenderState() in zRenderState.cpp writes filter, fog, vertex alpha, cull,
// address, both blends, shade mode, z-write and z-test on every state
// transition, and it runs before anything reads a state back.
static RxRenderStateVector sState;
static ShimStencilState sStencil;

// RenderWare passes the small integer states as the pointer itself -- the game
// writes (void*)rwBLENDSRCALPHA -- so this is the unpacking, not a cast between
// two kinds of pointer.
static inline RwUInt32 asUInt(void* value)
{
    return (RwUInt32)(uintptr_t)value;
}

// ---------------------------------------------------------------------------
// Fog and border colour packing
//
// The one caller builds the fog colour by hand at iCamera.cpp:373:
//
//     bite_me = (fogcolor.alpha << 24) | (fogcolor.red << 16) |
//               (fogcolor.green << 8)  |  fogcolor.blue;
//
// -- so the RwUInt32 crossing this API is ARGB, red in the second byte. librw
// wants the opposite: gl3device.cpp reads red out of the LOW byte and alpha out
// of the high one, which is ABGR. Forwarding the word unchanged would render
// blue fog where the level says red, and nothing would fail -- it would just
// look wrong on Jellyfish Fields.
//
// RenderWare's own documented packing could not be checked against a source, so
// what the shim honours is what the caller demonstrably builds. Get returns the
// same packing Set accepted, so the round-trip is exact either way.

static void unpackARGB(RwUInt32 packed, RwRGBA* out)
{
    out->alpha = (RwUInt8)(packed >> 24);
    out->red = (RwUInt8)(packed >> 16);
    out->green = (RwUInt8)(packed >> 8);
    out->blue = (RwUInt8)packed;
}

static RwUInt32 packARGB(const RwRGBA* in)
{
    return ((RwUInt32)in->alpha << 24) | ((RwUInt32)in->red << 16) | ((RwUInt32)in->green << 8) |
           (RwUInt32)in->blue;
}

static RwUInt32 packLibrwABGR(const RwRGBA* in)
{
    return ((RwUInt32)in->alpha << 24) | ((RwUInt32)in->blue << 16) | ((RwUInt32)in->green << 8) |
           (RwUInt32)in->red;
}

// ---------------------------------------------------------------------------
// Blend factors the target hardware refuses
//
// **The GameCube driver does not accept every RwBlendFunction in every slot,
// and the game relies on the ones it refuses being ignored.**
//
// _rwDlSetRenderState (rwsdk/driver/gcn/dlrendst.c, disassembled at
// 0x8024922C for SRCBLEND and 0x802492BC for DESTBLEND) validates the value
// before it reaches GXSetBlendMode:
//
//     SRCBLEND   accepts 1..2 and 5..10; REFUSES 0, 3, 4, 11 and above
//     DESTBLEND  accepts 1..8;           REFUSES 0 and 9 and above
//
// A refused value returns FALSE and changes NOTHING -- not the hardware, not
// the driver's own cache -- so the previous factor stays in force. The gap is
// the Flipper's: GX has one enum value for "the source colour" and "the
// destination colour" (GX_BL_SRCCLR == GX_BL_DSTCLR), so the src slot cannot
// name the source and the dst slot cannot name the destination. RenderWare
// answers by refusing rwBLENDSRCCOLOR/rwBLENDINVSRCCOLOR as a source factor
// and rwBLENDDESTCOLOR/rwBLENDINVDESTCOLOR as a destination one, and
// rwBLENDSRCALPHASAT in either.
//
// D3D9 has no such gap and librw passes the value straight to
// D3DRS_SRCBLEND, which is where the port and the console part company. It
// matters, because fourteen of the game's particle systems ask for exactly
// the refused combination -- src rwBLENDSRCCOLOR, dst rwBLENDINVSRCALPHA --
// and every one of them is a smoke, steam, mist or dust effect:
//
//     b201 STEAM/SMOKESTACK/FREEZE_BREATH, b301 SMOKE/MIST/STEAM,
//     b302+b303 BIGDUP_SMOKE, bb01 SMOKE/FIRE/DUST_CLOUD, ...
//
// On the console the src factor stays at the rwBLENDSRCALPHA that
// zRenderState() had just set, so those draw as ordinary alpha blends: soft
// translucent puffs. Honoured literally on D3D9 the source is multiplied by
// itself instead, and since Particle_cloud is a near-white bitmap whose shape
// lives entirely in its alpha channel (mean RGB 224/219/197, alpha 0..190),
// squaring it changes almost nothing and the alpha never gets a say. The
// result is a flat white square, which is what Robo Patrick's steam drew.
//
// So the validation belongs here, in the shim that stands in for the GameCube
// driver, rather than in librw -- it is this game's device behaviour, not a
// bug in the library. The Get shadow is left alone on a refusal for the same
// reason the driver leaves its cache alone: a Get must report the factor that
// is actually in force.
static bool blendFactorAccepted(RwRenderState state, RwUInt32 value)
{
    if (state == rwRENDERSTATESRCBLEND)
    {
        return (value >= rwBLENDZERO && value <= rwBLENDONE) ||
               (value >= rwBLENDSRCALPHA && value <= rwBLENDINVDESTCOLOR);
    }

    return value >= rwBLENDZERO && value <= rwBLENDINVDESTALPHA;
}

// ---------------------------------------------------------------------------

RwBool RwRenderStateSet(RwRenderState state, void* value)
{
    const RwUInt32 uvalue = asUInt(value);

    switch (state)
    {
    // --- states librw models -------------------------------------------

    case rwRENDERSTATETEXTURERASTER:
        // An RwRaster IS an rw::Raster, so the pointer goes straight through.
        sState.TextureRaster = (RwRaster*)value;
        rw::SetRenderStatePtr(rw::TEXTURERASTER, value);
        return TRUE;

    case rwRENDERSTATETEXTUREADDRESS:
        // RenderWare's combined form sets both axes, and so does librw's.
        sState.AddressModeU = (RwTextureAddressMode)uvalue;
        sState.AddressModeV = (RwTextureAddressMode)uvalue;
        rw::SetRenderState(rw::TEXTUREADDRESS, uvalue);
        return TRUE;

    case rwRENDERSTATETEXTUREADDRESSU:
        sState.AddressModeU = (RwTextureAddressMode)uvalue;
        rw::SetRenderState(rw::TEXTUREADDRESSU, uvalue);
        return TRUE;

    case rwRENDERSTATETEXTUREADDRESSV:
        sState.AddressModeV = (RwTextureAddressMode)uvalue;
        rw::SetRenderState(rw::TEXTUREADDRESSV, uvalue);
        return TRUE;

    case rwRENDERSTATETEXTUREFILTER:
        sState.FilterMode = (RwTextureFilterMode)uvalue;
        rw::SetRenderState(rw::TEXTUREFILTER, uvalue);
        return TRUE;

    case rwRENDERSTATESRCBLEND:
        if (!blendFactorAccepted(state, uvalue))
        {
            return FALSE;
        }
        sState.SrcBlend = (RwBlendFunction)uvalue;
        rw::SetRenderState(rw::SRCBLEND, uvalue);
        return TRUE;

    case rwRENDERSTATEDESTBLEND:
        if (!blendFactorAccepted(state, uvalue))
        {
            return FALSE;
        }
        sState.DestBlend = (RwBlendFunction)uvalue;
        rw::SetRenderState(rw::DESTBLEND, uvalue);
        return TRUE;

    case rwRENDERSTATECULLMODE:
        // The one state that has nowhere to live in RxRenderStateVector, which
        // is RenderWare's omission, not ours -- LoadDriverState cannot report
        // it and xShadowSimple therefore never restores it.
        rw::SetRenderState(rw::CULLMODE, uvalue);
        return TRUE;

    case rwRENDERSTATEZTESTENABLE:
        sState.Flags = uvalue ? (sState.Flags | RSFLAG_ZTESTENABLE) :
                                (sState.Flags & ~(RwUInt32)RSFLAG_ZTESTENABLE);
        rw::SetRenderState(rw::ZTESTENABLE, uvalue);
        return TRUE;

    case rwRENDERSTATEZWRITEENABLE:
        sState.Flags = uvalue ? (sState.Flags | RSFLAG_ZWRITEENABLE) :
                                (sState.Flags & ~(RwUInt32)RSFLAG_ZWRITEENABLE);
        rw::SetRenderState(rw::ZWRITEENABLE, uvalue);
        return TRUE;

    case rwRENDERSTATEVERTEXALPHAENABLE:
        sState.Flags = uvalue ? (sState.Flags | RSFLAG_VERTEXALPHAENABLE) :
                                (sState.Flags & ~(RwUInt32)RSFLAG_VERTEXALPHAENABLE);
        rw::SetRenderState(rw::VERTEXALPHA, uvalue);
        return TRUE;

    case rwRENDERSTATEFOGENABLE:
        sState.Flags = uvalue ? (sState.Flags | RSFLAG_FOGENABLE) :
                                (sState.Flags & ~(RwUInt32)RSFLAG_FOGENABLE);
        rw::SetRenderState(rw::FOGENABLE, uvalue);
        return TRUE;

    case rwRENDERSTATEFOGCOLOR:
        unpackARGB(uvalue, &sState.FogColor);
        rw::SetRenderState(rw::FOGCOLOR, packLibrwABGR(&sState.FogColor));
        return TRUE;

    case rwRENDERSTATESTENCILENABLE:
        sStencil.enable = (RwBool)uvalue;
        rw::SetRenderState(rw::STENCILENABLE, uvalue);
        return TRUE;

    case rwRENDERSTATESTENCILFAIL:
        sStencil.fail = (RwStencilOperation)uvalue;
        rw::SetRenderState(rw::STENCILFAIL, uvalue);
        return TRUE;

    case rwRENDERSTATESTENCILZFAIL:
        sStencil.zfail = (RwStencilOperation)uvalue;
        rw::SetRenderState(rw::STENCILZFAIL, uvalue);
        return TRUE;

    case rwRENDERSTATESTENCILPASS:
        sStencil.pass = (RwStencilOperation)uvalue;
        rw::SetRenderState(rw::STENCILPASS, uvalue);
        return TRUE;

    case rwRENDERSTATESTENCILFUNCTION:
        sStencil.function = (RwStencilFunction)uvalue;
        rw::SetRenderState(rw::STENCILFUNCTION, uvalue);
        return TRUE;

    case rwRENDERSTATESTENCILFUNCTIONREF:
        sStencil.functionRef = uvalue;
        rw::SetRenderState(rw::STENCILFUNCTIONREF, uvalue);
        return TRUE;

    case rwRENDERSTATESTENCILFUNCTIONMASK:
        sStencil.functionMask = uvalue;
        rw::SetRenderState(rw::STENCILFUNCTIONMASK, uvalue);
        return TRUE;

    case rwRENDERSTATESTENCILFUNCTIONWRITEMASK:
        sStencil.functionWriteMask = uvalue;
        rw::SetRenderState(rw::STENCILFUNCTIONWRITEMASK, uvalue);
        return TRUE;

    // --- Recorded but not rendered -------------------------------------
    //
    // librw has no counterpart for any of these, so they are stored -- a Get
    // still answers with what was Set, which is what the save/restore idiom
    // needs -- and FALSE says the value did not reach a renderer.

    case rwRENDERSTATESHADEMODE:
        // Fifteen call sites, and none of them will do anything until a backend
        // grows a flat-shading path. librw's own header says "? shademode" in
        // the TODO above its RenderState enum. Everything the game draws flat
        // (fonts, the fill quads, particles) will come out gouraud, which for
        // untextured single-colour geometry is the same picture -- but the
        // outlined text in xFont.cpp:3230 is not that.
        sState.ShadeMode = (RwShadeMode)uvalue;
        return FALSE;

    case rwRENDERSTATETEXTUREPERSPECTIVE:
        sState.Flags = uvalue ? (sState.Flags | RSFLAG_TEXTUREPERSPECTIVE) :
                                (sState.Flags & ~(RwUInt32)RSFLAG_TEXTUREPERSPECTIVE);
        return FALSE;

    case rwRENDERSTATEBORDERCOLOR:
        // No caller in src/SB. Packed the same way as the fog colour, since
        // that is the only packing this API has been shown to use.
        unpackARGB(uvalue, &sState.BorderColor);
        return FALSE;

    case rwRENDERSTATEFOGTYPE:
        sState.FogType = (RwFogType)uvalue;
        return FALSE;

    case rwRENDERSTATEFOGDENSITY:
        // Deliberately NOT recorded. The one caller passes a POINTER to a float
        // rather than a value (iCamera.cpp:380, `(void*)&pFogParams->density`),
        // so what the console's driver reads through it is a driver detail this
        // side has no way to check, and storing four bytes under a guess about
        // which four they are would be worse than storing nothing. librw has no
        // fog density state to forward to either way, and nothing reads it back.
        return FALSE;

    default:
        return FALSE;
    }
}

RwBool RwRenderStateGet(RwRenderState state, void* value)
{
    if (value == NULL)
    {
        return FALSE;
    }

    // Every one of these writes four bytes, which is what the callers' locals
    // are: RwBool, RwRaster*, and the mode enumerations are all RwInt32-sized.
    switch (state)
    {
    case rwRENDERSTATETEXTURERASTER:
        *(RwRaster**)value = sState.TextureRaster;
        return TRUE;

    case rwRENDERSTATETEXTUREADDRESS:
        // RenderWare answers the combined query only when the two axes agree.
        if (sState.AddressModeU != sState.AddressModeV)
        {
            return FALSE;
        }
        *(RwTextureAddressMode*)value = sState.AddressModeU;
        return TRUE;

    case rwRENDERSTATETEXTUREADDRESSU:
        *(RwTextureAddressMode*)value = sState.AddressModeU;
        return TRUE;

    case rwRENDERSTATETEXTUREADDRESSV:
        *(RwTextureAddressMode*)value = sState.AddressModeV;
        return TRUE;

    case rwRENDERSTATETEXTUREFILTER:
        *(RwTextureFilterMode*)value = sState.FilterMode;
        return TRUE;

    case rwRENDERSTATESRCBLEND:
        *(RwBlendFunction*)value = sState.SrcBlend;
        return TRUE;

    case rwRENDERSTATEDESTBLEND:
        *(RwBlendFunction*)value = sState.DestBlend;
        return TRUE;

    case rwRENDERSTATECULLMODE:
        // Not shadowed -- RxRenderStateVector has no field for it -- so this is
        // the one Get that does have to ask librw. Honest under a real backend;
        // under LIBRW_PLATFORM=NULL the device answers 0 and there is nothing
        // better to answer with. No caller in src/SB reads it.
        *(RwCullMode*)value = (RwCullMode)rw::GetRenderState(rw::CULLMODE);
        return TRUE;

    case rwRENDERSTATESHADEMODE:
        *(RwShadeMode*)value = sState.ShadeMode;
        return TRUE;

    case rwRENDERSTATETEXTUREPERSPECTIVE:
        *(RwBool*)value = (sState.Flags & RSFLAG_TEXTUREPERSPECTIVE) ? TRUE : FALSE;
        return TRUE;

    case rwRENDERSTATEZTESTENABLE:
        *(RwBool*)value = (sState.Flags & RSFLAG_ZTESTENABLE) ? TRUE : FALSE;
        return TRUE;

    case rwRENDERSTATEZWRITEENABLE:
        *(RwBool*)value = (sState.Flags & RSFLAG_ZWRITEENABLE) ? TRUE : FALSE;
        return TRUE;

    case rwRENDERSTATEVERTEXALPHAENABLE:
        *(RwBool*)value = (sState.Flags & RSFLAG_VERTEXALPHAENABLE) ? TRUE : FALSE;
        return TRUE;

    case rwRENDERSTATEFOGENABLE:
        *(RwBool*)value = (sState.Flags & RSFLAG_FOGENABLE) ? TRUE : FALSE;
        return TRUE;

    case rwRENDERSTATEFOGCOLOR:
        *(RwUInt32*)value = packARGB(&sState.FogColor);
        return TRUE;

    case rwRENDERSTATEBORDERCOLOR:
        *(RwUInt32*)value = packARGB(&sState.BorderColor);
        return TRUE;

    case rwRENDERSTATEFOGTYPE:
        *(RwFogType*)value = sState.FogType;
        return TRUE;

    case rwRENDERSTATESTENCILENABLE:
        *(RwBool*)value = sStencil.enable;
        return TRUE;

    case rwRENDERSTATESTENCILFAIL:
        *(RwStencilOperation*)value = sStencil.fail;
        return TRUE;

    case rwRENDERSTATESTENCILZFAIL:
        *(RwStencilOperation*)value = sStencil.zfail;
        return TRUE;

    case rwRENDERSTATESTENCILPASS:
        *(RwStencilOperation*)value = sStencil.pass;
        return TRUE;

    case rwRENDERSTATESTENCILFUNCTION:
        *(RwStencilFunction*)value = sStencil.function;
        return TRUE;

    case rwRENDERSTATESTENCILFUNCTIONREF:
        *(RwUInt32*)value = sStencil.functionRef;
        return TRUE;

    case rwRENDERSTATESTENCILFUNCTIONMASK:
        *(RwUInt32*)value = sStencil.functionMask;
        return TRUE;

    case rwRENDERSTATESTENCILFUNCTIONWRITEMASK:
        *(RwUInt32*)value = sStencil.functionWriteMask;
        return TRUE;

    case rwRENDERSTATEFOGDENSITY:
        // See the Set case: nothing was recorded, so there is nothing to hand
        // back and the caller's variable is left alone rather than zeroed.
        return FALSE;

    default:
        return FALSE;
    }
}

// RenderWare's pipeline reads the driver's current state into a vector so a
// node can restore it afterwards. That is exactly the copy this file keeps, so
// this is a structure assignment.
//
// Cull mode is missing from it, and that is RenderWare's own omission --
// RxRenderStateVector has no field for it. xShadowSimple.cpp:660 loads a vector
// and restores five states from it, and cull mode is not one of them, so
// nothing is lost here that is not also lost on the console.
RxRenderStateVector* RxRenderStateVectorLoadDriverState(RxRenderStateVector* rsvp)
{
    if (rsvp == NULL)
    {
        return NULL;
    }

    *rsvp = sState;
    return rsvp;
}

// ---------------------------------------------------------------------------
// The colour write mask.
//
// NOT a RenderWare render state -- rwcore.h has no rwRENDERSTATECOLORWRITE* and
// inventing one would put something in the C API mirror that retail never had.
// It is a GameCube/PS2 facility that the port has to reach anyway, because
// iDraw.cpp:iDrawSetFBMSK forwards to it, so it is a named seam of its own.
//
// mask is librw's ColorWriteMask: a set bit means the channel IS written, which
// is the opposite sense to the GS register iDraw takes. The inversion belongs
// with the caller that knows the register, not here.
void rwSetColorWriteMask(RwUInt32 mask)
{
    rw::SetRenderState(rw::COLORWRITEMASK, (rw::uint32)mask);
}

// ---------------------------------------------------------------------------
// Cutout transparency.
//
// Also NOT a RenderWare render state, and for a stronger reason than the mask
// above: it is not a state the game ever sets, it is a decision about how to
// DRAW one it does. RenderWare's D3D drivers infer transparency from the bound
// texture -- a raster with an alpha channel switches blending on by itself,
// without the application asking -- and that inference is what this changes.
//
// Why it is a setting rather than a fix. The two readings of a texture's alpha
// are both defensible at 640x480 and stop being interchangeable above it. Where
// a texture goes from transparent to opaque, filtering leaves a band a texel
// wide, and blending that band draws whatever is behind the surface through it.
// A texel is about a screen pixel at the size the art was drawn for, so the
// band reads as a soft edge; at 4K it is six pixels of the level's own
// background showing through a cave wall. Cutting instead -- keeping the band
// opaque up to `ref` and dropping the rest -- makes the silhouette on screen
// the silhouette in the artwork whatever the render size is, at the cost of
// the softness, which is the right trade for a fence and the wrong one for a
// pane of glass.
//
// So the reference is the caller's, 0 turns it off, and the narrowing that
// keeps a pane of glass out of it lives in the driver: only geometry drawn
// with no blend of its own and with depth writes on -- geometry the game is
// treating as opaque -- is ever cut.
void rwSetAlphaCutout(RwUInt32 ref)
{
#ifdef RW_D3D9
    rw::d3d::setAlphaCutout((rw::int32)ref);
#else
    // No device, nothing to infer from, nothing to correct. The NULL platform
    // build exists for the tests.
    (void)ref;
#endif
}
