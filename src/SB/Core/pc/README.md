# `src/SB/Core/pc` — the PC platform layer

The GameCube implementation of the game's platform interfaces lives in
`src/SB/Core/gc`. This is the same set of interfaces, for a host.

Everything here is phase 2 of `PCPORT.md`. To pick the work up, start with
`PCPORT-HANDOFF.md` at the repository root.

## Running it, and the switches

    BFBB_ASSETS=<dir>   where the retail assets are. Required; without it the
                        game looks beside the executable and finds nothing.

Everything below is off unless set, and each costs a load per frame when it is.

    BFBB_FPS            what the port is actually presenting, once a second.
                        The frame rate is not cosmetic: zGame.cpp:559
                        substitutes 1/60 s for any frame it measures under ten
                        microseconds, so a game running far above 60 runs its
                        simulation too fast rather than merely looking smooth.
                        This is how to tell whether the frame rate or the thing
                        that looks wrong is at fault.

    BFBB_PAD            port 0's button mask when it changes, names and all.
                        Every test the game makes against a pad is a bare mask,
                        so when a button does nothing this says whether the
                        backend produced the bit or the game ignored it. Those
                        need different fixes.

    BFBB_TALK           why R1 next to a villager did or did not start a
                        conversation: the three conditions of the gate in
                        zNPCGoalVillager.cpp, and whether the talk goal was
                        pushed. Silent until the player presses R1.

    BFBB_SND=<n>        the first n sounds that reach the mixer: asset id,
                        rate, channels, sample count and whether the bytes were
                        found. An inaudible sound has failed at one of four
                        places -- not in the table, bytes did not read, mixed at
                        zero, or no device -- and this separates the first two
                        from the rest. Defaults to 32 if set to anything but a
                        number.

    BFBB_SNDMIX         once a second, every live voice with the gain it is
                        actually being mixed at, plus the loaded sound tables
                        and whether each one's package is still open. A sound
                        that is playing and inaudible and a sound that never
                        started look identical from outside.

    BFBB_MUSIC          once a second, the gate each music track is stuck at --
                        its handle, its timer, its queued situation, the game
                        mode and the fader. Everything between zMusicNotify and
                        xSndPlay is file-static in zMusic.cpp, so "never
                        notified" and "notified and never fired" are otherwise
                        indistinguishable. Also prints the calling stack
                        whenever a music voice is released, which is what found
                        zTalkBox stopping the level music.

    BFBB_SNDWHO=<aid>   the calling stack the first three times that asset is
                        started. The retail Xbox assets carry no ADBG names, so
                        an asset id is all a sound has, and the platform layer
                        sees the call with no context at all. This is what found
                        the HUD counter behind a sound playing six times over.

    BFBB_AUDIO=0        force the silent path in the win32 backend. Voices still
                        keep time exactly as they do with a device, so this is
                        the way to tell a bug in the mixer apart from a bug in
                        the game logic that waits on it.

    BFBB_SNDCACHE=<mb>  how much decoded audio to keep resident. Default 192.
                        Sounds are read on first play and kept; the cache evicts
                        least-recently-used entries that no voice is playing.

    BFBB_TEX            textures that did not convert, with the format that
                        defeated the conversion, and textures a material named
                        that the asset store does not have. A missing asset and
                        an unsupported pixel format look identical on screen and
                        have nothing in common.

    BFBB_WATCHDOG=<n>   every n seconds, print the main thread's stack. For
                        HANGS -- a crash announces itself and a hang does not.
                        Two identical traces mean a deadlock; two different ones
                        mean it is running and not finishing.

    BFBB_TEST_CRASH     fault immediately, to check the crash handler still
                        works.

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
`src/SB` compiles against these headers:

    compiles            198 / 198 units   100.00%
    pointer width only    0 / 198 units   0.00%
    needs other work      0 / 198 units

Compiling is not linking, and the port now does both: `tools/pclink.py` reports
0 unresolved symbols and `build-pc/bfbb.exe` runs far enough to bring RenderWare
up on a D3D9 device. See LINKING.md.

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
| `isavegame` | **done** | a directory per target; saves written atomically |
| `iSnd` | **done** | 22 entry points, plus 3D volume and pan; `win32` backend is a software mixer on WASAPI, `null` is silent but keeps time |
| `iModel` | **done** | not verbatim: RpAtomic has no interpolator on PC. Needed the RpHAnim group |
| `iParMgr` | **done** | verbatim; the particle renderer, and `gRenderArr`/`gRenderBuffer` |
| `iScrFX` | **done** | not verbatim: a `RwRasterLock` bracket a host must not honour. Its motion blur was cut from retail before ship |
| `iEnv`, `iLight`, `iAnim`, `iMorph`, `iMath3`, `iCollide`, `iCollideFast` | **done** | all verbatim copies — see VERBATIM.txt and PORTING.md |
| `iCutscene`, `iAnimSKB` | **done** | ported, not copied; see the notes in CMakeLists.txt |
| `iDraw` | **done** | `iDrawSetFBMSK` forwards to a `COLORWRITEMASK` render state added to the librw fork (`D3DRS_COLORWRITEENABLE` on D3D9). It had been a no-op, which made the depth-priming first pass at four call sites paint an opaque copy of itself; iDraw.cpp keeps that reasoning, because it is what the implementation had to satisfy |
| `iFX` (1 fn) | **refusal** | `iFXanimUVCreatePipe` returns NULL, which xFX.cpp:883 already handles: atomics keep their default pipeline and surfaces with animated texture coordinates draw static. Needs a texture matrix in librw |
| `iFMV` (1 fn) | **refusal, and will not be ported** | Bink is proprietary. Movies return "ran to the end" immediately so the game advances past them. The plan of record is ffmpeg ahead of time plus a different player; the file lists what that needs |
| `ngcrad3d` | not ported | GameCube radiosity; no host counterpart |

**All eleven are done.** Seven were byte-identical copies of their `gc` counterparts,
which is the finding PORTING.md is built around: most of this layer is not
GameCube code, it is RenderWare calls and game logic that happen to live in the
platform directory. Two needed one hunk each (`iModel`, `iScrFX`), and two are
deliberate refusals whose files say what is lost and what closing them costs
(`iFX`, `iFMV`). `iDraw` is implemented but lossy.

**This table has been wrong before.** It said "header only" for nine interfaces
that were already implemented, because four rounds of porting updated the code
and not the table. If you port something, edit the row.

## Files that are not interfaces

- **`compat/`** shadows `<math.h>`, `<string.h>`, `<cmath>` and `<mem.h>` to
  add the CodeWarrior extensions the game's sources use — `FABS`, `stricmp`,
  `strcmpi`, `std::floorf` — chaining to the system headers with `include_next`. It is the
  host counterpart to `src/PowerPC_EABI_Support/include/math.h`, which is what
  the GameCube build sees. It must come first on the include path.
- **`iHost.h`**, **`iHostPosix.cpp`** and **`iHostWin32.cpp`** are the seam
  between this layer and the operating system: 21 functions covering the
  monotonic clock, frame pacing, local time, the low-address arena, and the
  filesystem. Everything above the seam -- `iTime`, `iMemMgr`, `iFile`,
  `iSystem`, `isavegame` -- holds the mapping onto the game's semantics and is
  the same on every host. **An `#ifdef` for the host OS belongs in an
  `iHost*.cpp`, never above one.** `tools/pcprogress.py --host` checks that both
  backends implement everything the header declares, which nothing else would
  notice: the layer is only ever built for one host at a time, so a function
  added to one backend and forgotten in the other builds clean on that host and
  fails to link on the other.
- **`iSndHost.h`**, **`iSndHostWin32.cpp`** and **`iSndHostNull.cpp`** are the
  device end of audio, the same arrangement as input. `win32` mixes all 64
  voices in software and feeds one WASAPI stream; no host API offers 64
  independent voices with their own rates, and the ones that offer voices at all
  bring a submix graph and a threading model with them. Mixing here makes the
  resampling exact and asks the operating system for nothing but somewhere to
  put the result.

  Both backends **keep time**, and that is not decoration. zTalkBox holds a line
  until its clip finishes, cutscenes gate on `iSndIsPlayingByHandle`, and NPCs
  stagger barks by asking whether the last one is done -- a backend that
  finished everything instantly would desynchronise all of them, and it would
  look like the port getting the game code wrong rather than the audio. So
  `null` reports a voice as playing for exactly its sample length, and `win32`
  does the same on the wall clock when no device opens. **Failing to find an
  output device is a configuration, not an error.**
- **`iSndData.cpp`** is where the samples come from, and the answer is not the
  asset system. A scene's sound assets are all in one layer, `PKR_LTYPE_SRAM`,
  which the packer maps to `PKR_LDDEST_SKIP` -- on the GameCube that layer goes
  to ARAM, which the CPU cannot address, so it is never loaded into main memory
  and `xSTFindAsset` on a `SND ` returns NULL and always will. The bytes are
  found the way `src/SB/Core/gc/iSnd.cpp` finds them, through the table of
  contents, and read on first play into a capped LRU cache. Xbox ADPCM is
  decoded on the way in, so the mixer only ever sees 16-bit PCM.
- **`iPadHost.h`** and **`iPadHostNull.cpp`** are the device end of input. The
  GameCube has one controller API that is always there; a host has several and
  none is guaranteed at build time, so the part that touches hardware is behind
  a seam and the part with the game's semantics in it is not. `null` reports no
  controllers, which is the correct answer for a build with no input library —
  the game shows its "please reconnect the controller" screen.
- **`VERBATIM.txt`** records the 19 files copied unchanged from `gc/`, with
  the hash each was copied at. Sixteen are headers; three are implementations
  -- `iMath3.cpp`, `iCollide.cpp` and `iCollideFast.cpp` -- which turned out to
  be pure geometry and collision with no AX, GX, OS or DVD in them, so they
  port as they stand. Copying beats rewriting: there is nothing a rewrite would
  improve, and a copy that drifts is caught where a rewrite that drifts is not. Copying is what keeps the two layers from
  sharing an include path, but it means a change to the `gc` header does not
  reach this one. `tools/pcprogress.py --drift` says when that has happened.
- **`tests/selftest.cpp`** exercises every implemented interface that does not
  need a renderer. The GameCube side is scored by a byte-identical DOL; a port
  has no such thing, so this is the substitute — 113 checks, so that
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
- `iSGAutoSave_Monitor` does not clear `globals.autoSaveFeature`. Retail does,
  which is the platform layer reaching into game state; its caller already
  returns the same answer and `zSaveLoad.cpp` turns the feature off itself.
- `iSGSaveFile` writes to a temporary and renames. The console got atomicity
  from CARD, which updates the directory entry after the sectors; losing power
  mid-write here would otherwise leave a truncated save where the old one was.

## Where saves go

`BFBB_SAVE_DIR` if set, else `$XDG_DATA_HOME/bfbb/saves`, else
`$HOME/.local/share/bfbb/saves`, else `./saves`. Files keep retail's names —
`SpongeBob00` through `SpongeBob02` — so a save is called the same thing on
both platforms.

One target is exposed, which is the host equivalent of a single memory card
inserted: it stops the game asking which card to save to on every write.
`ISG_HOST_TARGETS` raises it, and `mcdata` is already sized for two.
