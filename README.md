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
[Game Code]: https://img.shields.io/badge/Game%20Code-80.70%25-limegreen
[Game Fuzzy]: https://img.shields.io/badge/Game%20Close%20Match-99.22%25-yellowgreen
[Game Functions]: https://img.shields.io/badge/Game%20Functions-7250%20%2F%207673-lavender
[Overall]: https://img.shields.io/badge/Overall-64.64%25-darkgreen
[Overall Fuzzy]: https://img.shields.io/badge/Overall%20Close%20Match-79.09%25-olive
[Overall Functions]: https://img.shields.io/badge/Overall%20Functions-8420%20%2F%2010147-slateblue
[Build Status]: https://github.com/joeyballentine/bfbb/actions/workflows/build.yml/badge.svg
[actions]: https://github.com/joeyballentine/bfbb/actions/workflows/build.yml
[Discord Badge]: https://img.shields.io/discord/829152115322257436?color=%237289DA&logo=discord&logoColor=%23FFFFFF
[discord]: https://discord.gg/dVbGFdYU6A

A work-in-progress decompilation of SpongeBob SquarePants: Battle for Bikini Bottom.

It builds the following DOL:

main.dol: `sha1: 306526d90b48e99894c3138f5fc8f2716d9fecf6`

This repository does **not** contain any game assets or assembly whatsoever. An existing copy of the game is required.

Supported versions:

- `GQPE78`: (NTSC-U)

## About this fork

This is a fork of [bfbbdecomp/bfbb](https://github.com/bfbbdecomp/bfbb) where the
decompilation is driven by an LLM, as an experiment. All of the decomp work on the
`duplotron` branch was written by Claude. Claude used the existing tooling in the decomp repo as well as its own tooling. 
Most of the work was done completely autonomously, with minimal guidance to make sure it wasn't creating fakematches 
(at least, to the best of my ability). From what I can tell, the quality of the code it output is generally pretty good, 
and the process it used was basically looking at the asm/ghidra output, testing out various permutations of 
the c++ code in a scratchpad, and then applying the most-matching version to the repo. 

This was an experiment/proof of concept I did with my extra claude usage I wasn't going to use for anything else, so it didn't cost me any extra money.
Though much of the generated code seems pretty good, I would consider this mostly "slop", and  
will need to be carefully verified before merging to main. This was a "move-fast-break-things" approach to get something working, 
which worked for my personal purposes, but long-term we want the official decomp to be high quality.

This also uses a scheduler-patched CodeWarrior (`GC/2.0p1a`, produced at
configure time by `tools/patch_compiler.py`), which unblocks functions that
differ only by instruction scheduling. The patches were found by multiple Fable instances, 
and compared against a mwcc decomp to validate.

The work has gone almost entirely into game code rather than the SDK and
library code around it (the existing decomp project's goal), and that is what made the PC port on `treedome`
possible, since none of the non-game code was required to port to PC.

Slop warning: See [docs/DUPLOTRON.md](docs/DUPLOTRON.md) for what's been tried and ruled out. 

This fork still contains the matching gate that the original uses to build a matching .dol file. Though, it is also completely linkable to a working non-matching .dol file that can be played (with some bugs).

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
