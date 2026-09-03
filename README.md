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
| Sound rolloff | `[xbox] sound_rolloff` | Mixes sound effects the way the xbox Directsound implementation does, fixing various sound issues like the Kelp Forest waterfall. |

### New on PC

| Feature | Setting | What it does |
| --- | --- | --- |
| Resolution | `[video] width`, `height` | Renders at any size and scales the result to the display. Any size that is not 4:3 gives widescreen: the camera keeps the same vertical view and adds width, so nothing is stretched. `docs/RESOLUTION.md`. |
| Window mode | `[video] mode` | Exclusive fullscreen, borderless, or windowed. Separate from the render size. |
| UI anchoring | `[video] ui` | The HUD either stays in a centred 4:3 box as the console drew it, or moves out to the real screen edges. |
| Field of view | `[video] fov` | Widens or narrows the camera from the game's own 75 degrees. Applied as a difference, so the cutscene cameras and the Cruise Bubble's zoom keep their relative angles. |
| Frame rate and vsync | `[video] framerate`, `vsync` | Any rate, the monitor's refresh rate, or uncapped. The port runs one simulation step per frame, so this is the speed of the game as well as the picture. `docs/UNCAPPED.md` lists what was converted off a per-frame rate and what has not been swept yet. |
| Antialiasing | `[video] msaa` | Multi-Sample Anti-Aliasing |
| Per-pixel lighting | `[video] per_pixel_lighting` | Sums the lights per pixel instead of per vertex, so curved surfaces on low-polygon models stop shading in flat facets. Affects characters and objects; the level's lighting is baked into its vertex colours and does not change. |
| Shadow resolution | `[video] shadow_resolution` | Character shadows scale with the render size instead of staying at the consoles' 256 pixels. |
| Draw distance | `[video] draw_distance` | Drops the per-object cull distance, the low-detail swap and the 400-unit world clip. Affects what is drawn, not what is simulated, and not fog. |
| Loading screen | `[video] load_time` | A host loads a scene faster than the loading screen's bubble animation can play, so it flickers past. Give it a number of seconds to hold the screen up for. It is a minimum, not a delay: a longer load waits for nothing. `fancy` holds it for nothing and instead wipes the still of the level you left off the new one, bottom to top. `off` for neither. |
| Soundtrack replacement | `[audio] soundtrack` | Play your own files instead of the game's music. The game's music is mono, as are all 3537 of its sounds, so this is mainly how to get a stereo soundtrack in. Looping tracks loop where the game's version ended, not where your file does. |
| Controllers | `[input] controller`, `[pad]`, `[keyboard]` | Controllers go through SDL, so any modern pad works and one `[pad]` section fits them all. Every button is remappable. |
| Stick tuning | `[input] deadzone`, `camera_sensitivity` | How much slack a stick has before the game sees it, and how fast the right stick moves the camera. |
| Boot straight into a level | `[game] boot` | Names a scene to start in, skipping the menu. Retail's `SB.INI` has the same switch, but it lives with the assets, so two instances share it; `config.ini` is per instance and wins over it. |
| Skip the logos | `[game] intro_movies` | Off goes straight to the title screen. |
| Save folder | `[game] save_folder` | Where saves go. Empty is this machine's per-user data folder. |
| Six save slots | none | The save and load screens always drew two buttons, because the memory card had two slots, but the second did nothing. It is now a second folder with its own three slots. The first is still the save directory itself, so existing saves are where the game looks for them. |
| Save sizes in bytes | none | Those screens measured a memory card in 8 KB blocks. There is no card here, so the figures were byte counts with "block(s)" printed after them, which is where "Available Free Block(s): 2147483647 block(s)" came from. Now shown as "348 KB" or "1.5 MB", with the stray label removed. |
| Custom font | `[text] font`, `font_sans` | The game's fonts are texture atlases authored for 640x480, so above that they are magnified and go soft. Point these at TrueType files and the same text is drawn from outlines at the size it is actually drawn. Layout, spacing and colour stay the game's. No font ships with the port; `tools/getfont.py` fetches one and `tools/fontfit` sizes it against the atlas it replaces. |
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

## Getting the assets

No assets are included. The port reads the **Xbox** release's files, which you
extract yourself (i.e. out of a disc image with an Xbox
ISO extractor such as [extract-xiso](https://github.com/XboxDev/extract-xiso)).
The path has to name the folder that DIRECTLY contains `boot.HIP`, `font.HIP`,
`fmv/`, `hb/`, etc, not a folder above it and not the image:

```ini
[assets]
path = D:\path\to\extracted\xbox\game
```

The game writes that file with the defaults the first time it runs, so start it
once, fill the path in, and start it again. Backslashes or forward slashes both
work, and a path with spaces in it does not need quotes.

As of right now, GameCube and PS2 assets do not work.

If the folder is wrong or only partly extracted, the game prints an error and
exits before the window opens. It tells you the path it looked in and whether
`FONT.HIP` or `boot.HIP` was the file it could not find.

## Building

Windows only (for now). The output is a 32-bit executable, which the game's data layouts
require (see the comment above `BFBB_BUILD_32BIT` in `CMakeLists.txt`).

Start to finish: install the tools, clone with submodules, optionally install
FFmpeg and libusb, run `build-release.bat`, point `bin\config.ini` at your Xbox
files, run `bin\bfbb.exe`.

### 1. Install the tools

| Tool | Why | Where |
| --- | --- | --- |
| Visual Studio 2019+ with the "Desktop development with C++" workload | clang links against the MSVC libraries and the Windows SDK. The IDE is never used; [Build Tools](https://visualstudio.microsoft.com/downloads/) alone is enough. The workload must include the x86 (32-bit) toolchain, which it does by default. | Microsoft |
| clang for Windows | The compiler. Put `clang++` on `PATH`. Built and tested with clang 16. | [LLVM releases](https://github.com/llvm/llvm-project/releases), or the "C++ Clang tools for Windows" component of the VS installer |
| CMake 3.16 or newer | The build system. | [cmake.org](https://cmake.org/download/) |
| Ninja | The generator. Put `ninja` on `PATH`. | [ninja-build releases](https://github.com/ninja-build/ninja/releases) |
| Git | For the clone and the submodules. | [git-scm.com](https://git-scm.com/) |
| Python 3 (optional) | `tools/*.py` and the `fpsdep` test. Nothing needs it to produce the executable. | [python.org](https://www.python.org/) |

`clang++ --version`, `cmake --version` and `ninja --version` all have to answer
before anything below works. You do not need to run `vcvarsall` yourself:
`build-release.bat` finds Visual Studio with `vswhere` and enters the x86
environment itself.

### 2. Get the source

```sh
git clone --recurse-submodules -b treedome https://github.com/joeyballentine/bfbb.git
cd bfbb
```

Already cloned without them:

```sh
git submodule update --init --recursive
```

The submodules are librw, which is the renderer, and SDL, which is the window,
the controllers, the keyboard and the audio device. Neither is optional and
neither is vendored, so a setup without `third_party/librw` or `third_party/SDL`
will not configure.

### 3. Optional dependencies

Both are found at configure time. Install them before the first build, or
delete the build directory afterwards so CMake looks again.

**FFmpeg** decodes the startup videos and any replacement soundtrack in a
format other than WAVE. It has to be a 32-bit build with headers and import
libraries, which most prebuilt Windows FFmpeg is not any more. vcpkg builds one:

```sh
git clone https://github.com/microsoft/vcpkg %USERPROFILE%\vcpkg
%USERPROFILE%\vcpkg\bootstrap-vcpkg.bat
%USERPROFILE%\vcpkg\vcpkg install "ffmpeg[core,avcodec,avformat,swresample,swscale]:x86-windows"
```

**libusb** is what SDL reads five controllers through: the Switch 2 Pro
controller, its two Joy-Cons, the Switch 2 GameCube controller and the original
GameCube adapter. Nothing is installed on the player's machine; those pads are
composite devices whose USB interface already carries Microsoft OS descriptors,
so Windows binds it itself and their HID interface is left alone.

```sh
%USERPROFILE%\vcpkg\vcpkg install libusb:x86-windows
```

`build-release.bat` looks in `%USERPROFILE%\vcpkg\installed\x86-windows` and
uses whatever is there. Set `BFBB_VCPKG` if your vcpkg is somewhere else. The
DLLs are copied next to the executable automatically.

Skipping FFmpeg is a supported configuration: CMake prints `FMV decoder: none`
and the game advances past movies as if they had played. Skipping libusb leaves
every other controller working; those five report themselves as devices with no
layout, which the game says at startup.

### 4. Build

```sh
build-release.bat
```

This is the main build script. It enters the 32-bit MSVC environment, configures
`build-release\`, builds, and puts `bfbb.exe` and the DLLs it needs in `bin\`.
`build-debug.bat` does the same into `build-debug\` and is unoptimised and slow.
Both take a render backend as their one argument:

| Backend | What it is |
| --- | --- |
| `D3D9` | Direct3D 9, on a Win32 window the port creates. The default, and the only one with the Xbox glow and the cruise-bubble effects. |
| `GL3` | OpenGL 3.3 on an SDL3 window, falling back through 2.1, GLES 3.1 and GLES 2.0. The backend that can eventually run off Windows. It is missing many ported xbox features. |
| `NULL` | No renderer. Headless, and what the self-tests run against. |

```sh
build-release.bat GL3
```

The build directory is per configuration, not per backend: `build-release.bat
GL3` reconfigures `build-release\` and rebuilds it, because the backend is baked
into the CMake cache and into librw's compile definitions. Every build writes
into `bin\` and overwrites the previous files. `bin\BUILD-INFO.txt` says which config and
backend is sitting there.

To configure by hand instead, from an x86 developer command prompt:

```sh
cmake -S . -B build-pc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build build-pc
```

Add `-DBFBB_RENDER_BACKEND=GL3` for the OpenGL build and
`-DCMAKE_PREFIX_PATH=%USERPROFILE%/vcpkg/installed/x86-windows` for FFmpeg and
libusb. `-m32` is set by `CMakeLists.txt` before `project()` and is not
something to pass yourself. This leaves the executable in `build-pc\`; add
`-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=<repo>/bin` to put it where the scripts do.

### 5. Run it

```sh
bin\bfbb.exe
```

The first run writes `bin\config.ini` with every setting at its default, then
stops with an error box, because there is nowhere to read the game's files from
yet. Put your Xbox asset folder in it as shown in **Getting the assets**
above, and run it again. `BFBB_ASSETS` overrides `[assets] path` when it is set,
which is how to run a build against a second extraction without editing
anything.

### When it fails

| Symptom | Cause |
| --- | --- |
| `lld-link: error: <root>: undefined symbol: mainCRTStartup` at configure time | The MSVC environment is x64. Use `build-release.bat`, or open an x86 developer command prompt. |
| `ERROR: clang++ is not on PATH` | clang is not installed, or its `bin` directory is not on `PATH`. |
| `ERROR: no Visual Studio installation found` | No VS, or no `vswhere.exe`. Install the C++ Build Tools. |
| `third_party/librw is empty` | The submodules were not cloned. `git submodule update --init --recursive`. |
| `FMV decoder: none` when you installed FFmpeg | The build directory was configured before the install. Delete `build-release\` and build again. |
| Switching `BFBB_BUILD_32BIT` does nothing | `CMAKE_CXX_FLAGS_INIT` is read once, when the language is enabled. Configure a fresh directory. |
| The game starts and the window is blank | Almost always the asset path. The startup check catches a missing `FONT.HIP` or `boot.HIP`, but not a folder holding the wrong extraction. |

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


## Credit

The decompilation is the work of the
[bfbbdecomp](https://github.com/bfbbdecomp/bfbb) project and its contributors.
The renderer is [librw](https://github.com/aap/librw) by aap, through a
[fork](https://github.com/joeyballentine/librw) with the changes this port needed.

This would not be possible without their prior efforts.
