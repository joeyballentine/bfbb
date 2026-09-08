# bfbb_config

`config.ini` with controls on it. Windows only.

Every setting the port has, grouped by section, each with the description the
generated file carries and the values it accepts. A value that is not the
default says what the default was. Save writes the file; Cancel writes nothing.

## Why a separate program

`config.ini` is read before the window opens. `assets.path` says where the
game's data is and `video.mode` says how to open the window at all, and neither
can be answered from inside a game that has already started on the old values.
So it runs before the game does, not inside it.

## Where the settings come from

`kConfigSettings` in `../iConfigTable.cpp`, and nowhere else. That table already
held each setting's default and its comment; it now also holds its kind, the
words it accepts and its numeric range. Adding a row there gives it a control
here, with its description and validation, and nothing in `main.cpp` has to be
told about it.

The table is split out of `iConfig.cpp` so this program does not link it.
`iConfig.cpp` reaches `iPadBind.cpp` for the two binding sections, and that
reaches `xPad.h` and the pad backend — most of the game, to read a file this
program reads itself.

## How it writes

`../iConfigEdit.cpp`, which holds the file as its own lines and rewrites only
the text between the `=` and any trailing comment. Comments, blank lines,
ordering, spacing around the `=`, hand-written sections, the commented-out
`[pad]` block and lines this build does not recognise all survive a save
exactly. A save that changes nothing changes no bytes. The write goes to a
temporary file and is renamed over the target, so an interrupted save cannot
leave half a `config.ini`.

A setting the file never mentioned and that nobody touched is not written out.
The game answers those from the table, and writing one would pin it to whatever
today's default happens to be.

Which file it edits is the order the game searches in: `BFBB_CONFIG`, then
`config.ini` in the working directory, then beside the executable. An argument
overrides all three. A missing file is written at the defaults, as the game
does it.

## Not yet

- `[pad]` and `[keyboard]`. Editing a binding wants a control that captures a
  button press, and the grammar takes `,` `+` and `!`, which is a second screen
  rather than a row.
- Live validation. A value is checked on Save, which reports the first one that
  will not do and shows it.

## Building

Part of the normal PC build; it lands in `bin/` beside `bfbb.exe`. The one
gotcha is `bfbb_config.rc`, which lives in `../res/` next to the icon it names:
`rc.exe` resolves a resource's file against the script's own directory and the
include path, and a script elsewhere naming an icon in `res/` compiles to
nothing and reports nothing. The manifest is a CMake source rather than a line
in the `.rc`, because CMake embeds a manifest of its own into every executable
and a second `RT_MANIFEST` in the resources is a duplicate-resource link error.
