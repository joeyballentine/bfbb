# Docs

## PC port

- [BUILDING.md](BUILDING.md): building on Windows, Linux and macOS. Optional
  dependencies, backends, 64-bit, MinGW, troubleshooting.
- [PCPORT.md](PCPORT.md): developer guide. Branches, the GameCube gate, how the
  two builds are kept apart, conventions, pitfalls.
- [../src/SB/Core/pc/README.md](../src/SB/Core/pc/README.md): the platform
  layer, interface by interface, and the `BFBB_*` switches.
- [ANDROID.md](ANDROID.md): the Android port. Building the APK, first launch,
  files on the device, logs, how the platform pieces work.
- [RENDERING.md](RENDERING.md): the renderer and its backends, shipped options,
  the fixed-function mode, and proposed effects.
- [RESOLUTION.md](RESOLUTION.md): rendering above 640x480 and widescreen.
- [UNCAPPED.md](UNCAPPED.md): frame rates other than 60. The defect classes,
  what was fixed, and what is still open.

## Decomp

- [DUPLOTRON.md](DUPLOTRON.md): the decomp branch's record. The patched
  CodeWarrior, the ground rules, what has been tried.
- [dependencies.md](dependencies.md): what the GameCube build needs.

## Reference

From [bfbbdecomp](https://github.com/bfbbdecomp/bfbb):

- [splits.md](splits.md): format of `config/GQPE78/splits.txt`.
- [symbols.md](symbols.md): format of `config/GQPE78/symbols.txt`.
- [common_bss.md](common_bss.md): how `-common on` places uninitialised
  globals. Retail's placement shows whether a symbol was static.
- [comment_section.md](comment_section.md): CodeWarrior's `.comment` section
  and what it says about the compiler that built a unit.
