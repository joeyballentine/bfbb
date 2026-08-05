#!/usr/bin/env python3
"""Compare the ORDER of symbols within each section against the target.

Two objects can score 100% on every symbol and still link to different bytes,
because objdiff matches symbols by name while the linker lays them out in
definition order. abort_exit.o was exactly that: `exit` and `abort` both
100%, both sections the right size, but defined in the opposite order, so
flipping it to Matching moved 396 bytes of .text around. Swapping the two
definitions in the .c fixed it.

Usage: symorder.py <unit-fragment> ...
"""
import json
import os
import subprocess
import sys

CLI = os.path.abspath(os.path.join("build", "tools", "objdiff-cli.exe"))
CFG = json.load(open("objdiff.json"))
TMP = os.path.join(os.environ.get("TEMP", "."), "symorder.json")


def sides(unit):
    subprocess.run([CLI, "diff",
                    "-1", unit["target_path"].replace("/", os.sep),
                    "-2", unit["base_path"].replace("/", os.sep),
                    "-o", TMP, "--format", "json"], capture_output=True)
    return json.load(open(TMP))


def by_section(side):
    out, cur = {}, None
    for s in side.get("symbols", []):
        n = s["name"]
        if n.startswith("[") and n.endswith("]"):
            cur = n[1:-1]
            out.setdefault(cur, [])
        elif cur is not None and s.get("size"):
            out[cur].append(n)
    return out


for frag in sys.argv[1:]:
    hits = [u for u in CFG["units"] if frag in u["name"]]
    if not hits:
        print("no unit matching", frag)
        continue
    u = hits[0]
    d = sides(u)
    L, R = by_section(d["left"]), by_section(d["right"])
    print("==", u["name"])
    for sec in sorted(set(L) | set(R)):
        a, b = L.get(sec, []), R.get(sec, [])
        if a == b:
            continue
        if sorted(a) == sorted(b):
            print("   %s: SAME SET, WRONG ORDER" % sec)
        else:
            print("   %s: different sets" % sec)
        print("      target: %s" % ", ".join(a))
        print("      ours  : %s" % ", ".join(b))
