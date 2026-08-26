#ifndef ISAVEGAME_H
#define ISAVEGAME_H

// Saves on the GameCube live on a memory card, and the whole of this interface
// is shaped by that: cards are inserted and removed, formatted, run out of
// blocks, and are addressed by slot. A host filesystem has none of those
// states, but the game's save UI asks about every one of them, so the shape
// stays and each question gets the answer that is true on a host.
//
// The one structural difference from the gc header is st_ISG_MEMCARD_DATA,
// which is CARDFileInfo and CARDStat there. Both are runtime types; neither is
// ever overlaid on save data, so the layout is free.
//
// The PS2 DWARF for st_ISG_MEMCARD_DATA and st_ISGSESSION is reproduced in
// src/SB/Core/gc/isavegame.h; it is the reference for what each field meant.

#include <types.h>


enum en_ISG_IOMODE
{
    ISG_IOMODE_READ = 0x1,
    ISG_IOMODE_WRITE,
    ISG_IOMODE_APPEND
};

enum en_ISGMC_ERRSTATUS
{
    ISGMC_ERR_NONE,
    ISGMC_ERR_NOMEMCARD,
    ISGMC_ERR_MKDIR,
    ISGMC_ERR_OPEN,
    ISGMC_ERR_CLOSE,
    ISGMC_ERR_READ,
    ISGMC_ERR_WRITE
};

enum en_ASYNC_OPCODE
{
    ISG_OPER_NOOP,
    ISG_OPER_INIT,
    ISG_OPER_SAVE,
    ISG_OPER_LOAD
};

// This enum might be incorrect. The tooling choked on the enum values
// being 0xFFFFFFFF
enum en_ASYNC_OPSTAT
{
    ISG_OPSTAT_FAILURE = 0xFFFFFFFF,
    ISG_OPSTAT_INPROG = 0,
    ISG_OPSTAT_SUCCESS
};

enum en_ASYNC_OPERR
{
    ISG_OPERR_NONE,
    ISG_OPERR_NOOPER,
    ISG_OPERR_MULTIOPER,
    ISG_OPERR_INITFAIL,
    ISG_OPERR_GAMEDIR,
    ISG_OPERR_NOCARD,
    ISG_OPERR_NOROOM,
    ISG_OPERR_DAMAGE,
    ISG_OPERR_CORRUPT,
    ISG_OPERR_OTHER,
    ISG_OPERR_SVNOSPACE,
    ISG_OPERR_SVINIT,
    ISG_OPERR_SVWRITE,
    ISG_OPERR_SVOPEN,
    ISG_OPERR_LDINIT,
    ISG_OPERR_LDREAD,
    ISG_OPERR_LDOPEN,
    ISG_OPERR_TGTERR,
    ISG_OPERR_TGTREM,
    ISG_OPERR_TGTPREP,
    ISG_OPERR_UNKNOWN,
    ISG_OPERR_NOMORE
};

enum en_CHGCODE
{
    ISG_CHG_NONE,
    ISG_CHG_TARGET,
    ISG_CHG_GAMELIST
};

// One save "device". On the GameCube this wraps a CARDFileInfo and a CARDStat
// for the card in a slot; here it is a directory on disk playing the same
// role, so iSGTgtCount, iSGTgtState and the rest still have something to
// report on.
struct st_ISG_MEMCARD_DATA
{
    S32 inuse;
    S32 chan;
    S32 sectorSize;

    // Where this device's saves live. Slot 0 is the player's save directory;
    // a second exists so the "copy to the other card" UI has a target.
    char root[512];

    // Filled in by iSGTgtState, read by the save UI.
    S32 present;
    S32 formatted;
    S32 freeBytes;
    S32 freeFiles;
};

#define ISG_NUM_SLOTS 2
#define ISG_NUM_FILES 3
// Nothing outside isavegame touches a field of this -- xsavegame.cpp only
// passes the pointer along -- so the host build uses the names the PS2 DWARF
// gives them rather than carrying the GameCube's unk_ offsets forward.
struct st_ISGSESSION
{
    st_ISG_MEMCARD_DATA mcdata[ISG_NUM_SLOTS];

    // Index into mcdata, or -1 when no target has been selected.
    S32 slot;

    en_ASYNC_OPCODE as_curop;
    en_ASYNC_OPSTAT as_opstat;
    en_ASYNC_OPERR as_operr;

    en_CHGCODE chgcode;
    void (*chgfunc)(void*, en_CHGCODE);
    void* cltdata;

    // Set for the autosave session, which watches its target rather than
    // saving through it.
    S32 monitor;
};

enum en_NAMEGEN_TYPE
{
    ISG_NGTYP_GAMEDIR,
    ISG_NGTYP_GAMEFILE,
    ISG_NGTYP_CONFIG,
    ISG_NGTYP_ICONTHUM
};

S32 iSGStartup();
S32 iSGShutdown();
char* iSGMakeName(en_NAMEGEN_TYPE type, const char* base, S32 idx);
st_ISGSESSION* iSGSessionBegin(void* cltdata, void (*chgfunc)(void*, en_CHGCODE), S32 monitor);
void iSGSessionEnd(st_ISGSESSION* isgdata);
S32 iSGTgtCount(st_ISGSESSION* isgdata, S32* max);
S32 iSGTgtPhysSlotIdx(st_ISGSESSION* isgdata, S32 tidx);
U32 iSGTgtState(st_ISGSESSION* isgdata, S32 tgtidx, const char* dpath);
S32 iSGTgtFormat(st_ISGSESSION* isgdata, S32 tgtidx, S32 async, S32* canRecover);
S32 iSGTgtSetActive(st_ISGSESSION* isgdata, S32 tgtidx);
S32 iSGTgtHaveRoom(st_ISGSESSION* isgdata, S32 tidx, S32 fsize, const char* dpath,
                   const char* fname, S32* bytesNeeded, S32* availOnDisk, S32* needFile);
S32 iSGTgtHaveRoomStartup(st_ISGSESSION* isgdata, S32 tidx, S32 fsize, const char* dpath,
                          const char* fname, S32* bytesNeeded, S32* availOnDisk, S32* needFile);
U8 iSGCheckMemoryCard(st_ISGSESSION* isgdata, S32 index);
S32 iSGFileSize(st_ISGSESSION* isgdata, const char* fname);
char* iSGFileModDate(st_ISGSESSION* isgdata, const char* fname);
char* iSGFileModDate(st_ISGSESSION* isgdata, const char* fname, S32* sec, S32* min, S32* hr,
                     S32* mon, S32* day, S32* yr);
en_ASYNC_OPSTAT iSGPollStatus(st_ISGSESSION* isgdata, en_ASYNC_OPCODE* curop, S32 block);
en_ASYNC_OPERR iSGOpError(st_ISGSESSION* isgdata, char* errmsg);
S32 iSGReadLeader(st_ISGSESSION* isgdata, const char* fname, char* databuf, S32 numbytes,
                  S32 async);
S32 iSGSelectGameDir(st_ISGSESSION* isgdata, const char* dname);
void iSGMakeTimeStamp(char* str);
S32 iSGSetupGameDir(st_ISGSESSION* isgdata, const char* dname, S32 force_iconfix);
S32 iSGSaveFile(st_ISGSESSION* isgdata, const char* fname, char* data, S32 n, S32 async, char*);
S32 iSGLoadFile(st_ISGSESSION* isgdata, const char* fname, char* databuf, S32 async);
S32 iSG_mcidx2slot(S32 param1, S32* out_slot, S32* param3);
void iSGAutoSave_Startup();
st_ISGSESSION* iSGAutoSave_Connect(S32 idx_target, void* cltdata, void (*chg)(void*, en_CHGCODE));
void iSGAutoSave_Disconnect(st_ISGSESSION* isg);
S32 iSGAutoSave_Monitor(st_ISGSESSION* isg, S32 idx_target);
S32 iSGCheckForWrongDevice();
S32 iSGCheckForCorruptFiles(st_ISGSESSION*, char files[][64]);

#endif
