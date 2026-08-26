# `src/SB/Core/pc` — the PC platform layer

The GameCube implementation of the game's platform interfaces lives in
`src/SB/Core/gc`. This is the same set of interfaces, for a host.

Everything here is phase 2 of `PCPORT.md`.

## How the two builds stay apart

Game code includes its platform headers unqualified — `#include "iTime.h"` —
and the include path decides which one it gets. `configure.py` puts
`src/SB/Core/gc` on CodeWarrior's; `CMakeLists.txt` puts `src/SB/Core/pc` on
the host's. Neither directory is ever on the other build's path, so neither
build can reach the other's platform layer even by accident, and no game source
file names a platform in an include.

Where a *shared* file genuinely has to differ, it branches on `GAMECUBE`, which
only the CodeWarrior build defines. `xTRC.h` and `include/types.h` both do.

None of that is the actual guarantee, though. The guarantee is that
`build/GQPE78/main.dol` still hashes to
`306526d90b48e99894c3138f5fc8f2716d9fecf6` after any edit to shared source. It
did after every edit made so far.

## What is implemented

`tools/pcprogress.py` measures the other half of the question — how much of
`src/SB` compiles against these headers. Right now **158 of 198 units (79.8%)**.

| interface | state | notes |
|---|---|---|
| `iTime` | **done** | `CLOCK_MONOTONIC`; tick rate is microseconds |
| `iMath` | **done** | `isin`/`icos`/`itan` on libm; 35 units call `isin` alone |
| `iMemMgr` | **done** | arena mapped low, because the game allocator addresses memory with `U32` |
| `iFile` | **done** | host filesystem, case-insensitive fallback, queued async |
| `iPad` | **done** | mapping onto `_tagxPad`; the device end is a backend |
| `iSystem` | **done** | boot sequence, `iVSync` paces to 60 Hz until a renderer exists |
| `iTRC` | **done** | the three entry points game code uses; no disc, no error screen |
| `iColor` | **done** | pure; copied from `gc` unchanged |
| `iSnd` (22 fns) | header only | audio. The largest group after rendering |
| `isavegame` (29 fns) | header only | host files; the header is written, the body is not |
| `iModel` (19) | header only | librw |
| `iMath3` (10) | header only | mostly pure computation, but the header is RW-typed |
| `iCollide` (10) | header only | pure computation |
| `iScrFX` (10) | header only | librw |
| `iEnv` (7), `iLight` (7) | header only | librw |
| `iAnim` (5), `iCutscene` (4), `iParMgr` (4), `iDraw` (3), `iMorph` (2), `iFX` (1), `iCollideFast` (1) | header only | librw |
| `iFMV` (1) | header only | Bink is proprietary; re-encode or drop |
| `ngcrad3d` | not ported | GameCube radiosity; no host counterpart |

43 of the 178 interface functions game code actually calls are implemented.
The count is what `src/SB` *references*, not what the headers declare — the
headers declare a good deal that nothing outside `src/SB/Core/gc` ever used,
and that surface has been dropped rather than reproduced.

## Files that are not interfaces

- **`compat/`** shadows `<math.h>`, `<string.h>` and `<cmath>` to add the
  CodeWarrior extensions the game's sources use — `FABS`, `stricmp`,
  `std::floorf` — chaining to the system headers with `include_next`. It is the
  host counterpart to `src/PowerPC_EABI_Support/include/math.h`, which is what
  the GameCube build sees. It must come first on the include path.
- **`iPadHost.h`** and **`iPadHostNull.cpp`** are the device end of input. The
  GameCube has one controller API that is always there; a host has several and
  none is guaranteed at build time, so the part that touches hardware is behind
  a seam and the part with the game's semantics in it is not. `null` reports no
  controllers, which is the correct answer for a build with no input library —
  the game shows its "please reconnect the controller" screen.
- **`VERBATIM.txt`** records the 16 headers copied unchanged from `gc/`, with
  the hash each was copied at. Copying is what keeps the two layers from
  sharing an include path, but it means a change to the `gc` header does not
  reach this one. `tools/pcprogress.py --drift` says when that has happened.
- **`tests/selftest.cpp`** exercises every implemented interface that does not
  need a renderer. The GameCube side is scored by a byte-identical DOL; a port
  has no such thing, so this is the substitute — 37 checks, so that
  "implemented" is a measurement rather than a claim.

## Building

```sh
cmake -S . -B build-pc -G Ninja
cmake --build build-pc
./build-pc/pc_selftest
```

This never invokes `configure.py`, `ninja` at the repo root, or CodeWarrior.

## Known differences from retail, on purpose

Each of these is a place where reproducing the GameCube exactly would have been
wrong on a host, and the reasoning is at the site:

- `iGetCurrFormattedDate` writes the year's tens digit with `% 10`. Retail uses
  `% 100`, which is only the tens digit while the year is under 2100.
- `iFileLoad` opens the path it built. Retail builds the full path and then
  opens the *relative* name, which is invisible when `iFileFullPath` is a
  `strcpy` and would defeat `iFileSetPath` here.
- `iMemInit` reserves twice `IMEM_DRAM_SIZE`. `xMemInit` puts `gxHeap[1]` and
  `gxHeap[2]` past the end of the DRAM block retail allocated; on the console
  those addresses land in unclaimed arena, here they would be a wild write.
- `iFileRead` zeroes the part of the buffer past end-of-file. The DVD returns
  whole sectors, so retail's callers get padding where `fread` would leave
  whatever was in the buffer before.
- `iPadStopRumble()` — the argument-less overload — stops every port instead of
  reading `globals.currentActivePad`. Nothing calls it, and it is not worth a
  dependency from the platform layer on the game layer.
