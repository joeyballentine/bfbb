// RenderWare C API: RpWorld.
//
// RpWorld has librw's field order under RenderWare's field names (see
// include/rwsdk/rpworld.h and layout_world.cpp), so an RpWorld* IS an
// rw::World* and the add/remove pairs are a cast and a call. What is different
// about this group is how the mirror was made to fit at all.
//
// librw's own source calls World "a bit of a stub", and it means it: the whole
// struct is an Object and three linked lists. RenderWare's has fifteen members.
// Eleven have no counterpart, and the RpAtomic answer -- drop them, and let
// reaching for one be a compile error -- does not work here, because two of the
// eleven are read by game code that has to keep compiling:
//
//     iEnv.h:30       return &r3->world->boundingBox;   (inline, so it reaches
//                                                        every unit including
//                                                        xEnv.h)
//     xFX.cpp:425     RpWorldGetNumMaterials(world)  ->  world->matList
//     zEnv.cpp:106    likewise
//
// So RpWorldPluginAttach registers the eleven as a librw World plugin, and they
// live in memory librw allocated at an offset librw handed out. That is the
// whole difference between this and appending a member to somebody else's
// malloc: the plugin block is real, and the offset is checked at attach time
// against what the struct declaration assumes rather than trusted.
//
// Two of the eight functions in this group are incomplete. RpWorldStreamRead
// reads only the bounding box and says why below;
// RpCollisionWorldForAllIntersections is blocked on the same missing piece and
// is in collision_world.cpp.

#include <rwcore.h>
#include <rpworld.h>

#include "stream.h" // brings in librw's rw.h, which must not be included twice

// Declared rather than included: librw's src/d3d/rwxbox.h has no include guard
// and redefines InstanceData, InstanceDataHeader and ObjPipeline against what
// rw.h has already pulled in. One function is all this file needs.
namespace rw
{
    namespace xbox
    {
        void registerNativeDataPlugin(void);
        void registerVertexFormatPlugin(void);
    }
}

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static inline rw::World* asWorld(RpWorld* world)
{
    return reinterpret_cast<rw::World*>(world);
}

static inline rw::Light* asLight(RpLight* light)
{
    return reinterpret_cast<rw::Light*>(light);
}

static inline rw::Camera* asCamera(RwCamera* camera)
{
    return reinterpret_cast<rw::Camera*>(camera);
}

// The tail of RpWorld -- everything RenderWare has that librw does not.
static const rw::int32 kWorldTailOffset = (rw::int32)offsetof(RpWorld, flags);
static const rw::int32 kWorldTailSize = (rw::int32)(sizeof(RpWorld) - offsetof(RpWorld, flags));

// ---------------------------------------------------------------------------
// The plugin
//
// Registered under ID_WORLD, which is the world object's own chunk id. That is
// not a collision waiting to happen: the id namespace for plugins is per object
// type, nothing else in this port registers anything on a World, and the block
// being registered IS the world's own core fields rather than somebody's
// extension. If a second World plugin ever appears it lands after this one and
// the offset check in RpWorldPluginAttach still holds.

static void* worldTailCtor(void* object, rw::int32 offset, rw::int32 size)
{
    // Zeroed rather than left as whatever rwMalloc returned, because most of
    // these fields are never written again on this side and a caller reading
    // one must get a defined answer. `boundingBox` is the exception and
    // RpWorldCreate fills it in immediately after create returns.
    //
    // Addressed through the offset librw gave rather than through the struct,
    // because this is the one place that runs before anyone has confirmed the
    // two agree.
    memset((char*)object + offset, 0, (size_t)size);

    if (offset != kWorldTailOffset)
    {
        // RpWorldPluginAttach will report this as a failure. Touching a field
        // by name here would be the out-of-bounds write it exists to prevent.
        return object;
    }

    RpWorld* world = (RpWorld*)object;
    world->renderOrder = rpWORLDRENDERNARENDERORDER;
    reinterpret_cast<rw::MaterialList*>(&world->matList)->init();

    return object;
}

static void* worldTailDtor(void* object, rw::int32 offset, rw::int32 size)
{
    if (offset != kWorldTailOffset)
    {
        return object;
    }

    // Drops a reference on every material the world holds, which is what
    // RenderWare's RpWorldDestroy does with its material list. Always a no-op
    // today -- the list only ever gets filled by a world stream reader, and
    // there is not one -- but it is the correct teardown for when there is.
    RpWorld* world = (RpWorld*)object;
    reinterpret_cast<rw::MaterialList*>(&world->matList)->deinit();

    return object;
}

static void* worldTailCopy(void* dst, void* src, rw::int32 offset, rw::int32 size)
{
    // librw only copies plugin blocks for objects it can clone, and it cannot
    // clone a World. Kept correct anyway, because a wrong copy here would be
    // the silent kind: the clone would share the original's material array and
    // both would free it.
    memcpy((char*)dst + offset, (char*)src + offset, (size_t)size);
    if (offset == kWorldTailOffset)
    {
        reinterpret_cast<rw::MaterialList*>(&((RpWorld*)dst)->matList)->init();
    }
    return dst;
}

// Not on the 112-function list, but iSystem.cpp's RWAttachPlugins already calls
// it (line 210) in exactly the window it needs -- between RwEngineInit and
// RwEngineOpen -- for the same reason RpSkinPluginAttach and friends are called
// there. Nothing may create a world before this runs, and RpWorldCreate checks
// rather than assumes.
RwBool RpWorldPluginAttach(void)
{
    // **The plugins librw expects the APPLICATION to register, and nothing did.**
    //
    // A geometry's MESH HEADER does not come out of the portable part of a
    // RenderWare stream -- it is a plugin chunk, and librw skips a chunk it has
    // no plugin for. Without registerMeshPlugin, RpClumpStreamRead hands back
    // clumps whose geometries have a NULL meshHeader, and xJSP.cpp:15 walks
    // straight into it: atomic->geometry->mesh->totalIndicesInMesh faults
    // reading address 8, which is where totalIndicesInMesh sits in
    // RpMeshHeader. 78 clumps in the BOOT scene alone.
    //
    // This is not a librw bug. Its own tools do exactly this --
    // tools/clumpview/main.cpp:261 -- because a plugin costs space on every
    // object and only the application knows which it needs. What the port had
    // instead was a shim that registered the plugins the GAME names
    // (RpSkinPluginAttach and friends) and none of the ones RenderWare links in
    // for you, which is a seam the game cannot see across.
    //
    // Registered here because RpWorldPluginAttach is the first attach
    // iSystem.cpp's RWAttachPlugins makes, and all of these must land between
    // RwEngineInit and RwEngineOpen. Engine::init already registers the
    // per-platform ones (xbox::registerPlatformPlugins and friends), so those
    // are not repeated.
    rw::registerMeshPlugin();
    rw::registerNativeDataPlugin();
    rw::registerAtomicRightsPlugin();
    rw::registerMaterialRightsPlugin();
    rw::xbox::registerVertexFormatPlugin();
    rw::registerUVAnimPlugin();

    // Idempotent within one engine lifetime, and correctly re-registering after
    // a term/init cycle: PluginList::close frees every plugin at RwEngineTerm
    // and getPluginOffset then answers -1 again.
    rw::int32 offset = rw::World::getPluginOffset(rw::ID_WORLD);
    if (offset < 0)
    {
        offset = rw::World::registerPlugin(kWorldTailSize, rw::ID_WORLD, worldTailCtor,
                                           worldTailDtor, worldTailCopy);
    }

    // The declaration in rpworld.h puts the tail at sizeof(rw::World); librw
    // hands out sizeof(rw::World) for the first plugin registered. If those two
    // ever disagree, every field below RpWorld::flags is being read out of the
    // wrong bytes, so the attach fails rather than the game running on them.
    return offset == kWorldTailOffset;
}

// ---------------------------------------------------------------------------

RpWorld* RpWorldCreate(RwBBox* boundingBox)
{
    if (rw::World::getPluginOffset(rw::ID_WORLD) != kWorldTailOffset)
    {
        // Creating the world anyway would hand back an object whose matList and
        // boundingBox are somebody else's memory.
        return NULL;
    }

    // librw's World::create takes a BBox and then never looks at it -- it has
    // no field to put one in, which is the whole reason for the plugin block.
    // The caller's box is copied in below instead.
    rw::World* world = rw::World::create();
    if (world == NULL)
    {
        return NULL;
    }

    RpWorld* result = reinterpret_cast<RpWorld*>(world);

    // RenderWare builds a single root sector spanning this box here, and every
    // sector-based thing the world does starts from it. There is no sector code
    // on this side (see RpWorldStreamRead below), so the box is recorded and
    // rootSector stays NULL. iEnvGetBBox is the one reader and it gets the
    // right answer; iEnv.cpp:61 is the one caller that passes a real box, and
    // it passes an inverted one (1000..-1000) that RenderWare would have
    // treated as empty too.
    if (boundingBox != NULL)
    {
        result->boundingBox = *boundingBox;
    }

    return result;
}

// Defined in camera.cpp, which owns the list of live cameras. Declared here
// rather than in a shared header because it is the only thing the two files
// pass between them.
void rwDetachCamerasFromWorld(void* world);

RwBool RpWorldDestroy(RpWorld* world)
{
    if (world == NULL)
    {
        return FALSE;
    }

    rw::World* w = asWorld(world);

    // RenderWare leaves whatever is still in the world dangling; librw asserts
    // on it later, when the light or the clump is destroyed and finds itself
    // still pointing at freed memory. Everything reachable is detached first,
    // which turns a crash at an unrelated call site into the same outcome
    // retail has -- an object that is simply no longer in a world.
    //
    // FORLIST reads each link's successor before running the body, so removing
    // the entry it just handed over does not derail the walk.
    FORLIST(link, w->clumps)
    {
        // removeClump also takes the clump's own atomics, lights and cameras
        // back out of this world, so it has to run before the light lists are
        // walked or those entries would be visited twice.
        w->removeClump(rw::Clump::fromWorld(link));
    }

    FORLIST(link, w->localLights)
    {
        w->removeLight(rw::Light::fromWorld(link));
    }

    FORLIST(link, w->globalLights)
    {
        w->removeLight(rw::Light::fromWorld(link));
    }

    // Cameras cannot be found through the world: neither library keeps a list of
    // them, only a World* on each camera. camera.cpp keeps that list instead --
    // see the comment on the register there -- and this is what it is for.
    //
    // The game does NOT reliably remove its cameras first, whatever the earlier
    // note here claimed. iCamera.cpp:54 and zGame.cpp:1476 do, but
    // xShadowSetWorld adds the shadow camera to every scene's world and nothing
    // ever takes it out, so from the first scene change onwards it pointed at a
    // freed world. RpWorldAddCamera then refused to add it to the new one --
    // it was already in a world, as far as the pointer said -- and the d3d9
    // lighting path dereferenced the corpse.
    rwDetachCamerasFromWorld(w);

    w->destroy();
    return TRUE;
}

RpWorld* RpWorldAddCamera(RpWorld* world, RwCamera* camera)
{
    if (world == NULL || camera == NULL)
    {
        return NULL;
    }

    // librw asserts the camera is in no world; RenderWare's own precondition is
    // the same. Answered as a failure here rather than as an abort, because a
    // double add is a game bug and dropping the process is a poor way to
    // report it.
    if (camera->world != NULL)
    {
        return NULL;
    }

    // Also syncs the camera's frame, so that the frustum planes are valid
    // against the world straight away rather than one update later.
    asWorld(world)->addCamera(asCamera(camera));
    return world;
}

RpWorld* RpWorldRemoveCamera(RpWorld* world, RwCamera* camera)
{
    if (world == NULL || camera == NULL || camera->world != world)
    {
        return NULL;
    }

    asWorld(world)->removeCamera(asCamera(camera));
    return world;
}

RpWorld* RpWorldAddLight(RpWorld* world, RpLight* light)
{
    if (world == NULL || light == NULL)
    {
        return NULL;
    }

    if (light->world != NULL)
    {
        return NULL;
    }

    // Which of the two lists it lands in is decided by the light's type, and
    // the split is the same one RenderWare makes: rpLIGHTPOINT and above have a
    // position and go in lightList, directional and ambient go in
    // directionalLightList. xLightKit.cpp adds all four kinds.
    asWorld(world)->addLight(asLight(light));
    return world;
}

RpWorld* RpWorldRemoveLight(RpWorld* world, RpLight* light)
{
    if (world == NULL || light == NULL || light->world != world)
    {
        return NULL;
    }

    asWorld(world)->removeLight(asLight(light));
    return world;
}

// ---------------------------------------------------------------------------
// RpWorldStreamRead
//
// Reads the world's bounding box and nothing else: the result is an empty world
// the size of the stream's. Every retail Xbox level is a JSP, so no retail
// asset reaches this. Mods do, with placeholder BSPs for scenes that have no
// level, and the scene needs a world regardless: iCameraAssignEnv adds the
// camera to env->world without a NULL check. A world with triangles is
// reported, since its geometry, materials and collision are all dropped.
//
// What is missing is not a shim but a subsystem. librw has no world sector code
// of any kind -- no RpWorldSector, no plane sectors, no world chunk reader, and
// World::render's own comment says "this is very wrong, we really want world
// sectors". So there is nothing to forward to and nothing to mirror onto, which
// is why RpWorldSector below keeps RenderWare's own layout unmodified: it is
// not mirrored because there is no counterpart to mirror it onto.
//
// Writing the reader means writing two separable things:
//
//   1. The portable world chunk -- header, material list, and the plane/atomic
//      sector tree with its vertices, normals, prelit colours, texture
//      coordinates and polygons. This is a documented RenderWare stream format
//      and librw's MaterialList::streamRead already covers part of it.
//
//   2. The GameCube native path, which is what BFBB's own BSPs actually are.
//      A native world's atomic sectors carry no portable geometry at all; the
//      real data is a GameCube display list in a platform extension chunk, and
//      librw has PS2, D3D and OpenGL pipelines but no GameCube one. iFX.cpp:107
//      reading _rpDlWorldVtxFmtOffset off the current world is the game's own
//      evidence for which kind it is loading.
//
// The struct chunk is 64 bytes, or 76 in older streams that put a
// surface-properties block after the origin; the bounding box is its last 24
// bytes in both.
RpWorld* RpWorldStreamRead(RwStream* stream)
{
    rw::uint32 length;
    if (stream == NULL || !rw::findChunk(stream, rw::ID_STRUCT, &length, NULL) || length < 64 ||
        length > 128)
    {
        return NULL;
    }

    rw::uint8 buf[128];
    if (stream->read32(buf, length) != length)
    {
        return NULL;
    }

    RwBBox bbox;
    memcpy(&bbox, buf + length - sizeof(bbox), sizeof(bbox));

    // numTriangles follows rootIsWorldSector and the origin in the 64-byte form.
    rw::int32 numTriangles = 0;
    if (length == 64)
    {
        memcpy(&numTriangles, buf + 16, sizeof(numTriangles));
    }
    if (numTriangles != 0)
    {
        printf("bfbb: BSP world has %d triangles; BSP geometry is not read, loading it empty\n",
               (int)numTriangles);
        fflush(stdout);
    }

    return RpWorldCreate(&bbox);
}

// Render everything in the world.
//
// **This draws the clumps and NOT the level.** librw's World::render walks the
// world's clump list and renders each atomic; RenderWare's descends the world's
// sector tree and renders the static geometry in each sector it decides is
// visible. librw has no sectors -- its own comment on this function is "this is
// very wrong, we really want world sectors" -- so everything that would have
// come out of the tree is missing.
//
// For BFBB that is less bad than it sounds, and the reason is in docs/PCPORT.md: the
// Xbox assets carry no BSP at all. A JSP level's RpWorld is the empty one
// iEnv.cpp:61 creates with RpWorldCreate(&tmpbbox), and the level's geometry
// lives in atomics that xJSP renders itself. So the sectors this cannot walk
// are, on this asset set, always empty.
//
// It is still forwarded rather than refused, because iEnv.cpp:197 calls it on
// every environment render and a refusal would lose whatever HAS been added to
// the world -- lights are attached to it, and RpWorldAddClump is how a
// non-JSP model would get drawn if anything used it.
RpWorld* RpWorldRender(RpWorld* world)
{
    if (world == NULL)
    {
        return NULL;
    }

    asWorld(world)->render();
    return world;
}
