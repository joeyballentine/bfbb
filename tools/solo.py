#!/usr/bin/env python3
"""Compile ONE unit into a private temp object and diff it against the target.

The point is that this touches no shared build state. `ninja` writes into
build/GQPE78/, so two people (or two agents) working in the same checkout
clobber each other's objects and report.json. This compiles a single source
file into a throwaway directory using the exact compiler and flags that
build.ninja would use, then diffs that object against the original with
objdiff-cli. About two seconds, and any number of them can run at once.

Usage:
  solo.py <unit-fragment>                 list the unit's non-matching functions
  solo.py <unit-fragment> <symbol-frag>   side-by-side diff for one function
  solo.py <unit-fragment> --missing       list target functions absent from ours

A `|` in the left margin of a side-by-side diff marks a differing pair.

Requires a configured tree: run `configure.py` first so build.ninja exists, and
have build/tools/objdiff-cli.exe downloaded (any prior `ninja` does that).
"""
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.path.join(ROOT, "build", "tools", "objdiff-cli.exe")
CFG = json.load(open(os.path.join(ROOT, "objdiff.json")))
NINJA = open(os.path.join(ROOT, "build.ninja")).read()

# build.ninja puts the source on the same line as the `build` statement when it
# fits and on the next line when it does not, so match the whole rule body and
# search it rather than anchoring to a fixed position.
BUILD_RE = re.compile(
    r"^build (?P<obj>\S+\.o): (?P<rule>mwcc_sjis|mwcc) (?P<body>(?:.*\n)*?)  basedir",
    re.M)
SRC_RE = re.compile(r"(?:^|\s)(src[\\/]\S+\.(?:c|cp|cpp))(?:\s|$)")


def build_rules():
    out = {}
    for m in BUILD_RE.finditer(NINJA):
        body = m.group("body")
        mw = re.search(r"mw_version = (\S+)", body)
        cf = re.search(r"cflags = ((?:.*\$\n)*.*)\n", body)
        src = SRC_RE.search(body)
        if not (mw and cf and src):
            continue
        flags = re.sub(r"\s+", " ", cf.group(1).replace("$\n", " ")).strip()
        out[m.group("obj").replace("\\", "/")] = {
            "src": src.group(1).replace("\\", "/"),
            "mw": mw.group(1).replace("\\", "/"),
            "flags": flags,
        }
    return out


RULES = build_rules()


def find_unit(frag):
    hits = [u for u in CFG["units"] if frag in u["name"]]
    if len(hits) != 1:
        exact = [u for u in hits if u["name"].endswith(frag)]
        if len(exact) == 1:
            return exact[0]
        raise SystemExit("ambiguous or no match: " +
                         ", ".join(u["name"] for u in hits[:12]))
    return hits[0]


def compile_unit(unit):
    obj = unit["base_path"].replace("\\", "/")
    info = RULES.get(obj)
    if not info:
        raise SystemExit("no build rule for %s - is the source file missing?" % obj)
    td = tempfile.mkdtemp(prefix="solo_")
    out = os.path.join(td, "o.o")
    exe = os.path.join(ROOT, "build", "compilers", info["mw"], "mwcceppc.exe")
    wrap = os.path.join(ROOT, "build", "tools", "sjiswrap.exe")
    cmd = [wrap, exe] + shlex.split(info["flags"], posix=False) + \
        ["-c", info["src"], "-o", out]
    cmd = [c.strip('"') if c.startswith('"') and c.endswith('"') else c for c in cmd]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=600)
    if not os.path.exists(out):
        raise SystemExit("COMPILE FAILED:\n" + (r.stdout + r.stderr)[-3000:])
    return td, out


def diff(unit, obj, symbol=None):
    out = os.path.join(os.path.dirname(obj), "d.json")
    args = [CLI, "diff", "-1", os.path.join(ROOT, unit["target_path"]),
            "-2", obj, "-o", out, "--format", "json"]
    if symbol:
        args.append(symbol)
    subprocess.run(args, cwd=ROOT, capture_output=True)
    return json.load(open(out))


def text(entry):
    ins = entry.get("instruction")
    if not ins:
        return ""
    s = ins.get("formatted", "")
    rel = ins.get("relocation")
    if rel:
        s += "  <%s>" % rel.get("target_symbol")
    return s


def cleanup(td):
    for f in os.listdir(td):
        try:
            os.unlink(os.path.join(td, f))
        except OSError:
            pass
    try:
        os.rmdir(td)
    except OSError:
        pass


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    frag = sys.argv[1]
    sym = sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith("-") else None
    missing = "--missing" in sys.argv
    unit = find_unit(frag)
    td, obj = compile_unit(unit)
    try:
        d = diff(unit, obj, sym)
        # An object with no symbols at all - a stub unit - has no "symbols" key.
        left = {s["name"]: s for s in d.get("left", {}).get("symbols", [])
                if s.get("kind") == "SYMBOL_FUNCTION"}
        right = {s["name"]: s for s in d.get("right", {}).get("symbols", [])
                 if s.get("kind") == "SYMBOL_FUNCTION"}

        if missing:
            gone = [(int(s.get("size", 0)), n) for n, s in left.items()
                    if n not in right]
            print("%s: %d target functions not in our object"
                  % (unit["name"], len(gone)))
            for size, name in sorted(gone):
                print("  %6db  %s" % (size, name))
            return

        if not sym:
            bad = [(n, s.get("match_percent", 0.0), int(s.get("size", 0)))
                   for n, s in left.items() if s.get("match_percent", 0.0) < 100.0]
            print("%s: %d non-matching of %d" % (unit["name"], len(bad), len(left)))
            for name, pct, size in sorted(bad, key=lambda x: -x[1]):
                print("  %7.3f%%  %6db  %s" % (pct, size, name))
            return

        for name in [n for n in left if sym in n]:
            a, b = left[name], right.get(name)
            print("\n=== %s  %.3f%% ===" % (name, a.get("match_percent", 0.0)))
            ai = a.get("instructions", [])
            bi = (b or {}).get("instructions", [])
            for k in range(max(len(ai), len(bi))):
                ea = ai[k] if k < len(ai) else {}
                eb = bi[k] if k < len(bi) else {}
                kind = ea.get("diff_kind") or eb.get("diff_kind") or ""
                mark = " " if not kind or kind == "DIFF_NONE" else "|"
                print(" %s %-46s %s" % (mark, text(ea), text(eb)))
    finally:
        cleanup(td)


main()
