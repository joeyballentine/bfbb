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

### A SECOND witness for "what creates a literal before its first .text use?"

`zNPCTypeBossPlankton::update_move_orbit` (752 b, 99.936) is the cleanest
instance yet of the project's sharpest open problem, and it was measured to
the byte.

Every differing row is a `lwz rN, 0xNNN(r31)` whose DISPLACEMENT differs --
the code is otherwise byte-identical. Both objects anchor at `.rodata + 0`,
so those are absolute offsets for four zero-templates.

  * The target opens `.rodata` with **13 all-zero anonymous templates nothing
    references** -- `@405 @406 @410 @441` (0x0C), `@607..@613` (0x28), `@781`
    (0x0C), `@842` (0x10) = **356 bytes**. Note `@405/@406/@410/@441/@607-613`
    are the SAME ids already reproduced in `zNPCTypeRobot`.
  * Adding the sanctioned `__deadstripped_` block lands `sound_assets`,
    `beam_ring_curve`, `beam_glow_curve` and all `say_*` at the target's exact
    offsets (`beam_ring_curve` 0x340, `say_set` ending 0x42c -- both exact).
  * We are then **exactly 12 bytes short**: our templates land at 0x438,
    retail's at 0x444. Confirmed by throwaway probe -- one 12-byte dummy
    immediately before the function takes it to **100.000**. Probe removed.

Where the 12 bytes come from, and why it is blocked: the target's `.rodata`
order is NOT its definition order. `ring_to_world_vel` (`.text` 0x3834) and
`world_to_ring_loc` (later still) have their `xVec3 out = {...}` templates
created BEFORE `update_move_orbit`'s, though that function sits at `.text`
0x32c0; `register_tweaks`'s `@896` is created early and its body emitted at
`.text` index 25. Ours is definition order, and so is `zNPCTypeRobot`'s, so
mwcc is order-consistent and retail is not.

**This extends the open problem in a useful direction: it is not only
`.sdata2` scalars, it is LOCAL AGGREGATE TEMPLATES too**, and in every case
here the function's body is emitted at a call site rather than at its
definition. That is the signature of an implicitly-inline (in-class or
`inline`-keyword) definition -- which would explain `register_tweaks` if
retail defined it inside the class body, with the class placed between
`sound_assets` and `beam_ring_curve` (dwarf's declaration order agrees). It
does NOT explain `ring_to_world_vel`, whose emitted position matches its
definition position in both objects.

The `__deadstripped_` block was REMOVED per the "must move the number" rule --
its contents are verified and recorded in the agent report, one paste from
being re-added the day the construct is understood. One loose end: we emit an
unexplained 12-byte template `@254` at `.rodata` 0 that `zNPCTypeRobot` does
not have, created during header parsing and referenced by nothing.

### `zThrown_Update` cluster A: two ANTI-CORRELATED halves, 34 variants deep

3,784 bytes, 20 rows, and the most thoroughly bounded REGS case in the file
after `_xCameraUpdate`. Cluster A (16 rows, the bounce/friction reflection
loop) is a pure colouring difference that splits into two halves which cannot
be satisfied at once:

  * **GPR half (6 rows) -- SOLVED in principle.** Writing the negated headings
    as three named locals in x,y,z order (`F32 nx = -collis.colls[i].hdng.x;`
    ...) makes all six GPR rows byte-identical, including retail's
    `addi r4,r1,0x224 / addi r5,r1,0x220 / addi r3,r1,0x228` and the three
    `lfsx`.
  * **FP half (10 rows) -- then gets WORSE.** With `nx/ny/nz` as real two-use
    locals mwcc gives them fresh f6/f7/f8 and recycles the dead load registers
    for the `vel` loads; retail does the exact opposite (negations recycle
    f3/f6/f2 in place, including an in-place `fneg f2,f2`, and the vel loads
    take fresh f8/f9). The BASELINE spelling gets the negation colours exactly
    right and misses only the 3-cycle `{vel.y,vel.x,pz}`.

Net: baseline 99.794, named-locals 99.730. Thirty-four variants measured; the
two halves are anti-correlated in every partial hoist (99.736 gets the GPRs
right but commutes the `fmuls`). Retail hands its `{f7,f8,f9}` pool to the
LONGEST-lived value first (`pz`); we allocate in definition order. That is a
priority-vs-linear tie-break in the allocator, same family as
`_xCameraUpdate`.

**Two negative results from this pass that close off searches:**

  * **The residual is decided LOCALLY.** Adding an extra FP temp upstream (in
    the swept-sphere block) changed that block and left every cluster-A row
    bit-for-bit unchanged. So the "an allocator cursor set earlier in the
    function rotates everything downstream" reading is DEAD -- do not go
    hunting upstream for this class.
  * **The `zThrownCount` alias defect is NOT escape analysis.** Adding
    `U32* probe = &zThrownCount;` at file scope changed nothing at all, in any
    of the four functions. mwcc is not reasoning "this static's address is
    never taken"; it simply does not treat a store to an sda21 static as
    killing a pointer load. Note also that the same defect surfaces as
    *scheduling* in `zThrown_LaunchVel` and as *CSE* in `zThrown_AddFruit`.

Also settled here: the six `px/py/pz/tx/ty/tz` temps are real and must all be
computed before the first store (dropping them is 98.330; computing `t`
between the stores is 98.646), but their statement ORDER is completely inert
-- mwcc canonicalises it, so spend no measurements there.

### The register allocator is NOT patchable -- and here is its actual mechanism

**Do not re-open "patch the FP colour tie-break". There is no tie-break.**
Investigated 2026-08-22 by locating `Coloring.c`'s assertion strings
(`0x5bcbe8`) and following their three cross-references into the module at
roughly **`0x508680`-`0x508c60`**.

Map of the module:

  * `0x508680` -- the colouring driver. Loops over the five register classes
    (`cmp byte [esp+4], 5`), class in `byte [0x5ea299]`, per-class register
    counts at `[cls*4 + 0x5e9800]` and node counts at `[cls*4 + 0x5e9b04]`.
  * `0x508a20` -- **simplify**. Walks nodes in INDEX order, repeating to
    fixpoint; a node with degree (`word [n+0x12]`) < k goes on the stack
    (flagged `or word [n+0x16], 2`) and its neighbours' degrees are
    decremented; otherwise it goes on the spill-candidate list. If that list
    is non-empty it computes the classic Chaitin ratio at `0x508ad2`
    (`fild [n+0xc]` / `fild degree`, `fdivrp`) to pick a spill.
  * `0x508900` -- **select**. Builds the free mask by clearing each coloured
    neighbour's bit (`mov eax, 0xFFFFFFFE / rol eax, cl / and edx, eax`),
    then:

        xor ecx, ecx
        mov eax, 1 / shl eax, cl / and eax, edx
        jne -> mov word [node+0x14], cx      ; assign
        inc ecx / cmp ecx, numregs / jl

    i.e. **scan colours from 0 upward, take the first free one.** No
    preference, no coalescing hint, no cost term. There is nothing to flip.

And retail was built with this SAME binary (unpatched GC/2.0p1), so the
algorithm cannot be the difference -- only the input graph can be.

**Our patches are not the cause either, they are a large help.** The
`_xCameraUpdate` witness form measures **99.949 with GC/2.0p1a and 98.416
with unpatched GC/2.0p1**; clause C+/V/E3n are worth 1.5 points on that one
function.

### THE RULE: colour order = SOURCE DECLARATION ORDER

**This supersedes the interference-degree hypothesis below, which was wrong.**
Established 2026-08-22 by closing `_xCameraUpdate` (3,560 b) from 99.792 to
**exactly 100.0, 0 differing rows of 890**, with a rule that predicted every
one of ~270 builds.

In these blocks `k` for the FP class is far above every node's degree, so
simplify pushes every node in ONE pass in index order and select pops LIFO.
The net effect is simply:

    Values are coloured in the order they are DECLARED in the source, each
    taking the lowest colour not blocked by an already-coloured neighbour.

So the method for any REGS-class residual is mechanical, not a search:

  1. Read the target diff and note which physical register each value should
     get.
  2. Sort those values by target register ASCENDING.
  3. That is the order they must be DECLARED in.

`_xCameraUpdate` wanted `ppv`->f4, `vax`->f5, `vay`->f6, `dpv`->f7,
`vaz`->f3, so the declaration order had to be `ppv, vax, vay, dpv` with
`vaz` last (it is coloured last and takes the lowest colour the f0/f1/f2 load
temps leave free). We had been declaring `dpv` first, which pinned it to f5.

**The corollary that makes otherwise-impossible orders reachable:
DECLARATION order beats DEFINITION order.** A bare `F32 vax, vay;` declared
early but ASSIGNED later still colours in declaration order. That matters
because `vax = at.x * dpv;` cannot be *defined* before `dpv` -- but `vax` can
be *declared* before it. Use bare declarations to place a value early in the
colour order without moving its computation.

Measured ladder on this one function, all predicted correctly in advance:

    ppv, dpv, vay, vax   -> dpv=f5 vax=f7   99.949   6 rows
    ppv, dpv, vax, vay   -> dpv=f5 vax=f6   99.916  10 rows
    dpv, ppv, vay, vax   -> dpv=f4 ppv=f5   99.927   9 rows
    vay, ppv, dpv, vax   -> vay=f4 dpv=f6   99.893  12 rows
    ppv, vax, vay, dpv   -> 4,5,6,7 retail  100.000  0 rows

**Fidelity caveat, unresolved.** `dwarf/` lists this block as
`dpv, hpv, ppv, vax, vay, vaz`, and the dwarf order IS declaration order
elsewhere in this same file (it reproduces our already-matching `wcvx..psv`
and `it, ot, T_inv` orders). The 100% form contradicts it on `dpv`'s
position, and 20 builds failed to reconcile the two: with `dpv` declared
first the colouring pins at f4 or f5 across every structural spelling. Under
the rule the target permutation and the dwarf permutation are disjoint, so
no source with `dpv` first can produce retail's registers. Either the dwarf
order is not declaration order for this block, or there is one more input to
the node index. **A direct oracle exists if anyone wants it**: dwarf also
annotates `dpv // r4, hpv // r7, ppv // r1, vax // r5, vay // r7, vaz // r4`,
which are NOT physical registers (`hpv`/`vay` share, `dpv`/`vaz` share) and
so look like pre-allocation VIRTUAL register numbers. Emitting DWARF from our
own build and comparing vreg numbers would settle the node index directly.

Note the change also CORRECTS the rounding: the old
`right.x * ppv + at.x * dpv` rounds the `right.x` product and fuses the
other; retail's `fmuls f5, f0, f7` / `fmadds f5, f2, f4, f5` rounds
`at.x * dpv` and fuses `right.x * ppv`. The new source reproduces retail's
rounding exactly.

### OPEN CONFLICT: the GPR ordering key is not settled

Two passes measured the GPR key and got OPPOSITE answers. Do not treat either
as settled; test both handles on any new function.

  * `PlayerCollsSelectDepen` (zEntPlayer, 2026-08-22): a bare
    `xCollis* c; xCollis* cend;` declared early and assigned later was
    **BIT-IDENTICAL** to baseline, while moving the INITIALISER moved the
    colour exactly as predicted. Reads as: GPR key = definition point.
  * `PipeForAllSceneModels` (zScene, same day): hoisting
    `U32 remainSubObjBits;` -- a BARE declaration, no initialiser -- to the
    outer-loop top **DID** move its colour (to index 6) and put `model` on
    retail's r24, 99.176 -> 99.412. Reads as: GPR key = lexical declaration
    point.

The difference between the two experiments is that zScene's hoist crossed a
SCOPE boundary (into the enclosing loop) while zEntPlayer's stayed in the
same block. That is the obvious hypothesis -- a bare declaration may only
move a GPR's colour when it changes scope -- but it is UNTESTED. Whoever
touches this next should test it directly; it would resolve the conflict and
make the GPR case as predictive as the FP case.

**Sub-rule, measured and useful on its own:** a loop counter declared in a
`for`-init occupies the LAST slot of the declaration-ordered set. Declared
anywhere else -- outer-loop scope, if-block scope, or function scope, with or
without an initialiser -- it is EJECTED past later values (in
`PipeForAllSceneModels`, past `pipeCB` and an anonymous byte-offset temp,
from index 8 to index 10). So such a counter has exactly TWO reachable
colours. That is what makes `PipeForAllSceneModels` unreachable: retail needs
`k` at index 6, which requires it declared before `model` while STILL being a
for-init declaration -- a contradiction.

### Scoping note: check whether the function can reach 100 AT ALL first

`zSceneSetup` (3,196 b) has an FP cluster that looked like a good colouring
target. It is not worth attacking, because even with that cluster solved the
function still carries the `gCurEnv` store-then-reload row, whose only known
fix is the volatile read that is banned here (it banks zero and masks the
compiler defect). **A function with a second, independently-blocked residual
is worth zero no matter how tractable its first residual looks.** Enumerate
ALL clusters before starting.

### The alias patches do NOT cause zScene's load/store residuals -- measured

A pass suggested `zSceneInit`'s two clusters (a load moved across a store
that retail treated as a barrier) might be CAUSED by clause V / E3n rather
than merely uncured by them, which would be a cost line against those
patches. Measured directly, same source, both compilers:

    zSceneInit    GC/2.0p1a 98.203   vs  unpatched GC/2.0p1 94.729
    zSceneSetup   GC/2.0p1a 99.618   vs  unpatched GC/2.0p1 99.293
    PipeForAll…   identical in both

The patches are worth +3.5 points on the very function that raised the
suspicion. Claim disproven; do not re-open it.

**Scope note added 2026-08-22.** That disproof is about `zScene`, and it is
sound there. It is NOT a general exoneration of the patch clauses, and it had
started to be read as one. Clause E3n costs `zEntPlayer_AnimTable` **23,820
bytes** -- the largest single-function patch cost on record, and 92% of
everything removing E3n would win back. The honest framing: E3n is strongly
net-positive by function count while being byte-negative in its largest
individual case. Always price a clause both ways.

### CORRECTION: FP orders by DECLARATION, GPR orders by DEFINITION

**This corrects the corollary stated below.** "Declaration order beats
definition order" was measured on the FP class in `_xCameraUpdate` and it is
true THERE. It is FALSE for the GPR class, measured on
`PlayerCollsSelectDepen` (2026-08-22):

    bare `xCollis* c; xCollis* cend;` declared before colls/mat,
        assigned after            -> 99.818, 15 rows, BIT-IDENTICAL to baseline
    swap the two INITIALISED declarations colls/mat
                                  -> 98.662, registers swapped as predicted
    swap the two INITIALISED declarations c/cend
                                  -> 99.486, registers permuted as predicted

Moving a bare declaration is inert; moving the INITIALISER moves the colour.
So the ordering key differs by class:

    FP class   -> DECLARATION point   (bare-declaration loophole available)
    GPR class  -> DEFINITION point    (loophole NOT available)

**This reconciles the `xShadowSimple_Add` result** recorded below as "the rule
does not reach callee-saved GPRs at all": that pass moved sixteen
DECLARATION shapes, which for GPRs is exactly the inert dimension. The two
findings agree -- GPRs order by definition point.

**The GPR colour-index table is also not what you would assume.** Derived and
confirmed against two independently predicted permutations:

    colour index:  0    1    2    3    4    5    6    7
    register:     r27  r29  r28  r26  r30  r31  r25  r24

Values are coloured in definition order, each taking the lowest-index free
colour. It is NOT descending r31, r30, ... Anyone reasoning about GPR colours
must use this table, not intuition.

**`PlayerCollsSelectDepen` (1,868 b) is ARITHMETICALLY UNREACHABLE, proven.**
Target wants `c`=r25, `cend`=r26, `idx`=r30; we get r26, r30, r31. Sorted
ascending, the target order is `c, cend, idx` -- which is ALREADY our
definition order. It is a uniform shift within the free list, not a
permutation. The loop-1 trio interferes only with `{ent, colls, mat}`
(r27/r28/r29, identical on both sides), and everything else holding
r24/r25/r26/r30/r31 has a disjoint live range and can never block it. So the
trio always takes colour indices 3,4,5 = {r26, r30, r31} in whatever order
their definitions appear. Retail's {r25, r26, r30} = indices 6,3,4 SKIPS
index 5 (r31), which requires a coloured neighbour holding r31 -- and no such
neighbour exists in our graph. **Retail's source creates one extra
interference across loop 1 that ours does not.** That is an interference-graph
difference, not an ordering one, and it is the crisp falsifiable statement of
what retail's source must do: keep a value live across loop 1 that shares r31
with the loop-2 iterator.

### THE RULE, REFINED TWICE (2026-08-22, xShadowSimple)

Two refinements from closing `xShadowSimple_CalcCorners` (484 b) to
**100.000, 0 of 121 rows** and from failing on `xShadowSimple_Add`. Both
change how to apply the rule, so read them before the LIMIT section below.

**1. NAMING AN ANONYMOUS TEMP PULLS IT INTO THE DECLARATION-ORDERED SET.
This is the escape from the limit.**

`CalcCorners` wanted `dydz`-CSE=f5, `bx`=f6, `dydx`-CSE=f7; we had `dydx`=f6
and `bx`=f7. Declaration placement of `bx`/`by` was byte-identical in four
separate spellings, because they colour AFTER two anonymous merge-block
scratch temps. The value actually out of place was the anonymous
`cache->dydx` CSE temp. Binding it to a named local:

    F32 dydz = cache->dydz;
    F32 dydx = cache->dydx;
    ...
    ay = ax * dydx + az * dydz;

pulled it into the ordered set, and `bx`/`by` then snapped to retail's f6/f7
BY THEMSELVES (99.752, 6 rows). The rule then predicted the last step
arithmetically -- retail wants `dydz` below `dydx`, so declare `dydz` first --
and that measured **100.000 first try**.

So when the mis-coloured value is an anonymous CSE temp, do not permute the
named locals around it: NAME IT. That is also a faithful change in its own
right (a member read repeatedly is a plausible local in the original).

Note this also corrects the LIMIT's wording below: the obstructing temp here
came from a LATER statement yet was coloured EARLIER, so "an earlier
statement" is not the right test. The right test is simply whether the
mis-ordered value is anonymous -- and the answer is now to name it.

**2. THE RULE GOVERNS VOLATILE REGISTERS, NOT CALLEE-SAVED GPRs.**

`xShadowSimple_Add` (1,176 b) needs a permutation of six values across
r26-r31 -- both objects save exactly r26-r31. **Sixteen source shapes moved
NOT ONE callee-saved register**: `shadowWas` first, `castOnEnt` first, `j`
first, a full 14-position sweep of `j`, dwarf declaration order, `U8`->`U32`
on `moved`, `shadowWas` scoped into its own branch, all else-branch locals
scoped, and a named `xShadowSimpleQueue*` element pointer.

In the SAME function the volatile GPRs obey the rule exactly: `vert` and the
polygon-loop `j` trade r7/r6 purely on declaration order, earlier taking the
lower. That is what splits the `j` sweep (positions 0-3 give 99.541/25 rows,
4-13 give 99.320/37, the twelve extra rows being only `vert`/`j` swapping).

So callee-saved GPRs are assigned by a different mechanism that declaration
order does not reach. **Before starting a REGS residual, check which register
class the mis-coloured values are in.** FP and volatile GPRs are workable;
a permutation confined to the callee-saved GPR set is not, and
`xShadowSimple_Add` is the measured witness.

### THE RULE'S LIMIT: it orders NAMED LOCALS only, not anonymous temps

Established on `zThrown_Update` (3,784 b) over ~320 builds, 2026-08-22. The
function did NOT close and was reverted; the boundary condition is the
deliverable, and it tells you when to stop.

**The rule was confirmed here**, including the bare-declaration loophole:

  * `F32 px, py, pz;` declared x,y,z but ASSIGNED z,x,y is byte-identical to
    plain x,y,z -- declaration order beats definition order.
  * The rule correctly predicted `d`->f1, `px`->f3, `py`->f6 from the
    baseline declaration order.

**But its domain is named locals among themselves.** Anonymous expression
temporaries created by an EARLIER statement are ordered separately and always
precede the named locals of later statements. No declaration placement
reaches them:

    3 bare decls before `d`, assigned after          99.794, cluster A = 16
    same, comma-declared                             99.794, A = 16
    same, at FUNCTION scope                          99.794, A = 16
    named vx/vy/vz after the p's, used later         99.794, A = 16

all bit-identical to baseline. The confirmation that the named locals stay
behind: if they were coloured first, `px` would take f0; it takes f3 in every
build, because `OPB(f0)`, `d(f1)`, `nZ(f2)` are always coloured first.

Why that blocks this function. Cluster A is NOT "two anti-correlated halves"
as previously recorded -- it is ONE rotation in two register classes. Retail's
FP order is
`OPB(f0) .. d(f1) .. nZ(f2) .. nX(f3) .. OMF(f4) .. VZ(f5) .. nY/py(f6) ->
pz(f7) -> vel.y(f8) -> vel.x(f9)`; ours is identical except `pz` sits AFTER
the two `vel` temps instead of before. That single swap produces all 16 rows,
GPR bases included. To fix it `pz` must be declared before the two
`thrown->vel` temps -- but those are anonymous temps created inside the `d`
expression, and `pz = -hdng.z * d` cannot exist before `d`. **The required
order is not expressible.**

Two escapes were measured and both fail: naming the vel values and declaring
them after `pz` (CSE binds them to the temps `d` already created --
bit-identical), and removing `d` as a variable so its temps belong to `px`'s
statement (still ahead of `py`/`pz`; that is the 99.804 / A=14 form).

Also settled here: retail's `pz` is NOT computed in place (`fneg f2,f2` then
`fmuls f7,f2,f1`), which proves `nz` and `pz` are separate values in retail
and that the p/n fusion forms are wrong.

**Practical test before spending a session on a REGS residual:** work out the
required colour order, then ask whether every value in it is a NAMED LOCAL
whose declaration you can move. If the order requires placing a named local
ahead of an anonymous temp created by an earlier statement, stop -- it is not
expressible.

### Superseded: the interference-degree reading

Because select is lowest-free in stack order, a value can only receive a
HIGHER colour than another if the lower colour is **already taken when it is
coloured**. So when the target gives value X a higher register than we do,
retail coloured its competitor Y FIRST. Colouring pops LIFO off the simplify
stack, so Y was **pushed LATER** -- meaning Y kept degree >= k through more
simplify rounds, or sits later in node index order.

Concretely for `_xCameraUpdate`: retail has `dpv`->f7, `vax`->f5; we have the
reverse. So retail colours `vax` before `dpv`, i.e. **`vax` is pushed later
than `dpv`**. The lever is therefore `vax`'s INTERFERENCE DEGREE, not its
statement position -- which is why a ~950-build sweep over statement
orderings, operand orders and accumulate masks bottomed out at 6 rows without
touching it. Lengthen `vax`'s live range, or shorten `dpv`'s, so `vax`
survives more simplify rounds.

That rule applies to every REGS-class case in this file, including
`zThrown_Update` cluster A (retail hands `{f7,f8,f9}` to `pz` first; we
allocate in definition order) and `zThrownCollide_ThrowFruit`.

### `_xCameraUpdate`: 3,560 bytes behind ONE binary FP colour tie-break

The single cheapest compiler-side witness currently known. `_xCameraUpdate`
(3,560 b) sits at 99.792 in the tree; a clean, non-contorted source form
reaches **99.949 with exactly 6 differing rows**, and those 6 rows are a
literal two-register swap: retail colours `dpv`->f7 and `vax`->f5, we get
`dpv`->f5 and `vax`->f7. Everything else in the block is byte-identical --
same mnemonics, same operand positions, same instruction count, same branch
shapes, same frame, same callee-saved set.

`dpv` and `vax` interfere. After excluding the colours pinned by `at.x`(f0),
`at.z`(f3), `right.x`(f2), `right.y`(f1), `ppv`(f4) and `hpv`(f6), the only
two left are f5 and f7. Their live ranges, interference degrees and use
counts are identical on both sides, so there is no source-visible
discriminator -- it is one bit in the allocator's tie-break.

The search that establishes this is not a spot check: 288 builds sweeping
pv-order x va-order x accumulate mask, 144 more adding both operand-order
flips, and 500 random topological orderings of all 15 statements in the
block. **The floor of ~950 builds is 6 rows.** A separate probe also proved
that a change AFTER the block cannot alter the block's colouring, which
bounds where any fix could live.

The 99.949 form is NOT in the tree: it banks zero (only 100.0 counts) and it
contradicts `dwarf/` on two declaration orders, so it would trade fidelity
for nothing. But if anyone extends the compiler patch to the FP colour
tie-break, this is 3,560 bytes for one bit, with the source form recorded in
the agent transcript.

### Entry 4: the decisive test is whether SOURCE ORDER MOVES THE ROWS AT ALL

Two more entry-4 confirmations in `xCollide` (2026-08-22), and one of them
supplies the cleanest diagnostic yet for telling entry 4 apart from an
ordinary source-order problem.

`xSphereHitsOBB_nu` (99.849): **writing the three stores in the source as
`y, x, z` produces BYTE-IDENTICAL output.** The scheduler emits `y, x, z`
whatever the source says, so no spelling can reach it. That is the test to
run first on any transposed-store pair -- permute the source statements and
see whether the emitted rows move. If the output is bit-identical under
permutation, the reorder is happening below the source level and you are
looking at entry 4; stop. If the rows DO move (as in `xParabolaHitsEnv`,
where swapping made it 98.569 and moved a different row), you are at least
in contact with the scheduler, though it may still be unreachable.

Also measured and rejected on these two: binding `xVec3& N = data.N;` across
the whole block (byte-inert), and `(o)->assign(0.0f, 1.0f, 0.0f)` (96.109).

### `xSweptSphereToBox`: SOLVED as a diagnosis -- not expressible, and here is why

2,448 b, 99.158, 29 rows of 614. Re-worked 2026-08-22 with the colouring
rule. It does not close, and two things previously recorded here were WRONG.

**The real emission rule for this block** (replaces the old "defect 1 +
defect 2" reading, and every measured variant fits it):

    The nine (load, fmuls) pairs are emitted in ASSIGNMENT-statement order,
    with the value belonging to the FIRST STORE statement moved to the END
    of that list.

  base (assign asc, first store `aXx`) -> emitted `aXy..aZz, aXx`   99.158/29
  assignments reversed, stores asc     -> emitted `aZz..aXx`        99.098/31
  store `boxaZ.z` first                -> ascending, no move        99.163/24
  stores fully reversed                -> ascending, no move        99.144/26
  declaration list rotated one left     -> BIT-IDENTICAL to base

**CORRECTION 1: "temp assignment order is irrelevant (inert)" was FALSE.**
Reversing all nine assignments measures 99.098 / 31 rows and reverses the
emitted load order. The old "inert" reading came from moving only `aXx` to
last, which happens to produce the identical emitted list under the rule
above.

**CORRECTION 2: "defect 2 (a one-instruction interleave offset) exists" was
FALSE.** It was an artifact of the odd store order in the `boxaZ.z`-first
probe. With the stores fully reversed, the loads, the `fmuls`, the f8->f0
product colours AND the load/fmuls interleave are ALL byte-identical to
retail -- the entire computation half of the block matches. Only the nine
stores (in that variant's reversed order) and `dy`/`dz` differ.

**CORRECTION 3: "`dy`/`dz` in f22/f23 is downstream of the rotation" was
FALSE.** In the fully-reversed variant the rotation is gone and the products
carry retail's f8..f0, yet `dy`/`dz` are STILL f22/f23 against retail's
f26/f27. It is an INDEPENDENT second defect. Both sides save f21-f31 with
`rad`=f26 and `radsqr`=f25 identically, so retail has four values coloured
before `dy` that interfere with it and not with `dx`; nothing in our graph
does. That is the `PlayerCollsSelectDepen` shape -- an interference-graph
difference, not an ordering one.

**Why it is NOT EXPRESSIBLE.** Two constraints collide. (1) Emitted store
order tracks source store order, adjusted only for value-readiness --
demonstrated directly, since fully reversing the source stores fully reverses
the emitted stores. Retail's emitted stores are
`0x54,0x58,0x5c,0x48,0x4c,0x50,0x3c,0x40,0x44` and the values become ready in
exactly that order, so retail's source store order is the natural ascending
one with `boxaX.x` first. (2) The rule then moves the first-stored value to
the end of the load/fmuls list -- the rotation. To cancel it the first-stored
value must be `aZz`, but making `boxaZ.z` the first store forces its `stfs`
to be emitted third, or forces the whole store block into reverse. The
scheduler cannot defer a store that is first in source order and ready. So
retail's store order and a non-rotated load order are mutually exclusive
under this compiler.

**The DWARF form is the real source and confirms the diagnosis.**
`dwarf/` lists `dx, dy, dz, rad, radsqr, testdist, invZ, boxPos, boxaX,
boxaY, boxaZ` and NO `a??` temps at all. It measures 96.842 with a fully
serial `lfs/fmuls/stfs` chain through f0 (binding `const xVec3&` to the three
matrix rows is byte-identical to it, so that lead is closed too). Retail's
compiler hoisted nine loads over nine stack stores from that source; ours
will not. **The nine temps are a workaround for the load-hoist alias defect,
and the workaround is what injects the rotation.** If that predicate is ever
widened, start from the fully-reversed-stores variant, then delete the temps
entirely and use the DWARF form.

Note the colouring rule's LIMIT explains why declaration order is a live
lever for the products' colours but a dead one for emission order: these
single-assignment single-use locals are copy-propagated away, so the ordered
set that matters is the anonymous one -- and there is no anonymous temp left
to name, because naming them is exactly what the baseline already does.

### symorder's "SAME SET, WRONG ORDER" is not evidence on its own

`tools/symorder.py zPlatform` reports `.sdata2: SAME SET, WRONG ORDER`, and a
2026-08-21 pass read that as a fourth witness of the "dead constants created
early" problem alongside `zThrown`, `zShrapnel` and `zNPCTypeRobot`, with the
conclusion that the unit "will not link byte-identically even at 24/24".

**That conclusion is contradicted by the DOL hash and must not be repeated.**
`build/GQPE78/main.dol` is byte-exact against
`306526d90b48e99894c3138f5fc8f2716d9fecf6`. The shipped image therefore
already contains the correct `.sdata2` bytes for this unit; a real pool defect
here would make the whole DOL differ.

Two things make the report misleading for a unit like this, and both are
visible in the same output:

  * `.text: different sets` -- our object also carries inline/weak functions
    retail dead-strips (`__as__5xVec3`, `xVec3SMulBy`, `xModelGetFrame`,
    `xEntERIs*`, `xBoundCenter`, ...). Those extras own pool entries, so the
    two objects' pool ORDINALS are not comparable directly.
  * the only ordering difference is where the single 8-byte magic double sits
    among the 4-byte entries, and the 8-byte object is 8-aligned, so the
    alignment padding absorbs the difference and both land at the same offset.

objdiff agrees: the agent confirmed the relocation targets are identical
(`.sdata2` offset 0x58 on both sides). So references resolve to the same
place.

Before treating a symorder pool warning as real, check the DOL hash first,
and check whether `.text` sets differ. It is genuine evidence only for units
whose object is otherwise a set-match.

### The switch-tree "pivot convention" was WRONG. It is missing `case` labels.

**DISPROVEN 2026-08-22, and this is the correction that matters most in this
file.** `zEntPlayerEventCB` was closed to 100.0 by adding four `case X:`
labels that fall straight to `break`. There is no pivot defect and no
compiler patch to write -- do not spend a session on one.

What the earlier pass got right: our emitted case values, the case-body
order, and the body offsets were all identical to retail, and all 70
differing rows were in the search tree. What it got wrong was the inference.
It concluded both compilers partition identically and differ only in choosing
max-of-lower vs min-of-upper as the pivot. The real cause is that **retail's
case LIST was longer than ours**. Cases whose body is just `break` emit no
body and no `cmpwi` of their own, so they are invisible in a value-set
comparison -- but they change the shape of the tree CW builds.

The tell was there all along and was read past: **the target's dispatch was
one instruction LARGER than ours** (108 vs 107). A pure pivot-selection
difference cannot change the instruction count. A longer case list can.

Diagnostics, now that the shape is understood -- use these on any large CW
switch whose value set, body sizes and body order match but whose tree does
not:

  * the target's dispatch is BIGGER than yours -> you are missing labels.
  * a pivot `cmpwi V` with **no** `beq`: CW elides the equality test when the
    pivot tops a consecutive run of default-mapped cases and emits the
    boundary as `max+1`. So V-1 is the top of a run you have not written.
  * a **four**-instruction leaf `cmpwi / beq body / blt default / b default`
    where you emit three: that node has an all-default sibling child.
  * a lone default-mapped case shows up as a stray `beq <default>`; pairing
    it with its neighbour removes that row.

Add one probe case at a time and watch which subtree snaps into place. On
`zEntPlayerEventCB` fourteen case-set combinations localised it to exactly
{51, 52, 284, 285}; a single case at 285 scored 99.488 and at 286 scored
99.489, both carrying the stray `beq`, and only the two-element run [284,285]
removed it.

**Source POSITION of an empty case has zero effect on codegen**, so it cannot
be recovered from the object -- put such labels wherever reads best.

Still open on this shape: `zPlatformEventCB` (3,192 b). Note its residual was
separately measured and is NOT this defect -- zero of its rows are in the
search tree; all 12 are in case bodies, one motif across the six
`eEventRot*` cases. So the two big EventCB functions had two different
causes, which is exactly why the "deep switches cost us everywhere" reading
never held up.


Weigh that against the cost: locating median selection in a 6 MB stripped
binary with no debugger is open-ended, unlike clause E3n which was a
one-byte change at an address an agent had already pinpointed. Left as a
lead, not scheduled.

One thing it DOES settle: `xEvent.h`'s enum is correct. The case values match
retail exactly, so any future "the event enum is shifted" reading of that
diff is wrong and must not be acted on.

### A THIRD alias lead: pointer loads killed by a store to a file-scope static

`zThrown_LaunchVel` (91.934) and `zThrown_AddFruit` (88.657) share one root
cause, and it is the cleanest compiler-side witness pair currently on the
board. Both do:

```c
newThrown = &zThrownList[zThrownCount];
zThrownCount++;
newThrown->killTimer = stats->carry->killTimer;
```

Retail issues the `lwz` of `stats->carry` **after** the `stw` of
`zThrownCount` -- the store to the static kills the load. Our mwcc proves
`zThrowableModels` and `zThrownCount` cannot alias, and then either hoists
the load above the store (`LaunchVel`) or reuses the value already computed
for the earlier `stats->carry != &c_fruit` test (`AddFruit`). Every other
differing row in both functions -- the r7/r8 and r8/r9 renumbering, the
li/lis scheduling swaps, the extra live register -- is downstream of that
single decision.

This is adjacent to but OUTSIDE clause V. Clause V kills the literal pool
across a store to a small static; what is wanted here is killing
**pointer-dereference loads** across a store to a file-scope static.

Two things make this a good candidate if anyone extends the alias predicate:
856 bytes across two functions, and **no masking to unwind first** -- a
whole-variable `volatile` on `zThrownCount` is firmly disproven by the
target (it breaks six other functions in the unit and creates four new
non-matching ones), so nothing has been papered over here.

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
    zNPCTypeBossSandy sElbowDropThreshold -- masks a DIFFERENT defect: it
                      suppresses cross-block constant CSE (LICM), not
                      store-to-load forwarding. Kept because it is the closer
                      form (99.364 with, 99.349 without) and Process is
                      blocked by entry 4 regardless, so neither form banks
                      bytes. Revert it before measuring any LICM fix.

**ADDED 2026-08-22: `zEntPlayer.cpp` `bbash_tmr`**, at the single use site in
`zEntPlayerJumpUpdate` (`if (*(volatile F32*)&bbash_tmr >= 0.0f)`). It
qualifies -- it takes that function (1,460 b) all the way to exactly 100.000%
-- and it must be reverted before any measurement of a compiler-side fix.

**ADDED 2026-08-22: `iModel.cpp` `sEmptyAmbientLight`** (in `iModelInit`, 192 b)
**and `instance_camera`** (in `iModelStreamRead`, 628 b). Each takes its
function all the way to exactly 100.000% and each carries an in-source comment
saying it is a matching device. Three honest spellings were measured against
each (`*&`, cast, temp-then-assign) and all were bit-identical to baseline.

Two sites deliberately NOT added in the same unit, both by the same rule:
`zEntPlayerFloorUpdate`'s three store-then-reload clusters (`surfSlickTimer`,
`surfSlipTimer`, `surfFriction`), because a fourth entry-4 subrange x subrange
cluster at `0xa8/0xac(r1)` caps that function below 100.0 regardless; and
`zEntPlayer_Init`'s `drybob_anim_count`, where the device does not even reach
the site -- introducing an index local, with or without a volatile read, is
**bit-identical to baseline** because mwcc copy-propagates it away.

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

### VERIFIED: `zEntPlayer_AnimTable` is source-correct; clause E3n breaks it

Measured 2026-08-22, and then re-measured independently by me rather than
taken on report. 23,820 bytes, the largest single non-exact game function.

    zEntPlayer_AnimTable__Fv   tree (GC/2.0p1a)     97.249%
                               stock GC/2.0p1      100.000%
                               entry-3 ablated     100.000%
                               control (chk)        97.249%

The control is the whole patch rebuilt from pristine `2.0p1` by
`patch_compiler.py`'s own constants; it reproduces `PATCHED_SHA1`
`19480c5d...` byte-for-byte, so the ablations differ from the shipped
compiler by exactly one dispatch entry and nothing else.

**Nothing in `zEntPlayer.cpp` needs to change.** All 452 differing rows sit in
blocks that store a call result into the frame arrays `tranTbl1`/`tranTbl2`
(`stw r3, 0x18(r1)`, `0x1c(r1)`, ...). E3n pins that `stw` ahead of the
block's `lfs @NNN@sda21` / `lwz 0(rN)` static loads. Retail sinks it thirteen
instructions, to just before `mr r3, r31`, which keeps **r3 live** across the
whole argument setup; retail's allocator therefore cannot use r3 as scratch
and takes r4/r5/r6/r9, while ours frees r3 immediately and takes r3/r4/r5/r8.
One root cause, 452 rows.

**Re-priced tree-wide** (all 224 `main/SB/*` units, `2.0p1a` vs entry-3-on-
clause-C, solo basis): removing E3n is **+6 functions / +25,924 bytes** and
**-107 functions / -77,224 bytes**. `zEntPlayer_AnimTable` is 23,820 of the
25,924 -- 92% of the entire win. Other winners: `MoveNormal__14zNPCGoalPatrolFf`
716 b, `BasisBspline__FPA4_fPf` 576 b, `Process__18zNPCGoalJellyBirth` 348 b,
`xBoxFromCircle` 256 b, `get_bounds` (xFont anon) 208 b. Biggest losers:
`Process__12zNPCBPatrickFP6xScenef` 6040 b, `_xCameraUpdate` 3560 b,
`ConfigHelper__9NPCHazardF9en_npchaz` 2940 b, `Process__8zNPCTikiFP6xScenef`
2380 b.

**E3n stays.** But the narrowing is now the single largest identified win on
the board. NOT YET ACHIEVED: it means writing new x86 into the cave against
struct fields nobody has identified, and a guessed gate recorded as a rule is
worse than no rule.

**Both obvious gates are DEAD. Measured, three witnesses:**

    zEntPlayer_AnimTable     stw  into a 32-byte frame array     over-fires
    zLightningFunc_Render    stfs into a stack array element     over-fires
    turning__12zNPCDutchmanCFv  stfs/stw into an 8-byte xVec2    over-fires
    E3n's 19 motivating wins stfs to a scalar frame local        CORRECT

- **Store-opcode class (float vs integer) is dead:** the over-fire cases span
  both `stw` and `stfs`.
- **Frame-object size is dead:** the third witness is an 8-byte aggregate,
  which sits *between* the scalar wins and the arrays. No threshold at 4 or 8
  separates them. I promoted this gate on two witnesses and the third refuted
  it -- recorded so nobody re-promotes it.

**That hypothesis is dead too, and for an instructive reason.** I proposed that
the over-fire cases are PARTIAL writes to a multi-field frame object where the
motivating wins write a WHOLE scalar. Measurement: `[memrefA+0x2c] == 1`
(subrange) and `[memrefB+0x2c] == 0` (whole) at **every** E3n site, winner and
loser alike. Partialness is already fixed by the dispatch entry -- entry 3 *is*
subrange x whole -- so it cannot possibly discriminate within entry 3. A
whole-scalar store never reaches clause E3n at all.

**THE NARROWING ATTEMPT FAILED, and the failure is conclusive rather than
incomplete.** A dedicated session read the actual field values out of the
compiler's structures rather than guessing:

- **Frame-object size: FALSIFIED, not merely unfound.** Repros with declared
  frame arrays of 8, 12, 16, 24, 32 and 64 bytes plus 12- and 32-byte structs,
  reading every dword of the store's base-expression node from +0x00 to +0x68
  by bisection: **all eight shapes read identically in every field.** The
  descriptor carries no size, no element count, no array flag, and no
  reachable `Type*`-with-size.
- **Store opcode class: dead, and worse than thought.** A "FP stores only"
  gate (`opcode >= 50`) costs four zEntPlayer winners on its own
  (`CheckObjectAgainstMeleeBound`, `zEntPlayer_Damage`, `update_camera`,
  `WallJumpCallback`). Three need E3n to fire on `stb`/`sth`; `WallJumpCallback`
  needs it on **`stw`** -- the same opcode as the over-fire.
- 204-probe grid over every dword of memrefA/memrefB/insnA/insnB in both
  directions at six thresholds; 64-probe scan of the base node 0x00-0xa4; a
  full opcode ladder on both operands; and a sibling-alias-list-length gate.
  **Not one probe reached loser=100 with the winners kept.** The alias list
  length is exactly 1 at every site, winner and loser alike.

Both populations are real: stock `GC/2.0p1` keeps **0 of 11** zEntPlayer winners
while giving AnimTable 100.000. One rule must split them and **nothing in the
state clause E3n is given does**.

**So any real fix must change WHAT THE PREDICATE IS GIVEN, not what it tests.**
Two openings, neither attempted: put the gate in the dependency-graph builder
at `0x508100`/`0x508350`, which unlike `0x511fc0` can see the pending
load/store lists; or extend the base-object node, which is a compiler-wide
allocation change rather than a cave patch.

**Source-side mitigation that works today:** `const` on the read-only frame
aggregate cleared the gate entirely on the third witness -- 87.870% patched
before, 99.259% under every compiler after. Verified by checking out the
pre-fix blob and measuring both ways.

### zLightning: BOTH functions blocked. Do not send another agent at this file.

`RenderLightning` (2,948 b, 99.028%) and `zLightningFunc_Render` (1,580 b,
96.886%) were worked hard on 2026-08-22 and neither is source-reachable.
**Identical instruction counts on both sides** in both functions (737/737 and
395/395) -- there is no missing `case`, no wrong operand, no fused-vs-rounded
multiply anywhere. Every differing row is register renaming or ordering.

- `RenderLightning`: 106 rows in four clusters, the largest (63 rows) being a
  single cyclic rotation of the volatile register pool by two positions.
  **The decisive fact:** the function's FIRST `for (i = 1; i < last; i++)` loop
  is byte-exact, including `srwi. r0` for `flip` and `clrlwi. r5` for `i & 1`.
  The SECOND loop is the same source shape, and retail allocates it
  *differently from its own first loop* (`srwi. r3`, `clrlwi. r0`) while our
  compiler makes the same choice in both. Retail's compiler is not consistent
  with itself across two structurally identical loops in one function, so no
  single source spelling can satisfy both -- and we already match the first.
- `zLightningFunc_Render`: a callee-saved GPR permutation (r16/r17 and
  r24/r25/r26), which `xShadowSimple_Add` already established is not reachable
  from declaration order, plus an entry-4 same-array subrange store reorder.

Twelve source variants measured, all <= baseline; the two best were
bit-identical and the rest lost ground (worst: removing the `cr/cg/cb` temps,
-5.7 points).

**E3n over-fires here too** (measured with `verify_mw.py`): `zLightningFunc_Render`
is 96.886% patched and 98.635% under both stock `2.0p1` and `2.0p1a-no3`,
with entries 0, 1 and V all inert. It does not reach 100 either way so it is
not bankable, but it is the SECOND witness that E3n's over-firing is real and
recurring -- and its store site is `stfs` into `param[]`, an element of a stack
**array**, matching zEntPlayer's `stw` into the `tranTbl` **array**. Both
over-fire sites are frame arrays; E3n's motivating shape is a frame **scalar**.
That is the evidence behind the frame-object-size gate proposed above, and it
is evidence *against* a store-opcode-class gate, since one witness is an
integer store and the other a float store.

### The GPR scope hypothesis is now TESTED, and the answer is NO

The "OPEN CONFLICT" section below asks whether a bare declaration moves a GPR
colour *when it crosses a scope boundary* -- `PlayerCollsSelectDepen` said no,
`PipeForAllSceneModels` said yes -- and marks it untested.

It is now tested, from the other direction. Moving three initialised `U8`
declarations in `RenderLightning` from **function scope into an inner block** --
a real scope change with the definition point held fixed -- is **byte-identical**:
same percentage, and every objdiff relocation index unchanged, with only
anonymous pool ids renumbering. **A scope change on its own does not move a GPR
colour.** Whatever produced the `zScene` result depends on something else, and
the scope-crossing hypothesis should not be carried forward as the explanation.

### CAVEAT: naming an anonymous temp does not always pull it into the ordered set

These notes record "give the value a name" as *the* escape from the rule's
limit, on the strength of `xShadowSimple_CalcCorners`. It is not unconditional.
In `RenderLightning` the mis-coloured values `cg`/`cb` are **already** named
locals; the rule says defining them first should order them first; and defining
them first (two separate variants) moves only `cr` into the low pool while
`cg`/`cb` stay at r23/r24. So mwcc's live range for a local that is just a load
from a global does **not** reliably start at its source definition point -- it
behaves as though rematerialised at the use. Naming is necessary, not sufficient.

### Do not optimise on DIFFERING ROW COUNT. Use the percentage.

objdiff gives partial credit per instruction, so the two disagree. A measured
case: a `zLightningFunc_Render` variant produced **fewer** differing rows (43 vs
45) at a **lower** percentage (96.486 vs 96.886). Row counts are for cluster
bookkeeping only; `match_percent` is the thing report.json is built from.

### More dwarf counter-evidence (add to "dwarf is not an oracle")

For `zLightningFunc_Render`, dwarf lists a single function-scope `signed int i`
where four separate `for`-init counters measure 0.46 points better, and its
declaration order `numVerts, u, aVal` measures 0.05 points worse than our
`alpha, tex, nvert`. Separately it lists **no** `cr/cg/cb` locals at all, yet
removing ours costs 5.7 points -- so the GameCube build needs temps the PS2
DWARF has no name for. On the other side of the ledger, `dwarf/SB/Game/zLightning.cpp`
gives a third and fourth witness for the `RwRGBA* _col` macro form, listing
exactly one `class RwRGBA * _col;` per `RwIm3DVertexSetRGBA` invocation (12 and
4 respectively), matching our call sites one-for-one.

**Retail quirk, verified faithful rather than ours:** `RenderLightning`'s second
`for` loop has no `else { lastdir = dir = up; }` arm where the first loop does,
so in the `flags & 0x200` case it computes `flip` from values left over from the
first pass. Instruction counts match exactly, so retail shipped that asymmetry.
Flagged for PCPORT, not to be "fixed" here.

### The load-hoist-over-`stw` defect IS source-reachable. Retry it.

These notes file `xFX::eval_joint` as compiler-track: "predicted to flip and
did not... the surviving defect is presumably the `stw`, so the store side of
the clause declines there". `zCameraUpdate` has the same shape -- a small-static
`lfs` hoisted above the `stw r0,0x30(r1)`/`stw r3,0x34(r1)` pair of mwcc's own
int-to-float conversion scratch -- and **it is reachable from source**.

Binding the conversion result to a named local blocks the hoist:

    F32 dp = (F32)(MAX(32, MIN(x, 110)) - 32);
    dp = 0.016666668f * (dp * zcam_pad_pyaw_scale);

Four sites, **+0.95 pp**. The `fmuls` operand order corrects as a consequence.
**Retry this anywhere an `lfs <named global>` sits at the head of a clamp block
instead of at its use site.** Do not assume the store side of a clause
declining means the function is compiler-track.

**REFUTED FOR `eval_joint` SPECIFICALLY (2026-08-22, same day).** I predicted
the transfer in this very section and it does not hold. Binding the conversion
results to named `F32` locals in `eval_joint` is **bit-identical** (same diff
sha1), as are `(F32)` casting, bare-decl-then-assign, and adjacency changes.
Structural permutations do move rows (82.865, 64.250), so it is not entry 4 --
the baseline is simply already the best form.

The reason is worth more than the lead was. `eval_joint` is 105 rows,
byte-identical except that the `lfd` of the u32->float magic double sits after
`stw r5,0x14(r1)` in retail and five instructions earlier in ours. The pair is
(8-byte **whole** `.sdata2` load) x (**whole** 4-byte declared frame local
`alpha`), which dispatches to the **whole x whole** entry -- and E3n lives on
entry 3. The old note guessed "the store side of the clause declines"; the
truth is the clause is never *consulted*. Precisely-shaped candidate: **E3n's
rule applied to the whole x whole entry**, worth 416 b.

Generalise the method, not the fix: check which dispatch entry a pair actually
reaches before predicting that a source lever will move it.

### The E3n `const` lever is a GAIN lever, not only loss-recovery

These notes frame `const`-on-a-frame-local as the thing that recovered E3n's
ten regressions. It is more general. `const xVec3 tran_accum = cam->tran_accum;`
moved `zCameraUpdate` **+0.6 pp** with no E3n regression anywhere in sight --
and the `const` is correct on its own merits, since the local is never written,
only read at five sites. The idiom to scan for is `xVec3 local = <expr>;`
sharing a block with `.sdata2` literal loads, regardless of E3n history.

### TWO PRECONDITIONS on the "name the anonymous temp" escape

Found independently by two agents on the same day, in different units. The
escape (from `xShadowSimple_CalcCorners`) is recorded here as *the* way out of
the rule's limit. It has preconditions, and both were measured:

1. **The named temp must be genuinely multi-use.** In `zCameraFreeLookSetGoals`
   the mis-coloured value is a single-assignment, single-use accumulator;
   naming it is copy-propagated away and is byte-inert (two spellings, measured
   twice).
2. **Naming is necessary, not sufficient.** In `RenderLightning` the
   mis-coloured values are *already* named locals, and defining them first
   moves only the first of the three into the low pool. A local that is only a
   load from a global behaves as though rematerialised at its use, so its live
   range does not reliably start at its source definition point.

### REFINEMENT: the bare-declaration loophole needs a SINGLE live range

"DECLARATION order beats DEFINITION order... use bare declarations to place a
value early in the colour order" did **not** hold for `zCameraFreeLookSetGoals`'s
`newPitchGoal`: four bare-declaration positions, one of them crossing a scope
boundary, are all byte-identical. The variable is reassigned on every path, so
it splits into separate single-def values that colour at their definitions --
i.e. it behaves like the GPR case.

Corrected wording: *the bare-declaration loophole reaches a local with a single
live range. A local reassigned on every path colours at each definition and the
loophole is inert.*

### 2b census correction: `zCameraUpdate` would NOT cross 100.0

These notes carry a standing instruction to re-count how many functions would
actually **cross** 100.0 before spending a session on the store-to-load
forwarding fix. `zCameraUpdate` (3,892 b) is a five-site 2b witness and must be
counted as a **no**: a `volatile`-read probe on all five sites measures 99.681%
with 19 rows left, and those 19 are four REGS clusters that survive the fix
(an `f2`/`f3` transposition in the `dlerp` block, `r4`/`r5` between
`lassocam_enabled` and the pad byte, a uniform +1 colour shift in the lassocam
d/h lerp, and one `fmuls` destination). The probe was removed rather than left
installed at sub-100, per the install-only-at-100.0 rule.

### Refuted: clamp-literal early colour is NOT CSE with an earlier literal

The hypothesis that a clamp literal gets its early colour by sharing a pool
entry with an identical literal earlier in the function is **wrong**.
Substituting a wholly distinct literal (`1e-30f`, a different pool entry) in
`zCameraFreeLookSetGoals` left the colouring bit-identical.

### dwarf is wrong about `zCamera`'s pad locals, in a checkable way

`dwarf/SB/Game/zCamera.cpp` lists the pad stick locals as `signed int x` /
`signed int y`. The GameCube target keeps the raw byte in a register and
re-issues `extsb` at each use, which is `S8` behaviour, not `S32`. **Our `S8 x`
is right and DWARF is not.** Add to the "dwarf is not an oracle" list.

### zCamera: what is left, and why

`zCameraUpdate` 98.988% -- blocked, see the 2b census entry above.
`zCameraFreeLookSetGoals` 99.714% (7 rows) -- unfinished, not proven blocked.
Byte-identical until the first `fmadds` of the velocity dot product: retail
accumulates in place (`fmadds f0,f3,f2,f0`, dest = third operand) and gives the
`0.0f` clamp literal f7->f2, while we colour the literal f0 first, pushing the
accumulator to f2 and shifting everything downstream. **20 spellings measured**
(accumulate form, named chain, `MIN` macro, `if`-clamp both polarities,
`0.0f - x`, `*= -1.0f`, right-association, term reorder, reciprocal,
`const xVec3&` binding, fresh local, three `newPitchGoal` positions); every
faithful one is bit-identical, every distorted one is worse.
`zCameraFlyStart` 94.850% (4 rows) -- our scheduler hoists `lwz r0,0x1c(r1)`
from the frame local `info` above `stw r0, zcam_flypaused@sda21`. Frame-load x
static-store is exactly what clause C's static-storage gate excludes on purpose.
NOT entry 4 -- source permutation does move the rows -- so it is in contact with
the scheduler, but no faithful spelling reaches it.

### SECOND patch-cost witness, on entry 0: `xFXRenderProximityFade`

Verified by me, not taken on report. 1,612 bytes, `src/SB/Core/x/xFX.cpp`:

    tree (2.0p1a)      95.273%      2.0p1a-no1     95.273%
    control (chk)      95.273%      2.0p1a-no3     94.814%
    stock GC/2.0p1     99.504%      2.0p1a-noV     95.273%
    2.0p1a-no0        100.000%

Note the shape, which is unlike `zEntPlayer_AnimTable`: it reaches 100.0 ONLY
with **entry 0 ablated and the rest of the patch still on**. Stock is 99.504
and the full patch is 95.273, so the other clauses are worth +0.5 here while
entry 0's clause costs the last stretch. **Do not touch this `.cpp`** -- the
source is correct.

**Do not ablate entry 0 either.** Within xFX.cpp alone that trade is +1/-10
(losing `activate_ribbon`, `xFXShinyRender`, `xFXRingUpdate`,
`xFXFireworksUpdate`, `render_strip`, `xFXBubbleRender`, `xFXAuraAdd`,
`xFXShineUpdate`, `xFXStreakStart`, `xFXStreakUpdate`). Entry 0, like E3n, is
a **narrowing** target.

So there are now two independent dispatch entries with measured over-fire
costs. When a near-100% function resists source work, run the full matrix --
`tree / stock / no0 / no1 / no3 / noV / chk` -- not just stock.

Clause V has two cost witnesses in this unit as well, neither bankable:
`xFXShineRender` 90.425 -> **98.219** and `xFXStreakRender` 65.481 -> **93.415**
under `noV`.

### xFX: what is blocked, with the stop tests run

Six functions in this unit are completely patch-insensitive (identical under
all seven compilers) and therefore pure source-track: `RenderRotatedBillboard`,
`eval_joint`, `tri_data::init`, `MaterialSetEnvMap2`, `get_normal`, and both
`xFXanimUV*SetAngle`.

- **`xFXanimUVSetAngle` / `xFXanimUV2PSetAngle`** (92 b each, 83.478). One
  instruction moved: retail issues `stfs f1, xFXanimUVRotMat0@sda21` into the
  `icos` return shadow, before both `li` address materialisations; ours issues
  the ready `li`s first. **All 24 store permutations measured**; the natural
  `abcd` order is what the source has and the best distorted form is `dcba` at
  90.870. Same multiset, same registers. SCHED, blocked. Fixing one fixes both.
- **`tri_data::init`** (164 b, 97.439). Callee-saved GPR permutation (r27-r30).
  `vi`-before-`v`, no-reference and bare-pointer forms are all bit-identical to
  each other -- inert, confirming the `xShadowSimple_Add` rule that declaration
  order does not reach callee-saved GPRs.
- **`MaterialSetEnvMap2`** (180 b, 95.444). 3 rows: two adjacent independent
  instructions swapped (`mr r31,r4` vs `lis r4,@stringBase0@ha`), the r5-vs-r4
  choice a consequence of r4 still being live. Same shape as the recorded
  `iPadUpdate` SCHED case.
- **`RenderRotatedBillboard`** (1,440 b, 99.389). The **entire** residual is 44
  rows of 360, **all `lbz`/`stb`**: the four colour bytes get r0,r3,r4,r5
  ascending in retail and r5,r4,r3,r0 in ours, at six sites. All 16 subsets of
  naming the macro arguments were swept. Naming `_r` is always inert
  (copy-propagated). **Naming `_a` alone -> 99.694**, fixing alpha *and* green
  and leaving only r<->b. Naming all four fixes the RGBA cluster but trades it
  for a whole-function callee-saved permutation (98.181). This is direct
  evidence on the open question of whether the real `RwIm3DVertexSetRGBA` macro
  binds `_a` to a temp: **it does something that binds `_a`.** That is a
  shared-header change in `include/rwsdk/rwcore.h` and needs a tree-wide sweep
  before anyone believes it.

### `get_normal`: a REAL numerical defect, kept even though it banks zero

`xFXRibbon::get_normal` (432 b) 90.046 -> 99.769, from two genuine source
defects, both confirmed against the target's own asm:

**Retail rounds each square to single before adding; we fuse.**

    8002AABC  fmuls f2, f8, f8
    8002AAC0  fmuls f0, f7, f7
    8002AACC  fadds f5, f2, f0

Our `dir.y * dir.y + dir.z * dir.z` compiles to `fmuls` + `fmadds`, keeping the
first product at full internal precision. That is a real difference in shipped
arithmetic. Naming the two products forces the rounding. **Flag for PCPORT, and
do not "simplify" the named products back into one expression** -- they are
load-bearing for numerical fidelity, not for the percentage.

Also: retail loads `dir.x/y/z` into f9/f8/f7 and reuses them **in arm 1 only**
(arms 2-3 reload in retail too, and binding them in all three arms measures
78.935). The final +0.46 came from the bare-declaration loophole, `F32 dz, dy,
dx;` then assigning `dx,dy,dz` -- the FP rule predicted the target's ascending
dz=f7, dy=f8, dx=f9 exactly.

**This was initially reverted on the "install only at 100.0" rule and that was
wrong.** That rule exists to stop *hacks* being left in at sub-100 (volatile
probes, dead code to shift a pool). It does not cover faithful source. A change
that makes our arithmetic bit-match retail's belongs in the tree whether or not
it banks a function -- correctness outranks the percentage, in both directions.

Blocked at 4 rows: an f5/f6 swap between the anonymous `-a` temp and the
`dy2+dz2` sum. Naming the sum is *worse* (98.935, three spellings), and
operand permutations (`(dy2+dz2) * -a`, `(0.0f-a)*...`) are bit-identical --
canonicalised.

### HAZARD: a stale `.bak` in the shared scratchpad silently reverts a file

This actually happened on 2026-08-22 and cost real time, so it is written down
in full. `src/SB/Core/x/xFX.cpp` was found mid-session holding the blob of
commit `4e30d04`, **two commits behind HEAD**, silently discarding `e1b2c66`
and `ae45307`. Measured cost while it was in that state: 16 non-matching
instead of 12, with `DrawRing` knocked from 100 to 89.674.

**Cause.** A variant harness stored its baseline at
`<scratchpad>/xFX.cpp.bak` -- a generic name in a directory the agent believed
was session-private -- behind this guard:

    BAK = ".../scratchpad/xFX.cpp.bak"
    if not os.path.exists(BAK):
        shutil.copyfile(SRC, BAK)      # <-- silently reuses a STALE file

`xFX.cpp.bak` already existed, left by an **earlier session's** xFX agent whose
work predates those two commits. The guard declined to overwrite it, so the
harness's "baseline" was two-commit-old content, and its first `restore()`
wrote that over HEAD's.

**Why it is nasty:** a reverted file shows in `git status` as an ordinary
` M src/...`, indistinguishable from a live edit. Nothing warns you.

**Two mitigations, both cheap:**
1. Harnesses must derive their baseline from `git show HEAD:<path>`, never from
   a filesystem snapshot taken at unknown time.
2. Namespace scratch files per agent (`scratchpad/<agent>/`), never a bare
   `<unit>.cpp.bak` at the top level.

The scratchpad currently contains this exact hazard for several other units --
`xCollide.cpp.bak`, `zScene.cpp.bak`, `iSnd.cpp.orig`, `iSnd.cpp.base`,
`zLightning.cpp.base`, `zLightning.cpp.orig`, `zNPCTypeRobot.cpp.orig`,
`zCamera.cpp.orig`, `zThrown.cpp.orig`, `xFX.cpp.bak` -- all of unknown vintage.
**Assume every one of them is stale.**

**Gating rule this forces.** When gating an agent's work, do not accept that a
file changed; read the diff and confirm it is the change the agent described.
A silent revert and a real edit look identical in `git status`, and only the
diff distinguishes them. Also attribute report.json deltas to units: a build
picks up every modified file in the tree, including other agents' in-flight
work, so a gain measured after a build is not necessarily the gain of the unit
you are gating.

### The scratchpad is SHARED between concurrent agents, not per-session

Two agents running in the same session get the same scratchpad directory, and
one overwrote another's `vary.py` mid-run. **Namespace scratch files** under a
per-agent subdirectory. Brief agents accordingly.

### `const` on a read-only local aggregate: a lever that keeps paying

It paid **five separate times in one unit** (zNPCTypeDutchman), worth +3.5 to
+11 points each, and it cleared an E3n over-fire outright. These notes framed
it as the 12-byte three-register `xVec3` copy form. It is broader:

- it applies to **8-byte `xVec2`** as well;
- it applies to aggregates whose initialisers are **non-constant expressions**,
  where CW still copies an anonymous zero template in before overwriting.

**Scan for `xVecN local = {...};` that is never written afterwards.** The
`const` is correct on its own merits in every such case, so this is a free
correctness-and-percentage lever, not a trade.

### Permuting sibling `const T&` binding declarations is a cheap mechanical lever

Six builds, ~15 seconds, and it closed two functions. `play_sound` went
96.458 -> 100.000 by swapping two of three reference declarations (2 of the 6
permutations hit 100), and `Initiate::Enter` 99.394 -> 100.000 by moving one
reference below two others. Worth trying before any deeper analysis on a
function whose residual is REGS and whose head is a run of reference bindings.

### CORRECTION: `LassoNotify`'s dead branch IS source-reachable

These notes list it as "one unreachable branch instruction... documented dead
ends, not open puzzles". Too strong. Adding `case LASS_EVNT_BEGIN: break;`
**does** produce the second `b` and takes it 96.429 -> 99.393. It still does not
close, because that case also reshapes the pivot tree (`cmpwi 3/beq/bge/cmpwi
2/bge` becomes `cmpwi 2/beq/bge/cmpwi 0/beq`). Correct framing: *the dead branch
is reachable, but not simultaneously with the target's tree.* The change was
reverted because the label is invented and it banks nothing.

### RE-PRICE the `xVec2::create` header change before spending a session on it

The `.sbss2` entry names five functions "whose *entire* residue is that shift",
`clip_outside_circle` among them. **`clip_outside_circle` (xVec2 overload) was
closed to 100.000% from source alone, with no header change.** So whatever holds
`create__5xVec2Fff` (44 b, 63.636%) back is not a shift that also gates its
neighbours, and the "five functions unblocked by one header change" pricing is
wrong. Re-derive it.

### Lead: retail's `xatan2` may return `double`

`update_turn__12zNPCDutchmanFf` (260 b, 94.308%, 8 rows): the target emits an
extra `frsp f0, f31` narrowing `angle` before `angle + diff`; we forward `f31`
unrounded. Eight source shapes measured, all <= baseline.

A redundant `frsp` is what CW emits when the value's producer is **double**-typed.
So the lead is that retail's `xatan2` returns `double`, not `F32`. The mangled
name `xatan2__Fff` encodes only parameters, so the return type is invisible to
the linker and this is testable without breaking anything. `xMathInlines.h` is
shared, so it needs a tree-wide sweep. **Unfinished, not blocked.**

### dwarf, both directions, in a single unit

Decisive and CORRECT three times in zNPCTypeDutchman: `update_wave` has no
`tanx`/`tanz`, `update_flames` no `gx`/`gz`/`tanx`/`tanz`, `Initiate::Enter` no
`ox`/`oz` -- inlining each was part of every fix. WRONG twice in the same unit:
`add_spray`'s dwarf omits `mult`, yet naming `mult` *and declaring it first* is
exactly what reaches 100.000%; and `check_player_damage`'s dwarf lists no `xBox`
locals at all though the GC target plainly has two on the stack. Use it as a
hypothesis generator, never as an oracle.

### THE DISPATCH TABLE, DECODED. This settles which clause can ever see what.

Read out of `0x511fc0` rather than inferred. Given two instructions:

    memrefA = [insnA+0x18]        memrefB = [insnB+0x18]
    index   = [memrefA+0x2c]*3 + [memrefB+0x2c]
    kind byte at +0x2c:  0 = whole object,  1 = subrange

So the table at `0x5bd0bc` is indexed by operand *wholeness*, and the entries mean:

    entry 0 = whole    x whole
    entry 1 = whole    x subrange
    entry 3 = subrange x whole
    entry 4 = subrange x subrange

**Consequences, several of which correct these notes:**

- **Clause E3n's store operand is ALWAYS a subrange**, never a whole object.
  Both this file's "an `stfs` to a **scalar** declared frame local" and
  `patch_compiler.py`'s "an `stfs` to a declared frame local" are wrong. A
  whole-scalar store cannot reach E3n.
- **E3n's winners are not all `stfs`.** At least three zEntPlayer winners need
  it on `stb`/`sth` and one on `stw`.
- **`eval_joint` sits on entry 0** (whole 8-byte `.sdata2` load x whole 4-byte
  frame local), confirmed from the formula rather than surmised. "E3n's rule on
  the whole x whole entry" is therefore a genuinely NEW clause, not a
  relocation of this one.
- **Entry 4 being permanently blocked now has a structural reading**: it is
  subrange x subrange, i.e. two partial accesses, which is exactly the
  same-array-different-elements shape.

**memref layout** (from stock entry-4's overlap test at `0x512012` and clause
V's list walk):

    +0x00  value-number list next     +0x1c  value number
    +0x08  sibling-list head          +0x24  alias bitmap
    +0x0c  computed-address flag      +0x28  alias bit index
    +0x10  base-object node           +0x2c  kind byte (0 whole / 1 subrange)
    +0x14  subrange offset
    +0x18  access size

**instruction layout:** `+0x10` latency, `+0x14` flags, `+0x18` memref,
`+0x20` opcode id (16-bit).

**The opcode id table is at VA `0x5c3070`** -- 791 entries, 20-byte stride
`{char* mnemonic, u32 id, char* operand_format, u32 mask, u32 encoding}`.
`lwz`=34, `stb`=40, `sth`=44, `stw`=49, `lfs`=142, `stfs`=150, `stfd`=154.
Verified twice over: the encoding fields match real PPC primary opcodes, and a
threshold ladder's flip points land exactly on 49 and 150.

### AVAILABLE BUT NOT INSTALLED: 24 free cave bytes

Cave space is the binding constraint on every future clause and only 8 bytes are
free. Three cave blocks re-implement code the stock compiler already contains:
`stock0` open-codes `0x511FF2`, `may` open-codes `0x512081`, and `e13h`'s tail
open-codes `0x511FFF`. Replacing them with jumps takes the cave **428 -> 412,
freeing 24**.

Measured: all 224 `main/SB/*` units compile to **byte-identical objects** under
the shipped `2.0p1a` and the compacted build (0 differing, 0 failed). Compacted
compiler sha1 `bf72c2e4180ebef769deb949c63cad4ecd7c6f24`; bytes in
`scratchpad/e3n/compact_cave.hex`.

**Deliberately NOT installed.** It changes `PATCHED_SHA1`, which invalidates
every agent's variant compilers and forces a re-patch mid-flight, and there is
no clause waiting on the space. Install it when a clause actually needs the
room, not before.

### Reusable compiler-RE assets in `scratchpad/e3n/`

Do not rebuild these from scratch:
`cave.py` (symbolic re-assembler for the whole cave, validated to reproduce
`CAVE_BYTES` byte-for-byte), `mkvar.py` (variant builder), `probe.py`/`scan.py`
(zEntPlayer oracle -- one compile yields both the loser and 11 winners),
`read.py`/`fieldread.py` (sub-second repro oracle that reads real field values
by bisection), `mnem.py` + `opcodes.json` (the decoded opcode table), and
`hashsweep.py`.

Also confirmed here: rebuilding from pristine `74bc177b...` twice gives
`19480c5dcb2c3de3b870c1fb29db73f14f7b2889` both times, so `PATCHED_SHA1` is
correct and the patch is deterministic; and the recorded E3n price of
+6/-107 functions and +25,924/-77,224 bytes reproduces exactly.

### `solo.py`'s no-symbol mode returns EVERY symbol, not just non-matching ones

A harness that assumed otherwise gave a false reading for an iteration. Filter
on `match_percent < 100.0` yourself if you write against it (this is what
`verify_mw.run(..., None)` returns too).

### NEW LEVER: `j = i, i++` -> `j = i++`. It LOOKS like SCHED and is not.

`PointWithinTriangle` (672 b) closed to 100.000% on this alone, at three sites.
The loop was `for (i = 0, j = 2; i < 3; j = i, i++)`; the target emits
`addi i,1` **before** `addi ptr,4` and we emitted them the other way round.
The comma form cannot reach it; `j = i++` can.

**Why this matters beyond one function:** the residual presents as *two adjacent
independent instructions swapped*, which these notes elsewhere tell you to
classify as SCHED and stop on. That guidance is right in general and wrong
here. **Before writing off a transposed `addi` pair in a loop increment as
blocked, try re-spelling the increment.**

### dwarf's PS2 sibling for iCollide: right twice, wrong three times

`dwarf/SB/Core/p2/iCollide.cpp` exists and is the PS2 counterpart of the GC
file. Decisive-CORRECT on `iSphereHitsModel3` and `sphereHitsEnv3CB` (local sets
match ours exactly). Decisive-WRONG on `FindNearestPointOnLine` and both ray
functions: it lists no `dx/dy/dz`, no `sx/sy/sz` and no `RwV3d temp` in
`iRayHitsEnv`, yet the GC target's asm proves all of them must exist. Another
entry for "dwarf is not an oracle" -- hypothesis generator only.

### iCollide: what is left, and why

The unit is patch-insensitive where it matters: `PointWithinTriangle`,
`FindNearestPointOnLine`, `iRayHitsEnv` and `iRayHitsModel` are identical under
all of `- / 2.0p1 / no0 / no3 / noV`. `sphereHitsEnv3CB` and `iSphereHitsModel3`
*gain* 4-5 points from the patch. Nothing in the unit reaches 100 under any
variant, so it is pure source track.

- **`iSphereHitsModel3`** 98.913% (920 b) -- BLOCKED. All 9 rows are in the
  64-bit `collide_rwtime += t1 - t0` accumulate: same multiset, different
  registers, and the target interleaves `stw` between `addc` and `adde`.
  `= a + (b-c)`, `+= named delta` and reversed operands are all bit-identical
  (canonicalised); split-into-two is 96.174. dwarf's local list matches ours.
- **`sphereHitsEnv3CB`** 97.427% (1,228 b) -- three residual shapes. (a) A
  `mr r31, rN` at the three `idx = X = cbnumcs++` sites: retail keeps the load
  in a scratch and copies into `idx`'s callee-saved register, we coalesce;
  three spellings bit-identical. (b) Retail reloads `NEXT2` after storing it and
  reloads `cbnumcs` after `cbnumcs--` where we forward + `clrlwi` -- the
  store-to-load forwarding defect, `volatile`-only, not installed. (c) Two
  f0/f1/f2 transpositions.
- **`FindNearestPointOnLine`** 97.903% (248 b) -- UNFINISHED, one FP tie-break.
  Naming `dx/dy/dz` fixed the load order; also naming `sx/sy/sz` snapped both
  register groups onto the target's (`dx,dy,dz`->f4,f5,f6; `sy,sx,sz`->f7,f8,f9)
  and `dx * mu` fixed the operand order. What remains is `mu` getting f1 where
  retail gets f2, with both candidates dying at the `fsubs` -- the
  `_xCameraUpdate` one-bit shape. Four declaration positions byte-identical.
- **`iRayHitsEnv`** 94.139% / **`iRayHitsModel`** 91.824% -- ONE shared root
  cause, blocked. 51 of 58 rows in the former are a callee-saved permutation:
  we materialise `&isx.t.line.end` early (`addi r31, r1, 0x20`) and hold it
  across six calls; retail materialises it at the swap. In `iRayHitsModel` that
  costs a fifth callee-saved register (`stmw r27` vs the target's four) because
  retail reuses r28 for `mat` and then `&end`. **The `addi` is hoisted across
  `bl` instructions, so it is IR-level, not the scheduler.** Seven source shapes
  measured; baseline is best.

### WARNING: the `const` lever can INVERT an E3n verdict. Re-measure old ones.

Measured on `DiscoRender__10zNPCSleepyFv` (zNPCTypeRobot):

    before `const vec_ray`:   tree 72.850   stock 76.904   no3 76.904
    after  `const vec_ray`:   tree 81.053   stock 75.016   no3 75.016

Before the fix this is a textbook E3n over-fire. After it, **the over-fire is
gone and E3n is +6 points on the same function.** The `const` removes the
declared-frame-object store that E3n was gating on, so the clause stops firing
there at all.

**Consequence: any E3n over-fire recorded BEFORE the const lever was applied to
that function is suspect and must be re-measured.** A patch-or-source verdict is
only valid for the source as it stood when the matrix was run. Re-run the matrix
after every source change that touches a frame aggregate.

### SECOND entry-0 over-fire witness: `Setup__11zNPCFodBzztFv`

Verified here, not taken on report. 288 b, `src/SB/Game/zNPCTypeRobot.cpp`:

    tree 95.347 | stock GC/2.0p1 100.000 | no0 100.000 | no3 95.347 | noV 95.347

**Source-perfect; do not touch it.** Note it is NOT quite the same shape as
`xFXRenderProximityFade`, which needed `no0` *specifically* (stock gave only
99.504). This one reaches 100 under stock and `no0` alike, so it is a plain
entry-0 over-fire rather than a patch-interaction case.

Ablating entry 0 is not free even locally: within `zNPCTypeRobot` alone the
trade is +1/-2 (`RendConeOfDeath` 89.515->87.767, `RendConeRange`
87.884->87.054). Entry 0 remains a narrowing target, not an ablation target.

A **fourth E3n witness**, not bankable but worth the file: `RendConeOfDeath`
(808 b) is tree 90.777 / no3 92.762 / stock 91.163. Callee-saved GPR
permutation underneath, so it never reaches 100 either way.

### METHOD: compare the instruction MULTISET before reading the diff at all

The single most productive move of a recent session. Normalise registers and
pool ids, then compare the multiset of instructions on each side:

- multisets **equal** -> pure REGS/SCHED. Go straight to the allocator and
  scheduler sections; do not read the diff line by line looking for a source
  bug that is not there.
- multisets **differ** -> a real source difference, and the *difference itself*
  names it (an extra `fmadds` against a `fmuls`+`fadds` pair; a missing switch
  dispatch; an extra `mr`).

It separated three real-source cases from six pure REGS/SCHED cases in about a
minute each, and it surfaced a fused-multiply defect that reading the diff
top-to-bottom had missed. Script at `scratchpad/robot/ms.py`; solo.py's column
split is at character 50.

### OPEN LEAD: the construct that keeps a fully-degenerate switch alive

`DoAliveStuff__11zNPCTubeletFf` (384 b, 91.667%) is a SIZE difference with a
understood cause and an unknown cure. The target emits a live 6-instruction
switch dispatch (`lbz 0x84(r3)` / `cmplwi 1,beq` / `cmplwi 2,beq` / `cmplwi 4`)
whose case bodies are **all empty** -- every arm branches to the instruction
after the switch. The switched-on value is dead in retail too (the `AdjustHome`
third argument is a `.sdata2` literal, not `wid`). Our mwcc deletes the entire
switch once dead-store elimination empties the bodies.

Ruled out: writing the cases explicitly empty (91.667, unchanged), adding
`default:`, and grouping BOX/OBB. The two extra `lwz` reloading
`drv_data->driver` afterwards are a *consequence* -- the live switch splits the
basic block and kills the CSE. **The construct that keeps a degenerate switch
alive in CW is unknown.** Whoever finds it also gets the reload behaviour free.

### More dwarf counter-evidence (zNPCTypeRobot)

Wrong twice: `zNPCTubelet::ParseChild`'s dwarf lists a `zNPCTubeSlave* slave`
local, and writing it **costs** 1.07 points (96.395 -> 95.326).
`zNPCSleepy::RendConeRange`'s dwarf entry is a *different function* -- it has
`rad_fadeinInner`/`rad_fadeinOuter` statics and none of `pos_top`/`pos_bot`/
`rgba_*`, so the PS2 and GC bodies genuinely diverge there. Correct once, and
decisively: `TurnThemHeads`' missing `pos` local is what closed it.

### zNPCTypeRobot: remaining classification

- `Unbonk` 116 b 99.586 -- **BLOCKED**, the known epilogue `lwz r31`-before-
  `lwz r0` class; 3 permutations, baseline best.
- `ParseChild` 344 b 96.395 -- **BLOCKED**, callee-saved permutation
  (`xShadowSimple_Add` class), 4 shapes all <= baseline.
- `NightLightUVStep` 200 b 60.700 -- **BLOCKED**, entry 4: `lfs uv_nightlight[1]`
  hoisted over `stfs uv_nightlight[0]`, i.e. subrange x subrange. Rewriting
  `+=` as `x = x + y` measures 65.800 and narrows it to that one hoist, but was
  NOT installed: that is not a fidelity question and it banks nothing.
- `ConeOfRange` 324 b 95.802 -- unfinished. We hoist an extra `lfs 0.0f` to the
  head of the `MAX(0.0f, MIN(pct,1.0f))` clamp to fill the `fdivs` latency;
  retail loads it once at the use site into f31 after `rad2` dies, serving as
  both compare operand and result. 4 spellings, none moved it.
- `DiscoUpdate` 468 b 95.769 -- unfinished, same shape (two
  `uv_discoLight[i]` loads hoisting above `stb rgba_discoLight.alpha`),
  patch-insensitive across all five compilers.
- `RendConeOfDeath` 808 b 90.777 -- near-blocked. FP set is a clean descending
  declaration-order sequence except `sn`, defined inside the loop, colours
  third; bare-declaration hoists are byte-inert exactly as the rule predicts for
  a value redefined every iteration. Confirmed again here: naming the four RGBA
  macro arguments does NOT buy retail's all-loads-then-all-stores order when the
  temps are single-use -- they are copy-propagated away.
- `RendConeRange` 964 b 87.884 -- unfinished. `pos_vtx` and `vec_ray` occupy
  each other's frame slots (target 0x10/0x1c, ours 0x1c/0x10) but the
  reverse-declaration lever does NOT reach it: three placements cost 4.5 points,
  scoping buys +0.05, `const pos_bot` is -2.0. 8 shapes.
- `DiscoRender` 748 b 81.053 -- unfinished, improved 8.2 points. Remaining is
  the setup block's load order plus retail keeping `mem` and `vert_list` in two
  registers (`mr r26,r27`) where we coalesce.

### NEW COMPILER-TRACK SHAPE: rematerialise-vs-copy (`mr`)

Four independent witnesses in `zEntPlayer.cpp`, each otherwise byte-identical.
Retail keeps a value in a register and copies it with `mr`; our compiler
rematerialises it instead:

- `GetPatrickTarget` -- retail `mr r23, r27` vs our `li r23, 0`, with
  `li r27, 0` present and live in **both** objects
- `zEntPlayer_Update` -- retail `li r14,0 / mr r16,r14` vs our two `li`
- `get_reticle_bound` -- retail holds `addi r30,r29,0x94` across a call vs our
  two displacement loads
- `SpatulaGrabCB` -- retail `mr r4,r31` vs our `addi r4,r1,0x14`, and retail
  saves one more callee-saved GPR as a result

**This is neither the alias/reload defect nor the scheduler.** No source form
measured reaches any of the four (eight spellings on `get_reticle_bound` alone
are bit-identical or worse). Name it and count it before anyone spends a
session trying to spell around it.

### CORRECTION: E3n's store side is ALWAYS a subrange, so subrange-ness proves nothing

`zEntPlayer_Render`'s residual (8 rows) is a `lwz` of the 4-byte static
`gPTankDisable` hoisted above three `stfs` into a declared frame `xVec3` --
and E3n declines on it, while being worth +1.17 points elsewhere in the same
function (`no3` measures 96.849).

The agent reporting this read it as evidence for the "whole vs partial memref"
hypothesis, on the grounds that the store side is a subrange (`center.x`)
rather than a whole scalar. **That inference is invalid.** Per the decoded
dispatch formula, entry 3 *is* subrange x whole -- being a subrange is what
gets a pair TO clause E3n in the first place, so it cannot discriminate within
it. The hypothesis stays dead.

The *observation* is still a real puzzle and worth keeping: E3n is consulted
here and declines for a reason not yet identified, on a pair that satisfies
every clause condition as documented. Whoever revisits the clause should start
by finding out why.

### `PlayerTeeterCheck`: clause V's blind spot is VN table entry 1

444 b, 78.649%. A fully-unrolled 4-iteration loop where retail reloads
`0.424264f`, `0.2f` and `0.0f` per iteration and we hoist all three. The
killing stores are `stfsx`/`stfs` into the `floor_tmr[]` **array** -- subrange
memrefs, which dispatch to **VN table entry 1**, and clause V patches only
entry 0. Matches the documented `xFXAuraUpdate` finding exactly. Compiler-track,
and a concrete second site for anyone extending clause V.

### CAVEAT on the `MAX(k, expr)` signature entry

These notes carry a flat reading from `zEntHangable` that "no source shape keeps
the dead branch alive". In `CalcCombinedDepen` the target's second clamp emits
`ble L / b L2 / L: fmr` -- an empty then-arm -- and **`MAX(0.25f, dot2)` does
produce that two-branch form**. So the form is reachable and the flat reading is
too strong. But it produces it with the operands and destination register
transposed, while `if (...) {} else {...}`, `if (a < k)`, `if (a <= k)`,
`!(a > k)` and the self-ternary all collapse to a single `bgt`/`bge`. Correct
framing: *the two-branch form and the target's register map are individually
reachable and mutually exclusive here.*

### Lead: the callee-saved FP ordering key is not the volatile-FP rule

`CalcJumpImpulse_Smooth` (680 b, 88.147%): instruction multisets identical,
frame identical, callee-saved set identical (f18-f31). A pure REGS/SCHED
permutation on **callee-saved FP** registers. Notably **our ascending register
order is the exact reverse of our declaration order**, which does not match the
volatile-FP declaration-order rule at all. Worth a dedicated pass by anyone
testing whether callee-saved FP has its own ordering key.

### zEntPlayer: other classifications

- `zEntPlayer_SNDPlayStreamRandom` (1,076 b, 99.108) -- a `0.0f` literal we keep
  across basic blocks / hoist out of a loop where retail reloads it. **No
  intervening store**, so it is global CSE / LICM, not clause V. The existing
  source comment already had this right.
- `zEntPlayer_Update`'s empty `for (U32 i = 0; i < sc->num_npcs; i++) {}` --
  retail does not unroll it and reloads `sc->num_npcs` each iteration, with a
  second `i*4` induction variable surviving; we hoist the bound and unroll by 8.
  `S32` counter, `while` form, dead element load and dead element-pointer are
  all bit-identical to baseline. An unrolling/LICM decision, not source.
- `zEntPlayer_Init` (3,392 b, 95.660) -- 140 rows dominated by the unrolled
  `drybob_anim_count` loop, retail reloading the static after its own increment
  store where we forward.
- **`zEntPlayer_SNDInit` re-measured on the current tree**: 91.947 with the tree
  compiler and *worse* under every ablation (stock 88.457, no0 91.592, no3
  90.005, noV 90.519). Consistent with the correction already in this file, but
  the numbers are now current. It is not a clause-V win waiting to happen.
- The whole unit is patch-insensitive in the sense that matters: the tree
  default is best or tied for **every** function in it. Nothing here is
  already-correct source -- except `zEntPlayer_AnimTable`, which is.

### NEW LEVER: the `fmr` copy trio -- "modify the original FIRST, the copy second"

Three unexplained `fmr` instructions in the target next to an in-place
`fadds`/`fsubs` pair on the same value mean the source **copies a variable,
then modifies the ORIGINAL first and the COPY second**:

    ax = tx;  ay = ty;  az = tz;
    tx -= dx; ty -= dy; tz -= dz;   /* must come FIRST */
    ax += dx; ay += dy; az += dz;

**The statement order is load-bearing.** Writing the `+=` before the `-=` lets
copy propagation fold `ax = tx; ax += dx` back into `ax = tx + dx` and all three
`fmr`s vanish -- 86.136 against 93.491 on the same function.

Worth 9.1 points on `iRenderPushQuadStreak` and 8.1 on two more in the same
unit. It presents as **extra instructions in the target**, so the
instruction-multiset test catches it, but the fix is not obvious from the diff.

### The target's FRAME SIZE is evidence for declaration order

`iParMgrRenderParSys_Ground`/`_Flat`: declaring the `at`-row products
(`zdx,zdy,zdz`) bare and first, ahead of the `right`-row products, took Ground
97.488 -> 99.477. In definition order we spill an extra callee-saved FPR and
**the frame grows 0x90 -> 0xa0**. When our frame is larger than the target's,
the spill is the tell and declaration order is the lever -- check frame size
before hunting registers.

### Our allocator colours anonymous store-value groups in REVERSE source order

Three witnesses in one unit, and it looks like a single mechanism:

- `iRenderPushQuadStreak`'s `{px-dx, py-dy, pz-dz}` -- we give f5,f4,f3 where
  retail gives f3,f4,f5
- `iParMgrRenderParSys_Ground`'s six `{v0.xyz, v1.xyz}` store values -- exactly
  reversed
- `iRenderPushFlat` -- likewise

**In all three the middle element matches and the outer pair swaps.** No source
spelling reaches it: naming fails the multi-use precondition, and statement
permutation only makes it worse (~35 spellings measured on QuadStreak alone,
including all 120 declaration permutations and all 6 store-component
permutations; the floor is 4 rows). A good target if anyone re-opens the
allocator.

### The multi-use precondition, confirmed sharply -- same file, same day

In `Ground`/`Flat`, naming `px - xdx` (used **twice**) **paid**. In
`QuadStreak`, naming `px - dx` (used **once**) was **bit-identical** in five
declaration positions. Opposite outcomes on use count alone. This is now the
third independent confirmation; treat single-use naming as inert.

### Another mutually-exclusive sibling pair (the `RenderLightning` situation)

`iParMgrRenderParSys_Streak` and `_InvStreak` were diffed **against each other
in the target**: the two retail bodies are byte-identical except an f6/f7 swap
on the y lane. Retail's own compiler allocated two identical sources
differently, so **no single source can close both**. Same class as
`RenderLightning`'s two loops. The pre-existing source comment saying so is
verified against raw bytes.

### dwarf is decisively WRONG for `iRenderPushQuadStreak`

`dwarf/SB/Core/p2/iParMgr.cpp` lists the only float local as `size` -- no
`px/py/pz`, no `tx/ty/tz`, no `dx/dy/dz`. It **does** record register-allocated
locals elsewhere in the same function, so this is not a recording artifact.
Writing the function that way -- every position expression inlined, CSE-only --
measures **62.665%** against 99.900%. The GC target's three `fmr`s prove the
named values must exist.

### iParMgr: patch behaviour, and what is left

The patch is strongly positive here and there is **no new patch-cost witness**.
E3n is worth +1.00 pp on QuadStreak, +0.78 on Ground, +0.95 on Flat. On
`iParMgrInit`, clause V is worth **+15.1 pp** and entry 0 **+21.0 pp**
(tree 70.040 / stock 54.960 / no0 49.069 / no3 70.040 / noV 54.960).

- `QuadStreak` 99.900 (4 rows) -- REGS, the `_xCameraUpdate` one-bit tie-break:
  `(px-dx)` wants f3 and `(pz-dz)` wants f5, we produce the reverse. ~35
  spellings; floor is 4 rows.
- `Ground` 99.709 (16 rows) / `Flat` 99.452 (19 rows) -- same class, a
  permutation of anonymous store-value temps across f6-f11 / f4-f11.
- `Streak` 93.491 / `InvStreak` 93.082 -- structure now matches; residual is the
  `5.0f` literal colouring f1-vs-f3 plus z-lane scheduling, and the pair is
  mutually exclusive as above.
- `Sprite` 87.688 -- one finding left on the table: retail computes **all twelve
  `fmadds` into twelve distinct registers and then stores them all**, in
  component-major compute order with vertex-major store order. Reproducing that
  with 12 named temps works structurally but measures **85.795**, below the
  inline form, because our allocator then places them badly. Not shipped.

### `--relocs` IS NOT EVIDENCE ABOUT THE POOL. Read section sizes instead.

`iModel.cpp` carried `__deadstripped_sdata2_hack()`, a **fabricated** dead
function seeding `0.0f` that a previous pass had installed. Under
`solo.py --relocs`, removing it made five extra functions non-matching, which
reads convincingly as real pool work.

**It was not.** `readelf` shows `.sdata2` is 0x24 **with and without** it
(target 0x28). It moved anonymous *numbering* only -- cosmetic by this file's
own layout-not-numbering rule -- and banked zero under report.json semantics,
while emitting a surplus strong global the target does not have. Removed; the
DOL is unchanged by its removal, which confirms it was dead weight.

**Rule: to test a pool hypothesis, compare section sizes with `readelf`, never
`--relocs` row counts.** And a fabricated function that does not move the number
must come out, however convincing the wrong measurement looks.

### `>=` has its own branch signature (sibling to the `a <= b` note)

    MAX2(a,b) with `>=`  ->  fcmpo / cror eq,gt,eq / bne
    MAX2(a,b) with `>`   ->  fcmpo / ble

**A `cror` in a max-chain means the source used `>=`.** This caught a real
defect: `iModelCull` selected the max scale with `MAX` (`>`, from `macros.h`)
where the target uses the `>=` form of the file-local `MAX3` -- and its own
sibling `iModelCullPlusShadow` already used `MAX3` and matched that block
byte-for-byte.

### NAMED SHAPE: "the target unrolls a loop and we do not"

`iModelCullPlusShadow` sat at 74.45% because our loop body contained the entire
shadow-test block, so mwcc would not unroll it. Retail's `bgt` jumps **out** of
the loop to a block placed after the loop's `return 0`, leaving a small body it
unrolls 3x. Rewriting the `if` as `goto shadow_test;` with the block after the
loop was **+23.5 points in one edit**.

**The tell is `li r0,<n/3> / mtctr` in the target against our `li r0,<n>`**, and
the branch polarity says which side is cold. When you see it, ask whether the
`if` body inside our loop belongs outside it.

### COUNTER-EXAMPLE: a tiny residual is NOT evidence against source-reachability

These notes advise working one-away units in ascending percentage. In `iModel`
two of the three easiest closes were the **highest**-percentage entries --
`iModelVertEval` (99.831, 2 rows) and `iModelStreamRead` (99.522) -- and both
were genuine source bugs. The ascending-percentage heuristic would have
deprioritised exactly those. It is not reliable in `Core/gc`.

### `iModelMaterialMulCB`: another entry-0 pair, same shape as `eval_joint`

244 b, 95.738%, patch-insensitive. 4 rows of 61, all in the first of three
`U8_COLOR_CLAMP` blocks, where our scheduler hoists the u32->double magic `lfd`
above `stw r4,0x8(r1)` (the `col` struct copy). That pair is an 8-byte **whole**
`.sdata2` load x a **whole** 4-byte frame object = **dispatch entry 0**, so E3n
(entry 3) is never consulted. Second witness for the "E3n's rule on the
whole x whole entry" candidate clause, after `eval_joint`. Source permutations
do move rows (94.180, 90.738), so it is in contact with the scheduler, but
baseline is the best form.

### iModel: E3n witnesses in both directions, in one file

Over-fire, neither bankable: `SkinXform` (520 b) tree 96.600 / `no3` 98.138;
`SkinNormals` (632 b) 96.424 / 97.177. Counterweight from the same file:
`iModelAnimMatrices` is a strong E3n **winner** -- 95.107 tree against 83.467
stock and `no3`. A reminder that E3n's price is per-pair, not per-unit.

### iModel: the five left

- `iModelCullPlusShadow` 636 b, 97.906 -- **unfinished, not proven blocked**,
  patch-insensitive. Retail CSEs only `shadowVec->x` into loop 2 and reloads
  `->y`/`->z`; we hoist all three (f9/f10/f11) and the register map cascades.
  10 spellings all bit-identical. Naming all three reaches 98.000 but invents
  locals for +0.09, so not installed.
- `SkinXform` / `SkinNormals` -- **BLOCKED**. Multisets identical (pool id
  only); callee-saved GPR rotation (r26-r29), the `xShadowSimple_Add` class,
  plus two ALU-past-ALU swaps.
- `iModelAnimMatrices` 300 b, 95.107 -- **BLOCKED**, three clusters: a
  callee-saved r27/r28 swap (six declaration positions, all <= baseline, two
  bit-identical), a `stw` of `matrixStack[0].flags` scheduled among nine `stfs`
  to the same aggregate (**entry 4**), and an `addi`-past-`addi` pick swap.

Unit `.sdata2` remains 0x24 against the target's 0x28 (`.sbss` 0x2c vs 0x30) --
a genuine Phase-2a shortfall with **no honest route available**: nothing is
missing from `.text` and dwarf names no extra function (`solo.py --missing` = 0).

### A retail out-of-bounds read, reproduced deliberately (PCPORT)

`iModelCullPlusShadow`'s first frustum loop is unrolled 3x by mwcc, and mwcc
updates the secondary induction variable (`numPlanes`, r4) **wrongly**:
`subi r4,r4,1` sits after copy B's exit branch and `subi r4,r4,2` at the latch,
so at the six exits r4 is 5,5,4,2,2,1 where the correct remaining-plane counts
are 5,4,3,2,1,0. **On four of six exits the second loop runs one iteration too
many and reads `cam->frustumPlanes[6]`, past the array.** Verified against raw
bytes at `800C8484`-`800C84EC`. Our code reproduces it exactly, because it must.
Flag for PCPORT.

### THE CHEAPEST QUESTION IN THIS PROJECT: patch, or source?

Before spending a session on any near-100% residual, compile the unit with
**stock `GC/2.0p1`**. If it hits 100.0 there, the source is already correct
and the work is in the patch, not the `.cpp`. This costs one compile.

`scratchpad/verify_mw.py <unit> <mw_version|-> <symbol>` does it: it reuses
`build.ninja`'s own rule and flags but substitutes a chosen `$mw_version`, and
compiles into a private temp dir -- so it is safe to run while other agents are
building, and it never touches the shared compiler. Variant compilers live at
`scratchpad/compilers/GC/2.0p1a-{no0,no1,no3,noV,e3c,chk}`; `no3` ablates E3n,
`chk` is the control. Building a variant takes seconds; sweeping all 224 SB
units takes ~35 s at 8-way parallel.

### `solo.py`'s LEFT COLUMN IS THE TARGET. RIGHT IS OURS.

Stated in solo.py's own docstring (`left` comes from `-1 <target_path>`) and
got read backwards anyway, twice in one session, by me. It inverted the
diagnosis both times: on `zEntPlayer_AnimTable` it is **retail** that
accumulates `@stringBase0` through three registers and **ours** that does it
in place (the direction is the entire finding -- retail has more registers
occupied, not fewer), and on `xSpline`'s `Tridiag_Solve` it is **ours** that
takes `b,c,d` in plain parameter order and the **target** that scrambles.
Check the orientation before writing down a conclusion.

### The `int ourAnims[2]` idiom: 3 witnesses, not source-reachable

`ZNPC_AnimTable_NightLight` (176 b), `_Tubelet` (192 b) and `_BossSBobbyArm`
(184 b) are 5 rows each, a pure r4<->r5 transposition on the 8-byte
`@sda21`->frame copy of `int ourAnims[2]`. The trigger is exactly the
2-element array: `grep 'ourAnims\[2\]'` returns these three and nothing else,
and every 3-element sibling (`SleepyTime`, `BossSB1`) matches.

Only one allocator decision is involved: the copy's scratch temp is r4 for us
and r5 for retail, and the `li 0` the scheduler hoists above the copy is then
forced to be *the other* argument register, producing all five rows. For
retail to pick r5, r4 must be live across the copy -- retail's IR materialises
arg 2 before the array copy and ours does not.

**Stop test run and passed.** Six spellings -- array-first, `table`-declared-
first, array-then-bare-`table`-then-assign, `S32` vs `int`, literal `0` vs
`NULL` for arg 2, and a braced scope around the call -- are all bit-identical
(same 99.545%, same SHA-1 of the full diff text). Only two things move it and
both move it the wrong way: declaring the array after the call gives 79.636%,
making it 3 elements gives 76.091%. **Do not re-open.** This supersedes the
older note framing these three as "the best test case on the board".

### NEW CANDIDATE CLAUSE: literal load hoisted over an INDEXED frame store

`ZNPC_AnimTable_BossPlankton` (2,472 b) and `_BossSB2` (3,384 b) have
*identical* 17-row clusters at the closing `NPCC_BuildStandardAnimTran` call.
Both compile identically under stock `2.0p1` and patched `2.0p1a`, so the
patch is exonerated here. Root cause is a literal-load hoist, not a register
problem:

    target: addi r5,r1,0x18 / slwi r0,r17,2 / li r4,0 / lis r3,g_strz_bossanim@ha
            / stwx r4,r5,r0 / addi r4,r3,@l / mr r3,r18 / li r6,1
            / lfs f1,@1657@sda21 / bl
    ours:   lis r3,@ha / addi r5,r1,0x18 / addi r4,r3,@l / slwi r0,r17,2
            / li r3,0 / lfs f1,@437@sda21 / stwx r3,r5,r0 / mr r3,r18
            / li r6,1 / bl

Our scheduler hoists `lfs f1, @NNN@sda21` and the `lis @ha` **above** the
`stwx` into the frame array; retail does not. The r3-vs-r4 choice is a
consequence, not a cause. This is the clause-D shape that these notes already
price as net-negative -- except that the store here is **indexed** (`stwx`,
computed address), which is why clause C's `memref+0x0c == 0` gate and E3n's
declared-frame-object gate both decline. Precisely shaped candidate: *a small
static literal load may not hoist above an indexed store to a frame array.*
Worth 5,856 bytes across these two.

Source stop test on Plankton: `anim_list[anim_size++]`, adjacency of the
terminator write, explicit `0` for `ANIM_Unknown`, `&anim_list[0]` vs
`anim_list`, and binding the 0.2f to a named `F32` local are all bit-identical.
Moving the terminator write earlier *does* move the rows (97.508%), so unlike
the `ourAnims[2]` family there is real contact with the scheduler -- but the
baseline ordering is already the best one and the residual is the hoist.

### Two corrections this forced

- **`zEntPlayer_SNDInit` was not the prize.** It was dispatched as a
  10,160-byte function whose residual was "about 400 of 403 rows missing `lfs`
  reloads"; clause V moved it 90.519 -> 91.947 and `PlayerTeeterCheck` not at
  all. **That is the second per-unit attribution to collapse on contact with
  the compiler** (the first was the 1.750pp alias-predicate estimate). Agent
  reports are good at *characterising* a residual and bad at predicting what a
  compiler change will pay. Verify before promising.
- **The docstring's warning against E3n on scheduler entry 3 was stale.**
  Reverting entry 3 to clause C measures **+6/-88** (superseded: a full
  224-unit sweep on 2026-08-22 measures **+6/-107 functions, +25,924/-77,224
  bytes**), so E3n is worth +82 net
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

**Independently re-derived 2026-08-22, from a cold start, by an agent that had
not read this section** (the checkout had been rolled back by a container reset).
It reached the same conclusion by the same route -- small-data store kills a
cached `.sdata2` literal in retail and not in ours, with the large-global store
as the negative control -- and cost ~80 minutes to do it. The finding is
therefore replicated, not a one-off. It also independently hit the r30/r31 pin
on `zMainMemCardSpaceQuery`'s two anonymous temps (15 orders sampled, versus the
~600-order hill-climb above; same plateau, same cause).

**So: DO NOT SEND ANOTHER AGENT AT zMain.** Both of its functions are
diagnosed and both are blocked on compiler-track work, not source work.
`zMainParseINIGlobals` needs the second alias query site found in the code
generator's redundant-load path; `zMainMemCardSpaceQuery` needs an allocator
that can lift block-scope values above function-scope locals, which the select
routine at 0x508900 cannot express (lowest-free-colour, no preference term).

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

- **The `const` lever does NOT extend to scalar locals -- there it can HURT.**
  Measured on `lightning_ring::set_ring_segments`: a `const F32 angle_step =
  PI / 32.0f;` inside the init loop was the function's ENTIRE residual,
  because the const pulled the constant's `lfs` up into the `icos` return
  shadow. Replacing it with `angle += PI / 32.0f;` took the function to
  100.0. The lever is specifically about read-only local AGGREGATES, where it
  buys the three-register copy form; on a scalar there is no copy form to
  buy and all it does is move a load.

- **CW numbers same-scope locals in REVERSE declaration order.** So when the
  target's frame slots are the giveaway, the local that wants the HIGHER slot
  must be declared LAST. `apply_wave_damage` needed `xSphere inner; xSphere
  outer;` to put `outer` at 0x14 and `inner` at 0x24, and closed on that
  alone.

- **Declare-first-assign-later is a distinct lever from move-the-computation.**
  In `generate_zap_particles` retail's colouring wanted `points` created
  before `emitted`. Moving the whole `points` computation first measured
  **92.135** (it forces the source object into a callee-saved register early);
  declaring `S32 points;` first and assigning it after `emitted` measured
  100.0. The virtual register is created at the DECLARATION; the load stays
  where the assignment is.

- **Retail's `bne .+8 / b exit` is the shape CW emits for the LAST operand of
  an `||` chain**, not for a standalone `if (cond) return;`. Seeing that
  degenerate two-branch form means two guards in the original were one
  condition. Merging them closed `repel_player`.

- **One scheduler rule accounts for most of what is left in zEntPlayer:
  our scheduler always fills a float-load latency slot with an already-ready
  store, where retail leaves the stall.** Confirmed as the same single site in
  `BoulderRollCB`, `BoulderRollDoneCB`, `SlideTrackUpdate`,
  `zEntPlayer_SpringboardFX` and two of `zEntPlayerFloorUpdate`'s clusters --
  **3,232 bytes of otherwise-clean functions**. The canonical shape is three
  constant stores to adjacent members where retail emits
  `lfs f1,<0> / stfs f1,x / lfs f0,<3> / stfs f0,y / stfs f1,z` and we hoist
  `stfs f1,z` into the `lfs f0` shadow. Roughly ten source spellings have been
  measured against it across two passes; none reaches it.

  **This is scheduler entry 4, and entry 4 is DEAD -- do not re-open it.**
  The two reordered stores are subranges of the SAME aggregate
  (`info.vel.y` vs `info.vel.z`), which is exactly the subrange x subrange
  query on entry 4, measured **+16/-560 tree-wide** above. The same shape was
  independently reached from `xCollide` (`xParabolaHitsEnv` 576 b,
  `xSphereHitsOBB_nu` 956 b), so the true witness set is at least seven
  functions and about 4,764 bytes -- all of it behind a predicate that costs
  560 exact functions to satisfy. Recognise this shape and STOP: it is not an
  opportunity, it is the single largest confirmed dead end in the project.
  It was briefly written up as "the highest-value single shape on the board"
  before the entry-4 connection was made; that reading was wrong.

- **`zEntPlayer_SpringboardFX` is the sharpest `volatile` near-miss recorded.**
  Marking its function-local `static F32 sLastSpringboardBubbleEmit` volatile
  takes it from 12 differing rows to **2** -- fixing both the reload and an
  f30/f31 swap -- but scores 98.361 rather than 100.0, because the moved store
  scores worse than the substitutions it replaces. Reverted under the
  install-only-at-100.0 rule. Its last row is the scheduler shape above, so it
  closes for free if that is ever fixed.

- **Two independent agents have now reconstructed the SAME
  `RwIm3DVertexSetRGBA` from dwarf, so the header form is probably real.**
  `dwarf/` lists, for both `xFX::RenderRotatedBillboard`/`DrawRing` and
  `xShadowSimple_AddVerts`, exactly one `class RwRGBA* _col;` local per
  invocation of that macro -- and none for `SetPos`/`SetUV`. The stock
  RenderWare form introduces that temp:

      RwRGBA* _col = (RwRGBA*)&((_vert)->r);
      _col->red = (_r); _col->green = (_g); ...

  Our `include/rwsdk/rwcore.h` has the flattened `(_vert)->r = _r;` form.
  **Measured, hand-expanded in xFX: the `_col` form alone is byte-for-byte
  INERT.** So on its own it is a fidelity/naming change for zero match gain,
  reached by two routes. The open question is whether the real macro also
  binds `_a` to a temp, or is an inline FUNCTION (whose argument evaluation
  would hoist the `lbz` naturally) -- `xShadowSimple_AddVerts` needed six
  explicit `alpha = cache->alpha;` re-reads to reproduce retail, which an
  inline function's argument evaluation would give for free. That variant has
  NOT been measured tree-wide and is the thing to test if anyone opens this.

- **Distribute a constant to the USE site rather than folding it into the
  expression -- the constant's register is decided by which temp's live range
  STARTS first.** `zNPCGoalBossSandyLeap::Enter` 99.586 -> 100.0 (532 b) on
  exactly this. Retail allocates `10.0f`->f0 and `1.0f`->f2 so `fdivs` writes
  into the *numerator's* register; we allocated the reverse. Written as one
  statement, `mag = 10.0f * (1.0f / xsqrt(mag));`, mwcc evaluates the
  call-bearing operand first, so the `1.0f` temp is created first and takes
  f0 -- and once f0 holds a value dying at the `fdivs`, the divide targets f1
  and every downstream register follows. The fix:

      mag = 1.0f / xsqrt(mag);
      endX = endX * (10.0f * mag);
      endZ = endZ * (10.0f * mag);

  Now the `10.0f` temp is created after the reciprocal is already a plain
  variable, so it takes f0 and pushes `1.0f` to f2. Eleven other spellings
  measured, including every obvious split; ALL of them scored 99.586 or
  worse. Note especially that `mag = 1.0f / xsqrt(mag); mag *= 10.0f;` gives
  99.624 with retail's operands REVERSED -- higher number, wrong code.

  **The control that proves it**: `zNPCTypeKingJelly::get_away` is 100.0
  today and contains literally
  `F32 scale = 0.70710677f * (1.0f / xsqrt(dist2));` -- the folded spelling --
  and compiles to OUR pattern. So the folded form is provably not what
  Sandy's retail source had. When two call sites of the same idiom want
  different register maps, the difference is where the constant lives.

- **Retail's `fmadds` are usually IN PLACE -- write `base` then `+= a * b`,
  not one fused expression.** Written as `vax = right.x * ppv + at.x * dpv;`
  mwcc gives the product temp and the result different colours. Written as
  `F32 vax = at.x * dpv;` then `vax += right.x * ppv;` they coalesce, which is
  what retail emits (`fmadds f5, f2, f4, f5` -- destination and third operand
  the same register). On `_xCameraUpdate` this plus one declaration-order
  constraint removed 16 of 22 differing rows (99.792 -> 99.949). Look for a
  target `fmadds` whose destination equals one of its source registers; that
  is an accumulate in the original, not a fused expression.

- **`A + B + C` emits `fmuls(B) / fmadds(A) / fmadds(C)`** -- mwcc evaluates
  the RIGHT operand of the inner `+` first, so the MIDDLE term's multiply
  issues first. Source order `y,x,z` gives `fmuls(x)` first; right-associating
  as `A + (B + C)` gives `fmuls(z)` first. If the target's first `fmuls` is
  the middle term of a three-term dot product, your source order is already
  right -- measured on `zThrown_Update`, where only `x,y,z` reproduces
  retail's `fmuls f1,f6,f8`.

- **`c ? K-1 : K` is not `K - (c != 0)`.** Retail materialises the boolean
  SIGN-extended (`neg / or / srawi 31`, giving 0/-1) and ADDs it to a
  separately materialised constant (`lis/addi` then `add r3, r0, r3`); the
  subtraction form gives the LOGICAL 0/1 (`srwi`) plus `subf`. The
  ternary-with-two-constants is what makes mwcc build the constant as a value
  and fold the delta into a 0/-1 mask. Closed `zNPCBPlankton::next_goal`
  (95.185 -> 100.0). **Write `K - 1` literally** -- spelling it as the
  enumerator with the same value measured 87.407, and
  `K + -(c != 0)` measured 85.926 because the constant then folds into
  `addis/addi`.

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

**Scope check, 2026-08-21: zThrown's REMAINING functions are not pool-blocked.**
The unit is down to four non-matching from the eighteen that motivated the
probe experiment, and every diff row in all four was re-examined: the
anonymous `.sdata2` rows (`@844`, `@257` and friends) all render as
*identical*, so pool ordering is not what holds any of them back. Their
causes are register colouring (`ThrowFruit`, `zThrown_Update` cluster A),
store-to-load forwarding (`zThrown_Update` cluster B) and the alias question
below. So do not reach for the pool explanation here by reflex -- it was true
of the unit as a whole once and is not true of what is left.

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

## Lead: the `$localstaticN$` counter says zEntPlayer's TU is missing an entity

The target names the Chuck offset vector
`offsetChuck$localstatic4$get_reticle_bound__FR5xVec3Rf`; ours is
`$localstatic3$`. CW's `$localstaticN$` counter runs over function-scope
statics in inline/instantiated functions in the TU, so **retail's translation
unit has one more such static before that point than ours** -- most likely
inside an inlined function pulled in from a header, since none of our own
file-local statics before that point use the `localstatic` mangling.

objdiff scores those rows as identical (it pairs relocations by offset), so
it costs nothing today. But it is a genuine missing entity in the TU, it is
the same family as the `.rodata`-ordering problem, and unlike that one it
names a specific counter you can check.

## Queued: dwarf identifier-name recovery for zThrown

Byte-neutral (locals do not affect codegen), recovered from
`dwarf/SB/Game/zThrown.cpp`, not yet applied. Worth a dedicated pass:

    zThrown_Update:  killIt->removethis, bound->oldbound, pos->oldpos,
                     delta->stackDelta, dir->velunit, oldGravity->oldgrav,
                     stackTgt-block d->posdot, sws t->lerp, lim->lerpdist,
                     reflection-loop d->dothdng,
                     hx/hy/hz->boxX/boxYupper/boxZ
    ThrowFruit:      idx->collfound, speed->velmag, pct->lerp

Caveat recorded with them: dwarf OMITS about ten float locals in
`zThrown_Update` (all of `px..tz`, `nx/nz`, `r`, `center`) that the asm proves
must exist. So for this unit dwarf is name evidence only, and is NOT a
completeness oracle -- consistent with it having pointed the wrong way five
times this week.

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
