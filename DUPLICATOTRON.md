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
| matched functions | 6491 / 10147 | 7190 / 10147 |
| fuzzy match | 57.343% | 62.680% |
| complete units | 195 / 543 | 195 / 543 |

## Where the remaining functions are

Classified with `tools/classify.py`. This counts the
2681 non-matching functions in units objdiff can compare; the project
total of 2957 also includes units with no diffable object at all.

| class | count | meaning |
|---|---|---|
| MISSING | 1029 | not written yet — no symbol in our object |
| OTHER | 799 | same count, different code |
| SIZE | 654 | different instruction count — real source difference |
| POOL | 144 | **identical code**, only anonymous `@NNN` literal/template pool composition differs |
| SCHED | 35 | identical instruction multiset, different order |
| REGS | 20 | identical mnemonics, different register numbers |

`POOL` functions are byte-identical apart from which anonymous constant they
reference; objdiff pairs anonymous symbols by ordinal position within a
section. Biggest clusters: `xFont` (13), `zEntCruiseBubble` (13), `xMath` (9),
`iMath3` (7), `zNPCGoalRobo` (7), `zNPCTypeVillager` (7), `zNPCSupport` (7),
`zNPCTypeKingJelly` (7), `zNPCSupplement` (7). See **Settled** below for why
this is not the cheap bucket it looks like.

86 units are within 3 functions of being complete — finishing those is the
fastest route to raising `complete_units`.

## Tooling

The tools that earned their keep now live in `tools/` and are documented in
`tools/README.md`: `solo.py` (compile+diff one unit without touching the
build), `symdump.py` (read a static table out of the target object),
`classify.py` (bucket the remaining functions by why they differ), `smoke.py`
(compile everything, report only failures), `flagsweep.py` (find a library's
real compiler flags).

Still scratch-only, because they are either superseded or too tied to this
machine to commit:

- `vary.py <src> <obj> <unit> <sym> <variants.py>` — patch a source snippet,
  rebuild just that object, report the percent, restore. Most REGS-class fixes
  here were found by feeding it 3-4 expression shapes.
- `pools.py <unit-frag>` — target vs ours data-symbol layout, for diagnosing
  POOL mismatches. `tools/symdump.py` covers most of what this was for.
- `gh.sh <out.c> <func>...` — Ghidra headless decompilation of the
  symbol-bearing `sbgcM.elf`, ~5s per batch, and the only source of a starting
  point for the MISSING bucket. Not committed because it hardcodes a local
  Ghidra install and project path. Use
  `ghidra_11.3.1_PUBLIC_20250219`, not `ghidra_11.3_DEV` (too old for the
  project file). Batch 10-20 names per invocation; one call costs the same as
  twenty.

## Patterns that keep working

- A float compared against a literal zero: `if (speed)` gives
  `fcmpu speed, 0.0` — `if (speed != 0.0f)` gives the operands the other way.
- Reusing a parameter as the destination (`f2 = tmp - f2;`) pins the result to
  that parameter's register.
- Collapsing two statements into one expression changes which temporaries are
  live at the same time, and therefore the register numbering.
- A helper whose return value is never used is usually `void` in the original;
  a non-void return forces the result into `f1`/`r3`.
- **The vtable pointer goes where the first `virtual` is declared.** CW does
  not force it to offset 0. `struct A { void* p; S32 n; virtual void f(); };`
  puts the vptr at 8, and a derived class stores its vtable there. If the
  target stores the vtable at 0, move the virtual declarations above the data
  members. This is also how to tell a *base* that has virtuals from a derived
  class that introduces them: adding `virtual` only to the derived one puts
  the vptr after the base's members.
- Container index parameters are **`u32` (`unsigned long`)**, not `U32`
  (`unsigned int`) — `CFUl` vs `CFUi` in the mangled name. This keeps coming
  up; check it before assuming a body is wrong.
- **`a <= b` on floats gives `cror eq,lt,eq` then `beq`; `!(a > b)` gives
  `ble`.** They are semantically identical and compile differently. Rewriting a
  condition under negation took Dutchman's `turning() const` from 91% to 98.9%.
- **Hoist a `const&` out of an if/else.** When the target computes an address
  *before* the branch and both arms use it, the original bound a reference
  first rather than indexing inside each arm. `const sound_asset& asset =
  sound_assets[which];` ahead of the `if` took `kill_sound` from 52% to 100%
  and `play_sound` from 3% to 96%.
- **CodeWarrior inlines a same-TU callee only if it is defined *earlier*.**
  So forward-declare the helper and put its body *after* the caller when the
  target emits a real `bl`. Conversely, a static helper defined above its only
  caller will be inlined whether you want it or not.
- **A table that is uninitialised in our source lands in `.bss`; the target
  has it in `.rodata`/`.data`.** Every relocation against it then mismatches,
  which can make a dozen unrelated functions look broken. `tools/symdump.py`
  and a real initialiser fix all of them at once.
- **Position in the file decides pool index.** Anonymous literals are
  allocated in codegen order, so a function sitting too early in the file
  steals the low pool indices from whatever should own them. Moving
  `register_tweaks` after `ParseINI` in Dutchman was worth a whole cluster.
  The corollary is the most productive move found so far: **write the missing
  pool-contributing functions in the target's source order.** In
  `zNPCSupplement` that realigned pool indices 0-36 and cascaded thirteen
  unrelated functions to 100% for free. Get the target's source order from its
  `.text` symbol order, and the authoritative pool contents from
  `tools/symdump.py` plus `dtk elf disasm`.

  **But first-use order is not the whole rule**, and this is the sharpest
  open problem in the project. `zThrown` is now the reduced test case: its
  pool has exactly the target's 22 entries with exactly the target's values,
  and differs by the position of **one object**, `0.5f`. Inserting a single
  unused `static F32 probe(F32 x) { return 0.5f * x; }` between
  `zThrown_Setup` and `zThrown_AddTempFrame` takes the unit from 18
  non-matching to **9** - nine functions flip at once. That is measured, not
  theorised, and it was removed again because shipping dead code to shift a
  pool is not decompiling.

### What is known about the missing construct

- `.sdata2` section order is ascending `@NNN` order - the compiler's object
  id, assigned at creation - in the target as well as ours. This is not a
  sorting question. It is: *what creates a literal before its first `.text`
  use?*
- **A function's new literals are always contiguous in our output.** In the
  target, `zFruit_Update` uses ids 842, 847 and 932. Non-contiguous, so it
  did not create `0.5f`; it reused one created elsewhere.
- **`fruitPattern`'s static-local suffix pins the boundary.** Ours is `$279`
  with body literals `@293/@294` - the static's id comes *first*. The
  target's is `$863` with body literals `@844/@845` - the static's id comes
  *last*. So `0.5f`, `0.0f`, `1.0f` and `1e-5f` were all created before
  `zFruit_ColorFade` was parsed at all.
- **Fingerprint.** The construct sits immediately after the `airTime`
  computation in `zThrown_Setup`, is compact (ids 1-2 apart), uses
  `0.5f, 0.0f, 1.0f, 1e-5f` in that order, and emits nothing -
  `zThrown_Setup` is byte-count-identical to ours and `AddTempFrame` matches
  100%.
- `zShrapnel` has the same shape (`0.5f` created early, used only late),
  which suggests one shared construct - a header inline or a debug/assert
  macro - rather than a per-unit accident.

Mechanisms ruled out by direct experiment (introduce a novel constant at the
top of a unit, consume it at the bottom, see where it lands;
`poolorder.py` in the scratchpad dumps any object's section symbols in
address order):

- an `inline`-keyword function defined early, called late - lands at the
  **call site**, not the definition; weak inlines compile after `.text`
- a `static` non-`inline` function defined early - lands at index 0, but it
  is also *emitted* there, so `.text` and pool order still agree
- a file-scope `static const F32`, a **default argument** value, and a
  **class static member function** - all land at the use site
- dead code: `F32 unused = 0.5f;` and `if (0) { ... 0.5f ... }` are folded
  before pool allocation

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
checkout clobbers the others' objects and `report.json`. `tools/solo.py`
removes the contention - it compiles a single unit into a private temp directory with the
exact flags from `build.ninja` and diffs that against the target object, ~2s,
writing no shared state. So N agents can share one checkout as long as:

- each owns a different `.cpp` (+ its own `.h`),
- nobody runs `ninja`, `configure.py`, or touches git,
- nobody edits a shared header - they report the change they want instead.

The integrating session runs the real build once at the end.

**Dispatch about two at a time, not eight.** Eight concurrent agents burned
through the session usage limit and all eight died mid-edit at the same
moment, leaving eight half-written units and nothing verified. They resume
cleanly from their transcripts, so it is recoverable, but a stalled fleet
still costs an integration cycle. Two at a time finishes the same work with
the failure surface of two.

An agent that dies mid-edit has left a file that may not compile. Resume it
with an explicit instruction to run `tools/solo.py` on its units and fix any
compile error **before** writing anything new.

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
own claim that a unit is clean** - re-run `tools/solo.py` and `clang-format -n
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

Note `tools/solo.py` parses `build.ninja`, which has two rule layouts: the source
file is on the same line as the `build` statement when it fits, on the next
line when it does not. The parser handles both; if you extend it, keep that.

## Priority: game code first

`src/SB/**` comes before library code (rwsdk, MSL, Dolphin SDK, bink,
MetroTRK), even though library code is in scope on this branch. Raw function
count makes rwsdk look like the biggest lever — it is not what makes the
decompilation worth anything. Library units are a fallback for when the game
units are saturated, or when one cheap enabling fix unblocks a whole bucket.

## rwsdk — 1039 functions, now reachable

Nothing here was imported from anywhere. `include/rwsdk/*.h` are upstream
files reconstructed from the BFBB PS2 DWARF data, and `configure.py` already
listed all 120 rwsdk units with `objdiff.json` targets. They were simply never
built: 118 of the 120 `.c` files did not exist, and with no source there is no
build rule, so `solo.py` could not touch them.

Two things were in the way, both now fixed:

- The headers were written for the C++ TUs that also include them — bare tag
  names used as types, `typedef struct X;` with no declarator, an empty
  struct, a member `operator=`, and quoted includes that only resolve with
  `-i include/rwsdk`. But the rwsdk objects compile `-lang=c`: their target
  symbols are unmangled. Every tag now has a self-typedef (structs/unions in a
  block near the top, enums immediately after their definition — C has no
  incomplete enum type), and the C++-only pieces are behind `__cplusplus`.
- Empty stub `.c` files exist for the other 118 units so `configure.py` emits
  their rules.

Verified with a compile of all 343 units the build knows about: 0 failures.

`ctbsp` is the worked example: 8 non-matching -> 4 in one pass, straight from
`gh.sh` output read against `rpcollbsptree.h`. The rwsdk headers are good
enough that struct offsets mostly just line up.

## Queued shared-header changes

Agents may not edit shared headers, so they report them instead. Outstanding:

- **`containers.h` — `static_queue<T>::iterator::operator-=` returns by value,
  not by pointer.** Two agents asked for this independently with the same
  evidence: the target's `__ami__` ends `bl operator+=; mr r4,r3;
  lwz r3,0(r3); lwz r4,4(r4); blr`, dereferencing and returning the 8-byte
  iterator in the r3/r4 pair. Ours returns the pointer and stops. Currently
  75%.
- **`containers.h` — `static_queue<T>::size()` is `(_last + (N + 1) - _first)
  & N`,** not `_last - _first`. Target: `addi r0,_last,0x20; subf r0,_first,r0;
  clrlwi r3,r0,27`. Currently 63.8%.
- **`containers.h` — `erase` should reuse `it._it` rather than caching
  `_first`.** REGS-class: identical instruction multiset, different registers,
  because the target's `add r4, r6, r4` reuses the register already holding
  `it._it` from the `cmplw`.
- **`xSnd.h` — declare `xSndPlay3DFade`.** Two units declare it locally. The
  signature is forced by the mangled name
  `xSndPlay3DFade__FUiffUiUiPC5xVec3ff14sound_categoryff`, though the meaning
  of the final two `F32` parameters is still a guess.
- **`zMovePoint.h` — add inline `RadiusArena()`, `NodeByIndex(S32)` and define
  `NumNodes()`.** The target emits all three out of line into
  `zNPCTypeRobot.o`. There is a stopgap `inline zMovePoint::NumNodes` sitting
  in `zNPCTypeRobot.h` that belongs here.
- **`zNPCHazard.h` — `UVAModelInfo::Valid` should be `const` and defined
  inline** (`return model && uv;`). The target symbol is
  `Valid__12UVAModelInfoCFv`, emitted per-TU; it is currently declared and
  defined nowhere, which is a live unresolved external.
- **`xShadow.h` — declare `gShadowObjectRadius`,
  `xShadowVertical_FillCache`, `xShadowVertical_DrawCache`,
  `xShadowReceiveShadowSetup` and `xShadowReceiveShadow`.** All are defined in
  `xShadow.cpp` and declared nowhere; `zNPCSupplement.cpp` carries a local
  prototype block as a workaround. Note `xShadowReceiveShadowSetup` must be
  declared returning **`U32`** — `xShadow.cpp` defines it `S32`, but the caller
  emits `cmplwi`, and the unsigned declaration is what took
  `NPCC_RenderProjTexture` to 100%.
- **`zNPCGoalStd.h` — `zNPCGoalAttackMonsoon::SpitCloud` takes `F32 dt`**
  (`SpitCloud__21zNPCGoalAttackMonsoonFf`), and
  `zNPCGoalAttackHammer` is missing `ShockwaveTests(xVec3*, F32)` and
  `FXStreakUpdate(xVec3*)` entirely. Four fully decoded functions are waiting
  on these three declarations.

### Rejected

- **`containers.h` — `tier_queue<T>::wrap_block` returning `u32`.** The
  evidence was real (it removes two `clrlwi` from
  `tier_queue<joint_data>::clear`) but measured in isolation it costs a
  different `xFX` function, net -1. Kept as `U8` until something explains both
  diffs at once. **Measure every requested header change on its own before
  believing it** — of four container changes requested with disassembly
  evidence, one was a regression and one was neutral.

## Open leads

- **Epilogue `lwz` swap** (`lwz r31` before `lwz r0`) — checked, it is the sole
  blocker for only 2 functions project-wide, so it is *not* worth a compiler
  patch.
- **Ghidra for the MISSING bucket.** 1483 functions have no implementation at
  all; `gh.sh` gives a usable starting point for each.
