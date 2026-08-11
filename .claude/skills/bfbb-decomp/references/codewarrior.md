# CodeWarrior source shapes

The compiler is `mwcceppc` 2.0p1a targeting Gekko. These are shapes that produce
different code from an equivalent-looking alternative. When instructions match
but registers or scheduling do not, the source form is usually the reason.

## Expression and control flow

| Shape | Effect |
|---|---|
| `x *= k` vs `x = x * k` | the compound form survives where the other gets folded |
| `a <= b` on floats | emits `cror eq,lt,eq` + `beq` |
| `!(a > b)` on floats | emits `ble`. Pick the one matching the target |
| small `switch` | becomes a **binary search**, not a jump table |
| sequential `cmplwi` on one CSE'd load | the source was an **if/else-if chain**, not a switch — worth ~6% on a recent function |
| `if (a < b) {X} else {Y}` vs `if (a >= b) {Y} else {X}` | different branch emitted. Swapping arms has been worth 9–12 points |
| `arr[i].member` vs `ptr->member` | different register allocation |
| `S32& n = arr[i];` vs `S32 n = arr[i];` | binding as a reference changes register pressure; often the difference between ~97% and 100% |

## Copies and inlining

- **Copy-initialisation** (`T v = expr;`) inlines a struct copy. **Copy-assignment** (`v = expr;`) routes through an out-of-line `__as__`. These are not interchangeable.
- CW inlines a same-TU callee **only if it is defined earlier in the file**. If the target inlines something you call, move that definition above the caller.
- CW in this build will **not** inline a function that returns a struct by value. Proven with a local static defined immediately above its caller; it still emitted a `bl`. Do not fight it.
- `INLINE` is never defined in this build, so MSL `cmath` only *declares* the `std::` functions; each TU defines its own under `#ifndef INLINE`.

## Layout

- **Local declaration order controls stack slot assignment.** Take the order from `dwarf/`. Reordering three locals to the original order took one function from 99.016% to 100%. It is not a general fix though — see the bfbb-recovering-source skill for when it works and when it does not.
- Local declaration order can also assign stack slots **descending**: on one function four `xVec3` locals landed at 0x30/0x24/0x18/0x0c in declaration order. If your stack offsets are permuted, permute the declarations.
- A raster or similar value chosen by a condition usually needs to be a **hoisted local with an explicit `if/else`**, not a ternary written inline in the call.
- Vertex or struct field stores sometimes must go through explicit scalar temps loaded in a specific order; a direct member-to-member store interleaves load/store differently, and a whole-struct copy routes through an out-of-line `__as__`.

## Literal pool

Anonymous literals (`@1234`) are numbered by a **file-global counter**, but numbering is cosmetic — objdiff compares relocations by **target offset**. What matters is `.sdata2` *layout*.

- Filling in a function interns new literals and shifts every later offset, which can knock neighbours off 100%.
- Conversely, writing a large function at the target's position can seed the pool correctly and lift unrelated neighbours to exact for free.
- Constant folding can silently prevent a literal from being created at all. `2.0f * (PI * i) / 3.0f` with a locally-assigned `i = 0` folds to `0.0f` and never interns `2.0f`/`PI`/`3.0f`, shifting the whole file's pool by 12 bytes. Assigning the index in *both* arms of a branch blocks the front-end fold while the value stays 0.
- Retail sometimes has **ghost literals** with no surviving reference — constants from code that was optimised away. You cannot reproduce those without fabricating dead code. Do not; report the ceiling instead.

**Diagnosing a pool permutation.** Disassemble both objects, list `.sdata2` in slot order, and for each slot record the *first function that references it* in emission order. Compare the two lists side by side. If the values are the same multiset in a different order, it is ordering, not content, and the first row where they disagree names the culprit.

The worked case is `zNPCGoalRobo`, and it is a ghost-literal ceiling, not a fixable defect. Slots 0–13 agree exactly. At slot 14 the target has `3.0f` and at 15 `1e-05f`, both interned *before* `FlankPlayer__19zNPCGoalAlertFodderFf` interns `6.0f` — yet their first use is `OrbitPlayer__20zNPCGoalAlertFodBzztFf` and `MoveChase__19zNPCGoalAlertHammerFf`, thirteen and twenty-eight functions later. Nothing between `CheckSpot` and `FlankPlayer` emits a load of either. Ours interns the same two values, correctly, at the point they are first used. The two-slot head start offsets every later slot, which is why ~100 functions in that unit sit at 99.x% each off by exactly one relocation — and why the unit reads well in `report.json` (99.x counts as matched) while being unable to reach complete coverage.

Function **definition order** is worth checking at the same time, since it drives interning order: extract `.fn` order from both objects and take the longest increasing subsequence. `zNPCGoalRobo` scores 320 of 361 common symbols with 10 descents, so its source order is also genuinely wrong in places — but fixing that cannot realign a pool whose head is already displaced by ghosts.

## Source shapes worth trying before you give up

Each of these was measured on a real function, and the gain is the measured one.

- **Brace-initialised aggregates emit a template, not stores.** `xVec2 v = { a, b };` with non-constant elements makes CW emit an 8-byte **`.sbss2`** template and copy it to the stack before storing the fields; `xVec3 v = { a, b, c };` does the same with a 12-byte **`.rodata`** template. Using `xVec2::create`/`xVec3::create` instead costs a `bl` and misplaces the template. Switching six functions in `zNPCTypeBossPlankton` to brace-init moved five of them from 47–64% to 99.3–99.7%.
- **`xabs()` and `std::fabsf()` are not interchangeable.** `xabs` inlines to `fabs`+`frsp`; `std::fabsf` emits `bl fabsf__3stdFf`. Retail used both, in different functions in the same file. MSL's `cmath` in this tree does not declare `fabsf`, so a file-local `namespace std { float fabsf(float); }` is the byte-neutral way to reach it.
- **Collapse early returns into one condition.** `if (A) { return X; } else if (B && C)` and `if (A || (B && C))` produce visibly different exit structure. `update_follow_player` went 49% → **100%** purely from that collapse.
- **A `switch` is not an if/else-if chain.** CW compiles `switch` over small integers as a binary search. `stun`'s dialogue selection went 84% → 96% on that change alone; `impart_velocity` dispatches on `flag.move` the same way.
- **…but a *dense* `switch` becomes a jump table, and the arm that decides it is often one you left out.** Binary search in your object where the target has a jump table means the case set is not dense enough. Adding a `case 0:` arm took `zEntPlayerSurfDamageUpdate` from 78.0% → 85.4%, and adding an empty `case 6:` made `zNPCGoalBossPatSpin::Process` emit the target's 7-entry table `@2886` instead of a search. Count the target's arms before you write the `switch`.
- **`if (floatvar)` and `floatvar != 0.0f` compile differently.** The implicit-bool form emits `fcmpu (var, 0)`; the explicit comparison emits `fcmpu (0, var)` — the operands are swapped. Retail overwhelmingly used the implicit form. Worth 97.9% → 99.9% on `zEntPlayerVelUpdate` on its own, and it cleaned three more sites in `zEntPlayerJumpUpdate`.
- **Read the branch to recover the operator.** A plain `bgt` / `ble` / `blt` / `bge` in the target means the source used `<` or `>`, possibly negated — so `!(x > 0.0f)` is right and `x <= 0.0f` is not. `<=` and `>=` on floats *always* route through the `cror eq,lt,eq` pair. The branch mnemonic tells you which one the author wrote.
- **`cond ? x : default` and `(x == 0) ? default : x` are not the same shape.** Only the first reproduces the `beq`/`b` ternary structure.
- **`MAX(MAX(a,b),c)`** from `xMath.h` reproduces retail's double-evaluated max; a three-way hand-rolled comparison does not.
- **Splitting a fused expression into a named local blocks `fmadds`.** `apply_yaw` went 97.25% → **99.92%** from hoisting one multiply.
- **`&= 0xfffd` and `&= ~0x2` are different instructions.** The 16-bit literal gives `andi.`; the complement gives `rlwinm`.
- **`xfeq0(x)`** (`xMath.h`, `((x) >= -1e-5f) && ((x) <= 1e-5f)`) is the source of the `fcmpo` / `cror eq,gt,eq` / `bne` triple. If you see that triple, do not hand-roll the comparison — use the macro, and note the epsilon is `1e-5f`, not `1e-6f`.
- **CW assigns stack slots in *descending* declaration order** for some frames. If every `r1+offset` in a function is permuted but the instructions are right, reverse the local declaration list. That took Sandy's 3852-byte `Process` from a permuted frame to exact offsets in one edit.
- **`inline` in the `.cpp` reproduces a weak symbol without touching a header.** When the target has a member as `scope:weak` in its own `.text` section, retail declared it `inline`. Marking it `inline` at the definition in the `.cpp` gets the weak scope and the `.sbss2`/`.rodata` template ordering that comes with it. Moving the body into the class in the *header* also works but adds the template object to every including TU — which broke a `Matching` unit when tried.

## Things that are not your fault

- **Scheduling residue.** Identical instruction multiset in a different order, often just two epilogue instructions swapped. Not reachable from source.
- **Float scheduling.** Our build hoists a `lfs` of a constant above an intervening store where the target does not.
- **FPR/GPR colouring.** Same instructions, rotated register assignment, usually because one extra value stays live. Sometimes fixable by changing where a variable is initialised, often not.
