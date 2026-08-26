#!/usr/bin/env python3
"""How much of the game code compiles for the PC port.

The GameCube build has report.json, which scores bytes against the retail DOL.
A port cannot be scored that way -- there is nothing to compare against -- so
this is the equivalent question it CAN answer: does the unit compile, on a
modern toolchain, against the PC platform headers?

That is a real gate and not a proxy. Anything that fails here is something the
port cannot build, and the failure names the reason.

Usage:
    tools/pcprogress.py                 summary
    tools/pcprogress.py --list          per-unit verdict
    tools/pcprogress.py --errors        first error from each failing unit
    tools/pcprogress.py xEnt zPlayer    only units matching these fragments
"""

import argparse
import collections
import concurrent.futures
import hashlib
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

INCLUDES = [
    "include",
    "include/rwsdk",
    "src/SB/Core/pc/compat",
    "src/SB/Core/pc",
    "src/SB/Core/x",
    "src/SB/Game",
]

# -fno-strict-aliasing is not a style choice here: the source is full of
# *(U32*)&someFloat, which exists because it made CodeWarrior emit the right
# instruction. See "Other things that will bite" in PCPORT.md.
CXXFLAGS = [
    "-std=c++17",
    "-fsyntax-only",
    "-fno-strict-aliasing",
    # No -fpermissive. It downgrades real errors to warnings, so the count came
    # out higher than any build that would actually succeed -- CMakeLists.txt
    # does not pass it, and the two disagreed.
    "-w",
    "-DPLATFORM_PC",
    "-DNON_MATCHING",
]


def units(filters):
    out = []
    for d in ("src/SB/Core/x", "src/SB/Game"):
        for p in sorted((ROOT / d).rglob("*.cpp")):
            rel = p.relative_to(ROOT).as_posix()
            if not filters or any(f in rel for f in filters):
                out.append(p)
    return out


def compile_one(path):
    cmd = ["g++", *CXXFLAGS]
    for inc in INCLUDES:
        cmd += ["-I", str(ROOT / inc)]
    cmd.append(str(path))
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    return path, r.returncode == 0, r.stderr


# Casting a pointer to U32/S32. On the GameCube a pointer is 32 bits and this
# is exact; on an LP64 host it truncates, and the game does it constantly --
# every asset-overlaid struct addresses memory with U32. This is the open
# question in PCPORT.md's "Asset caveats", not a defect to fix unit by unit, so
# it gets its own line rather than being mixed in with real porting work.
POINTER_WIDTH = re.compile(r"cast from .*\*.* to .* loses precision")


def all_errors(stderr):
    return re.findall(r"(?:fatal error|error): (.*)", stderr)


def pointer_width_only(stderr):
    errs = all_errors(stderr)
    return bool(errs) and all(POINTER_WIDTH.search(e) for e in errs)


def first_error(stderr):
    for line in stderr.splitlines():
        m = re.search(r"(fatal error|error): (.*)", line)
        if m:
            return m.group(2).strip()
    return "(no error line)"


def classify(msg):
    if re.search(r"No such file or directory", msg):
        m = re.search(r"([\w.\-/]+\.h)", msg)
        return f"missing header: {m.group(1) if m else '?'}"
    return msg[:90]


def check_drift():
    """The pc/ headers copied unchanged from gc/ can silently fall behind.

    VERBATIM.txt records the gc hash each was copied at. Copying is what keeps
    the two platform layers from sharing an include path, so it is the right
    trade -- but it has to be a measured one.
    """
    manifest = ROOT / "src/SB/Core/pc/VERBATIM.txt"
    if not manifest.exists():
        print("no VERBATIM.txt; nothing claimed to be a verbatim copy")
        return True

    stale, missing, ok = [], [], 0
    for line in manifest.read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        recorded, name = line.split()
        gc = ROOT / "src/SB/Core/gc" / name
        pc = ROOT / "src/SB/Core/pc" / name
        if not gc.exists() or not pc.exists():
            missing.append(name)
            continue
        now = hashlib.sha1(gc.read_bytes()).hexdigest()
        if now != recorded:
            stale.append((name, "gc header changed since it was copied"))
        elif hashlib.sha1(pc.read_bytes()).hexdigest() != now:
            stale.append((name, "pc copy was edited; it is no longer verbatim"))
        else:
            ok += 1

    for name in missing:
        print(f"  MISSING  {name}")
    for name, why in stale:
        print(f"  DRIFTED  {name}: {why}")
    print(f"{ok} verbatim headers still match gc/, {len(stale)} drifted, "
          f"{len(missing)} missing")
    return not stale and not missing


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("filters", nargs="*")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--errors", action="store_true")
    ap.add_argument("--drift", action="store_true",
                    help="check the headers pc/ copied verbatim from gc/")
    args = ap.parse_args()

    if args.drift:
        sys.exit(0 if check_drift() else 1)

    todo = units(args.filters)
    if not todo:
        sys.exit("no units matched")

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=16) as ex:
        for path, ok, err in ex.map(compile_one, todo):
            results.append((path, ok, err))

    results.sort(key=lambda r: r[0].as_posix())
    good = [r for r in results if r[1]]
    bad = [r for r in results if not r[1]]

    if args.list:
        for path, ok, err in results:
            rel = path.relative_to(ROOT).as_posix()
            print(f"{'ok  ' if ok else 'FAIL'} {rel}")
        print()

    if args.errors:
        for path, ok, err in bad:
            rel = path.relative_to(ROOT).as_posix()
            print(f"{rel}\n    {first_error(err)}")
        print()

    reasons = collections.Counter(classify(first_error(e)) for _, ok, e in bad if not ok)
    if reasons:
        print("blockers, most units first:")
        for reason, n in reasons.most_common(15):
            print(f"  {n:4d}  {reason}")
        print()

    total = len(results)
    ptr = [r for r in bad if pointer_width_only(r[2])]
    other = len(bad) - len(ptr)

    print(f"compiles           {len(good):4d} / {total} units   {100.0 * len(good) / total:.2f}%")
    print(f"pointer width only {len(ptr):4d} / {total} units   "
          f"{100.0 * len(ptr) / total:.2f}%   (see PCPORT.md, Asset caveats)")
    print(f"needs other work   {other:4d} / {total} units")


if __name__ == "__main__":
    main()
