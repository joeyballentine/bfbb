# Which number means what

Four measurements exist and they disagree on purpose. Quoting the wrong one is how this project has repeatedly fooled itself.

| Tool | Measures | Use it for |
|---|---|---|
| `tools/solo.py` | per-function % against the target object, one unit, private temp build | all before/after work inside a unit |
| `build/GQPE78/report.json` | project metric: `matched_functions`, `fuzzy_match_percent`, `complete_units` | the headline; produced only by a real `ninja` |
| `build/GQPE78/progress.json` | what the progress site shows: `matched_code` bytes | quoting external progress |
| DOL sha1 | whether `Matching` units still link byte-identically | the only proof for a `Matching` unit |

**Never compare a solo.py number against a report.json number.** They count by different rules.

## Keep solo.py's output small while you iterate

The default listings are big — a unit list is 45–120 rows and a symbol diff is one row per instruction, so a 2 KB function prints ~500. Every one of those rows stays in context for the rest of the run, and you will run the tool dozens of times.

- `-q` / `--quiet` — header line only. This is what you want between edits, when all you need is whether the non-matching count moved.
- `--top N` — the N worst rows. Enough to pick the next target.
- Symbol diffs now print **only differing rows plus 3 lines of context** by default, with `... N identical` markers. `-C 1` tightens it further, which is the right setting when a function is at 99.x% and every difference is an isolated relocation. `--full` restores the every-row output when you genuinely need to inspect instruction scheduling or ordering.

Truncation always says what it withheld, so a short listing is never mistaken for a clean one. Take the full listing once at the start and once at the end — that is what you report — and stay in `-q` in between.

**Beware stale objects when comparing solo.py against the built object.** `solo.py` always compiles fresh into a temp dir. `build/GQPE78/**.o` is only as current as the last successful `ninja`, and a build interrupted by an unrelated compile error leaves some objects untouched.

This produced a convincing false alarm worth recognising. `check_hide_entities` measured **100% under solo.py and 91.047% in `build/`'s object**, with report.json agreeing with the stale object — which looked exactly like solo.py being unfaithful to the real build, in a unit nothing had touched. After a clean `ninja` all three agree at 100%. Nothing was ever wrong.

**Before concluding that two measurements disagree, re-run `ninja` to completion and re-measure.** A stale `.o` explains far more disagreements than a genuine tooling difference does.

**Run `ninja` twice after a large multi-file change, and trust the second report.** This is reproducible, not superstition: after two agents' work landed together, the first build reported `zCutsceneMgr` at 14/18 with `check_hide_entities` at 91.047%, and an immediately following build — same sources, nothing edited in between — reported 15/18 and 100%. It happened twice, in the same unit, on unrelated changes. The first report can be assembled against objects the same run is still replacing. The DOL sha1 was correct both times; only the report was wrong.

The residual difference is benign: counting non-matching symbols off the built object includes data symbols like `[.sdata2-0]`, which `solo.py` does not, so the built-object count runs a little higher on most units.

## report.json credits near-misses

It counts functions at 99.x% as matched. Consequences:

- A wave that moves seven functions from ~0.5% to 72–100% and promotes six neighbours to exact can show **+1** in report.json.
- A change measuring **+7 exact** in solo.py showed **+0 functions and −1 complete unit** in report.json, because every one of the seven was a 99.x%→100% crossing already being counted.

Judge stub-filling and near-miss work by solo.py. Judge project progress by report.json.

## Byte-neutral changes and how to prove them

Renaming identifiers **cannot** change codegen — names never reach a non-debug object. So after a pure rename, every percentage must be identical. Any movement means you changed something other than a name.

Reordering local declarations **does** change codegen, because declaration order drives stack slot assignment. Different rules apply: keep if the target improved and no neighbour regressed, revert otherwise.

Scratch tooling for this (paths vary by session, rebuild if absent):

- `vs_head.py <unit-frag> <src-path> ...` — swaps in HEAD's version of a file, measures, swaps back, and reports which functions changed. The honest way to prove a change caused no collateral damage.
- `snapshot.py <out.json> <src>...` then `snapshot.py --cmp <before> <after>` — same idea across many units, for evaluating a shared-header change.

## Three ways this project has fooled itself

**1. objdiff is blind to definition order.** It pairs symbols by name. Deleting a function from a `.cpp` and letting a header inline provide it relocated the symbol inside `zEnt.o`; objdiff still reported the unit at 38/38 while the object was no longer byte-identical, and the DOL sha1 failed. **A unit reading 100% in objdiff is not proof its object is byte-exact.**

**2. Comparing only exact-match sets hides regressions.** A comparison that diffs "which functions are at 100%" reported a change as clean while it had dropped two units' `.sdata2` match (65.2%→63.8% and 92.1%→90.6%). No function crossed the 100% boundary, so nothing showed. Always diff the *full percentage distribution*, including data symbols.

**3. Raw `mwcc` output is not comparable to the target object.** A tool that compiled a unit and byte-compared it against `objdiff.json`'s target reported *every* unit as differing, including ones known byte-exact. dtk post-processes the extracted objects. Always run a control on a known-good unit before trusting a new measurement tool.

## Weak inline symbols appear in only one target object

An `inline` function that CW also emits out-of-line produces a **weak** symbol in every TU that uses it. The linker keeps one. dtk extracts target objects from the *linked* DOL, so the survivor shows up in exactly one object and is absent from all the others.

So "our object defines a weak symbol the target object does not" is the expected state, not a bug. `xDrawLine__FPC5xVec3PC5xVec3` is the worked example: `.text:0x8001F430`, `size:0x4`, `scope:weak`, landing in `xEntMotion.o` — the first TU in link order that calls it — while the five other calling TUs show it only on our side. The empty `inline` in `xDraw.h` is correct; the 4 bytes are the `blr`.

Check `config/GQPE78/symbols.txt` for the mangled name before concluding a symbol is spurious. `scope:weak` there means the source shape is already right.

## Before touching a shared header

Find the real blast radius, which is wider than a filename grep suggests — headers reach far more TUs transitively. What matters is whether any of them is `Matching`, because a `Matching` unit must stay byte-identical or the DOL breaks.

```
grep -n '"SB/Game/<unit>.cpp"' configure.py     # Matching / NonMatching
```

A unit containing a 0%-fuzzy function cannot be byte-identical, so it is never `Matching` — those units are safe to fill in freely.

When retail inlined a helper into some callers and not others, a single global choice is wrong either way. Define the inline in the header and let the TUs that must not expand it opt out with a macro (see `XSNDPLAY3D_OUT_OF_LINE`). Defining the body `WEAK` instead is catastrophic: it emits a weak copy into every including TU, measured at −217 exact across 24 units.
