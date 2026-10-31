#ifndef IMODPATCH_H
#define IMODPATCH_H

// What GameCube mods change in the game's code. Retail has none of it; the PC
// port's pc/iModPatch.h does it in C++. Macros rather than inline functions, so
// the code around them compiles as it did.
#define iModPatchSpinHover() 0
#define iModPatchBowlFullTurn() 0
#define iModPatchScriptDone(waiting) (waiting)
#define IMODPATCH_ASSET_TYPES
#define IMODPATCH_ASSET_TYPE_COUNT 0
#define IMODPATCH_MAX_HITCHES 32
#define IMODPATCH_MAX_LASSO_TARGETS 64
#define iModPatchVarRead(v, context) ((v)->varCB(context))
#define iModPatchFindGoal(psy, gid) ((psy)->FindGoal(gid))
#define iModPatchRemainder(n, d) ((n) % (d))

#endif
