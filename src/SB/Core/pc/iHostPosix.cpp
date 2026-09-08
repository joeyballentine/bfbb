#include "iHost.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
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

#ifdef __APPLE__
// _NSGetExecutablePath, which is macOS's answer to /proc/self/exe.
#include <mach-o/dyld.h>
#endif

// backtrace(). A glibc extension that macOS also carries; musl and the BSDs
// have neither, and iHostPrintCallers is a diagnostic, so it prints nothing
// there rather than the build failing.
#if defined(__GLIBC__) || defined(__APPLE__)
#define BFBB_HAVE_EXECINFO 1
#include <cxxabi.h>
#include <execinfo.h>
#endif

// The POSIX half of the iHost seam. See iHost.h for what each of these is for;
// this file is only the spelling.
//
// "POSIX" here is Linux and macOS. Where the two disagree the difference is
// inside one function, marked __APPLE__; a third host wanting in adds its arm
// the same way.

// macOS spells it MAP_ANON and defines MAP_ANONYMOUS only when the SDK is in a
// non-strict mode, which depends on how the translation unit was configured.
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif

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
#ifdef __APPLE__
    // macOS has no clock_nanosleep, so there is no absolute deadline to hand
    // the kernel and the deadline has to be held here instead. Re-reading the
    // clock each time round is what keeps that equivalent: the remaining time
    // is measured from now rather than from when the caller worked out the
    // interval, so a slow caller cannot make the pacing drift. The loop also
    // finishes the wait when a signal cuts nanosleep short.
    for (;;)
    {
        U64 now = iHostMonotonicNs();
        if (now >= targetNs)
        {
            return;
        }

        U64 remainingNs = targetNs - now;

        timespec ts;
        ts.tv_sec = (time_t)(remainingNs / NS_PER_SEC);
        ts.tv_nsec = (long)(remainingNs % NS_PER_SEC);

        if (nanosleep(&ts, NULL) == 0 || errno != EINTR)
        {
            return;
        }
    }
#else
    timespec ts;
    ts.tv_sec = (time_t)(targetNs / NS_PER_SEC);
    ts.tv_nsec = (long)(targetNs % NS_PER_SEC);

    // TIMER_ABSTIME rather than a duration, so a slow caller cannot make the
    // pacing drift by however long it took to work out the interval.
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
#endif
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
    const int prot = PROT_READ | PROT_WRITE;
    const int flags = MAP_PRIVATE | MAP_ANONYMOUS;

#ifdef MAP_32BIT
    // Linux x86-64: map inside the first 2 GB, where a U32 address is exact.
    void* low = mmap(NULL, size, prot, flags | MAP_32BIT, -1, 0);
    if (low != MAP_FAILED)
    {
        return low;
    }
#endif

    // No MAP_32BIT -- macOS, and Linux on anything but x86-64. Ask for specific
    // low base addresses instead and take the first that comes back low, which
    // is what iHostWin32.cpp does with VirtualAlloc for the same reason. A hint
    // is only a hint and the kernel may place the mapping anywhere, so the
    // result is checked rather than assumed.
    //
    // On macOS this depends on the executable being linked with a small
    // __PAGEZERO. The default one is 4 GB wide, which is exactly the range the
    // game allocator can address, and every hint below would land inside it;
    // CMakeLists.txt passes -pagezero_size, and the comment there says why the
    // value it passes still leaves a null dereference faulting.
    for (U64 base = 0x04000000ULL; base < 0x80000000ULL; base += 0x04000000ULL)
    {
        void* p = mmap((void*)(uintptr_t)base, size, prot, flags, -1, 0);
        if (p == MAP_FAILED)
        {
            continue;
        }

        if ((U64)(uintptr_t)p + (U64)size <= 0x100000000ULL)
        {
            return p;
        }

        munmap(p, size);
    }

    // Nothing low was free. Take whatever the OS offers and let iMemInit's
    // "above 4 GB" check refuse to start, rather than truncating silently.
    void* any = mmap(NULL, size, prot, flags, -1, 0);
    return any != MAP_FAILED ? any : NULL;
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

FILE* iHostCreateNewFile(const char* path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0)
    {
        return NULL;
    }

    FILE* f = fdopen(fd, "wb");
    if (f == NULL)
    {
        close(fd);
        return NULL;
    }

    return f;
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

bool iHostExeDir(char* out, size_t outsize)
{
    // There is no portable POSIX spelling of this question. Linux answers it
    // with /proc/self/exe and macOS with _NSGetExecutablePath; the BSDs have a
    // sysctl, and the port does not build on those, so this returns false there
    // rather than guessing. The caller has a fallback; see iConfig.cpp.
    char buf[1024];

#ifdef __APPLE__
    uint32_t n = (uint32_t)sizeof(buf);
    if (_NSGetExecutablePath(buf, &n) != 0)
    {
        return false;
    }

    // The path _NSGetExecutablePath hands back is the one used to launch the
    // process, symlinks and all, and a launcher that spells it with a `..`
    // would leave the directory below naming somewhere else. realpath is
    // allowed to fail -- a deleted binary, a directory that cannot be searched
    // -- and the unresolved path is still better than nothing.
    char resolved[1024];
    if (realpath(buf, resolved) != NULL)
    {
        snprintf(buf, sizeof(buf), "%s", resolved);
    }
#else
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
    {
        return false;
    }
    buf[n] = '\0';
#endif

    char* slash = strrchr(buf, '/');
    if (slash == NULL)
    {
        return false;
    }

    // "/bfbb" -- the executable is in the root directory. Trimming to nothing
    // would produce a relative path from an absolute one.
    if (slash == buf)
    {
        snprintf(out, outsize, "/");
        return true;
    }

    *slash = '\0';
    snprintf(out, outsize, "%s", buf);
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
#ifndef __APPLE__
    // XDG only on Linux. macOS has its own convention and no XDG_DATA_HOME to
    // read, so honouring the variable there would put the saves somewhere no
    // other Mac application looks.
    const char* xdg = getenv("XDG_DATA_HOME");
    if (xdg != NULL && xdg[0] != '\0')
    {
        snprintf(out, outsize, "%s", xdg);
        return true;
    }
#endif

    const char* home = getenv("HOME");
    if (home != NULL && home[0] != '\0')
    {
#ifdef __APPLE__
        snprintf(out, outsize, "%s/Library/Application Support", home);
#else
        snprintf(out, outsize, "%s/.local/share", home);
#endif
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
#ifdef __APPLE__
    return "macos";
#else
    return "posix";
#endif
}

// Nothing portable to show one with. X11, Wayland, macOS and a headless
// server disagree completely, and none of it belongs in the host seam for
// the sake of one message the caller has already printed. Deliberately
// empty rather than absent, so the interface is the same on both hosts.
void iHostErrorBox(const char*, const char*)
{
}

#ifdef BFBB_HAVE_EXECINFO
// backtrace_symbols gives one string per frame, in a format that differs
// between the two hosts and carries the mangled name in the middle of it:
//
//     bfbb(_Z8xSndPlayj+0x2c) [0x40126a]                        glibc
//     3   bfbb   0x000000010a1b2c34 _Z8xSndPlayj + 44           macOS
//
// The name is what the caller is reading for, so it is cut out and demangled;
// the rest of the line is left alone, because the module and the address in it
// are the part that survives when there is no symbol at all.
static void iPrintFrame(S32 depth, char* text)
{
    char* name = NULL;
    char* tail = NULL;

    char* open = strchr(text, '(');
    if (open != NULL)
    {
        // glibc: module(symbol+0xoff) [0xaddr]
        char* plus = strpbrk(open + 1, "+)");
        if (plus != NULL && plus != open + 1)
        {
            *plus = '\0';
            name = open + 1;
            tail = plus + 1;
        }
    }
    else
    {
        // macOS: index module address symbol + off. The symbol is the fourth
        // field, so skip three and cut at the trailing " + ".
        char* p = text;
        for (int field = 0; field < 3 && p != NULL; field++)
        {
            p += strspn(p, " ");
            p = strchr(p, ' ');
        }

        if (p != NULL)
        {
            p += strspn(p, " ");
            char* space = strchr(p, ' ');
            if (space != NULL)
            {
                *space = '\0';
                tail = space + 1;
            }
            name = p;
        }
    }

    if (name == NULL)
    {
        printf("bfbb:   #%-2d %s\n", depth, text);
        return;
    }

    int status = 0;
    char* pretty = abi::__cxa_demangle(name, NULL, NULL, &status);

    printf("bfbb:   #%-2d %s  %s\n", depth, status == 0 && pretty != NULL ? pretty : name,
           tail != NULL ? tail : "");

    free(pretty);
}
#endif

// Symbols, but no line numbers: backtrace_symbols reads the dynamic symbol
// table rather than debug info, so a static function does not appear by name at
// all. This is a diagnostic and nothing depends on its output, so that is worth
// having; a host without backtrace() at all prints nothing.
//
// Linking with -rdynamic is what makes the executable's own symbols visible
// here; without it every frame in the game reads as the module and an offset.
void iHostPrintCallers(const char* why, S32 maxFrames)
{
#ifdef BFBB_HAVE_EXECINFO
    if (maxFrames < 1)
    {
        maxFrames = 1;
    }
    else if (maxFrames > 48)
    {
        maxFrames = 48;
    }

    // One more than asked for, because frame 0 is this function and the caller
    // counts from the frame that called it, as CaptureStackBackTrace's skip
    // argument does on the Win32 side.
    void* frames[49];
    int got = backtrace(frames, maxFrames + 1);

    printf("bfbb: callers -- %s\n", why != NULL ? why : "");

    char** text = backtrace_symbols(frames, got);
    if (text == NULL)
    {
        return;
    }

    for (int i = 1; i < got; i++)
    {
        iPrintFrame(i - 1, text[i]);
    }

    free(text);
    fflush(stdout);
#else
    (void)why;
    (void)maxFrames;
#endif
}
