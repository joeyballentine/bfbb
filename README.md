# Battle for Bikini Bottom: Unofficial PC port (WIP)

A native PC build of SpongeBob SquarePants: Battle for Bikini Bottom, compiled
from decompiled game code and librw. This is not an emulator or wrapper.

**This is still a work in progress.** I believe now it is in a state where it can be fully played through without any game breaking issues for casual play, but there are still many bugs that need to be fixed and it is definitely not ready for speedrunning yet (though many of the same tricks/glitches still work).

There are no downloads or releases. You have to build the game yourself and get the game assets from the xbox version.

The xbox assets are live-patched to change xbox wording to pc terminology for a better experience. There is currently no support for GameCube or PS2 assets (which are lower quality anyway, so this is the better PC-like experience anyway).

This branch (`treedome`) is the PC port. For the full decomp it is based on, see the `duplotron` branch.

This project is LLM-driven. I am trying to make it as good of an experience as possible, but I am not a C++ expert nor am I that knowledgeable on renderware semantics. An official hand-made PC port made by people that know what they are doing will surely come at some point, but for now this is the best we have.

For the original/official decomp repo, see
[bfbbdecomp/bfbb](https://github.com/bfbbdecomp/bfbb).

Note that the goal is not a period-accurate port (though it can definitely be run that way), but rather a modern implementation with many extra features. Since this is a personal project, I will be implementing things as I personally see fit. The official PC port, whenever that comes, will surely be more conservative in what it adds. 

## How it works

The original game as programmed by Heavy Iron was designed with multi-platform compiling in mind. 
The platform-specific game code is segmented into `i*` interfaces, separate from the rest of the game code. The port
reimplements those (as well as implementing new ones) for PC.

Everything outside of that (`src/SB/Core/x` and `src/SB/Game`) is the same code
the GameCube build uses. Changes there have to keep the GameCube build byte identical using build flags.

## What the port adds

Everything here is on by default and has a switch in `config.ini`. See
**Settings** below.

### Xbox parity

The GameCube release has these stubbed out: the functions are empty in the
decomp but still have live call sites, and the Xbox version implements them. They
were recovered from the `.xbe`.

| Feature | Setting | What it is |
| --- | --- | --- |
| Glow | `[xbox] glow` | The full-screen bloom. A bright pass, two blurs, composited back over the frame. Both shaders decoded from the Xbox build's pixel shader definitions. |
| Cruise Bubble distortion | `[xbox] distortion` | Swirls the picture while you fly the Cruise Bubble. The offset map is a genuine Xbox asset, `BXCruiseBubbleDistort`, already shipping in `plat.HIP`. |
| Loading screen still | `[xbox] snapshot` | The frame you just left, behind the rising loading bubbles, instead of the GameCube backdrop. Falls back to the backdrop on the first load of a run. |
| Cave reverb | `[xbox] reverb` | In the Mermalair and the caves. The game side already worked on both consoles; `iSndSetEnvironmentalEffect` was the empty part. The Xbox's reverb is DSP microcode that is not on the disc, so its twelve I3DL2 parameters were read out of the binary and fed to a reverb built on Microsoft's published I3DL2 design. |

### New on PC

| Feature | Setting | What it does |
| --- | --- | --- |
| Resolution | `[video] width`, `height` | Renders at any size and scales the result to the display. Any size that is not 4:3 gives widescreen: the camera keeps the same vertical view and adds width, so nothing is stretched. `docs/RESOLUTION.md`. |
| Window mode | `[video] mode` | Exclusive fullscreen, borderless, or windowed. Separate from the render size. |
| UI anchoring | `[video] ui` | The HUD either stays in a centred 4:3 box as the console drew it, or moves out to the real screen edges. |
| Frame rate and vsync | `[video] framerate`, `vsync` | Any rate, the monitor's refresh rate, or uncapped. The port runs one simulation step per frame, so this is the speed of the game as well as the picture. `docs/UNCAPPED.md` lists what was converted off a per-frame rate and what has not been swept yet. |
| Antialiasing | `[video] msaa`, `alpha_to_coverage` | MSAA, plus alpha-to-coverage for the alpha-tested edges MSAA cannot touch: foliage, fences, grates, cave walls. |
| Per-pixel lighting | `[video] per_pixel_lighting` | Sums the lights per pixel instead of per vertex, so curved surfaces on low-polygon models stop shading in flat facets. Affects characters and objects; the level's lighting is baked into its vertex colours and does not change. |
| Shadow resolution | `[video] shadow_resolution` | Character shadows scale with the render size instead of staying at the consoles' 256 pixels. |
| Draw distance | `[video] draw_distance` | Drops the per-object cull distance, the low-detail swap and the 400-unit world clip. Affects what is drawn, not what is simulated, and not fog. |
| Loading screen time | `[video] load_time` | Keeps the loading screen up for a set number of seconds so its bubble animation has time to play, instead of flickering past at host loading speeds. A minimum, not a delay: a longer load waits for nothing. `off` loads as fast as the machine can. |
| Soundtrack replacement | `[audio] soundtrack` | Play your own files instead of the game's music. The game's music is mono, as are all 3537 of its sounds, so this is mainly how to get a stereo soundtrack in. Looping tracks loop where the game's version ended, not where your file does. |
| Controllers | `[input] controller`, `[pad]`, `[keyboard]` | Controllers go through SDL, so any modern pad works and one `[pad]` section fits them all. Every button is remappable. |
| Six save slots | none | The save and load screens always drew two buttons, because the memory card had two slots, but the second did nothing. It is now a second folder with its own three slots. The first is still the save directory itself, so existing saves are where the game looks for them. |
| Save sizes in bytes | none | Those screens measured a memory card in 8 KB blocks. There is no card here, so the figures were byte counts with "block(s)" printed after them, which is where "Available Free Block(s): 2147483647 block(s)" came from. Now shown as "348 KB" or "1.5 MB", with the stray label removed. |
| PC wording | `[text] platform_wording` | The Xbox text is rewritten as it loads, so nothing offers to reboot to the dashboard or calls a save folder a memory card. The files on disc are never touched. |

### Fixed bugs

Two bugs in the original game that the port fixes instead of copying:

- **3D sound panned the wrong way.** L and R were flipped on GameCube. The
  community's Action Replay fix for the disc was ported here.
- **The pause menu's bamboo frame is missing the rope at its corners.** The rope
  is painted on the ends of the horizontal poles, but the vertical poles are
  closer to the camera and are drawn afterwards, so they cover it up. The port
  swaps the two depths and draws the vertical poles first. This has nothing to do
  with the render size. The rope was invisible at 640x480 as well.

## You need your own copy

No assets are included. The port reads the **Xbox** release's files, which you
extract yourself. Put the folder with `boot.HIP`, `fmv/`, `hb/` and so on in
`config.ini`:

```ini
[assets]
path = D:\path\to\extracted\xbox\game
```

The game writes that file with the defaults the first time it runs, so start it
once, fill the path in, and start it again. Backslashes or forward slashes both
work, and a path with spaces in it needs no quotes.

GameCube and PS2 assets do not work.

If the folder is wrong or only partly extracted, the game prints an error and
exits before the window opens. It tells you the path it looked in and whether
`FONT.HIP` or `boot.HIP` was the file it could not find.

## Building

Windows only right now. Two renderers build: D3D9, which is the default, and
OpenGL 3.3 on SDL3 with `-DBFBB_RENDER_BACKEND=GL3`. The GL build is the one
that can eventually run elsewhere; nothing else in the port is ported off
Windows yet, so it does not today.

You need:

- clang targeting `i386-pc-windows-msvc`, plus the MSVC toolchain and Windows SDK
  it links against. The port is 32-bit and that isn't negotiable (see the comment
  above `add_compile_options(-m32)` in `CMakeLists.txt`).
- CMake 3.16+ and Ninja.
- The submodules: `git submodule update --init --recursive`. That is librw,
  which the renderer is, and SDL, which reads the controllers. A checkout
  without SDL still builds with `-DBFBB_INPUT_BACKEND=win32`, which reaches
  Xbox controllers through XInput and nothing else.

Then:

```sh
cmake -S . -B build-pc -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build-pc
```

Run it:

```sh
build-pc\bfbb.exe
```

The first run writes a `config.ini` beside the executable and stops, because
there is nowhere to read the game's files from yet. Put your asset folder in it
as shown above, then run it again. See **Settings** below for the rest.

`BFBB_ASSETS` overrides `[assets] path` when it is set, which is how to run a
build against a second extraction without editing anything.

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

### Switch 2 controllers (optional)

Five controllers are readable only over raw USB: the Switch 2 Pro controller,
its two Joy-Cons, the Switch 2 GameCube controller and the original GameCube
adapter. SDL drives those through libusb, which vcpkg will build:

```sh
vcpkg install libusb:x86-windows
```

Same `-DCMAKE_PREFIX_PATH` as above, and `libusb-1.0.dll` is copied next to the
exe automatically. Nothing is installed on the player's machine. Those pads are
composite devices whose USB interface already carries Microsoft OS descriptors,
so Windows binds it itself and their HID interface is left alone.

Skip it and every other controller still works. Those five report themselves as
devices with no layout, which the game says at startup.

## Settings

`config.ini` is written next to the executable on first run, with every setting
at its default and a comment saying what it does. Read that file for what the
values mean. Only `[assets] path` has to be filled in, and `BFBB_ASSETS`
overrides it when set.

A newer build appends settings an older `config.ini` predates rather than
rewriting it, so your edits survive an update.

Three things `config.ini` cannot tell you:

- The sticks are not remappable. The left one moves, the right one turns the
  camera, and on the keyboard that is WASD and IJKL.
- A Switch 2 controller is shared with Steam, and only one program can hold one
  at a time. Steam takes it whenever its Nintendo configuration support is on,
  and the port then reports it as a device it cannot use. Turn that off in
  Steam's controller settings, or add `bfbb.exe` to Steam as a non-Steam game
  and let Steam Input hand it over as a standard pad.
- A controller SDL does not recognise says so at startup, with its USB ids, and
  cannot be played on until it has a layout. Put a `gamecontrollerdb.txt` beside
  `bfbb.exe` to give it one. Use the community file of that name, or a single
  line from SDL's own gamepad mapping tool.

`src/SB/Core/pc/README.md` documents the platform layer interface by interface
and lists the `BFBB_*` build switches.

## Checking a change

The port shares source with the GameCube build, so first question is always
whether you broke the decomp:

```sh
python tools/gcgate.py     # DOL sha1 and matched function count, both must PASS
```

Then the port's own checks:

```sh
ctest --test-dir build-pc --output-on-failure
python tools/pclink.py
python tools/pcprogress.py --drift --m32 --cc clang++
```

`ctest` runs four: `pc_selftest` over the platform layer, `rw_selftest` against
a live librw engine, `fps_selftest` over the rate helpers, and `fpsdep`, a
static sweep for code that measures time in frames. That last one fails on
anything not in `tools/fpsdep.json`, so a new per-frame rate in shared code has
to be read and either fixed or recorded.

Edits to shared code go inside `#ifdef PLATFORM_PC` with the original expression
kept in the `#else`, unless you've actually measured that the unguarded version
still matches.

## State of things

Working: rendering, world, characters, animation, collision, audio, music,
input, saves, HUD, menus, movies, loading screen. Widescreen works throughout,
including the camera, the HUD and the menus.

Not done: this is still Windows only -- the OpenGL renderer builds and runs,
but the input, audio, movie and host layers underneath it are Win32. Two effects
are D3D9-only and go quiet under GL3: the Xbox glow and the cruise-bubble screen
warp, both for want of a way to sample the frame buffer. The frame rate is a setting rather than
a cap now, but the sweep behind it is not finished -- `docs/UNCAPPED.md` says
what is still keyed to a frame count. The rest is a lot of smaller things. Individual source files have their own
notes on what is wrong and why, which are more useful than a list here.

## Credit

The decompilation is the work of the
[bfbbdecomp](https://github.com/bfbbdecomp/bfbb) project and its contributors.
The renderer is [librw](https://github.com/aap/librw) by aap, through a
[fork](https://github.com/joeyballentine/librw) with the changes this port needed.

This would not be possible without their prior efforts.
