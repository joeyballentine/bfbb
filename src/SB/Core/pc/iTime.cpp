#include "iTime.h"
#include "iHost.h"
#include "iSystem.h"

#include <types.h>

#include <string.h>

static iTime sStartupTime;
static F32 sGameTime;

// A monotonic clock, not the wall clock: retail reads the GameCube timebase,
// which no user action can move. Wall-clock jumps (NTP, DST, the user editing
// the clock) would otherwise show up as a single enormous frame delta. Which
// clock that is on this host is iHost's problem.
static iTime iTicksNow()
{
    return (iTime)(iHostMonotonicNs() / (1000000000ULL / ITIME_TICKS_PER_SECOND));
}

void iTimeInit()
{
    sStartupTime = iTicksNow();
}

void iTimeExit()
{
}

iTime iTimeGet()
{
    return iTicksNow() - sStartupTime;
}

F32 iTimeDiffSec(iTime time)
{
    return (F32)time / (GET_BUS_FREQUENCY() / 4);
}

F32 iTimeDiffSec(iTime t0, iTime t1)
{
    return iTimeDiffSec(t1 - t0);
}

void iTimeGameAdvance(F32 elapsed)
{
    sGameTime += elapsed;
}

void iTimeSetGame(F32 time)
{
    sGameTime = time;
}

// The GameCube build reads the calendar through OSTicksToCalendarTime; on a
// host it is whatever reentrant local-time call that host has. These four live
// in iSystem.cpp on the GameCube side purely because that is where the OS calls
// were -- the declarations have always been in iTime.h, so they sit with the
// rest of the clock here.

static iHostCalendar iCalendarNow()
{
    iHostCalendar out;
    iHostLocalTime(&out);
    return out;
}

S32 iGetMinute()
{
    return iCalendarNow().min;
}

S32 iGetHour()
{
    return iCalendarNow().hour;
}

S32 iGetDay()
{
    return iCalendarNow().mday;
}

S32 iGetMonth()
{
    return iCalendarNow().mon + 1;
}

static const char* months[] = { "January ",   "February ", "March ",    "April ",
                                "May ",       "June ",     "July ",     "August ",
                                "September ", "October ",  "November ", "December " };

static const char* dotw[] = { "Sunday ",   "Monday ", "Tuesday ", "Wednesday ",
                              "Thursday ", "Friday ", "Saturday " };

U32 iGetCurrFormattedDate(char* str)
{
    char* start = str;
    iHostCalendar td = iCalendarNow();

    strcpy(str, dotw[td.wday]);
    strcat(str, months[td.mon]);
    str += strlen(str);

    S32 mday = td.mday;
    if (mday >= 10)
    {
        *str++ = (mday / 10) + '0';
    }

    *str++ = (mday % 10) + '0';
    *str++ = ',';
    *str++ = ' ';

    // Retail writes the third digit as (year / 10) % 100, which is only the
    // tens digit while year < 2100 -- 2135 would emit '=' where '3' belongs.
    // Ported as % 10 because the port has no matching obligation and every
    // caller displays this to a player.
    S32 year = td.year + 1900;
    *str++ = (year / 1000) + '0';
    *str++ = ((year / 100) % 10) + '0';
    *str++ = ((year / 10) % 10) + '0';
    *str++ = (year % 10) + '0';
    *str++ = '\0';

    return str - start;
}

U32 iGetCurrFormattedTime(char* str)
{
    char* start = str;
    S32 am = 0;
    iHostCalendar td = iCalendarNow();

    S32 hour = td.hour;
    if (hour < 12)
    {
        am = 1;
    }
    else
    {
        hour -= 12;
    }

    if (hour == 0)
    {
        hour = 12;
    }

    if (hour >= 10)
    {
        *str++ = (hour / 10) + '0';
    }

    *str++ = (hour % 10) + '0';
    *str++ = ':';
    *str++ = (td.min / 10) + '0';
    *str++ = (td.min % 10) + '0';
    *str++ = ':';
    *str++ = (td.sec / 10) + '0';
    *str++ = (td.sec % 10) + '0';
    *str++ = ' ';

    if (am)
    {
        *str++ = 'A';
        *str++ = '.';
        *str++ = 'M';
        *str++ = '.';
    }
    else
    {
        *str++ = 'P';
        *str++ = '.';
        *str++ = 'M';
        *str++ = '.';
    }

    *str++ = '\0';

    return str - start;
}

// Retail's profiler was stripped from the shipping build ("Redacted. :}").
// Nothing is missing here that the GameCube build had.
void iProfileClear(U32 sceneID)
{
}

void iFuncProfileDump()
{
}

void iFuncProfileParse(char* elfPath, S32 profile)
{
}
