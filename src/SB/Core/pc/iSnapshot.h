#ifndef ISNAPSHOT_H
#define ISNAPSHOT_H

#include <types.h>

// PC-only: the last frame the port presented, kept so the loading screen can
// stand on it.
//
// The Xbox release drew its loading screen over a still of the level you were
// leaving; the GameCube and PS2 releases drew it over a texture asset instead.
// Everything else about that screen is the same on all three -- the bubbles
// rising over it are live particles on every platform, spawned fifty at a time
// by zFX_SpawnBubbleWall and run by the particle tank between load steps -- so
// the whole of the difference is which raster fills the background quad.
//
// The hook for it is still in the shipped GameCube code. zGameTakeSnapShot
// (zGame.cpp) is an empty function with a live call site in
// zGameScreenTransitionBegin, under its own eGameWhere_TransitionSnapShot
// marker, and the quad's UVs and tint next to it (bgu1..bga) are plain globals
// rather than constants. This fills that function in.
//
// **Why a capture rather than rendering the level live.** By the time the
// loading screen exists the outgoing level is gone: both scene-change paths
// (zMain.cpp's loop and the portal arm of zGameUpdateMode) run zGameExit, which
// takes the scene down and pops its memory, BEFORE zGameInit reaches
// zSceneInit and zGameScreenTransitionBegin. There is no world left to draw.
// Keeping one alive to draw would mean two scenes resident at once and a
// different scene lifetime; copying the last frame costs one blit.
//
// **Why no thread.** The loader already yields a frame per step -- zSceneInit
// calls zGameScreenTransitionUpdate between xSTLoadStep calls -- so the screen
// animates without one, which is the only thing a loader thread would buy.
// Against it: the loader is a synchronous state machine driven from the same
// loop that draws, xMemMgr is a bump allocator with no locking, and the device
// is created single-threaded.

// Off unless BFBB_LOADSNAP is set. Retail's static background is the default
// because it is what the console shipped; this is the Xbox behaviour offered,
// not substituted. Nothing outside the implementation asks: every function here
// answers for the feature being off on its own, and the switch is read once so
// that the per-frame test is a load.

// Copy what is about to be presented into the snapshot. Called once a frame
// from RwCameraShowRaster, which is the only place in the port that knows a
// frame is finished. Does nothing while latched, so the loading screen cannot
// photograph itself.
void iSnapshotCapture();

// Freeze the snapshot and hand it to the loading screen; zGameTakeSnapShot.
void iSnapshotLatch();

// Thaw, so the next frames refresh it again; zGameScreenTransitionEnd.
void iSnapshotRelease();

// The latched frame as a texture, or NULL when there is not one to give: the
// feature is off, no frame has been captured yet (the first boot has none), the
// backend cannot do it, or a device reset has emptied the surface since it was
// taken. Every caller has to have a fallback for NULL, and the fallback is the
// background the console draws.
//
// A texture rather than a raster because that is what the call site wants:
// zGameScreenTransitionUpdate's background test is a texture lookup whose
// result it then takes the raster from, and returning a texture leaves that
// expression exactly as the GameCube build compiles it.
struct RwTexture* iSnapshotBackgroundTexture();

#endif
