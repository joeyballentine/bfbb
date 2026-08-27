#ifndef IHOST_H
#define IHOST_H

#include <types.h>

#include <stddef.h>

// PC-only. There is no GameCube counterpart to this file, for the same reason
// there is none for iPadHost.h: the console has exactly one implementation of
// each of these and calls it directly, while a host has several and none of
// them is guaranteed to exist.
//
// This is the seam between the platform layer and the operating system. The
// i* files above it -- iTime, iMemMgr, iFile, iSystem, isavegame -- hold the
// mapping onto the game's semantics, which is the part worth reading and the
// part that is the same everywhere. Everything below it is one OS's spelling.
//
// The layer was originally written against POSIX directly, with sys/mman.h,
// CLOCK_MONOTONIC, localtime_r and opendir at 26 call sites across five files.
// That is what this replaces. The rule that keeps it from growing back: an
// #ifdef for the host OS belongs in an iHost*.cpp, never above this line.

// ---------------------------------------------------------------------------
// Time

// A monotonic clock: unaffected by NTP, DST, or the user editing the wall
// clock. Retail reads the GameCube timebase, which no user action can move,
// and a wall-clock jump would otherwise surface as one enormous frame delta.
// The epoch is arbitrary and means nothing except relative to itself.
U64 iHostMonotonicNs();

// Sleeps until iHostMonotonicNs() reaches targetNs. Returns immediately if
// that moment has passed. Absolute rather than a duration so that pacing
// cannot drift by the cost of computing the interval.
void iHostSleepUntilNs(U64 targetNs);

// Fields carry the same meaning as struct tm, deliberately: mon is 0-11 and
// year counts from 1900, so the call sites read the same as they did against
// localtime_r and the retail-behaviour comments around them still apply.
struct iHostCalendar
{
    S32 sec;
    S32 min;
    S32 hour;
    S32 mday;
    S32 mon;
    S32 year;
    S32 wday;
};

// Local civil time for a Unix timestamp. Thread-safe: the host-specific
// reentrant call, because localtime() returns a shared buffer.
void iHostLocalTimeOf(S64 unixSeconds, iHostCalendar* out);

// Local civil time, now.
void iHostLocalTime(iHostCalendar* out);

// ---------------------------------------------------------------------------
// Virtual memory

// Reserves `size` bytes of read/write memory, preferring an address that fits
// in 32 bits. gMemInfo.DRAM.addr is a U32 and xMemInitHeap does pointer
// arithmetic on it as an integer, so every address the game allocator hands
// out has to survive the round trip back to a pointer. malloc on a 64-bit host
// does not guarantee that; this does its best to, and iMemInit refuses to
// start above 4 GB rather than truncate silently.
//
// Returns NULL on failure. Pair with iHostRelease, passing the same size.
void* iHostReserveLow(U32 size);
void iHostRelease(void* p, U32 size);

// ---------------------------------------------------------------------------
// Filesystem
//
// Paths are '/'-separated everywhere above this seam, including on Windows,
// because that is what the game's own asset names use. A backend that needs a
// different separator converts on the way through.

bool iHostPathExists(const char* path);

struct iHostFileInfo
{
    bool is_dir;
    bool is_file;
    U64 size;

    // Last modification, as a Unix timestamp, for iHostLocalTimeOf.
    S64 mtime;
};

bool iHostStat(const char* path, iHostFileInfo* out);

// Creates one directory. Succeeds if it already exists, so a caller walking a
// path does not have to distinguish the two.
bool iHostMakeDir(const char* path);

bool iHostRemoveFile(const char* path);

// Removes an empty directory.
bool iHostRemoveDir(const char* path);

// Renames `from` over `to`, replacing `to` if it exists. NOT plain rename():
// POSIX rename() replaces silently, but the Windows CRT's fails outright when
// the destination exists, which would break every save after the first --
// isavegame writes to a temporary and renames it over the real file precisely
// so that a crash mid-write cannot destroy the previous save.
bool iHostRenameReplace(const char* from, const char* to);

// Bytes available to this user on the volume holding `path`. False if the host
// cannot say.
bool iHostFreeBytes(const char* path, U64* out);

// Directory iteration. iHostDirNext returns NULL at the end; the string it
// returns is valid until the next call on the same handle. "." and ".." are
// not reported -- no caller wants them, and every caller would have to filter.
struct iHostDir;

iHostDir* iHostDirOpen(const char* path);
const char* iHostDirNext(iHostDir* d);
void iHostDirClose(iHostDir* d);

// The platform's per-user data directory, with no game-specific suffix --
// $XDG_DATA_HOME or ~/.local/share on POSIX, %APPDATA% on Windows. Where the
// saves go *within* that is the game's policy and lives in isavegame.cpp.
// False if the host has no such concept, in which case the caller falls back
// to a relative path.
bool iHostUserDataDir(char* out, size_t outsize);

// The system's scratch directory -- $TMPDIR or /tmp on POSIX, GetTempPath on
// Windows. No trailing separator. Used by the selftest; nothing in the game
// depends on it.
bool iHostTempDir(char* out, size_t outsize);

// Sets an environment variable for this process, overwriting any existing
// value; a NULL value removes it. POSIX spells these setenv/unsetenv and
// Windows spells both _putenv_s.
bool iHostSetEnv(const char* name, const char* value);

// Print the calling stack, symbolised, prefixed with `why`.
//
// For DIAGNOSTICS, not for errors: the question it answers is "which game code
// leads here", which comes up constantly in a port because the platform layer
// sees a call with no context and the code that made it is 200 files away.
// The alternative is guessing from the arguments, which is slow and often
// wrong. A host that cannot symbolise its own stack prints nothing.
void iHostPrintCallers(const char* why, S32 maxFrames);

// Case-insensitive compare, for the disc filesystem's benefit. POSIX spells it
// strcasecmp and Windows spells it _stricmp.
S32 iHostStrCaseCmp(const char* a, const char* b);

// Names the backend that was linked in, for the startup log.
const char* iHostName();

#endif
