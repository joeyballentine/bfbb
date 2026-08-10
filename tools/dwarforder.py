"""Find functions whose local declaration ORDER differs from dwarf/.

Unlike renaming, reordering local declarations changes generated code: under
both MW compilers declaration order drives stack slot assignment. Reordering
three locals to the dwarf order took zNPCTypeRobot's NPCMessage from 99.016%
to 100%.

So this is a worklist, not a patch. It ranks candidates by how close the
function already is, because a function at 99.x% whose only problem is stack
layout is the one most likely to flip; a function at 40% has bigger problems
and reordering it proves nothing.

Only the locals present in BOTH is compared, and only their relative order, so
extra or missing locals on either side do not create noise.

Usage:
  dwarforder.py [<path.cpp> ...]      default: every changed file with a peer
  dwarforder.py --min 95 [...]        only functions already at >= 95%
"""
import json
import os
import re
import subprocess
import sys

MIN = 0.0
if "--min" in sys.argv:
    i = sys.argv.index("--min")
    MIN = float(sys.argv[i + 1])
    del sys.argv[i:i + 2]
args = sys.argv[1:]

IDENT = r"[A-Za-z_~][A-Za-z_0-9]*"
DW_FUNC = re.compile(r"^(?P<sig>[A-Za-z_].*?)\((?P<params>.*)\)\s*(?:const\s*)?\{\s*$", re.M)
DW_LOCAL = re.compile(r"^\s+(?:class |struct |enum |union |signed |unsigned |static )*"
                      r"[A-Za-z_][A-Za-z_0-9:<>, ]*[ *&]+(?P<name>" + IDENT + r")"
                      r"(?:\[[^\]]*\])?;\s*//", re.M)
# our local: a declaration statement inside a body
OUR_LOCAL = re.compile(r"^\s+(?:const\s+|static\s+|volatile\s+|struct\s+|class\s+|unsigned\s+|signed\s+)*"
                       r"[A-Za-z_][A-Za-z_0-9:<>]*\s*[*&]?\s+(?P<name>" + IDENT + r")"
                       r"\s*(?:\[[^\]]*\])?\s*(?:=[^;]*)?;", re.M)

KEYWORDS = {"return", "if", "for", "while", "switch", "else", "do", "case", "break",
            "continue", "goto", "sizeof", "new", "delete", "typedef", "using"}


def dwarf_order(path):
    txt = open(path, encoding="utf-8", errors="replace").read()
    out = {}
    for m in DW_FUNC.finditer(txt):
        nm = m.group("sig").split("(")[0].strip().split()[-1].lstrip("*&")
        if nm in KEYWORDS:
            continue
        start = m.end()
        end = txt.find("\n}", start)
        body = txt[start:end if end > 0 else len(txt)]
        seq, seen = [], set()
        for x in DW_LOCAL.finditer(body):
            n = x.group("name")
            if n not in seen:
                seen.add(n)
                seq.append(n)
        if nm not in out or len(seq) > len(out[nm]):
            out[nm] = seq
    return out


def our_order(path):
    txt = open(path, encoding="utf-8", errors="replace").read()
    txt = re.sub(r"//[^\n]*", "", txt)
    txt = re.sub(r"/\*.*?\*/", "", txt, flags=re.S)
    out = {}
    pat = re.compile(r"(?:^|\n)[^\n;{}]*?(?:(" + IDENT + r")::)?(" + IDENT + r")\s*"
                     r"\(([^;{}()]*(?:\([^()]*\)[^;{}()]*)*)\)\s*(?:const\s*)?\{")
    for m in pat.finditer(txt):
        nm = m.group(2)
        if nm in KEYWORDS:
            continue
        i = m.end() - 1
        depth = 0
        for j in range(i, len(txt)):
            if txt[j] == "{":
                depth += 1
            elif txt[j] == "}":
                depth -= 1
                if depth == 0:
                    break
        body = txt[i:j]
        seq, seen = [], set()
        for x in OUR_LOCAL.finditer(body):
            n = x.group("name")
            if n in KEYWORDS or n in seen:
                continue
            seen.add(n)
            seq.append(n)
        out.setdefault(nm, (seq, body))
    return out


pct = {}
rep = "build/GQPE78/report.json"
if os.path.exists(rep):
    for u in json.load(open(rep))["units"]:
        for fn in u.get("functions") or []:
            base = fn["name"].split("__")[0]
            v = fn.get("fuzzy_match_percent", 0.0)
            if base not in pct or v > pct[base]:
                pct[base] = v

if args:
    files = args
else:
    files = subprocess.run(["git", "diff", "--name-only", "bfbbdecomp/main...HEAD",
                            "--", "src/*.cpp"], capture_output=True, text=True).stdout.split()

rows = []
for f in files:
    dw = "dwarf/" + f[len("src/"):]
    if not (os.path.exists(dw) and os.path.exists(f)):
        continue
    D, O = dwarf_order(dw), our_order(f)
    for nm, dseq in D.items():
        if nm not in O:
            continue
        oseq, _ = O[nm]
        common = [n for n in dseq if n in oseq]
        ours = [n for n in oseq if n in common]
        if len(common) < 2 or common == ours:
            continue
        p = pct.get(nm)
        if p is None or p < MIN or p >= 100.0:
            continue
        rows.append((p, f[len("src/"):], nm, common, ours))

rows.sort(reverse=True)
print("%-7s %-34s %s" % ("fuzzy", "file", "function"))
for p, f, nm, d, o in rows:
    print("%6.2f%% %-34s %s" % (p, f, nm))
    print("        dwarf order: %s" % ", ".join(d))
    print("        our order  : %s" % ", ".join(o))
print("\n%d candidate functions (>= %.0f%%, order differs, >=2 shared locals)" % (len(rows), MIN))
