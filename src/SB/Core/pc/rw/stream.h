#ifndef SB_CORE_PC_RW_STREAM_H
#define SB_CORE_PC_RW_STREAM_H

// The port's definition of RwStream.
//
// Every other object in this directory is mirrored: RwFrame, RwRaster and the
// rest are declared in include/rwsdk with librw's field order under
// RenderWare's field names, so the two pointers are the same bytes. RwStream
// cannot be, and not because the fields are in a different order --
// RenderWare's stream is a POD tagged union and librw's is an abstract class
// with a vtable. Naming librw's vptr `type` would be a lie the layout
// assertions could not catch.
//
// So rwsdk leaves the type incomplete on PC and the shim defines it here, as
// something that IS an rw::Stream. Game code cannot touch its fields (that is
// now a compile error, which is the point), and the shim's own conversions go
// through the base class rather than a reinterpret_cast, so the compiler
// checks them.
//
// Include this from any shim file that has to hand an RwStream* to librw --
// INSTEAD of "rw.h", not as well as. librw's rw.h has no include guard, so
// including it twice redefines every type in it.

#include <rwcore.h>

#include "rw.h"

struct RwStream : public rw::Stream
{
    // RenderWare's RwStreamClose takes a second argument the stream writes its
    // result into -- for a memory stream, the RwMemory naming the block and how
    // much of it was used. librw has no equivalent, and what belongs there
    // depends on what kind of stream this is, so each kind answers for itself.
    //
    // Also the last chance to free anything the stream still owns: called
    // exactly once, immediately before the stream is destroyed.
    virtual void close_(void* pData) = 0;
};

#endif
