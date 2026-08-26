# The RenderWare shim

librw is a *reimplementation* of RenderWare in namespace `rw::`, not an
implementation of the RenderWare C API. It exports 1124 `rw::` C++ symbols and
zero `Rw*`/`Rp*` C functions. re3 and reVC are not the precedent people expect
here: they were **written** against librw's API. This code is decompiled and
calls the RenderWare C API, and it has to keep calling it, because those call
sites are what the GameCube build matches against.

So the port needs this layer: the RenderWare C API, implemented on top of `rw::`.
As measured today: 124 RenderWare symbols referenced by the PC build, 119
defined here. `TODO.md` carries the list, the command that regenerates it, and
the two ways that command has been wrong.

## The thing to understand before adding to it

The two libraries agree on **value** types and disagree on **object** types.

`RwMatrixTag` and `rw::Matrix` are byte-identical -- `right`, `flags`, `up`,
`pad1`, `at`, `pad2`, `pos`, `pad3` -- because librw deliberately mirrors
RenderWare's layout there. `RwV3d` and `rw::V3d` are both three floats. Anything
built only from those can be reinterpret-cast across the seam at zero cost, and
that is what `value.cpp` does.

Objects are a different matter. Ours:

    struct RwFrame { RwObject object; RwLLLink inDirtyListLink;
                     RwMatrix modelling; RwMatrix ltm;
                     RwLinkList objectList; RwFrame *child, *next, *root; };

librw's:

    struct Frame { PLUGINBASE Object object; LLLink inDirtyList;
                   LinkList objectList; Matrix matrix; Matrix ltm; ... };

Same members, different order, plus a plugin block in front. They are not
interchangeable, and the game does not treat `RwFrame` as opaque -- there are
about 120 direct field accesses across 33 files, and `RwFrameGetMatrix(f)` is
a macro for `&(f)->modelling`. A shim that only wrapped calls would leave every
one of those reading the wrong bytes.

**That decision is settled: the port mirrors librw's layouts.** In a PC build
`RwFrame` is declared with librw's field *order* under RenderWare's field
*names*, guarded by `#ifndef PLATFORM_PC` (retail's struct) / `#else` (the
mirrored one) so the console keeps retail's own. An `RwFrame*` is then an
`rw::Frame*` and nothing converts at the seam.

`PLATFORM_PC` and not `GAMECUBE`, which is what the guards used to say: `-DGAMECUBE`
only covers `src/SB`, so `src/rwsdk`, `src/dolphin` and `src/bink` were silently
getting the port's layouts and `src/rwsdk` eventually stopped compiling.
`PLATFORM_PC` is a positive marker only the port defines, and it fails safe. See
commit `549e40c8`.

Two types do not fit that shape and are the documented exceptions.

`RwStream` is the first.
RenderWare's stream is a POD tagged union, librw's is an abstract class with a
vtable, and there is no assignment of RenderWare's names to librw's bytes that
would not be a lie. So `rwsdk` leaves it INCOMPLETE on PC and the shim defines
it in `stream.h`, deriving from `rw::Stream`. That works only because no game
code reads a stream's fields -- and leaving the type incomplete is what keeps
it that way, since any attempt is now a compile error.

`RpWorld` is the second, and it is the one to read before mirroring anything
else that librw only half has. librw's `World` is an `Object` and three linked
lists against RenderWare's fifteen members; eleven have no counterpart, and two
of those eleven -- `boundingBox` and `matList` -- are read by game code, so the
`RpAtomic` answer of dropping what will not fit was not available. What made it
fit was the mechanism RenderWare itself grows objects with: librw's `World`
carries `PLUGINBASE`, so `RpWorldPluginAttach` registers the missing eleven as a
plugin and they live in memory **librw allocated**, at an offset **librw handed
out**. That is the whole difference between this and appending a member to a
struct somebody else mallocs, which is a write past the end of the object. Two
things make it safe and both are non-negotiable for the next type that needs it:
the C declaration's tail offset is `static_assert`ed to equal `sizeof` librw's
struct, and the offset librw actually returns is checked against it at attach
time. See `world.cpp` and `layout_world.cpp`.

Converting instead was considered and does not work: the game holds `RwFrame*`
for an entity's lifetime and writes `->modelling` directly, while librw mutates
the same objects through its own parent/child links. Two copies would need
syncing both ways, and direct field access offers no call boundary to hook.

The rule that keeps this honest: **a type mirrored in `include/rwsdk` gets its
offsets asserted in the same commit** -- in `layout.cpp`, or in a `layout_*.cpp`
beside it when a group is big enough to be worked on separately, as the
asset-reading path's types are in `layout_stream.cpp`. The failure it guards
against is silent -- game code keeps compiling and starts reading the wrong
field. `RwFrame` is done; the rest are listed in `TODO.md`.

## Building it

librw is not vendored, so nothing here is in `CMakeLists.txt` yet -- wiring it
in would break a build that has no librw to link against. To work on it:

    git clone --depth 1 https://github.com/aap/librw
    cmake -S librw -B librw-build -G Ninja       -DCMAKE_CXX_COMPILER=clang++ -DLIBRW_PLATFORM=NULL -DLIBRW_TOOLS=OFF       -DCMAKE_CXX_FLAGS="-m32 -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH -Wno-everything"
    cmake --build librw-build

Then compile against `-Ilibrw -Ilibrw/src`. It builds clean 32-bit under clang
targeting the MSVC ABI, which was checked before any of this was written.

`LIBRW_PLATFORM=NULL` is the core with no renderer backend, which is all the
shim needs to compile against. `GL3` or `D3D9` come later, with a window.

## Checking it

A clean compile proves nothing here: the shim compiled and linked for a whole
commit before anyone found out that it could not create a frame, because librw's
engine had never been started. `tests/selftest.cpp` is the answer to that --
it runs the shim and checks the values that come back:

    clang++ -m32 -std=c++17 -Wno-everything -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH \
      -DPLATFORM_PC -DNON_MATCHING \
      -D_DEBUG -D_DLL -D_MT -Xclang --dependent-lib=msvcrtd \
      -Xclang --dependent-lib=ucrtd -Xclang --dependent-lib=vcruntimed \
      -Xlinker /NODEFAULTLIB:libucrt.lib -Xlinker /NODEFAULTLIB:libucrtd.lib \
      -Iinclude -Iinclude/rwsdk -Isrc/SB/Core/pc/compat -Isrc/SB/Core/pc \
      -Isrc/SB/Core/x -Isrc/SB/Game -Ilibrw -Ilibrw/src \
      src/SB/Core/pc/rw/tests/selftest.cpp src/SB/Core/pc/rw/*.cpp \
      librw-build/src/librw.lib -o rwselftest.exe

The CRT flags are the fiddly part and are not optional: librw was built debug,
and a mismatch here surfaces as unresolved `__imp__*` symbols that look like a
missing library rather than like the wrong CRT. CMake will handle all of it
once librw is vendored.

Add to `tests/selftest.cpp` whenever you add to this directory. The test is not
in `CMakeLists.txt` for the same reason the shim is not.
