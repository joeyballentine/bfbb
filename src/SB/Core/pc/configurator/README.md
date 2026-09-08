# bfbb_config

`config.ini` with controls on it.

Every setting the port has, grouped by section, each with the description the
generated file carries and the values it accepts. A value that is not the
default says what the default was. Save writes the file; Cancel writes nothing.

## A model and two windows

`config_model.cpp` is the program without a window on it: the settings as
editable values, what the file said about each, and the reading and writing of
that file. Nothing in it draws, measures or asks the user anything -- a call
that can fail fills in a sentence saying why and the caller decides how to show
it. `pc_selftest` links it and checks it, which nothing could do while it lived
inside a window ctest cannot run.

`main_wx.cpp` draws it in wxWidgets, which is the front end on every host. wx
wraps the platform's own controls rather than imitating them: Win32 common
controls on Windows, GTK on Linux, Cocoa on macOS.

`main_win32.cpp` draws it in Win32 controls directly. It predates the wx one
and is kept because it needs no dependency at all: `-DBFBB_CONFIG_UI=win32`
builds a configurator on a Windows machine with no wxWidgets anywhere, and that
is one `add_executable` with four sources.

The two are interchangeable and neither knows anything the other does not.
Adding a setting to `kConfigSettings` gives it a row in both.

## Why a separate program

`config.ini` is read before the window opens. `assets.path` says where the
game's data is and `video.mode` says how to open the window at all, and neither
can be answered from inside a game that has already started on the old values.
So it runs before the game does, not inside it.

## Where the settings come from

`kConfigSettings` in `../iConfigTable.cpp`, and nowhere else. That table already
held each setting's default and its comment; it now also holds its kind, the
words it accepts and its numeric range. Adding a row there gives it a control
here, with its description and validation, and nothing in the front end has to
be told about it.

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

Part of the normal PC build; it lands in `bin/` beside the game.

`BFBB_CONFIG_UI` picks the front end: `auto`, `wx`, `win32` or `off`. `auto`
takes wx wherever wxWidgets can be found, Win32 controls on a Windows host
where it cannot, and builds nothing anywhere else.

`BFBB_WX` says where wxWidgets comes from, on the same three words SDL uses:
`auto`, `system`, `vendored`. Off Windows an installed `libwxgtk` or a `brew
install wxwidgets` answers `auto` and `third_party/wxWidgets` is the fallback.
On Windows `auto` usually finds nothing and falls back to the Win32 front end
rather than starting a long compile nobody asked for; `-DBFBB_WX=vendored`
asks for the submodule.

Two Windows gotchas, both pre-dating the wx front end. `bfbb_config.rc` lives
in `../res/` next to the icon it names: `rc.exe` resolves a resource's file
against the script's own directory and the include path, and a script elsewhere
naming an icon in `res/` compiles to nothing and reports nothing. The manifest
is a CMake source rather than a line in the `.rc`, because CMake embeds a
manifest of its own into every executable and a second `RT_MANIFEST` in the
resources is a duplicate-resource link error.

One more, specific to wx. wx includes the C++ standard library and the game's
own sources never do, so building the wx front end holds the toolchain to a
standard the rest of this tree does not: on Windows, Microsoft's STL refuses
any Clang older than the version it shipped against, and a clang that compiles
the whole game will not compile `main_wx.cpp`. MSVC, MinGW, or a new enough
clang all work.
