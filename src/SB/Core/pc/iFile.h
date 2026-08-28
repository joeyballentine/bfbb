#ifndef IFILE_H
#define IFILE_H

#include <types.h>

enum IFILE_READSECTOR_STATUS
{
    IFILE_RDSTAT_NOOP,
    IFILE_RDSTAT_INPROG,
    IFILE_RDSTAT_DONE,
    IFILE_RDSTAT_FAIL,
    IFILE_RDSTAT_QUEUED,
    IFILE_RDSTAT_EXPIRED
};

// The GameCube and PS2 shapes of this struct are both in the gc header, chosen
// by #ifdef; this is the third. It is embedded by value in tag_xFile, which is
// a runtime type and never overlaid on asset data, so the layout is ours to
// choose.
struct tag_iFile
{
    U32 flags;
    char path[512];
    void* handle;
    S32 offset;
    S32 length;
    S32 asynckey;
};

#define IFILE_OPEN_READ 0x1
#define IFILE_OPEN_WRITE 0x2
#define IFILE_OPEN_ABSPATH 0x4

#define IFILE_SEEK_SET 0
#define IFILE_SEEK_CUR 1
#define IFILE_SEEK_END 2

struct tag_xFile;

void iFileInit();
void iFileExit();
U32* iFileLoad(char* name, U32* buffer, U32* size);
U32 iFileOpen(const char* name, S32 flags, tag_xFile* file);

// PC-only. The full path of the first file the game cannot run without that
// is not under BFBB_ASSETS, or NULL when they are all there. Asked once by
// iSystemInit, because the alternative is a hang: zMainLoadFontHIP spins on
// `while (xSTLoadStep('FONT') < 1.0f)` with no exit and no caller to fail to.
const char* iFileMissingAssetPath();
S32 iFileSeek(tag_xFile* file, S32 offset, S32 whence);
U32 iFileRead(tag_xFile* file, void* buf, U32 size);
S32 iFileReadAsync(tag_xFile* file, void* buf, U32 aSize, void (*callback)(tag_xFile*),
                   S32 priority);
IFILE_READSECTOR_STATUS iFileReadAsyncStatus(S32 key, S32* amtToFar);
U32 iFileClose(tag_xFile* file);
U32 iFileGetSize(tag_xFile* file);
void iFileReadStop();
void iFileFullPath(const char* relname, char* fullname);
void iFileSetPath(char* path);
U32 iFileFind(const char* name, tag_xFile* file);
void iFileGetInfo(tag_xFile* file, U32* addr, U32* length);
void iFileAsyncService();

#endif
