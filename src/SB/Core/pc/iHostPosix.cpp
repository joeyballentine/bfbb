#include "iHost.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// The POSIX half of the iHost seam. See iHost.h for what each of these is for;
// this file is only the spelling.

// ---------------------------------------------------------------------------
// Time

static const U64 NS_PER_SEC = 1000000000ULL;

U64 iHostMonotonicNs()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (U64)ts.tv_sec * NS_PER_SEC + (U64)ts.tv_nsec;
}

void iHostSleepUntilNs(U64 targetNs)
{
    timespec ts;
    ts.tv_sec = (time_t)(targetNs / NS_PER_SEC);
    ts.tv_nsec = (long)(targetNs % NS_PER_SEC);

    // TIMER_ABSTIME rather than a duration, so a slow caller cannot make the
    // pacing drift by however long it took to work out the interval.
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
}

void iHostLocalTimeOf(S64 unixSeconds, iHostCalendar* out)
{
    time_t when = (time_t)unixSeconds;
    tm t;
    localtime_r(&when, &t);

    out->sec = t.tm_sec;
    out->min = t.tm_min;
    out->hour = t.tm_hour;
    out->mday = t.tm_mday;
    out->mon = t.tm_mon;
    out->year = t.tm_year;
    out->wday = t.tm_wday;
}

void iHostLocalTime(iHostCalendar* out)
{
    iHostLocalTimeOf((S64)time(NULL), out);
}

// ---------------------------------------------------------------------------
// Virtual memory

void* iHostReserveLow(U32 size)
{
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_32BIT
    // Linux x86-64: map inside the first 2 GB, where a U32 address is exact.
    flags |= MAP_32BIT;
#endif
    void* p = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (p == MAP_FAILED)
    {
        return NULL;
    }
    return p;
}

void iHostRelease(void* p, U32 size)
{
    if (p != NULL)
    {
        munmap(p, size);
    }
}

// ---------------------------------------------------------------------------
// Filesystem

bool iHostPathExists(const char* path)
{
    return access(path, F_OK) == 0;
}

bool iHostStat(const char* path, iHostFileInfo* out)
{
    struct stat st;
    if (stat(path, &st) != 0)
    {
        return false;
    }

    out->is_dir = S_ISDIR(st.st_mode) != 0;
    out->is_file = S_ISREG(st.st_mode) != 0;
    out->size = (U64)st.st_size;
    out->mtime = (S64)st.st_mtime;
    return true;
}

bool iHostMakeDir(const char* path)
{
    if (mkdir(path, 0755) == 0)
    {
        return true;
    }

    // Already there is success: a caller walking a path should not have to
    // tell "I made it" from "it was there".
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool iHostRemoveFile(const char* path)
{
    return unlink(path) == 0;
}

bool iHostRemoveDir(const char* path)
{
    return rmdir(path) == 0;
}

bool iHostTempDir(char* out, size_t outsize)
{
    const char* t = getenv("TMPDIR");
    snprintf(out, outsize, "%s", (t != NULL && t[0] != 0) ? t : "/tmp");
    return true;
}

bool iHostSetEnv(const char* name, const char* value)
{
    if (value == NULL)
    {
        return unsetenv(name) == 0;
    }
    return setenv(name, value, 1) == 0;
}

bool iHostRenameReplace(const char* from, const char* to)
{
    // POSIX rename() already replaces an existing destination atomically.
    return rename(from, to) == 0;
}

bool iHostFreeBytes(const char* path, U64* out)
{
    struct statvfs vfs;
    if (statvfs(path, &vfs) != 0)
    {
        return false;
    }

    *out = (U64)vfs.f_bavail * (U64)vfs.f_frsize;
    return true;
}

struct iHostDir
{
    DIR* d;
};

iHostDir* iHostDirOpen(const char* path)
{
    DIR* d = opendir(path);
    if (d == NULL)
    {
        return NULL;
    }

    iHostDir* h = (iHostDir*)malloc(sizeof(iHostDir));
    if (h == NULL)
    {
        closedir(d);
        return NULL;
    }

    h->d = d;
    return h;
}

const char* iHostDirNext(iHostDir* h)
{
    if (h == NULL)
    {
        return NULL;
    }

    for (dirent* e = readdir(h->d); e != NULL; e = readdir(h->d))
    {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
        {
            continue;
        }
        return e->d_name;
    }

    return NULL;
}

void iHostDirClose(iHostDir* h)
{
    if (h != NULL)
    {
        closedir(h->d);
        free(h);
    }
}

bool iHostUserDataDir(char* out, size_t outsize)
{
    const char* xdg = getenv("XDG_DATA_HOME");
    if (xdg != NULL && xdg[0] != '\0')
    {
        snprintf(out, outsize, "%s", xdg);
        return true;
    }

    const char* home = getenv("HOME");
    if (home != NULL && home[0] != '\0')
    {
        snprintf(out, outsize, "%s/.local/share", home);
        return true;
    }

    return false;
}

S32 iHostStrCaseCmp(const char* a, const char* b)
{
    return strcasecmp(a, b);
}

const char* iHostName()
{
    return "posix";
}

// No symboliser is assumed on a POSIX host: backtrace()/backtrace_symbols are
// glibc extensions and give mangled names without line numbers even there.
// Saying nothing is better than saying something misleading, and this is a
// diagnostic -- nothing depends on its output.
void iHostPrintCallers(const char*, S32)
{
}
