# Docs

## PC port

- [PCPORT.md](PCPORT.md) is the design record. Why librw, why 32-bit, why Xbox
  assets, and where the platform seam is.
- [PCPORT-HANDOFF.md](PCPORT-HANDOFF.md) is the practical one. How to verify a
  change without fooling yourself, and what past investigations found. A few of
  them record being wrong, which is deliberate.
- [../src/SB/Core/pc/README.md](../src/SB/Core/pc/README.md) covers the platform
  layer interface by interface, plus the `BFBB_*` switches.
- [UNCAPPED.md](UNCAPPED.md) is why the simulation runs at a fixed step, and the
  audit of what would have to change to let the frame rate go free.
- [RESOLUTION.md](RESOLUTION.md) is the same audit for rendering above 640x480:
  what the virtual screen already gives you, and the camera rasters that have to
  move together.

## Decomp

- [DUPLOTRON.md](DUPLOTRON.md) is the decomp branch's record: the
  scheduler-patched CodeWarrior, the ground rules, what's been tried.
- [dependencies.md](dependencies.md) is what the GameCube build needs.

## Reference

Inherited from [bfbbdecomp](https://github.com/bfbbdecomp/bfbb) and still
accurate:

- [splits.md](splits.md), format of `config/GQPE78/splits.txt`
- [symbols.md](symbols.md), format of `config/GQPE78/symbols.txt`
- [common_bss.md](common_bss.md), how `-common on` places uninitialised globals
- [comment_section.md](comment_section.md), CodeWarrior's `.comment` section

`getting_started.md`, `github_actions.md` and `images/` were dropped. They were
dtk template setup for starting a new decomp ("rename GAMEID", "create a repo
from this template") and pointed at a `.github.example/` folder that doesn't
exist here. Real CI is in `.github/workflows/`.
