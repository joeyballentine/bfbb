#ifndef IMEMMGR_H
#define IMEMMGR_H

#include <types.h>

// The GameCube header also exports `the_heap`, an OSHeapHandle. That type is
// dolphin's and nothing outside src/SB/Core/gc reads the variable, so the host
// build does not carry it.
extern U32 mem_base_alloc;
extern U32 mem_top_alloc;

void iMemInit();
void iMemExit();

#endif
