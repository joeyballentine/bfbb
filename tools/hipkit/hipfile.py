"""Read and write HIP/HOP archives.

The container is a tree of chunks: a four-character big-endian id, a
big-endian size, then that many payload bytes padded to an even length.
xhipio.cpp reads it; xpkrsvc.cpp interprets the tree.

Layout of a package:

    HIPA (empty)
    PACK
        PVER  subver, cltver, compatver
        PFLG  flags
        PCNT  asset count, layer count, ...
        PCRT  unix time, build string
        PMOD  unix time
        PLAT  tag, platform, video, language, title
    DICT
        ATOC
            AINF  0
            AHDR  id, type, offset, size, pad, flags
                ADBG align, "", name, checksum
            ...
        LTOC
            LINF  0
            LHDR  layer type, count, asset ids...
                LDBG checksum
    STRM
        DHDR  0
        DPAK  the asset payloads, back to back

Asset offsets in AHDR are absolute file offsets.
"""

import struct
import time


def fourcc(s):
    return struct.unpack('>I', s.encode('ascii'))[0]


def unfourcc(v):
    return struct.pack('>I', v & 0xFFFFFFFF).decode('latin-1')


class Chunk:
    def __init__(self, cid, payload=b'', children=None):
        self.cid = cid if isinstance(cid, str) else unfourcc(cid)
        self.payload = payload
        self.children = children if children is not None else []
        self.offset = 0

    def find(self, cid):
        for c in self.children:
            if c.cid == cid:
                return c
        return None

    def findall(self, cid):
        return [c for c in self.children if c.cid == cid]

    def __repr__(self):
        return '<%s %d bytes, %d children>' % (self.cid, len(self.payload), len(self.children))


# Chunks whose payload is itself a chunk list rather than opaque bytes.
CONTAINERS = {'PACK', 'DICT', 'ATOC', 'LTOC', 'STRM'}

# Chunks that carry fixed fields and then a chunk list.
MIXED = {'AHDR': 24, 'LHDR': None}


def _parse(buf, start, end):
    out = []
    pos = start
    while pos + 8 <= end:
        cid = buf[pos:pos + 4].decode('latin-1')
        size = struct.unpack_from('>I', buf, pos + 4)[0]
        body = pos + 8
        stop = body + size
        ch = Chunk(cid)
        ch.offset = pos
        if cid in CONTAINERS:
            ch.children = _parse(buf, body, stop)
        else:
            ch.payload = buf[body:stop]
        out.append(ch)
        pos = stop + (stop & 1)
    return out


class Reader:
    """A cursor over a chunk payload, matching HIPLRead* semantics."""

    def __init__(self, data):
        self.d = data
        self.p = 0

    def long(self):
        v = struct.unpack_from('>i', self.d, self.p)[0]
        self.p += 4
        return v

    def ulong(self):
        v = struct.unpack_from('>I', self.d, self.p)[0]
        self.p += 4
        return v

    def string(self):
        e = self.d.index(b'\0', self.p)
        s = self.d[self.p:e].decode('latin-1')
        n = e - self.p
        self.p = e + 1
        if not (n & 1):
            self.p += 1
        return s

    def eof(self):
        return self.p >= len(self.d)


class Writer:
    def __init__(self):
        self.b = bytearray()

    def long(self, v):
        self.b += struct.pack('>I', v & 0xFFFFFFFF)
        return self

    def string(self, s):
        raw = s.encode('latin-1') + b'\0'
        if len(raw) & 1:
            raw += b'\0'
        self.b += raw
        return self

    def bytes(self, d):
        self.b += d
        return self

    def out(self):
        return bytes(self.b)


class Asset:
    def __init__(self, aid, atype, data, name='', align=0, flags=0, checksum=0, layer=0):
        self.aid = aid
        self.atype = atype          # four-character string
        self.data = data
        self.name = name
        self.align = align
        self.flags = flags
        self.checksum = checksum
        self.layer = layer
        self.pad = 0

    def __repr__(self):
        return '<%s %08X %r %d bytes>' % (self.atype, self.aid, self.name, len(self.data))


LAYER_NAMES = ['DEFAULT', 'TEXTURE', 'BSP', 'MODEL', 'ANIMATION', 'VRAM', 'SRAM',
               'SNDTOC', 'CUTSCENE', 'CUTSCENETOC', 'JSPINFO']


def _mkchunk(cid, payload):
    out = cid.encode('ascii') + struct.pack('>I', len(payload)) + payload
    if len(payload) & 1:
        out += b'\0'
    return out


class Package:
    def __init__(self):
        self.subver = 2
        self.cltver = 1
        self.compatver = 1
        self.flags = 0x0A
        self.pcnt_rest = (0, 0, 0)
        self.time_made = int(time.time())
        self.build_string = 'hipkit'
        self.time_mod = int(time.time())
        self.plat_tag = fourcc('XBOX')
        self.plat = ('Xbox', 'NTSC', 'US Common', 'Sponge Bob')
        self.assets = []            # in file order
        self.layers = []            # list of [layer_type, [aid, ...], checksum]
        self.byid = {}

    # ---- reading -------------------------------------------------------

    @classmethod
    def load(cls, path):
        with open(path, 'rb') as f:
            return cls.parse(f.read())

    @classmethod
    def parse(cls, buf):
        pkg = cls()
        pkg.raw = buf
        top = _parse(buf, 0, len(buf))
        byid = {}
        for c in top:
            if c.cid == 'PACK':
                for sc in c.children:
                    r = Reader(sc.payload)
                    if sc.cid == 'PVER':
                        pkg.subver = r.long()
                        pkg.cltver = r.long()
                        pkg.compatver = r.long() if not r.eof() else 1
                    elif sc.cid == 'PFLG':
                        pkg.flags = r.long()
                    elif sc.cid == 'PCNT':
                        r.long()
                        r.long()
                        pkg.pcnt_rest = (r.long(), r.long(), r.long())
                    elif sc.cid == 'PCRT':
                        pkg.time_made = r.long()
                        if pkg.subver > 1:
                            pkg.build_string = r.string()
                    elif sc.cid == 'PMOD':
                        pkg.time_mod = r.long()
                    elif sc.cid == 'PLAT':
                        pkg.plat_tag = r.ulong()
                        pkg.plat = (r.string(), r.string(), r.string(), r.string())
            elif c.cid == 'DICT':
                atoc = c.find('ATOC')
                for ah in atoc.findall('AHDR'):
                    r = Reader(ah.payload)
                    a = Asset(r.ulong(), unfourcc(r.ulong()), b'')
                    off = r.ulong()
                    size = r.ulong()
                    a.pad = r.ulong()
                    a.flags = r.ulong()
                    for sub in _parse(ah.payload, r.p, len(ah.payload)):
                        if sub.cid == 'ADBG':
                            d = Reader(sub.payload)
                            a.align = d.long()
                            d.string()
                            a.name = d.string()
                            if pkg.subver > 1:
                                a.checksum = d.ulong()
                    a.data = buf[off:off + size]
                    pkg.assets.append(a)
                    byid[a.aid] = a
                ltoc = c.find('LTOC')
                for lh in ltoc.findall('LHDR'):
                    r = Reader(lh.payload)
                    ltype = r.long()
                    n = r.long()
                    ids = [r.ulong() for _ in range(n)]
                    chk = 0
                    for sub in _parse(lh.payload, r.p, len(lh.payload)):
                        if sub.cid == 'LDBG' and pkg.subver > 1:
                            chk = Reader(sub.payload).ulong()
                    pkg.layers.append([ltype, ids, chk])
                    for i in ids:
                        if i in byid:
                            byid[i].layer = ltype
        pkg.byid = byid
        return pkg

    def by_type(self, atype):
        return [a for a in self.assets if a.atype == atype]

    def get(self, aid):
        return self.byid.get(aid)

    # ---- writing -------------------------------------------------------

    def add(self, asset, layer):
        self.assets.append(asset)
        self.byid[asset.aid] = asset
        asset.layer = layer
        for l in self.layers:
            if l[0] == layer:
                l[1].append(asset.aid)
                return
        self.layers.append([layer, [asset.aid], 0])

    def save(self, path):
        with open(path, 'wb') as f:
            f.write(self.build())

    def build(self):
        # A layer is read as ONE block: PKR_LoadStep_Async seeks to the first
        # asset's offset and reads laysize bytes, then hands each asset the
        # slice at (its offset - the first's). So a layer's assets have to sit
        # contiguously in the file, in the order its LHDR lists them, and
        # d_pad has to be the real gap to the next one.
        self.assets = [self.byid[i] for _, ids, _ in self.layers for i in ids]

        # AHDR offsets are absolute, so the dictionary has to be sized before
        # the payloads are placed. Its length does not depend on the offset
        # values, only on their count, so one pass with placeholders is enough.
        head = self._header(placeholder=True)
        start = len(head) + 8 + _chunklen(4) + 8   # STRM + DHDR + DPAK headers
        offsets, end = self._place(start)
        head = self._header(placeholder=False, offsets=offsets)

        body = bytearray(end - start)
        for a in self.assets:
            off = offsets[a.aid] - start
            body[off:off + len(a.data)] = a.data
        # A layer's size is rounded up to 2K, and the loader reads that much.
        body += b'\0' * (2048 + (-len(body)) % 2048)

        strm = _mkchunk('DHDR', struct.pack('>I', 0)) + _mkchunk('DPAK', bytes(body))
        return head + _mkchunk('STRM', strm)

    def _place(self, base):
        """Lay the assets out layer by layer and record each one's gap."""
        offsets = {}
        cur = base
        for _, ids, _ in self.layers:
            if not ids:
                continue
            # The loader allocates each layer's block 64-byte aligned, so an
            # asset's alignment within the block is its file alignment only
            # while the layer itself starts on a boundary that divides it.
            cur += (-cur) % 2048
            prev = None
            for i in ids:
                a = self.byid[i]
                al = a.align if a.align > 1 else 1
                cur += (-cur) % al
                if prev is not None:
                    prev.pad = cur - (offsets[prev.aid] + len(prev.data))
                offsets[a.aid] = cur
                cur += len(a.data)
                prev = a
            prev.pad = 0
        return offsets, cur

    def _header(self, placeholder, offsets=None):
        pack = b''
        pack += _mkchunk('PVER', struct.pack('>III', self.subver & 0xFFFFFFFF,
                                             self.cltver & 0xFFFFFFFF,
                                             self.compatver & 0xFFFFFFFF))
        pack += _mkchunk('PFLG', struct.pack('>I', self.flags & 0xFFFFFFFF))
        pack += _mkchunk('PCNT', struct.pack('>IIIII', len(self.assets), len(self.layers),
                                             *[v & 0xFFFFFFFF for v in self.pcnt_rest]))
        w = Writer().long(self.time_made)
        if self.subver > 1:
            w.string(self.build_string)
        pack += _mkchunk('PCRT', w.out())
        pack += _mkchunk('PMOD', struct.pack('>I', self.time_mod & 0xFFFFFFFF))
        w = Writer().long(self.plat_tag)
        for s in self.plat:
            w.string(s)
        pack += _mkchunk('PLAT', w.out())

        atoc = _mkchunk('AINF', struct.pack('>I', 0))
        for a in self.assets:
            off = 0 if placeholder else offsets[a.aid]
            hdr = struct.pack('>IIIIII', a.aid, fourcc(a.atype), off, len(a.data),
                              a.pad, a.flags & 0xFFFFFFFF)
            dbg = Writer().long(a.align).string('').string(a.name)
            if self.subver > 1:
                dbg.long(a.checksum)
            atoc += _mkchunk('AHDR', hdr + _mkchunk('ADBG', dbg.out()))

        ltoc = _mkchunk('LINF', struct.pack('>I', 0))
        for ltype, ids, chk in self.layers:
            body = struct.pack('>II', ltype, len(ids))
            body += b''.join(struct.pack('>I', i) for i in ids)
            if self.subver > 1:
                body += _mkchunk('LDBG', struct.pack('>I', chk))
            ltoc += _mkchunk('LHDR', body)

        dic = _mkchunk('ATOC', atoc) + _mkchunk('LTOC', ltoc)
        return _mkchunk('HIPA', b'') + _mkchunk('PACK', pack) + _mkchunk('DICT', dic)


def _chunklen(payloadlen):
    return 8 + payloadlen + (payloadlen & 1)
