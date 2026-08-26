#ifndef BFBB_PC_COMPAT_STRING_H
#define BFBB_PC_COMPAT_STRING_H

// MSL's <string.h> carries the case-insensitive comparisons under their
// Microsoft spellings. POSIX has the same functions under different names.
#include_next <string.h>

#include <strings.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int stricmp(const char* a, const char* b)
{
    return strcasecmp(a, b);
}

static inline int strnicmp(const char* a, const char* b, size_t n)
{
    return strncasecmp(a, b, n);
}

#ifdef __cplusplus
}
#endif

#endif
