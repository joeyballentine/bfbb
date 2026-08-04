# duplicatotron-3000

Experimental branch. **Not intended to be merged into `main`.**

Goal: drive the decompilation to 100% matching, using a scheduler-patched
CodeWarrior (`GC/2.0p1a`, derived at build time by `tools/patch_compiler.py`)
and whatever else it takes. Upstream is not interested in the patched
toolchain, so this branch tracks `main` one-way and never flows back.

## Ground rules

- `main.dol` sha1 must stay `306526d90b48e99894c3138f5fc8f2716d9fecf6`.
- `report.json` is the only metric. No change lands without a real build.
- No regressions: `matched_functions` never goes down.
- When a function is blocked purely on instruction scheduling, extending the
  compiler patch is fair game.

## Status

| metric | value |
|---|---|
| matched functions | 6491 / 10147 (63.97%) |
| complete units | 195 / 543 |
| complete code | 10.99% |

Remaining gap in SB (game) code, by failure mode:

| mode | count |
|---|---|
| pure scheduling (same instructions, different order) | 59 |
| register allocation only | 65 |
| real source difference / not yet written | 2835 |

So the bulk of the remaining work is ordinary decompilation, not compiler
patching.
