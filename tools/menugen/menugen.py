"""Turn a menu package's logic assets into C++ builder code.

    python tools/menugen/menugen.py --hip <xbox>/mn/mnu3.HIP --names <gc>/files \
        --out src/SB/Core/pc/menu --prefix mnu3

Reads the Xbox HIP (what the port loads) and names every asset ID it can from
the GameCube disc's packer listings (*.LIP / *.LOP), which keep the names the
Xbox build stripped. Writes one .cpp per asset group plus a build function that
src/SB/Core/pc/iAssetOverride.cpp calls in place of the HIP's own assets.

This is a bootstrap. Once the files are generated they are the source: edit
them, do not regenerate over them.

Before writing anything the generator serializes its own model back to bytes
and compares against the HIP, so a field spec that is wrong fails here rather
than in the game. BFBB_MENU_VERIFY=1 does the same check on the C++ at runtime.
"""

import argparse
import collections
import decimal
import os
import re
import struct
import sys
import textwrap

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'hipkit'))
import hipfile  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))


def xstrhash(s):
    # xString.cpp's xStrHash: lowercase letters fold to upper. Asset names
    # are ASCII, so the sign of char does not come into it.
    h = 0
    for c in s.encode('ascii'):
        h = (((c - (c & (c >> 1) & 0x20)) & 0xFF) + h * 0x83) & 0xFFFFFFFF
    return h


# --- names ------------------------------------------------------------------

LIP_LINE = re.compile(r'^\t0x([0-9A-Fa-f]{8})  \((....)\)  (.*?)\s*\(\s*\d+\)\s*$')


def load_names(root):
    names = {}
    for dirpath, _, files in os.walk(root):
        for f in files:
            if not f.upper().endswith(('.LIP', '.LOP')):
                continue
            with open(os.path.join(dirpath, f), 'r', encoding='latin-1') as fh:
                for line in fh:
                    m = LIP_LINE.match(line.rstrip('\r\n'))
                    if not m:
                        continue
                    aid = int(m.group(1), 16)
                    name = m.group(3)
                    if name.isascii() and xstrhash(name) == aid:
                        names.setdefault(aid, name)
    return names


def load_source_strings(names):
    """Names the listings miss: zVar variable names, Xbox-only assets the game
    code spells out. Any string literal in the source that hashes to an ID."""
    lit = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
    found = {}
    for sub in ('src/SB/Game', 'src/SB/Core/x', 'src/SB/Core/pc'):
        for dirpath, _, files in os.walk(os.path.join(ROOT, sub)):
            for f in files:
                if not f.endswith(('.cpp', '.h')):
                    continue
                txt = open(os.path.join(dirpath, f), 'r', encoding='latin-1').read()
                for m in lit.finditer(txt):
                    s = m.group(1)
                    if s and s.isascii() and '\\' not in s:
                        found.setdefault(xstrhash(s), s)
    return found


def load_package_strings(paths, wanted):
    """Xbox-only assets are not in the GameCube listings, but a few of their
    names survive as strings inside the Xbox packages (condition and variable
    names, mostly). Every printable run and each of its suffixes is tried."""
    found = {}
    for path in paths:
        data = open(path, 'rb').read()
        for m in re.finditer(rb'[ -~]{4,}', data):
            s = m.group(0).decode('ascii')
            for i in range(len(s) - 3):
                h = xstrhash(s[i:])
                if h in wanted:
                    found.setdefault(h, s[i:])
    return found


def load_enum(path, enum_name):
    src = open(path, 'r', encoding='latin-1').read()
    m = re.search(r'enum\s+' + enum_name + r'\s*\{(.*?)\};', src, re.S)
    out = {}
    v = 0
    for item in m.group(1).split(','):
        item = re.sub(r'//.*', '', item).strip()
        if not item:
            continue
        if '=' in item:
            k, e = [x.strip() for x in item.split('=')]
            v = int(e, 0)
        else:
            k = item
        out.setdefault(v, k)
        v += 1
    return out


# --- field specs --------------------------------------------------------------
#
# (name, struct code, count, kind). kind picks how a value is written:
#   id    an asset ID, written H("name") when the name is known
#   hex   flags
#   int   a plain number
#   float
#   btype en_ZBASETYPE
#   pad   compiler padding, never written unless nonzero

BASE = [('id', 'I', 1, 'id'), ('baseType', 'B', 1, 'btype'), ('linkCount', 'B', 1, 'int'),
        ('baseFlags', 'H', 1, 'hex')]
ENT = BASE + [
    ('flags', 'B', 1, 'hex'), ('subtype', 'B', 1, 'int'), ('pflags', 'B', 1, 'hex'),
    ('moreFlags', 'B', 1, 'hex'), ('pad', 'B', 1, 'int'), ('_pad0', 'B', 3, 'pad'),
    ('surfaceID', 'I', 1, 'id'), ('ang', 'f', 3, 'vec'), ('pos', 'f', 3, 'vec'),
    ('scale', 'f', 3, 'vec'), ('redMult', 'f', 1, 'float'), ('greenMult', 'f', 1, 'float'),
    ('blueMult', 'f', 1, 'float'), ('seeThru', 'f', 1, 'float'),
    ('seeThruSpeed', 'f', 1, 'float'), ('modelInfoID', 'I', 1, 'id'),
    ('animListID', 'I', 1, 'id'),
]
UI = ENT + [
    ('uiFlags', 'I', 1, 'hex'), ('dim', 'H', 2, 'int'), ('textureID', 'I', 1, 'id'),
    ('uva', 'f', 2, 'float'), ('uvb', 'f', 2, 'float'), ('uvc', 'f', 2, 'float'),
    ('uvd', 'f', 2, 'float'),
]
UIFT = UI + [
    ('uiFontFlags', 'H', 1, 'hex'), ('mode', 'B', 1, 'int'), ('fontID', 'B', 1, 'int'),
    ('textAssetID', 'I', 1, 'id'), ('bcolor', 'B', 4, 'int'), ('color', 'B', 4, 'int'),
    ('inset', 'H', 4, 'int'), ('space', 'H', 2, 'int'), ('cdim', 'H', 2, 'int'),
    ('max_height', 'I', 1, 'int'),
]
GRUP = BASE + [('itemCount', 'H', 1, 'int'), ('groupFlags', 'H', 1, 'hex')]
COND = BASE + [('constNum', 'I', 1, 'int'), ('expr1', 'I', 1, 'id'), ('op', 'I', 1, 'int'),
               ('value_asset', 'I', 1, 'id')]
TIMR = BASE + [('seconds', 'f', 1, 'float'), ('randomRange', 'f', 1, 'float')]
SFX = BASE + [
    ('flagsSFX', 'H', 1, 'hex'), ('freq', 'H', 1, 'int'), ('freqm', 'f', 1, 'float'),
    ('soundAssetID', 'I', 1, 'id'), ('attachID', 'I', 1, 'id'), ('loopCount', 'B', 1, 'int'),
    ('priority', 'B', 1, 'int'), ('volume', 'B', 1, 'int'), ('pad', 'B', 1, 'int'),
    ('pos', 'f', 3, 'vec'), ('innerRadius', 'f', 1, 'float'), ('outerRadius', 'f', 1, 'float'),
]
DPAT = BASE
CNTR = BASE + [('count', 'h', 1, 'int'), ('_pad0', 'B', 2, 'pad')]
# sceneID holds the scene's four characters in reading order in memory.
PORT = BASE + [('assetCameraID', 'I', 1, 'id'), ('assetMarkerID', 'I', 1, 'id'),
               ('ang', 'f', 1, 'float'), ('sceneID', 'I', 1, 'tag')]


def floats(prefix, names):
    return [(prefix + n, 'f', 1, 'float') for n in names]


def rgba(prefix):
    return [(prefix + c, 'B', 1, 'int') for c in 'rgba']


# DYNA assets: an xDynAsset header, then a payload chosen by the header's type,
# which is xStrHash of a type name. zScene.cpp's table maps names to classes.
DYNA_BASE = BASE + [('type', 'I', 1, 'id'), ('version', 'H', 1, 'int'),
                    ('handle', 'H', 1, 'int')]
HUD = DYNA_BASE + [('loc', 'f', 3, 'vec'), ('size', 'f', 3, 'vec')]
METER = HUD + floats('', ['start_value', 'min_value', 'max_value', 'increment_time',
                          'decrement_time']) + [
    ('sound.' + n, 'I', 1, 'id') for n in ['start_increment', 'increment', 'start_decrement',
                                           'decrement']]
TEXTBOX_XJ = ['ztextbox::asset_type::XJ_LEFT', 'ztextbox::asset_type::XJ_CENTER',
              'ztextbox::asset_type::XJ_RIGHT']
TEXTBOX_EX = ['ztextbox::asset_type::EX_UP', 'ztextbox::asset_type::EX_CENTER',
              'ztextbox::asset_type::EX_DOWN']
DYNA_TYPES = {
    'hud:model': ('xhud::model_asset', 'HudModel', HUD + [('model', 'I', 1, 'id')]),
    'hud:text': ('xhud::text_asset', 'HudText',
                 HUD + [('text_box', 'I', 1, 'id'), ('text', 'I', 1, 'id')]),
    'hud:meter:font': ('xhud::font_meter_asset', 'HudFontMeter', METER + [
        ('font.id', 'I', 1, 'id'), ('font.justify', 'i', 1, 'int')] +
        floats('font.', ['w', 'h', 'space', 'drop_x', 'drop_y']) + rgba('font.c.') +
        rgba('font.drop_c.') + [('counter_mode', 'B', 1, 'int'), ('pad1', 'B', 1, 'int'),
                                ('pad2', 'B', 1, 'int'), ('pad3', 'B', 1, 'int')]),
    'hud:meter:unit': ('xhud::unit_meter_asset', 'HudUnitMeter', METER + [
        ('model[0].id', 'I', 1, 'id'), ('model[0].loc', 'f', 3, 'vec'),
        ('model[0].size', 'f', 3, 'vec'), ('model[1].id', 'I', 1, 'id'),
        ('model[1].loc', 'f', 3, 'vec'), ('model[1].size', 'f', 3, 'vec'),
        ('offset', 'f', 3, 'vec'), ('fill_forward', 'I', 1, 'int')]),
    'game_object:text_box': ('ztextbox::asset_type', 'TextBox', DYNA_BASE + [
        ('text', 'I', 1, 'id')] + floats('bounds.', 'xywh') + [('font', 'I', 1, 'int')] +
        floats('size.', ['width', 'height']) + floats('space.', 'xy') + rgba('color.') +
        floats('inset.', ['left', 'top', 'right', 'bottom']) + [
        ('xjustify', 'I', 1, TEXTBOX_XJ), ('expand', 'I', 1, TEXTBOX_EX),
        ('max_height', 'f', 1, 'float'), ('backdrop.type', 'I', 1, 'int')] +
        rgba('backdrop.color.') + [('backdrop.texture', 'I', 1, 'id')]),
    # The flags are eight-bit bool bitfields; trigger_pads is a U8 on PC and
    # holds 0, 1 or 2 (see zTalkBox.h).
    'game_object:talk_box': ('ztalkbox::asset_type', 'TalkBox', DYNA_BASE + [
        ('dialog_box', 'I', 1, 'id'), ('prompt_box', 'I', 1, 'id'), ('quit_box', 'I', 1, 'id')] +
        [(n, 'B', 1, 'int') for n in ['trap', 'pause', 'allow_quit', 'trigger_pads', 'page',
                                      'show', 'hide', 'audio_effect']] +
        [('teleport', 'I', 1, 'id')] +
        [('auto_wait.type.' + n, 'B', 1, 'int') for n in ['time', 'prompt', 'sound', 'event']] +
        [('auto_wait.delay', 'f', 1, 'float'), ('auto_wait.which_event', 'i', 1, 'int')] +
        [('prompt.' + n, 'I', 1, 'id') for n in ['skip', 'noskip', 'quit', 'noquit', 'yesno']]),
}
DYNA_BY_HASH = {}  # filled in main, once xstrhash is defined

# type tag -> (C++ struct, spec, file group, function suffix). DYNA's struct
# and spec depend on the asset; see DYNA_TYPES.
TYPES = collections.OrderedDict([
    ('TEXT', (None, None, 'text', 'Text')),
    ('UI  ', ('zUIAsset', UI, 'ui', 'UI')),
    ('UIFT', ('zUIFontAsset', UIFT, 'uifont', 'UIFont')),
    ('DYNA', (None, None, 'dyna', 'Dyna')),
    ('GRUP', ('xGroupAsset', GRUP, 'logic', 'Group')),
    ('COND', ('zCondAsset', COND, 'logic', 'Cond')),
    ('TIMR', ('xTimerAsset', TIMR, 'logic', 'Timer')),
    ('DPAT', ('xBaseAsset', DPAT, 'logic', 'Dispatcher')),
    ('SFX ', ('xSFXAsset', SFX, 'logic', 'SFX')),
    ('CNTR', ('xCounterAsset', CNTR, 'logic', 'Counter')),
    ('PORT', ('xPortalAsset', PORT, 'logic', 'Portal')),
])


def dyna_spec(data):
    """(C++ struct, suffix, spec) for a DYNA asset's payload."""
    h = struct.unpack_from('<I', data, 8)[0]
    if h not in DYNA_BY_HASH:
        raise ValueError('DYNA type %08X has no spec; add it to DYNA_TYPES' % h)
    return DYNA_BY_HASH[h]


def spec_fmt(spec):
    return '<' + ''.join('%d%s' % (n, c) for _, c, n, _ in spec)


def unpack(spec, data):
    fmt = spec_fmt(spec)
    size = struct.calcsize(fmt)
    vals = struct.unpack_from(fmt, data, 0)
    out = collections.OrderedDict()
    i = 0
    for name, _, n, _ in spec:
        out[name] = tuple(vals[i:i + n]) if n > 1 else vals[i]
        i += n
    return out, size


def pack(spec, fields):
    flat = []
    for name, _, n, _ in spec:
        v = fields[name]
        flat.extend(v if n > 1 else [v])
    return struct.pack(spec_fmt(spec), *flat)


LINK = struct.Struct('<HHI4fII')


# --- formatting ---------------------------------------------------------------

def fbits(f):
    return struct.unpack('<I', struct.pack('<f', f))[0]


def ffmt(f):
    b = fbits(f)
    if b == 0:
        return '0.0f'
    if b == 0x80000000:
        return '-0.0f'
    e = (b >> 23) & 0xFF
    if e == 0 or e == 0xFF:
        return None  # denormal, inf, nan: not a float anyone typed
    for p in range(1, 10):
        s = '%.*g' % (p, f)
        if fbits(float(s)) == b:
            # Positional, not scientific: 130.0f, not 1.3e+02f.
            s = format(decimal.Decimal(s), 'f')
            if '.' not in s:
                s += '.0'
            return s + 'f'
    raise AssertionError(f)


class Emitter:
    def __init__(self, names, events, btypes):
        self.names = names
        self.events = events
        self.btypes = btypes
        self.unnamed = set()
        self.texts = {}

    def id(self, v):
        if v == 0:
            return '0'
        n = self.names.get(v)
        if n is not None:
            return 'H(%s)' % cstr(n)
        self.unnamed.add(v)
        return '0x%08X' % v

    def value(self, kind, v):
        if kind == 'id':
            return self.id(v)
        if kind == 'hex':
            return '0x%X' % v if v else '0'
        if kind == 'btype':
            return self.btypes.get(v, str(v))
        if kind in ('float', 'vec'):
            s = ffmt(v)
            return s if s is not None else 'iAssetFloatBits(0x%08X)' % fbits(v)
        if kind == 'tag':
            b = struct.pack('<I', v)
            if all(0x20 <= c < 0x7F for c in b):
                return 'iAssetTag(%s)' % cstr(b.decode('ascii'))
            return '0x%08X' % v
        if isinstance(kind, list):
            return kind[v] if v < len(kind) else str(v)
        return str(v)

    def event(self, v):
        return self.events.get(v, str(v))

    def param(self, f):
        b = fbits(f)
        if b in self.names and b != 0:
            return 'iAssetParamID(%s)' % self.id(b)
        s = ffmt(f)
        return s if s is not None else 'iAssetFloatBits(0x%08X)' % b


def cstr(s):
    out = '"'
    for ch in s:
        o = ord(ch)
        if ch == '"':
            out += '\\"'
        elif ch == '\\':
            out += '\\\\'
        elif ch == '\n':
            out += '\\n'
        elif ch == '\r':
            out += '\\r'
        elif ch == '\t':
            out += '\\t'
        elif 0x20 <= o < 0x7F:
            out += ch
        else:
            out += '\\x%02X""' % o
    return out + '"'


# --- model ----------------------------------------------------------------------

class Parsed:
    pass


def parse_asset(a, spec):
    p = Parsed()
    p.aid = a.aid
    p.atype = a.atype
    p.data = a.data
    if a.atype == 'TEXT':
        ln = struct.unpack_from('<I', a.data, 0)[0]
        p.text = a.data[4:4 + ln].decode('latin-1')
        p.tail = a.data[4 + ln:]
        return p
    p.fields, size = unpack(spec, a.data)
    assert p.fields['id'] == a.aid
    items = []
    if a.atype == 'GRUP':
        n = p.fields['itemCount']
        items = list(struct.unpack_from('<%dI' % n, a.data, size))
        size += 4 * n
    p.items = items
    lc = p.fields['linkCount']
    p.links = [LINK.unpack_from(a.data, size + 32 * i) for i in range(lc)]
    p.tail = a.data[size + 32 * lc:]
    return p


def serialize(p, spec):
    """What iAssetPkg builds from the emitted code, in Python."""
    if p.atype == 'TEXT':
        body = struct.pack('<I', len(p.text)) + p.text.encode('latin-1') + b'\0'
        return pad4(body)
    f = dict(p.fields)
    f['linkCount'] = len(p.links)
    if p.atype == 'GRUP':
        f['itemCount'] = len(p.items)
    out = pack(spec, f)
    out += b''.join(struct.pack('<I', i) for i in p.items)
    out += b''.join(LINK.pack(*l) for l in p.links)
    return out


def pad4(b):
    return b + b'\0' * (-len(b) % 4)


# --- defaults -------------------------------------------------------------------

SKIP_DEFAULT = {'id', 'linkCount', 'itemCount'}


def mode_defaults(parsed, spec):
    d = collections.OrderedDict()
    for name, _, _, kind in spec:
        if name in SKIP_DEFAULT:
            d[name] = None
            continue
        c = collections.Counter(p.fields[name] for p in parsed)
        d[name] = c.most_common(1)[0][0]
    return d


def is_zero(v):
    return all(x == 0 for x in v) if isinstance(v, tuple) else v == 0


def field_lines(em, spec, fields, base, var):
    lines = []
    for name, _, n, kind in spec:
        if name in SKIP_DEFAULT:
            continue
        v = fields[name]
        if base is not None and base[name] == v:
            continue
        if base is None and is_zero(v):
            continue
        if kind == 'pad':
            raise ValueError('nonzero padding %r in %s' % (v, var))
        if kind == 'vec':
            lines.append('%s.%s = xVec3{ %s };' % (var, name,
                                                  ', '.join(em.value(kind, x) for x in v)))
        elif n > 1:
            lines.append(' '.join('%s.%s[%d] = %s;' % (var, name, i, em.value(kind, x))
                                  for i, x in enumerate(v)))
        elif isinstance(kind, list) and v >= len(kind):
            # Retail stores values the enum has no name for (text box expand 100).
            lines.append('%s.%s = (decltype(%s.%s))%d;' % (var, name, var, name, v))
        else:
            line = '%s.%s = %s;' % (var, name, em.value(kind, v))
            # An unnamed text ID says nothing; the string it points at does.
            if kind == 'id' and v not in em.names and v in em.texts:
                line += ' // %s' % cstr(em.texts[v][:60])
            lines.append(line)
    return lines


def link_line(em, l):
    src, dst, target, p0, p1, p2, p3, widget, chk = l
    args = [em.event(src), em.event(dst), em.id(target)]
    params = [p0, p1, p2, p3]
    tail = []
    if any(fbits(x) for x in params) or widget or chk:
        tail = [em.param(x) for x in params]
        while tail and tail[-1] == '0.0f' and not (widget or chk):
            tail.pop()
    if widget or chk:
        tail += [em.id(widget), em.id(chk)]
        if chk == 0:
            tail.pop()
    return 'p.Link(%s);' % ', '.join(args + tail)


# --- emit ------------------------------------------------------------------------

class _Header:
    TEXT = ("Generated by tools/menugen/menugen.py from the Xbox {hip}, then kept by hand. "
            "This file is the source of {pkg}'s {what} now; edit it directly. "
            "See src/SB/Core/pc/menu/README.md.")

    def format(self, **kw):
        lines = textwrap.wrap(self.TEXT.format(**kw), 77)
        return ''.join('// %s\n' % l for l in lines)


HEADER = _Header()


def emit_text(em, pkg, parsed):
    out = [HEADER.format(hip=os.path.basename(pkg.hip), pkg=pkg.prefix.upper(),
                         what='text'), '#include "iAssetBuild.h"', '',
           'void %s_BuildText(iAssetPkg& p)' % pkg.fn, '{']
    for p in parsed:
        body = p.text
        chunks = re.split(r'(?<=\n)', body)
        if len(chunks) <= 1:
            lit = cstr(body)
            out.append('    p.Text(%s, %s);' % (em.id(p.aid), lit))
        else:
            out.append('    p.Text(%s,' % em.id(p.aid))
            for i, c in enumerate(chunks):
                if c == '':
                    continue
                out.append('           %s%s' % (cstr(c), ');' if i == len(chunks) - 1 else ''))
            if chunks[-1] == '':
                out[-1] += ');'
    out.append('}')
    return '\n'.join(out) + '\n'


def emit_group(em, pkg, tag, parsed):
    # One set of defaults per struct (DYNA has several), one build function
    # in HIP order: creation order runs across all of a type's assets.
    out = []
    kinds = collections.OrderedDict()
    for p in parsed:
        kinds.setdefault(p.suffix, []).append(p)
    defaults = {}
    for suffix, group in kinds.items():
        cstruct, spec = group[0].cstruct, group[0].spec
        defaults[suffix] = mode_defaults(group, spec)
        dname = 'k%sDefaults' % suffix
        out.append('static %s %sInit()' % (cstruct, dname[1:]))
        out.append('{')
        out.append('    %s a;' % cstruct)
        out.append('    memset(&a, 0, sizeof(a));')
        for l in field_lines(em, spec, defaults[suffix], None, 'a'):
            out.append('    ' + l)
        out.append('    return a;')
        out.append('}')
        out.append('')
        out.append('static const %s %s = %sInit();' % (cstruct, dname, dname[1:]))
        out.append('')
    out.append('void %s_Build%s(iAssetPkg& p)' % (pkg.fn, TYPES[tag][3]))
    out.append('{')
    for i, p in enumerate(parsed):
        if i:
            out.append('')
        out.append('    {')
        out.append('        %s a = k%sDefaults;' % (p.cstruct, p.suffix))
        for l in field_lines(em, p.spec, p.fields, defaults[p.suffix], 'a'):
            out.append('        ' + l)
        out.append("        p.Begin('%s', %s, a);" % (tag, em.id(p.aid)))
        for it in p.items:
            out.append('        p.Item(%s);' % em.id(it))
        for l in p.links:
            out.append('        ' + link_line(em, l))
        out.append('        p.End();')
        out.append('    }')
    out.append('}')
    return '\n'.join(out) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--hip', required=True)
    ap.add_argument('--names', required=True, help='GameCube files/ folder with the .LIP listings')
    ap.add_argument('--out', required=True)
    ap.add_argument('--prefix', required=True, help='file prefix, e.g. mnu3')
    ap.add_argument('--beside', action='append', default=[],
                    help='a package always loaded when this one is (mnu4.HIP for mnu3). '
                         'The packer skips any asset it already has, so the code leaves '
                         'those out too.')
    args = ap.parse_args()

    names = load_names(args.names)
    source_names = load_source_strings(names)
    events = load_enum(os.path.join(ROOT, 'src/SB/Core/x/xEvent.h'), 'en_xEventTags')
    btypes = load_enum(os.path.join(ROOT, 'src/SB/Game/zBase.h'), 'en_ZBASETYPE')
    em = Emitter(names, events, btypes)

    hip = hipfile.Package.load(args.hip)
    for aid, s in source_names.items():
        names.setdefault(aid, s)

    class Pkg:
        pass
    pkg = Pkg()
    pkg.hip = args.hip
    pkg.prefix = args.prefix
    pkg.fn = args.prefix.upper()

    # The packer's per-type order: layers in LTOC order, assets in each LHDR's
    # order (PKR_bld_typecnt). The code keeps it, so objects are created in the
    # order retail created them.
    beside = set()
    for path in args.beside:
        beside.update(a.aid for a in hipfile.Package.load(path).assets)
    ordered = []
    skipped = 0
    for ltype, ids, _ in hip.layers:
        for aid in ids:
            a = hip.byid.get(aid)
            if a is not None and a.atype in TYPES:
                if aid in beside:
                    skipped += 1
                    continue
                ordered.append(a)

    bytype = collections.OrderedDict((t, []) for t in TYPES)
    bad = 0
    for name, (cstruct, suffix, spec) in DYNA_TYPES.items():
        DYNA_BY_HASH[xstrhash(name)] = (cstruct, suffix, spec)
    for a in ordered:
        if a.atype == 'DYNA':
            cstruct, suffix, spec = dyna_spec(a.data)
        else:
            cstruct, spec, _, suffix = TYPES[a.atype]
        p = parse_asset(a, spec)
        p.cstruct, p.suffix, p.spec = cstruct, suffix, spec
        got = serialize(p, spec)
        if got != a.data:
            bad += 1
            print('MISMATCH %s %08X %s: %d vs %d bytes' % (a.atype, a.aid, names.get(a.aid, '?'),
                                                         len(got), len(a.data)))
        bytype[a.atype].append(p)
        if a.atype == 'TEXT':
            em.texts[a.aid] = p.text
    if bad:
        sys.exit('%d assets do not round-trip; fix the specs first' % bad)

    def emit_all():
        files = collections.OrderedDict()
        for tag, parsed in bytype.items():
            if not parsed:
                continue
            cstruct, spec, group, suffix = TYPES[tag]
            if tag == 'TEXT':
                files.setdefault(group, []).append(emit_text(em, pkg, parsed))
                continue
            files.setdefault(group, []).append(emit_group(em, pkg, tag, parsed))
        return files

    # First pass finds the IDs nothing names; the Xbox packages are then
    # searched for just those.
    emit_all()
    xbox_dir = os.path.dirname(os.path.abspath(args.hip))
    xbox_pkgs = [os.path.join(xbox_dir, f) for f in os.listdir(xbox_dir)
                 if f.upper().endswith(('.HIP', '.HOP'))]
    xbox_root = os.path.dirname(xbox_dir)
    xbox_pkgs += [os.path.join(xbox_root, f) for f in os.listdir(xbox_root)
                  if f.upper().endswith('.HIP')]
    for aid, s in load_package_strings(xbox_pkgs, em.unnamed).items():
        names.setdefault(aid, s)
    # Text this package points at in another one (mnu4, boot) is quoted too.
    for path in xbox_pkgs:
        if path.upper().endswith('.HIP'):
            for a in hipfile.Package.load(path).by_type('TEXT'):
                ln = struct.unpack_from('<I', a.data, 0)[0]
                em.texts.setdefault(a.aid, a.data[4:4 + ln].decode('latin-1'))
    em.unnamed = set()
    files = emit_all()

    os.makedirs(args.out, exist_ok=True)

    plural = {'GRUP': 'groups', 'COND': 'conditionals', 'TIMR': 'timers',
              'DPAT': 'dispatchers', 'SFX ': 'sound effects', 'CNTR': 'counters',
              'PORT': 'portals'}
    logic = [plural[t] for t, v in bytype.items() if v and t in plural]
    what = {'ui': 'UI widgets', 'uifont': 'text widgets',
            'dyna': 'HUD widgets, text boxes and talk boxes',
            'logic': ', '.join(logic[:-1]) + ' and ' + logic[-1] if len(logic) > 1
            else ''.join(logic)}
    for group, parts in files.items():
        path = os.path.join(args.out, '%s_%s.cpp' % (args.prefix, group))
        with open(path, 'w', newline='\n') as f:
            if group == 'text':
                f.write(parts[0])
                continue
            f.write(HEADER.format(hip=os.path.basename(args.hip), pkg=pkg.fn, what=what[group]))
            f.write('#include "iAssetBuild.h"\n\n')
            f.write('\n'.join(parts))

    owned = [t for t, v in bytype.items() if v]
    entry = [HEADER.format(hip=os.path.basename(args.hip), pkg=pkg.fn, what='asset list'),
             '#include "iAssetBuild.h"', '',
             '// The types this package builds. The HIP\'s own assets of these types are',
             '// never seen; everything else in it is used as shipped.',
             'extern const U32 k%sOwnedTypes[] = { %s, 0 };' % (
                 pkg.fn, ', '.join("'%s'" % t for t in owned)), '']
    fns = [TYPES[t][3] for t in owned]
    entry += ['void %s_Build%s(iAssetPkg& p);' % (pkg.fn, f) for f in fns]
    entry += ['', 'void %s_Build(iAssetPkg& p)' % pkg.fn, '{']
    entry += ['    %s_Build%s(p);' % (pkg.fn, f) for f in fns]
    entry += ['}', '']
    with open(os.path.join(args.out, '%s.cpp' % args.prefix), 'w', newline='\n') as f:
        f.write('\n'.join(entry))

    counts = ', '.join('%s %d' % (t.strip(), len(v)) for t, v in bytype.items() if v)
    print('%s: %s' % (os.path.basename(args.hip), counts))
    print('owned types: %s' % ' '.join(repr(t) for t, v in bytype.items() if v))
    print('%d left out: already in %s' % (skipped, ', '.join(os.path.basename(b)
                                                             for b in args.beside) or '-'))
    print('%d referenced IDs without a name' % len(em.unnamed))
    for v in sorted(em.unnamed):
        print('  0x%08X' % v)


if __name__ == '__main__':
    main()
