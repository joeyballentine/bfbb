# SpongeBob SquarePants: Battle for Bikini Bottom

[![Discord Badge]][discord]
[![Build Status]][actions]

[![Game Code]][progress]
[![Game Fuzzy]][progress]
[![Game Functions]][progress]

[![Overall]][progress]
[![Overall Fuzzy]][progress]
[![Overall Functions]][progress]

[progress]: docs/DUPLOTRON.md
[Game Code]: https://img.shields.io/badge/Game%20Code-81.97%25-limegreen
[Game Fuzzy]: https://img.shields.io/badge/Game%20Close%20Match-99.35%25-yellowgreen
[Game Functions]: https://img.shields.io/badge/Game%20Functions-7308%20%2F%207673-lavender
[Overall]: https://img.shields.io/badge/Overall-65.50%25-darkgreen
[Overall Fuzzy]: https://img.shields.io/badge/Overall%20Close%20Match-79.18%25-olive
[Overall Functions]: https://img.shields.io/badge/Overall%20Functions-8478%20%2F%2010147-slateblue
[Build Status]: https://github.com/joeyballentine/bfbb/actions/workflows/duplotron.yml/badge.svg?branch=duplotron
[actions]: https://github.com/joeyballentine/bfbb/actions/workflows/duplotron.yml
[Discord Badge]: https://img.shields.io/discord/829152115322257436?color=%237289DA&logo=discord&logoColor=%23FFFFFF
[discord]: https://discord.gg/dVbGFdYU6A

A work-in-progress decompilation of SpongeBob SquarePants: Battle for Bikini Bottom.

It builds the following DOL:

main.dol: `sha1: 306526d90b48e99894c3138f5fc8f2716d9fecf6`

This repository does **not** contain any game assets or assembly whatsoever. An existing copy of the game is required.

Supported versions:

- `GQPE78`: (NTSC-U)

## About this fork

This is a fork of [bfbbdecomp/bfbb](https://github.com/bfbbdecomp/bfbb) where
the decompilation on the `duplotron` branch was written by Claude, as an
experiment.

Claude did the work mostly unattended, using the decomp's tooling plus tools it
wrote itself. For each function it read the assembly and Ghidra output, tried
source variants in a scratchpad, and applied the best-matching one. I checked
it for fakematches as well as I could.

The code is generally decent, but treat it as unreviewed. It needs careful
verification before any of it goes to upstream `main`. It was a fast
proof of concept run on Claude usage I had spare.

Differences from upstream:

- **Game code first.** Almost all work went into the game code in `src/SB`,
  not the SDK and libraries. That is what the PC port on the `treedome` branch
  needed.
- **Patched compiler.** The build uses `GC/2.0p1a`, a scheduler-patched
  CodeWarrior that `tools/patch_compiler.py` derives from the stock compiler at
  build time. It matches functions that differed only in instruction
  scheduling. Several Fable instances found the patches, and they were
  checked against a decompilation of mwcc.
- **Playable build.** The matching build still gates on the retail sha1.
  `python configure.py --non-matching` links every `src/SB` unit from source
  into a `main.dol` that runs, with some bugs.

[docs/DUPLOTRON.md](docs/DUPLOTRON.md) records what has been tried and ruled
out. It and the other notes files in `docs/` are working notes for Claude, not
documentation.

# Dependencies

## Windows

On Windows, it's **highly recommended** to use native tooling. WSL or msys2 are **not** required.  
When running under WSL, [objdiff](#diffing) is unable to get filesystem notifications for automatic rebuilds.

- Install [Python](https://www.python.org/downloads/) and add it to `%PATH%`.
  - Also available from the [Windows Store](https://apps.microsoft.com/store/detail/python-311/9NRWMJP3717K).
- Download [ninja](https://github.com/ninja-build/ninja/releases) and add it to `%PATH%`.
  - Quick install via pip: `pip install ninja`

## macOS

- Install [ninja](https://github.com/ninja-build/ninja/wiki/Pre-built-Ninja-packages):

  ```sh
  brew install ninja
  ```

- Install [wine-crossover](https://github.com/Gcenx/homebrew-wine):

  ```sh
  brew install --cask --no-quarantine gcenx/wine/wine-crossover
  ```

After OS upgrades, if macOS complains about `Wine Crossover.app` being unverified, you can unquarantine it using:

```sh
sudo xattr -rd com.apple.quarantine '/Applications/Wine Crossover.app'
```

## Linux

- Install [ninja](https://github.com/ninja-build/ninja/wiki/Pre-built-Ninja-packages).
- For non-x86(\_64) platforms: Install wine from your package manager.
  - For x86(\_64), [wibo](https://github.com/decompals/wibo), a minimal 32-bit Windows binary wrapper, will be automatically downloaded and used.

# Building

- Clone the repository:

  ```sh
  git clone https://github.com/joeyballentine/bfbb.git
  ```

- Using [Dolphin Emulator](https://dolphin-emu.org/), extract your game to `orig/GQPE78`.
  ![](assets/dolphin-extract.png)
  - To save space, the only necessary files are the following. Any others can be deleted.
    - `sys/main.dol`
- Configure:

  ```sh
  python configure.py
  ```

  To use a version other than `GQPE78` (NTSC-U), specify it with `--version`.

- Build:

  ```sh
  ninja
  ```

# Visual Studio Code

If desired, use the recommended Visual Studio Code settings by renaming the `.vscode.example` directory to `.vscode`.

# Diffing

Once the initial build succeeds, an `objdiff.json` should exist in the project root.

Download the latest release from [encounter/objdiff](https://github.com/encounter/objdiff). Under project settings, set `Project directory`. The configuration should be loaded automatically.

Select an object from the left sidebar to begin diffing. Changes to the project will rebuild automatically: changes to source files, headers, `configure.py`, `splits.txt` or `symbols.txt`.

![](assets/objdiff.png)
