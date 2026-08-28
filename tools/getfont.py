#!/usr/bin/env python3
"""Fetch or install a TrueType font for the port to draw text with.

    python tools/getfont.py                     # download SpongeBoyTT1
    python tools/getfont.py --file X.ttf        # install one you already have
    python tools/getfont.py --url URL           # some other .ttf or .zip

It saves into fonts/, which is gitignored, and prints the config.ini line to
paste. Nothing is redistributed with this repository.

**Why bother.** The game's fonts are texture atlases authored for a 640x480
framebuffer, so above that they are magnified and text is the first thing to go
soft. `[text] font` points at an outline to draw the same letterforms from
instead, at whatever size they are actually drawn. Everything else -- layout,
spacing, colour, the inline tags -- stays the game's. See
src/SB/Core/pc/iFont.h.

**Which font.** SpongeBoyTT1 is the face the game itself used. The default here
is FontSpace's copy, whose archive states `license: Freeware`; check that for
yourself if it matters to you, and use --file if you would rather bring your
own. Any .ttf works, and a wrong one costs nothing -- set [text] font back to
empty and the game's own atlas is used again, exactly as the console draws it.
"""

import argparse
import io
import os
import sys
import urllib.error
import urllib.request
import zipfile

# FontSpace's download endpoint for SpongeBoyTT1. The family id is stable and
# the page it comes from is:
DEFAULT_PAGE = "https://www.fontspace.com/spongeboytt1-font-f29761"
DEFAULT_URL = "https://www.fontspace.com/get/family/pqjyw"

# A bare urllib User-Agent is refused by most font sites.
HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0 Safari/537.36"
    ),
    "Accept": "*/*",
}

# 0x00010000 is a TrueType outline; the rest are the other sfnt flavours that
# turn up behind a .ttf or .otf extension.
SFNT_MAGIC = (b"\x00\x01\x00\x00", b"true", b"OTTO", b"ttcf")


def fetch(url):
    req = urllib.request.Request(url, headers=HEADERS)
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def font_from(data, name_hint):
    """The font bytes, whether `data` is a font or an archive holding one."""
    if data[:4] in SFNT_MAGIC:
        return name_hint, data

    if data[:2] == b"PK":
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            members = [n for n in z.namelist() if n.lower().endswith((".ttf", ".otf"))]
            if not members:
                raise SystemExit("that archive holds no .ttf or .otf")

            # Anything the archive says about its own terms is worth showing
            # rather than burying, since this script's whole job is fetching
            # somebody else's file.
            for note in ("info.txt", "README.txt", "license.txt"):
                if note in z.namelist():
                    text = z.read(note).decode("utf-8-sig", "replace").strip()
                    if text:
                        print("the archive says:")
                        for line in text.splitlines():
                            print("    {}".format(line))
                        print()
                    break

            # The shortest name, which is the plain weight when a whole family
            # is shipped together.
            member = sorted(members, key=len)[0]
            return os.path.basename(member), z.read(member)

    if data[:6] == b"\x37\x7a\xbc\xaf\x27\x1c":
        raise SystemExit(
            "that is a .7z, which Python cannot open without another package. "
            "Extract the .ttf yourself and pass that with --file."
        )

    if data[:1] == b"<" or b"<html" in data[:2048].lower():
        raise SystemExit(
            "that URL returned a page rather than a file. Download it in a "
            "browser and pass it with --file."
        )

    raise SystemExit("that is neither a font nor a zip.")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--file", help="a .ttf, .otf or .zip already on this machine")
    ap.add_argument("--url", help="download from here instead of the default")
    ap.add_argument("--out", help="where to put it (default: fonts/ in the repository)")
    args = ap.parse_args()

    if args.file:
        if not os.path.isfile(args.file):
            raise SystemExit("no such file: {}".format(args.file))
        with open(args.file, "rb") as f:
            data = f.read()
        hint = os.path.basename(args.file)
    else:
        url = args.url or DEFAULT_URL
        if not args.url:
            print("from {}".format(DEFAULT_PAGE))
        print("fetching {}".format(url))
        try:
            data = fetch(url)
        except (urllib.error.HTTPError, urllib.error.URLError) as e:
            print("  refused: {}".format(e))
            print()
            print("Download it in a browser and run:")
            print("    python tools/getfont.py --file <the .ttf or .zip>")
            return 1
        hint = "font.ttf"

    name, ttf = font_from(data, hint)

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = args.out or os.path.join(root, "fonts")
    os.makedirs(out_dir, exist_ok=True)

    path = os.path.join(out_dir, name)
    with open(path, "wb") as f:
        f.write(ttf)

    print("saved {} ({:,} bytes)".format(path, len(ttf)))
    print()
    print("Put this in config.ini:")
    print()
    print("    [text]")
    print("    font = {}".format(path.replace("\\", "/")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
