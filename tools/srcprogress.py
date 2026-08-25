#!/usr/bin/env python3
"""How much of the source is DONE, as opposed to how much of it matches.

report.json's "exact" counts a function only when our object is byte-identical
to retail's. That conflates two very different states:

  - our C says something different from retail's C            (work remains)
  - our C is right and only the scheduler or register allocator
    puts the instructions somewhere else                      (nothing to write)

The second is real progress the exact metric refuses to show, and it is a large
share of what is left. This tool separates them and reports a second number:

  source-complete = exact + codegen-only

A function counts as codegen-only when semdiff's multiset test finds no
difference: registers, branch destinations and anonymous pool ordinals erased,
every literal and memory offset kept. Reordering cannot change a multiset and
renaming cannot change a normalised one, so a clean multiset means the two
objects execute the same operations on the same values.

WHAT THIS NUMBER IS NOT
-----------------------
It is an upper bound on completeness, not a proof. The multiset test has one
known hole: two different float constants both normalise to `lfs R, @P@sda21`,
so a wrong .sdata2 value cancels out. Units where `datadiff.py --consts`
reports a value difference are therefore marked (!) and their codegen-only
verdicts are weaker than the rest. The iCollide 0.7010677/0.70710677 bug lived
in exactly that hole.

Treat source-complete as "nothing more to write here that we can see", and the
DOL sha1 as the only proof of anything.

Usage:
  srcprogress.py                 whole-project summary plus per-unit table
  srcprogress.py <unit-frag> ... restrict to matching units
  srcprogress.py --list          list every function still needing source work
  srcprogress.py --json          machine-readable, for tracking over time
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import semdiff

ROOT = semdiff.ROOT
GAME = "main/SB/"


def consts_suspect():
    """Units whose .sdata2 holds a value we do not have, or vice versa.

    A codegen-only verdict inside one of these is less trustworthy, because the
    multiset test cannot see a wrong float.
    """
    import datadiff
    import struct
    bad = set()
    for u in datadiff.CFG["units"]:
        if not u["name"].startswith(GAME):
            continue
        t = os.path.join(ROOT, u["target_path"])
        o = os.path.join(ROOT, u["base_path"])
        if not (os.path.exists(t) and os.path.exists(o)):
            continue
        for sec in (".sdata2", ".sbss2"):
            T, O = datadiff.section(t, sec), datadiff.section(o, sec)
            if T == O:
                continue
            def words(b):
                from collections import Counter
                return Counter(struct.unpack(">I", b[i:i + 4])[0]
                               for i in range(0, len(b) // 4 * 4, 4))
            A, B = words(T), words(O)
            miss, extra = A - B, B - A
            miss.pop(0, None)
            extra.pop(0, None)
            if miss or extra:
                bad.add(u["name"])
    return bad


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    want_list = "--list" in sys.argv
    want_json = "--json" in sys.argv

    report = json.load(open(os.path.join(ROOT, "build/GQPE78/report.json")))
    sizes, pcts = {}, {}
    for u in report["units"]:
        if not u["name"].startswith(GAME):
            continue
        for f in u.get("functions", []):
            sizes[(u["name"], f["name"])] = int(f.get("size") or 0)
            pcts[(u["name"], f["name"])] = f.get("fuzzy_match_percent")

    suspect = consts_suspect()

    units = [u for u in semdiff.CFG["units"] if u["name"].startswith(GAME)
             and (not args or any(a.lower() in u["name"].lower() for a in args))]

    semantic = set()
    for u in units:
        for hit in semdiff.scan(u):
            semantic.add((hit["unit"], hit["name"]))

    rows = {}
    todo = []
    for (un, fn), sz in sizes.items():
        if args and not any(a.lower() in un.lower() for a in args):
            continue
        r = rows.setdefault(un, dict(exact_n=0, exact_b=0, cg_n=0, cg_b=0,
                                     sem_n=0, sem_b=0))
        p = pcts[(un, fn)]
        if p == 100.0:
            r["exact_n"] += 1
            r["exact_b"] += sz
        elif (un, fn) in semantic:
            r["sem_n"] += 1
            r["sem_b"] += sz
            todo.append((sz, p, un, fn))
        else:
            r["cg_n"] += 1
            r["cg_b"] += sz

    tot = dict(exact_n=0, exact_b=0, cg_n=0, cg_b=0, sem_n=0, sem_b=0)
    for r in rows.values():
        for k in tot:
            tot[k] += r[k]
    allb = tot["exact_b"] + tot["cg_b"] + tot["sem_b"]
    alln = tot["exact_n"] + tot["cg_n"] + tot["sem_n"]

    if want_json:
        print(json.dumps(dict(total_bytes=allb, total_functions=alln,
                              **tot,
                              suspect_units=sorted(suspect)), indent=1))
        return 0

    if want_list:
        print("%d function(s) still needing source work, worst first\n" % len(todo))
        for sz, p, un, fn in sorted(todo, reverse=True):
            mark = " (!)" if un in suspect else ""
            print("  %7d  %6.2f%%  %-26s %s%s"
                  % (sz, p or 0.0, un.replace(GAME, "")[:26], fn[:52], mark))
        return 0

    def pc(x):
        return 100.0 * x / allb if allb else 0.0

    print("Game code: %d functions, %d bytes\n" % (alln, allb))
    print("  exact            %5d fns  %8d bytes  %6.2f%%"
          % (tot["exact_n"], tot["exact_b"], pc(tot["exact_b"])))
    print("  codegen-only     %5d fns  %8d bytes  %6.2f%%   source right, "
          "compiler places it differently" % (tot["cg_n"], tot["cg_b"], pc(tot["cg_b"])))
    print("  " + "-" * 68)
    print("  SOURCE-COMPLETE  %5d fns  %8d bytes  %6.2f%%"
          % (tot["exact_n"] + tot["cg_n"], tot["exact_b"] + tot["cg_b"],
             pc(tot["exact_b"] + tot["cg_b"])))
    print()
    print("  needs source     %5d fns  %8d bytes  %6.2f%%"
          % (tot["sem_n"], tot["sem_b"], pc(tot["sem_b"])))
    if suspect:
        print("\n  %d unit(s) carry a .sdata2 value difference, so codegen-only "
              "verdicts\n  inside them are weaker -- marked (!) below and in --list."
              % len(suspect))

    print("\nUnits with source work left, by bytes:\n")
    print("  %8s %5s  %8s  %-34s" % ("bytes", "fns", "src-done", "unit"))
    for un, r in sorted(rows.items(), key=lambda x: -x[1]["sem_b"]):
        if not r["sem_b"]:
            continue
        ub = r["exact_b"] + r["cg_b"] + r["sem_b"]
        done = 100.0 * (r["exact_b"] + r["cg_b"]) / ub if ub else 0.0
        mark = " (!)" if un in suspect else ""
        print("  %8d %5d  %7.2f%%  %-34s%s"
              % (r["sem_b"], r["sem_n"], done, un.replace(GAME, "")[:34], mark))
    done_units = [u for u, r in rows.items() if not r["sem_b"]]
    print("\n  %d of %d units are source-complete." % (len(done_units), len(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
