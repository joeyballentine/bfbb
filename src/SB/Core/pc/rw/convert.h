#ifndef RW_CONVERT_H
#define RW_CONVERT_H

// Shim-internal. See convert.cpp for what this is for and why librw does not do
// it itself.
//
// Both take librw types rather than the RenderWare ones because only the shim
// calls them, and the callers already hold the librw object.

namespace rw
{
    struct Atomic;
    struct Clump;
}

// Convert one atomic's geometry off the platform it was authored for, if it
// needs it. Safe to call on anything: a geometry that is already portable, or
// that was authored for the platform being rendered with, is left alone.
void rwConvertAtomicToCurrentPlatform(rw::Atomic* atomic);

// Every atomic in the clump.
void rwConvertClumpToCurrentPlatform(rw::Clump* clump);

#endif
