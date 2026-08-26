// The two RenderWare plugins the port attaches and otherwise does not touch.
//
// RpSkin, RpMatFX, RpPTank, RpWorld and RpCollision each have a file of their
// own because the game calls into them. These two it only ATTACHES: iSystem's
// RWAttachPlugins registers them so that the objects they extend come out the
// right size and their chunks survive a stream round trip, and no game code
// calls an RpHAnim* or RpUserData* function on the PC path.
//
// They still have to be attached rather than skipped. Both grow an object --
// HAnim an atomic and a frame, UserData a geometry, a material and a world --
// and a model whose file carries their extension chunks is read by librw's
// plugin stream reader, which skips a chunk it has no plugin for. Attaching
// costs nothing and not attaching loses data silently.
//
// Like every other attach, strictly between RwEngineInit and RwEngineOpen:
// Engine::open freezes object sizes.

#include <rwcore.h>
#include <rphanim.h>
#include <rpusrdat.h>

#include "rw.h"

RwBool RpHAnimPluginAttach(void)
{
    // Registers the hierarchy on an atomic and the node on a frame, which is
    // what makes a skinned model's bone hierarchy read back. librw's own skin
    // pipeline looks the hierarchy up through it.
    rw::registerHAnimPlugin();
    return TRUE;
}

RwBool RpUserDataPluginAttach(void)
{
    rw::registerUserDataPlugin();
    return TRUE;
}
