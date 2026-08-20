#!/usr/bin/env python3
"""How to launch the CodeWarrior tools, read off build.ninja.

solo.py, symorder.py and smoke.py all compile a unit by hand, and all three
used to spell the command out themselves: run `sjiswrap.exe` directly, look
for the compiler under `build/compilers`. Both assumptions are Windows-and-
default-layout only. On Linux the .exe files are launched through
`build/tools/wibo`, and `configure.py --compilers <dir>` puts the compilers
wherever it likes.

ninja already knows all of this, so take it from there instead of guessing:
the `mwcc` and `mwcc_sjis` rules carry the launcher, the optional sjiswrap
wrapper and the compiler path, and everything before `$cflags` is exactly the
prefix needed to invoke the compiler. Note the two rules differ by more than
the wrapper name -- plain `mwcc` does not go through sjiswrap at all -- so
pass the rule the unit is actually built with.
"""
import os
import re
import shlex

__all__ = ["compile_prefix", "objdiff_cli", "dtk"]

_CONT = re.compile(r"\$\n\s*")


def _rule_command(ninja, rule):
    m = re.search(r"^rule %s\n  command = ((?:.*\$\n)*.*)\n" % re.escape(rule),
                  ninja, re.M)
    if not m:
        raise SystemExit("no `rule %s` in build.ninja - re-run configure.py" % rule)
    return _CONT.sub(" ", m.group(1))


def compile_prefix(ninja, rule, mw_version):
    """argv prefix that gets you as far as the compiler, `$cflags` next.

    Covers the launcher (wibo on non-Windows), the sjiswrap wrapper when the
    rule uses one, and the compiler path including any --compilers override.
    """
    command = _rule_command(ninja, rule)
    head = command.split("$cflags")[0]
    return shlex.split(head.replace("$mw_version", mw_version))


def objdiff_cli(root):
    """Path to objdiff-cli, which download_tool.py names per platform."""
    for name in ("objdiff-cli.exe", "objdiff-cli"):
        path = os.path.join(root, "build", "tools", name)
        if os.path.exists(path):
            return path
    raise SystemExit("build/tools/objdiff-cli not found - run `ninja` once")


def dtk(root):
    """Path to decomp-toolkit, which download_tool.py names per platform."""
    for name in ("dtk.exe", "dtk"):
        path = os.path.join(root, "build", "tools", name)
        if os.path.exists(path):
            return path
    raise SystemExit("build/tools/dtk not found - run `ninja` once")
