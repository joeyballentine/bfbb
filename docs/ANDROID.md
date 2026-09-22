# The Android port

The PC port, built as an Android app. It runs on a Samsung Galaxy S24+
(Adreno 750) with both the GL3 and the Vulkan renderer.

Package: `io.github.joeyballentine.bfbb`. The app ships no game data. The
player supplies the files from their own copy of the Xbox release.

## Requirements

- arm64-v8a. Other ABIs build with `-Pbfbb.abis` (see below), but arm64 is the
  default and the only one tested on a device.
- Android 7.0 (API 24) or newer. `minSdk` and `ANDROID_PLATFORM` are both 24.
  `targetSdk` and `compileSdk` are 35.
- OpenGL ES 2.0 or newer for GL3. The manifest declares 2.0; librw asks for
  ES 3.1, then 3.0, then 2.0.
- Vulkan 1.3 for `video.backend = vulkan`. librw refuses an older loader or
  device.

## Building the APK

```
git submodule update --init --recursive
cd android
gradle assembleDebug
```

The APK is `android/app/build/outputs/apk/debug/app-debug.apk`.

- No Gradle wrapper is checked in. Use Gradle 8.9 or newer; the Android
  Gradle plugin is pinned to 8.7.3 in `android/build.gradle`.
- JDK 17. Android Studio's bundled JBR works.
- The Android SDK, and NDK `28.2.13676358` (r28c), pinned in
  `android/app/build.gradle`. Gradle installs it if it is missing.
- All submodules. The Vulkan backend needs `third_party/Vulkan-Headers`,
  `third_party/volk` and `third_party/VulkanMemoryAllocator`; CMake stops if
  one is empty. `third_party/wxWidgets` is not used.

Gradle builds the native code through `externalNativeBuild`, pointed at the
repository's own `CMakeLists.txt`. There is no second build system. Only the
`main` target is built.

The debug build type compiles native code as `RelWithDebInfo`. AGP's default
for debug is `-O0`.

### Gradle properties

| Property | Default | Effect |
| --- | --- | --- |
| `-Pbfbb.backends` | `GL3;VULKAN` | Passed as `BFBB_RENDER_BACKENDS`. `GL3` or `VULKAN` builds an APK with one renderer. |
| `-Pbfbb.abis` | `arm64-v8a` | Comma-separated `abiFilters`. `x86_64` builds for the emulator. |

### What CMake does differently on Android

Each is an `if(ANDROID)` block in `CMakeLists.txt`.

- The game is a shared library, `libmain.so` (target `main`), not an
  executable.
- SDL is the vendored submodule, linked statically into `libmain.so`.
- `BFBB_HOST_TOOLS` is off: `pc_selftest`, `rw_selftest`, `fps_selftest` and
  `fontfit` are not built.
- `BFBB_BUILD_CONFIGURATOR` defaults off. `bfbb_config` is a wxWidgets app.
- `zMain.cpp` is compiled with `-include android/iAndroidGameMain.h`.
- `bfbb_platform` and `main` link `liblog`.
- Setting `BFBB_BUILD_32BIT` on an Android build is an error. The ABI decides
  the word size.

## CI

`.github/workflows/android.yml` runs `gradle assembleDebug` for arm64-v8a on
pushes and pull requests that touch `android/`, `CMakeLists.txt`,
`src/SB/Core/pc/android/`, `iHostPosix.cpp`, `bfbb_main.cpp`,
`compat/intrin.h`, `tools/android/`, the workflow itself or `.gitmodules`.
It can also be started by hand.

It checks that the APK contains `lib/arm64-v8a/libmain.so`. Nothing is run.

On a push it publishes the APK to a rolling prerelease, `android-latest`,
deleted and recreated each run so the tag points at the built commit:

```
.../releases/download/android-latest/bfbb-arm64-v8a.apk
```

The job needs `permissions: contents: write`.

## First launch

`ProfileActivity` is the launcher entry.

1. If no game files are found, it opens `ImportActivity`.
2. On the first run with game files, it asks for a video profile: modern or
   vanilla. The answer is kept in the app's shared preferences.
3. It starts `BfbbActivity`, which runs the game.

### Importing the game files

`ImportActivity` explains what is needed and offers two choices. Both use the
system folder picker. The player picks the folder holding `boot.HIP` and
`font.HIP`, or the folder directly above it.

**Copy into the app.** Needs no permission. Copies into
`Android/data/io.github.joeyballentine.bfbb/files/assets`. Only the files the
game reads are copied:

- `.HIP` and `.HOP` files at the top level and in two-letter level folders
- `.xmv` files in `fmv/`
- `sb.ini`

The copy goes to `assets.importing` and is renamed to `assets` when complete.
If there is not room for two copies, the old `assets` is deleted first. The
screen stays on during the copy.

**Use the folder where it is.** Needs All files access
(`MANAGE_EXTERNAL_STORAGE` on Android 11+, `READ_EXTERNAL_STORAGE` on 10 and
below). The folder must be on the device's storage or an SD card. Its path is
saved and passed to the game as `BFBB_ASSETS`. Any earlier copy in the app's
directory is deleted.

A linked folder that no longer holds `boot.HIP` and `font.HIP`, or that the
app can no longer read, sends the next launch back to the import screen.

### Video profile

`BfbbActivity` passes the saved profile to the game as `BFBB_PROFILE`. It is
always set, defaulting to `modern`. `ResolveVideoProfile` in `iSystem.cpp`
reads it ahead of `video.profile` in `config.ini`.

- `modern`: the display's aspect ratio, 1080 lines or the display's height if
  lower, HUD at the screen edges (`video.ui = native`), draw distance
  unlimited.
- `vanilla`: 640x480, HUD in a 4:3 box, console draw distance.

### Launcher shortcuts

Long-press the app icon (`android/app/src/main/res/xml/shortcuts.xml`):

- **Play modern** and **Play vanilla** save that profile and start the game. A
  game already running keeps the profile it started with.
- **Import game files** opens the import screen.

## Playing

- **Touch controls** are on by default on Android (`input.touch_controls =
  auto`). A floating stick on the left half, A B X Y, L, R, Z, HUD and Start.
  A drag on the right half turns the camera through `iCameraLook`. Code:
  `iPadTouch.cpp` for input, `iTouchOverlay.cpp` and `rw/touch_overlay.cpp`
  for drawing. Both GL3 and Vulkan draw the overlay.
- **Controllers** work through SDL (`iPadHostSDL.cpp`). The touch controls
  hide while a controller on port 0 is in use and return when the screen is
  touched.
- **Back button** pauses. It is trapped with `SDL_HINT_ANDROID_TRAP_BACK_BUTTON`
  and sent to the game as Start.
- **Orientation** is landscape, either way up.
- **Game text** says "device" where the Xbox text says "Xbox console"
  (`iTextPatch.cpp`, `assets.platform_wording`).
- **Backgrounding** stops the game clock (`iTimeSuspend` / `iTimeResume`,
  from `WatchBackground` in `iPadHostSDL.cpp`).

## Files on the device

| What | Where |
| --- | --- |
| Game files (copied) | `/sdcard/Android/data/io.github.joeyballentine.bfbb/files/assets` |
| Log | `/sdcard/Android/data/io.github.joeyballentine.bfbb/files/bfbb-log.txt` |
| `config.ini` | internal storage: `files/config.ini` |
| Saves | internal storage: `files/bfbb/saves` |
| Button prompt glyphs | internal storage: `files/buttons/<set>/*.png` |

"Internal storage" is the app's private directory
(`/data/data/io.github.joeyballentine.bfbb/`). It needs `adb shell run-as` to
reach, and is wiped when the app is uninstalled or its storage is cleared.

`config.ini` is written with the defaults on the first run. Android-specific
defaults: `video.profile = modern` (overridden by `BFBB_PROFILE`),
`input.touch_controls = auto` (on). There is no settings screen on the device;
edit the file over adb:

```
adb shell run-as io.github.joeyballentine.bfbb cat files/config.ini > config.ini
# edit
adb push config.ini /data/local/tmp/
adb shell run-as io.github.joeyballentine.bfbb cp /data/local/tmp/config.ini files/config.ini
```

The button glyphs ship in the APK's assets, from `src/SB/Core/pc/res`.
`BfbbActivity.extractButtonGlyphs` copies them to internal storage once per
install or update, keyed on the package's last update time.

## Logs and crashes

stdout and stderr go to two places:

- logcat, tag `bfbb`, at INFO: `adb logcat -s bfbb`
- `bfbb-log.txt` in the external files directory, readable from a file
  manager

The file is truncated at each launch and capped at 8 MB. Lines printed before
`iAndroidStartup` knows the file's path are held in memory (up to 64 KB) and
written when it opens.

Fatal startup errors (missing game files, non-Xbox assets) also show an SDL
message box and go to logcat at ERROR.

On a crash, `FatalSignal` in `bfbb_main.cpp` prints `bfbb: CRASH --` with the
signal and a stack from `iHostPrintCallers`. On Android that walks the stack
with `_Unwind_Backtrace` and names frames with `dladdr`.

### Where it stopped

The startup output comes in a fixed order.

| Last line printed | Where it died |
| --- | --- |
| nothing, and no log file | `libmain.so` did not load or `SDL_main` was not found. Check logcat for `AndroidRuntime` and `SDL`. |
| `bfbb: PC port, renderers: ...` | between `dlopen` and `SDL_main` |
| `bfbb: android -- this log is also ...` | in `iSystemInit`: config, arena or asset check |
| `iMemInit: arena at ... is outside the low 4 GB` | the arena landed above 4 GB |
| `bfbb: FATAL -- the game's files were not found.` | the assets path; the next lines say where it looked |
| `bfbb: SDL opened a video device but no window or OpenGL context came up` | GL3 context creation |
| `bfbb: the Vulkan device did not come up` | Vulkan device creation |

`iAndroidStartup` prints the internal, external and assets paths. On GL3,
engine start prints the GL version, renderer, GLES or desktop profile, and
whether S3TC is present.

## How the platform pieces work

### Entry point

`SDLActivity` loads `libmain.so`, finds `SDL_main` with `dlsym`, and runs it on
its own thread.

- `BfbbActivity.getLibraries()` returns `{"main"}`. SDL is static, so there is
  no `libSDL3.so`.
- SDL's Java half is compiled from `third_party/SDL/android-project`, so both
  halves move with the submodule.
- `iAndroidGameMain.h` is force-included into `zMain.cpp` and renames `main` to
  `bfbbGameMain`. `zMain.cpp` is unchanged.
- `iAndroidMain.cpp` defines `SDL_main` with default visibility. It calls
  `iAndroidStartup`, then `bfbbGameMain`.

### Startup in two halves

`src/SB/Core/pc/android/iAndroid.cpp`:

- `iAndroidOpenLog` runs from the `StartupBanner` static constructor in
  `bfbb_main.cpp`, during `dlopen`, before the banner prints. It `dup2`s a
  pipe onto fds 1 and 2 and starts a thread that writes each line to logcat
  and the log file.
- `iAndroidStartup` runs at the top of `SDL_main`, once SDL's JNI is up. It
  sets the orientation and back-button hints, asks SDL for the app's
  directories, opens the log file, and sets `BFBB_ASSETS` to
  `<external>/assets` if that directory exists and `BFBB_ASSETS` is not
  already set.

`BfbbActivity.onCreate` runs before both and sets `BFBB_PROFILE`, and
`BFBB_ASSETS` for a linked folder, through `nativeSetenv`. It also creates the
`assets` directory and extracts the button glyphs.

### Host seam

`iHostPosix.cpp` has `__ANDROID__` arms. They read three environment
variables that `iAndroidStartup` sets from SDL:

| Variable | SDL call |
| --- | --- |
| `BFBB_ANDROID_INTERNAL` | `SDL_GetAndroidInternalStoragePath` |
| `BFBB_ANDROID_CACHE` | `SDL_GetAndroidCachePath` |
| `BFBB_ANDROID_EXTERNAL` | `SDL_GetAndroidExternalStoragePath` |

- `iHostExeDir` and `iHostUserDataDir` return the internal directory.
- `iHostTempDir` returns the cache directory.
- `iHostRunDetached` returns false.
- `iHostName` returns `"android"`.
- `iHostErrorBox` is in `iAndroid.cpp`, not `iHostPosix.cpp`: it logs at
  ERROR and shows `SDL_ShowSimpleMessageBox`.

Each reader returns false while its variable is unset.

### Arena below 4 GB

The game allocator addresses its arena as a `U32`. `iHostReserveLow` in
`iHostPosix.cpp` maps it by trying `mmap` hints from `0x04000000` upward in
64 MB steps, with `MAP_FIXED_NOREPLACE`. `iMemInit` exits if the result is
above 4 GB. arm64 Android has no 4 GB `__PAGEZERO`, and the arena lands low on
the S24+.

`tools/android/arena_probe.c` runs the same loop as a standalone program, for
checking a device without the game.

### GLES

librw's GL3 backend runs on GLES. On Android:

- The SDL3 profile loop tries GL 3.3, GL 2.1, ES 3.1, ES 3.0, ES 2.0, and
  takes the first where window, context and glad load all succeed.
- `gl3Caps.gles` selects the ES shader preamble.
- Without S3TC (`gl3Caps.dxtSupported` false, the normal case on Adreno and
  Mali), DXT textures are decompressed on the CPU.
- im2d and im3d colours are passed as RGBA; `GL_BGRA` is desktop-only.
- Skin bones are uploaded as three rows, to fit 256 vertex uniforms.
- librw does not call `find_package(OpenGL)` on Android. glad loads every
  entry point through `SDL_GL_GetProcAddress`.

### GL context and surface loss

- `BfbbActivity` lists every configuration change in `android:configChanges`,
  so rotation, keyboard and UI-mode changes do not recreate the activity.
- SDL keeps the EGL context across backgrounding and makes it current again on
  resume.
- Vulkan recreates the surface on `VK_ERROR_SURFACE_LOST_KHR` after the app
  returns from the background.
- librw's GL3 backend has no path to rebuild rasters, buffers or shaders. If a
  driver destroys the EGL context, SDL creates a new one and librw's objects
  are gone.

### Window

`iWindowSDL.cpp` makes the window fullscreen on Android in any mode other than
windowed, so the system bars hide. GL3's window is made fullscreen in
`iWindowDeferredCreated`.

## Testing on a phone

1. Enable wireless debugging and pair: `adb pair <ip>:<port>`, then
   `adb connect <ip>:<port>`. The serial `adb devices` prints can contain
   spaces; quote it in `-s`.
2. On Git Bash, `export MSYS_NO_PATHCONV=1` so device paths are not rewritten.
3. Build and install:

   ```
   cd android
   gradle assembleDebug
   adb install -r app/build/outputs/apk/debug/app-debug.apk
   ```

4. Watch the log: `adb logcat -s bfbb`.
5. Pull the log file:
   `adb pull /sdcard/Android/data/io.github.joeyballentine.bfbb/files/bfbb-log.txt`.

The screen locks after about 30 seconds without a touch, which pauses the
game. Keep it awake for timing runs.

Avoid the x86_64 emulator: its SwiftShader GPU pins the host CPU.

## Known issues

- The CI job fails. `android.yml` initialises only `third_party/librw` and
  `third_party/SDL`, and the default `GL3;VULKAN` build needs the three Vulkan
  submodules. `android-latest` does not currently exist.
- The release notes `android.yml` writes are out of date: they describe the
  assets folder as the only import route and mention the arena question as
  open.
- GL3 has no recovery if the driver loses the EGL context (see above).
- `video.profile` in `config.ini` has no effect when the game is started from
  the launcher, because `BFBB_PROFILE` is always set. `custom` cannot be
  chosen on the device.
- No settings screen on the device. `config.ini` is edited over adb.
- Using a folder in place needs All files access, which Google Play does not
  grant to games.
- Release builds are not configured for signing; CI publishes a debug-signed
  APK.
