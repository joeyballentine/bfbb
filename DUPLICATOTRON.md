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
| matched functions | 6491 / 10147 | 6777 / 10147 |
| fuzzy match | 57.343% | 59.125% |
| complete units | 195 / 543 | 195 / 543 |

## Where the remaining functions are

Classified with `classify_all.py` (see tooling below). This counts the
2837 non-matching functions in units objdiff can compare; the project
total of 3370 also includes units with no diffable object at all.

| class | count | meaning |
|---|---|---|
| MISSING | 1281 | not written yet — no symbol in our object |
| OTHER | 735 | same count, different code |
| SIZE | 626 | different instruction count — real source difference |
| POOL | 147 | **identical code**, only anonymous `@NNN` literal/template pool composition differs |
| SCHED | 31 | identical instruction multiset, different order |
| REGS | 17 | identical mnemonics, different register numbers |

`POOL` functions are byte-identical apart from which anonymous constant they
reference; objdiff pairs anonymous symbols by ordinal position within a
section. Biggest clusters: `xFont` (13), `zEntCruiseBubble` (13), `xMath` (9),
`iMath3` (7), `zNPCGoalRobo` (7), `zNPCTypeVillager` (7), `zNPCSupport` (7),
`zNPCTypeKingJelly` (7), `zNPCSupplement` (7). See **Settled** below for why
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
- `flagsweep.py <obj-frag> [flags]` and `optsweep.py` — recompile each unit
  with an extra compiler flag appended and count how many functions reach
  100%. This is what found the MSL opt level and inlining mode. **Its baseline
  is only trustworthy for `.c` units:** for SB `.cpp` units the reconstructed
  command differs from the real ninja one, and it reported gains a real build
  did not produce. Always confirm with a full build.
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

- **MSL_C compiler flags were wrong - +51.** `configure.py` built
  `MSL_C.PPCEABI.H` at `-opt level=0 -inline off`. Sweeping against the target
  objects: level 0 matches *nothing* anywhere, level 4 is best or tied for
  every unit (+40), and `-inline on` beats `-inline off` in seven more (+11).
  Nothing regressed. Two Runtime units want `-inline deferred` (+1).
- **dolphin and SB flags are right.** A 42-flag sweep over all 90 dolphin
  units and a 15-flag sweep over all 164 SB units produced no verified gain.
- **bink is built with ProDG (SN gcc), not CodeWarrior.** CodeWarrior `asm`
  blocks do not compile there; use GNU inline asm. `binkngc`'s time-base
  readers are single asm blocks with hardcoded r11/r9/r0.

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

## Working the MISSING bucket

`zNPCTypeDutchman` went 59 -> 86 matching in one pass. What worked:

1. Ghidra headless (`gh.sh`) on a batch of the smallest missing symbols at
   once. The one-liners (`get_orbit`, `get_center`, `get_facing`,
   `enable_emitter`, `emit_particles`, `PRIV_GetLassoData`, `IsAlive`, ...)
   came out matching first try, 11 for 11.
2. Reading struct offsets straight out of the Ghidra output against the
   headers - `this+0x24` is `xEnt::model`, `model+0x4c` is `Mat`, and so on.
3. For anonymous-namespace tweak structs, computing field offsets from the
   declaration and looking up the one the target loads (`tweak+0x170` turned
   out to be `damage.slime_time`).
4. **Template members only appear when something calls them.** Explicit
   instantiation (`template struct static_queue<T>;`) is silently ignored by
   this compiler - CodeWarrior still only emits used members. Writing one real
   method (`update_slime`) pulled in nine container functions with it.

A second pass added `update_hand_trail`, `refresh_reticle`, `halt`,
`turning`, `get_eye_loc` and the Nil/Disappear/Reappear goal entry points.
`zNPCTypeDutchman` is now 94 / 227.

Six of those sit at 99.4-99.8% blocked on one thing: the target's `.sdata2`
float pool starts `@1603, @1604, @1605 (0.0f), @1606 (1.0f)` while ours has
thirteen entries ahead of `0.0f`. Literals are allocated in first-use order,
so the pool only lines up once the functions ahead of them in the file are
written. `@1603`/`@1604` are used by the no-argument `turning() const`, which
suggests that function sits near the top of the original file.

`static_queue::init` was also simply wrong: `_max_size` is the rounded-up
power of two and `_max_size_mask` is that minus one, not the shift count and
the power. Fixing it also fixed the instantiations in `xDecal` and
`xLaserBolt`.

## xFX / tier_queue

`xFX` went 109 -> 80 non-matching. `tier_queue`, `tier_queue_allocator` and
the ribbon update/insert path are written; what remains there is the render
side (`render`, `render_strip`, `eval_joint`, `refresh_joint`, `get_normal`)
plus `init`, `set_texture` and `xFXRibbonSceneEnter`.

Type signatures carry real information here:

- Container index parameters are **`u32` (`unsigned long`)**, not `U32`
  (`unsigned int`). The mangling says so: `CFUl` vs `CFUi`. Getting this wrong
  costs every caller a mismatched `bl` target.
- `tier_queue::empty` reads `_size` directly; going through `size()` costs a
  call. `static_queue::empty` *does* go through `size()` - the two containers
  genuinely differ, so do not assume symmetry.
- `xFXRibbon::need_update` returns `bool` (caller tests with `clrlwi.`);
  `render_compare` returns `S32` (caller tests with `cmpwi`). One instruction
  in the *caller* is the only tell.

## Header-declared helpers are free wins

A whole class of missing functions are small helpers the headers *declare* but
nobody ever defined - `xVec3::create`, unary `operator-`, `safe_normal`,
`up_normal`, `xVec2::create`/`operator*`/`operator+=`/`operator*=`. In the
target they are weak per-TU symbols, so defining them inline in the header
makes them appear everywhere they are used at once. Cheap, and it also removes
unresolved externs.

Still unwritten in this class, worth doing next: `basic_rect<F32>` accessors,
`xSCurve`, `xQuickCullForSphere`, and `auto_tweak::load_param<T1,T2>` (that
last one is a template with per-type specialisations - `<f,f>` calls
`zParamGetFloat`, `<i,i>` calls `zParamGetInt`, `<xVec3,i>` calls
`zParamGetVector` - so it needs explicit specialisations, not one body).

## Running several agents in parallel

Naive parallelism does not work here: every agent running `ninja` in one
checkout clobbers the others' objects and `report.json`. `solo.py` removes the
contention - it compiles a single unit into a private temp directory with the
exact flags from `build.ninja` and diffs that against the target object, ~2s,
writing no shared state. So N agents can share one checkout as long as:

- each owns a different `.cpp` (+ its own `.h`),
- nobody runs `ninja`, `configure.py`, or touches git,
- nobody edits a shared header - they report the change they want instead.

The integrating session runs the real build once at the end.

First run of this, six agents on six units, +138 functions verified by a
clean build:

| unit | non-matching before -> after |
|---|---|
| `zNPCTypeBossPlankton` | 105 -> 58 |
| `zNPCTypeKingJelly` | 93 -> 56 |
| `zNPCFXCinematic` | 72 -> 51 |
| `xShadow` | 28 -> 18 |
| `zNPCTypeBossSB2` | 101 -> 94 |
| `zEntPlayerBungeeState` | 70 -> 69 |

Two of the six ran out of session mid-edit and left work that did not
compile; both were salvageable after a few minutes of repair, so a dead
agent is worth triaging rather than reverting. **Do not trust an agent's
own claim that a unit is clean** - re-run `solo.py` and `clang-format -n
--Werror` on its files yourself before committing. One agent reported no
new formatting violations when it had introduced one.

Things committed from that run that are guesses, not evidence, and should
be revisited if they ever block something:

- `enum en_npcburst` in `zNPCFXCinematic` is **fabricated**. The mangled
  name of the function forces *an* enum of that name; the enumerators are
  invented. No such enum exists anywhere in the repo.
- `sphere_hits_sphere_xz` returns bare `4`/`2`/`1`; there was surely an
  enum.
- `SysEvent` uses `switch ((S32)toEvent)` purely as a shape hack.
- `ShadowLight` / `ShadowCamera` / `ShadowCameraRaster` are declared
  `volatile` as a matching device, not because they are.
- The bone index `21` in Plankton's `aim_gun` is a literal.

Note `solo.py` parses `build.ninja`, which has two rule layouts: the source
file is on the same line as the `build` statement when it fits, on the next
line when it does not. The parser handles both; if you extend it, keep that.

## Open leads

- **Epilogue `lwz` swap** (`lwz r31` before `lwz r0`) — checked, it is the sole
  blocker for only 2 functions project-wide, so it is *not* worth a compiler
  patch.
- **Ghidra for the MISSING bucket.** 1483 functions have no implementation at
  all; `gh.sh` gives a usable starting point for each.
