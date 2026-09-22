# Building

The short version is in the [README](../README.md#building). This file covers
the rest: optional dependencies, other compilers, 64-bit, and configuring by
hand. Android is in [ANDROID.md](ANDROID.md).

## Tools

| Tool | Why | Where |
| --- | --- | --- |
| Visual Studio 2019+ with "Desktop development with C++" | clang links against the MSVC libraries and the Windows SDK. The IDE is not used; [Build Tools](https://visualstudio.microsoft.com/downloads/) is enough. The workload must include the x86 toolchain, which it does by default. | Microsoft |
| clang for Windows | The compiler. Put `clang++` on `PATH`. Tested with clang 22. | [LLVM releases](https://github.com/llvm/llvm-project/releases), or "C++ Clang tools for Windows" in the VS installer |
| CMake 3.16+ | Build system. | [cmake.org](https://cmake.org/download/) |
| Ninja | Generator. Put `ninja` on `PATH`. | [ninja-build releases](https://github.com/ninja-build/ninja/releases) |
| Git | Clone and submodules. | [git-scm.com](https://git-scm.com/) |
| Python 3 (optional) | `tools/*.py` and the `fpsdep` test. Not needed for the executable. | [python.org](https://www.python.org/) |

`clang++ --version`, `cmake --version` and `ninja --version` must all work.
`build-release.bat` finds Visual Studio with `vswhere` and enters the MSVC
environment itself, so `vcvarsall` is not needed.

## Submodules

`third_party/librw` (the renderer) and `third_party/SDL` (window, input, audio)
are required. The configure fails without them.

```sh
git submodule update --init --recursive
```

## Optional dependencies

CMake looks for these at configure time. Install them before the first build,
or delete the build directory afterwards.

Use the vcpkg triplet for the architecture you build: `x86-windows` for the
default 32-bit build, `x64-windows` for `x64`. Both can be installed side by
side.

**FFmpeg** decodes the startup movies and soundtrack files that are not WAVE.
It must match the build's architecture and carry headers and import libraries.
vcpkg builds one:

```sh
git clone https://github.com/microsoft/vcpkg %USERPROFILE%\vcpkg
%USERPROFILE%\vcpkg\bootstrap-vcpkg.bat
%USERPROFILE%\vcpkg\vcpkg install "ffmpeg[core,avcodec,avformat,swresample,swscale]:x86-windows"
```

Without it CMake prints `FMV decoder: none` and the game skips the movies.

**libusb** lets SDL read five controllers: the Switch 2 Pro controller, its two
Joy-Cons, the Switch 2 GameCube controller and the original GameCube adapter.
Players install no driver; Windows binds these devices through their Microsoft
OS descriptors. Without libusb, those five report no layout at startup and
every other controller still works.

```sh
%USERPROFILE%\vcpkg\vcpkg install libusb:x86-windows
```

**SDL3**, if installed, is linked instead of building `third_party/SDL`. This
saves compiling SDL once per build directory.

```sh
%USERPROFILE%\vcpkg\vcpkg install sdl3:x86-windows
```

Or build the submodule once and install it:

```sh
cmake -S third_party/SDL -B build-sdl -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_INSTALL_PREFIX=%USERPROFILE%/sdl3
cmake --build build-sdl --target install
```

Add `-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON` under MinGW.

CMake test-links against the SDL it finds and falls back to the submodule if
that fails. `-DBFBB_SDL=vendored` skips the search. `-DBFBB_SDL=system` fails
instead of falling back.

`build-release.bat` reads `%USERPROFILE%\vcpkg\installed\<arch>-windows`. Set
`BFBB_VCPKG` if vcpkg is elsewhere. The DLLs are copied next to the executable.

## Build scripts

`build-release.bat` enters the MSVC environment, configures `build-release\`,
builds, and copies `bfbb.exe`, `bfbb_config.exe` and their DLLs into `bin\`.
`build-debug.bat` does the same into `build-debug\`, unoptimised.

The first argument picks the renderers. The default Windows set is D3D9, D3D11
and GL3, all in one executable; `video.backend` chooses at startup.

| Backend | Notes |
| --- | --- |
| `D3D9` | The only one with the fixed-function path. |
| `D3D11` | |
| `GL3` | OpenGL 3.3, falling back through 2.1, GLES 3.1, 3.0 and 2.0. Windows, Linux, macOS, Android. |
| `VULKAN` | Vulkan 1.3. Windows and Android. Not in the default set: `build-release.bat VULKAN`. |
| `NULL` | No renderer. Used by the self-tests. |

```sh
build-release.bat GL3
```

The backend set is stored in the CMake cache, so changing it reconfigures and
rebuilds `build-release\`. Every build overwrites `bin\`.
`bin\BUILD-INFO.txt` records the configuration, backends and architecture.

The second argument is the architecture, `x86` (default) or `x64`:

```sh
build-release.bat D3D9 x64
```

x64 builds into `build-release-x64\` and reads the `x64-windows` vcpkg
triplet. 32-bit is the default because it is the configuration that gets
played; see the comment above `BFBB_BUILD_32BIT` in `CMakeLists.txt`.

## Configuring by hand

From an x86 developer command prompt:

```sh
cmake -S . -B build-pc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build build-pc
```

| Option | Effect |
| --- | --- |
| `-DBFBB_RENDER_BACKENDS=GL3` | Renderer list, semicolon-separated. |
| `-DCMAKE_PREFIX_PATH=%USERPROFILE%/vcpkg/installed/x86-windows` | Finds FFmpeg, libusb and SDL. |
| `-DBFBB_BUILD_CONFIGURATOR=OFF` | Skips `bfbb_config`. Otherwise, with no wxWidgets installed, the first build compiles `third_party/wxWidgets`, which is slow. |
| `-DBFBB_BUILD_32BIT=OFF` | 64-bit. Use an x64 prompt and the `x64-windows` prefix path. Needs a fresh build directory. |
| `-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=<repo>/bin` | Puts the executable where the scripts do. Otherwise it lands in `build-pc\`. |

`CMakeLists.txt` sets `-m32` itself. Do not pass it.

## MinGW

MinGW-w64 replaces clang and Visual Studio. It must be an **i686** toolchain:
the configure stops if the compiler produces 64-bit pointers.

```sh
cmake -S . -B build-pc -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build-pc
```

`build-release.bat` does not support MinGW.

## Linux and macOS

Plain CMake with the GL3 backend. Less tested than Windows.

```sh
cmake -S . -B build-pc -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-pc
```

## Troubleshooting

| Symptom | Cause |
| --- | --- |
| `lld-link: error: <root>: undefined symbol: mainCRTStartup` at configure | The MSVC environment does not match the architecture: an x64 prompt for a 32-bit build, or the reverse. Use `build-release.bat`. |
| `ERROR: clang++ is not on PATH` | clang is missing or its `bin` is not on `PATH`. |
| `ERROR: no Visual Studio installation found` | No VS or no `vswhere.exe`. Install the C++ Build Tools. |
| `third_party/librw is empty` | Submodules not cloned. `git submodule update --init --recursive`. |
| `FMV decoder: none` after installing FFmpeg | The build directory predates the install. Delete it and rebuild. |
| Changing `BFBB_BUILD_32BIT` does nothing | It is read once when the language is enabled. Use a fresh build directory. |
| The window opens blank | Usually the asset path points at the wrong extraction. The startup check only catches a missing `FONT.HIP` or `boot.HIP`. |
