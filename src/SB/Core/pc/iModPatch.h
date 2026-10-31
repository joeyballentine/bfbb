#ifndef IMODPATCH_H
#define IMODPATCH_H

#include <types.h>

#include "xBehaviour.h"

// PC-only: what GameCube mods change in the game's code, done in C++.
//
// A GameCube mod can ship a DOL patch next to its assets. The port cannot run
// PowerPC code, so each patch it supports is written out here. The patches read
// bytes the GameCube leaves free in .sbss, which the mod's scenes set through
// eEventGivePowerUp (iGCMem.h); iModPatchByte gives those bytes PC storage.
//
// BFBB Deluxe 1.1.1 reads two of them:
// - 0x803CB8F4: when it is 1, dampen_velocity zeroes the vertical velocity, so
//   the bubble spin and the air melee hold the player in the air.
// - 0x803CB8F5: when it is not 0, PlayerAbsControl no longer cuts the bubble
//   bowl's turn rate to a tenth.
// Retail never writes either, so both are off in an unmodded game.

// The PC's stand-in for the free GameCube byte at `address`, or NULL.
U8* iModPatchByte(U32 address);

bool iModPatchSpinHover();
bool iModPatchBowlFullTurn();

// Script assets (SCRP). Retail has a _zScript object but no packer handler for
// its asset, so no package can make one. Mods built with Industrial Park add
// them, with the "Script Asset Support" code that every GameCube mod loader
// ships: one write puts 'SCRP' in assetTypeHandlers, the other turns
// zScriptUpdate's `state == WAITING` into `!=`, so a script that has run out
// of events sends eEventExpired. Retail packages hold no SCRP assets, so
// neither change reaches an unmodded game.
#define IMODPATCH_ASSET_TYPES { 'SCRP' },
#define IMODPATCH_ASSET_TYPE_COUNT 1

// Whether a script that ran out of events is done: `waiting` is retail's
// test, that the script is in ZSCRIPT_STATE_WAITING.
inline bool iModPatchScriptDone(bool waiting)
{
    return !waiting;
}

// zEntPlayer's trailer hitches. Retail keeps 32 and never checks; Second
// Strike's b305 has 35. On the GameCube the extra three land in sStankTag and
// work. On the PC they land on whatever the compiler put after the array.
#define IMODPATCH_MAX_HITCHES 64

// zLasso's guide lists, one per enemy Sandy can lasso. Retail keeps 64 and
// never checks; Second Strike's gl03, rb01 and sm04 need 65. On the GameCube
// the 65th lands on zLight's tables after scene init has filled them, and no
// light reads them in those levels; on the PC it wrote over zGoo's statics,
// and the next goo test faulted.
#define IMODPATCH_MAX_LASSO_TARGETS 256

// zConditional's read of a zVarEntry. The counter value, enabled and visible
// entries (0x0F to 0x11) read a field of an object found by ID, and retail
// reads it through NULL when the scene has no such object. Sock Expedition's
// mnu4 tests a counter that hb04 lacks. Dolphin, which GameCube mods are made
// on, returns 0 for those reads, and so does this.
#define iModPatchVarRead(v, context) iModPatchVarReadEntry((v)->entry, (v)->varCB, context)

inline U32 iModPatchVarReadEntry(U32 entry, U32 (*read)(void*), void* context)
{
    if (context == NULL && entry >= 0x0F && entry <= 0x11)
    {
        return 0;
    }
    return read(context);
}

// zNPCRobot's FindGoal('NGRj') before DieQuietly. A robot whose instinct has
// no dead goal gets the safe goal back, a smaller class, and DieQuietly's write
// to flg_deadinfo lands past it, on the first word of the next goal. On the
// GameCube that word is a list field; on the PC it is the vtable pointer, and
// the next call through it jumps to garbage. Second Strike's b306 duplotron
// adopts such robots. A missing goal gets a scratch one to write into.
inline xGoal* iModPatchFindGoal(xPsyche* psy, S32 gid)
{
    static U32 sScratch[64];

    xGoal* goal = psy->FindGoal(gid);
    if (goal == NULL || goal->GetID() != gid)
    {
        return (xGoal*)sScratch;
    }
    return goal;
}

// xGroup's pick of a random or next item, `n % itemCount`. A mod's group can
// be empty: BFBB Deluxe's gl03 has one, sent an event at scene setup. The
// GameCube's divwu by zero gives an undefined number and no fault, and the
// loop over no items ignores it; x86 faults.
inline U32 iModPatchRemainder(U32 n, U32 d)
{
    return d != 0 ? n % d : 0;
}

#endif
