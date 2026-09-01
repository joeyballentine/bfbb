#!/usr/bin/env python3
"""Extract the button-prompt glyphs from a BFBB font.HIP into loose PNGs.

The PC port draws button prompts from a folder of PNGs rather than from the
textures in the player's font.HIP, so that the glyph can follow the CONTROLLER
instead of the disc. This produces one such folder per console.

Files come out named for the HOST input they depict -- a.png is the button in
the south position, lt.png is the left trigger -- not for the retail texture
slot. Two reasons:

  * The retail slot numbering is per-console. Xbox's button_picture_02 is the
    GameCube's and PS2's button_picture_03. Resolving that here means the
    runtime never has to know which disc a set came from.

  * The port lets config.ini rebind anything, so the glyph has to be chosen
    from the binding, and a binding names host inputs.

All three discs agree on the POSITION of every face button -- jump is south,
Bubble Bash is north, Bubble Bounce is east, Bubble Spin is west -- and differ
only in the letter printed on it. That is what makes a positional rename
correct rather than a guess. src/SB/Core/pc/iPadGlyph.h is the
runtime half.

Usage:
    python tools/padglyphs.py <font.HIP> <output directory>
"""

import os
import struct
import sys

# The texture behind each host input, per console. The key is the output file
# name; the value is the retail RWTX asset name on that disc.
#
# The face buttons are keyed by position, which is why the four rows differ
# between consoles: xbox pad_button2 and gamecube pad_button3 are both the
# north button, and both come out as y.png.
FACE = {
    "xbox": {"a": 1, "y": 2, "b": 3, "x": 4},
    "gamecube": {"a": 1, "x": 2, "y": 3, "b": 4},
    "ps2": {"a": 1, "x": 2, "y": 3, "b": 4},
}

# The rest is the same slot on every disc. lb and rb take the L2/R2 slots
# because that is where each console drew its fourth and fifth shoulder: the
# Xbox's white and black buttons, the GameCube's made-up "L2" pill and its Z,
# the PS2's literal L2 and R2.
SHARED = {
    "lt": "pad_button_L1",
    "rt": "pad_button_R1",
    "lb": "pad_button_L2",
    "rb": "pad_button_R2",
    "start": "pad_button_start",
    "back": "pad_button_select",
    "stick_left": "pad_button_L_analog",
    "stick_right": "pad_button_R_analog",
}


def hip_assets(path):
    """Every asset in a HIP, as (id, offset, size, name, type)."""
    data = open(path, "rb").read()
    out = []

    def be(o):
        return struct.unpack_from(">I", data, o)[0]

    def walk(o, end):
        while o + 8 <= end:
            magic = data[o : o + 4]
            size = be(o + 4)
            body = o + 8
            if magic == b"AHDR":
                aid = be(body)
                typ = data[body + 4 : body + 8].decode("latin1")
                off, sz = struct.unpack_from(">II", data, body + 8)
                out.append([aid, off, sz, "", typ])
                walk(body + 24, body + size)
            elif magic == b"ADBG":
                out[-1][3] = data[body + 4 : body + size].split(b"\0")[0].decode("latin1")
            elif magic in (b"PACK", b"DICT", b"ATOC", b"LTOC", b"LHDR"):
                walk(body, body + size)
            o = body + size

    walk(0, len(data))
    return data, out


def rw_chunk(blob, start, end, want, little=True):
    """The body offset and size of the first `want` chunk in [start, end)."""
    fmt = "<III" if little else "<III"
    o = start
    while o + 12 <= end:
        typ, size, _ = struct.unpack_from(fmt, blob, o)
        if typ == want:
            return o + 12, size
        o += 12 + size
    raise ValueError("chunk %04x not found" % want)


def native_chunks(blob):
    """Every chunk of the TEXTURENATIVE inside a TEXDICTIONARY."""
    o, size = rw_chunk(blob, 0, len(blob), 0x16)
    o, size = rw_chunk(blob, o, o + size, 0x15)
    out = []
    end = o + size
    while o + 12 <= end:
        typ, csize, _ = struct.unpack_from("<III", blob, o)
        out.append((typ, o + 12, csize))
        o += 12 + csize
    return out


def native_struct(blob):
    """The STRUCT body of the TEXTURENATIVE inside a TEXDICTIONARY."""
    for typ, o, size in native_chunks(blob):
        if typ == 0x01:
            return o, size
    raise ValueError("no STRUCT in TEXTURENATIVE")


def unswizzle(src, w, h, bpp):
    """The Xbox's Morton order, transcribed from librw d3d/xbox.cpp."""
    mask_u = mask_v = 0
    i = j = 1
    while True:
        c = 0
        if i < w:
            mask_u |= j
            j <<= 1
            c = j
        if i < h:
            mask_v |= j
            j <<= 1
            c = j
        i <<= 1
        if not c:
            break
    dst = bytearray(w * h * bpp)
    v = 0
    for y in range(h):
        u = 0
        for x in range(w):
            d = (y * w + x) * bpp
            s = (u | v) * bpp
            dst[d : d + bpp] = src[s : s + bpp]
            u = (u - mask_u) & mask_u
        v = (v - mask_v) & mask_v
    return bytes(dst)


def decode_xbox(blob):
    o, _ = native_struct(blob)
    has_alpha, unknown, w, h = struct.unpack_from("<hhHH", blob, o + 76)
    depth, levels, kind, compression = struct.unpack_from("<BBBB", blob, o + 84)
    if compression or depth != 32:
        raise ValueError("expected uncompressed 32-bit, got depth=%d comp=%d" % (depth, compression))
    px = unswizzle(blob[o + 92 : o + 92 + w * h * 4], w, h, 4)
    # Xbox stores BGRA where the PNG wants RGBA.
    out = bytearray(px)
    out[0::4], out[2::4] = px[2::4], px[0::4]
    return w, h, bytes(out)


def decode_gamecube(blob):
    # The GameCube header carries three extra words before the name, so every
    # field sits 16 bytes later than the Xbox's, and it is big-endian.
    o, _ = native_struct(blob)
    w, h = struct.unpack_from(">HH", blob, o + 92)
    depth, levels, fmt = struct.unpack_from(">BBB", blob, o + 96)
    if fmt != 5 or depth != 16:
        raise ValueError("expected RGB5A3, got fmt=%d depth=%d" % (fmt, depth))
    raw = blob[o + 108 : o + 108 + w * h * 2]
    out = bytearray(w * h * 4)
    i = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            for y in range(4):
                for x in range(4):
                    p = struct.unpack_from(">H", raw, i)[0]
                    i += 2
                    d = ((by + y) * w + bx + x) * 4
                    if p & 0x8000:
                        out[d + 0] = ((p >> 10) & 31) * 255 // 31
                        out[d + 1] = ((p >> 5) & 31) * 255 // 31
                        out[d + 2] = (p & 31) * 255 // 31
                        out[d + 3] = 255
                    else:
                        out[d + 0] = ((p >> 8) & 15) * 17
                        out[d + 1] = ((p >> 4) & 15) * 17
                        out[d + 2] = (p & 15) * 17
                        out[d + 3] = ((p >> 12) & 7) * 255 // 7
    return w, h, bytes(out)


def decode_ps2(blob):
    # The first STRUCT holds only the platform fourcc; the pixels are in the
    # later, larger one, behind a 0x40-byte descriptor.
    o, size = max(((o, s) for t, o, s in native_chunks(blob) if t == 0x01), key=lambda c: c[1])
    ext_o, ext_size = rw_chunk(blob, o, o + size, 0x01)
    w, h, depth = struct.unpack_from("<III", blob, ext_o)
    palette_size = struct.unpack_from("<I", blob, ext_o + 52)[0]
    if depth != 32 or palette_size != 0:
        raise ValueError("expected 32-bit unpalettised, got depth=%d pal=%d" % (depth, palette_size))

    # What follows the descriptor is a DMA upload, not a bitmap: a chunk header,
    # a GIF tag, three register writes setting the transfer up, then a second
    # GIF tag introducing the pixels. Counting forward through all of that means
    # transcribing a format to reach a buffer whose size is already known, and
    # getting the count wrong shifts the image instead of failing -- it comes
    # out looking like it wraps. With no palette the pixels are the last thing
    # in the block, which is what the check above makes true, so count back.
    data = blob[ext_o + ext_size : o + size]
    out = bytearray(data[len(data) - w * h * 4 :])
    # PS2 alpha runs 0..128, where 128 is opaque.
    for i in range(3, len(out), 4):
        out[i] = min(out[i] * 255 // 128, 255)
    return w, h, bytes(out)


DECODERS = {"xbox": decode_xbox, "gamecube": decode_gamecube, "ps2": decode_ps2}


def write_png(path, w, h, rgba):
    import zlib

    raw = b"".join(b"\0" + rgba[y * w * 4 : (y + 1) * w * 4] for y in range(h))

    def chunk(tag, body):
        return (
            struct.pack(">I", len(body))
            + tag
            + body
            + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)
        )

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def detect_console(assets_by_name):
    """Which disc this font.HIP came off, by the size of a known texture."""
    sizes = {4264: "xbox", 4408: "ps2", 2232: "gamecube"}
    size = assets_by_name["pad_button1"][2]
    if size not in sizes:
        raise SystemExit("pad_button1 is %d bytes; not a known console's" % size)
    return sizes[size]


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__)
    hip, outdir = argv[1], argv[2]

    data, assets = hip_assets(hip)
    by_name = {a[3]: a for a in assets if a[4] == "RWTX"}
    if "pad_button1" not in by_name:
        raise SystemExit("%s has no pad_button textures -- is it a font.HIP?" % hip)

    console = detect_console(by_name)
    decode = DECODERS[console]

    wanted = dict(SHARED)
    for token, slot in FACE[console].items():
        wanted[token] = "pad_button%d" % slot

    os.makedirs(outdir, exist_ok=True)
    for token, asset in sorted(wanted.items()):
        entry = by_name.get(asset)
        if entry is None:
            raise SystemExit("%s is missing %s" % (hip, asset))
        _, off, size, _, _ = entry
        w, h, rgba = decode(data[off : off + size])
        write_png(os.path.join(outdir, token + ".png"), w, h, rgba)

    print("%s -> %s (%d glyphs, %dx%d)" % (console, outdir, len(wanted), w, h))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
