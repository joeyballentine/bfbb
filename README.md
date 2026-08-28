# Battle for Bikini Bottom, PC port (WIP)

A native PC build of SpongeBob SquarePants: Battle for Bikini Bottom, compiled
from decompiled game code. This is not an emulator or wrapper.

**It's unfinished.** It boots and plays, but there are still bugs and unfinished parts. There are no downloads or releases. You build it yourself from your own copy of the game.

That being said, I believe now it is in a state where it can be fully played through without any game breaking issues, but I'm not entirely sure.

This branch (`treedome`) is the PC port. It sits on `duplotron`, which is the
decompilation it compiles. Both are AI-driven experiments built on
[bfbbdecomp/bfbb](https://github.com/bfbbdecomp/bfbb).

## How it works

The GameCube game's platform code sits behind 25 `i*` interfaces. The port
reimplements those for a PC: Win32 windowing, D3D9 via
[librw](https://github.com/aap/librw) instead of RenderWare, XInput and keyboard
instead of the GameCube pad, WASAPI instead of AX/MIX, FFmpeg instead of Bink.

Everything above that seam (`src/SB/Core/x` and `src/SB/Game`) is the same code
the GameCube build uses. It isn't a rewrite, so changes there have to keep the
GameCube build byte identical. That's checked on every commit.

## What it adds over the GameCube release

The GameCube version shipped with a few things stubbed out. The functions are
empty in the decomp but still have live call sites, and the Xbox release did
them for real, so they were recovered from that build rather than invented.

All four are on by default and each can be turned off in `config.ini`, which
the game writes with the defaults the first time it runs. Off, what is left is
what the GameCube release does.

- **Cruise Bubble distortion.** `xScrFxDistortionRender` and `distort_screen`
  are both empty on GameCube. On Xbox they swirl the picture while you fly the
  Cruise Bubble. The offset map is a genuine Xbox asset, `BXCruiseBubbleDistort`,
  which already ships in `plat.HIP`.
- **Glow**, what people usually call the Xbox version's bloom. A bright pass,
  two blur passes, then composited back over the frame. Both shaders were
  decoded from the Xbox build's pixel shader definitions.
- **The loading screen still.** `zGameTakeSnapShot` is empty on GameCube. On
  Xbox it grabbed the frame that the loading screen bubbles rise over. Falls
  back to the background asset when there is no previous frame, as on the first
  load of a run.
- **Cave reverb** in the Mermalair and the caves, which only the Xbox release
  has. The game side was never missing: `zSceneInitEnvironmentalSoundEffect`
  already picks the cave effect for nine scenes and `xSnd` forwards it. What is
  empty, on GameCube as well, is `iSndSetEnvironmentalEffect`, which is why
  neither console has any of it. The Xbox's reverb itself is DSP microcode that
  isn't on the disc and has never been disassembled, so it can't be copied. Its
  twelve I3DL2 parameters were read out of the Xbox binary and fed to a reverb
  built on Microsoft's published I3DL2 design instead. The settings are the
  Xbox's, the implementation is not.

One retail bug is fixed rather than reproduced:

- **3D sound panned the wrong way.** A sound on your right came out of the left
  speaker. That's retail's bug and only the GameCube release has it, so the port
  inherited it by reproducing `iSndCalcVol3d` faithfully. The community's Action
  Replay fix for the disc negates one constant, and this does the same thing.
  Fixed on the PC side only, since the GameCube code has to stay byte identical.

## You need your own copy

No assets are included. The port reads the **Xbox** release's files, which you
extract yourself. Point `BFBB_ASSETS` at the folder with `boot.HIP`, `fmv/`,
`hb/` and so on.

GameCube assets won't work. The movies are Bink there and `.xmv` on Xbox, and
only `.xmv` can be played here.

## Building

Windows only right now. The renderer is D3D9 and there's no GL3 window backend
yet, so other platforms stop at `RwEngineOpen`.

You need:

- clang targeting `i386-pc-windows-msvc`, plus the MSVC toolchain and Windows SDK
  it links against. The port is 32-bit and that isn't negotiable (see the comment
  above `add_compile_options(-m32)` in `CMakeLists.txt`).
- CMake 3.16+ and Ninja.
- The librw submodule: `git submodule update --init --recursive`

Then:

```sh
cmake -S . -B build-pc -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build-pc
```

Run it:

```sh
set BFBB_ASSETS=D:\path\to\extracted\xbox\game
build-pc\bfbb.exe
```

It writes a `config.ini` beside the executable on the first run, with every
setting at its default and a comment on each. That is where the Xbox features
above are turned on and off, and where the resolution is set:

```ini
[video]
width = 1280
height = 960
```

The default is the consoles' 640x480. Keep the ratio at 4:3 — the camera and
every 2D layer work in a 4:3 space, so 1280x720 is the same picture stretched
rather than a wider view. `docs/RESOLUTION.md` has the details, including what
raising it does not fix.

The same section holds the draw distance, which is on:

```ini
[video]
draw_distance = on
```

The consoles pop an object out of existence past a distance the level author
set, swap distant ones for lower-detail models, and clip the world itself at
400 units. None of that is a budget a PC has. Turning it off puts all three
back exactly. It leaves fog alone — a fogged level's far plane is where the
picture is already 100% fog, so there is nothing behind it to reveal — and it
does not make anything *think* further away than it used to, only draw.

`src/SB/Core/pc/README.md` describes it, and lists the `BFBB_*` diagnostics.

### Movies (optional)

FMV needs FFmpeg, and it has to be a 32-bit build with headers and import libs.
That's the annoying part: most prebuilt Windows FFmpeg is x64 only these days.
[vcpkg](https://github.com/microsoft/vcpkg) will build one:

```sh
vcpkg install "ffmpeg[core,avcodec,avformat,swresample,swscale]:x86-windows"
```

Then add `-DCMAKE_PREFIX_PATH=<vcpkg>/installed/x86-windows` to the cmake line.
The DLLs get copied next to the exe automatically.

Skip it if you don't care. CMake prints `FMV decoder: none` and the game just
advances past movies as if they'd played.

## Checking a change

The port shares source with the GameCube build, so first question is always
whether you broke the decomp:

```sh
python tools/gcgate.py     # DOL sha1 and matched function count, both must PASS
```

Then the port's own checks:

```sh
build-pc\pc_selftest.exe
build-pc\rw_selftest.exe
python tools/pclink.py
python tools/pcprogress.py --drift --m32 --cc clang++
```

Edits to shared code go inside `#ifdef PLATFORM_PC` with the original expression
kept in the `#else`, unless you've actually measured that the unguarded version
still matches.

## State of things

Working: rendering, world, characters, animation, collision, audio, input, saves,
HUD, movies, loading screen.

Not done: GL3 has no window backend, so this is Windows only. Beyond that it's
lots of smaller things. Individual files carry their own notes about what's
wrong and why, which are more useful than a list here.

## Credit

The decompilation is the work of the
[bfbbdecomp](https://github.com/bfbbdecomp/bfbb) project and its contributors.
The renderer is [librw](https://github.com/aap/librw) by aap, through a
[fork](https://github.com/joeyballentine/librw) with the changes this port needed.

This would not be possible without their prior efforts.
