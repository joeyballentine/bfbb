#ifndef ITIME_H
#define ITIME_H

#include <types.h>

#define JANUARY 1
#define FEBRUARY 2
#define MARCH 3
#define APRIL 4
#define MAY 5
#define JUNE 6
#define JULY 7
#define AUGUST 8
#define SEPTEMBER 9
#define OCTOBER 10
#define NOVEMBER 11
#define DECEMBER 12

typedef S64 iTime;

// The GameCube's iTime is a raw timebase tick: the bus clock divided by four,
// which is why game code converts with GET_BUS_FREQUENCY() / 4 rather than
// calling iTimeDiffSec. Five files in src/SB do that arithmetic inline, so the
// host tick rate is not free -- GET_BUS_FREQUENCY() in iSystem.h is defined
// from this constant so that those five sites stay correct without edits.
//
// A microsecond tick keeps F32 precision on par with retail's 40.5 MHz one
// while staying exactly representable.
#define ITIME_TICKS_PER_SECOND 1000000

S32 iGetMinute();
S32 iGetHour();
S32 iGetDay();
S32 iGetMonth();
U32 iGetCurrFormattedDate(char* input);
U32 iGetCurrFormattedTime(char* input);
void iTimeInit();
void iTimeExit();
iTime iTimeGet();
F32 iTimeDiffSec(iTime t0, iTime t1);
F32 iTimeDiffSec(iTime time);
void iTimeGameAdvance(F32 elapsed);
void iTimeSetGame(F32 time);
void iProfileClear(U32 sceneID);
void iFuncProfileDump();
void iFuncProfileParse(char* elfPath, S32 profile);

#endif
