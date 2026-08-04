#include "types.h"

static u32 next = 1;

int rand()
{
    return (((next = next * 1103515245 + 12345) >> 16) & 0x7fff);
}

void srand(u32 seed)
{
    next = seed;
}
