#ifndef BFBB_PC_COMPAT_MEM_H
#define BFBB_PC_COMPAT_MEM_H

// MSL splits the mem* family out of <string.h> into its own <mem.h>, which
// xSpline.cpp includes. There is no such header on a host; the functions are
// all in <string.h>.
//
// The GameCube build sees src/PowerPC_EABI_Support/include/mem.h instead.
#include <string.h>

#endif
