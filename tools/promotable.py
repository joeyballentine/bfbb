#!/usr/bin/env python3
"""Units whose functions and data all match but which configure.py still links
from the extracted object.

A unit only reaches `complete_units`, and only links from OUR object, when
configure.py marks it Matching. objdiff reaching 100% is necessary and not
sufficient -- it pairs symbols by name and is blind to definition order -- so a
promotion is a hypothesis that the DOL sha1 has to confirm.

Usage: promotable.py
"""

import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
rep = json.load(open(os.path.join(ROOT, "build/GQPE78/report.json")))
cfg = open(os.path.join(ROOT, "configure.py")).read()

out = []
for u in rep["units"]:
    n = u["name"]
    if not n.startswith("main/SB/"):
        continue
    m = u["measures"]
    if m.get("matched_functions") != m.get("total_functions"):
        continue
    if int(m.get("matched_data", 0)) != int(m.get("total_data", 0)):
        continue
    st = "?"
    for ext in (".cpp", ".c"):
        path = n.replace("main/", "") + ext
        mm = re.search(r'Object\((\w+), "%s"[,)]' % re.escape(path), cfg)
        if mm:
            st = mm.group(1)
            break
    if st != "Matching":
        out.append((st, path, m.get("total_functions", 0)))

for st, path, nf in sorted(out):
    print("%-14s %-44s %d functions" % (st, path, nf))
print("%d unit(s) fully matching but not marked Matching" % len(out))
