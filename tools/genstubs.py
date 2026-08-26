#!/usr/bin/env python3
"""Generate stub implementations for the unported PC platform modules.

Run once, review the output, then edit the files by hand. This is scaffolding
for getting a LINK, not a code generator anyone should keep running: the point
of the stubs is to find out which module the startup path demands first, and
each one gets replaced by a real implementation as that happens.

Driven by two sources that have to agree:

  * the linker's undefined-symbol list (tools/pclink.py --list), which is
    authoritative about what is actually MISSING -- headers declare plenty that
    is already defined elsewhere, and gc/*.cpp has file-static helpers that must
    not be stubbed at all.
  * the PC headers in src/SB/Core/pc, which are authoritative about the
    SIGNATURES. Writing those out by hand from demangled linker output is how
    you get a stub that compiles and then silently fails to resolve.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PC = ROOT / "src/SB/Core/pc"

MODULES = ["iModel", "iParMgr", "iScrFX", "iLight", "iAnim", "iEnv",
           "iMorph", "iDraw", "iFX", "iFMV"]

DECL = re.compile(r'^\s*([A-Za-z_][\w\s\*&:<>]*?[\s\*&])([A-Za-z_]\w*)\s*\(([^;{]*)\)\s*;\s*$')
# The type capture has to swallow the '*' -- an earlier version put it outside
# the group and generated `RpWorld gLightWorld;` for `extern RpWorld*
# gLightWorld;`, which compiles and is a different symbol.
VAR = re.compile(r'^\s*extern\s+(.+?[\s\*])([A-Za-z_]\w*)\s*;\s*$')


def default_return(ret):
    r = ret.replace('const', '').strip()
    if r == 'void':
        return None
    if '*' in r or '&' in r:
        return 'NULL'
    if r == 'bool':
        return 'false'
    if r in ('F32', 'float', 'double'):
        return '0.0f'
    return '0'


def main():
    undef_path = pathlib.Path(sys.argv[1])
    undef = undef_path.read_text(encoding='utf-8')
    wanted = set(re.findall(r'\b(\w+)\s*\(', undef))
    # data symbols: the tail identifier of any line with no call parens
    for line in undef.splitlines():
        if '(' not in line:
            m = re.search(r'([A-Za-z_]\w*)\s*$', line.strip())
            if m:
                wanted.add(m.group(1))

    for mod in MODULES:
        header = PC / (mod + ".h")
        if not header.exists():
            print(f"-- {mod}: no PC header, skipped", file=sys.stderr)
            continue

        funcs, vars_ = [], []
        seen = set()
        for line in header.read_text(encoding='utf-8').splitlines():
            stripped = line.strip()
            if stripped.startswith('//') or stripped.startswith('#'):
                continue

            mv = VAR.match(line)
            if mv and mv.group(2) in wanted:
                vars_.append((mv.group(1).strip(), mv.group(2)))
                continue

            mo = DECL.match(line)
            if not mo:
                continue
            ret, name, args = mo.group(1).strip(), mo.group(2), mo.group(3).strip()
            if ret.startswith('static') or ret.startswith('typedef'):
                continue
            if name not in wanted:
                continue
            key = (name, args)
            if key in seen:
                continue
            seen.add(key)
            funcs.append((ret, name, args))

        if not funcs and not vars_:
            continue

        out = PC / (mod + ".cpp")
        if out.exists():
            print(f"-- {mod}.cpp already exists, not overwriting", file=sys.stderr)
            continue

        body = []
        body.append(f'#include "{mod}.h"')
        body.append('')
        body.append('#include "iStub.h"')
        body.append('')
        body.append(f'// {mod}: not ported yet.')
        body.append('//')
        body.append('// Stubs, so that the port LINKS and can be run. Every one of these reports')
        body.append('// itself the first time it is called (see iStub.h), and that report is the')
        body.append('// worklist: the order the startup path demands these in is what decides')
        body.append('// which module gets written first.')
        body.append('//')
        body.append(f'// The real implementation is src/SB/Core/gc/{mod}.cpp. Read it before')
        body.append('// replacing anything here -- most of these modules are RenderWare calls and')
        body.append('// game logic rather than GameCube hardware, so a port is closer to a copy')
        body.append('// than it looks.')
        body.append('')

        for vtype, vname in vars_:
            body.append(f'{vtype} {vname};')
        if vars_:
            body.append('')

        for ret, name, args in funcs:
            body.append(f'{ret} {name}({args})')
            body.append('{')
            body.append('    IPORT_STUB();')
            dr = default_return(ret)
            if dr is not None:
                body.append(f'    return {dr};')
            body.append('}')
            body.append('')

        out.write_text(chr(10).join(body), encoding='utf-8', newline=chr(10))
        print(f"{mod}.cpp: {len(funcs)} functions, {len(vars_)} data symbols")


if __name__ == "__main__":
    main()
