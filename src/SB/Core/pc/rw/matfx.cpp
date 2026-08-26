// RenderWare C API: RpMatFX, the material effects plugin.
//
// Nothing here is mirrored, because nothing here is a struct the game can see:
// every RpMatFX call takes an RpMaterial or an RpAtomic and the effect data
// lives in a plugin block behind it. librw keeps its own rw::MatFX there and
// reaches it the same way. What the two sides do share is the effect
// identifiers, and layout_geometry.cpp asserts all seven agree.
//
// Where the attach goes, same as skin.cpp: RpMatFXPluginAttach is
// RenderWare's own entry point, RWAttachPlugins in iSystem.cpp already calls
// it between RwEngineInit and RwEngineOpen, and librw's registerMatFXPlugin()
// registers an Atomic plugin and a Material plugin -- both of which grow
// object sizes that Engine::open freezes. So it belongs in the attach
// function, not in RwEngineInit.
//
// One behaviour worth knowing: librw's setEffects only frees the old effect
// data when it is REPLACED by a different effect, not when it is cleared.
// RpMatFXMaterialSetEffects(m, rpMATFXEFFECTNULL) therefore leaves the
// material's environment map texture referenced until the material is
// destroyed. xFX.cpp does exactly that when it turns shine off, so a level
// that toggles effects holds those textures alive rather than leaking them
// unboundedly -- the count is bounded by the number of materials.

#include <rwcore.h>
#include <rpmatfx.h>
#include <rpworld.h>

#include "rw.h"

static inline rw::Material* asMaterial(const RpMaterial* m)
{
    return const_cast<rw::Material*>(reinterpret_cast<const rw::Material*>(m));
}

RwBool RpMatFXPluginAttach(void)
{
    rw::registerMatFXPlugin();
    return TRUE;
}

RpAtomic* RpMatFXAtomicEnableEffects(RpAtomic* atomic)
{
    if (atomic == NULL)
    {
        return NULL;
    }

    // Sets the atomic's flag and swaps in the material-effects pipeline, which
    // is both halves of what RenderWare's does.
    rw::MatFX::enableEffects(reinterpret_cast<rw::Atomic*>(atomic));
    return atomic;
}

RpMaterial* RpMatFXMaterialSetEffects(RpMaterial* material, RpMatFXMaterialFlags flags)
{
    if (material == NULL)
    {
        return NULL;
    }

    // Allocates the material's effect block on first use, and lays out the two
    // effect slots the way the combined effects need them -- BUMPENVMAP fills
    // both, everything else fills one.
    rw::MatFX::setEffects(asMaterial(material), (rw::uint32)flags);
    return material;
}

RpMatFXMaterialFlags RpMatFXMaterialGetEffects(const RpMaterial* material)
{
    if (material == NULL)
    {
        return rpMATFXEFFECTNULL;
    }

    // Zero for a material that was never given an effect block, which is what
    // iModel.cpp and xFX.cpp test for.
    return (RpMatFXMaterialFlags)rw::MatFX::getEffects(asMaterial(material));
}

// The two Setup calls are RenderWare's convenience form: they fill in every
// property of one effect at once, and they require RpMatFXMaterialSetEffects
// to have been called first with an effect that includes this one. xFX.cpp
// pairs them that way at both call sites. librw enforces the same
// precondition, by looking the effect up in the material's two slots and doing
// nothing when it is not there -- so a Setup without a SetEffects is quietly
// ignored on both sides rather than corrupting the other effect.

RpMaterial* RpMatFXMaterialSetupBumpMap(RpMaterial* material, RwTexture* texture, RwFrame* frame,
                                        RwReal coef)
{
    if (material == NULL)
    {
        return NULL;
    }

    rw::MatFX* fx = rw::MatFX::get(asMaterial(material));
    if (fx == NULL)
    {
        return NULL;
    }

    fx->setBumpTexture(reinterpret_cast<rw::Texture*>(texture));
    fx->setBumpCoefficient(coef);

    // librw has no setBumpFrame: its Bump effect carries a frame field that
    // nothing in librw reads or writes. RenderWare uses it to orient the bump
    // basis, and the D3D9/GL3 bump pipelines that would need it are not
    // written on this side either, so the frame is stored directly rather than
    // dropped -- it is there for whoever writes those.
    rw::int32 i = fx->getEffectIndex(rw::MatFX::BUMPMAP);
    if (i >= 0)
    {
        fx->fx[i].bump.frame = reinterpret_cast<rw::Frame*>(frame);
    }

    return material;
}

RpMaterial* RpMatFXMaterialSetupEnvMap(RpMaterial* material, RwTexture* texture, RwFrame* frame,
                                       RwBool useFrameBufferAlpha, RwReal coef)
{
    if (material == NULL)
    {
        return NULL;
    }

    rw::MatFX* fx = rw::MatFX::get(asMaterial(material));
    if (fx == NULL)
    {
        return NULL;
    }

    fx->setEnvTexture(reinterpret_cast<rw::Texture*>(texture));
    fx->setEnvFrame(reinterpret_cast<rw::Frame*>(frame));
    fx->setEnvFBAlpha(useFrameBufferAlpha);
    fx->setEnvCoefficient(coef);

    return material;
}

RpMaterial* RpMatFXMaterialSetBumpMapCoefficient(RpMaterial* material, RwReal coef)
{
    if (material == NULL)
    {
        return NULL;
    }

    rw::MatFX* fx = rw::MatFX::get(asMaterial(material));
    if (fx == NULL)
    {
        return NULL;
    }

    fx->setBumpCoefficient(coef);
    return material;
}

RpMaterial* RpMatFXMaterialSetEnvMapCoefficient(RpMaterial* material, RwReal coef)
{
    if (material == NULL)
    {
        return NULL;
    }

    rw::MatFX* fx = rw::MatFX::get(asMaterial(material));
    if (fx == NULL)
    {
        return NULL;
    }

    // xFX.cpp calls this every frame on every material of a shiny model, and
    // on a material whose effect is not an env map it is a no-op on both
    // sides. MaterialSetShininess checks the effect first; MaterialSetEnvMap
    // does not, and relies on this.
    fx->setEnvCoefficient(coef);
    return material;
}

// The MatFX pipeline, by GameCube name.
//
// iModel.cpp:111 assigns this to an atomic whose material has an effect on it:
//
//     model->pipeline = RpMatFXGetGameCubePipeline(rpMATFXGAMECUBEATOMICPIPELINE);
//
// It is the one GameCube-specific line in a 1087-line file that is otherwise
// entirely portable, and the port answers it with the equivalent for whatever
// backend is linked. librw keeps one MatFX pipeline per platform in
// matFXGlobals.pipelines[], indexed by rw::platform, which is what the console's
// GameCube pipeline IS on its side -- the same object under a name that only
// makes sense there.
//
// So this is not a stub: an atomic that gets it renders with the environment
// and bump map effects, and an atomic that gets NULL falls back to the default
// pipeline and renders without them.
RxPipeline* RpMatFXGetGameCubePipeline(RpMatFXGameCubePipeline gamecubePipeline)
{
    if (gamecubePipeline != rpMATFXGAMECUBEATOMICPIPELINE)
    {
        return NULL;
    }

    if (rw::platform < 0 || rw::platform >= rw::NUM_PLATFORMS)
    {
        return NULL;
    }

    return reinterpret_cast<RxPipeline*>(rw::matFXGlobals.pipelines[rw::platform]);
}
