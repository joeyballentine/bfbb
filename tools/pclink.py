#!/usr/bin/env python3
"""Link the PC port and classify what does not resolve.

WHY THIS EXISTS, given tools/pcprogress.py already reports 198/198.

pcprogress compiles each translation unit ALONE, with -fsyntax-only. That
answers "is this file valid C++ against the port's headers" and nothing else.
It is structurally blind to every defect that only appears when two objects are
put together, and the port's first full link found five of them in one run:

  * WEAK expanded to NOTHING on every compiler that is not CodeWarrior, because
    include/types.h stubs out __declspec. Every WEAK definition in the game was
    a strong definition here. iFileAsyncService collided with the port's own.
  * `globals` is defined in BOTH zMain.cpp and xCamera.cpp. CodeWarrior merges
    them as common symbols; C++ has no common symbols.
  * xSndPlay3D has one out-of-line body and N inline ones, which is what
    CodeWarrior emitted and what two units are matched against. A host linker
    calls that a duplicate.
  * Three RenderWare functions in the shim had C++ linkage where their callers
    expected C linkage -- including two written the same afternoon the first
    one was found and documented.

None of those can be seen by compiling. All of them are obvious the moment
something resolves symbols, which is the whole argument for this file.

Run it after `cmake --build build-pc`:

    python tools/pclink.py                 # summary
    python tools/pclink.py --list          # every unresolved symbol
    python tools/pclink.py --raw           # the linker's own output

A NOTE ON THE CRT. The probe links an empty translation unit alongside the
archives. That is not a formality: without a compiler-driver input, clang does
not emit the --dependent-lib directives that name the CRT, and eight ordinary
libc symbols (_realloc, _strtok, _feof ...) show up as unresolved. They are a
probe artifact and reporting them as port gaps would be a lie.
"""

import argparse
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build-pc"

ARCHIVES = [
    "bfbb_game.lib",
    "bfbb_platform.lib",
    "bfbb_rw.lib",
    "third_party/librw/src/librw.lib",
]

# The platform modules that have a header in src/SB/Core/pc and no
# implementation. Symbols belonging to these are expected, not news: they are
# the port's known worklist and they dominate the count.
UNPORTED_MODULES = [
    "iModel", "iEnv", "iLight", "iFX", "iScrFx", "iDraw",
    "iAnim", "iMorph", "iParMgr", "iFMV",
]

# The game spells this one both ways -- iScrFxBegin and iScrFXShutdown -- so
# matching on the source spelling would report two modules where there is one.
MODULE_ALIASES = {"iScrFx": "iScrFX"}

# Symbols that belong to an unported module but are not spelled with its name.
# Each was resolved by asking which object defines it in the GameCube build,
# which is what --explain does for anything new.
MODULE_GLOBALS = {
    "gLightWorld": "iLight",
    "gRenderArr": "iParMgr",
    "gRenderBuffer": "iParMgr",
    "giAnimScratch": "iAnim",
    "FastS16weight2": "iMorph",
    "iCameraMotionBlurActivate": "iScrFX",
    "iCameraSetBlurriness": "iScrFX",
}

RENDERWARE = ["_rpCollisionGeometryDataOffset", "AtomicDefaultRenderCallBack"]

# The OS libraries the port genuinely links against, for the same reason the
# empty translation unit above pulls in the CRT: without them a D3D9 build
# reports CreateWindowExA, Direct3DCreate9 and a dozen more as unresolved, and
# they are a deficiency of this probe rather than a gap in the port. CMake
# passes these through librw's own link interface and iWindowWin32.cpp's needs.
SYSTEM_LIBS = [
    "-luser32",
    "-lgdi32",
    "-lkernel32",
    "-ld3d9",
    # SymFromAddr and friends, for iHostPrintCallers.
    "-ldbghelp",
    # CoCreateInstance and friends, for the WASAPI backend in iSndHostWin32.cpp.
    # The audio interfaces themselves need no import library -- their GUIDs are
    # defined in that file rather than taken from uuid.lib.
    "-lole32",
]


def demangled_name(sym):
    """The bare identifier out of an lld-link message, mangled or not."""
    m = re.search(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", sym)
    if m:
        return m.group(1)
    return sym.lstrip("_?")


def classify(sym):
    name = demangled_name(sym)
    for mod in UNPORTED_MODULES:
        if name.startswith(mod) or mod in sym:
            return ("platform", MODULE_ALIASES.get(mod, mod))
    for global_name, mod in MODULE_GLOBALS.items():
        if global_name in sym:
            return ("platform", mod)
    for rw in RENDERWARE:
        if rw in sym:
            return ("renderware", rw)
    return ("game", name)


def run_link():
    if not BUILD.exists():
        sys.exit(f"{BUILD} does not exist -- configure and build it first:\n"
                 "  cmake -S . -B build-pc -G Ninja -DCMAKE_CXX_COMPILER=clang++\n"
                 "  cmake --build build-pc")

    missing = [a for a in ARCHIVES if not (BUILD / a).exists()]
    if missing:
        sys.exit("not built yet: " + ", ".join(missing) + "\n  cmake --build build-pc")

    cxx = shutil.which("clang++")
    if cxx is None:
        sys.exit("clang++ not found on PATH")

    anchor = BUILD / "pclink_crt_anchor.cpp"
    anchor.write_text(
        "// Empty on purpose. Its only job is to make the compiler driver emit\n"
        "// the --dependent-lib directives that name the CRT; see the module\n"
        "// docstring in tools/pclink.py.\n"
    )

    cmd = [
        cxx, "-m32", "-fuse-ld=lld-link",
        "-D_DEBUG", "-D_DLL", "-D_MT",
        "-Xclang", "--dependent-lib=msvcrtd",
        "-Xclang", "--dependent-lib=ucrtd",
        "-Xclang", "--dependent-lib=vcruntimed",
        "-Xlinker", "/NODEFAULTLIB:libucrt.lib",
        "-Xlinker", "/NODEFAULTLIB:libucrtd.lib",
        "-Xlinker", "/subsystem:console",
        "-Xlinker", "/errorlimit:0",
        str(anchor),
    ] + [str(BUILD / a) for a in ARCHIVES] + SYSTEM_LIBS + [
        "-o", str(BUILD / "pclink_probe.exe"),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, cwd=BUILD)
    return proc.stdout + proc.stderr


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true", help="print every unresolved symbol")
    ap.add_argument("--raw", action="store_true", help="print the linker's own output")
    args = ap.parse_args()

    out = run_link()
    if args.raw:
        print(out)
        return 0

    undefined = sorted(set(re.findall(r"undefined symbol: (.+)", out)))
    duplicate = sorted(set(re.findall(r"duplicate symbol: (.+)", out)))

    if duplicate:
        # A duplicate stops the linker before it resolves anything, so the
        # undefined count below is not meaningful until these are gone.
        print(f"DUPLICATE SYMBOLS ({len(duplicate)}) -- these mask the undefined list:")
        for d in duplicate:
            print(f"  {d}")
        print()

    groups = {"platform": {}, "renderware": [], "game": []}
    for sym in undefined:
        kind, key = classify(sym)
        if kind == "platform":
            groups["platform"].setdefault(key, []).append(sym)
        elif kind == "renderware":
            groups["renderware"].append(sym)
        else:
            groups["game"].append(sym)

    platform_total = sum(len(v) for v in groups["platform"].values())

    print(f"unresolved symbols: {len(undefined)}")
    print()
    print(f"  platform modules not yet ported   {platform_total:3d}")
    for mod in sorted(groups["platform"], key=lambda m: -len(groups["platform"][m])):
        print(f"      {mod:<12} {len(groups['platform'][mod]):3d}")
    print(f"  RenderWare the shim lacks         {len(groups['renderware']):3d}")
    print(f"  game code                         {len(groups['game']):3d}")

    if args.list:
        for title, items in (
            ("RenderWare", groups["renderware"]),
            ("game code", groups["game"]),
        ):
            if items:
                print(f"\n{title}:")
                for s in items:
                    print(f"  {s}")
        print("\nplatform modules:")
        for mod in sorted(groups["platform"]):
            print(f"  {mod}:")
            for s in groups["platform"][mod]:
                print(f"    {s}")

    return 0 if not undefined and not duplicate else 1


if __name__ == "__main__":
    sys.exit(main())
