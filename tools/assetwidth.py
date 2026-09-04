#!/usr/bin/env python3
"""Report every struct whose layout changes between the 32- and 64-bit builds.

A struct that is overlaid on asset bytes has to be the size the asset file was
written for. Any struct holding a pointer grows at 64 bits, so reading an asset
through it walks the bytes at the wrong stride -- the defect that broke the LOD
table, the shrapnel records and the curve points.

The sizes come from clang, not from parsing headers: each translation unit in
build-debug-x64/compile_commands.json is re-parsed with -fdump-record-layouts,
once as configured and once with -m32, and the two sets of `sizeof=` are
compared. Structs that differ are listed, with the ones that appear in
asset-reading code marked ASSET -- those are the ones to check.

    python tools/assetwidth.py                 # asset-context structs only
    python tools/assetwidth.py --all           # every struct that changes size
    python tools/assetwidth.py --jobs 4

Exit status is 1 if a struct that changes size is read from asset memory
without a transform, so this can gate a build.
"""

import argparse
import concurrent.futures
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DB = ROOT / 'build-debug-x64' / 'compile_commands.json'

# Only game code. The RenderWare shim and librw are host-side by construction
# and never have an asset laid over them.
UNIT_DIRS = ('/src/SB/Core/x/', '/src/SB/Game/')

# What an asset read looks like. A struct named within a few lines of one of
# these is a candidate for being overlaid on asset bytes.
ASSET_CALLS = (
    'xSTFindAsset', 'xSTFindAssetByType', 'xSTGetAsset', 'xSTAssetCountByType',
    'readXForm', 'zSceneFindAsset',
)

# Runtime objects that a readXForm builds, or that librw owns. An asset never
# lies underneath one of these, whatever the call site looks like.
NOT_OVERLAID = {
    'RpAtomic', 'RwRaster', 'RwTexture', 'RpClump', 'RpWorld', 'RpGeometry',
    'xBase', 'xEnt', 'xModelInstance', 'xParSys', 'zParEmitter', 'zCutsceneMgr',
    'zEntSimpleObj', '_zPortal', 'st_ZDISPATCH_DATA',
}

# Transforms already in place: these types are rebuilt at load, so their size
# changing is the point rather than a bug. Keep in step with zAssetTypes.cpp.
TRANSFORMED = {
    'xAnimTable', 'xAnimAssetFile', 'xAnimAssetState', 'xAnimAssetEffect',
    'xLightKit', 'xLightKitLight',
    'xCutsceneInfo', 'xCutsceneTOC',
    'xCMheader', 'xCMcredits',
    'xCurveAsset',
    'zShrapnelAsset', 'zFragAsset', 'zFragProjectileAsset',
    'zFragParticleAsset', 'zFragSoundAsset', 'zFragLightningAsset',
    'xParEmitterCustomSettings',
    'zLODTable',
    'xJSPHeader', 'xJSPHeaderGC', 'xJSPNodeInfo', 'xClumpCollBSPTriangle',
    'xMorphSeq', 'xMorphTarget',
    'xVolumeAsset', 'xBound',
}

SIZEOF = re.compile(r'^\s*\|\s*\[sizeof=(\d+),', re.M)
RECORD = re.compile(r'^\*\*\* Dumping AST Record Layout\s*$')
NAME = re.compile(r'^\s*\d+\s*\|\s*(?:struct|class|union)\s+([\w:]+)\s*$', re.M)


def layouts(command, directory, extra):
    """Return {record name: sizeof} for one translation unit."""
    args = command.replace('\\', '/').split()
    out_flag = args.index('-o')
    args = args[:out_flag] + args[out_flag + 2:]
    args = [a for a in args if a != '-c']
    args += ['-fsyntax-only', '-Xclang', '-fdump-record-layouts'] + extra

    try:
        p = subprocess.run(args, cwd=directory, capture_output=True, text=True,
                           errors='replace', timeout=600)
    except (OSError, subprocess.TimeoutExpired):
        return {}

    sizes = {}
    block = []
    for line in p.stdout.splitlines():
        if RECORD.match(line):
            take(block, sizes)
            block = []
        else:
            block.append(line)
    take(block, sizes)
    return sizes


def take(block, sizes):
    if not block:
        return
    text = '\n'.join(block)
    n = NAME.search(text)
    s = SIZEOF.search(text)
    if not n or not s:
        return
    name = n.group(1).split('::')[-1]
    size = int(s.group(1))
    # A record can be dumped more than once; every dump of one type agrees.
    sizes.setdefault(name, size)


def unit_widths(entry):
    d = entry['directory']
    c = entry['command']
    return layouts(c, d, []), layouts(c, d, ['-m32'])


# A struct laid over the bytes that follow another asset struct, as in
# `(zEntDestructObjAsset*)(asset + 1)`.
OVERLAY = re.compile(r'\(\s*([A-Za-z_]\w*)\s*\*\s*\)\s*\(\s*\w+\s*\+')


def asset_context():
    """Two sets: names read from asset memory, and names placed after a header.

    The first is a struct the asset system hands out, so the file's bytes are
    underneath it and a size change is a bug. The second is any
    `(T*)(header + 1)`, which is the same shape but is usually a runtime
    allocation the same code sized -- worth an eye, not a failure.
    """
    read = set()
    placed = set()

    for path in list((ROOT / 'src' / 'SB').rglob('*.cpp')):
        try:
            text = path.read_text(encoding='utf-8', errors='replace')
        except OSError:
            continue

        for m in OVERLAY.finditer(text):
            placed.add(m.group(1))

        lines = text.splitlines()
        for i, line in enumerate(lines):
            if not any(call in line for call in ASSET_CALLS):
                continue
            window = '\n'.join(lines[max(0, i - 3):i + 4])
            for m in re.finditer(r'\(\s*(?:const\s+)?([A-Za-z_]\w*)\s*\*+\s*\)', window):
                read.add(m.group(1))
            for m in re.finditer(r'sizeof\(\s*([A-Za-z_]\w*)\s*\)', window):
                read.add(m.group(1))

    return read - NOT_OVERLAID, placed - NOT_OVERLAID - read


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--all', action='store_true',
                    help='list every struct that changes size, not just asset ones')
    ap.add_argument('--jobs', type=int, default=6,
                    help='parallel clang invocations (default 6)')
    args = ap.parse_args()

    if not DB.exists():
        print('no %s -- configure the 64-bit build first:' % DB)
        print('    build-debug.bat D3D9 x64')
        return 2

    entries = [e for e in json.loads(DB.read_text())
               if any(d in e['file'].replace('\\', '/') for d in UNIT_DIRS)]

    print('reading %d units at both widths...' % len(entries))

    wide = {}
    narrow = {}
    done = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for w, n in pool.map(unit_widths, entries):
            wide.update(w)
            narrow.update(n)
            done += 1
            if done % 25 == 0:
                print('  %d/%d' % (done, len(entries)))

    changed = sorted(name for name in narrow
                     if name in wide and wide[name] != narrow[name])

    if not changed:
        print('\nno struct changes size. That cannot be right -- check the build.')
        return 2

    in_asset, placed = asset_context()
    in_asset |= {n for n in changed if n.endswith('Asset')} - NOT_OVERLAID
    placed -= in_asset
    bad = []

    print()
    for name in changed:
        is_asset = name in in_asset
        known = name in TRANSFORMED
        if not (args.all or is_asset or name in placed):
            continue

        if is_asset and not known:
            mark = 'ASSET, NO TRANSFORM'
            bad.append(name)
        elif known:
            mark = 'asset, transformed'
        elif name in placed:
            mark = 'review: placed after a header'
        else:
            mark = 'runtime only'

        print('%-34s %4d -> %-4d  %s' % (name, narrow[name], wide[name], mark))

    print('\n%d struct%s change size; %d of them are read from asset memory.'
          % (len(changed), '' if len(changed) == 1 else 's',
             sum(1 for n in changed if n in in_asset)))

    if bad:
        print('\nNo transform for: %s' % ', '.join(bad))
        print('An asset is laid out for the 32-bit struct. Either rebuild it at')
        print('load (a readXForm in zAssetTypes.cpp) or read the disc offsets')
        print('explicitly, and add the type to TRANSFORMED here.')
        return 1

    print('\nEvery asset-overlaid struct that changes size has a transform.')
    print('The `placed after a header` rows are the same shape over a runtime')
    print('allocation; they are only a bug if the buffer came from an asset.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
