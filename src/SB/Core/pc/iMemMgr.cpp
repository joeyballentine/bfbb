#include "iMemMgr.h"
#include "iHost.h"
#include "iSystem.h"
#include "xMemMgr.h"

#include <types.h>

#include <stdio.h>
#include <stdlib.h>

extern xMemInfo_tag gMemInfo;

U32 mem_base_alloc;
U32 mem_top_alloc;

static void* sArena;
static U32 sArenaSize;

// Retail's DRAM heap. xMemInit carves three heaps out of gMemInfo.DRAM, and
// their sizes are load-bearing: raising this changes which allocations fail,
// so it stays at the console's value until something is measured to need more.
#define IMEM_DRAM_SIZE 0x384000

// xMemInit places gxHeap[1] and gxHeap[2] at DRAM.addr + DRAM.size, each
// IMEM_DRAM_SIZE long -- that is, entirely PAST the block retail allocated for
// DRAM. On the GameCube those addresses land in the rest of the OS arena and
// nothing else ever claims them, so the overrun is invisible. Here it would be
// a wild write into whatever the host allocator put next, so the arena is
// reserved at twice the size and the second half is left for those two heaps.
#define IMEM_ARENA_SIZE (IMEM_DRAM_SIZE * 2)

// gMemInfo.DRAM.addr is a U32, and xMemInitHeap does pointer arithmetic on it
// as an integer. Every address the game allocator hands out must therefore fit
// in 32 bits and survive the round trip back to a pointer. On a 64-bit host
// that is not true of malloc, so the arena comes from iHostReserveLow, which
// asks the OS for a low address by whatever means that OS offers.
void iMemInit()
{
    sArenaSize = IMEM_ARENA_SIZE;
    sArena = iHostReserveLow(sArenaSize);

    if (sArena == NULL)
    {
        fprintf(stderr, "iMemInit: could not reserve a %u byte arena\n", sArenaSize);
        exit(-5);
    }

    // Not an optional check. If the host handed back an address above 4 GB the
    // truncation below would silently produce a heap base that points at the
    // wrong memory, and the failure would surface much later as corruption.
    unsigned long long base64 = (unsigned long long)(uintptr_t)sArena;
    if (base64 + sArenaSize > 0x100000000ULL)
    {
        fprintf(stderr,
                "iMemInit: arena at %#llx is outside the low 4 GB; the game "
                "allocator addresses memory with U32 and cannot use it\n",
                base64);
        exit(-5);
    }

    U32 base = (U32)base64;
    mem_base_alloc = base;
    mem_top_alloc = base + IMEM_DRAM_SIZE;

    // Retail describes the console's fixed memory map here. On a host there is
    // no OS region at address 0 and no separate audio RAM, but xMemMgr reads
    // DRAM, and the remaining three are reported by the debug memory display,
    // so they are filled in with what they mean rather than left as garbage.
    gMemInfo.system.addr = 0;
    gMemInfo.system.size = 0;
    gMemInfo.system.flags = 0x20;

    gMemInfo.stack.addr = 0;
    gMemInfo.stack.size = 0;
    gMemInfo.stack.flags = 0x820;

    gMemInfo.DRAM.addr = base;
    gMemInfo.DRAM.size = IMEM_DRAM_SIZE;
    gMemInfo.DRAM.flags = 0x820;

    // ARAM. The host has no equivalent; xSndMgr is what read it.
    gMemInfo.SRAM.addr = 0;
    gMemInfo.SRAM.size = 0;
    gMemInfo.SRAM.flags = 0x660;
}

void iMemExit()
{
    if (sArena != NULL)
    {
        iHostRelease(sArena, sArenaSize);
        sArena = NULL;
    }
    gMemInfo.DRAM.addr = 0;
}
