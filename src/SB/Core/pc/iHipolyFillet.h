#ifndef IHIPOLYFILLET_H
#define IHIPOLYFILLET_H

// Fillet the sharp folds of a tessellated world's landscape. What it does
// and what it guards against is at the top of iHipolyFillet.cpp.

#include "iHipolyTess.h"

struct iHipolyFilletParams
{
    F64 angleDeg;    // a fold between two landscape faces sharper than this is a crease
    F64 radius;      // units from a crease the rounding reaches
    U32 iters;       // Taubin passes
    F64 maxMove;     // units a vertex may move
    F64 floorMove;   // units a floor or cap vertex may move, and only down
};

struct iHipolyFilletStats
{
    U32 seeds;
    U32 band;
    U32 moved;
    U32 blocked;
    U32 sunk;
    U32 exposed;
    U32 poking;
    F64 maxMove;
};

// Move the positions of `results` (one per atomic, tessellated together) in
// place. `natural[k][t]` says triangle t of result k is landscape;
// `landable[k][t]` that the shipped tree collides with it as a floor.
void iHipolyFillet(iHipolyResult* results, U32 numResults, const U8* const* natural,
                   const U8* const* landable, const iHipolyFilletParams& params,
                   iHipolyFilletStats* stats);

#endif
