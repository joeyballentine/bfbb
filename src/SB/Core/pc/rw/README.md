# The RenderWare shim

librw is a *reimplementation* of RenderWare in namespace `rw::`, not an
implementation of the RenderWare C API. It exports 1124 `rw::` C++ symbols and
zero `Rw*`/`Rp*` C functions. re3 and reVC are not the precedent people expect
here: they were **written** against librw's API. This code is decompiled and
calls the RenderWare C API, and it has to keep calling it, because those call
sites are what the GameCube build matches against.

So the port needs this layer: the RenderWare C API, implemented on top of `rw::`.
112 functions, listed in `TODO.md`.

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
*names*, guarded by `GAMECUBE` so the console keeps retail's own struct. An
`RwFrame*` is then an `rw::Frame*` and nothing converts at the seam.

Converting instead was considered and does not work: the game holds `RwFrame*`
for an entity's lifetime and writes `->modelling` directly, while librw mutates
the same objects through its own parent/child links. Two copies would need
syncing both ways, and direct field access offers no call boundary to hook.

The rule that keeps this honest: **a type mirrored in `include/rwsdk` gets its
offsets asserted in `layout.cpp` in the same commit.** The failure it guards
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
