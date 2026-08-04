# duplicatotron-3000

Experimental branch. **Not intended to be merged into `main`.**

Goal: drive the decompilation to 100% matching, using a scheduler-patched
CodeWarrior (`GC/2.0p1a`, derived at build time by `tools/patch_compiler.py`)
and whatever else it takes. Upstream is not interested in the patched
toolchain, so this branch tracks `main` one-way and never flows back. Library
code (MSL, Dolphin SDK, rwsdk, bink, MetroTRK) is in scope here even though it
is off-limits for upstream PRs.

## Ground rules

- `main.dol` sha1 must stay `306526d90b48e99894c3138f5fc8f2716d9fecf6`.
- `report.json` is the only metric. No change lands without a real build.
- No regressions: `matched_functions` never goes down.
- When a function is blocked purely on instruction scheduling, extending the
  compiler patch is fair game.

## Status

| metric | at branch point | now |
|---|---|---|
| matched functions | 6491 / 10147 | 6511 / 10147 |
| fuzzy match | 57.343% | 57.388% |
| complete units | 195 / 543 | 195 / 543 |

## Where the remaining 3636 functions are

Classified with `classify_all.py` (see tooling below):

| class | count | meaning |
|---|---|---|
| MISSING | 1483 | not written yet — no symbol in our object |
| OTHER | 710 | same count, different code |
| SIZE | 699 | different instruction count — real source difference |
| POOL | 146 | **identical code**, only anonymous `@NNN` literal/template pool composition differs |
| SCHED | 29 | identical instruction multiset, different order |
| REGS | 16 | identical mnemonics, different register numbers |

`POOL` functions are byte-identical apart from which anonymous constant they
reference; objdiff pairs anonymous symbols by ordinal position within a
section. Biggest clusters: `xFont` (13), `zEntCruiseBubble` (13), `xMath` (9),
`zNPCGoalRobo` (9), `zNPCTypeBossPlankton` (9). See **Settled** below for why
this is not the cheap bucket it looks like.

86 units are within 3 functions of being complete — finishing those is the
fastest route to raising `complete_units`.

## Tooling (scratchpad)

- `od.py <unit-frag> [sym-frag]` — authoritative per-function diff via
  `objdiff-cli diff --format json` against the real build outputs. With no
  symbol it lists a unit's non-matching functions and percents.
- `pools.py <unit-frag>` — target vs ours data-symbol layout, for diagnosing
  POOL mismatches.
- `classify_all.py` — classifies every non-matching function project-wide.
- `vary.py <src> <obj> <unit> <sym> <variants.py>` — patch a source snippet,
  rebuild just that object, report the percent, restore. The main workhorse:
  most fixes here were found by trying 3-4 expression shapes.
- `gh.sh <out.c> <func>...` — Ghidra headless decompilation of the
  symbol-bearing `sbgcM.elf`, ~5s per batch. Use
  `ghidra_11.3.1_PUBLIC_20250219`, not `ghidra_11.3_DEV` (too old for the
  project file).

## Patterns that keep working

- A float compared against a literal zero: `if (speed)` gives
  `fcmpu speed, 0.0` — `if (speed != 0.0f)` gives the operands the other way.
- Reusing a parameter as the destination (`f2 = tmp - f2;`) pins the result to
  that parameter's register.
- Collapsing two statements into one expression changes which temporaries are
  live at the same time, and therefore the register numbering.
- A helper whose return value is never used is usually `void` in the original;
  a non-void return forces the result into `f1`/`r3`.

## Settled

- **`xVec3::operator=` — done, +11.** The hand-written definition was blocking
  every implicitly generated assignment operator of a struct containing an
  `xVec3`. CodeWarrior inlines an *implicit* member `operator=` into the
  enclosing implicit one, but emits a call when the member's is *user-declared*.
  Deleting both declaration and definition lets CW generate it; it is still
  emitted out of line in `xBound.o` and still matches byte for byte. The
  apparent contradiction (`__as__5xVec3FRC5xVec3` exists in the target objects)
  was the clue misread: that symbol *is* the compiler-generated one.
- **POOL is not cheap after all.** Investigated `xMath`: our `.sdata2` pool has
  the same *contents* but the double literal used by `xurand` lands at index 1
  instead of 8, because the functions ahead of it in the file are stubbed and
  allocate one float literal where the original allocates seven. Aligning a
  pool needs the whole unit reconstructed, not a local edit. Treat POOL as a
  *symptom* of unit incompleteness rather than an independent bucket.
- **`xVec3 v = { 0, 0, 0 }` in `xVec3::cross`** matches the target's anonymous
  rodata template and takes `cross` from 36% to 91%, but it adds that template
  to the `.rodata` of every unit that includes `xVec3.h`, which shifts `zVar`'s
  pool and **breaks the DOL**. Reverted.

## Open leads

- **Epilogue `lwz` swap** (`lwz r31` before `lwz r0`) — checked, it is the sole
  blocker for only 2 functions project-wide, so it is *not* worth a compiler
  patch.
- **Ghidra for the MISSING bucket.** 1483 functions have no implementation at
  all; `gh.sh` gives a usable starting point for each.
