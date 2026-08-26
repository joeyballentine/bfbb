#ifndef BFBB_PC_COMPAT_STRING_H
#define BFBB_PC_COMPAT_STRING_H

// MSL's <string.h> carries the case-insensitive comparisons under their
// Microsoft spellings. The game uses all three of stricmp, strnicmp and
// strcmpi, so the layer has to supply whichever ones the host lacks.
//
// There is no portable spelling of these. POSIX has strcasecmp/strncasecmp in
// <strings.h>; the Microsoft CRT has _stricmp/_strnicmp in <string.h> and no
// <strings.h> at all. An earlier version of this header included <strings.h>
// unconditionally, which made 145 of 198 units fail to compile on Windows for
// a reason that had nothing to do with the port.
#include_next <string.h>

#if defined(_WIN32) && !defined(__CYGWIN__)
#define BFBB_STRCASECMP _stricmp
#define BFBB_STRNCASECMP _strnicmp
#else
#include <strings.h>
#define BFBB_STRCASECMP strcasecmp
#define BFBB_STRNCASECMP strncasecmp
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline int stricmp(const char* a, const char* b)
{
    return BFBB_STRCASECMP(a, b);
}

static inline int strnicmp(const char* a, const char* b, size_t n)
{
    return BFBB_STRNCASECMP(a, b, n);
}

// The other spelling MSL carries for the same comparison. zSaveLoad.cpp uses
// this one and zEntPlayer.cpp uses stricmp.
static inline int strcmpi(const char* a, const char* b)
{
    return BFBB_STRCASECMP(a, b);
}

#ifdef __cplusplus
}
#endif

#endif
