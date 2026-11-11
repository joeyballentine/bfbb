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
// librw has no world sectors, so the sector tree a world stream carries is the
// port's own (sector.h): RpWorldStreamRead builds it, RpWorldRender draws it,
// and collision_world.cpp queries it.

#include <rwcore.h>
#include <rpworld.h>

#include "sector.h"
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

static void destroySectorTree(RpSector* sector)
{
    if (sector == NULL)
    {
        return;
    }

    if (sector->type == rwSECTORATOMIC)
    {
        rwshim::AtomicSector* atomicSector = (rwshim::AtomicSector*)sector;
        if (atomicSector->atomic != NULL)
        {
            rw::Frame* frame = atomicSector->atomic->getFrame();
            atomicSector->atomic->destroy();
            frame->destroy();
        }
        delete[] atomicSector->sector.polygons;
        delete atomicSector;
        return;
    }

    RpPlaneSector* plane = (RpPlaneSector*)sector;
    destroySectorTree(plane->leftSubTree);
    destroySectorTree(plane->rightSubTree);
    delete plane;
}

static void* worldTailDtor(void* object, rw::int32 offset, rw::int32 size)
{
    if (offset != kWorldTailOffset)
    {
        return object;
    }

    // The sectors first: their geometries hold references on the world's
    // materials.
    RpWorld* world = (RpWorld*)object;
    destroySectorTree(world->rootSector);
    world->rootSector = NULL;
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

    // RenderWare builds a single empty root sector spanning this box here.
    // Nothing reads it -- a created world is a JSP level's, whose geometry and
    // collision live in the JSP -- so rootSector stays NULL and the world's
    // sector walks find nothing. iEnv.cpp:61 is the one caller that passes a
    // real box, and it passes an inverted one (1000..-1000) that RenderWare
    // would have treated as empty too.
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
// Reads a world stream the way GameCube's RenderWare does (RpWorldStreamRead,
// PlaneSectorStreamRead and WorldSectorStreamRead in the retail DOL): versions
// 3.1 to 3.5, the plane and atomic sector tree, and each atomic sector's
// vertices, prelight, texture coordinates, polygons and meshes.
//
// Only the portable format is read. Retail ships no BSP, and every one in mods
// is portable: BFBB Deluxe's hbc1 (RenderWare 3.3) and the SEE mod's
// placeholders (3.1 and 3.4). A native GameCube world carries display lists in
// place of polygons and is refused.
//
// Each atomic sector with polygons gets a librw atomic over a geometry made
// from its arrays and its BINMESH meshes, which RpWorldRender draws. The
// RpWorldSector points into that geometry, so collision tests the triangles
// that are drawn.

static const rw::uint32 kWorldVersionMin = 0x31000;
static const rw::uint32 kWorldVersionMax = 0x35000;

// The world chunk's STRUCT. Up to 3.4.0.0 it has surface properties after the
// origin and no bounding box (52 bytes); after that, the reverse (64 bytes).
struct WorldStreamHeader
{
    RwInt32 rootIsWorldSector;
    RwV3d inverseOrigin;
    RwInt32 numTriangles;
    RwInt32 numVertices;
    RwInt32 numPlaneSectors;
    RwInt32 numWorldSectors;
    RwInt32 colSectorSize;
    RwUInt32 format;
    RwBBox boundingBox;
};

struct PlaneSectorStreamHeader
{
    RwInt32 type;
    RwReal value;
    RwInt32 leftIsWorldSector;
    RwInt32 rightIsWorldSector;
    RwReal leftValue;
    RwReal rightValue;
};

struct AtomicSectorStreamHeader
{
    RwInt32 matListWindowBase;
    RwInt32 numPolygons;
    RwInt32 numVertices;
    RwV3d inf;
    RwV3d sup;
    RwInt32 collSectorPresent;
    RwInt32 unused;
};

struct SectorReader
{
    rw::Stream* stream;
    RpWorld* world;
    RwUInt32 format;
    RwInt32 numTexCoordSets;
};

// The format bits a sector's geometry takes over. They have the same values in
// RpWorldFlag and rw::Geometry::Flags.
static const rw::uint32 kSectorGeometryFlags =
    rw::Geometry::TRISTRIP | rw::Geometry::TEXTURED | rw::Geometry::PRELIT | rw::Geometry::NORMALS |
    rw::Geometry::LIGHT | rw::Geometry::MODULATE | rw::Geometry::TEXTURED2;

static const rw::uint32 kAtomicSectorChunk = 0x9;
static const rw::uint32 kPlaneSectorChunk = 0xA;
static const rw::uint32 kCollisionPluginChunk = 0x11D;

static RpSector* readSector(SectorReader* reader, bool isAtomic);

static bool skipBytes(rw::Stream* stream, rw::uint32 count)
{
    stream->seek((rw::int32)count);
    return !stream->eof() || count == 0;
}

// The BINMESH chunk: a header, then each mesh's index count, its index into
// the world's material list and its indices as 32-bit values.
static bool readSectorMeshes(rw::Stream* stream, rw::Geometry* geo, rw::uint32 length)
{
    struct
    {
        rw::uint32 flags;
        rw::uint32 numMeshes;
        rw::uint32 totalIndices;
    } header;

    if (length < sizeof(header) || stream->read32(&header, sizeof(header)) != sizeof(header) ||
        header.numMeshes > 0xFFFF ||
        length < sizeof(header) + header.numMeshes * 8 + header.totalIndices * 4)
    {
        return false;
    }

    rw::MeshHeader* meshHeader = geo->allocateMeshes(header.numMeshes, header.totalIndices, FALSE);
    meshHeader->flags = header.flags;

    rw::Mesh* mesh = meshHeader->getMeshes();
    rw::uint16* indices = mesh->indices;
    rw::uint32 indicesLeft = header.totalIndices;
    for (rw::uint32 i = 0; i < header.numMeshes; i++, mesh++)
    {
        rw::uint32 numIndices = stream->readU32();
        rw::int32 matIndex = stream->readI32();
        if (numIndices > indicesLeft || matIndex < 0 || matIndex >= geo->matList.numMaterials)
        {
            return false;
        }

        mesh->numIndices = numIndices;
        mesh->material = geo->matList.materials[matIndex];
        mesh->indices = indices;
        for (rw::uint32 j = 0; j < numIndices; j++)
        {
            rw::uint32 index = stream->readU32();
            if (index >= (rw::uint32)geo->numVertices)
            {
                return false;
            }
            indices[j] = (rw::uint16)index;
        }
        indices += numIndices;
        indicesLeft -= numIndices;
    }

    return skipBytes(stream, length - sizeof(header) - header.numMeshes * 8 -
                                 (header.totalIndices - indicesLeft) * 4);
}

// The vertex arrays and polygons of an atomic sector's STRUCT, after its
// header, into a new geometry.
static rw::Geometry* readSectorGeometry(SectorReader* reader,
                                        const AtomicSectorStreamHeader& header, RpPolygon* polygons)
{
    rw::Stream* stream = reader->stream;
    rw::int32 numVertices = header.numVertices;
    rw::uint32 flags = rw::Geometry::POSITIONS | (reader->format & kSectorGeometryFlags) |
                       ((rw::uint32)reader->numTexCoordSets << 16);

    rw::Geometry* geo = rw::Geometry::create(numVertices, header.numPolygons, flags);
    if (geo == NULL)
    {
        return NULL;
    }

    rw::MorphTarget* morphTarget = &geo->morphTargets[0];
    stream->read32(morphTarget->vertices, numVertices * sizeof(rw::V3d));

    if (reader->format & rpWORLDNORMALS)
    {
        for (rw::int32 i = 0; i < numVertices; i++)
        {
            RpVertexNormal packed;
            stream->read8(&packed, sizeof(packed));
            morphTarget->normals[i].x = packed.x / 128.0f;
            morphTarget->normals[i].y = packed.y / 128.0f;
            morphTarget->normals[i].z = packed.z / 128.0f;
        }
    }

    if (reader->format & rpWORLDPRELIT)
    {
        stream->read8(geo->colors, numVertices * sizeof(rw::RGBA));
    }

    for (rw::int32 i = 0; i < reader->numTexCoordSets; i++)
    {
        stream->read32(geo->texCoords[i], numVertices * sizeof(rw::TexCoords));
    }

    stream->read16(polygons, header.numPolygons * sizeof(RpPolygon));

    rw::int32 numMaterials = reader->world->matList.numMaterials;
    for (rw::int32 i = 0; i < header.numPolygons; i++)
    {
        const RpPolygon& polygon = polygons[i];
        if (polygon.matIndex >= numMaterials || polygon.vertIndex[0] >= numVertices ||
            polygon.vertIndex[1] >= numVertices || polygon.vertIndex[2] >= numVertices)
        {
            geo->destroy();
            return NULL;
        }

        geo->triangles[i].v[0] = polygon.vertIndex[0];
        geo->triangles[i].v[1] = polygon.vertIndex[1];
        geo->triangles[i].v[2] = polygon.vertIndex[2];
        geo->triangles[i].matId = polygon.matIndex;
    }

    // The whole world list, so that a mesh's material index is the world's.
    rw::MaterialList* worldMaterials = reinterpret_cast<rw::MaterialList*>(&reader->world->matList);
    for (rw::int32 i = 0; i < worldMaterials->numMaterials; i++)
    {
        geo->matList.appendMaterial(worldMaterials->materials[i]);
    }

    return geo;
}

static RpSector* readAtomicSector(SectorReader* reader)
{
    rw::Stream* stream = reader->stream;
    rw::uint32 length;
    AtomicSectorStreamHeader header;
    if (!rw::findChunk(stream, rw::ID_STRUCT, &length, NULL) || length < sizeof(header) ||
        stream->read32(&header, sizeof(header)) != sizeof(header))
    {
        return NULL;
    }

    rw::uint32 vertexSize = sizeof(RwV3d) + sizeof(RwTexCoords) * reader->numTexCoordSets;
    if (reader->format & rpWORLDNORMALS)
    {
        vertexSize += sizeof(RpVertexNormal);
    }
    if (reader->format & rpWORLDPRELIT)
    {
        vertexSize += sizeof(RwRGBA);
    }

    if (header.numVertices < 0 || header.numVertices > 0xFFFF || header.numPolygons < 0 ||
        header.numPolygons > 0xFFFF ||
        length < sizeof(header) + header.numVertices * vertexSize +
                     header.numPolygons * sizeof(RpPolygon))
    {
        return NULL;
    }

    rwshim::AtomicSector* atomicSector = new rwshim::AtomicSector();
    RpWorldSector* sector = &atomicSector->sector;
    sector->type = rwSECTORATOMIC;
    sector->matListWindowBase = (RwUInt16)header.matListWindowBase;
    sector->numVertices = (RwUInt16)header.numVertices;
    sector->numPolygons = (RwUInt16)header.numPolygons;
    sector->tightBoundingBox.inf = header.inf;
    sector->tightBoundingBox.sup = header.sup;
    rwLinkListInitialize(&sector->collAtomicsInWorldSector);
    rwLinkListInitialize(&sector->noCollAtomicsInWorldSector);
    rwLinkListInitialize(&sector->lightsInWorldSector);

    rw::Geometry* geo = NULL;
    rw::uint32 dataSize = 0;
    if (header.numPolygons > 0)
    {
        sector->polygons = new RpPolygon[header.numPolygons];
        geo = readSectorGeometry(reader, header, sector->polygons);
        if (geo == NULL)
        {
            destroySectorTree((RpSector*)atomicSector);
            return NULL;
        }

        dataSize = header.numVertices * vertexSize + header.numPolygons * sizeof(RpPolygon);
        sector->vertices = (RwV3d*)geo->morphTargets[0].vertices;
        sector->preLitLum = (RwRGBA*)geo->colors;
        for (RwInt32 i = 0; i < reader->numTexCoordSets; i++)
        {
            sector->texCoords[i] = (RwTexCoords*)geo->texCoords[i];
        }
    }

    // A sector without polygons still carries its arrays if it has vertices;
    // nothing reads them.
    bool ok = skipBytes(stream, length - sizeof(header) - dataSize);

    rw::uint32 extensionLength = 0;
    ok = ok && rw::findChunk(stream, rw::ID_EXTENSION, &extensionLength, NULL);
    while (ok && extensionLength >= 12)
    {
        rw::ChunkHeaderInfo chunk;
        ok = rw::readChunkHeaderInfo(stream, &chunk) && chunk.length <= extensionLength - 12;
        if (!ok)
        {
            break;
        }

        if (chunk.type == rw::ID_MESH && geo != NULL)
        {
            ok = readSectorMeshes(stream, geo, chunk.length);
        }
        else
        {
            if (chunk.type == kCollisionPluginChunk)
            {
                atomicSector->collides = true;
            }
            ok = skipBytes(stream, chunk.length);
        }
        extensionLength -= 12 + chunk.length;
    }

    if (ok && geo != NULL && geo->meshHeader != NULL)
    {
        geo->calculateBoundingSphere();
        rw::Atomic* atomic = rw::Atomic::create();
        atomic->setFrame(rw::Frame::create());
        atomic->setGeometry(geo, 0);
        atomicSector->atomic = atomic;
    }

    // The atomic took its own reference, or the sector has nothing to draw.
    if (geo != NULL)
    {
        geo->destroy();
    }

    if (!ok)
    {
        destroySectorTree((RpSector*)atomicSector);
        return NULL;
    }

    return (RpSector*)atomicSector;
}

static RpSector* readPlaneSector(SectorReader* reader)
{
    rw::Stream* stream = reader->stream;
    rw::uint32 length;
    PlaneSectorStreamHeader header;
    if (!rw::findChunk(stream, rw::ID_STRUCT, &length, NULL) || length < sizeof(header) ||
        stream->read32(&header, sizeof(header)) != sizeof(header) ||
        !skipBytes(stream, length - sizeof(header)) ||
        (header.type != 0 && header.type != 4 && header.type != 8))
    {
        return NULL;
    }

    RpPlaneSector* plane = new RpPlaneSector();
    plane->type = header.type;
    plane->value = header.value;
    if (reader->format & rpWORLDSECTORSOVERLAP)
    {
        plane->leftValue = header.leftValue;
        plane->rightValue = header.rightValue;
    }
    else
    {
        plane->leftValue = header.value;
        plane->rightValue = header.value;
    }

    plane->leftSubTree = readSector(reader, header.leftIsWorldSector != 0);
    if (plane->leftSubTree != NULL)
    {
        plane->rightSubTree = readSector(reader, header.rightIsWorldSector != 0);
    }

    if (plane->leftSubTree == NULL || plane->rightSubTree == NULL)
    {
        destroySectorTree((RpSector*)plane);
        return NULL;
    }

    return (RpSector*)plane;
}

static RpSector* readSector(SectorReader* reader, bool isAtomic)
{
    if (isAtomic)
    {
        return rw::findChunk(reader->stream, kAtomicSectorChunk, NULL, NULL) ?
                   readAtomicSector(reader) :
                   NULL;
    }

    return rw::findChunk(reader->stream, kPlaneSectorChunk, NULL, NULL) ? readPlaneSector(reader) :
                                                                          NULL;
}

// The union of the atomic sectors' boxes, for streams that predate the world's
// own (_rpWorldFindBBox).
static void addSectorBoxes(RpSector* sector, RwBBox* box, bool* first)
{
    if (sector->type != rwSECTORATOMIC)
    {
        addSectorBoxes(((RpPlaneSector*)sector)->leftSubTree, box, first);
        addSectorBoxes(((RpPlaneSector*)sector)->rightSubTree, box, first);
        return;
    }

    const RwBBox& tight = ((RpWorldSector*)sector)->tightBoundingBox;
    if (*first)
    {
        *box = tight;
        *first = false;
        return;
    }

    box->inf.x = tight.inf.x < box->inf.x ? tight.inf.x : box->inf.x;
    box->inf.y = tight.inf.y < box->inf.y ? tight.inf.y : box->inf.y;
    box->inf.z = tight.inf.z < box->inf.z ? tight.inf.z : box->inf.z;
    box->sup.x = tight.sup.x > box->sup.x ? tight.sup.x : box->sup.x;
    box->sup.y = tight.sup.y > box->sup.y ? tight.sup.y : box->sup.y;
    box->sup.z = tight.sup.z > box->sup.z ? tight.sup.z : box->sup.z;
}

RpWorld* RpWorldStreamRead(RwStream* stream)
{
    rw::uint32 length;
    rw::uint32 version;
    if (stream == NULL || !rw::findChunk(stream, rw::ID_STRUCT, &length, &version) ||
        version < kWorldVersionMin || version > kWorldVersionMax)
    {
        return NULL;
    }

    rw::uint8 buf[64];
    if (length > sizeof(buf) || stream->read32(buf, length) != length)
    {
        return NULL;
    }

    WorldStreamHeader header;
    memset(&header, 0, sizeof(header));
    if (version <= 0x34000)
    {
        if (length < 52)
        {
            return NULL;
        }
        memcpy(&header, buf, 16);
        memcpy(&header.numTriangles, buf + 28, 24);
    }
    else
    {
        if (length < 64)
        {
            return NULL;
        }
        memcpy(&header, buf, 64);
    }

    if (header.colSectorSize > 0 || (header.format & rpWORLDNATIVE))
    {
        printf("bfbb: BSP world format %08X is not a portable RenderWare world\n",
               (unsigned)header.format);
        fflush(stdout);
        return NULL;
    }

    RpWorld* world = RpWorldCreate(&header.boundingBox);
    if (world == NULL)
    {
        return NULL;
    }

    world->flags = header.format;
    world->worldOrigin.x = -header.inverseOrigin.x;
    world->worldOrigin.y = -header.inverseOrigin.y;
    world->worldOrigin.z = -header.inverseOrigin.z;

    SectorReader reader;
    reader.stream = stream;
    reader.world = world;
    reader.format = header.format;
    reader.numTexCoordSets = (header.format >> 16) & 0xFF;
    if (reader.numTexCoordSets == 0)
    {
        reader.numTexCoordSets = (header.format & rpWORLDTEXTURED2) ? 2 :
                                 (header.format & rpWORLDTEXTURED)  ? 1 :
                                                                      0;
    }
    world->numTexCoordSets = reader.numTexCoordSets;

    if (!rw::findChunk(stream, rw::ID_MATLIST, NULL, NULL) ||
        rw::MaterialList::streamRead(stream,
                                     reinterpret_cast<rw::MaterialList*>(&world->matList)) == NULL)
    {
        RpWorldDestroy(world);
        return NULL;
    }

    world->rootSector = readSector(&reader, header.rootIsWorldSector != 0);
    if (world->rootSector == NULL)
    {
        RpWorldDestroy(world);
        return NULL;
    }

    if (version < 0x34003)
    {
        bool first = true;
        addSectorBoxes(world->rootSector, &world->boundingBox, &first);
    }

    return world;
}

static void renderSectors(RpSector* sector, rw::Camera* camera)
{
    if (sector->type != rwSECTORATOMIC)
    {
        renderSectors(((RpPlaneSector*)sector)->leftSubTree, camera);
        renderSectors(((RpPlaneSector*)sector)->rightSubTree, camera);
        return;
    }

    rw::Atomic* atomic = ((rwshim::AtomicSector*)sector)->atomic;
    if (atomic != NULL &&
        camera->frustumTestSphere(atomic->getWorldBoundingSphere()) != rw::Camera::SPHEREOUTSIDE)
    {
        atomic->render();
    }
}

// The sectors the camera can see, then the clumps added to the world.
RpWorld* RpWorldRender(RpWorld* world)
{
    if (world == NULL)
    {
        return NULL;
    }

    if (world->rootSector != NULL && rw::engine->currentCamera != NULL)
    {
        renderSectors(world->rootSector, rw::engine->currentCamera);
    }

    asWorld(world)->render();
    return world;
}
