#include "iHost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>

// The Win32 half of the iHost seam. See iHost.h for what each of these is for;
// this file is only the spelling.

// ---------------------------------------------------------------------------
// Time

static const U64 NS_PER_SEC = 1000000000ULL;

// Written this way because a literal backslash in a character constant is
// exactly the sort of thing a careless sed over this tree would mangle.
static const char BS_CHAR = (char)92;

// QueryPerformanceCounter, not GetTickCount: the same reason POSIX uses
// CLOCK_MONOTONIC rather than the wall clock. QPC is monotonic and, since
// Windows 7, consistent across cores.
static U64 iQpcFreq()
{
    static U64 freq = 0;
    if (freq == 0)
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = (U64)f.QuadPart;
    }
    return freq;
}

U64 iHostMonotonicNs()
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);

    U64 freq = iQpcFreq();

    // Split the division so a machine that has been up for weeks does not
    // overflow the multiply. (ticks * 1e9) wraps a U64 after about 18 seconds
    // at a 1 GHz counter, which is not hypothetical -- QPC frequency is often
    // exactly that.
    U64 ticks = (U64)c.QuadPart;
    return (ticks / freq) * NS_PER_SEC + ((ticks % freq) * NS_PER_SEC) / freq;
}

void iHostSleepUntilNs(U64 targetNs)
{
    U64 now = iHostMonotonicNs();
    if (targetNs <= now)
    {
        return;
    }

    U64 remainingNs = targetNs - now;

    // A plain Sleep() rounds to the scheduler tick, which defaults to about
    // 15.6 ms -- most of a frame, so pacing with it would judder badly. A
    // high-resolution waitable timer honours the actual deadline. The flag
    // needs Windows 10 1803; CreateWaitableTimerEx fails on older systems, so
    // fall back to an ordinary timer, which is still better than Sleep.
    static HANDLE timer = NULL;
    if (timer == NULL)
    {
        timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                       TIMER_ALL_ACCESS);
        if (timer == NULL)
        {
            timer = CreateWaitableTimerW(NULL, FALSE, NULL);
        }
    }

    if (timer != NULL)
    {
        // Negative means relative, in 100 ns units.
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(remainingNs / 100);
        if (due.QuadPart != 0 && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE))
        {
            WaitForSingleObject(timer, INFINITE);
            return;
        }
    }

    Sleep((DWORD)(remainingNs / 1000000));
}

void iHostLocalTimeOf(S64 unixSeconds, iHostCalendar* out)
{
    time_t when = (time_t)unixSeconds;
    tm t;
    localtime_s(&t, &when);

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
    // Windows has no MAP_32BIT. The equivalent is to ask for specific low base
    // addresses and take the first that is free. Start above 64 MB to stay
    // clear of where images are normally based, and step by 64 MB because that
    // is the allocation granularity's practical stride for a block this large.
    for (U64 base = 0x04000000ULL; base < 0x80000000ULL; base += 0x04000000ULL)
    {
        void* p =
            VirtualAlloc((LPVOID)(uintptr_t)base, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (p != NULL)
        {
            return p;
        }
    }

    // Nothing low was free. Take whatever the OS offers and let iMemInit's
    // "above 4 GB" check refuse to start, rather than truncating silently.
    return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

void iHostRelease(void* p, U32 size)
{
    if (p != NULL)
    {
        // MEM_RELEASE frees the whole reservation and requires size 0.
        VirtualFree(p, 0, MEM_RELEASE);
    }
}

// ---------------------------------------------------------------------------
// Filesystem

bool iHostPathExists(const char* path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

bool iHostStat(const char* path, iHostFileInfo* out)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad))
    {
        return false;
    }

    bool dir = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    out->is_dir = dir;
    out->is_file = !dir;
    out->size = ((U64)fad.nFileSizeHigh << 32) | (U64)fad.nFileSizeLow;

    // FILETIME counts 100 ns units from 1601; Unix time counts seconds from
    // 1970. 11644473600 is the gap in seconds.
    U64 ft =
        ((U64)fad.ftLastWriteTime.dwHighDateTime << 32) | (U64)fad.ftLastWriteTime.dwLowDateTime;
    out->mtime = (S64)(ft / 10000000ULL) - 11644473600LL;
    return true;
}

bool iHostMakeDir(const char* path)
{
    if (CreateDirectoryA(path, NULL))
    {
        return true;
    }

    // Already there is success, as on the POSIX side.
    iHostFileInfo info;
    return iHostStat(path, &info) && info.is_dir;
}

bool iHostRemoveFile(const char* path)
{
    return DeleteFileA(path) != 0;
}

bool iHostRemoveDir(const char* path)
{
    return RemoveDirectoryA(path) != 0;
}

bool iHostTempDir(char* out, size_t outsize)
{
    char buf[MAX_PATH + 1];
    DWORD n = GetTempPathA(sizeof(buf), buf);
    if (n == 0 || n > MAX_PATH)
    {
        return false;
    }

    // GetTempPath always ends in a backslash; the seam's paths do not carry a
    // trailing separator, and the rest of the layer uses forward slashes.
    while (n > 0 && (buf[n - 1] == BS_CHAR || buf[n - 1] == '/'))
    {
        buf[--n] = 0;
    }

    for (DWORD i = 0; i < n; i++)
    {
        if (buf[i] == BS_CHAR)
        {
            buf[i] = '/';
        }
    }

    snprintf(out, outsize, "%s", buf);
    return true;
}

bool iHostSetEnv(const char* name, const char* value)
{
    // _putenv_s with an empty value is how Windows spells unsetenv.
    return _putenv_s(name, value != NULL ? value : "") == 0;
}

bool iHostRenameReplace(const char* from, const char* to)
{
    // Not rename(): the CRT's fails when the destination exists. MOVEFILE_
    // REPLACE_EXISTING is the behaviour POSIX rename() already has, and
    // WRITE_THROUGH makes the replacement durable before this returns.
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool iHostFreeBytes(const char* path, U64* out)
{
    ULARGE_INTEGER avail;
    if (!GetDiskFreeSpaceExA(path, &avail, NULL, NULL))
    {
        return false;
    }

    *out = (U64)avail.QuadPart;
    return true;
}

struct iHostDir
{
    HANDLE h;
    WIN32_FIND_DATAA fd;
    bool pending;
};

iHostDir* iHostDirOpen(const char* path)
{
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s/*", path);

    iHostDir* d = (iHostDir*)malloc(sizeof(iHostDir));
    if (d == NULL)
    {
        return NULL;
    }

    d->h = FindFirstFileA(pattern, &d->fd);
    if (d->h == INVALID_HANDLE_VALUE)
    {
        free(d);
        return NULL;
    }

    // FindFirstFile has already produced the first entry, so the first call to
    // iHostDirNext must return it rather than advancing past it.
    d->pending = true;
    return d;
}

const char* iHostDirNext(iHostDir* d)
{
    if (d == NULL)
    {
        return NULL;
    }

    for (;;)
    {
        if (!d->pending)
        {
            if (!FindNextFileA(d->h, &d->fd))
            {
                return NULL;
            }
        }
        d->pending = false;

        if (strcmp(d->fd.cFileName, ".") == 0 || strcmp(d->fd.cFileName, "..") == 0)
        {
            continue;
        }

        return d->fd.cFileName;
    }
}

void iHostDirClose(iHostDir* d)
{
    if (d != NULL)
    {
        FindClose(d->h);
        free(d);
    }
}

bool iHostUserDataDir(char* out, size_t outsize)
{
    const char* appdata = getenv("APPDATA");
    if (appdata != NULL && appdata[0] != '\0')
    {
        snprintf(out, outsize, "%s", appdata);
        return true;
    }

    return false;
}

S32 iHostStrCaseCmp(const char* a, const char* b)
{
    return _stricmp(a, b);
}

const char* iHostName()
{
    return "win32";
}

// Symbolised caller stack, for diagnostics. See iHost.h.
//
// CaptureStackBackTrace rather than StackWalk64: this is called from ordinary
// running code with no CONTEXT to hand, and the frame-pointer walk in
// bfbb_main.cpp's crash handler needs one. Symbols come from the PDB through
// dbghelp, the same way, and a build without one prints addresses -- still
// enough to tell two different callers apart.
void iHostPrintCallers(const char* why, S32 maxFrames)
{
    static bool symbolsReady = false;
    if (!symbolsReady)
    {
        symbolsReady = true;
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        SymInitialize(GetCurrentProcess(), NULL, TRUE);
    }

    if (maxFrames < 1)
    {
        maxFrames = 1;
    }
    else if (maxFrames > 48)
    {
        maxFrames = 48;
    }

    void* frames[48];
    USHORT got = CaptureStackBackTrace(1, (DWORD)maxFrames, frames, NULL);

    printf("bfbb: callers -- %s\n", why != NULL ? why : "");

    char symbolBuffer[sizeof(SYMBOL_INFO) + 512];
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)symbolBuffer;
    memset(symbolBuffer, 0, sizeof(symbolBuffer));
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 500;

    HANDLE process = GetCurrentProcess();

    for (USHORT i = 0; i < got; i++)
    {
        DWORD64 addr = (DWORD64)(uintptr_t)frames[i];
        DWORD64 displacement = 0;
        const char* name = "?";

        if (SymFromAddr(process, addr, &displacement, symbol))
        {
            name = symbol->Name;
        }

        IMAGEHLP_LINE64 line;
        memset(&line, 0, sizeof(line));
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;

        if (SymGetLineFromAddr64(process, addr, &lineDisplacement, &line))
        {
            printf("bfbb:   #%-2d %s  (%s:%lu)\n", (int)i, name, line.FileName,
                   (unsigned long)line.LineNumber);
        }
        else
        {
            printf("bfbb:   #%-2d %s + 0x%llx\n", (int)i, name,
                   (unsigned long long)displacement);
        }
    }

    fflush(stdout);
}
