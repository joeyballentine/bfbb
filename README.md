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

## The text says PC

The port runs on the Xbox release's assets, and its text talks about an Xbox.
The pause menu offers to "Reboot to Xbox Dashboard", an autosave asks you not to
turn off "your Xbox console", and the load screen names the save location as
"MEMORY CARD slot 1", which here is a folder. Some of it is older than that:
the shared menu archives still carry PlayStation 2 wording the Xbox release
never finished stripping, down to the DUALSHOCK.

That text is rewritten as it loads, in memory. Your archives are never touched,
so this is a real switch and not a one-way conversion. `platform_wording = off`
in `config.ini` gives you exactly what the disc says. It covers 235 whole
strings and 156 word swaps across the 63 archives, and it never rewrites
anything inside `{markup}`, where the names are lookup keys rather than prose.

`src/SB/Core/pc/iTextPatch.h` is the long version.

## Retail bugs it fixes

Two bugs in the original game that the port fixes instead of copying:

- **3D sound panned the wrong way.** A sound on your right came out of the left
  speaker. Only the GameCube release has this bug, and the port started out with
  it because `iSndCalcVol3d` was decompiled accurately. The community's Action
  Replay fix for the disc negates one constant, and this does the same. It is
  fixed only in the PC build, because the GameCube code has to stay byte
  identical.
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

GameCube assets won't work. The movies are Bink there and `.xmv` on Xbox, and
only `.xmv` can be played here.

If the folder is wrong or only partly extracted, the game prints an error and
exits before the window opens. It tells you the path it looked in and whether
`FONT.HIP` or `boot.HIP` was the file it could not find. Previously it got as far
as loading the font, where the original code waits in a loop for a file that
never arrives, so it looked like a hang.

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

## Settings

The game writes `config.ini` next to the executable the first time it runs. Every
setting is at its default, with a comment explaining it. Only `[assets] path`
has to be filled in — see **You need your own copy** above — and anything that
differs from the console behaviour can be turned back off.

### Display

```ini
[video]
mode = fullscreen
width = 1280
height = 720
ui = pillarbox
```

`mode` is `fullscreen` (exclusive D3D9, at the resolution the desktop is already
using), `borderless` (a window with no border covering one monitor), or
`windowed`.

The window mode and the render size are separate settings. The game renders at
`width` by `height`, and the result is scaled to fit whatever it is shown on. So
you can render at 640x480 and fill a 4K display, or render above 4K and have it
scaled back down.

The default size is 640x480, which is what the consoles used. Any size that is
not 4:3 gives you widescreen. The camera keeps the same vertical view and adds
width, so at 1280x720 you see more of the level to the left and right. Nothing
is stretched. `docs/RESOLUTION.md` covers this in detail, including what a
higher resolution does not improve.

`ui` controls where the interface is drawn when the render size is not 4:3. At
4:3 the two options do the same thing.

- `pillarbox` keeps the whole interface inside a centred 4:3 area, exactly as
  the console drew it. On a 16:9 screen the HUD sits some way in from each side.
- `native` moves the HUD out to the real screen edges. Each counter moves along
  with its icon and stays the same size.

Menus, textboxes and cutscene overlays stay in the 4:3 area under both options.
They are full-screen images, so there is nothing in them to move separately.

The menus themselves fill the screen either way. The backdrop is stretched to
the real screen size, the underwater light pattern is drawn across the whole
picture instead of the 4:3 area, and the bamboo frame is rebuilt with extra
copies of the bamboo texture it already repeats, so it gets wider without the
poles getting thicker.

### Scenery

```ini
[video]
draw_distance = on
alpha_cutout = on
```

The consoles stop drawing an object past a distance the level designer set, swap
distant objects for lower-detail versions, and clip the world itself at 400
units. A PC does not need any of those limits. Turning `draw_distance` off
restores all three exactly.

It does not affect fog. In a fogged level the far clip plane sits where the
picture is already fully fogged, so there is nothing behind it to reveal. It
also does not change how far away the game thinks things are, only what it
draws.

`alpha_cutout` changes how transparent parts of a texture are drawn. Foliage,
fences, grates and cave walls are solid shapes cut out of a texture.
RenderWare's D3D drivers draw these by blending, which leaves a partly
transparent border one texel wide around the edge of the shape. At the
resolution the art was drawn for, that border is a soft edge about a pixel wide.
At 4K it is about six pixels, and you can see the level's own sky through the
edges of cave walls.

With the setting on, that border is drawn fully solid up to a cutoff and
discarded past it, so the shape on screen matches the shape in the texture at
any resolution. `off` gives you the console behaviour. A number from 1 to 255
sets the cutoff, and `on` means 128. Glass, water, particles and the interface
are not affected, because they ask to be blended.

### Your own soundtrack

```ini
[audio]
soundtrack = D:\path\to\a\folder
```

Leave this empty, which is the default, to use the game's own music.

The game's music is mono. So is every other sound in it, all 3537 of them, and
this setting is mainly a way to play stereo versions instead. Whatever sample
rate and channel count a file has are used as they are.

Files are matched to tracks by name, so `music_00_hb_44.flac` needs no further
setup. If your files are named after the songs instead, as a soundtrack release
usually is, put a `soundtrack.txt` next to them listing `asset name = file`, one
per line.

Looping tracks loop at the point the game's version ended, not at the end of
your file. A soundtrack release usually adds a proper ending to a track that
loops in game, and without this you would hear that ending come round every
time.

If one file fails to decode, only that track is affected. It falls back to the
game's own music, and the rest of the folder still plays.

WAVE files work in any build. Other formats need a build with FFmpeg, the same
dependency the movies use.

### The rest

`[xbox]` has a switch for each of the four Xbox features described at the top of
this file. `[text]` has `platform_wording`. `src/SB/Core/pc/README.md` documents
the platform layer interface by interface and lists the `BFBB_*` switches.

## Saves

The save screens were written for a GameCube memory card, which measures space in
8 KB blocks. There is no memory card here, so every one of those figures was
really a byte count with "block(s)" printed after it. That is where "Available
Free Block(s): 2147483647 block(s)" came from.

Sizes are now shown in bytes with a suitable unit, such as "348 KB" or "1.5 MB",
and the "block(s)" that the surrounding text asset added is removed, so the
number and the label agree.

Both save slots on those screens now work. The save and load screens always drew
two buttons, because the memory card had two slots, but the second one did
nothing and choosing it gave an error. It is now a second folder with its own
three save slots, so there are six saves in total instead of three. The first
one is still the save directory itself, so saves made before this are still
where the game looks for them.

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

Working: rendering, world, characters, animation, collision, audio, music,
input, saves, HUD, menus, movies, loading screen. Widescreen works throughout,
including the camera, the HUD and the menus.

Not done: there is no GL3 window backend, so this is Windows only. The frame
rate is capped at 60, and `docs/UNCAPPED.md` explains what breaks without the
cap. The rest is a lot of smaller things. Individual source files have their own
notes on what is wrong and why, which are more useful than a list here.

## Credit

The decompilation is the work of the
[bfbbdecomp](https://github.com/bfbbdecomp/bfbb) project and its contributors.
The renderer is [librw](https://github.com/aap/librw) by aap, through a
[fork](https://github.com/joeyballentine/librw) with the changes this port needed.

This would not be possible without their prior efforts.
