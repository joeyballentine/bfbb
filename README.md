# Battle for Bikini Bottom: Unofficial PC port (WIP)

A native PC and Android build of SpongeBob SquarePants: Battle for Bikini Bottom,
compiled from decompiled game code and librw. Not an emulator or a wrapper.

[Discord](https://discord.gg/5gcmHBVFzU) · Decomp base: the `duplotron` branch ·
Upstream decomp: [bfbbdecomp/bfbb](https://github.com/bfbbdecomp/bfbb) ·
[Docs](docs/README.md)

## Status

- The game can be played start to finish. Known bugs remain.
- Not ready for speedrunning. Many console tricks and glitches still work.
- No downloads or releases. You build it yourself.
- Needs the game files from the **Xbox** release. GameCube and PS2 files are not
  supported.
- The goal is a modern port with optional extras, not a period-accurate one.
  Every extra can be turned off.
- This project is written with an LLM. I am not a C++ or RenderWare expert, so
  expect code that someone with that background would write differently. A
  hand-written port by people who know the engine will come later and will
  likely be more conservative about what it adds. Until then, this is a
  personal project, and features go in as I see fit.

## Quick start (Windows)

1. Install Visual Studio Build Tools (C++ workload), clang, CMake, Ninja and Git.
2. `git clone --recurse-submodules -b treedome https://github.com/joeyballentine/bfbb.git`
3. Optionally install FFmpeg and libusb through vcpkg.
4. Run `build-release.bat`.
5. Run `bin\bfbb.exe` once. It writes `bin\config.ini` and stops.
6. Set `[assets] path` in `config.ini` to your extracted Xbox files. Run it again.

## Getting the assets

No assets are included. Extract the Xbox disc image yourself, for example with
[extract-xiso](https://github.com/XboxDev/extract-xiso). Point `[assets] path`
at the folder that directly contains `boot.HIP`, `font.HIP`, `fmv/` and `hb/`:

```ini
[assets]
path = D:\path\to\extracted\xbox\game
```

Either slash direction works. Paths with spaces need no quotes. The
`BFBB_ASSETS` environment variable overrides the setting.

If the folder is wrong or incomplete, the game exits before opening a window
and says which path it checked and which file (`FONT.HIP` or `boot.HIP`) was
missing.

## Building

These steps are for Windows. Linux and macOS build with plain CMake and the GL3
backend. [docs/BUILDING.md](docs/BUILDING.md) covers optional dependencies,
64-bit, MinGW, other platforms and configuring by hand. Android is
[below](#android).

### 1. Install the tools

- Visual Studio 2019+ or its Build Tools, with "Desktop development with C++".
- clang for Windows, with `clang++` on `PATH`. Tested with clang 22.
- CMake 3.16+ and Ninja, both on `PATH`.
- Git.

### 2. Get the source

```sh
git clone --recurse-submodules -b treedome https://github.com/joeyballentine/bfbb.git
```

The librw and SDL submodules are required. If you cloned without them:

```sh
git submodule update --init --recursive
```

### 3. Optional dependencies

- **FFmpeg** plays the startup movies. Without it the game skips them.
- **libusb** is needed for Switch 2 controllers and the GameCube adapter.

```sh
git clone https://github.com/microsoft/vcpkg %USERPROFILE%\vcpkg
%USERPROFILE%\vcpkg\bootstrap-vcpkg.bat
%USERPROFILE%\vcpkg\vcpkg install "ffmpeg[core,avcodec,avformat,swresample,swscale]:x86-windows" libusb:x86-windows
```

Install them before the first build, or delete `build-release\` afterwards.

### 4. Build

```sh
build-release.bat
```

This puts `bfbb.exe`, `bfbb_config.exe` and their DLLs in `bin\`.
`build-debug.bat` makes an unoptimised build.

The executable contains Direct3D 9, Direct3D 11 and OpenGL. `[video] backend`
picks one at startup. Vulkan is a separate build: `build-release.bat VULKAN`.

### 5. Run

```sh
bin\bfbb.exe
```

### When it fails

| Symptom | Cause |
| --- | --- |
| `ERROR: clang++ is not on PATH` | clang is missing or its `bin` is not on `PATH`. |
| `ERROR: no Visual Studio installation found` | Install the C++ Build Tools. |
| `third_party/librw is empty` | Run `git submodule update --init --recursive`. |
| `FMV decoder: none` after installing FFmpeg | Delete `build-release\` and build again. |
| The window opens blank | The asset path points at the wrong folder. |

More in [docs/BUILDING.md](docs/BUILDING.md#troubleshooting).

## Android

arm64, Android 7.0 or newer. Vulkan needs a Vulkan 1.3 driver.

Build with Gradle 8.9, JDK 17, the Android SDK, and the NDK version pinned in
`android/app/build.gradle`:

```sh
cd android
gradle assembleDebug
```

The APK is `android/app/build/outputs/apk/debug/app-debug.apk`. It contains
GL3 and Vulkan and uses GL3 by default. `-Pbfbb.backends=GL3` or
`-Pbfbb.backends=VULKAN` builds with only one.

On first launch, pick the Xbox game folder. The app either copies the files or
reads them in place (needs "All files access"). Then pick the `modern` or
`vanilla` video profile. Long-press the app icon to change the profile or import
again. Back pauses. A controller hides the touch controls.

Details in [docs/ANDROID.md](docs/ANDROID.md).

## Settings

`config.ini` is written next to the executable on first run, with every setting
and a comment explaining it. `bfbb_config.exe` edits it with a GUI. Android has
no configurator. A newer build adds new settings to an existing `config.ini`
without touching your edits.

Settings under `[experimental]` are off by default.

Things `config.ini` does not tell you:

- The sticks cannot be remapped. Left moves, right turns the camera. On the
  keyboard that is WASD and IJKL.
- Steam holds Switch 2 controllers while its Nintendo configuration support is
  on, and the port then cannot use them. Turn that off, or add `bfbb.exe` to
  Steam as a non-Steam game.
- A controller SDL does not recognise is reported at startup with its USB ids.
  Put a `gamecontrollerdb.txt` next to `bfbb.exe` to give it a layout.

[src/SB/Core/pc/README.md](src/SB/Core/pc/README.md) documents the platform
layer and the `BFBB_*` build switches.

## Features

### Xbox parity

The GameCube release has these as empty functions with live call sites. They
were recovered from the Xbox `.xbe`.

| Feature | Setting | What it is |
| --- | --- | --- |
| Glow | `[xbox] glow` | Full-screen bloom: a bright pass, two blurs, added back over the frame. Shaders decoded from the Xbox build. |
| Cruise Bubble distortion | `[xbox] distortion` | Warps the picture while flying the Cruise Bubble, using the Xbox offset map in `plat.HIP`. |
| Loading screen still | `[xbox] snapshot` | Shows the frame you just left behind the loading bubbles. |
| Cave reverb | `[xbox] reverb` | Reverb in the Mermalair and caves, using the Xbox's I3DL2 parameters. |
| Sound rolloff | `[xbox] sound_rolloff` | Uses the Xbox's DirectSound distance and volume curves. Fixes sounds such as the Kelp Forest waterfall. |

### Video

| Feature | Setting | What it does |
| --- | --- | --- |
| Renderer | `[video] backend` | Direct3D 9, Direct3D 11, OpenGL or Vulkan. |
| Video profile | `[video] profile` | `vanilla`: 640x480, 4:3 HUD, console draw distance. `modern`: native aspect up to 1080p, HUD at the screen edges, no draw distance limit. `custom`: the individual settings. |
| Resolution | `[video] width`, `height` | Any render size. Non-4:3 sizes are widescreen: the vertical view stays the same and width is added. Rendering above the display size supersamples. See [docs/RESOLUTION.md](docs/RESOLUTION.md). |
| Window mode | `[video] mode` | Fullscreen, borderless or windowed. |
| UI anchoring | `[video] ui` | HUD in a centred 4:3 box, or at the screen edges. |
| Field of view | `[video] fov` | Changes the camera's 75-degree FOV. Cutscene and Cruise Bubble cameras keep their relative angles. |
| Frame rate and vsync | `[video] framerate`, `vsync` | Any cap, the display's refresh rate, or uncapped. See [docs/UNCAPPED.md](docs/UNCAPPED.md). |
| Antialiasing | `[video] msaa` | MSAA. Off by default. |
| Per-pixel lighting | `[video] per_pixel_lighting` | Smooth shading on characters and objects. The level's baked lighting does not change. |
| Fixed-function mode | `[video] pipeline` | Direct3D 9 without shaders, for pre-2002 hardware. Unfinished: no glow, distortion, per-pixel lighting, toon shading or environment maps. See [docs/RENDERING.md](docs/RENDERING.md). |
| Shadow resolution | `[video] shadow_resolution` | Character shadows scale with the render size instead of staying at 256 pixels. |
| Draw distance | `[video] draw_distance` | Removes the per-object cull distance, the low-detail swap and the world clip. Does not change fog or simulation. |
| Loading screen | `[video] load_time` | Minimum seconds to show the loading screen, since loads are near-instant. `fancy` wipes from the old level to the new one instead. `off` for neither. |

### Audio, input and game

| Feature | Setting | What it does |
| --- | --- | --- |
| Soundtrack replacement | `[audio] soundtrack` | Plays your own music files instead of the game's mono music. |
| Controllers | `[input] controller`, `[pad]`, `[keyboard]` | Any SDL-supported controller. Every button is remappable. |
| Stick tuning | `[input] deadzone`, `camera_sensitivity` | Stick deadzone and right-stick camera speed. |
| Control presets | `[input] preset` | Xbox, PS2 or GameCube layout. `auto` follows the connected controller. |
| Button prompts | `[input] button_icons` | Xbox, GameCube, PS2 or custom icons from `buttons/`. Icons follow your bindings. |
| Touch controls | `[input] touch_controls` | On-screen controls. On by default on Android. |
| Boot into a level | `[game] boot` | Starts in the named scene, skipping the menu. Overrides `SB.INI`. |
| Skip the logos | `[game] intro_movies` | Goes straight to the title screen. |
| Save folder | `[game] save_folder` | Where saves go. Empty means the per-user data folder. |
| Six save slots | none | The unused second memory-card slot is now a second folder with three more slots. |
| Save sizes | none | The save screens show KB/MB instead of memory-card blocks. |
| Custom font | `[font] face`, `sans` | Draws text from TrueType fonts at the render size instead of the 640x480 atlases. `tools/getfont.py` fetches one. The other `[font]` keys tune the fit. |
| Mod folder | `[assets] mod` | Loads an Xbox mod's files in place of the originals without modifying the asset folder. Modded games use separate saves. |
| PC wording | `[assets] platform_wording` | Rewrites Xbox-specific text (dashboard, memory card) as it loads. |

### Experimental

| Feature | Setting | What it does |
| --- | --- | --- |
| Smoothed geometry | `[experimental] hipoly_assets` | Tessellates the level and models into curved surfaces at load and rebuilds collision to match. F8 toggles it. The other `hipoly_` keys tune it. |
| Toon shading | `[experimental] toon` | Cel shading with ink outlines. `solid_flat_props` gives flat props thickness for the outlines. The other `toon_` keys tune it. Shader pipeline only. |
| World lighting | `[experimental] world_lighting` | Lights the level at runtime instead of using baked vertex colours. `world_light_shadows` and `day_night_cycle` extend it. |

### Fixed bugs

The port fixes these bugs from the original game. The two with a setting can be
turned off for console-accurate behaviour.

| Bug | Fix | Setting |
| --- | --- | --- |
| 3D sound is panned to the wrong side on GameCube. | Swaps left and right, as the community's Action Replay code does. | none |
| The pause menu's bamboo frame has no rope at its corners. The rope is drawn on the horizontal poles, and the vertical poles are drawn over it. | Draws the vertical poles first. | `[fixes] menu_rope` |
| The sky is missing on Goo Lagoon's pier. The skydome sits beyond the far clip plane, which that level sets at the fog distance. | Shrinks the dome around the camera until it fits. It looks the same on screen. | `[fixes] sky_clip` |

## How it works

The game code comes from the [bfbbdecomp](https://github.com/bfbbdecomp/bfbb)
decompilation of the GameCube release. The port compiles that code with clang
instead of the GameCube's CodeWarrior compiler.

Heavy Iron wrote the game to build for several consoles. Code that talks to the
hardware (files, controllers, sound, rendering, memory cards) sits behind `i*`
interfaces such as `iFile`, `iPad` and `iSnd`. Each console had its own
implementation. The port adds a PC implementation in `src/SB/Core/pc/`, built
on SDL for the window, input and audio, and on librw for rendering.

librw is an open-source reimplementation of RenderWare, the engine the game
was built on. It reads the Xbox release's models, textures and levels, which
is why the port needs Xbox files.

All other game code (`src/SB/Game` and `src/SB/Core/x`) is shared with the
GameCube build of the decomp. That build must still produce a byte-identical
copy of the original executable, so port-only changes in shared code sit
behind build flags.

[docs/PCPORT.md](docs/PCPORT.md) covers the build split and how to check it.
[src/SB/Core/pc/README.md](src/SB/Core/pc/README.md) covers each interface.

## Credit

- Decompilation: [bfbbdecomp](https://github.com/bfbbdecomp/bfbb) and its
  contributors.
- Renderer: [librw](https://github.com/aap/librw) by aap, through a
  [fork](https://github.com/joeyballentine/librw).
- Platform layer: [SDL](https://github.com/libsdl-org/SDL).
- Movie and music decoding: [FFmpeg](https://ffmpeg.org/).
- Settings program: [wxWidgets](https://www.wxwidgets.org/).
