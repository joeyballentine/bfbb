#include "isavegame.h"
#include "iHost.h"

#include <types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Saves, on a host filesystem.
//
// The GameCube version of this file is 2048 lines, and most of that is memory
// card: mounting and unmounting, probing sector sizes, formatting, repairing a
// broken card, and building the 8 KB banner-and-icon blob that CARD requires be
// written at the head of every file so the console's own file manager can show
// it. None of that exists here.
//
// What does carry over is the shape the game was written against, because
// xsavegame.cpp is shared and asks all the same questions: how many targets are
// there, is this one formatted, is there room, what is on it. Each gets the
// answer that is true of a directory.
//
// The file names are retail's, so a save is named the same thing on both.

// Retail's, from iSG_cubeicon_size: every card file carries a banner and icon
// ahead of the payload, and iSGFileSize subtracts it back off. A host file is
// its payload, so the two cancel and neither appears below.

static S32 g_isginit;

// The directory holding the save targets. Chosen once, at iSGStartup.
static char g_saveroot[512];

// One target per memory card slot the console had, and the port exposes both.
//
// Reporting a single target did NOT remove the second one from the interface,
// which is what it was supposed to do: the save and load screens are UI assets
// with two buttons baked into them -- MNU3 LD MC1 and MC2, SV MC1 and MC2 --
// and the chooser is drawn whatever this number says. All reporting one did was
// make the second button dead, so choosing it fell into the single-target arm of
// zSaveLoad_CardCheckFormatted, found the physical slot did not match, and came
// back as an error.
//
// Two makes it a folder like the first, with its own three game slots. That is
// six saves rather than three, and it costs no screen the player was not
// already being shown.
//
// ISG_NUM_SLOTS is the ceiling: mcdata is sized by it, and iSG_mcidx2slot maps a
// target index straight onto a slot index.
#define ISG_HOST_TARGETS 2

#if ISG_HOST_TARGETS > 2
#error "iSG_target_root has a name for two folders, not more"
#endif

static st_ISGSESSION g_isgdata_MAIN;

static bool iSG_mkdir_p(const char* path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);

    for (char* p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            iHostMakeDir(tmp);
            *p = '/';
        }
    }

    return iHostMakeDir(tmp);
}

// config.ini's game.save_folder, empty until iSystem says otherwise.
static char g_configroot[512];

void iSGSetSaveRoot(const char* dir)
{
    snprintf(g_configroot, sizeof(g_configroot), "%s", dir != NULL ? dir : "");
}

// BFBB_SAVE_DIR wins so a player can point saves anywhere; otherwise the
// setting, and otherwise wherever this host keeps per-user data, which is where
// its users expect to find it. Which directory that is belongs to iHost; the
// "/bfbb/saves" under it is this game's policy and belongs here.
static void iSG_resolve_saveroot()
{
    const char* explicit_dir = getenv("BFBB_SAVE_DIR");
    if (explicit_dir != NULL && explicit_dir[0] != '\0')
    {
        snprintf(g_saveroot, sizeof(g_saveroot), "%s", explicit_dir);
        return;
    }

    if (g_configroot[0] != '\0')
    {
        snprintf(g_saveroot, sizeof(g_saveroot), "%s", g_configroot);
        return;
    }

    char base[512];
    if (iHostUserDataDir(base, sizeof(base)))
    {
        snprintf(g_saveroot, sizeof(g_saveroot), "%s/bfbb/saves", base);
        return;
    }

    snprintf(g_saveroot, sizeof(g_saveroot), "saves");
}

// The first target is the save root ITSELF rather than a subdirectory of it.
// That is not symmetry for its own sake being thrown away -- it is what keeps
// saves written while this build exposed one target where the game can still
// find them. Only the second target needs somewhere new to live.
static void iSG_target_root(S32 slot, char* out, size_t outsize)
{
    if (slot <= 0)
    {
        snprintf(out, outsize, "%s", g_saveroot);
    }
    else
    {
        snprintf(out, outsize, "%s/second", g_saveroot);
    }
}

// The full path of one save file on the active target.
static bool iSG_path(st_ISGSESSION* isgdata, const char* fname, char* out, size_t outsize)
{
    if (isgdata == NULL || isgdata->slot < 0 || fname == NULL || fname[0] == '\0')
    {
        return false;
    }

    snprintf(out, outsize, "%s/%s", isgdata->mcdata[isgdata->slot].root, fname);
    return true;
}

S32 iSGStartup()
{
    if (g_isginit++ != 0)
    {
        return g_isginit;
    }

    iSG_resolve_saveroot();

    // Retail's iSG_start_your_engines brings CARD up and loads the icon
    // artwork. The host equivalent is making sure the directory exists, so
    // that a first run has somewhere to save to before anything asks.
    for (S32 i = 0; i < ISG_HOST_TARGETS; i++)
    {
        char root[512];
        iSG_target_root(i, root, sizeof(root));
        iSG_mkdir_p(root);
    }

    return g_isginit;
}

S32 iSGShutdown()
{
    if (g_isginit > 0)
    {
        g_isginit--;
    }

    return 1;
}

// Retail's rotating buffer, kept because callers hold on to several of these
// at once -- xSGTgtHaveRoom copies one while xSG_chdir_gamedir holds another --
// and would alias if there were only one.
char* iSGMakeName(en_NAMEGEN_TYPE type, const char* base, S32 idx)
{
    static S32 rotate = 0;
    static char rotatebuf[8][32] = { 0 };

    char* use_buf = rotatebuf[rotate++];
    if (rotate == 8)
    {
        rotate = 0;
    }

    *use_buf = '\0';

    switch (type)
    {
    case ISG_NGTYP_GAMEFILE:
        snprintf(use_buf, 32, "%s%02d", base != NULL ? base : "SpongeBob", idx);
        break;

    // A memory card has no directories -- every file sits at the root -- so
    // retail returns an empty name for all three of these and the callers
    // treat that as "no subdirectory". The host layout is the same, one flat
    // directory per target, so the answer is the same.
    case ISG_NGTYP_GAMEDIR:
    case ISG_NGTYP_CONFIG:
    case ISG_NGTYP_ICONTHUM:
        break;
    }

    return use_buf;
}

st_ISGSESSION* iSGSessionBegin(void* cltdata, void (*chgfunc)(void*, en_CHGCODE), S32 monitor)
{
    memset(&g_isgdata_MAIN, 0, sizeof(st_ISGSESSION));

    g_isgdata_MAIN.slot = -1;
    g_isgdata_MAIN.chgfunc = chgfunc;
    g_isgdata_MAIN.cltdata = cltdata;
    g_isgdata_MAIN.monitor = monitor;

    return &g_isgdata_MAIN;
}

void iSGSessionEnd(st_ISGSESSION* isgdata)
{
    if (isgdata == NULL)
    {
        return;
    }

    memset(isgdata, 0, sizeof(st_ISGSESSION));
}

// Retail probes both card slots and counts the ones that answer. A directory
// is always there, so the count is however many targets this build exposes.
S32 iSGTgtCount(st_ISGSESSION* isgdata, S32* max)
{
    if (max != NULL)
    {
        *max = ISG_NUM_SLOTS;
    }

    return ISG_HOST_TARGETS;
}

S32 iSG_mcidx2slot(S32 tidx, S32* out_slot, S32* ready)
{
    *out_slot = -1;

    if (ready != NULL)
    {
        for (S32 i = 0; i < ISG_NUM_SLOTS; i++)
        {
            ready[i] = (i < ISG_HOST_TARGETS);
        }
    }

    if (tidx < 0 || tidx >= ISG_HOST_TARGETS)
    {
        return 0;
    }

    // Every target this build exposes is present, so the index into the
    // present ones and the physical slot are the same number. On the console
    // they differ whenever slot A is empty and slot B is not.
    *out_slot = tidx;
    return 1;
}

S32 iSGTgtPhysSlotIdx(st_ISGSESSION* isgdata, S32 tidx)
{
    S32 idx = -1;
    if (iSG_mcidx2slot(tidx, &idx, NULL))
    {
        return idx;
    }

    return -1;
}

S32 iSGTgtSetActive(st_ISGSESSION* isgdata, S32 tgtidx)
{
    S32 slot = -1;
    if (!iSG_mcidx2slot(tgtidx, &slot, NULL))
    {
        return 0;
    }

    st_ISG_MEMCARD_DATA* data = &isgdata->mcdata[slot];

    if (!data->inuse)
    {
        data->inuse = 1;
        data->chan = slot;
        data->sectorSize = 0;
        iSG_target_root(slot, data->root, sizeof(data->root));
    }

    isgdata->slot = slot;
    return 1;
}

// The state bits, as xSGTgtIsFormat reads them: 0x1 present, 0x2 formatted,
// 0x4 needs formatting, 0x1000000 no such target. Retail also reports a card
// with an unreadable encoding (0x4000000) or an illegal sector size
// (0x8000004); a directory has neither failure mode.
U32 iSGTgtState(st_ISGSESSION* isgdata, S32 tgtidx, const char* dpath)
{
    S32 slot = -1;
    iSG_mcidx2slot(tgtidx, &slot, NULL);

    if (slot < 0)
    {
        return 0x1000000;
    }

    if (slot != isgdata->slot)
    {
        iSGTgtSetActive(isgdata, tgtidx);
    }

    char root[512];
    iSG_target_root(slot, root, sizeof(root));

    iHostFileInfo st;
    if (!iHostStat(root, &st) || !st.is_dir)
    {
        // Present as a target, but not yet usable -- the game offers to
        // format, which here means creating the directory.
        return 0x1 | 0x4;
    }

    return 0xF;
}

// "Formatting" a card is making it able to hold files. For a directory that is
// creating it.
S32 iSGTgtFormat(st_ISGSESSION* isgdata, S32 tgtidx, S32 async, S32* canRecover)
{
    if (canRecover != NULL)
    {
        *canRecover = 0;
    }

    S32 slot = -1;
    if (!iSG_mcidx2slot(tgtidx, &slot, NULL))
    {
        return 0;
    }

    char root[512];
    iSG_target_root(slot, root, sizeof(root));

    return iSG_mkdir_p(root) ? 1 : 0;
}

U8 iSGCheckMemoryCard(st_ISGSESSION* isgdata, S32 index)
{
    return (index >= 0 && index < ISG_HOST_TARGETS) ? 1 : 0;
}

// Retail returns the slot holding a card from another region, which the TRC
// rules require be reported rather than silently ignored. No host equivalent.
S32 iSGCheckForWrongDevice()
{
    return -1;
}

static S32 iSG_free_bytes(const char* path)
{
    U64 avail;
    if (!iHostFreeBytes(path, &avail))
    {
        return 0;
    }

    // The game compares this against a save size in a S32 and displays it. A
    // modern disk overflows that, and reporting a negative number of free
    // bytes would read as "no room" -- clamp instead.
    if (avail > 0x7FFFFFFFULL)
    {
        return 0x7FFFFFFF;
    }

    return (S32)avail;
}

static S32 iSG_have_room(st_ISGSESSION* isgdata, S32 fsize, const char* fname, S32* bytesNeeded,
                         S32* availOnDisk, S32* needFile)
{
    if (isgdata->slot < 0)
    {
        return 0;
    }

    st_ISG_MEMCARD_DATA* data = &isgdata->mcdata[isgdata->slot];
    if (!data->inuse)
    {
        return 0;
    }

    S32 avail = iSG_free_bytes(data->root);

    // Overwriting a file that already exists costs only the difference, and
    // needs no new directory entry. Retail draws the same distinction, because
    // a card can be out of file slots while still having free blocks.
    S32 existing = 0;
    S32 need_entry = 1;

    if (fname != NULL && fname[0] != '\0')
    {
        char path[512];
        if (iSG_path(isgdata, fname, path, sizeof(path)))
        {
            iHostFileInfo st;
            if (iHostStat(path, &st))
            {
                existing = (S32)st.size;
                need_entry = 0;
            }
        }
    }

    S32 needed = fsize - existing;
    if (needed < 0)
    {
        needed = 0;
    }

    if (bytesNeeded != NULL)
    {
        *bytesNeeded = needed;
    }
    if (availOnDisk != NULL)
    {
        *availOnDisk = avail;
    }
    if (needFile != NULL)
    {
        *needFile = need_entry;
    }

    return needed <= avail;
}

// The unit the save UI displays in. See isavegame.h for why it is not blocks.
//
// U64 internally because iSGFormatFreeSpace has a whole disk to word, which the
// S32 the rest of this interface passes around cannot hold.
static void iSG_format_bytes(U64 bytes, char* out, U32 outsize)
{
    if (out == NULL || outsize == 0)
    {
        return;
    }

    // Rounded up: a save that needs one byte past a kilobyte needs the next one.
    U64 units = (bytes + 1023u) / 1024u;
    const char* name[] = { "KB", "MB", "GB", "TB" };
    S32 i = 0;

    // The remainder of the last division is the tenth, so it has to be kept as
    // the loop climbs rather than recovered from `units` afterwards.
    U64 rem = 0;
    while (units >= 1024 && i < 3)
    {
        rem = units % 1024;
        units /= 1024;
        i++;
    }

    if (i == 0)
    {
        snprintf(out, outsize, "%u KB", (unsigned)units);
        return;
    }

    // One decimal, in integers rather than through a float, and TRUNCATED where
    // the whole number is rounded up: 1.99 GB reading as "2.0 GB" would be a
    // lie in the one direction that matters when the question is whether
    // something fits.
    snprintf(out, outsize, "%u.%u %s", (unsigned)units, (unsigned)(rem * 10 / 1024), name[i]);
}

void iSGFormatSize(S32 bytes, char* out, U32 outsize)
{
    // A negative can only come of an overflow upstream. It is not free space.
    iSG_format_bytes(bytes > 0 ? (U64)bytes : 0, out, outsize);
}

// Free space, asked of the host directly rather than taken from the S32 the
// game carries it in.
//
// That S32 is iSG_have_room's answer, and it is clamped to 2 GB because the game
// compares it against a save size in a signed 32-bit int -- see iSG_free_bytes.
// The clamp is right for the comparison and useless for the display: every disk
// this will ever run on would read "2.0 GB". The save screen's free-space line
// asks a different question and gets a different number.
//
// The save root's volume, not the active target's: the second target lives
// inside the first, so there is only ever one volume to report on.
void iSGFormatFreeSpace(char* out, U32 outsize)
{
    U64 avail = 0;
    if (!iHostFreeBytes(g_saveroot, &avail))
    {
        avail = 0;
    }

    iSG_format_bytes(avail, out, outsize);
}

S32 iSGTgtHaveRoom(st_ISGSESSION* isgdata, S32 tidx, S32 fsize, const char* dpath,
                   const char* fname, S32* bytesNeeded, S32* availOnDisk, S32* needFile)
{
    return iSG_have_room(isgdata, fsize, fname, bytesNeeded, availOnDisk, needFile);
}

// Retail's startup variant differs only in how it accounts for the card's
// per-file overhead before any game files exist. There is no per-file overhead
// here, so the two are the same question.
S32 iSGTgtHaveRoomStartup(st_ISGSESSION* isgdata, S32 tidx, S32 fsize, const char* dpath,
                          const char* fname, S32* bytesNeeded, S32* availOnDisk, S32* needFile)
{
    return iSG_have_room(isgdata, fsize, fname, bytesNeeded, availOnDisk, needFile);
}

S32 iSGFileSize(st_ISGSESSION* isgdata, const char* fname)
{
    if (isgdata->slot < 0)
    {
        return -1;
    }

    char path[512];
    if (!iSG_path(isgdata, fname, path, sizeof(path)))
    {
        return -1;
    }

    iHostFileInfo st;
    if (!iHostStat(path, &st) || !st.is_file)
    {
        return -1;
    }

    return (S32)st.size;
}

char* iSGFileModDate(st_ISGSESSION* isgdata, const char* fname)
{
    return iSGFileModDate(isgdata, fname, NULL, NULL, NULL, NULL, NULL, NULL);
}

char* iSGFileModDate(st_ISGSESSION* isgdata, const char* fname, S32* sec, S32* min, S32* hr,
                     S32* mon, S32* day, S32* yr)
{
    static char datestr[0x40] = { 0 };

    char path[512];
    iHostFileInfo st;

    if (iSG_path(isgdata, fname, path, sizeof(path)) && iHostStat(path, &st))
    {
        iHostCalendar t;
        iHostLocalTimeOf(st.mtime, &t);

        S32 v_sec = t.sec;
        S32 v_min = t.min;
        S32 v_hr = t.hour;
        S32 v_mon = t.mon + 1;
        S32 v_day = t.mday;
        S32 v_yr = t.year + 1900;

        sprintf(datestr, "%02d/%02d/%04d %02d:%02d:%02d", v_mon, v_day, v_yr, v_hr, v_min, v_sec);

        if (sec != NULL)
        {
            *sec = v_sec;
        }
        if (min != NULL)
        {
            *min = v_min;
        }
        if (hr != NULL)
        {
            *hr = v_hr;
        }
        if (mon != NULL)
        {
            *mon = v_mon;
        }
        if (day != NULL)
        {
            *day = v_day;
        }
        if (yr != NULL)
        {
            *yr = v_yr;
        }
    }
    else
    {
        sprintf(datestr, "<Unknown Modification>");
    }

    return datestr;
}

void iSGMakeTimeStamp(char* str)
{
    iHostCalendar t;
    iHostLocalTime(&t);

    sprintf(str, "%02d/%02d/%04d %02d:%02d:%02d", t.mon + 1, t.mday, t.year + 1900, t.hour, t.min,
            t.sec);
}

// "Is there a game to load on this target." Retail answers it by counting the
// save files that exist, because a memory card has no directory to look for.
S32 iSGSelectGameDir(st_ISGSESSION* isgdata, const char* dname)
{
    if (isgdata->slot < 0 || !isgdata->mcdata[isgdata->slot].inuse)
    {
        return 0;
    }

    S32 count = 0;
    for (S32 idx = 0; idx < ISG_NUM_FILES; idx++)
    {
        const char* n = iSGMakeName(ISG_NGTYP_GAMEFILE, NULL, idx);
        if (iSGFileSize(isgdata, n) > 0)
        {
            count++;
        }
    }

    return count != 0;
}

S32 iSGSetupGameDir(st_ISGSESSION* isgdata, const char* dname, S32 force_iconfix)
{
    // Retail returns 1 without doing anything: there is no directory to make on
    // a memory card. Here there is, and iSGStartup already made it -- but a
    // player can delete it between runs, so this is where it comes back.
    if (isgdata->slot < 0)
    {
        return 0;
    }

    return iSG_mkdir_p(isgdata->mcdata[isgdata->slot].root) ? 1 : 0;
}

static void iSG_fail(st_ISGSESSION* isgdata, en_ASYNC_OPERR err)
{
    isgdata->as_opstat = ISG_OPSTAT_FAILURE;
    isgdata->as_operr = err;
}

// Written to a temporary and renamed, so that losing power partway through
// leaves the previous save intact rather than a truncated one. The console got
// this for free -- CARD writes whole sectors and updates the directory entry
// last.
S32 iSGSaveFile(st_ISGSESSION* isgdata, const char* fname, char* data, S32 n, S32 async,
                char* label)
{
    char path[512];

    if (isgdata->slot < 0 || !iSG_path(isgdata, fname, path, sizeof(path)))
    {
        iSG_fail(isgdata, ISG_OPERR_NOCARD);
        return 0;
    }

    if (!iSG_mkdir_p(isgdata->mcdata[isgdata->slot].root))
    {
        iSG_fail(isgdata, ISG_OPERR_GAMEDIR);
        return 0;
    }

    char tmp[544];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE* f = fopen(tmp, "wb");
    if (f == NULL)
    {
        iSG_fail(isgdata, ISG_OPERR_SVOPEN);
        return 0;
    }

    bool wrote = (n <= 0) || (fwrite(data, 1, (size_t)n, f) == (size_t)n);

    // The rename is only atomic if the bytes are on their way to the disk
    // first; without this the new name can appear pointing at nothing.
    if (wrote)
    {
        wrote = (fflush(f) == 0);
    }

    fclose(f);

    if (!wrote)
    {
        iHostRemoveFile(tmp);
        iSG_fail(isgdata, ISG_OPERR_SVWRITE);
        return 0;
    }

    if (!iHostRenameReplace(tmp, path))
    {
        iHostRemoveFile(tmp);
        iSG_fail(isgdata, ISG_OPERR_SVWRITE);
        return 0;
    }

    isgdata->as_opstat = ISG_OPSTAT_SUCCESS;
    isgdata->as_operr = ISG_OPERR_NONE;
    return 1;
}

// Reads the first numbytes of a save. The name is retail's: the save UI calls
// it to pull just the header off each slot -- the label, progress and play
// time it lists -- without reading the whole file.
S32 iSGReadLeader(st_ISGSESSION* isgdata, const char* fname, char* databuf, S32 numbytes, S32 async)
{
    char path[512];

    if (isgdata->slot < 0 || !iSG_path(isgdata, fname, path, sizeof(path)))
    {
        iSG_fail(isgdata, ISG_OPERR_NOCARD);
        return 0;
    }

    if (numbytes <= 0 || databuf == NULL)
    {
        iSG_fail(isgdata, ISG_OPERR_LDREAD);
        return 0;
    }

    FILE* f = fopen(path, "rb");
    if (f == NULL)
    {
        iSG_fail(isgdata, ISG_OPERR_LDOPEN);
        return 0;
    }

    size_t got = fread(databuf, 1, (size_t)numbytes, f);
    fclose(f);

    // A short read means the file is smaller than its own header says, which
    // is what a truncated save looks like. Retail reads whole sectors and
    // would get padding, so the callers do not check -- say so here instead of
    // handing back a partly-filled buffer.
    if (got != (size_t)numbytes)
    {
        memset(databuf + got, 0, (size_t)numbytes - got);
        iSG_fail(isgdata, ISG_OPERR_CORRUPT);
        return 0;
    }

    isgdata->as_opstat = ISG_OPSTAT_SUCCESS;
    isgdata->as_operr = ISG_OPERR_NONE;
    return 1;
}

S32 iSGLoadFile(st_ISGSESSION* isgdata, const char* fname, char* databuf, S32 async)
{
    S32 numBytes = iSGFileSize(isgdata, fname);
    return iSGReadLeader(isgdata, fname, databuf, numBytes, async);
}

// Retail's I/O is synchronous even where the interface is not -- every
// operation has already finished by the time it returns, and this reports what
// it left behind. Host file I/O is the same.
en_ASYNC_OPSTAT iSGPollStatus(st_ISGSESSION* isgdata, en_ASYNC_OPCODE* curop, S32 block)
{
    if (curop != NULL)
    {
        *curop = isgdata->as_curop;
    }

    return isgdata->as_opstat;
}

en_ASYNC_OPERR iSGOpError(st_ISGSESSION* isgdata, char* errmsg)
{
    // Retail's strings, in en_ASYNC_OPERR order. They survive in the shipping
    // binary only because the array is static; nothing renders them.
    static const char* errmsgs[] = {
        "No current error",
        "No operation in async queue",
        "Too many async ops queued simultaneously",
        "Init Failed",
        "Unable to access Save Game Directory",
        "Access Error - no card ?!? (eg yanked out)",
        "Access Error - no room on card (file handles free bytes, etc)",
        "Access Error - card is damaged or something bad",
        "Access Error - file being loaded appears to be corrupt (I-Level)",
        "Access Error - general problem",
        "Save Error - Not enough free space to save file",
        "Save Error - during initalization (async queue)",
        "Save Error - during write",
        "Save Error - opening file",
        "Load Error - during initalization (async queue)",
        "Load Error - during read",
        "Load Error - opening file",
        "Target problem (general error)",
        "Target Error - media removed or changed",
        "Target Error - Not ready for I/O (unformatted?)",
        "Operation encountered unknown error",
        NULL
    };

    if (errmsg == NULL)
    {
        return isgdata->as_operr;
    }

    if (isgdata->as_operr < ISG_OPERR_NOMORE)
    {
        strncpy(errmsg, errmsgs[isgdata->as_operr], 0x80);
    }
    else
    {
        strncpy(errmsg, errmsgs[ISG_OPERR_UNKNOWN], 0x80);
    }

    errmsg[0x7f] = '\0';
    return isgdata->as_operr;
}

// Retail checks each save file's card metadata -- the icon header and the
// directory entry -- and reports the ones that do not hold up, so the game can
// offer to delete them. A file that is not there at all is not corrupt.
//
// The host has no such metadata, so the only damage visible at this level is a
// file that exists and cannot be read, or is empty. The save's own checksum is
// checked a layer up, in xSG_ld_validate.
S32 iSGCheckForCorruptFiles(st_ISGSESSION* isgdata, char files[][64])
{
    if (isgdata->slot < 0)
    {
        return 0;
    }

    memset(files, 0, ISG_NUM_FILES * 64);

    S32 ret = 0;
    for (S32 i = 0; i < ISG_NUM_FILES; i++)
    {
        char* name = iSGMakeName(ISG_NGTYP_GAMEFILE, NULL, i);

        char path[512];
        if (!iSG_path(isgdata, name, path, sizeof(path)))
        {
            continue;
        }

        iHostFileInfo st;
        if (!iHostStat(path, &st))
        {
            continue;
        }

        bool bad = !st.is_file || st.size == 0;

        if (!bad)
        {
            FILE* f = fopen(path, "rb");
            bad = (f == NULL);
            if (f != NULL)
            {
                fclose(f);
            }
        }

        if (bad)
        {
            strcpy(files[ret], name);
            ret++;
        }
    }

    return ret;
}

void iSGAutoSave_Startup()
{
}

st_ISGSESSION* iSGAutoSave_Connect(S32 idx_target, void* cltdata, void (*chg)(void*, en_CHGCODE))
{
    st_ISGSESSION* isgdata = iSGSessionBegin(cltdata, chg, 1);
    if (isgdata == NULL)
    {
        return NULL;
    }

    if (iSGTgtSetActive(isgdata, idx_target) == 0)
    {
        iSGSessionEnd(isgdata);
        return NULL;
    }

    return isgdata;
}

void iSGAutoSave_Disconnect(st_ISGSESSION* isg)
{
    iSGSessionEnd(isg);
}

// Retail also clears globals.autoSaveFeature here when the card has gone, which
// is the platform layer reaching into game state. It is not reproduced: the
// caller, XSGAutoData::HWCheckConnect, already returns this value, and
// zSaveLoad.cpp turns the feature off itself on a false. A save directory
// cannot be pulled out mid-frame the way a card can, so the branch that
// mattered on the console cannot fire here anyway.
S32 iSGAutoSave_Monitor(st_ISGSESSION* isg, S32 idx_target)
{
    if (isg == NULL)
    {
        return 0;
    }

    U32 state = iSGTgtState(isg, idx_target, NULL);
    if (state == 0 || (state & 1) == 0)
    {
        return 0;
    }

    return 1;
}
