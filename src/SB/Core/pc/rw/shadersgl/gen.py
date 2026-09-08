# Regenerates the checked-in .inc files from their .frag sources.
#
# Same arrangement as librw's own GL shaders (third_party/librw/src/gl/shaders):
# the source is wrapped one string literal per line and checked in, so the build
# needs no shader tooling. librw does it with a sed recipe in a Makefile; make is
# not installed on Windows, and this writes the same bytes.
#
# Run it when a .frag changes, and commit the .inc with it.

import os
import sys

SHADERS = [
    ("glow_bright.frag", "glow_bright_frag_src", "glow_bright_gl.inc"),
    ("glow_blur.frag", "glow_blur_frag_src", "glow_blur_gl.inc"),
    ("distort.frag", "distort_frag_src", "distort_gl.inc"),
]


def generate(src, var, out):
    with open(src, "r", newline="") as f:
        lines = f.read().splitlines()

    body = ["const char *%s =" % var]
    for line in lines:
        # Blank lines stay blank, which is what librw's sed leaves them.
        body.append('"%s\\n"' % line if line else "")
    body.append(";")

    with open(out, "w", newline="\n") as f:
        f.write("\n".join(body) + "\n")


def main():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    for src, var, out in SHADERS:
        generate(src, var, out)
        print("wrote %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
