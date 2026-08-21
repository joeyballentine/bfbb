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

| metric | at branch point | now (2026-08-14) |
|---|---|---|
| matched functions | 6491 / 10147 | 8051 / 10147 |
| complete units | 195 / 543 | 232 / 543 |
| **game code exact** | — | **67.750%** (bytes) |
| **game code fuzzy** | — | **95.778%** (6882 / 7673 functions) |
| **game units linked** | — | **91 / 224** |
| SDK code | — | **90 / 90 units, 100.000% fuzzy — complete** |

Game code is tracked separately because it is the part that is actually
being worked, and the project figure understates it badly: Renderware and
Bink contribute 1375 functions at 6.6% and 2.4% and have never been
touched. By category: SDK 99.484%, **game 94.775%**, MSL 79.045%,
Renderware 6.629%, Bink 2.371%.

**Report exact and fuzzy together; they have decoupled.** `matched_functions`
counts a function **only at exactly 100.0%** — not at >=99% (verified: `zFX`
matched 54, functions at 100.0 = 54, functions at >=99.0 = 59). So near-miss
work is nearly invisible in fuzzy: the 2026-08-12 batch converted 24 functions
from 99.9% to 100.0% and moved fuzzy **+0.014** while moving exact **+0.61**.
The inverse also holds — the `xShadow` pass the same day moved that unit's
fuzzy +3.14 and exact by **zero**, because nothing crossed 100.0.

Three different denominators answer three different questions, and the
flattering one is the least useful:

| measure | value | what it means |
|---|---|---|
| functions exact | 86.76% (6657/7673) | flatters: unmatched functions average 654 b, matched ones 146 b |
| **bytes exact** | **59.70%** | the honest headline |
| units linked | 88 / 224 | what a port actually needs — one bad function poisons a unit |

**A fourth number, and it is the one that bites: `complete_units` counts
`Object(Matching, ...)` markers in `configure.py`, not units that reach 100% in
`report.json`.** On 2026-08-12 `zMenu` hit 14/14 functions at 100.0 with
`report.json` scoring **100.0 on every section, code and data** — and a real
link still moved the DOL, because `.sbss` is laid out in declaration order and
ours was `menu_fmv_played, card, sInMenu, corruptFileCount, time_last,
time_current, sAttractMode_timer` against the target's `menu_fmv_played,
time_last, time_current, sAttractMode_timer, card, sInMenu, corruptFileCount`.
Reordering three declarations fixed it and the unit linked.

So finishing a unit's functions is **necessary but not sufficient**. The closing
sequence is always: `tools/symorder.py <unit>` until it says "every section
matches the target's symbol order", then `tools/fliptest.py --test <unit>`, then
`--apply`, then a real `ninja` with the DOL sha1 checked. Skipping it means the
work does not count where it matters.

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

58 game units are within 3 functions of being complete — finishing those is the
fastest route to raising `complete_units`. See the roadmap below.

## Roadmap to 100% game code

Written 2026-08-12 against that day's build. Re-derive the numbers before
trusting them; the shape is what matters, not the digits.

**1,017 functions and 663,320 bytes remain, across 221 game units of which 87
are fully matching.** The distribution is barbell-shaped, and that dictates the
order of everything below:

| band | count | note |
|---|---|---|
| 99-100 | 185 | 168,944 b — each 1-3 instructions out |
| 90-99 | 455 | the big middle |
| 0-90 (nonzero) | 278 | broken bodies |
| absent | 99 | 98 with no symbol + 1 wrong `operator=` |

**58 units are 1-3 functions from complete**, while **30 units hold 623 of the
1,017 functions** (61%), zEntPlayer alone holding 88.

### Phase 1 — convert units, not functions

Clearing every unit 1-3 away costs ~118 functions and takes fully-matching
units **87 -> 145**. Nothing else has that leverage, because a unit only
becomes `Matching` — the DOL linking *our* code — when every function in it is
exact. Order: the 24 one-away units first, then the 13 harder ones, then 2-away
(18 units), then 3-away (16). Batch ~4 small units per agent; they share idioms.

**Measured correction (2026-08-12, batch 1 of 8 one-away units): the ordering
above is inverted, and the yield is far lower than "~118 functions" implies.**
Eight of the eleven units whose single blocker was *already above 99%* were
attempted. **One converted** (`zMenu`, and only after the `.sbss` reorder
above). The other seven were all compiler-class and unreachable from source:

| unit | blocker | class |
|---|---|---|
| `zMenu` 99.622 | `zMenuLoop` | **fixed** — volatile static read into a local |
| `iPad` 99.883 | `iPadUpdate` | SCHED — two adjacent independent `lis` swapped |
| `zCollGeom` 99.770 | `zCollGeom_Init` | operand canonicalisation; 12 shapes measured |
| `xEntMotion` 99.694 | `xEntMotionDebugDraw` | REGS — allocator tie-break |
| `zEntButton` 99.429 | `zEntButton_Init` | SCHED — rotation of four `addi` |
| `iAnim` 99.581 | `iAnimBlend` | REGS — copy base coalesced r21 vs r12 |
| `zNPCTypeBoss` 99.565 | `ZNPC_AnimTable_BossSBobbyArm` | REGS — r4/r5 on a 2-word `@sda21` copy |
| `zAnimList` 99.488 | `zAnimListInit` | 2b reload-after-store |

**The lesson is that a tiny residual is evidence *against* source-reachability,
not for it.** A function 2-3 instructions out has usually already had its shape
solved by whoever left it there; what remains is the allocator or the scheduler.

**Refined again after batch 2 — rank by absolute differing-instruction count,
not by percentage.** Percentage conflates two unrelated things: a *small*
function a few instructions out, and a *large* function with real bugs.
`zGameModeSwitch` reads 94.872% because it is 78 instructions and 4 of them
differ — a SCHED interleave, unreachable. `MorphCommon` reads 94.608% because it
is 498 instructions with genuine source bugs, and it yielded three of them. Same
percentage, opposite prospects. Of batch 2's first four, only the one genuinely
large function carried real bugs; two were a single scheduler idiom repeated at
six sites (`zTextBox`/`zUIFont`, the same float-literal-versus-stack-store shape
as `xFont`'s `render_fill_rect`, which already carries a "float scheduling"
comment).

So work the one-away units in *ascending* percentage — the 13 sitting at 84-98%
(`xHudFontMeter` 84.6, `zGoo` 90.7, `xClimate` 92.4, `zEGenerator` 92.4,
`zUIFont` 93.3, `zGameState` 94.9, `zTextBox` 94.9, `iMorph` 94.6, `xSkyDome`
96.5, `xNPCBasic` 98.6, `xstransvc` 98.6, `iTRC` 98.5) are where real source
bugs still live. Skip `xSFX` 72.9 — see "Will this reach 100%?".

Three of the seven have **provably identical instruction multisets**, so they
are exactly the population a scheduler/allocator patch converts. Best test case
on the board: the two-word `@sda21` aggregate copy adjacent to a call taking two
null arguments, with three witnesses (`ZNPC_AnimTable_BossSBobbyArm` 99.565,
`ZNPC_AnimTable_NightLight` 99.545, `ZNPC_AnimTable_Tubelet` 99.583) against two
controls that match at 100.0 because they address a three-word table with
`lis/addi` (`ZNPC_AnimTable_SleepyTime`, `ZNPC_AnimTable_BossSB1`).

**Batch 3 (2026-08-12, post-E3n): the five never-investigated one-away units
are also compiler-class. Retire them.** Ranked by differing rows rather than
percentage — which is the right heuristic and still did not find source work:

| unit | blocker | rows | class |
|---|---|---|---|
| `zEntHangable` | `zEntHangable_UpdateFX` | 2/65 | **branch form** — see below |
| `xSkyDome` | `xSkyDome_AddEntity` | 4/78 | SCHED |
| `xstransvc` | `XST_unlock` | 5/22 | REGS (r5<->r6) |
| `xMovePoint` | `xMovePointGetNext` | 10/75 | REGS (r4<->r6) |
| `xNPCBasic` | `Init__9xNPCBasic` | 11/127 | SCHED |

`zEntHangable` is a **new residue class worth naming: codegen branch form, not
scheduling.** Verified against raw target bytes in `build/GQPE78/asm/`. The
target's `case 2:` body is *two* instructions, so CW picks its 1-instruction
inverted dispatch and falls through; ours is one instruction, because CW drops
the unreachable end-of-case branch, so it picks the 2-instruction
`beq case / b default` form instead. ~30 shapes measured: every `switch` form
gives 99.769 with the same 2 rows, every `if`/`goto`/loop form collapses to a
single `beq` (96.769), and extra arms flip CW into its tree form (90-93%). No
source shape keeps the dead end-of-case branch alive.

**Boundary on the E3n `const` lever, measured on four units: it does not reach a
store made *through a pointer parameter*, only stores into a declared frame
object.** Top-level `const` on a pointer parameter (`xEnt* const ent`) does not
change the CW mangled name, so it is free to try without touching a header — and
it moves nothing. Do not retry it.

**`dwarf/` is not always right.** It lists `xMovePointGetNext`'s locals as only
`rnd, idx, previousOption`; writing it that way measures **92.467%** against the
current 99.333%. Keep the locals. Use DWARF as evidence, not as an oracle.

**Retail bug, faithful, flagged for PCPORT:** `xSkyDome_AddEntity`'s first loop
is `for (i = 0; i > sSkyCount; i++)` — it never executes, so the duplicate-entity
guard is dead and an entity can be added to `sSkyList` twice. The target emits
`cmpw`/`bgt`, so retail shipped it. A PC port will want `i < sSkyCount`.

**Verify completeness with `tools/symorder.py`, not `report.json`.** It scored
`zSurface` 28/28 while `solo.py` had a function at 99.733% and our object
emitted a weak `xVec3::operator=` the retail link deduplicated. Five units sit
at 100% and still cannot be marked Matching for this class of reason (see
Open leads).

### Phase 2 — the structural blockers

These block dozens of functions each and decide whether 100% is reachable.

**2a. Deadstripped-literal pool displacement.** A function the retail link
removed left `.sdata2`/`.rodata`/`.sbss2` literals behind that nothing
surviving references, so every later pool offset shifts. Priced on 2026-08-12
at **44 functions across just `zNPCFXCinematic` and `zNPCTypeDutchman`** (both
measured with throwaway probes that were then removed), and it is the same
mechanism as the eleven ghost `.rodata` templates in 15+ units and the
`.sbss2` object pinning `ZDSP_elcb_event` at 99.984%. **Fabricating bodies is
not allowed** — the objects *are* referenced, which fails condition 1 of the
`__deadstripped_<unit>()` exception. The legitimate attack is `dwarf/`: if a
deadstripped function can be *named* there, its real body is recoverable and
the problem dissolves honestly. Highest-value investigation on the board and
not yet run against this question.

**2026-08-14: `zPlatform` is the fourth and most informative 2a witness, and it
re-confirms the zero pricing the hard way.** Mid-investigation `report.json`
and `solo.py` appeared to *agree* at 21/24, which looked like a counter-example
to the blindness. It was not. `zPlatFM_Update` was being held off 100.0 by a
single **non-pool** row — the target stores `tmrs[i] = 0.0f` *before* the
`flags &= ~(1 << i)` read-modify-write, we did it after. Swapping those two
statements moved `solo.py` 99.431 -> 99.883 and flipped `report.json` to a
clean **100.0 while 21 pool rows remain in the diff**, banking all 3588 bytes.

Lesson, and it is the practical one: **when `report.json` and `solo.py`
disagree about a unit, the delta is the pool bucket and you should ignore it;
when they agree, do not conclude the pool is being counted — look for the one
non-pool row hiding among the pool rows and fix that instead.** Sorting a
function's diff rows into pool vs non-pool before touching anything is worth
more than any amount of pool archaeology.

The two functions still short here (`zPlatformEventCB` 3192b at 99.098,
`zPlatform_PaddleCollide` 888b at 99.865) are blocked by SCHED/REGS residue,
not by the pool: a 180.0f/`toParam[1]` load permutation at six sites and an
f3/f5 two-cycle respectively. So the displacement below is worth **zero
`matched_functions`**, as originally priced. It is recorded because the
fingerprint is the best one yet, not because it is billable.

Both sections hold the *same 30 objects with the same values*; the whole
difference is that the target creates `3.0f` (`@974`) and `1e-5f` (`@976`)
between `zPlatform_PaddleStartRotate` and `zPlatform_PaddleCollide`, while we
create `3.0f` last of all (`@1187`) and `1e-5f` inside `zPlatFM_Update`
(`@696`). Seed those two, in that order, at that point and every other object
in the section lands on the target's exact offset — head and tail both.

What makes this witness better than the `zThrown`/`zShrapnel`/`zNPCTypeRobot`
`0.5f` ones: it is **two constants, two ids apart, and it reuses a `2.0f` that
already exists** at `@968`. That is a fingerprint, not a single float.
`EASE()` in `xMathInlines.h` is `rhs * ((rhs*3.0f) - (rhs*2.0f)*rhs)` — it
creates exactly `3.0f` and reuses `2.0f`. `1e-5f` occurs only in
`xVec3NormalizeMacro`/`xfeq0`. So the missing construct plausibly eases a
scalar and then normalises a vector, in that order.

`zPlatform_PaddleStartRotate` is at **100.0%** for us and its last pool
reference is `@875`, so the creator is not in its body — it sits textually
between that function and `PaddleCollide` and emitted no surviving code, i.e.
a link-deadstripped file-local. `dwarf/` has no `zPlatform` file and
`solo.py --missing` reports 0, so the honest route is still blocked. **Do not
fabricate a body here**: the constants are referenced, so condition 1 of the
`__deadstripped_<unit>()` exception fails, exactly as for the other three.
(Contrast `xCamera`, same day: there the ghost `.rodata` block *is* clean of
inbound relocations — only `@405` is referenced, addend 0, as the materialised
section base — so `__deadstripped_xCamera()` was legitimate and took `.rodata`
to layout-identical. The two cases differ on condition 1, nothing else.)

Helper: `scratchpad/zplat_pool.py` compiles a unit via `solo.py`'s own
machinery and prints the `.sdata2` slot map with values — reusable for any 2a
investigation.

**RUN 2026-08-12. Repriced: 2a is worth ZERO `matched_functions`, because
`report.json` is blind to literal-pool displacement.** Across the game units,
545 functions differ *only* by a relocation whose target symbol name differs in
the compiler-assigned `@NNN` id — and **all 545 already read exactly 100.0 in
`report.json`**. Verified directly on `zNPCFXCinematic`: `solo.py`/objdiff says
30 non-matching of 93, `report.json` says 16 of 93, and the delta is exactly the
14 pool-only functions. `NCIN_MaryBoom` measures 99.894% under objdiff and
**100.0000** in `report.json`; `report.json` scores that unit's `.sdata2` 100.0
while the target section is 0xc0 bytes and ours is 0xb8.

This also resolves the standing "`solo.py` and `report.json` disagree and both
are right" puzzle — **the delta *is* the pool bucket.**

**Worse: the blindness is not limited to anonymous ids. `report.json` will score
a function 100.0 while it references an entirely different named global.**
`ZNPC_AnimTable_ThunderCloud` read `g_strz_roboanim` where the target reads
`g_strz_cloudanim` — a real behavioural bug, the thunder cloud playing robot
animations — and `report.json` called it 100.0 both before and after the fix,
while objdiff moved 99.420 -> 100.000. Whenever a relocation *target* is the
only difference, the relocated field is zero in both objects and the byte
comparison passes. Five game functions currently counted as matched reference
the wrong symbol: `HurtThePlayer` and `WipeIt` (`zNPCHazard`), `Subscribe`
(`zNPCSpawner`), `ParseINI` (`zNPCSleepy`), and `ThunderCloud` (now fixed).

Three of those five pointed at a device that has now been removed. **RESOLVED
2026-08-12** (`87692902`, `0c6050f7`): `zNPCHazard`, `zNPCSpawner`, `zScene` and
`zNPCTypeRobot` declared externs that nothing anywhere defined —
`_958_Hazard // 0.0f`, `_959_Hazard // 1.0f`, `_1041_Hazard // -1.0f`,
`_805_Spawner // 5.0f`, `_1250`, `_1251`, `_2013`, `_2014`, `byte_803D0884`, and
`zNPCSleepy::init`. They were a pool-slot device, and **the names literally
encode the target's anonymous pool ids** — the target references `@958`, `@959`,
`@1041`, `@805`, `@1250`… at exactly those sites. Someone read the numbering off
the target and named externs after it instead of writing the constants.

The `zNPCHazard`/`zNPCSpawner` values came from the comments; `zScene`'s four
carried no comments and were recovered from the target object
(`@1250` = `0f 0f 0f 00`, `@2013` = `00 00 00 ff`, `@1251`/`@2014` zero, in
`.sdata2`/`.sbss2` respectively). Nine undefined symbols eliminated; those
objects can now in principle link.

**Removing a placeholder can convert its neighbours.** One edit in
`zNPCSpawner` — `_805_Spawner` to `5.0f` — took six functions to 100.0, five of
them untouched ones sitting at 99.8-99.93%. In `zNPCHazard` nothing shifted at
all, because those literals reused slots that already existed. Both outcomes are
normal; measure the whole unit either way.

Two related placeholders of the *defined* kind were also removed: `xFont`'s
`_1107` (unused dead `.rodata`) and `zScene`'s `_2098_0` (a hand-written
288-byte jump table duplicating the compiler's own switch table, displacing
every later `.data` object by 288 bytes). After both, each unit's `.data` and
`.sdata2` match the target's layout exactly.

**Expect the metric not to notice.** That batch made 16 functions byte-exact and
`report.json`'s `matched_functions` moved by **1**, because pool-only and
relocation-target differences already scored 100.0 there. The work is still
real: byte-exactness is what `Matching` requires.

So: **price pool work in units linked, never in `matched_functions`.** The
honest price of 2a is **9 units, 0 functions** — 9 of the units that are 1-3
report-functions from complete carry ≥1 pool-only function and will therefore
fail `fliptest` even at `report.json` 100%: `zDiscoFloor` (14), `iMath3` (7),
`xClimate` (4), `xHudMeter` (3), and `xTRC`, `xCM`, `zAssetTypes`, `iCamera`,
`iScrFX` (1 each). 52 game units carry at least one.

**And `dwarf/` does not help the two units 2a was priced on.** A sweep of all
198 `dwarf/` files that map to a unit with a target object found 29 dwarf-only
function definitions across 13 units (22 of 24 checked are absent from
`config/GQPE78/symbols.txt`, i.e. from the whole retail DOL) — but
`zNPCFXCinematic` (77 dwarf defs) and `zNPCTypeDutchman` (98) have **zero**.
Point `dwarf/` at the 13 units that do: best are `zDiscoFloor` (3 away, 14
pool-only, names `clip_render`, `sphere_hits_screen`, `compare_buckets`,
`insert_atomic`) and `xTRC` (small, 3 away, `DisplayMessage`,
`pad_message_valid`). Caveats: absent-from-DOL has three causes (deadstripped,
GC-inlined, PS2-only — `xShadowReceiveShadowFastPS2` and `strtosjis` are plainly
the third), and `dwarf/` is DWARF from a *linked* PS2 ELF, so anything the PS2
linker also dropped is invisible there too.

**Two framing errors corrected.** A name-based orphan scan is mostly false
positives: CodeWarrior materialises one `.rodata` section base and addresses
later constants by displacement, so in `zNPCFXCinematic` the "orphans" at
`+0x170`/`+0x17c`/`+0x188` are all reached by `NCIN_SleepyLamp_AR` off a single
`@405` base pair. `zVar` has 18 `.rodata` objects with 2 named; `zNPCHazard` 53
with 14. Treat the project-wide name-based count of 323 as an upper bound, not a
population. Second, the eleven-ghost-template id range is **not** stable across
units (`@612`-`@618` here, `_617`-`_623` in `zVar`), so the "shared range is the
clue" lead below is wrong. `zNPCFXCinematic`'s actual defect is pool *ordering*,
not orphans: both pools hold the same 40 objects with the same values, but the
target creates `3.0f` and the u32→double magic at `+0x18`/`+0x20` ahead of
`3.141593` while we create them at `+0x60`/`+0x80`, and the unit has no stubs
and no MISSING functions left, so no unwritten body can explain it.

One genuine orphan does remain there: `@1756` = `(0.25f, 0.0f, 0.0f)`, 12 bytes,
whose id places its owner between `NCIN_SleepyDRay_AR` and `NCIN_FodProd_Upd` —
so one of `MaryBoom`, `PeteBonk`, `FireSpiral_Upd`, `FireSpiral_AR`,
`ShieldPop`, `OilHazard` should declare a 12-byte aggregate initialiser with
that value. Four of the six are 88-99%.

**2b. The reload-after-aliasing-store defect.** Retail's `mwcceppc` reloads a
value after a possibly-aliasing store; this branch's compiler forwards it.
One rule, and no source form reaches it short of `volatile`, which is wrong and
was already rejected on `zNPCHazard::Discard`. This is a compiler patch, which
is what this branch is for. Prior art: the float-meme alias patch priced at
+55/+77 functions.

**CENSUSED 2026-08-12 — and it does not price like the float meme.** Across all
542 units with both objects:

| tier | what it is | functions |
|---|---|---|
| NAMED | surplus load of a *named* global; symbol identical in both objects, so the comparison is exact | **90** (58 game) |
| MEMBER | surplus load of a struct member `0xN(reg)`; registers normalised, so collisions possible | 96 |
| ANON | surplus load of an anonymous pool literal; ids differ across objects, so per-literal attribution is impossible | 87 |

**The number that matters is 8.** Of the 58 game NAMED candidates, only 8 have a
match percentage consistent with the reload being their *sole* cause (comparing
the surplus load count against the observed deficit). The other 50 have other
differences too, so fixing 2b moves them but does not convert them:
`iModelStreamRead` 99.490, `zParPTankSteamUpdate` 98.919, `zLightningUpdate`
98.802, `zParPTankSparkleUpdate` 98.776, `zUIRenderAll` 98.652,
`PlayerMountHackUpdate` 94.545, `xCMupdate` 91.500, `xSerialShutdown` 80.000.
Distribution of the 58 by band: 2 at 99-100, 31 at 90-99, 16 at 50-90, 9 below.

**The "seven witnesses in `xFX`" claim below is stale** — re-measured after the
2026-08-12 `xFX` work, `DrawRing` (93 loads vs 93, `Im3DBufferPos` 4 vs 4),
`xFXShineRender` (55 vs 55) and `xFXStreakRender` (33 vs 33) have *identical*
load counts and are not reload cases at all. `zAnimListInit` is also excluded,
because the `volatile` device means we now emit the reload; its residual is the
`mr` copy. The surviving verified witnesses are `activate_ribbon`,
`xFXAuraUpdate` and `NPCHazard::Discard` (NAMED), plus `xFXRingCreate` and
`LightResetFrame` (ANON, float-literal reloads).

### The switch-tree pivot convention: real, but MUCH narrower than it looks

`zEntPlayerEventCB` was simulated instruction-by-instruction: our 43 case
values and their body order in `.text` are IDENTICAL to retail, and all 70
differing rows are in the binary search tree. At every node both compilers
choose the same partition and differ only in the pivot -- retail takes the
max of the lower half, we take the min of the upper half.

A standalone repro confirms our side. Eight sparse cases
(3, 9, 37, 205, 300, 449, 488, 500) give a root of `cmpwi r3, 0x12c` (300),
i.e. element n/2 of the sorted list; retail's convention would root at 205,
element n/2 - 1. So it is an off-by-one in median selection, not a different
tree shape.

**But the "this costs every function with a deep switch" reading is wrong,
and was checked before anyone spends a session on it.** Of the 41 `*EventCB`
functions in game code, **38 already match at 100.0**. The largest matching
one is `zEntDestructObjEventCB` at 1,184 b; the only non-matching ones are
`zVolumeEventCB` (268 b, 89.179 -- small, so almost certainly a different
defect), `zEntPlayerEventCB` (2,624 b) and `zPlatformEventCB` (3,192 b),
which are the two largest such functions in the game. The divergence
therefore appears only once the case count is large enough, and total
exposure is about **5,816 bytes in two functions**, not a project-wide tax.

Weigh that against the cost: locating median selection in a 6 MB stripped
binary with no debugger is open-ended, unlike clause E3n which was a
one-byte change at an address an agent had already pinpointed. Left as a
lead, not scheduled.

One thing it DOES settle: `xEvent.h`'s enum is correct. The case values match
retail exactly, so any future "the event enum is shifted" reading of that
diff is wrong and must not be acted on.

### The store-to-load forwarding defect: isolated, and DEPRIORITISED

A 30-line repro reproduces it standalone with the shipped 2.0p1a compiler and
the project's own cflags. All three field shapes fall out of it:

```c
S32 g_cnt, g_max, g_out, g_arr[64];
void a(void) { g_cnt++; if (g_cnt == g_max) g_out = 1; }     /* Tiki Process */
void b(void) { g_max--; g_out = g_arr[g_max]; }              /* iSndSceneExit */
extern S32 find(void); S32* g_ptr;
void c(void) { g_ptr = (S32*)find(); if (g_ptr == 0) g_out = 2; } /* InitFX */
```

We emit, for `a`, `lwz r3,g_cnt / addi / stw r3,g_cnt / lwz r0,g_max / cmpw
r3,r0` -- the compare uses the forwarded `r3`. Retail reloads `g_cnt`. In `c`
the compare is even hoisted ABOVE the store (`bl find / cmplwi r3,0 / stw
r3,g_ptr`), where retail is `stw / lwz / cmplwi`.

Flag sweep on the repro: **no named `-opt no*` switch disables it** --
`nocse`, `nopropagation`, `nolifetimes`, `noglobal_optimizer`, `nopeephole`,
`noschedule`, `nodeadcode`, `nostrength`, `noloopinvariants` all still
forward. It turns on at the `-O2` threshold (`-O0`/`-O1` reload, `-O2`
upward forward). So it is gated by opt level inside the optimizer, not by a
flag, and there is no cheap toggle to bisect it with.

**Deprioritised on cost/benefit, not on difficulty.** Nearly every known
witness is ALREADY matched via the `volatile` device -- Tiki's `Process` and
`zNPCTiki_InitFX`, `NPCHazard::Discard`, and the iSnd sites. Fixing the
compiler would mostly let those `volatile` qualifiers be deleted, which is a
source-fidelity gain rather than exact bytes. Before anyone spends a session
on it, re-check the census below and count how many functions would actually
CROSS 100.0 as a result; when that count was last taken it was approximately
zero. Note also `xFXAuraUpdate` (85.294) is a *different* sub-case -- there
the killing store is indirect (`0x4(r31)`), not a plain static.

**Census of `volatile` sites installed for the store-then-reload defect.**
These are all reproductions of a reload retail genuinely performs, each
evidenced against the target, and each is file-local. But they MASK the
compiler-side fix: if the value-numbering path is ever widened to cover this
class, every one of these must be reverted first or the measurement will read
as zero. Keep this list current.

    iSnd.cpp          ua_stream_buffer, stream_buffer  (file-scope globals)
    iSnd.cpp          sinfo_array_max                  (file-scope global)
    iSnd.cpp          snd_id, strm_id                  (function-local statics)
    zNPCTypeTiki.cpp  cloudEmitter                     (file-scope static)
    zNPCTypeTiki.cpp  numTikisOnScreen                 (pointer-cast at the use)
    zNPCHazard.cpp    g_cnt_activehaz                  (file-scope static)
    zEntPlayer.cpp    sPlayerIgnoreSound, bbash_start_ht, idle_tmr
    zScene.cpp        oldOffsetx, oldOffsety, scobj_idbps (pointer-cast)

One site was deliberately NOT added: `zSceneSetup`'s `gCurEnv`. Reading it
through a volatile lvalue does reproduce retail's `stw`/`lwz` pair and moves
the function 99.618 -> 99.743, but a second, scheduler-class cluster still
blocks it from 100.0, so the device would bank no bytes while adding to the
masking problem. The rule that follows: install this device only where it
takes a function ALL the way to 100.0.

Clause V does not fire on these. Clause V kills the *literal pool* across a
store to a small static; this defect is the compiler forwarding the *stored
value itself* to a following load of the same static. That is store-to-load
forwarding, a different transform, and it is reachable from neither patched
dispatch table. Eight-plus witnesses across four units now, which is the
argument for finding it rather than qualifying more variables.

**The store-then-reload shape appears three times in `zNPCTypeTiki` alone**
(`numTikisOnScreen` in `Process`, `cloudEmitter` in `zNPCTiki_InitFX`, and a
literal-inside-an-unrolled-loop variant in `SetCarryState`). Fourteen
non-volatile source spellings and eleven compiler-flag settings were measured
against it; nothing but `volatile` reproduces it. Note also that for
`numTikisOnScreen` a *whole-variable* `volatile` is disproven by the target --
it takes `zNPCTiki_PickTikisToAnimate` from 100.0 to 97.806, because the
volatile store stops mwcc sinking `li r0, 0` -- so the pointer-cast spelling is
the minimum-blast-radius form of the same fact, not a weaker one. Whatever the
real construct is, it is neither a statement-level rewrite nor a flag.

**`NPCHazard::Discard` re-measured 2026-08-21, after clause V shipped: the
`volatile` on `g_cnt_activehaz` is still load-bearing.** Removing it drops
`Discard` from 100.0 to **90.161** — mwcc collapses the
`subi r0,r3,1 / stw / lwz` triple into a single `subi r3,r3,1` and reorders
the store past the `srawi`. Retail genuinely stores and reloads the counter,
so this site is outside clause V's reach and the device stays. It also has no
measurable effect on `zNPCHazard_ScenePrepare`/`SceneFinish`, whose own
residual (a `stw` to the same symbol sinking past 24 unrolled null stores) is
a separate, unsolved issue.

Most-reloaded symbols, in case the trigger is narrower than "any global":
`__ctype_map` (7), `RwEngineInstance` (6), `cb_bink_sound` (6), `globals` (5),
`cb_bink_IO` (4), `gTRKCPUState` (3).

**Method warning — four separate attempts failed the same way**, all worth
knowing before writing any cross-object comparison: objdiff row *alignment*
hides a surplus when it pairs the extra instruction against one of ours (2/9
recall); raw operand text never matches because anonymous pool ids differ
(`@1171` left, `@531` right) and registers differ (0/9); collapsing all
anonymous ids to one token merges every distinct float literal in a function
(9/9 recall but 43% of all non-matching functions flagged); `relocation.
target_symbol` is an **index into that side's symbol list**, not a name, and the
indices differ between objects; and symbols of the form `name$1234` are
function-local statics whose ids also differ. Normalise all five before
counting, and require that *we* do not load a named symbol the target lacks —
that last check is what separates the defect from a plain wrong-symbol source
bug. The census script is `cen_2b_v7.py` (scratchpad), validated at 5/5 recall
on the re-verified witnesses.

**Byproduct worth mining: 30 functions reference a different named symbol than
the target does.** Some are naming artifacts, but the first one checked was a
real bug — `ZNPC_AnimTable_ThunderCloud` used `g_strz_roboanim` where the target
uses `g_strz_cloudanim`, fixed in `504aebf3` for 99.420% -> 100.000%. Triaging
the rest is cheap, high-yield source work.

**2c. Weak/deduplicated symbols. REWRITTEN 2026-08-12 — the old framing was
wrong.** It said our surplus `operator=` instantiations block units from
Matching. They do not. Most of them are harmless and always will be.

**The rule: a surplus or misplaced symbol blocks `Matching` only if this unit
is the symbol's owner in the retail link.** Every TU that used an `xVec3`
assignment emitted a weak copy of `__as__5xVec3FRC5xVec3`; the retail linker
kept exactly one and discarded the rest, so dtk's extracted objects cannot show
it anywhere except its owner. Ours emits it for the same reason theirs did, and
the linker drops it for the same reason. `symorder.py` will report that surplus
for every unit that assigns an `xVec3`, forever, and it means nothing.

Use **`tools/symowner.py <symbol>`** to resolve a name through
`config/GQPE78/symbols.txt` to an address and then through `splits.txt` to the
owning unit. `__as__5xVec3FRC5xVec3` lives at `.text:0x8000B264 scope:weak`,
inside `SB/Core/x/xBound.cpp`.

Proof the surplus does not block: `zEnt.cpp`, `zEntTrigger.cpp`,
`zPendulum.cpp`, `xSurface.cpp`, `xHudText.cpp` and `zVar.cpp` are all
`Object(Matching, ...)` today and all emit surplus `.text` symbols their target
objects lack — `zEnt` emits `__as__5xVec3FRC5xVec3` itself, mid-`.text`, and
`zVar` emits a **strong global** `__deadstripped_zVar__Fv` at position 0.

Re-triaged against that rule:

| unit | actual blocker |
|---|---|
| `xParSys` | `using_ptank_render`, which it owns — **fixed, unit now links** (`98560c47`) |
| `xClimate` | none. Its only symbol difference is xBound's. The blocker is `UpdateRain` at 92.405% |
| `zSurface` | `.sdata2` ordering of `@900`, the signed int->float magic — a 2a deadstripped artifact, see below |
| `xDebug` | `__as__10iColor_tagFRC10iColor_tag`, which it owns, one position too early |
| `xModel` | owns `__as__11RwMatrixTagFRC11RwMatrixTag` and misplaces it — but it also has two broken functions, so symbols are not the first problem |

**`xParSys` is the worked example.** It sat at 0 non-matching of 22 and could
not link because CodeWarrior emits a header-defined `inline` at end-of-TU while
the target has `using_ptank_render` at `.text` position 3, right after its first
caller. Moving the definition into the `.cpp` between `par_sprite_update` and
`render_par_sprite` put it exactly there and the unit linked, with the three
foreign-owned weak symbols still present and still harmless.

**`__deadstripped_<unit>()` does NOT make a unit unlinkable** — an earlier note
here claimed it did, on the basis of `xDebug`. `zVar` and `xHudText` both carry
the idiom and both link. In `xDebug` the stub is load-bearing for a different
reason: it is a call-forcing stub, not the `.rodata` template kind, and it is
the only thing causing ten weak inlines that `xDebug.o` *owns*
(`0x80017DA4`-`0x80018064`, ~704 bytes) to be emitted at all. Removing it loses
all ten. Keep it. `zFX`'s copy is likewise not a problem.

**`zSurface`'s real blocker** is that `@900` (`43 30 00 00 80 00 00 00`, the
signed int->float magic) sits after `zSurfaceUpdate`'s four floats in our
`.sdata2` and before them in the target's. The constant is allocated at the
TU's *first* signed int->float conversion in source order; in retail that
happened inside a function between `zSurfaceGetSlideStopAngle` and
`zSurfaceUpdate` that no longer exists. Proven with a throwaway probe: inserting
such a conversion into `zSurfaceGetSlickness` moved the constant to the target's
exact slot, `.sdata2` matched completely and `zSurfaceUpdate` reached 100%.
Reordering the `switch` cases does not move it and costs 38 points. So this is a
Phase 2a deadstripped-code artifact, the `__deadstripped_` exception does not
cover it (condition 1 fails — the object *is* referenced), and `dwarf/` lists no
zSurface function we lack.

**Method warning for header blast-radius sweeps.** An include-path overlay does
not work: mwcc's `-gccinc` own-directory rule resolves `#include "xParSys.h"`
from `src/SB/Core/x/` before any `-i` path, so the overlay is silently ignored
and the sweep appears to prove no change. Copy the whole tree and patch it.
Always run an `#error` positive control first.

### Another scheduler patch — measured 2026-08-12, and the answer is NO-GO

Sized before building anything, because the population had never been measured.

**First, a measurement artifact that invalidated the starting numbers.** objdiff
prints a local branch's target as a section-relative address, so every branch in
a function reads as a difference whenever the function sits at a different
`.text` offset. `classify.py` compared raw text and so inflated `OTHER` roughly
2x while hiding ~500 already-byte-equal functions inside it. Fixed in
`eebc2025`; buckets went OTHER 914 -> 443, POOL 150 -> 493, SCHED 26 -> 83,
REGS 23 -> 94. **The old "SCHED 26" floor was an artifact.** Anyone quoting
`classify.py` numbers from before that commit should re-run it.

**The real population**, with the two streams realigned independently and pool
ids, local-static ids, resolved relocation names and branch deltas all
normalised:

| class | project | game | converts on a scheduler patch? |
|---|---|---|---|
| SCHED sole blocker | 89 | 86 | yes |
| SCHED-modulo-registers sole | 87 | 85 | only if the reorder is pre-RA — unproven |
| SCHED **plus** a separate REGS window | 42 | 41 | **no** — needs both |
| REGS sole blocker | 88 | 87 | no |
| byte-equal (pool/placement only) | 545 | 543 | already 100.0 in report.json |

That third row is a class nobody had named. A naive count scores those 42 as
gains and they do not convert; carry the distinction so the next patch is not
over-priced.

**The filter that kills it.** A patch at the `0x511fc0` alias oracle is an alias
*edge* — it can only stop one memory reference crossing another. Of the 218
scheduling-sole functions, **78 contain a motion that crosses no memory
operation at all** (an `addi` sinking past an `addi`, an `mr` past an `fmuls`).
Those are pick-order/tiebreak, and every tiebreak mutation is already measured
and dead. At most 140 are reachable by any alias predicate, before asking
whether one predicate covers them.

**No cluster has a head.** Reducing each permutation to its motions gives 673
events across 218 functions; the largest single shape is 23 events / 18
functions and it is one of the unreachable ALU-past-ALU ones. The top 32 shapes
cover 292 of 673.

**The best candidate rule is clause D, which is already dead.** "A literal load
may not hoist over a stack store" converts **11** functions (14 if three
register fixes come free) — `zNPCGoalRobo` x3, `xFX::eval_joint`,
`xRayHitsSceneFlags`, `LeafNodeBoxPolyIntersect`, `KickOilGlobby`,
`auto_tweak::load_param<iColor_tag,i>`, `CollidePyramidBoxTop`, `NCIN_Zapper`,
`xFont::render_fill_rect`. Scanning all 7,251 functions currently at exactly
100.0, it puts **243 of them at risk across 821 sites — 17x the gain**, and
`xFont::get_texture_size` is the recorded witness of clause D knocking a
function off 100%. Broader rules are worse (the full-memory-barrier form reaches
33 and is the unconditional-may-alias experiment already measured at -144/-330);
narrower ones reach 4-7.

**REGS is not approachable, and the allocator is now mapped so nobody has to
ask again.** 88 sole blockers, of which 31 are a single register 2-cycle; the
biggest cluster is `f0<->f1` at 6 functions and the `ZNPC_AnimTable_*` family
is 3.

The allocator, read from `.text` on 2026-08-12 — previously unmapped:

- **RA/schedule driver `0x508680`**, once per function. Five-iteration loop over
  register classes (current class at `[0x5ea299]`); per class it compares live
  ranges `[0x5e9b04+class*4]` against physical count `[0x5e9800+class*4]` and
  runs build → simplify → select → spill.
- **Allocatable-mask builder `0x4fe4d0(class)`**, from `.bss` tables `[0x5e3b68]`
  (slot → register bit) and `[0x5e5c78]` (reserved flags).
- **Simplify/degree `0x508a20`**, iterating the live-range array `[0x5e9858]` by
  index — i.e. IR creation order — degrees at `[lr+0x12]`. Classic
  Chaitin-Briggs; this fixes the select-stack order.
- **Color/select `0x508900`**, the decisive one: it clears each interference
  neighbour's colour bit, then scans from bit 0 upward and assigns the
  **lowest-numbered free register**.

So the `r4`/`r5` difference is **not a tie-break knob** — it is coloring order.
Whichever live range is coloured first takes r4 and the other takes r5, and that
order comes from creation index. Changing it is a whole-program change with the
same blast radius as the tie-break experiments already measured dead
(ties-keep-earlier −1204). There is no narrow gate. That is *why* REGS is a
wall, not merely that it is one.

**The dependency-graph framing is mechanically impossible**, so that door is
closed too. The store builder `0x508350` creates WAR/WAW edges by walking only
the **backward** pending lists `[0x5e0866]`/`[0x5e0862]`. The discriminating
reload in `get_texture_size` is a *forward* instruction, five after the store, so
it is not in any list at edge-creation time — it only becomes visible later as an
outgoing RAW edge at the RAW builder `0x508100`. An earlier note here claimed the
builder "is given that information"; it is not. A reload gate would need a new
forward block-scan or a post-DAG cleanup pass, neither of which exists. And
empirically it would not have separated the populations anyway: gains are 32%
reloaded (10/31), losses 40% (36/89) — statistically indistinguishable.

**One untried framing, flagged not recommended.** The discriminator between
clause D's gains and losses is whether the stored slots are reloaded in the same
block. The alias predicate is not given that information — but the dependency
graph builder at `0x508100`/`0x508350`, which walks the pending load/store lists
at `[0x5e0866]`/`[0x5e0862]`, is. Installing the predicate there rather than at
`0x511fc0` is the only unexplored option. It is a much larger RE job, the
ceiling is still ~14 functions, and nothing about it has been measured.

Better uses of a session, by the same measurement: 98 game functions have no
symbol at all (39,896 bytes), 394 game functions are genuine source differences,
and `zNPCGoalRobo` and `zNPCHazard` each carry 17 scheduling-sole functions plus
real source work.

### Phase 3 — the near-miss sweep (~185 functions)

~~Cheap per function, but **run it after 2a**. A large share of the 185 are
pool-shift victims no source edit can fix.~~

**Corrected 2026-08-12: Phase 3 does not depend on 2a and can run now.** Pool
victims already read 100.0 in `report.json`, so by construction *none* of the
185 near-misses is a pool-shift victim — they are a disjoint population. What the
batch-1 measurement does say is that the near-miss pool is much thinner than 185
suggests: 7 of the 8 near-misses attempted were REGS/SCHED/2b and unreachable
from source. Triage by class before assigning, and expect a low hit rate at the
top of the percentage range.

### Phase 4 — the long tail (30 units, 623 functions, 410,192 b)

Real decomp work: `zEntPlayer` (88), `zNPCTypeRobot` (42), `zNPCGoalRobo`
(39), `zNPCHazard` (38), `xFX` (34), `zNPCTypeBossPlankton` (25). zEntPlayer
needs a dedicated multi-run campaign, never a shared batch — at 146 KB with 54
already-matched functions, a pool disturbance risks a lot at once. Plus the 98
absent bodies, concentrated in `zEntPickup`, `iParMgr`, `xShadowSimple`,
`zNPCGoalStd`, `xSnd`.

### Phase 5 — the queued shared-header changes

Deliberately last, applied one at a time with a full sweep after each. High
blast radius: `xVec3.h` reaches 188 TUs, `xClumpColl.h` 169, `xFX.h` 74. See
"Queued shared-header changes" below.

### Will this reach 100%?

**Not from source alone.** `xSFXUpdateEnvironmentalStreamSounds` had five
errors proved against the target and its control flow brought to an exact
match; eight variants measured, all *below* the untouched baseline. REGS-class
and SCHED residues have resisted every source shape tried across many
functions. `LassoNotify` is one unreachable branch instruction. These are
documented dead ends, not open puzzles.

The realistic path is **two tracks in parallel**: source work clears Phases 1,
3 and 4, while the compiler track (2b, plus whatever 2a's DWARF work reveals)
closes the last stretch. If the compiler track fails, the branch plateaus —
high, but short of 100%.

Recommended order: **Phase 1 first** (best leverage, lowest risk, visible as
whole units), with **2a's DWARF investigation alongside** — it is research
rather than edits, and it gates Phase 3.

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

## The alias patch: clause C+, and what the predicate cannot reach

Shipped 2026-08-21. **Clause C+ on dispatch entry 0: +19 functions to exactly
100.0, 9,400 bytes, zero functions lost, DOL unchanged.** Game exact
73.214 -> 73.786 in one change.

Clause C excluded these sites through the **size test and the flags mask**,
not the storage gate. Of the four bits it excludes (0x08/0x10/0x20/0x40) only
**0x20** unlocks anything, and every site it unlocks is an indirect store such
as `stw r0, 0x4(r31)` whose memref reports the *pointee's* size rather than
the access width -- so `flags & ~0x86 == 0` and `sizeof <= 4` each
independently reject the pair. Clause C+ tolerates 0x20 and drops the size cap
on the store side only, keeping both static-storage gates and the
differing-opcode test.

**Entry 0 only.** On entries 1/3 as well it is +19/-3, and one loss is
`zPickupTableInit` in a *complete* unit, which breaks the link.

Space came from **deduplication**: the two literal copies of clause C are now
one shared body reached by a 10-byte `call` stub per entry, so free `.text`
padding went from 25 bytes to 43. The refactor was validated before any
semantic change -- the deduped *strict* clause C produced 8060 exact and not
one changed function across all 451 units.

### Clause V: the value-numbering store kill (SHIPPED)

The site above was found and patched, 2026-08-21. **+25 functions to exactly
100.0, zero lost, DOL unchanged, game exact 74.511 -> 75.732** -- the largest
single change in this project's history.

**How it was located, since the method generalises.** `mwcceppc.exe` still
contains its `__FILE__` strings (`Alias.c`, `ValueNumbering.c`, `Scheduler.c`
...) for the `CError_FATAL` call sites. Scanning `.text` for the *address
constants* of those strings gives a module map; a naive `E8`-scan call graph
then answers "who calls what, from which module". That separates two Alias.c
entry points:

| routine | table | called from |
|---|---|---|
| `0x511fc0` | 3x3 at `0x5bd0bc` | Scheduler.c, CodeMotion.c -- clauses A/B/C/C+/E3n |
| `0x511a30` | 3-entry at `0x5bd068` | **ValueNumbering.c only** -- clause V |

`0x511a30(memref, instr)` is the **store kill for local value numbering**: it
dispatches on the stored memref's kind byte (`+0x2c`) and stamps a fresh value
number via `0x50a2c0`. Stock kills only the stored object and its precomputed
alias sets, so a `.sdata2` constant survives the store and the next statement
reuses it. Verified under gdb (wibo maps the PE at 0x400000; sjiswrap
relocates to 0x110000): the repro shows nine loads in value-numbering pass 1
and five in pass 2 -- the literal loads die there, not in the scheduler.

**Clause V, entry 0 only:** when the store's base expression is a plain static
object, walk the value-number list and also kill every object that is itself a
plain static of size <= 4. Both halves are load-bearing; each was measured
tree-wide:

| variant | result |
|---|---|
| shipped (static base + static <=4 kill) | **+25 / -0** |
| gate the store on size <=4 instead of static base | +11 |
| kill the whole list (`call 0x511a00`) | +26 / **-24** |
| kill every static regardless of size | +10 / -0 |
| filter killed objects on size alone | +25 / **-5** |
| filter on the kind byte instead of base expression | +25 / **-1** |
| entry 1 (subrange stores) | +1 / **-30** |
| entry 2 | inert |

Entry 1's -30 is the compiler agreeing with the large-object contrast above:
retail deliberately does *not* kill on large-object stores, and killing there
changes CodeWarrior's unroll factor.

The cave was re-assembled as one 413-byte position-independent block at
0x57ea4c (68 new bytes did not fit in 45 fragmented free ones): clause B is
now a single body reached by `CALL` from both handlers, and the two stock
answers share one tail. **The refactor was validated before clause V was
added** -- rebuilt from clean `GC/2.0p1`, all 451 units produced identical
object SHA-1s and not one symbol moved. 27 of 451 objects change and none
belongs to a complete unit.

`PATCHED_SHA1 = 918652d8063c37ff4d172244f4fcbfa88e0ea062`.

### Clause V does NOT need a same-object size exemption -- measured, inert

`xFXAuraUpdate`'s `sAuraPulseAng[0] += ..; [1] += ..; if ([0] > k)` looked like
a clause-V gap: retail reloads `[0]` after the store to `[1]`, the array is
`static F32[2]`, and clause V only kills statics of size <= 4. **The premise is
false.** Both `[0]` and `[1]` are *subrange* memrefs (kind 1), so the store
dispatches to VN table **entry 1**, and clause V (entry 0) is never consulted.
Proved by pointing each VN entry in turn at a kill-all stub on a 20-line repro:
only entry 1 changes the output. Adding a same-object exemption to clause V
measures **+0/-0, zero of 451 objects changed**.

The correctly-targeted version is worse. Stock entry 1 at `0x511b0e` already
walks the other subranges of the same object and kills each one whose
`[offset,size)` **overlaps** the store; `[0]` and `[1]` do not overlap, so it
declines -- correctly, by C semantics. Retail kills anyway. Defeating that
overlap gate (two bytes, `je` -> `nop nop` at `0x511bb3`) measures
**+0/-47**, and `xFXAuraUpdate` itself drops 85.294 -> 80.537. Losses include
`xShadowReceiveShadow`, `xBoxInitBoundOBB`, `zEntPickup_GivePickup`,
`zGridInit`, six `xFont` functions and five `xClumpColl`/`xScene` routines.

And even that is only half: the stored value is still forwarded. The other
half lives on **scheduler entry 4** (subrange x subrange). Entry-1 no-overlap
plus entry 4 answering may-alias *does* reproduce retail's sequence exactly on
the repro -- and measures **+16/-560** tree-wide, matching the docstring's
existing -199 warning for entry 4. `xFXAuraUpdate` still does not reach 100.
**This direction is dead.**

**`gFrameCount` hoisting is a THIRD site, reachable from neither table.** With
all nine scheduler entries answering may-alias unconditionally *and* all three
VN entries killing everything, the `lwz r4, gFrameCount@sda21` is still hoisted
out of the 8-unrolled loop, byte-identical to stock. It is decided in
loop-invariant motion or global CSE. Anyone chasing it needs to find that site
first, the way `ValueNumbering.c` was found.

### Two corrections this forced

- **`zEntPlayer_SNDInit` was not the prize.** It was dispatched as a
  10,160-byte function whose residual was "about 400 of 403 rows missing `lfs`
  reloads"; clause V moved it 90.519 -> 91.947 and `PlayerTeeterCheck` not at
  all. **That is the second per-unit attribution to collapse on contact with
  the compiler** (the first was the 1.750pp alias-predicate estimate). Agent
  reports are good at *characterising* a residual and bad at predicting what a
  compiler change will pay. Verify before promising.
- **The docstring's warning against E3n on scheduler entry 3 was stale.**
  Reverting entry 3 to clause C measures **+6/-88**, so E3n is worth +82 net
  in the current tree, not the "+22/-18" recorded against it.

**Every unit's residuals now need re-measuring.** Attributions recorded before
2026-08-21 were made against a compiler that has since changed twice.

### The redundant-load path is a SECOND patch site, and it is untouched

`zMainParseINIGlobals` (8,980b, 99.276%) is capped by a defect the installed
patch cannot reach, and the third pass on it pinned down why.

A 20-line standalone repro (`extern F32 a1..a6;` + `if (u) { a1 = DEG2RAD(a1);
... }`) reproduces the **entire** residual under this unit's exact flags:

- **One statement alone compiles to retail's sequence exactly**, register roles
  included (`lfs f2,@PI / lfs f1,val / lfs f0,@180 / fmuls / fdivs / stfs`).
  The arithmetic, operand order and allocation are already right; the sole
  defect is that *consecutive* statements share the two literal loads.
- The reuse is **strictly basic-block-local** -- a `goto`/label between two
  statements reloads both literals and restores retail's roles at zero
  instruction cost. The retail block is one basic block of 36 straight-line
  instructions, so there is no free second boundary to exploit.
- **It is not the global optimizer.** `#pragma opt_common_subs off` and
  `#pragma global_optimizer off` change nothing (both verified accepted with
  `#pragma warn_illpragma on`). It is the **code generator's redundant-load
  elimination**.
- **It is governed by an alias query.** Inserting `*p = 1.0f;` through an
  `F32*` parameter between two statements makes the next statement reload both
  literals and revert to retail's exact `f2/f1/f0` roles. The query exists; our
  compiler simply answers "no alias".
- **The contrast is inside the same function.** The three
  `globals.player.g.*SlideAngle = DEG2RAD(...)` statements sixty lines earlier
  share their literals and are byte-identical to ours. The nine `zcam_*`
  statements do not share. The only difference is the store: a 4-byte
  `@sda21` scalar with an opaque `extern` definition versus a member of a
  large named object reached through a base register. Retail answers
  **may-alias** for (`lfs` of a <=4-byte anonymous `.sdata2` constant) x
  (`stfs` to a <=4-byte opaque named static) and no-alias for the large-object
  store; we answer no-alias for both. A `v_big` probe reproduces retail's
  large-object behaviour exactly, so **only the small-static case is wrong**.

That is the clause-A/clause-C predicate shape -- two <=4-byte statics,
differing opcodes, plain load/store -- but applied in the **code generator's
redundant-load path**, not the instruction scheduler's may-alias predicate.
`patch_compiler.py` only redirects the scheduler dispatch table at 0x5bd0bc,
which is exactly why clause C+ moved 19 functions tree-wide and moved this one
by zero. Whoever extends the patch next should look for the second query site.

Ruled out beyond the earlier lists: an `inline F32 d2r(F32)` helper (emits a
real `bl`; the unit is `-inline off`), per-statement braced temps, unused
labels (stripped before the optimizer), `extern F32 a[]` with `a[0]`, the
`(&a1)[0]` spelling, and a double-precision spelling (wrong shape entirely).

**`zMainMemCardSpaceQuery`'s pool ceiling is real but is NOT its blocker.**
Our object does emit out-of-line `NSCREENX`/`NSCREENY` bodies (declared
`inline` in `xFont.h`, not inlined because the unit is `-inline off`) which
intern 1/640 and 1/480; retail's zMain.o has neither, and across the six
target objects referencing them only `xDebug.o` defines them. But
`solo.py --relocs` costs just 0.049 pp here and `report.json` does not count
relocation rows at all -- the blocker is register allocation. Our allocator
always gives r31/r30 to the two block-scope values and r29..r22 to the
function-scope locals in declaration order; retail puts `workArea` and
`startBytes` *above* those, which no declaration permutation can reach. An
automated hill-climb over ~600 declaration orders plateaus at 97.803% and
never reaches 100.

### Clause E3n's load-side size bound widened to 8 (SHIPPED 2026-08-21)

Clause E3n reads "an `stfs` to a declared frame local may not be crossed by a
*later* small static load". Its load-side test was `sizeof(B) <= 4`, which
declined on the one object that matters most for this shape: the
unsigned-int-to-float magic double `0x4330000000000000`, an **8-byte**
`.sdata2` object. With the clause declining, our scheduler hoisted that `lfd`
above a run of stores to declared frame locals; retail leaves it at its first
use.

The fix is one byte in the cave -- `cmp ecx, 4` -> `cmp ecx, 8` at `0x57eb75`,
same instruction length, no reflow. Tree-wide measurement against the
otherwise identical build:

    main.dol   306526d90b48e99894c3138f5fc8f2716d9fecf6  (unchanged)
    GAME exact 76.523 -> 76.639   (+0.116)
    GAME fuzzy 98.9078 -> 98.9106 (+0.0028)
    exact functions +3, -0

Gains: `xFX::DrawRing`, `zNPCTypeKingJelly::load_param<iColor_tag,int>`, and
`zNPCBalloonBoy::PlatAnimSet` (48.419 -> 100.0). Only `DrawRing` was
predicted; the other two were found by the sweep, which is the usual argument
for measuring these tree-wide rather than on the witness unit alone.

Three functions get *worse* in fuzzy without crossing 100 -- `xFont::get_bounds`
61.827 -> 49.423, `cruise_bubble::add_trail_sample` 97.661 -> 91.516,
`zUI_Render` 91.348 -> 90.159 -- so they cost no `matched_code` and the net is
+3/-0 on the metric that counts.

**`xFX::eval_joint` was predicted to flip and did not** (98.077, unchanged).
Its `lfd` crosses `stfs f0, 0x8(r1)` *and* `stw r5, 0x14(r1)`; the surviving
defect is presumably the `stw`, so the store-side of the clause is what
declines there. Do NOT widen A's test to match -- that is the symmetric rule
the clause-C notes measure at -50 exact functions. Only the load side moved.

### Measured NO-GOs, so nobody re-opens them

- **Relaxing the static-storage gate on the store side: -80 (+29/-109), and
  none of the intended functions move.** The premise -- "the load side
  qualifies, the store side is pointer-based and fails the gate" -- is FALSE.
  Two probes prove it: allowing the store side only when its base expr is not
  a frame object, and only when it has no base expr, **both change nothing
  anywhere**, so those pointer-based stores never reach clause C at all. The
  only population the relaxation admits is stack traffic, i.e. exactly what
  the gate exists to exclude (the docstring's -50).
- **The whole avenue is bounded.** With **all nine dispatch entries answering
  "may alias" unconditionally**, only 5 of the 21 functions this project had
  attributed to the alias predicate reach 100.0, and most get *worse*. A
  1.750pp attribution built from per-unit agent reports did not survive
  contact with the compiler. Treat "blocked on the reload defect" as a
  hypothesis to test, not a diagnosis.
- Also measured full-tree and rejected: store side gated on computed-address
  -5; on object-link no gains; dropping the differing-opcode test +21/-7;
  requiring the load side to be a whole object +19/-8; requiring load-side
  offset 0 +19/-7.

**Method note worth copying.** The agent hashed every compiled object before
and after: 28 of 451 changed and none belonged to a complete unit, which is
how it predicted the DOL would survive before any link was run. That is a
better proxy than percentages, because objdiff pairs symbols by name and is
blind to definition order.

## Patterns that keep working

- **A same-value expression used as both an allocation size and a copy size
  gets CSE'd into a callee-saved register and wrecks the register map.**
  Retail's is *signed* at one site and unsigned at the other:
  `(S32)sizeof(xVec3) * numVertices` for the `memcpy`, plain
  `sizeof(xVec3) * numVertices` for the `xMemAllocSize` argument. Different
  result type, different tree, no CSE -- retail recomputes `mulli`/`slwi` at
  each site. This took `zFXGooEnable` from 93.296 to exactly 100 (1,000 bytes)
  and was the only thing that did.

- **Bind each argument of a store macro to a named temp first.**
  `RwIm3DVertexSetPos`/`SetRGBA` written with expressions inline emits
  load/store/load/store; retail emits all loads then all stores. Our compiler
  will not reorder a load across a store through a pointer, so the temps have
  to impose the order from the source side. Worth ~9 points on
  `zFXGooRenderAtomic`, whose position blocks became byte-exact.

- **Declaration ORDER alone can be the entire residual.** In
  `zNPCBSandy_BossDamageEffect`, `S32 j;` before `S32 i;` -- with every use
  unchanged -- flipped which subscript mwcc materialises with `slwi` and which
  rides the induction variable, and took the function from 97.971 to 100.
  Cheap to try, and it costs nothing to be wrong.

- **`x = !(flags & bit)` narrows to 8 bits because C++ `!` yields `bool`.**
  Written as an explicit `if/else` assigning 1 and 0, mwcc if-converts to the
  same `cntlzw`/shift pair with no `bool->int` conversion node, giving
  retail's full-32-bit `srwi`/`extlwi`. Keep the variable `S32`: making it
  `bool` fixes the shift and breaks every later use of it.

- **Retail does not always fold a literal multiply.** Its inliner emits
  `fmadds f0, <1.0f>, (a-b), b` for `LERP(1.0f, b, a)`; written as a literal,
  mwcc folds `1.0f*x` to `fadds` and folds the `0.0f` case away entirely.
  Routing the blend factor through a local reproduces the unfolded form.

- **CAVEAT on the const-aggregate rule below: it is not unconditional.** It
  did not fire on `zFX`'s `validate_popper`, where the 12-byte copy still used
  two scratch registers with `const` applied; an intermediate model local was
  also measured and rejected. Try it, measure it, drop it if it does nothing.

- **`const` on a read-only local aggregate changes the copy expansion.** A
  12-byte `xVec3` copy-init emits `lwz/lwz/stw/stw/lwz/stw` with two scratch
  registers when the local is non-const, and `lwz/lwz/lwz/stw/stw/stw` with
  three when it is `const` -- the latter interleaves freely with surrounding
  FP work, which is what retail does. Same for `xVec2`. In
  `zNPCTypeBossPlankton` this was the *sole* change needed for
  `update_follow_camera` (83.784 -> 100) and `Enter__22zNPCGoalBPlanktonFlank`
  (94.435 -> 100), and it carried four more functions. Cheap to test on any
  unit with local vector copies.

- **An array element bound to a reference addresses differently.** Retail
  emits `mulli / addi <member offset> / lwzx` for `territory[i].timer`; the
  plain subscript emits `mulli / add / lwz <disp>`. Writing
  `territory_data& t = territory[active_territory];` then `t.timer`
  reproduces retail's form, and was the only change `stun` needed to reach
  100.0.

- **Locals that never existed are the single most productive find, every
  time.** A pointer temp (`xMat4x3* mat = ent->model->Mat`) the original did
  not have changes register allocation and suppresses the reload the target
  performs. `dwarf/` lists the real set. The inverse matters as much: a local
  that *should* exist, e.g. `F32 fadeDist = 0.0f;`, because mwcc folds a
  literal `0.0f + x` and the target does not fold.

- **Cross products take `xVec3` struct operands, not six `F32` scalars.**
  Scalars give the right relative register order rotated by one
  (`ax=f4..bz=f3` instead of `f3..f8`). Worth 1,924 bytes on
  `xShadowReceiveShadow`.

- **`x / 2.0f` is not `x * 0.5f`.** mwcc canonicalises a multiply so the
  constant loads first (`fmadds f0, 0.5, x, y`); a divide by an exact power of
  two folds to the same pool entry but cannot commute, giving retail's
  `fmadds f0, x, 0.5, y`. All three multiply spellings were measured and
  produce the wrong order.

- **`if (len)` is not `if (len != 0.0f)`.** Written against a literal, mwcc
  emits `fcmpu cr0, const, len`; the implicit test emits `fcmpu cr0, len,
  const`, which is retail's order.

- **`x OP= c` is not `x = x OP c`.** mwcc evaluates the constant first for the
  second form and the destination first for the compound assignment. If the
  target loads the memory operand before the literal in an `fadds`/`fsubs`,
  the source used `+=`. Timers are the usual site.

- **A three-way ladder, not a two-way one: `x = c * x` / `x *= c` / `x = c
  * expr`.** The refinement of the note above, measured on
  `NPCHazard::DeathStar`. With `F32 spd = 0.4f * this->custdata.typical.rad_max;`
  mwcc issues `lfs <0.4f>` before `lfs <member>` and lands the product with
  `fmuls f1, f1, f0`. Splitting into a load then `spd *= 0.4f;` fixes the load
  order but keeps the wrong destination register (99.565). Only
  `F32 spd = member; spd = 0.4f * spd;` gives both, and it is 100.0. So the
  choice is not merely which operand issues first -- it also decides which
  register the result is written to, and the two are set independently.

- **mwcc evaluates the RIGHT operand of a binary `*` first when both sides
  have side effects.** This is why folding a `spd_factor` temp back into one
  expression in `NPCHazard::KickBlooshBlob` left the emitted `xurand()` call
  order untouched while flipping `fmuls f31, f31, f0` into the target's
  `fmuls f31, f0, f31` (99.868 -> 100.0). Useful whenever the only diff is a
  commuted `fmuls` and the operands are calls: fold the temp away rather than
  swapping the operands in the source, which is inert. The inverse move --
  hoisting a *named* temp out of such an expression -- sinks the multiply past
  the call and is much worse (93.649 measured).

- **The `const`-aggregate lever does not fire on aggregates that already emit
  the three-register form**, so apply it one declaration at a time and keep
  only the ones that move. In `NPCHazard::Render` three of five candidates
  paid (93.330 -> 94.930) and two were inert.

- **A member load and an adjacent aggregate copy sharing a base register get
  clustered, and then load order and copy position cannot both be had.**
  Measured across `Upd_OilOoze`, `Upd_OilGlob`, `TarTarLinger`,
  `Upd_ChuckBloosh` and `StagColGeneral`: declaring the scalar before the
  vector gives retail's multiply-before-copy position but the wrong load
  order; swapping gives retail's load order but sinks the multiply below the
  copy. No source form yields both. When the copy source is a `.rodata` base
  instead (a *different* register, as in `DeathStar`) there is no clustering
  and the asymmetry inverts -- which is why `DeathStar` is solvable and these
  five are not. Treat this shape as blocked, not as unfinished work.

- **`MAX(k, expr)` has a distinctive signature** and is often mistaken for an
  `if`-clamp: the literal is loaded into the *result* register before the
  compare, the compare is `fcmpo cr0, <literal>, value` (literal first), and
  there is an `ble L / b L2 / L: fmr` pair with an empty then-arm. If the store
  comes from the literal's register, the source used the macro. Worth
  94.245 -> 94.858 on `thunderCountCB` by itself.

- **A two-statement fract idiom tells you which variable retail assigned to.**
  `x = x - (F32)(S32)x;` emits `fsubs f5, f5, f3` (writes back into its own
  register); assigning into a *different* variable emits `fsubs f3, f5, f3`.
  Read the overwritten register in the target and you know the destination.
  Same family as the `x OP= c` note, applied to the destination rather than
  the operand order.

- **When the only diff is a commuted `fmuls`/`fmadds` and NEITHER operand has
  side effects, swapping the source operands is not the fix** -- it moves the
  evaluation order too and just trades one wrong row for another. Make one
  operand already-computed instead: `factor = xurand() - 0.5f;` then
  `ePos.x += (1.0f - gfactor) * factor;`. This is the other half of the
  right-operand-first note above, which covers only the side-effecting case.

- **Binding store-macro arguments to named temps works, but the temps must be
  INTERLEAVED so only two are live at once.** This is the rule that took
  `SandyLimbSpring::SpringRender` 92.635 -> 100.0 (844 b), and the failed
  intermediate is the instructive half. `RwIm3DVertexSetPos` is a
  three-statement macro; with the products written inline each `->y =`/`->z =`
  is its own statement, mwcc gives each one `f0`, and it therefore reloads the
  `0.9f`/`1.1f` literal per component. Retail loads the literal once and keeps
  two products live. Declaring **all four** temps up front only reaches 97.370:
  with sin/cos in `f2`/`f3` and the first pair in `f0`/`f1` there is no scratch
  left, so mwcc re-materialises the second pair at the use site and the second
  vertex keeps the reload/interleave shape. Declaring the second pair *below*
  the first macro call is the last 2.6 points. So when this lever half-works,
  the fix is usually to move declarations down, not to add more of them.

- **A countdown loop on the parameter** (`while (numTriangles--)`) rather than
  an index loop: the index form costs a callee-saved register and shifts the
  whole file. Worth 83.611 -> 100.0 on `shadowCacheLeafCB`.

- **One shared loop counter per function**, not one `S32 i` per `for`-init.
  mwcc allocates a fresh register per declaration; this took five functions to
  100.0 in `zNPCHazard` alone.

- **Evaluate all components into temporaries, then store.** The target
  computes three sums into `t0x/t0y/t0z` and stores after; a per-component
  load/add/store makes the store to `.x` kill the cached `.y`/`.z`.

- **Read the stack frame before guessing at the body.** The `stwu r1, -N`
  in the prologue is a hard measurement of how much local storage the
  original declared, and every `r1`-relative offset in the diff is a slot
  map you can solve. `start_detaching` was 0xe0 against our 0xd0; the
  missing 0x10 is exactly what `xMat4x3` adds over `xMat3x3` (`pos` plus
  its padding), and because `xMat4x3 : xMat3x3` the `right`/`up`/`at`
  offsets are shared, so nothing else in the function moved. Changing the
  one declaration erased the whole prologue/epilogue diff. Before this,
  look for the *smallest* type change that accounts for the delta — a
  derived type, a bigger array bound — and only then consider an unused
  local.
- **CW numbers same-scope locals in reverse declaration order.** Slots go
  up as declarations go back: the last-declared local gets the lowest
  `r1` offset, the first-declared the highest. Given a target slot map
  you can therefore read off the original declaration order directly and
  reorder to match. In `start_detaching` the target's `eulerVec` at 0x50
  and `world_loc` at 0x5c proved `eulerVec` was declared *after*
  `world_loc`, i.e. down at its first use rather than at the top.
- **Bind a `&` to an aggregate the target keeps in a callee-saved
  register.** When the target computes an address once, holds it across a
  call, and reaches everything through `rN+offset` while we recompute
  `globals@ha`/`@l` at each use, the original bound a reference.
  `xMat4x3& cam = globals.camera.mat;` took `start_detaching` from
  94.097 to 99.172. Declaration point matters: placed before the call it
  matches, placed after it costs 4 points.
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
- **`x / 2.0f` is not `x * 0.5f`.** Both emit a single `fmuls` against 0.5f,
  but the division form emits `fmuls rD, var, const` and the multiplication
  form `fmuls rD, const, var`. Operand order is otherwise canonicalised by CW
  and unreachable, so this is one of the few ways to choose it. (`zNPCGoalRobo`
  `LaunchRoboBits` -- it was the entire residue.) Note the trick does not
  generalise to other constants: for `fv * 2.0f` neither `2.0f * fv` nor a
  named temp moves the operands.
- **`S32 flag = (cond) ? 1 : 0; if (flag)`** is the only shape that reproduces
  CW's `li 1 / b / li 0 / cmpwi` boolean materialisation. A bare `if (cond)`,
  an `S32`/`bool` temp assigned from the comparison, and a two-statement
  `if (c) flag = 1;` all get folded away -- all four measured.
- **The narrowing type of a boolean temp picks the compare.** `S32 x =
  (bool)(a && b);` gives `clrlwi` at definition plus *signed* compares at use;
  plain `bool` gives `li 0/1` and `clrlwi.`; `U8` gives `clrlwi` + `cmplwi`;
  `S8` gives `extsb`. Four distinct emissions from one expression.
- **`F32 x = expr; x *= k;` versus `F32 x = expr * k;`** decides whether the
  multiply lands before or after an intervening call.
- **`arr[i++]` in the body with `arr[i]` in the condition** reproduces a
  non-CSE'd double load plus `lwzx base, offset` addressing, where a hoisted
  pointer will not.
- **Reading a `U8` flag field as its declared signed type emits `extsb.`;
  retail often wants the plain byte** (`cmplwi`), i.e. `*(U8*)&field`. Same
  result for a 0/1 flag, but it changes register allocation across the whole
  function -- worth 1.7 points on one `zNPCGoalRobo` function.
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

- **`xVec2::create` brace-init is the strongest-evidenced open header change,
  and it is still UNVERIFIED.** `zNPCTypeDutchman` reports `create__5xVec2Fff`
  at 63.636%: the target loads an 8-byte all-zero `@512`, stores it to the
  return slot, then overwrites both words; we go straight to two `stfs`. The
  agent slot-mapped `.sbss2` on both sides -- target 19 slots, ours 18, in
  exact 1:1 owner order with a uniform 8-byte offset -- and named five
  functions whose *entire* residue is that shift and which should reach 100.0
  with the one change: `clip_outside_circle(xVec3)` 99.143, `update_eye_glow`
  99.787, `calc_beam_loc` 99.859, `Process__PostFlame` 99.915, and `create`
  itself.

  **Do not apply it on that evidence alone.** The identical change to
  `xVec3::create` was equally well evidenced, made both overloads byte-exact,
  and still came out +2/-5 with a broken DOL on a full rebuild, because it adds
  an object to `.sbss2`/`.rodata` in *every* TU that instantiates it and
  `.sbss2` ordering is per-object. Measure with `find src -name "*.cpp" -o
  -name "*.c" | xargs touch && ninja`, a per-function `report.json` diff, and
  the `306526d9...` DOL check before believing either direction.

- **`xVec3::create` brace-init: measured, and it BREAKS THE DOL. Do not apply.**
  The target's `create__5xVec3Ffff`/`create__5xVec3Ff` load a 12-byte all-zero
  `.rodata` template, copy it to the frame and then overwrite all three words,
  so `xVec3 v = { 0.0f, 0.0f, 0.0f };` reproduces them exactly and takes both
  from 50.000% to byte-identical. It is still a no-go. A full build gives
  **+2 functions, -5 functions, and `main.dol` sha1
  `e81045024e60853b3c12a37cc9bf5b1682b60bea`** instead of `306526d9...`.

  The losses are the point: `xMath3Init`, `xParEmitterEmitSphereEdge`,
  `get_triangle_area` (`zFX`), `FodBombBubbles` (`zNPCHazard`) and
  `zVarGameSlotInfo` (`zVar`) each fall off 100.0, and several sit in units
  that are `Matching`, which is why the link moves.

  Two lessons. First, a header change that is provably right *for one function*
  can still be wrong for the tree -- this one is genuinely what retail's
  `create` did, and it still cannot land while other units depend on the
  current shape. Second, and more useful: **"most of the damage is POOL-class,
  so it is free" is not a safe inference.** That was the reasoning used to
  re-open this after it had been rejected on `solo.py` counts, and it was
  wrong -- the regressions were real `OTHER`/`SIZE` losses, not pool ordinals.
  Re-price on `report.json` *and* check the DOL before believing either
  direction.

  The same brace-init question is open for `xVec2::create`, which is written up
  further down as a recommended fix. It has not been measured this way. Do that
  before applying it.

- **`xVec3::cross` zero-template: same family, same result, DO NOT APPLY.**
  In the target, `cross` copies its 12 zero bytes from a *local anonymous*
  `.rodata` template (`@410`), not from the external `m_Null__5xVec3`, so
  `xVec3 v = { 0.0f, 0.0f, 0.0f };` instead of `= xVec3::m_Null;` makes the
  function byte-exact: `cross__5xVec3CFRC5xVec3` 59.355 -> **100.0**.

  A full build then gives **-4 functions, exact 71.410 -> 71.304, and DOL
  `ef8df9bd7b548636bb5b2380faeb61969725c308`**. Five functions fall off 100.0
  and they are *the same five* the `xVec3::create` entry above names --
  `xMath3Init`, `xParEmitterEmitSphereEdge`, `get_triangle_area` (`zFX`),
  `zVarGameSlotInfo` (`zVar`), `FodBombBubbles` (`zNPCHazard`) -- plus six more
  regressions in `xCamera`, `zNPCGoalRobo`, `zNPCTypeRobot`, `zNPCHazard`,
  `xCM`. Adding an anonymous template to *any* `xVec3` inline reshuffles
  `.sbss2`/`.rodata` in every TU that instantiates it, and the victim list is
  a property of the class, not of which method you touched.

  **The blast-radius estimate that justified trying it was wrong in an
  instructive way.** It was measured as "`cross__5xVec3CFRC5xVec3` appears in
  exactly one unit in `report.json`, so only that object changes". That counts
  where the *out-of-line body* is emitted; it does not count where the inline
  is *instantiated*, which is every TU that calls it. For a header change,
  grep the call sites, never the symbol table.

- **Making `xsqrt` an `inline` in `xMathInlines.h` does not compile.** The
  hypothesis is well-evidenced -- `xsqrt__Ff` is STB_WEAK in retail's
  `xBound.o`, and the four literals it creates (0.5, 3.0, 100000, 1e-5) are
  exactly the group missing from six of our objects (`xBound`,
  `zNPCTypeBossSB2`, `zNPCTypeBossPlankton`, `zEntCruiseBubble`,
  `zEntPlayerBungeeState`, `zNPCTypePrawn`). But `isinf` lives in MSL's
  `math_api.h`, which `xMathInlines.h` does not include, so ~250 TUs fail with
  `undefined identifier 'isinf'` (10 of 451 units before smoke.py aborted).
  Adding that include shifts include order, and therefore anonymous pool
  serials, project-wide. Anyone retrying this needs an answer for `isinf`
  first.

- **Stripping the dead aggregate initialisers out of `xLaserBolt.h:160`
  (`xVec3 temp = { 0, 0, 0 }`) and `zNPCTypeBossSB2.h:287` (`xVec2 cur = {..}`)
  is wrong.** Those templates leak into every TU that includes the headers and
  shift `.rodata`/`.sbss2`, which is real -- `zScene.o` carried both -- but
  removing them costs **exact 70.659 -> 70.648, -1 function**, and breaks the
  two functions that own them: `perturb_dir` 100.0 -> 80.889 and `turning`
  87.87 -> 65.833. The initialisers are what retail wrote. Fix the *consumer*
  by dropping the unnecessary `#include` (see `zScene.cpp`), not the header.

- **`solo.py`'s `@NNN` ordinals are not comparable to the target's, and a
  large finding was built on the assumption that they are.** solo compiles into
  a private temp dir where mwcc numbers anonymous literals differently from the
  real build. An agent measured "32 functions / 28,840 bytes in `zEntPlayer`
  blocked by a two-slot `.sdata2` misalignment", proposed a third `float_fix`
  shim to create the slots, and reported solo going 80 -> 48 non-matching. On a
  real build the payoff was **exactly zero**: `zSandy_AnimTable` (14,188b),
  `zEntPlayerVelUpdate`, `CheckObjectAgainstMeleeBound` and `zEntPlayer_Damage`
  were already at 100.0 in `report.json`, and `matched_code`/`matched_data`
  were unchanged to four decimal places.

  Rule: **use `solo.py` for code shape, never for pool questions.** To test a
  suspected pool mismatch, diff the object the real build produced:
  `objdiff-cli diff -1 build/GQPE78/obj/<unit>.o -2 build/GQPE78/src/<unit>.o`.
  A useful cross-check is that `matched_code` equals the sum of the sizes of
  the functions at exactly 100.0 in `report.json`.

- **A second compiler defect, sibling to 2b: a small loop bound that retail
  keeps in a callee-saved register, mwcc folds into `cmpwi`.**
  `iSndWaitForDeadSounds` wants `li r31, 0x8c` / `cmpw r0, r31`;
  `iSndSceneExit` wants `li r30, 0x190` / `cmpw r0, r30`. Seventeen source
  spellings were measured (plain/`const`/`register`/`static` locals, separate
  assignment, `for`-init, `do/while`, `goto`, reversed and negated compares,
  `long`/`U32` types, redundant self-assignment, extra break test) under
  GC/2.0p1, GC/2.0p1a and GC/2.6 -- **every one folds**. The `i = 0x8c;`
  re-assignment hack currently in `iSndWaitForDeadSounds` buys 95.455 against
  80.636 for the honest form and no matched function.

- **`ninja` does not track every header dependency. Incremental builds after a
  header edit can be measured on stale objects.** `zNPCGoalRobo.cpp` includes
  `xEnt.h` on line 5; `ninja -t deps` lists 180 dependencies for its object and
  `xEnt.h` is not among them, so after editing that header `ninja` reported
  "no work to do" and left a stale object behind. 376 of 542 objects predated
  the edit, most legitimately (bink/rwsdk do not include it), but not all.

  This matters because it silently changed a conclusion. The incremental build
  of the inline-helper fix reported **0 flipped, 0 lost**; a forced full
  rebuild of the same tree reported **0 flipped, 1 lost**. The regression was
  real and the incremental build hid it.

  **After any shared-header change, force a full rebuild before believing the
  number**: `find src -name "*.cpp" -o -name "*.c" | xargs touch && ninja`.
  Every header change measured before 2026-08-13 was measured incrementally and
  may be understated. HEAD as of `eeacf61b` has since been re-verified with a
  full rebuild -- DOL sha1 intact, 8029 functions -- so the committed state is
  sound; it is the *rejected* experiments whose costs may have been larger than
  recorded.

  **RESOLVED, and `solo.py` was the one telling the truth.** The apparent
  `solo.py` / `report.json` disagreement on `check_hide_entities` was not a
  compiler or flags problem: solo's private compile and ninja's object are
  **byte-identical** (verified by hashing both, plus a third compile with
  ninja's exact `-o <dir>` and `-MMD` form -- all three sha1-equal), and solo
  derives its 77 flag tokens from `build.ninja` with no divergence at all.

  The function's 172 bytes already matched the target. What differed was
  **binding**: ours `GLOBAL`, the target's `LOCAL`. objdiff pairs by name and
  scored it 100.0; dtk's report pairs local symbols by ordinal, so when the
  inline-helper change altered the object's local-symbol set it mispaired this
  function against a different one and reported 91.047%. Adding the missing
  `static` (`58dcb963`) fixed the binding, and the inline-helper change is now
  measured at **zero cost** -- no function gained or lost tree-wide, DOL
  unchanged.

  Two things worth carrying forward. `report.json` can report a *false*
  regression when a unit's local-symbol set changes, so a surprising drop on a
  function you did not touch is worth checking against the raw bytes before
  believing it. And a `GLOBAL` symbol that should be `LOCAL` is not cosmetic --
  it makes that unit's scores fragile against unrelated edits.

- **RETRACTED: "we emit inline helpers in dozens of objects, retail emits one".
  That was a methodological error, and the method is the lesson.** The target
  objects under `build/GQPE78/obj/` are not retail's compiler output. They are
  reconstructed by decomp-toolkit from the *linked* DOL, where `mwld` had
  already collapsed every weak duplicate into a single copy. So a target object
  set can only ever show **one** definition of any weak or inline-emitted
  symbol, no matter how many the original compile produced.

  Measured across three unrelated symbols, all showing the same shape:

  | symbol | target objects | ours |
  |---|---|---|
  | `__as__5xVec3FRC5xVec3` | 1 | 73 |
  | `__as__4xBoxFRC4xBox` | 1 | 7 |
  | `__as__7xSphereFRC7xSphere` | 1 | 7 |

  **Counting definitions of a weak symbol across target objects measures the
  linker, not the source.** Do not draw conclusions from it. Reference counts
  are still meaningful -- ours 856 against the target's 893 for `__as__5xVec3`
  says our call sites broadly agree -- but definition counts are not.

  Two changes were made on the strength of the bad reading before it was
  caught. `8211ea95` moved `xEntGetPos`/`xModelGetFrame` out of their headers
  into single `WEAK` definitions; it measured at zero cost with the DOL intact,
  but the premise was false, an `inline` one-line accessor in a header is what
  the original source almost certainly had, and `zLight` still fails
  `fliptest` without it. **Reverted in `be71d261`.** The second, giving `xVec3`
  a user-declared `operator=` with one out-of-line definition, was far worse
  and never landed: it makes `xVec3` non-trivially-copyable, so every struct
  containing one gets a member-wise implicit `operator=`, and a full rebuild
  measured **+1 function, -39** -- `__as__9xEntFrame` fell to 0.000%,
  `__as__13zThrownStruct` to 54%, `__as__5xBBox` to 50%. This is the exact
  inverse of the `xCollis::tri_data` finding below: a user-declared `operator=`
  on a widely-embedded value type is poison, in both directions.

  `zLight` remains unlinked and its actual blocker is **unknown**. It is 17/17
  functions at 100%, its object carries `__as__5xVec3` where the target's does
  not, and removing the other two surplus symbols did not help -- so the
  surplus-weak-symbol theory does not explain it either. Note `zVar` is
  `Matching` while carrying a surplus `__deadstripped_zVar`, so surplus symbols
  are evidently tolerable in at least some cases. Start there.

- **`xDebug` is blocked on something else and is *not* covered by the above.**
  Its 16 functions are at 100% and `fliptest` still fails. The set difference
  is our surplus `__deadstripped_xDebug` carrier -- tolerable in itself, since
  `zVar` is `Matching` carrying the same device -- plus one ordering
  difference: the target emits `__as__10iColor_tag` immediately after
  `create__5xfont`, its only caller, while we emit it two slots later, after
  `NSCREENY`/`NSCREENX`. Emission order is otherwise exactly reverse order of
  first use on both sides. Tried and failed: adding an explicit `iColor_tag`
  assignment between the NSCREEN calls and `xfont::create` in the carrier, on
  the theory that retail's deadstripped function had one -- CW folds the
  trivial copy, emits nothing, and the order does not move. This looks like a
  difference in when the compiler flushes a nested instantiation dependency,
  not something reachable from source shape.

- **A user-declared `operator=` on a union member was holding `xCollis` back,
  and it was never legal.** `xCollis::tri_data` carried a hand-written
  `operator=`, which makes `xCollis` non-trivially copyable, so
  `__as__10xEntCollis` emitted a loop calling `__as__7xCollis` where the target
  does a flat `lwz/lwzu + stw/stwu` copy of 180 8-byte units. It sat at
  **0.000%**. `tri_data` is used as a union member, and C++98 forbids a union
  member with a non-trivial copy assignment operator -- CodeWarrior accepted it
  anyway, which is why it survived. Removing it: 0.000 -> 100.000, no
  regression anywhere in a full build. **Worth sweeping for the same shape
  elsewhere**: a nested type with a hand-written `operator=` that is also used
  inside a union is both illegal and a matching blocker.

- **`xSndIsPlayingByHandle` should stay `U8`. Measured and rejected.** The
  reading that it should be `U32` -- because the adjacent `xSndIsPlaying` is
  `U32` over the same `bool`-returning `iSnd*` call, and because we emit a
  `clrlwi` retail does not -- is wrong. Changing it does not even fix
  `xSndIsPlayingByHandle` itself, and it costs `zEntPlayer_SNDPlayStream` and
  `zEntPlayer_SNDStopStream` their 100.0. Net -2. Do not retry.

- **"POOL is worth zero" has a corollary that is worth a great deal: driving a
  REAL row *into* the POOL bucket is a full `report.json` win.** These are two
  different moves and it is easy to conflate them. Realigning the pool under
  rows that are *already* POOL-only buys nothing, because they read 100.0
  already. But taking a function whose instructions genuinely differ and fixing
  the source until the only remaining difference is a pool ordinal moves it
  from below 100.0 to exactly 100.0. Measured on `zNPCGoalRobo`: one function
  reached byte-exact and twelve more became POOL-only, and the build scored
  **all thirteen**. So the instruction to agents is "do not work on POOL rows",
  never "do not let a function end up POOL-only" -- POOL-only is a finished
  function.

- **CodeWarrior emits `__declspec(section)` functions in REVERSE order of
  definition.** `Runtime/__mem.c` defined `memset, __fill_mem, memcpy` and the
  object came out `memcpy, __fill_mem, memset` against a target of `memset,
  __fill_mem, memcpy`. Reversing the definitions produced the target order,
  `symorder.py` went green, `fliptest` passed and the unit is now `Matching`.
  Declaration order in the header is *not* the driver -- that was changed first
  and moved nothing, so `__mem.h` is untouched. Worth trying wherever a
  `.init`/`.ctors`/`.dtors` section is in the right set but the wrong order.
  Note this was undiagnosable until `4647a07f` restored the `__declspec` guard
  and moved these three functions out of `.text`; it is the second unit that
  one-character fix has unblocked.

  **Do not generalise this to `.bss`.** Function-local statics are laid out in
  **ascending** declaration order, the opposite way round, and the two are
  separate mechanisms. Measured on `zEntPlayer_Init`'s four `drybob` arrays:
  declaring them `chgData, oldData, chgTime, oldTime` (matching the target's
  `.bss` order at `r31+0x6ac/0x7ac/0x8ac/0x9ac`) gives **94.829%**, reversing
  them gives **94.513%**. This entry originally claimed the reverse rule
  covered both and `52461655` acted on that; `b1d360cf` corrects it. The
  ascending order is also what `dwarf/` reports, so `dwarf/` is a usable
  cross-check for `.bss` layout but says nothing about `__declspec(section)`.

- **Never buy pool alignment with an explicit template instantiation.** In
  `zNPCHazard`, `xUtil_choose<int>` is instantiated by the target at the call
  site, so its int->float magic constant owns `.sdata2` 0x120; our build defers
  instantiation to end of TU and parks it at 0x150. Adding
  `template S32 xUtil_choose<S32>(const S32*, S32, const F32*);` at the call
  site realigned the whole `.sdata2` tail and took the unit **39 -> 33** in
  `solo.py`. It was still the wrong trade, for two reasons, and it was reverted.

  First, an explicit instantiation gives the symbol **GLOBAL** binding where the
  target's is **LOCAL** (`readelf -sW`). objdiff does not compare binding, so
  `solo.py` and `report.json` are both blind to it -- but `symorder`,
  `fliptest` and the real link are not. This is the same axis that surfaced the
  dead `__declspec`, and it is worth remembering that a device invisible to the
  metric can still be a genuine object difference.

  Second, and decisively: **it bought zero.** Measured directly by building both
  ways -- with the line, Game Code 6780 functions and 1038976 bytes; without it,
  6780 and 1038976, identical to the byte. The ten `solo.py` rows it moved were
  all already 100.0 in `report.json`, because they were pool rows. This is
  Phase 2a doing exactly what 2a was repriced to do in the entry below, and it
  is the second time a `solo.py` gain of this shape has evaporated on the
  metric. **Price data-layout work against `report.json` before accepting it,
  never against `solo.py`.**

  The corollary cuts the other way too, so check rather than assume: `xFX`'s
  missing `.rodata` strings looked like the same trap and were not.
  `xFXRibbonSceneEnter` read 99.947 in `report.json`, not 100.0, so the
  `__deadstripped_xFX` carriers bought a real function.

- **`__declspec` was dead tree-wide, and the fix completed the SDK category.**
  `include/types.h` had `#ifdef __MWERKS__ / #define __declspec(x)`. The
  original commit `06a3f860` wrote `#ifndef` -- stub the attribute for the host
  and IDE compilers that cannot parse it -- and `50c8ffa7` flipped it, which
  inverts the meaning: the attribute became a no-op under CodeWarrior, the one
  compiler where it carries information, and stayed live for the compilers that
  choke on it. Dead as a result: 24 `section` attributes, 19 `weak`, and the 23
  uses of the `WEAK` macro, which expands through `__declspec(weak)`.

  Verified against the compiler, not assumed: `__declspec(section ".ctors")`
  works unaided, an undeclared section name is a hard error, and `.ctors`,
  `.dtors`, `.init`, `.sdata2` -- every name the tree uses -- are accepted.
  `.bss`/`.sbss`/`.sbss2` are rejected, and `.ctors`/`.dtors` accept data but
  not code. So the attribute was never unsupported; it never arrived.

  Restoring `#ifndef` moved `memset`/`memcpy`/`__fill_mem` and
  `__init_hardware`/`__flush_cache` from `.text` to `.init`, moved the three
  `_reference` objects from `.sdata2` to `.ctors`/`.dtors`, and changed 32
  symbols from GLOBAL to WEAK, in every case toward the target. DOL sha1
  unchanged; 7924 -> 7935 functions; four units became linkable and
  `complete_units` went 227 -> 231. **SDK Code is now 90/90 at 100.000%
  fuzzy.**

  The general lesson: a macro that neutralises a compiler attribute is
  invisible to every diff tool here, because the object is self-consistently
  wrong. `tools/` gained nothing to detect this; the way it surfaced was
  comparing **symbol binding and section** between target and ours, which no
  existing tool did. That comparison is worth keeping -- it still reports
  ~2044 mismatches, of which 1033 are WEAK-in-target/GLOBAL-in-ours from
  header-defined functions and are a separate, unexamined lever.

- **A "cluster of near-identical functions at 99.8%" is a pool-alignment
  symptom, not a codegen problem.** `xFont` showed 14 `parse_tag_*` and 17
  `reset_tag_*` functions all at 99.7-99.9%, which reads like one missing
  source idiom repeated. It was one bad line in an unrelated function: a
  `typedef __typeof__(((struct font_asset){ 0 }).char_pos[0])` in
  `get_tex_bounds` emitted a 404-byte anonymous `.rodata` object referenced by
  nothing. Those bytes shifted every later `.rodata` offset, and since objdiff
  pairs anonymous symbols **by ordinal within a section**, every `cb$` callback
  table after it mispaired. Removing it flipped ~50 functions at once.
  `xFont` went 67 -> 9 non-matching.

  So: when many functions in one unit sit just under 100% with a single
  differing relocation each, look for a surplus or missing *data* object
  earlier in the section before looking at any of the functions. Same shape as
  the `.sbss2` entry below, and the same shape as `zNPCTypeRobot`'s `.bss`
  ordering pass.

  Note the metric consequence: report.json already scored most of those 50 at
  100.0 (it is blind to anonymous pool ids), so a 58-function solo.py gain
  showed up as far less on the project figure. **Always quote the report.json
  delta, not the solo.py delta.**

- **`classify.py` was misfiling pool-only functions as OTHER.** `norm_reloc`
  collapsed `@NNNN` but not the `$NNNN` suffix CodeWarrior appends to a
  function-local static (`npcmsg$1475`, `skipstates$1647`) -- a per-TU counter
  that differs between the two objects for the same variable. 178 instructions
  in `zNPCTypeRobot` alone carry one, and 11 of the rows the tool ranked as
  that unit's highest-value work were pool-only. Fixed in `d695e5d9`. Any
  ranking produced by `classify.py` before that commit over-states OTHER.

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
- ~~**Clause D is dead — do not refit it.**~~ **REFITTED AND INSTALLED
  2026-08-12 as E3n (`3316f9f0`, `e88c7360`). This entry was stale and cost the
  project real progress; read the correction below before believing anything
  that follows it.**

  Re-measured on today's tree the same clause is **+59/−10**, not +22/−18, and
  with eight of the ten losses recovered from source it is **+63 net**: Game
  Code 6673 → 6736 functions, exact 60.038% → 62.235%, DOL sha1 unchanged,
  `complete_units` unchanged at 89. Nothing about the compiler changed. The
  *tree* changed — most functions clause D used to damage have since been
  rewritten or converted by ordinary source work.

  **The generalisable lesson: a shelved patch verdict is a property of the patch
  TIMES the tree it was measured against. Re-measure before trusting any of
  them, including E3n itself.** Two separate investigations this month reached
  "no-go" on this clause by reasoning from the recorded numbers instead of
  re-running them.

  **The lever that recovered the losses: `const` on the destination frame
  local.** E3n's gate requires the store side to be a *declared* frame object
  (`base word0 == 0x00010005`, `[base+0x18] != 0`); `const` clears that field,
  no alias edge is created, and the retail schedule returns. Every loss was the
  same idiom — `xVec3 local = <expr>;`, a compiler-generated three-word struct
  copy sharing a block with a `.sdata2` literal load. It is semantically
  accurate wherever the local is never modified, so it is a faithfulness
  improvement rather than a hack. It does **not** work on by-value call
  arguments, nor where the local is written later (`BasisBspline`'s `Ntemp`,
  `nearestTrackCB`'s `pdx[]`/`pdz[]`).

  Two losses remain and are the standing price: `xMath3::xBoxFromCircle`
  (77.875%; recoverable to 99.375% only via an aggregate initialiser that drops
  the target's 12-byte zero `.rodata` template `@441`, so the faithful shape is
  kept) and `xSpline::BasisBspline` (96.264%).

  Also settled, so nobody re-runs it: the losses were **not** "false matches"
  that only worked by accident under the old scheduler. `ColTestCyl`'s split
  multiply and `dampen_velocity`'s comparison ladder look contorted but are
  faithful — the natural fused forms measure 82.5% and 47.8%. Roughly 45 source
  shapes were measured across the ten before the `const` lever was found.

  The original entry, preserved because its measurements were honest at the
  time: a directional rule (an `stfs` to a
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

**`xVec2::create` is now diagnosed exactly** (2026-08-12, second independent
derivation). Mapping every `.sbss2` object in the `zNPCTypeDutchman` target to
its referencing function pins the one we are missing: `@512` at `+0x000`, an
8-byte all-zero template owned by `create__5xVec2Fff`. The target body loads that
template, stores it to the frame, overwrites both words with `f1`/`f2`, and
reloads — so `src/SB/Core/x/xMath2.h:75` should read `xVec2 v = { 0.0f, 0.0f };`,
not `xVec2 v;`. Ours sits at **63.636%**, and target `.sbss2` is 19 objects
(0x98) against our 18 (0x90). `create__5xVec2Fff` is emitted in exactly one
target object project-wide, so the *symbol* blast radius is one unit — but the
parse-time `.sbss2` template risk is header-wide and must be swept.

Related, from the same sweep: only 31 units have `.sbss2` at all and 14 disagree
in size — short by 8 in `xCutscene, xScene, rpptank, zScene, zNPCTypePrawn,
zNPCTypeBossPlankton, zNPCTypeDutchman`, short by 4 in `iTRC, zDispatcher,
zEntPlayerBungeeState, zEntPlayerOOBState, zMenu, zGame, zMain`, long by 8 in
`zAssetTypes, zNPCTypeSubBoss, zNPCTypeCommon, zNPCGoals`. Note `zDispatcher` is
now 4 bytes **short**, not long — the sign flipped after the `xGrid.h` fix
(`134129c2`), so the `ZDSP_elcb_event` note in Open leads is stale.

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
- `zAnimListInit` reads `nals` through `*(volatile S32*)&nals` as a matching
  device. `nals` is not volatile in retail — the target simply reloads it after
  the store (the 2b defect). The cast buys fuzzy 98.581 -> 99.488 and **zero**
  matched functions, because volatile then forbids CSE-ing the reload into the
  following `slwi`, so the two residuals are mutually exclusive from source.
  Kept only because removing it costs fuzzy for nothing; it is not evidence.
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
  - `xDebug.cpp` — the blocker is `__as__10iColor_tag`, which xDebug **owns**,
    sitting one position too early in `.text`. Defining `__deadstripped_xDebug`
    is NOT a problem; it is what causes the ten weak inlines xDebug owns to be
    emitted at all. The trailing weak group comes out in reverse order of use
    inside that stub, so this is a statement-order question within it.
  - ~~`xParSys.cpp` emits four `operator=` instantiations the retail link
    dropped~~ — **fixed and linked, `98560c47`.** Those four were owned by
    other units and never mattered; the real blocker was `using_ptank_render`,
    which xParSys owns, emitted at end-of-TU because it was a header `inline`.
    See the rewritten 2c above for the general rule and `tools/symowner.py`.
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
