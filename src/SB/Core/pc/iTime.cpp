#include "iTime.h"
#include "iSystem.h"

#include <types.h>

#include <string.h>
#include <time.h>

static iTime sStartupTime;
static F32 sGameTime;

// CLOCK_MONOTONIC, not the wall clock: retail reads the GameCube timebase,
// which no user action can move. Wall-clock jumps (NTP, DST, the user editing
// the clock) would otherwise show up as a single enormous frame delta.
static iTime iHostTicks()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (iTime)ts.tv_sec * ITIME_TICKS_PER_SECOND +
           (iTime)ts.tv_nsec / (1000000000 / ITIME_TICKS_PER_SECOND);
}

void iTimeInit()
{
    sStartupTime = iHostTicks();
}

void iTimeExit()
{
}

iTime iTimeGet()
{
    return iHostTicks() - sStartupTime;
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
// host that is localtime_r. These four live in iSystem.cpp on the GameCube
// side purely because that is where the OS calls were -- the declarations have
// always been in iTime.h, so they sit with the rest of the clock here.

static tm iHostCalendar()
{
    time_t now = time(NULL);
    tm out;
    localtime_r(&now, &out);
    return out;
}

S32 iGetMinute()
{
    return iHostCalendar().tm_min;
}

S32 iGetHour()
{
    return iHostCalendar().tm_hour;
}

S32 iGetDay()
{
    return iHostCalendar().tm_mday;
}

S32 iGetMonth()
{
    return iHostCalendar().tm_mon + 1;
}

static const char* months[] = { "January ",   "February ", "March ",    "April ",
                                "May ",       "June ",     "July ",     "August ",
                                "September ", "October ",  "November ", "December " };

static const char* dotw[] = {
    "Sunday ", "Monday ", "Tuesday ", "Wednesday ", "Thursday ", "Friday ", "Saturday "
};

U32 iGetCurrFormattedDate(char* str)
{
    char* start = str;
    tm td = iHostCalendar();

    strcpy(str, dotw[td.tm_wday]);
    strcat(str, months[td.tm_mon]);
    str += strlen(str);

    S32 mday = td.tm_mday;
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
    S32 year = td.tm_year + 1900;
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
    tm td = iHostCalendar();

    S32 hour = td.tm_hour;
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
    *str++ = (td.tm_min / 10) + '0';
    *str++ = (td.tm_min % 10) + '0';
    *str++ = ':';
    *str++ = (td.tm_sec / 10) + '0';
    *str++ = (td.tm_sec % 10) + '0';
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
