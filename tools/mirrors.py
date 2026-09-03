#!/usr/bin/env python3
"""Report structs defined in more than one place.

A second declaration of a struct is invisible to the compiler: nothing checks
it against the first, and the two only have to disagree about one member's
width for code that casts between them to write past the end of an object.
zTalkBox keeps its own copy of xtextbox::layout and counted with U32 where
xtextbox counts with size_t, which cost 2 KB at 64 bits and corrupted whatever
the linker had put after it.

Pairs that differ only by src/SB/Core/gc vs src/SB/Core/pc are the platform
split and are skipped -- those two are never linked together.

    python tools/mirrors.py            names with a duplicate definition
    python tools/mirrors.py --all      including the ones that carry no
                                       pointer or size_t member
"""

import argparse
import collections
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = ('.h', '.hpp', '.cpp', '.c')
SKIP_PARTS = ('dwarf', 'tests', 'third_party', 'build', 'build-debug', 'build-release')

DEF = re.compile(r'^[ \t]*(?:struct|class)\s+([A-Za-z_]\w*)\s*(?::[^;{]*)?\{', re.M)

# a member whose width moves between 32 and 64 bits
WIDE = re.compile(r'\*\s*\w+\s*(?:\[[^\]]*\])?\s*;|\bsize_t\b|\bUPtr\b|\bSPtr\b|\bptrdiff_t\b')


def body(text, start):
    """The braces of one definition, given the offset just past its `{`."""
    depth = 1
    i = start
    while i < len(text) and depth:
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
        i += 1
    return text[start:i - 1]


def platform_pair(files):
    """True when the definitions differ only by the gc/pc split."""
    stripped = {f.replace('/gc/', '/*/').replace('/pc/', '/*/') for f in files}
    return len(stripped) == 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--all', action='store_true',
                    help='report duplicates with no pointer or size_t member too')
    args = ap.parse_args()

    defs = collections.defaultdict(list)

    for path in sorted(ROOT.glob('src/SB/**/*')) + sorted(ROOT.glob('include/**/*')):
        if path.suffix not in SOURCES or not path.is_file():
            continue
        if any(part in SKIP_PARTS for part in path.parts):
            continue

        rel = path.relative_to(ROOT).as_posix()
        text = path.read_text(encoding='utf-8', errors='replace')

        for m in DEF.finditer(text):
            defs[m.group(1)].append((rel, body(text, m.end())))

    reported = 0

    for name in sorted(defs):
        places = defs[name]
        files = sorted({f for f, _ in places})

        if len(files) < 2 or platform_pair(files):
            continue

        wide = any(WIDE.search(b) for _, b in places)
        if not wide and not args.all:
            continue

        bodies = {re.sub(r'\s+', ' ', b).strip() for _, b in places}
        same = 'identical' if len(bodies) == 1 else 'DIFFERENT'

        print('%-24s %-10s %s' % (name, same, ', '.join(files)))
        reported += 1

    print('\n%d duplicated definition%s' % (reported, '' if reported == 1 else 's'))
    print('A DIFFERENT pair is only a bug if something casts one to the other.')
    print('The fix is to use the one type; a static_assert on sizeof holds a')
    print('pair that has to stay separate. Matching is on the bare name, so a')
    print('name nested in two different structs shows up here and is fine.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
