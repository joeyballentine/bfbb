#include "binkngc.h"
#include "bink.h"
#include "dolphin/os/OSAlloc.h"


RADMEMALLOC usermalloc = NULL;
RADMEMFREE userfree = NULL;
RADMEMALLOC userarammalloc = NULL;
RADMEMFREE useraramfree = NULL;



void radmemset16(void* dest, u16 value, u32 size) {
    int half_size = size >> 1;
    int sprayed_value = (value << 16) | value;
    u16* d16 = dest;
    u32* d32 = dest;
    
    while (half_size--) {
        *d32++ = sprayed_value;
    }

    d16 = (U16*)d32;
    if ((size & 1))
        *d16 = value;
}

void RADSetMemory(RADMEMALLOC malloc_fn, RADMEMFREE free_fn) {
    usermalloc = malloc_fn;
    userfree = free_fn;
}

void* radmalloc(u32 size)
{
    u32 request;
    void *rawBlock;
    u8 fromUser;
    u32 addr;
    u32 offset;
    u8 *aligned;
    if (size == 0 || size == 0xFFFFFFFF)
        return 0;
    request   = size + 0x40;
    if (usermalloc != 0 && (rawBlock = usermalloc(request))) {
        if (rawBlock != 0 && rawBlock != (void *)-1) {
            fromUser = 3;
        } else {
            return 0;
        }
    } else {
        rawBlock = OSAllocFromHeap(__OSCurrHeap, request);
        if (rawBlock == 0) {
            return 0;
        }
        fromUser= 0;
    }
    addr    = (u32)rawBlock;
    offset  = (u32)(0x40 - (addr & 0x1F)) & 0xFF;
    aligned  = (u8 *)rawBlock + offset;
    aligned[-1] = (u8)offset;   
    aligned[-2] = fromUser;        
    if (fromUser == 3)      
        *(void **)(aligned - 8) = (void *)userfree;
    return aligned;
}

void radfree(void* ptr)
{
    u8* ptrU8 = (u8*)ptr;
    u32* ptrU32 = (u32*)ptr;

    void (*customFree)(void*);
    if (ptr)
    {
        if ((ptrU8[-2]) == 3)
        {
            customFree = (void*)(ptrU32[-2]);
            customFree(ptrU8 - ptrU8[-1]);
        }
        else
        {
            OSFreeToHeap((void*)__OSCurrHeap, ptrU8 - ptrU8[-1]);
        }
    }
}

void RADSetAudioMemory(RADMEMALLOC malloc_fn, RADMEMFREE free_fn) {
    userarammalloc = malloc_fn;
    useraramfree = free_fn;
}

void* radaudiomalloc(u32 size) {
    if (userarammalloc) {
        return userarammalloc(size);
    }
    return NULL;
}

void radaudiofree(void* ptr) {
    if (useraramfree) {
        useraramfree(ptr);
    }
}

u32 mult64andshift(u32 a, u32 b, s32 shift)
{
    u64 product = (u64)a * (u64)b;
    u32 hi = (u32)(product >> 32);
    u32 lo = (u32)product;

    return (hi << (32 - shift)) | (lo >> shift);
}

void ReadTimeBase(u64* dest)
{
    __asm__ __volatile__("1: mftbu 11\n"
                         "   mftb  9\n"
                         "   mftbu 0\n"
                         "   cmpw  11,0\n"
                         "   bne   1b\n"
                         "   stw   11,0(%0)\n"
                         "   stw   9,4(%0)\n"
                         :
                         : "r"(dest)
                         : "r0", "r9", "r11", "memory");
}

void RADCycleTimerStartAddr64(u64* dest)
{
    __asm__ __volatile__("1: mftbu 11\n"
                         "   mftb  9\n"
                         "   mftbu 0\n"
                         "   cmpw  11,0\n"
                         "   bne   1b\n"
                         "   stw   11,0(%0)\n"
                         "   stw   9,4(%0)\n"
                         :
                         : "r"(dest)
                         : "r0", "r9", "r11", "memory");
}
