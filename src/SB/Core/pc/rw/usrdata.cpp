// RenderWare C API: RpUserData, the geometry half of it.
//
// plugins.cpp says of RpUserDataPluginAttach that "no game code calls an
// RpHAnim* or RpUserData* function on the PC path". That was true when it was
// written and is not any more: iMorph.cpp is the first and only caller, and it
// is a real one -- it keeps its DirtyMorph cache in a "MORPHSTATE" integer
// array hung off the geometry (iMorph.cpp:241-246), and reads it back on every
// morph render (iMorph.cpp:58).
//
// So the plugin is no longer attached for stream fidelity alone, and the note
// in plugins.cpp has been corrected.
//
// There is no RenderWare source to work from here: src/rwsdk/plugin/userdata/
// rpusrdat.c is a two-line include-only stub, because the DOL takes this plugin
// from the retail object. librw has the whole thing though, and the two agree
// closely enough that these are casts and calls:
//
//   RpUserDataArray { RwChar* name; RpUserDataFormat format; RwInt32 numElements; void* data; }
//   rw::UserDataArray { char* name;  uint32 datatype;         int32 numElements;  void* data; }
//
// The format enumeration matches value for value, which layout_usrdata below
// asserts rather than assumes -- a renumbering would store the morph cache as
// the wrong type and read it back as garbage, with nothing to catch it.

#include <rwcore.h>
#include <rpworld.h>
#include <rpusrdat.h>

#include "rw.h"

#include <stddef.h>

// --- the layout claims these functions rest on ----------------------------

static_assert(sizeof(RpUserDataArray) == sizeof(rw::UserDataArray),
              "RpUserDataArray and rw::UserDataArray differ in size");
static_assert(offsetof(RpUserDataArray, name) == offsetof(rw::UserDataArray, name),
              "user data name moved");
static_assert(offsetof(RpUserDataArray, format) == offsetof(rw::UserDataArray, datatype),
              "user data format is not where librw's datatype is");
static_assert(offsetof(RpUserDataArray, numElements) == offsetof(rw::UserDataArray, numElements),
              "user data numElements moved");
static_assert(offsetof(RpUserDataArray, data) == offsetof(rw::UserDataArray, data),
              "user data data moved");

static_assert((int)rpNAUSERDATAFORMAT == (int)rw::USERDATANA, "user data format NA renumbered");
static_assert((int)rpINTUSERDATA == (int)rw::USERDATAINT, "user data format INT renumbered");
static_assert((int)rpREALUSERDATA == (int)rw::USERDATAFLOAT, "user data format REAL renumbered");
static_assert((int)rpSTRINGUSERDATA == (int)rw::USERDATASTRING,
              "user data format STRING renumbered");

static inline rw::Geometry* asGeometry(const RpGeometry* geometry)
{
    return const_cast<rw::Geometry*>(reinterpret_cast<const rw::Geometry*>(geometry));
}

RwInt32 RpGeometryAddUserDataArray(RpGeometry* geometry, RwChar* name, RpUserDataFormat format,
                                   RwInt32 numElements)
{
    if (geometry == NULL || name == NULL || numElements <= 0)
    {
        // RenderWare returns the new array's INDEX, so there is no in-band
        // failure value that is not also a valid answer. -1 is what the one
        // caller checks against.
        return -1;
    }

    return rw::UserDataArray::geometryAdd(asGeometry(geometry), name, (rw::int32)format,
                                          numElements);
}

RwInt32 RpGeometryGetUserDataArrayCount(const RpGeometry* geometry)
{
    if (geometry == NULL)
    {
        return 0;
    }

    return rw::UserDataArray::geometryGetCount(asGeometry(geometry));
}

RpUserDataArray* RpGeometryGetUserDataArray(const RpGeometry* geometry, RwInt32 data)
{
    if (geometry == NULL || data < 0)
    {
        return NULL;
    }

    return reinterpret_cast<RpUserDataArray*>(
        rw::UserDataArray::geometryGet(asGeometry(geometry), data));
}

// Not called by anything on the PC path today, and written anyway because the
// header declares it and a caller reaching for it should get the right answer
// rather than a link error. The sizes are RenderWare's own: an int and a float
// are four bytes, a string is a pointer to one.
RwInt32 RpUserDataGetFormatSize(RpUserDataFormat format)
{
    switch (format)
    {
    case rpINTUSERDATA:
        return sizeof(RwInt32);
    case rpREALUSERDATA:
        return sizeof(RwReal);
    case rpSTRINGUSERDATA:
        return sizeof(RwChar*);
    default:
        return 0;
    }
}
