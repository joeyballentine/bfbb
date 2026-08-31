#!/usr/bin/env python3
"""Functions the compiler patch COSTS: exact under stock, not exact under ours.

The branch's GC/2.0p1a is a net win by a wide margin, but every clause has
collateral, and a function the patch broke looks exactly like a function whose
source is wrong. Compiling the same unit twice -- once with each compiler --
separates them for free, and the answer is worth more than another source
variant: a function that is 100.000% under stock has nothing left to recover
from source.

Usage:
  patchcost.py [unit-frag ...] [--stock GC/2.0p1]
"""

import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOLO = os.path.join(ROOT, "tools", "solo.py")
FRAGS = [a for a in sys.argv[1:] if not a.startswith("-")]
STOCK = "GC/2.0p1"
if "--stock" in sys.argv:
    STOCK = sys.argv[sys.argv.index("--stock") + 1]

rep = json.load(open(os.path.join(ROOT, "build/GQPE78/report.json")))
units = [u["name"] for u in rep["units"] if u["name"].startswith("main/SB/")]
if FRAGS:
    units = [u for u in units if any(f in u for f in FRAGS)]

ROW = re.compile(r"^\s+([\d.]+)%\s+(\d+)b\s+(\S+)$")


def bad(unit, mw=None):
    """{symbol: percent} for every non-matching function in one unit."""
    cmd = [sys.executable, SOLO, unit.replace("main/", "")]
    if mw:
        cmd += ["--mw", mw]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    out = {}
    for line in r.stdout.splitlines():
        m = ROW.match(line)
        if m:
            out[m.group(3)] = (float(m.group(1)), int(m.group(2)))
    return out


def do(unit):
    ours, stock = bad(unit), bad(unit, STOCK)
    # exact under stock = absent from stock's non-matching list
    return [(unit, sym, pct, size) for sym, (pct, size) in ours.items()
            if sym not in stock]


rows = []
with ThreadPoolExecutor(6) as ex:
    for r in ex.map(do, units):
        rows.extend(r)

rows.sort(key=lambda r: -r[2])
print("%-34s %-52s %8s %7s" % ("unit", "function", "ours", "bytes"))
for unit, sym, pct, size in rows:
    print("%-34s %-52s %7.3f%% %6d" % (unit.replace("main/SB/", ""), sym[:52], pct, size))
print("\n%d function(s) exact under %s and not under the patched compiler"
      % (len(rows), STOCK))
