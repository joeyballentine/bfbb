#!/usr/bin/env python3
"""Find code that measures time in frames rather than seconds.

Retail ran at a fixed 60 fps, so a great deal of game code does something once
per FRAME that should happen a fixed number of times per SECOND. The PC port
runs at any frame rate, and about sixty of those sites have been rebased onto
`dt` behind `#ifdef PLATFORM_PC`. Almost none of them are reachable from a unit
test -- they need a scene, a model, a player -- so what protects them is this:
the shapes are mechanically recognisable, every known one is recorded in
fpsdep.json, and a new one fails the build.

Four shapes, all of them checked only on the PC arm, so a fixed site reports its
guarded line and not the retail line beside it:

  timestep  a hardcoded console frame -- 1/60, 1/30, 59.999996, 119.99999.
            Sometimes a real unit conversion, which is why the baseline exists
  damping   `x *= 0.97f` once a frame settles at a rate set by the frame rate.
            The fix is the codebase's own `xpow(k, 60.0f * dt)`
  counter   `++` or `--` on a member or a file static inside a function taking
            dt. A counter ticked once a frame measures frames
  gate      a random draw against a constant on a per-frame path. A 4% chance
            per FRAME is four times the pops at 240 fps
  history   one sample pushed per frame into something with a fixed number of
            slots -- a ribbon's joint queue, a streak's fifty elements, a FIR
            ring. The window is then measured in frames, and no coefficient can
            be rebased to fix it: the samples have to arrive at a fixed rate.
            This shape is why the sweeps for the four above found nothing in the
            cruise bubble's wake, which was a quarter of its length at 240 fps

It is a lead generator, not a defect list. Roughly one in five of what it finds
is worth changing, and which one cannot be told without reading the function --
see docs/UNCAPPED.md, which names the six ways a false positive wears the same
clothes as a real one. The baseline is therefore a record of what has been
READ and judged, not of what is correct.

Usage:
  fpsdep.py                  report anything not in the baseline; exit 1 if any
  fpsdep.py --all            list every hit, baselined or not
  fpsdep.py --shape counter  only that shape
  fpsdep.py --update         rewrite the baseline from what is there now
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(ROOT, "tools", "fpsdep.json")

ROOTS = ["src/SB/Game", "src/SB/Core/x"]

# A console frame, in the spellings the codebase actually uses. 59.999996 and
# 119.99999 are the same thing on the other side of a division: an emitter rate
# set to a multiple of 60 immediately before a 1/60 window is spelling a
# particle COUNT.
TIMESTEP = re.compile(
    r"0\.0166666|0\.016666668|1\.0f\s*/\s*60\.0f|1\.0f\s*/\s*60\b"
    r"|0\.0333333|1\.0f\s*/\s*30\.0f|1\.0f\s*/\s*30\b"
    r"|59\.999996|119\.99999")

# `x *= 0.97f`, but not `*= 1.0f`, `*= -1.0f` or a normalise.
#
# The coefficient is not always a literal and the multiply is not always `*=`.
# A sweep that only matched `*= 0.97f` missed nine live sites: seven spellings
# of `npdata->vel *= fac_keep` in zNPCSupplement.cpp, King Jelly's
# `vel *= tweak.vel_decay`, and the chandelier's
# `xVec3SMul(&ent->vel, &ent->vel, 0.97f)`.
DAMPING = re.compile(
    r"\*=\s*-?0\.\d+f?\s*;"
    r"|\*=\s*[\w.>-]*(keep|decay|damp|drag|fric|falloff|slow)\w*\s*;"
    r"|xVec3SMulBy\s*\([^;]*,\s*0\.\d+f?\s*\)"
    r"|xVec3SMul\s*\([^;]*,\s*0\.\d+f?\s*\)",
    re.IGNORECASE)

# ++ or -- on something that survives the frame: a member, a this->, or a name
# that looks like a file static. A bare local is not interesting.
COUNTER = re.compile(r"(\+\+|--)\s*(this->|\w+(->|\.)\w+)|(\w+(->|\.)\w+|this->\w+)\s*(\+\+|--)")

# A random draw tested against a constant.
GATE = re.compile(r"(xurand\s*\(\s*\)|xrand\s*\(\s*\))\s*[<>]=?\s*[-\d.]"
                  r"|[-\d.]\w*\s*[<>]=?\s*(xurand\s*\(\s*\)|xrand\s*\(\s*\))"
                  r"|xUtil_yesno\s*\(\s*[\d.]"
                  r"|xrand\s*\(\s*\)\s*&\s*0x")

# A function whose body is a frame: it takes the frame's length.
DT_PARAM = re.compile(r"\b(F32|float|f32)\s*&?\s*(dt|seconds|timeDelta|elapsed)\b")

# A sample pushed into a bounded store. The container calls are the reliable
# half; the rolling index is the half that catches a hand-rolled ring, which is
# what zLasso's five-slot FIR is.
HISTORY = re.compile(
    r"\.(insert|push_front|push_back|push)\s*\("
    r"|->(insert|push_front|push_back|push)\s*\("
    r"|xFXStreakUpdate\s*\("
    r"|\b(head|tail|first|next|write|widx|windex)\s*(\+\+|=\s*\w+\s*\+\s*1\b)")

SHAPES = ("timestep", "damping", "counter", "gate", "history")


def pc_arm(lines):
    """Yield (lineno, text) for the lines that survive with PLATFORM_PC defined.

    Only PLATFORM_PC conditionals are resolved. Every other #if is treated as
    taken, because the point is to see the port's own code, not to preprocess.
    """
    stack = []

    for i, ln in enumerate(lines, 1):
        st = ln.strip()

        if st.startswith("#if"):
            is_pc = "PLATFORM_PC" in st
            negated = st.startswith("#ifndef") or st.startswith("#if !")
            stack.append([is_pc, (not negated) if is_pc else True])
            continue

        if st.startswith("#elif"):
            if stack:
                stack[-1][1] = not stack[-1][0]
            continue

        if st.startswith("#else"):
            if stack:
                stack[-1][1] = (not stack[-1][1]) if stack[-1][0] else True
            continue

        if st.startswith("#endif"):
            if stack:
                stack.pop()
            continue

        if all(live for _, live in stack):
            yield i, ln


def in_dt_function(rows):
    """Mark the rows that sit inside a function taking the frame's length.

    Brace depth, not a parser: a signature with a dt parameter opens a region
    that lasts until depth returns to where it started.
    """
    depth = 0
    dt_depth = None
    marked = []

    for lineno, ln in rows:
        code = re.sub(r"//.*", "", ln)

        marked.append((lineno, ln, dt_depth is not None))

        depth += code.count("{") - code.count("}")

        if dt_depth is None and DT_PARAM.search(code) and "(" in code and "{" in code:
            dt_depth = depth - 1
        elif dt_depth is None and DT_PARAM.search(code) and "(" in code \
                and not code.rstrip().endswith(";"):
            # Signature and brace on separate lines, the file's usual style.
            dt_depth = depth
        elif dt_depth is not None and depth <= dt_depth:
            dt_depth = None

    return marked


def normalise(text):
    """A key that survives reformatting and renumbering."""
    return re.sub(r"\s+", " ", text.strip())


def scan():
    hits = []

    for root in ROOTS:
        for dirpath, _, filenames in os.walk(os.path.join(ROOT, root)):
            if os.path.basename(dirpath) == "tests":
                continue

            for name in sorted(filenames):
                if not name.endswith((".cpp", ".h")):
                    continue

                path = os.path.join(dirpath, name)
                rel = os.path.relpath(path, ROOT).replace("\\", "/")

                with open(path, encoding="utf-8", errors="replace") as fh:
                    lines = fh.read().split("\n")

                for lineno, ln, inside in in_dt_function(list(pc_arm(lines))):
                    code = re.sub(r"//.*", "", ln)

                    if not code.strip() or code.strip().startswith("*"):
                        continue

                    shape = None

                    if TIMESTEP.search(code):
                        shape = "timestep"
                    elif inside and DAMPING.search(code):
                        shape = "damping"
                    elif inside and GATE.search(code):
                        shape = "gate"
                    elif inside and HISTORY.search(code):
                        shape = "history"
                    elif inside and COUNTER.search(code):
                        shape = "counter"

                    if shape:
                        hits.append((rel, lineno, shape, normalise(ln)))

    return hits


def load_baseline():
    if not os.path.exists(BASELINE):
        return {}

    with open(BASELINE, encoding="utf-8") as fh:
        return json.load(fh)


def key_of(rel, shape, text):
    return "%s\t%s\t%s" % (rel, shape, text)


def main():
    args = sys.argv[1:]
    show_all = "--all" in args
    update = "--update" in args
    shape_filter = None

    if "--shape" in args:
        shape_filter = args[args.index("--shape") + 1]

    hits = scan()

    if shape_filter:
        hits = [h for h in hits if h[2] == shape_filter]

    if update:
        counts = {}

        for rel, _, shape, text in hits:
            counts[key_of(rel, shape, text)] = counts.get(key_of(rel, shape, text), 0) + 1

        with open(BASELINE, "w", encoding="utf-8") as fh:
            json.dump(counts, fh, indent=1, sort_keys=True)
            fh.write("\n")

        print("baseline: %d sites, %d lines" % (len(counts), len(hits)))
        return 0

    baseline = load_baseline()

    # --shape narrows the scan, so it has to narrow the baseline with it or
    # every other shape reads as a site that has gone away.
    if shape_filter:
        baseline = {k: n for k, n in baseline.items()
                    if k.split("\t")[1] == shape_filter}

    seen = {}
    new = []

    for rel, lineno, shape, text in hits:
        k = key_of(rel, shape, text)
        seen[k] = seen.get(k, 0) + 1

        if seen[k] > baseline.get(k, 0):
            new.append((rel, lineno, shape, text))

    if show_all:
        by_shape = {s: 0 for s in SHAPES}

        for rel, lineno, shape, text in hits:
            by_shape[shape] += 1
            print("%-6s %s:%d: %s" % (shape, rel, lineno, text[:110]))

        print("\n%d hits: %s" % (len(hits),
                                 ", ".join("%s %d" % (s, by_shape[s]) for s in SHAPES)))

    gone = sorted(k for k, n in baseline.items() if seen.get(k, 0) < n)

    if gone:
        print("\n%d baselined site%s no longer present -- rerun with --update:"
              % (len(gone), "" if len(gone) == 1 else "s"))

        for k in gone:
            rel, shape, text = k.split("\t")
            print("  %-6s %s: %s" % (shape, rel, text[:100]))

    if new:
        print("\n%d site%s not in the baseline:" % (len(new), "" if len(new) == 1 else "s"))

        for rel, lineno, shape, text in new:
            print("  %-6s %s:%d: %s" % (shape, rel, lineno, text[:100]))

        print("\nRead the enclosing function before touching it. docs/UNCAPPED.md lists")
        print("the six shapes a false positive comes in. If it is fine as it stands,")
        print("record it with: tools/fpsdep.py --update")
        return 1

    if not show_all:
        print("fpsdep: %d known sites, none new" % len(hits))

    return 0


if __name__ == "__main__":
    sys.exit(main())
