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

## Things that are not your fault

- **Scheduling residue.** Identical instruction multiset in a different order, often just two epilogue instructions swapped. Not reachable from source.
- **Float scheduling.** Our build hoists a `lfs` of a constant above an intervening store where the target does not.
- **FPR/GPR colouring.** Same instructions, rotated register assignment, usually because one extra value stays live. Sometimes fixable by changing where a variable is initialised, often not.
