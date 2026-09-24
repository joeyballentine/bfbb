#ifndef ISAVETHUMB_H
#define ISAVETHUMB_H

#include <types.h>

// PC-only: a still of the game, kept beside each save file and shown in the
// save list where retail showed a stock picture of the level.
//
// The picture is the frame the snapshot (iSnapshot.h) last copied, which is
// the last one presented. When that is is the caller's business: the pause
// takes one as the game stops, before the pause menu is drawn over it, and an
// autosave takes one on the spot. So the still follows xbox.snapshot: with it
// off there is no frame to take, and the list shows the stock pictures.
//
// On disk it is a small uncompressed TGA beside the save, "SpongeBob00.tga",
// which any image viewer opens.

struct RwTexture;

// Take a still from the last presented frame and keep it, replacing any kept
// before. Nothing is kept when there is no frame to take.
void iSaveThumbCapture();

// Whether a still is kept.
S32 iSaveThumbHave();

// Write the kept still to `path`. FALSE when none is kept or it could not be
// written.
S32 iSaveThumbWrite(const char* path);

// A texture of the still at `path`, or NULL for none. The caller owns it and
// frees it with iSaveThumbFree. `aspect` gets width over height.
RwTexture* iSaveThumbLoad(const char* path, F32* aspect);
void iSaveThumbFree(RwTexture* texture);

#endif
