# tools

Build plumbing plus the analysis tools used to work the decompilation.

## Build plumbing

| | |
|---|---|
| `project.py`, `ninja_syntax.py` | decomp-toolkit build generation, driven by `configure.py` |
| `patch_compiler.py` | derives the scheduler-patched CodeWarrior at build time |
| `decompctx.py` | flattens a translation unit and its includes into one file |
| `download_tool.py`, `transform_dep.py` | fetch pinned tools, rewrite depfiles |
| `cwexec.py` | reads the CodeWarrior launch command out of `build.ninja`, so the analysis tools compile the way ninja does |
| `report.py`, `upload_progress.py` | progress reporting |

## Analysis

These read `objdiff.json` and `build.ninja`, so run `configure.py` first.
They compile through `cwexec.py`, which takes the launcher, the sjiswrap
wrapper and the compiler path from the `mwcc`/`mwcc_sjis` rules rather than
assuming them - so they work on Linux (where the `.exe` files run under
`wibo`) and with `configure.py --compilers <dir>`.

### `solo.py` — compile and diff one unit, without touching the build

```
solo.py <unit-fragment>                 non-matching functions and percents
solo.py <unit-fragment> <symbol-frag>   side-by-side instruction diff
solo.py <unit-fragment> --missing       target functions absent from our object
```

Compiles a single source file into a temp directory with the exact compiler and
flags `build.ninja` would use, then diffs it against the original object. About
two seconds, and it writes no shared state — so any number of them can run
concurrently in one checkout while `ninja` would have them clobbering each
other's objects and `report.json`.

`--missing` is where the volume is: it lists what has not been written at all.

### `symdump.py` — read a static table out of the target object

```
symdump.py <unit-fragment>                      data symbols, largest first
symdump.py <unit-fragment> <sym> [u32|f32|raw]  dump the bytes
```

The original objects still carry all their static data — goal sequence tables,
sound asset lists, hook name arrays, tweak defaults, jump tables. When a
function you are reconstructing indexes into one of those, read the contents
instead of guessing at them. Guessed data produces source that looks
authoritative and is wrong, and it also settles enum values that would
otherwise be invented.

### `classify.py` — why the remaining functions differ

"3000 functions left" is not actionable. This buckets them:

- `MISSING` — not written yet
- `POOL` — byte-identical apart from an anonymous `@NNN` literal-pool index, so
  blocked on translation-unit completeness rather than on the function itself
- `SCHED` — same instructions, different order
- `REGS` — same mnemonics, different registers; usually responds to reshaping
  the source expression
- `SIZE` / `OTHER` — a real source difference

Also prints the units closest to complete, which is the cheapest way to raise
`complete_units`.

### `smoke.py` — does everything still compile?

```
smoke.py                        every unit
smoke.py <frag> [<frag>...]     only matching units
smoke.py --skip <frag>,<frag>   everything except these
```

Compiles every unit into a throwaway object and reports only failures. Useful
for checking a shared-header change against the whole tree without writing
build state or waiting for a link. It proves the tree compiles; it says nothing
about whether the DOL still matches — run a real build for that.

### `flagsweep.py` — find a library's real compiler flags

```
flagsweep.py <object-path-fragment> [flag[,flag...]]
```

Appends a candidate flag and counts how many more functions reach 100%. This is
how `MSL_C`'s `-opt level=4 -inline on` and rwsdk's `-fp_contract on` were
found. The reconstructed command matches ninja's exactly for `.c` units but not
always for C++ ones, so treat a hit as a hypothesis and confirm it with a full
build before changing `configure.py`.
