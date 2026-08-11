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
| matched functions | 6491 / 10147 | 7678 / 10147 |
| fuzzy match | 57.343% | 73.512% |
| complete units | 195 / 543 | 223 / 543 |
| **game code fuzzy** | — | **91.074%** (6520 / 7673) |

Game code is tracked separately because it is the part that is actually
being worked, and the project figure understates it badly: Renderware and
Bink contribute 1375 functions at 6.6% and 2.4% and have never been
touched. By category: SDK 99.484%, **game 91.074%**, MSL 79.045%,
Renderware 6.629%, Bink 2.371%.

Note the gap between game fuzzy (91.07%) and game `matched_code`
(~55%). That is the near-miss effect, and a large and growing share of it
is *known unfixable* rather than pending: ghost-literal pool
displacements cap ~30 functions in `zEntCruiseBubble`, ~27 in
`zNPCTypeBossPatrick` and 11 in `zNPCFXCinematic` (that last one priced
exactly, with a throwaway probe that was measured and then removed).
Do not read the remaining 9% as 9% of remaining work.

Merged `bfbbdecomp/main` (d226f0ae..24d388c4) at `297ce59f`.

See **PCPORT.md** for the PC-port follow-up plan, and specifically for why the
port's gate is "all of `src/SB/**` written and correct" rather than this table.

**`report.json` credits near-misses, so it understates stub work.** The xFX
wave moved seven functions from ~0.5% to 72-100% and promoted six neighbours
to exact, and `report.json` recorded **+1** for the unit — because it was
already counting those six at 99.3-99.9%, while the newly-filled bodies at
72-96% are still below its bar. Same for zEntPlayer: six stubs filled, +3
recorded. Judge stub waves by `solo.py` non-matching counts; `report.json`
only sees the crossings.

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
reference. Biggest clusters: `xFont` (13), `zEntCruiseBubble` (13), `xMath` (9),
`iMath3` (7), `zNPCGoalRobo` (7), `zNPCTypeVillager` (7), `zNPCSupport` (7),
`zNPCTypeKingJelly` (7), `zNPCSupplement` (7). See **Settled** below for why
this is not the cheap bucket it looks like.

**objdiff compares relocations by target offset, not by symbol name.** This
corrects the earlier assumption that anonymous names are cosmetic.
`sStripVert$2188` vs `sStripVert_2188` is *not* flagged — the name differs but
the target is the same object. `@958@sda21` vs `@256@sda21` *is* flagged,
because those two literals sit at different `.sdata2` offsets. So what matters
is pool **layout**, not pool numbering, and layout is something source order
can actually control.

That makes POOL partially reachable, and it cuts both ways within a file.
Writing `DrawRing` at the target's position in `xFX.cpp` seeded `.sdata2` with
the target's first nine constants in the target's own order, and six unrelated
neighbours went from 99.3-99.9% to exact as a side effect. The inverse is the
standing hazard: one unwritten function that owns an early pool slot shifts
everything after it. `xFXRenderProximityFade` owns slot 0x2C (255.0f), so
nothing in `xFX` can align past 0x28 until it exists — which is the current
ceiling on four other functions in that unit.

Practical consequence: when a unit has several near-100% functions and one
large unwritten function early in the file, write the big one first. Filling
small stubs around it just re-shuffles a pool that is going to move again.

86 units are within 3 functions of being complete — finishing those is the
fastest route to raising `complete_units`.

## Tooling

The tools that earned their keep now live in `tools/` and are documented in
`tools/README.md`: `solo.py` (compile+diff one unit without touching the
build), `symdump.py` (read a static table out of the target object),
`classify.py` (bucket the remaining functions by why they differ), `smoke.py`
(compile everything, report only failures), `flagsweep.py` (find a library's
real compiler flags).

**`tools/solo.py` counts differ from `report.json` counts, and both are
right.** For `zNPCTypeRobot`, `solo.py` reports 129 non-matching where
`report.json` reports 86 — but running `objdiff-cli` directly on the object
`ninja` built gives 129 as well. `solo.py` is faithful to the build; dtk's
report counts matched functions by a different rule. So never compare a
`solo.py` number against a `report.json` number. Use `solo.py` for
before/after *within* a unit, and `report.json` as the project metric.

Still scratch-only, because they are either superseded or too tied to this
machine to commit:

- `vary.py <src> <obj> <unit> <sym> <variants.py>` — patch a source snippet,
  rebuild just that object, report the percent, restore. Most REGS-class fixes
  here were found by feeding it 3-4 expression shapes.
- `pools.py <unit-frag>` — target vs ours data-symbol layout, for diagnosing
  POOL mismatches. `tools/symdump.py` covers most of what this was for.
- `candidate.py <unit-frag> <src-path> <cand1> <cand2>` — swap each candidate
  version of a source file into the tree, compile that one unit, and report
  exact-match counts plus which functions are exact in only one of them.
  Always restores the original. Written for merge-conflict resolution: it
  answers "is upstream's version of this file better than ours" with a number
  instead of an opinion. See "Merging upstream" below.
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

**Seen in three units now.** `zThrown` (`0.5f`), `zShrapnel` (`0.5f`), and
`zNPCTypeRobot`, whose `.rodata` opens with **eleven zero-filled objects we do
not produce**, ten of which have no relocation pointing at them from any
section. Inserting eleven dummies proves they are worth exactly 11 functions.
Dead constants created early and referenced late or never is the recurring
shape, and it is unlikely to be three separate accidents.

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
- a member declared in a class body in a header, and an out-of-class `inline`
  member defined in a header - both land at the end of `.sdata2`
- **section splitting.** The target objects have many `.text` sections (18 for
  `zEntCruiseBubble`) where ours have one, which looked like the answer: weak
  inlines in their own COMDATs would explain early literal ids with no
  main-`.text` presence. Sweeping 32 flags found the mechanism — **`-sym on`
  produces 12 `.text` sections** — but it changes no literal ordering and
  yields **zero** additional matches on `zEntCruiseBubble`, `zThrown` or
  `zNPCTypeRobot`. Section layout and pool allocation are independent.

**Unverified, from the `zEntCruiseBubble` agent, and it contradicts what is
written above:** that objdiff pairs anonymous symbols **by name when the
`@NNN` ids coincide**, not purely by ordinal. Its evidence is that
`state_player_halt::update` reaches 100% because our `0.0001f` happens to get
id `@1721`, matching the target's, and that adding an unrelated function which
shifts our id numbering knocked it straight back to 99.904%. If true, aligning
the *count* of preceding objects matters as much as their order. Worth
confirming before anyone builds a strategy on either model.

## Settled

- **An aggregate initialiser in an inline function seeds a junk `.sbss2`
  object -- this was the anonymous-literal mystery.** CodeWarrior creates an
  anonymous 4-byte all-zero object at *parse* time for an aggregate
  initialiser inside an `inline` function, in every TU that includes the
  header, whether or not the function is ever instantiated. Minimal repro
  under the real flags: `inline grid_index f(U16 a, U16 b) { grid_index i =
  {a, b}; return i; }` emits `@1 4 bytes .sbss2`; rewritten as member
  assignments it emits nothing. `xGrid.h`'s `get_grid_index` was doing this,
  which is where `@148` in `zDispatcher` and `@150` in `xModelBucket` came
  from, and it shifted every later `.sbss2` operand by four bytes in about
  140 units. Fixed in commit 134129c2.

  **But the shape is not wrong everywhere -- measure, do not pattern-match.**
  The only two other instances in the tree, `zNPCTypeDutchman.h`'s
  `xVec2 facing = {0,0}` and `xLaserBolt.h`'s `xVec3 temp = {0,0,0}`, are
  both correct: removing them costs `xLaserBolt` a matched function and drops
  `zNPCTypeDutchman`'s fuzzy. In those two the original really did write an
  aggregate initialiser. Both were measured on a full build and reverted.

  **Correction: retail's `get_grid_index` did have the initialiser too.** The
  claim previously recorded here -- that `xGrid.h` was the one case where the
  original did not write one -- is refuted by the target itself. Disassemble
  `get_grid_index__FRC5xGridff` and the first thing it does is
  `lwz r5, @587@sda21` / `stw r5, 0x8(r1)`, filling the whole 4-byte
  `grid_index` local from a constant before either field is assigned, and
  `@587` is `.sbss2:0x803D0818`, `size:0x4`, `scope:local` -- precisely the
  junk object. So removing the initialiser was still the right call on the
  numbers (it was worth ~140 units), but the reason cannot be "retail did not
  write one". Something else about our header's reach differs: retail emitted
  that object in *fewer* TUs than our `xGrid.h` does, so keeping it added four
  bytes where retail had none.

  The consequence for anyone in `xScene`: `get_grid_index` sits at **60.558%**
  and **cannot cross 99%** while the initialiser is deliberately absent -- the
  missing `lwz`/`stw` pair alone is more than 1% of a 43-instruction function.
  Treat it as a priced ceiling, not a lead. There is a second, independent
  defect in it that *is* reachable if anyone wants the fuzzy: retail computes
  both products before the first `bl` and parks the z product in `f31`, where
  we park the raw `z` argument and recompute after the call. Hoisting both
  `(v - min) * inv_csize` products into locals should recover it -- but it is
  a shared header with a ~140-unit blast radius and it buys no function, so
  measure the sweep before spending the time.

  This is the general answer to "what creates a literal before its first
  `.text` use", and it is worth checking other headers for the same shape
  before assuming a pool ordering is unreachable. `iMath3` and `iScrFX` are
  both blocked on exactly that: their target pools are seeded with constants
  that no function in the `.cpp` materialises, and their anonymous indices
  start around 555 and 527 against our 68 and 37, i.e. the original created
  far more anonymous objects during header parsing than we do.
- **`complete_units` is a `configure.py` marker, and `report.json` cannot
  tell you when to set it.** A unit reaching 100% in `report.json` does not
  raise `complete_units`; that number counts `Object(Matching, ...)` entries
  (`Matching = True`, `NonMatching = False`, `Equivalent = config.non_matching`,
  configure.py:401). Flipping a marker makes dtk link *our* object instead of
  the extracted original, so it is only safe when our object would link to
  the same bytes -- and `report.json` scores things 100% that would not. It
  called `zDispatcher` 23/23 with 100% matched data while the built object's
  `.data` was 92 bytes against the target's 96.

  The reliable test is a real link, and it is cheap: the objects are already
  built, so flipping one marker and running `ninja` costs about nine seconds.
  `tools/fliptest.py --test` flips each candidate on its own and reports
  PASS/FAIL; `tools/symorder.py` explains the failures. Do not bisect --
  test every candidate singly, because the failures are independent.

  The trap worth knowing: **objdiff matches symbols by name, so it is blind to
  function order**, while the linker lays functions out in definition order.
  Two units scored a flat 100% on every symbol and still moved the DOL.
  `abort_exit.c` defined `abort` before `exit` and the target has `exit`
  first -- swapping the definitions was the entire fix. `mem.c`'s `.text` was
  the *exact reverse* of ours, which is the `-inline deferred` signature; the
  earlier whole-tree flag sweep could not have found it, because reversing
  the order does not move the objdiff percent. It is worth re-checking other
  units for that signature.
- **Compiler patch clause C — +41 on its own, and it kills the zThrown float
  meme.** Dispatch entries 1 and 3 (whole object vs subrange, both operand
  orders) only tested "same base object", so a load of a small global or a
  float literal hoisted across a store to a *different* small global —
  `stfs c_fruit` vs `lfs globals.throwHeight` in `zThrown_Setup`. Clause C
  answers may-alias for those entries iff both base expressions have word 0
  == 5, both sizes are <= 4, the opcodes differ, and both instructions are
  plain loads/stores. Word 0 == 5 is the **static-storage gate**: frame and
  stack objects carry `0x00010005` and are excluded. That gate is the whole
  safety property — ungated, the same predicate pins integer-conversion stack
  traffic (`stw` frame slot vs `lfd` magic double) and costs 50 exact
  functions (+84/-50). Full details, including which variants were measured
  and rejected, are in the `tools/patch_compiler.py` docstring and commit
  message. `zThrown_Setup` 85.50% -> 99.35%; the residue is an r6/r7/r8
  allocation permutation, i.e. REGS, not SCHED. Three sub-100 functions
  wiggle down (`NPAR_TubeSpiralMagic` 98.9 -> 81.8, `VFXSmokeStack`
  83.4 -> 77.6, `zEntPlayerTSlideUpdate` 94.4 -> 94.2) against ~84 that
  improve.
- **Clause C had to be taught about volatile — +11 more.** A volatile access
  sets instruction flag bit `0x80`, so the plain-load/store test `flags & ~6
  == 0` rejected every volatile reference before it reached the clause. That
  is why `zMenu`, whose timers are `static volatile F32`, kept hoisting a
  literal across a store to a different small static *with* clause C
  installed. Widening the mask to `~0x86` and adding the clause to entry 0 as
  well, both behind the static-storage gate, measures 7279 -> 7290 with zero
  units regressed and the DOL sha1 unchanged. Widening entry 0's *clause A*
  the same way is not safe — it pins volatile frame locals and measures -4.
  Patched compiler sha1 is now
  `7d3ff244fb371e3b15b0becd41ac04b627869ae8`.
- **Clause D is dead — do not refit it.** A directional rule (an `stfs` to a
  declared frame local may not be crossed by a *later* small static load,
  entry 3 only) hits exactly the motion four otherwise-finished functions
  need, and measures +22 functions to 100% against 18 whose percent drops.
  The gain and loss populations are indistinguishable in every field the
  predicate can see — same opcodes, sizes, overlapping offsets, same storage
  classes, same base-expression words. stfs-only, the declared-local gate,
  entry-3-only, offset thresholds and a literal-only static side were all
  measured; none separates them. Recovering the losses from the *source* side
  was then tried on four of them (`dampen_velocity`, `BoundAsRadius`,
  `get_texture_size`, `xBoxFromCircle`): statement reordering, operand swaps,
  binding the literal to a local, and initializer restructuring all left the
  percent unchanged. The instruction *set* already matches; only the order
  differs, and with the edge added the list schedule is deterministic, so
  source shape has no purchase on it.
- **Edge latency is not the lever either — the zero-latency lead is
  refuted.** The suspicion was that clause D's drops came not from adding the
  alias edge but from the edge carrying normal store-to-load latency, where
  retail's placement looked like what a zero-latency edge would produce. The
  mechanism is real and now fully read out of the binary. `0x5084f0` is the
  edge builder, `cdecl(from, to, flag)`: `flag != 0` takes the latency from
  `word[from+0x10]` and, when bit 0 of the to-side access flags is set, adds
  `word[[0x5e0850]+8]` from the machine model; `flag == 0` gives latency 0.
  It has ten call sites; exactly three are the may-alias sites (`0x5081fd`,
  `0x508376`, `0x5083ab`, each `call 0x511fc0` / `test al,al` / `je` /
  `push 1`), and the barrier, volatile and branch chains already pass 0. The
  same call sites confirm operand A is the earlier instruction
  (`push [later]+0xc` then `push [current]+0xc`). Measured against the
  installed build: zeroing only the new clause-E edges takes it from
  +20/-17 to +10/-20; zeroing the shipped clauses A/B/C costs 108 functions;
  zeroing every alias edge costs ~1000. And the premise is simply false —
  `xFont.o` and `zNPCTypeCommon.o` come out byte-identical either way,
  because an `stfs`'s `word[insn+0x10]` is already 0, so those edges never
  carried latency to begin with. Nothing to install.
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

## The STUB bucket

Found by `tools/stubs.py`. A **stub** is a function whose symbol exists in
both the target and our object but whose body in our source is an empty `{ }`
placeholder, so our object has a bare `blr`. This is a distinct bucket from
MISSING (no symbol at all) and from the near-100% classes (real code, wrong
details), and it was not being tracked at all until now.

Started at **74 functions, 37640 bytes, across 20 units**; down to **52
functions, 32116 bytes, across 15 units** after the first wave and the
24d388c4 merge. These are among the cheapest work left: the target
disassembly is complete and readable, and because the symbol already exists
and is already correctly placed in `.text`, you are filling in a body rather
than deciding where a definition goes.

Current state — re-run `python tools/stubs.py` rather than trusting this table:

| unit | stubs | bytes |
|---|---|---|
| `zNPCFXCinematic` | 20 | 8200 |
| `xFX` | 10 | 8512 |
| `zEntPlayer` | 6 | 3704 |
| `xCollide` | 3 | 200 |
| `zMain`, `xParCmd` | 2 each | 2136 / 1112 |
| `zLasso`, `xCutscene`, `zNPCTypeKingJelly`, `xSnd`, `xLaserBolt`, `iFX`, `xParEmitterType`, `xHudFontMeter`, `xHud` | 1 each | 4048 / 2444 / 648 / 352 / 340 / 164 / 108 / 88 / 60 |

Note the two singletons with outsized bodies: `zLasso_Render` at 4048 bytes
and `xCutscene_Render` at 2444 are each worth more code than most whole units
in the table.

**Look for a matched sibling before reading any stub's asm.** Several stubs are
near-copies of a function already written in the same file, and the sibling is
a far better starting point than the disassembly:

- `xQuickCullForRay`/`xQuickCullForBox` in `xCollide.cpp` are the same
  one-line forwarder as `xQuickCullForBound` in `xBound.cpp:399` —
  `xQuickCullForX(&xqc_def_ctrl, q, x)`. Both went straight to 100%.
- `xHudFontMeter::load` is the `init_base` + placement-new idiom shared by
  `xHudText`, `xHudModel` and `xHudUnitMeter`. Its comment claimed it was
  stubbed because the real body "caused a build failure" — the actual cause
  was that the definition named its third parameter `size_t` (of type `u32`),
  shadowing the type so `new` would not parse. Renaming it fixed the build and
  the function matched 100% immediately. **Treat "this does not compile"
  comments as unverified.**
- `xParCmd_AlphaInOut_Update` is `xParCmd_SizeInOut_Update` with `custAlpha`
  for `custSize`, writing `p->m_cfl[3]` and then `p->m_c[3] = (U8)p->m_cfl[3]`.
  0.990% -> 87.525% from the sibling alone.

`xParCmd_SizeInOut_Update` (90.542%) and `_AlphaInOut_Update` (87.525%) now
share the same two residuals, both already flagged in the SizeInOut source: the
clamp is not the `CLAMP` macro, and `0.33333334f` is cached before the loop
rather than reloaded. Crack that idiom once and both functions move —
`xParCmd_Shaper_Update` (708b, still a stub) reuses it a third time.

Worked so far: the `init_sound`/`play_sound`/`kill_sound` family across
Plankton, SB2 and Prawn (the same idiom in three files — crack one and the
rest follow), Ambient's Jelly/Neptune group, and `xFont`'s four `parse_tag`
colour channels.

Still open and worth taking in one pass because the members are siblings:
`xFont`'s remaining group, `zNPCFXCinematic` (20 stubs, and note
`zNPCB_SB2::singleton` and `zNPCFXCutscenePickTable` become available as a
by-product — `g_cutmap` does not exist in our source at all), and `xSnd`'s
entire fader subsystem (`xSndStopFade` is a stub; `update_faders`,
`fade_data::operator=` and `xSndPlay3DFade` are all at 0%, and there is no
`faders[]` array in the source — it needs `fade_data` moved above line 130).

Two cautions learned filling these:

- A filled body can emit new anonymous literals that shift *other* functions.
  Implementing `zNPCNeptune::AnimPick` in place added two `@12` `.rodata`
  entries ahead of `PlayWithAnimSpd`'s array and knocked it off 100%. Moving
  the definition to the target's position fixed it. Run `symorder.py` after
  every stub.
- Filling a stub in a unit whose pool is already misaligned gets you
  instruction-identical code that still does not reach 100% under `solo.py`.
  All four `xFont` `parse_tag` functions are in that state, as is the
  pre-existing correct `parse_tag_yspace`.

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

## dwarf/ — the resource this project has been under-using

`dwarf/` is **already tracked in the repo**: 231 files, 32 MB, essentially 1:1
with the source tree (110 game `.cpp` against 110 in `src/SB/Game`, 89 against
88 in `src/SB/Core/x`). It is DWARF-derived source from the **PS2** build, and
until now it was referenced in this document exactly once, in passing, about
rwsdk headers. It should be the second thing you open after the asm diff.

For every function it gives the full signature, every local by name, and the
register or stack slot each one occupied:

```
void CollideReview(class zNPCRobot * this /* r21 */) {
        class zNPCGoalCommon * goal;   // r2
        signed int goaldidit;          // r16
        class xEntCollis * npccol;     // r20
        class xVec3 vec_depen;         // r29+0x90
        float goodep;                  // r29+0x9C
```

Three things transfer usefully to the GameCube build:

- **Local declaration order**, which controls stack layout under CW as much as
  under MW MIPS. Reordering three locals in `NPCMessage` from the DWARF order
  took it 99.016% -> **100%**, and the same trick fixed `CornerOfArena` and
  `zNPCSlick::AnimPick`.
- **Signatures for functions declared nowhere in our tree.** This is the big
  one: the zNPCTypeRobot pass stalled on 16 member functions with no
  declaration anywhere, and DWARF has most of them (`VFXStarTrek`,
  `NightLightUVStep`, `SnoreNZeez` matched the asm-derived signatures exactly).
  Recovering a header declaration from DWARF beats guessing it.
- **Local *names***, which make a reconstructed body readable and reviewable
  instead of `iVar1`/`uVar5`.

**A DWARF definition omits parameters that were unnamed or unused, and this
will silently corrupt a rename pass.** The same file carries both forms — the
full declaration near the top, and the definition further down with only the
named parameters:

```
line   12  signed int zGustEventCB(class xBase *, class xBase *, unsigned int, float *, class xBase *);
line 1418  signed int zGustEventCB(class xBase * to /* r2 */, unsigned int toEvent /* r2 */) {
```

Our signature is `(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
xBase* b)` and the **declaration agrees with it exactly**. Match names against
the *definition* positionally and you rename our `from` to `to` and our `to` to
`toEvent`. Nothing downstream catches it: renames are byte-neutral, so
`solo.py` stays identical and the DOL still hashes. **Always resolve a
short parameter list against the declaration, never against the definition.**
`tools/dwarfaudit.py` now refuses to compare across an arity mismatch and
reports those functions under `ARITY` instead.

The corollary is that most apparent "signature divergences" are not divergences
at all — they are unused parameters being dropped. `RepelBowlBall` and
`ConeOfRange` showing no parameters is this effect, not a PS2/GC difference.

**Register numbers are MIPS and do not transfer at all.** Genuine PS2/GC
divergence does exist, so the asm still overrides DWARF — but check the
declaration before concluding you have found one.

### Reordering locals to dwarf order: tried, mostly does not work

Worth recording as a negative result so nobody re-runs it. `tools/dwarforder.py`
ranks candidates; two agent waves worked **39 functions** between them.

Yield: **three real improvements** — `HAZ_Iterate` 98.636% -> 99.848%,
`zThrownCollide_ThrowFruit` 97.342% -> 97.650%, `TurnThemHeads` 96.584% ->
96.619% — plus about ten reorders kept at zero cost because they now match the
original order without changing codegen. Everything else reverted or was
unachievable, and several regressed hard: `ZNPC_AnimTable_Tubelet` 99.583 ->
86.042, `render_closeup` 99.868 -> 90.198, `NCIN_OilHazard` 96.923 -> 89.650.

Why it underperforms, in order of importance:

1. **Scope, not order, is the real blocker.** dwarf flattens block-scoped
   locals into one list per block, so reaching its order usually means hoisting
   variables out of the `if`/`for` blocks they live in. That is a code change,
   not a reorder, and it was the reason for most skips.
2. **A near-100% function is usually wrong for some other reason**, and
   disturbing a working stack layout makes it worse. Reordering is only
   plausible when stack layout is the *last* remaining defect.
3. **Statics are not declaration-ordered.** See below.

The premise is still sound, which is why the failures are worth understanding
rather than dismissing: of functions we already match byte-for-byte, **85%
already agree with dwarf's order** (367 of 433). dwarf order really is the
original source order. It just is not usually the thing standing between a
function and 100%.

### Function-scope statics are listed in DESCENDING address order

Not declaration order. `zEntCruiseBubble::init_states` is twelve statics
annotated `// @ 0x005CB880` and interleaved with compiler `@8149` init-guard
flags; sorted ascending by address they come out in exactly the order our
source already had. Reordering to dwarf's listing cost 99.161% -> 94.699%.

`tools/dwarforder.py` therefore matches only entries annotated with a register
or stack slot (`// r18`, `// r29+0x90`). That alone removed a third of the
worklist, 50 candidates down to 32 at >=95%.

Checking our static order against ascending-address order is *also* not a
lever: 70% already agree, and the disagreements are mostly an artifact of
comparing across sections (`.bss` at `0x50FExxx` versus `.data` at
`0x5CBxxxx`), where relative address says nothing about declaration order.

One case is worth knowing about because it looks like a missed win and is not.
Reordering `DoWallJumpCheck`'s three statics to dwarf's *descending* listing
flipped a neighbour, `PlayerCollCheckEnv`, to 100%. But ascending-address order
for those three is `sAtdist, sSweptrad, sVerticalCos`, which is already exactly
what our source has — so the reorder moved us *away* from the original source
order and the flip was coincidental. Almost certainly it compensated for a
`.bss` layout error elsewhere in the unit rather than fixing anything. Treat a
percentage win from a change you know to be less faithful as a symptom, not a
fix; the real bug is whatever made the compensation work.

**Best combination found so far**, and worth assembling up front for any future
bulk pass: Ghidra output for control flow and call graph, plus a dump of the
target's `.sdata2`/`.rodata` decoded as floats/RGBA to resolve `@NNNN` literals,
plus `dwarf/` for local names and declaration order. Ghidra alone gets to
roughly 85-90%; the last ten points came almost entirely from the other two.

## Bulk Ghidra: what it can and cannot do

Measured, not estimated. `tools/ghidra/DumpFuncs.java` plus the local
`analyzeHeadless` wrapper dumped **every game function currently at 0.000%
fuzzy — 597 unique names across 38 units, 278 KB of code — in 73 seconds, with
0 decompile failures and 0 not-found.** Extraction is emphatically not the
bottleneck. Per-unit corpora land in `scratchpad/ghidra/<unit>.c`.

**It is safe to attempt.** A unit containing a 0%-fuzzy function cannot be
byte-identical, so none of those 38 units are `Matching` — verified, 0 of 38.
Bulk-filling them **cannot break the DOL sha1**. The only exposure is pool
shifts knocking neighbours off 100% inside those same units, which `solo.py`
before/after catches.

**What the output is actually like**, over the 100-block zNPCTypeRobot sample:

| trait | share | meaning |
|---|---|---|
| decompile failures | 0% | control flow always recovered |
| `halt_baddata`, unrecovered jumptables | 0% | no dead ends |
| correct mangled callee names | ~all | the symbol-bearing ELF earning its keep |
| `undefined*` types present | 52% | needs retyping against our headers |
| raw offset derefs `*(int *)(p + 0x228)` | 30% | needs mapping to struct members |
| `goto`/`LAB_` | 5% | |
| bogus `undefined8 param_1..9` signature | 5% | C++ `this` misplaced by ABI misread |
| `extraout_*` artifacts | 1% | garbage, e.g. `param_1 = extraout_f1;` |

So: **the algorithm and call graph come out right; compilable C++ does not.**
Even the cleanest cases need work — `xMat3x3RMulVec` decompiles perfectly but as
`(float *param_1, float *param_2, float *param_3)` with `param_2[5]` indexing,
where we need `xMat3x3*` and named members. Effectively **0% compile as-is.**

The correct use is therefore **Ghidra output as agent input, not as committed
code.** Agents previously started from raw PPC asm; starting from recovered
control flow with real callee names is a large accelerant. It is not auto-fill,
and anything from it needs the same measurement discipline as hand-written code.

Two traps worth keeping:

**Never pass function names as command-line arguments on Windows.** Mangled C++
names can contain `<` and `>` — `xUtil_choose<i>__FPCiiPCf` is real and lives in
this project. cmd.exe reads them as redirection and the entire `analyzeHeadless`
invocation dies **silently, in 0.2 s, with no error output**. `DumpFuncs.java`
takes a list file for exactly this reason.

**Some symbols have several copies in the ELF.** 597 requested names produced
650 blocks. Duplicated symbols are annotated in the per-unit corpora; check the
address matches the unit before trusting a copy.

## Shared-header changes: the xSndPlay3D case

Settled, and the method generalises. The 9-arg `xSndPlay3D(const xVec3*, ...)`
is now `inline` in `zEnt.h`, with two TUs opting out via
`#define XSNDPLAY3D_OUT_OF_LINE` before the include. Result: **+12 exact
(zFX +10, zEntTeleportBox +2), nothing lost, `complete_units` unchanged.**

Getting there killed three assumptions worth writing down.

**1. objdiff being blind to definition order can hide a broken `Matching`
unit.** The naive change — mark it `inline`, delete the body from `zEnt.cpp` —
built a `main.dol` that failed its sha1. Not because of the unit I expected:
`zEnt.cpp` is `Matching` too, and deleting the out-of-line body *relocates the
symbol inside `zEnt.o`*. objdiff pairs symbols by name, so it still cheerfully
reported zEnt at 38/38 while the object was no longer byte-identical. **A unit
reading 100% in objdiff is not proof its object is byte-exact.** For `Matching`
units, only the DOL sha1 is proof.

**2. A `solo.py` gain can be worth exactly zero on the project metric.** That
same change measured net +7 exact and showed **+0 matched functions, −1
complete unit** in `report.json` — every one of the +7 was a 99.x% -> 100%
crossing that report.json already counted. Before trading a complete unit for
exactness, check whether report.json can even see the gain.

**3. `WEAK` is not a substitute for `inline` here.** Defining the body `WEAK`
in the header (the convention this codebase uses elsewhere in `zEnt.h`) emits a
weak copy into every TU that includes it: **net −217 exact, 340 functions out
of exact across 24 units.** Catastrophic. Do not reach for it.

**The pattern worth reusing:** retail inlined a given helper into some callers
and not others, so a single global choice is wrong either way. Define the
inline in the header by default and let the TUs that must not expand it opt out
with a macro. Finding which TUs those are is mechanical — snapshot every caller
with `snapshot.py`, apply the change, snapshot again, and read the drop list.

`XSNDPLAY3D_OUT_OF_LINE` is currently set by four TUs: `zEnt.cpp` (layout of a
`Matching` unit), `zEntDestructObj.cpp` (pool shift in a `Matching` unit),
`zLasso.cpp` and `zPlatform.cpp` (pool shift, both `NonMatching`).

**4. Comparing exact-match sets is not enough, and this cost real accuracy.**
The first version of `snapshot.py --cmp` only diffed which functions were at
100%. It reported the change as clean. It was not: the inline interned a `0.25f`
literal in `zLasso` and `zPlatform`, dropping their `.sdata2` match from
65.217% to 63.830% and 92.063% to 90.625%. No function crossed the 100%
boundary, so nothing showed up. A later agent working `zLasso` found it
independently and applied the opt-out; `zPlatform` was only caught by re-running
the comparison over the **full percentage distribution**. `snapshot.py --cmp`
now always prints an `ANY DROP` section covering sub-100% functions and data
symbols. Read it.

`snapshot.py <out.json> <src>...` / `snapshot.py --cmp <before> <after>`
(scratch) does that sweep: compiles each unit privately, records every symbol's
percentage, and diffs two snapshots into per-unit GAIN/LOST lists with a net.
It replaces the old `shadowhdr.py`, which could not measure any TU including
`<new.h>` because `-cwd explicit` breaks that include chain.

## Merging upstream

This branch tracks `bfbbdecomp/main` but never merges back. Upstream keeps
decompiling the same functions we do, so most conflicts are two independent
implementations of one function — not something a 3-way merge can judge.

**Resolve by measurement, not by reading.** Extract both sides and compare
exact-match counts:

```
git show :2:<path> > ours.cpp     # stage 2 = HEAD (ours)
git show :3:<path> > theirs.cpp   # stage 3 = upstream (theirs)
python candidate.py <unit-frag> <path> ours.cpp theirs.cpp
```

The "exact only in X" lists are the part that matters. A side can lose 10-for-1
overall and still hold the one function the other lacks — take the bulk winner,
then port the individual wins across.

From the 24d388c4 merge, all seven conflicts:

| file | resolution | evidence |
|---|---|---|
| `xShadow.cpp` | ours | ours 46 exact, theirs 28, **zero** exact-only-in-theirs |
| `zFX.cpp` | ours + 1 port | ours 63, theirs 53; theirs held `zFX_SpawnBubbleTrail` |
| `zEntTeleportBox.cpp` | ours | dead tie 32/32, nothing exact-only either way |
| `xHudFontMeter.cpp` | ours | their only hunk (a `const`) already in ours |
| `zNPCTypeRobot.cpp` | ours | their only hunk (a float literal) already in ours |
| `xShadow.h` | ours | cosmetic param rename, ours a superset |
| `zNPCHazard.h` | **both** | their `const` + our added declaration |

Two traps worth knowing:

**A one-hunk upstream diff can produce a huge conflict.** `zNPCTypeRobot.cpp`
showed ~20 conflicting lines for what was a single changed float literal. We
had relocated `zNPCSleepy_Timestep` for definition-order matching, and the
merge could not align the moved block. Always diff `<merge-base>..upstream` for
the file before judging the conflict — the real change is usually tiny, and
often something we already have.

**Upstream's side can carry code that is dead in ours.** Their
`zNPCTypeRobot.cpp` hunk included `extern char stringBase[];`, live upstream
but dead here because we replaced every `stringBase + 0xNN` with real string
literals. Check whether a symbol is actually referenced before preserving it.

The one genuine win in that merge was a bug: our 2-arg
`zFX_SpawnBubbleTrail` passed `&bubblehit_pos_rnd` / `&bubblehit_vel_rnd`,
copy-pasted from `zFX_SpawnBubbleHit` directly above it. Upstream had the
correct `&bubbletrail_*`. Worth reading their version of anything we already
"finished" — they catch things.

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

- ~~**`zEnt.h` — the 9-arg `xSndPlay3D`.**~~ **DONE**, all 27 callers measured.
  Landed as an `inline` in the header with `XSNDPLAY3D_OUT_OF_LINE` opt-outs in
  `zEnt.cpp` and `zEntDestructObj.cpp`: +12 exact, nothing lost, no unit
  flipped. See "Shared-header changes: the xSndPlay3D case" above — the
  original -1-complete-unit framing was wrong in both directions, and the three
  assumptions it broke are the reusable part.
- **`xDebug.h` — no `xVec3*` overload of `xDebugAddTweak`.** There are `F32*`,
  `S16*`, `U8*` and `const char*` ones. `zNPCTypePrawn.cpp` currently carries
  a file-scope declaration instead, which is the same workaround Dutchman
  already uses. The header version is **unvalidated** — nobody has measured
  the collateral on everything that includes `xDebug.h`.

- ~~**`containers.h` — three container fixes.**~~ **ALL DONE.** The first two
  landed in `65afa1ba` on 2026-08-04 and sat in this queue for a week after
  they had shipped; `operator-=` and the wrap-`size()` both measure 100% today.
  Note the size() one was **`fixed_queue`, not `static_queue`** —
  `static_queue::size()` is an 8-byte `lwz`/`blr` and was never the subject.

  **The third was landed on a misreading, and the misreading is the lesson.**
  The evidence recorded here said the target's `add r4, r6, r4` "reuses the
  register already holding `it._it`". It does not: for
  `erase(const iterator&, const iterator&)` the argument mapping is
  `r3=this, r4=&it, r5=&other`, so `lwz r6, 0x0(r3)` loads `this->_first`
  (offset 0), and the `add` sums **`_first`**, not `it._it`. `65afa1ba`
  rewrote the source to `it._it` on that basis and recorded "no measured
  gain" — the two spellings are value-equivalent inside the
  `it._it == _first` branch, so nothing caught it. Corrected to `_first` and
  written with **one** temp rather than two (the target keeps exactly one
  value live across the `stw`), `erase` is now **100%** in all three
  instantiations, 97.759% → 100%.

  Two habits follow. **Re-verify a queued item against the tree before
  working it** — half this queue had already shipped. And **"no measured
  gain" on a change made for a stated reason means the reason is probably
  wrong**, not that the change is free.
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

- ~~**`containers.h` — `tier_queue<T>::wrap_block` returning `u32`.**~~
  **LANDED** once `xFX` was rewritten. The original evidence was right (a
  `U8`-returning member forces `clrlwi r3,r3,24` at every call site, and the
  target's own `wrap_block` is `clrlwi r3,r4,24; blr`, i.e. a `u32` return
  with a `(U8)` truncation in the body), but when first measured it cost a
  different `xFX` function for net -1, so it was held. After the 17 absent
  bodies landed, that conflicting function no longer exists: re-swept over
  the 75 TUs, it is 1 improved / 0 regressed, taking
  `tier_queue<joint_data>::clear` 97.273% → 100%.

  The rule it was filed under still stands, and so does its converse:
  **measure every requested header change on its own before believing it —
  and re-measure a rejected one after the unit around it changes.** A
  rejection is a measurement of a tree, not a fact about the source.

## Open leads

- **What actually creates the eleven ghost `.rodata` templates.** Four 12-byte
  and seven 40-byte all-zero anonymous objects open the `.rodata` of at least
  `zVar` and `xParEmitterType`, referenced by nothing in either object, and
  they carry the *same* anonymous index range (`_617`–`_623`) in both. That
  shared range is the clue: a single header almost certainly emits them into
  every TU that includes it, which is exactly the parse-time aggregate-
  initialiser mechanism written up under "Settled". Upstream never found the
  cause and worked around it with `__deadstripped_*` in 15 files; the
  workaround is now sanctioned (see the skill), but finding the header would
  let all fifteen be deleted and would probably unblock units nobody has
  connected to this yet. Look for a header with eleven aggregate initialisers
  in `inline` bodies, sized 12 and 40 bytes.

- **The reload-after-aliasing-store defect now has seven witnesses in one
  unit.** Retail's `mwcceppc` **reloads** a value after an intervening store
  that could alias it, where this branch's compiler keeps the cached copy. In
  `xFX` alone this is the sole blocker for six of the nine remaining sub-90%
  functions, and it is the same family as the alias oracle at `0x511fc0` (see
  `project_float_meme_root_cause`), not the scheduler patch:

  - `xFXRingCreate` (89.983%) — retail reloads `1.0f` (`@958`) once per
    `*= 1.0f / lifetime`, because each `stfs` into the ring invalidates the
    `.sdata2` load. The pre-existing `// non-matching: 1.0f is only loaded
    once` comment turns out to be exactly this.
  - `activate_ribbon` (45.000%) — retail reloads `active_ribbons_size` after
    `active_ribbons[i] = ribbon`; we keep it in a register and sink the store.
  - `xFXAuraUpdate` (85.147%) — retail reloads `gFrameCount` in each of four
    unrolled iterations; we hoist it out.
  - `LightResetFrame`, `DrawRing`, `xFXShineRender`, `xFXStreakRender` — same
    shape, plus one base-address materialisation retail repeats and we CSE.

  This is a better-evidenced patch target than anything currently on the list:
  it is one rule, it is measurable in a single unit, and no source form reaches
  it short of `volatile`, which would be wrong (and was already rejected on
  `zNPCHazard::Discard` for emitting a load the target does not have). The
  natural companion case is `zNPCHazard::Discard`'s residual, which survives a
  control experiment.

- **Epilogue `lwz` swap** (`lwz r31` before `lwz r0`) — checked, it is the sole
  blocker for only 2 functions project-wide, so it is *not* worth a compiler
  patch.
- **Ghidra for the MISSING bucket.** 1483 functions have no implementation at
  all; `gh.sh` gives a usable starting point for each.
- **Five units at 100% still cannot be marked Matching.** Run
  `tools/symorder.py` on each for the specific reason.
  - `xDebug.cpp` defines `__deadstripped_xDebug`, and `__as__10iColor_tag`
    sits in the wrong place in `.text`.
  - `xParSys.cpp` emits four `operator=` instantiations the retail link
    dropped, plus an unreferenced 4-byte `.sbss2` object -- the same problem
    that pins `ZDSP_elcb_event`.
  - `global_destructor_chain.c` and `__init_cpp_exceptions.cpp` put their
    `_reference` objects in `.sdata2`, where the target has them in `.ctors`
    and `.dtors`. The sources already carry
    `__declspec(section ".dtors")` and it is simply not being honoured.
    `#pragma section`, dropping `const`, and `-sdata 0 -sdata2 0` were all
    tried: the thresholds move the objects to `.rodata` instead of `.sdata2`
    but never to `.dtors`, and they also push `fragmentID` out of `.sdata`,
    so they are the wrong answer. `__init_cpp_exceptions` is 20 bytes of
    `.text` off besides.
  - `ptankgcntransforms.c` is missing `_rwConst`/`_rwConstants`/`_rwFifo`.

  Note that defining a function the retail link deadstripped is *not*
  automatically fatal -- `mem_funcs.c`, `FILE_POS.C`, `nubevent.c` and
  `float.c` all do and all link fine, so check the order before blaming the
  extra symbol.
- **`report.json` marks units complete that are not.** It scored `zSurface`
  28/28 while `solo.py` had `zSurfaceUpdate` at 99.733% and our object
  additionally emitted an out-of-line `xVec3::operator=` that the retail
  link deduplicated away. The link test caught it. This is the same class of
  artifact that blocks `xModel`: our object carries weak copies of
  `xAnimFileRawTime` and the `xMat3x3`/`xMat4x3` assignment operators which
  survive only in `xAnim.o` and `xCamera.o` in the retail link, so dtk's
  extracted object lacks both them and the `0.5f` literal one of them
  creates.
- **`xSFXUpdateEnvironmentalStreamSounds` — the source corrections are known
  and still do not help.** Five errors were proved against the target and the
  control flow was brought to an exact match, yet the best variant scored
  69.8% against a 72.883% baseline, so the file was left alone. The
  corrections, for whoever tries again: `break` -> `continue` on the
  `dist > cachedOuterDistSquared` test; `s_managedEnvSFX[0] = NULL` rather
  than assigning through `->id` (the target stores through the array's own
  address); `bestDist2[k] > dist` rather than `dist > *bestDist2`;
  `bestSFX[k] == NULL` rather than comparing the array address; and the tail
  calls `xSFXPlay(best)` on both paths. The blocker is that the target
  indexes all three arrays with a *variable* (`li`/`slwi`/`stwx`) while every
  source shape tried -- S32/U32/S8/register/const index, declared early or
  late, literal-load plus variable-store, volatile -- makes mwcc fold the
  index and hoist `&arr[k]` into callee-saved registers, costing ~10
  instructions and adding an `stmw` prologue. Eight variants measured, all
  65-70%.
- **An unreferenced 4-byte zero object in `.sbss2` blocks
  `ZDSP_elcb_event` at 99.984%.** One relocation index is off by one because
  our object carries an extra anonymous `@148` ahead of the `iColor_tag
  clear` constant. Its id is low, so it is created during header parsing, and
  it survives commenting out every removable `#include` -- it comes from the
  mandatory `zDispatcher.h`/`zGlobals.h` chain and cannot be reached from the
  .cpp. This is the same unsolved problem as the POOL bucket: what creates a
  literal before its first `.text` use.
